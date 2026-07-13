/*
 * Nuclei uDMA interface
 *
 * Copyright (c) 2026 Nuclei.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "block/aio.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/dma/nuclei_udma.h"
#include "sysemu/dma.h"

static void nuclei_udma_kick_m2m(NucleiUDMAState *s);
static void nuclei_udma_run_m2m_bh(void *opaque);
static void nuclei_udma_update_irq(NucleiUDMAState *s);
static bool nuclei_udma_validate_m2m_start(NucleiUDMAState *s,
                                           uint32_t channel);
static uint32_t nuclei_udma_compose_pam_ctrl(const NucleiUDMAPAChannel *ch);
static void nuclei_udma_prime_pam_channel(NucleiUDMAState *s, uint32_t channel);
static void nuclei_udma_stop_pam_channel(NucleiUDMAState *s, uint32_t channel,
                                         bool clear_enable);

static uint64_t nuclei_udma_addr_mask(const NucleiUDMAState *s)
{
    return MAKE_64BIT_MASK(0, s->addr_width);
}

static uint64_t nuclei_udma_compose_addr(const NucleiUDMAState *s,
                                         uint32_t low, uint32_t high)
{
    uint64_t addr = ((uint64_t)(high & ADDR_H_MASK) << 32) | low;

    return addr & nuclei_udma_addr_mask(s);
}

static uint32_t nuclei_udma_extract_field(uint32_t value, uint32_t mask,
                                          unsigned shift)
{
    return (value & mask) >> shift;
}

static uint32_t nuclei_udma_m2m_mode_from_ctrl(uint32_t ctrl)
{
    return nuclei_udma_extract_field(ctrl, MCTRL_TRANS_MODE_MASK,
                                     MCTRL_TRANS_MODE_SHIFT);
}

static uint32_t nuclei_udma_m2m_mode(const NucleiUDMAM2MChannel *ch)
{
    return nuclei_udma_m2m_mode_from_ctrl(ch->run_ctrl);
}

static bool nuclei_udma_width_code_to_bytes(uint32_t width_code,
                                            uint32_t *width_bytes)
{
    switch (width_code) {
    case 0:
        *width_bytes = 1;
        return true;
    case 1:
        *width_bytes = 2;
        return true;
    case 2:
        *width_bytes = 4;
        return true;
    case 3:
        *width_bytes = 8;
        return true;
    case 4:
        *width_bytes = 16;
        return true;
    default:
        return false;
    }
}

static bool nuclei_udma_pam_width_code_to_bytes(uint32_t width_code,
                                                uint32_t *width_bytes)
{
    return nuclei_udma_width_code_to_bytes(width_code, width_bytes) &&
           *width_bytes <= 4;
}

static uint32_t nuclei_udma_pam_req_queue_capacity(const NucleiUDMAState *s)
{
    return MIN(MAX(1u, s->pamem_request_queue_depth),
               NUCLEI_UDMA_PAM_MAX_REQ_QUEUE_DEPTH);
}

static void nuclei_udma_pam_clear_request_state(NucleiUDMAPAChannel *ch)
{
    memset(ch->req_queue, 0, sizeof(ch->req_queue));
    ch->req_queue_head = 0;
    ch->req_queue_tail = 0;
    ch->req_queue_count = 0;
    ch->req_pending_beats = 0;
    ch->req_next_seq = 0;
}

static bool nuclei_udma_pam_req_queue_empty(const NucleiUDMAPAChannel *ch)
{
    return ch->req_queue_count == 0;
}

static bool nuclei_udma_pam_req_queue_full(const NucleiUDMAState *s,
                                           const NucleiUDMAPAChannel *ch)
{
    return ch->req_queue_count >= nuclei_udma_pam_req_queue_capacity(s);
}

static NucleiUDMAPARequest *nuclei_udma_pam_req_queue_head(
    NucleiUDMAPAChannel *ch)
{
    if (nuclei_udma_pam_req_queue_empty(ch)) {
        return NULL;
    }

    return &ch->req_queue[ch->req_queue_head];
}

static const NucleiUDMAPARequest *nuclei_udma_pam_req_queue_peek(
    const NucleiUDMAPAChannel *ch)
{
    if (nuclei_udma_pam_req_queue_empty(ch)) {
        return NULL;
    }

    return &ch->req_queue[ch->req_queue_head];
}

static bool nuclei_udma_pam_req_direction_compatible(
    const NucleiUDMAPAChannel *ch, NucleiUDMAPADirection direction)
{
    const NucleiUDMAPARequest *req = nuclei_udma_pam_req_queue_peek(ch);

    return !req || req->direction == direction;
}

static uint32_t nuclei_udma_pam_available_request_beats(
    const NucleiUDMAPAChannel *ch, uint32_t width_bytes)
{
    uint32_t total_beats = ch->cur_tsize / width_bytes;

    if (total_beats <= ch->req_pending_beats) {
        return 0;
    }

    return total_beats - ch->req_pending_beats;
}

static bool nuclei_udma_pam_req_queue_push(NucleiUDMAState *s,
                                           NucleiUDMAPAChannel *ch,
                                           NucleiUDMAPADirection direction,
                                           uint32_t req_beats)
{
    uint32_t capacity = nuclei_udma_pam_req_queue_capacity(s);
    NucleiUDMAPARequest *req;

    if (nuclei_udma_pam_req_queue_full(s, ch) || req_beats == 0) {
        return false;
    }

    /*
     * This is a logical requester queue for pa-mem. It intentionally does
     * not try to recreate cycle-accurate bus transaction overlap or reorder.
     */
    req = &ch->req_queue[ch->req_queue_tail];
    req->direction = direction;
    req->req_len_beats = req_beats;
    req->remaining_beats = req_beats;
    req->seq = ch->req_next_seq++;

    ch->req_queue_tail = (ch->req_queue_tail + 1) % capacity;
    ch->req_queue_count++;
    ch->req_pending_beats += req_beats;
    return true;
}

static void nuclei_udma_pam_req_queue_pop_head(NucleiUDMAState *s,
                                               NucleiUDMAPAChannel *ch)
{
    uint32_t capacity = nuclei_udma_pam_req_queue_capacity(s);
    NucleiUDMAPARequest *req = nuclei_udma_pam_req_queue_head(ch);

    if (!req) {
        return;
    }

    ch->req_pending_beats -= req->remaining_beats;
    memset(req, 0, sizeof(*req));
    ch->req_queue_head = (ch->req_queue_head + 1) % capacity;
    ch->req_queue_count--;

    if (ch->req_queue_count == 0) {
        ch->req_queue_head = 0;
        ch->req_queue_tail = 0;
    }
}

static void nuclei_udma_raise_pam_irq(NucleiUDMAState *s, uint32_t channel,
                                      uint32_t irq_mask)
{
    s->pamem[channel].irq_stat |= irq_mask & PAM_IRQ_MASK;
    nuclei_udma_update_irq(s);
}

static void nuclei_udma_prime_pam_channel(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    ch->base_src_addr = nuclei_udma_compose_addr(s, ch->src_addr,
                                                 ch->src_addr_hi);
    ch->base_dst_addr = nuclei_udma_compose_addr(s, ch->dst_addr,
                                                 ch->dst_addr_hi);
    ch->cur_src_addr = ch->base_src_addr;
    ch->cur_dst_addr = ch->base_dst_addr;
    ch->initial_tsize = ch->size & MSIZE_TSIZE_MASK;
    ch->cur_tsize = ch->initial_tsize;
    ch->size = ch->cur_tsize;
    ch->active = true;
    ch->error = false;
    ch->half_irq_fired = false;
    nuclei_udma_pam_clear_request_state(ch);
}

static void nuclei_udma_stop_pam_channel(NucleiUDMAState *s, uint32_t channel,
                                         bool clear_enable)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    ch->active = false;
    ch->error = false;
    ch->half_irq_fired = false;
    nuclei_udma_pam_clear_request_state(ch);
    if (clear_enable) {
        ch->ctrl &= ~PAM_CTRL_TRANS_EN;
    }
}

static void nuclei_udma_reload_pam_channel(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    ch->cur_src_addr = ch->base_src_addr;
    ch->cur_dst_addr = ch->base_dst_addr;
    ch->cur_tsize = ch->initial_tsize;
    ch->size = ch->cur_tsize;
    ch->half_irq_fired = false;
    ch->active = true;
    nuclei_udma_pam_clear_request_state(ch);
}

static bool nuclei_udma_pam_direction_ready(const NucleiUDMAPAChannel *ch,
                                            NucleiUDMAPADirection direction)
{
    const NucleiUDMAPARequest *req = nuclei_udma_pam_req_queue_peek(ch);

    return req && req->direction == direction && req->remaining_beats != 0 &&
           ch->active && ch->cur_tsize != 0;
}

static void nuclei_udma_complete_pam_beat(NucleiUDMAState *s, uint32_t channel,
                                          uint32_t width_bytes)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];
    NucleiUDMAPARequest *req = nuclei_udma_pam_req_queue_head(ch);
    uint32_t mode = nuclei_udma_extract_field(ch->ctrl,
                                              PAM_CTRL_TRANS_MODE_MASK,
                                              PAM_CTRL_TRANS_MODE_SHIFT);

    if (!req || req->remaining_beats == 0 || ch->req_pending_beats == 0) {
        return;
    }

    req->remaining_beats--;
    ch->req_pending_beats--;
    ch->cur_tsize -= width_bytes;
    ch->size = ch->cur_tsize;

    if (req->remaining_beats == 0) {
        nuclei_udma_pam_req_queue_pop_head(s, ch);
    }

    if (!ch->half_irq_fired && ch->cur_tsize <= (ch->initial_tsize / 2)) {
        ch->half_irq_fired = true;
        nuclei_udma_raise_pam_irq(s, channel, PAM_IRQ_HTRANS);
    }

    if (ch->cur_tsize != 0) {
        return;
    }

    nuclei_udma_raise_pam_irq(s, channel, PAM_IRQ_FTRANS);

    if (mode == PAM_TRANS_MODE_CONTINUOUS && (ch->ctrl & PAM_CTRL_TRANS_EN)) {
        nuclei_udma_reload_pam_channel(s, channel);
        return;
    }

    nuclei_udma_stop_pam_channel(s, channel, true);
}

static void nuclei_udma_fail_pam_channel(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    ch->error = true;
    nuclei_udma_pam_clear_request_state(ch);
    nuclei_udma_raise_pam_irq(s, channel, PAM_IRQ_RSP_ERR);
    nuclei_udma_stop_pam_channel(s, channel, true);
}

static void nuclei_udma_raise_m2m_irq(NucleiUDMAState *s, uint32_t channel,
                                      uint32_t irq_mask)
{
    s->m2m[channel].irq_stat |= irq_mask & M2M_IRQ_MASK;
    nuclei_udma_update_irq(s);
}

static uint64_t nuclei_udma_adjust_addr(const NucleiUDMAState *s, uint64_t addr,
                                        uint32_t delta, bool subtract)
{
    if (subtract) {
        addr -= delta;
    } else {
        addr += delta;
    }

    return addr & nuclei_udma_addr_mask(s);
}

static void nuclei_udma_prime_m2m_channel(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];
    uint32_t mode;

    ch->run_ctrl = ch->ctrl;
    ch->run_rpt = ch->rpt;
    ch->run_rau = ch->rau;
    ch->base_src_addr = nuclei_udma_compose_addr(s, ch->src_addr,
                                                 ch->src_addr_hi);
    ch->base_dst_addr = nuclei_udma_compose_addr(s, ch->dst_addr,
                                                 ch->dst_addr_hi);
    ch->cur_src_addr = ch->base_src_addr;
    ch->cur_dst_addr = ch->base_dst_addr;
    ch->cur_lla_addr = nuclei_udma_compose_addr(s, ch->lla, ch->lla_hi);
    ch->initial_tsize = ch->size & MSIZE_TSIZE_MASK;
    ch->cur_tsize = ch->initial_tsize;
    mode = nuclei_udma_m2m_mode(ch);
    ch->repeat_left = (mode == M2M_TRANS_MODE_REPEAT) ?
                      (ch->run_rpt & MRPT_TRANS_RPT_MASK) : 0;
    ch->size = ch->cur_tsize;
    ch->error = false;
    ch->half_irq_fired = false;
    ch->repeat_pause_armed = (mode == M2M_TRANS_MODE_REPEAT) &&
                             !(ch->run_rpt & MRPT_RPT_IRQ_EN);
    ch->repeat_wait_irq_clear = false;
    ch->stop_pending = false;
}

static void nuclei_udma_stop_m2m_channel(NucleiUDMAState *s, uint32_t channel,
                                         bool clear_enable)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    ch->active = false;
    ch->queued = false;
    ch->linked_list_active = false;
    ch->repeat_pause_armed = false;
    ch->repeat_wait_irq_clear = false;
    ch->stop_pending = false;
    if (clear_enable) {
        ch->ctrl &= ~MCTRL_TRANS_EN;
    }
}

static bool nuclei_udma_m2m_is_final_repeat_iteration(
    const NucleiUDMAM2MChannel *ch)
{
    return ch->repeat_left <= 1;
}

static bool nuclei_udma_m2m_should_raise_transfer_irq(
    const NucleiUDMAM2MChannel *ch)
{
    if (nuclei_udma_m2m_mode(ch) != M2M_TRANS_MODE_REPEAT) {
        return true;
    }

    if (!(ch->run_rpt & MRPT_RPT_IRQ_EN)) {
        return true;
    }

    return nuclei_udma_m2m_is_final_repeat_iteration(ch);
}

static void nuclei_udma_complete_m2m_channel(NucleiUDMAState *s,
                                             uint32_t channel,
                                             bool clear_enable,
                                             bool raise_full_irq)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    ch->cur_tsize = 0;
    ch->size = 0;
    nuclei_udma_stop_m2m_channel(s, channel, clear_enable);

    if (raise_full_irq) {
        nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_FTRANS);
    }
}

static void nuclei_udma_fail_m2m_channel(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    ch->error = true;
    nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_RSP_ERR);
    nuclei_udma_stop_m2m_channel(s, channel, true);
}

static void nuclei_udma_fail_m2m_lla_channel(NucleiUDMAState *s,
                                             uint32_t channel,
                                             const char *reason)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: channel %u linked-list load failed (%s) at 0x%"
                  PRIx64 "\n",
                  __func__, channel, reason, ch->cur_lla_addr);
    ch->error = true;
    nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_LLA_ERR);
    nuclei_udma_stop_m2m_channel(s, channel, true);
}

static void nuclei_udma_reload_continuous_m2m_channel(NucleiUDMAState *s,
                                                      uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    ch->cur_src_addr = ch->base_src_addr;
    ch->cur_dst_addr = ch->base_dst_addr;
    ch->cur_tsize = ch->initial_tsize;
    ch->size = ch->cur_tsize;
    ch->half_irq_fired = false;
    ch->active = true;
    ch->queued = false;
}

static bool nuclei_udma_advance_repeat_m2m_channel(NucleiUDMAState *s,
                                                   uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];
    uint32_t src_delta;
    uint32_t dst_delta;

    if (nuclei_udma_m2m_is_final_repeat_iteration(ch)) {
        return false;
    }

    src_delta = ch->run_rau & MRAU_RSAU_MASK;
    dst_delta = nuclei_udma_extract_field(ch->run_rau, MRAU_RDAU_MASK,
                                          MRAU_RDAU_SHIFT);
    ch->repeat_left--;
    ch->cur_src_addr = nuclei_udma_adjust_addr(s, ch->cur_src_addr, src_delta,
                                               !!(ch->run_rpt & MRPT_RPT_SAUM));
    ch->cur_dst_addr = nuclei_udma_adjust_addr(s, ch->cur_dst_addr, dst_delta,
                                               !!(ch->run_rpt & MRPT_RPT_DAUM));
    ch->cur_tsize = ch->initial_tsize;
    ch->size = ch->cur_tsize;
    ch->half_irq_fired = false;
    ch->active = true;
    ch->queued = false;

    return true;
}

static NucleiUDMALLAResult nuclei_udma_load_m2m_lla_descriptor(
    NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];
    uint32_t desc[M2M_LLA_DESC_WORDS];
    MemTxResult result;
    uint32_t mode;
    size_t i;

    if (ch->cur_lla_addr & 0x3) {
        nuclei_udma_fail_m2m_lla_channel(s, channel, "unaligned descriptor");
        return NUCLEI_UDMA_LLA_ERROR;
    }

    result = dma_memory_read(s->dma_as, ch->cur_lla_addr, desc,
                             M2M_LLA_DESC_SIZE, MEMTXATTRS_UNSPECIFIED);
    if (result != MEMTX_OK) {
        nuclei_udma_fail_m2m_lla_channel(s, channel, "memory read error");
        return NUCLEI_UDMA_LLA_ERROR;
    }

    for (i = 0; i < ARRAY_SIZE(desc); i++) {
        desc[i] = le32_to_cpu(desc[i]);
    }

    ch->src_addr = desc[M2M_LLA_DESC_SRCADDR];
    ch->dst_addr = desc[M2M_LLA_DESC_DSTADDR];
    ch->ctrl = desc[M2M_LLA_DESC_MCTRL] & MCTRL_WRITABLE_MASK;
    ch->rpt = desc[M2M_LLA_DESC_MRPT] & MRPT_WRITABLE_MASK;
    ch->size = desc[M2M_LLA_DESC_MSIZE] & MSIZE_TSIZE_MASK;
    ch->rau = desc[M2M_LLA_DESC_MRAU];
    ch->lla = desc[M2M_LLA_DESC_MLLA];
    ch->src_addr_hi = desc[M2M_LLA_DESC_SRCADDR_H] & ADDR_H_MASK;
    ch->dst_addr_hi = desc[M2M_LLA_DESC_DSTADDR_H] & ADDR_H_MASK;
    ch->lla_hi = desc[M2M_LLA_DESC_MLLA_H] & ADDR_H_MASK;

    if (!(ch->ctrl & MCTRL_TRANS_EN)) {
        nuclei_udma_fail_m2m_lla_channel(s, channel, "descriptor disabled");
        return NUCLEI_UDMA_LLA_ERROR;
    }

    mode = nuclei_udma_m2m_mode_from_ctrl(ch->ctrl);
    if (mode == M2M_TRANS_MODE_CONTINUOUS) {
        nuclei_udma_stop_m2m_channel(s, channel, true);
        return NUCLEI_UDMA_LLA_STOP;
    }

    if (!nuclei_udma_validate_m2m_start(s, channel)) {
        nuclei_udma_fail_m2m_lla_channel(s, channel,
                                         "invalid descriptor configuration");
        return NUCLEI_UDMA_LLA_ERROR;
    }

    nuclei_udma_prime_m2m_channel(s, channel);
    ch->active = true;
    ch->queued = false;
    ch->linked_list_active = true;

    return NUCLEI_UDMA_LLA_OK;
}

static bool nuclei_udma_validate_m2m_start(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];
    uint32_t src_width_code;
    uint32_t dst_width_code;
    uint32_t src_width_bytes;
    uint32_t dst_width_bytes;
    uint64_t src_addr;
    uint64_t dst_addr;
    uint32_t mode;

    mode = nuclei_udma_m2m_mode_from_ctrl(ch->ctrl);
    if (mode == 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %u has reserved mem2mem mode %u\n",
                      __func__, channel, mode);
        return false;
    }

    if ((ch->size & MSIZE_TSIZE_MASK) == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %u cannot start with TSIZE=0\n",
                      __func__, channel);
        return false;
    }

    src_width_code = nuclei_udma_extract_field(ch->ctrl, MCTRL_MSWIDTH_MASK,
                                               MCTRL_MSWIDTH_SHIFT);
    dst_width_code = nuclei_udma_extract_field(ch->ctrl, MCTRL_MDWIDTH_MASK,
                                               MCTRL_MDWIDTH_SHIFT);
    if (!nuclei_udma_width_code_to_bytes(src_width_code, &src_width_bytes) ||
        !nuclei_udma_width_code_to_bytes(dst_width_code, &dst_width_bytes)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %u has unsupported width encoding "
                      "(src=%u dst=%u)\n",
                      __func__, channel, src_width_code, dst_width_code);
        return false;
    }
    if (src_width_bytes != dst_width_bytes) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %u currently requires matching src/dst "
                      "widths (%u vs %u bytes)\n",
                      __func__, channel, src_width_bytes, dst_width_bytes);
        return false;
    }
    if ((ch->size & MSIZE_TSIZE_MASK) % src_width_bytes) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %u TSIZE=0x%x is not aligned to width %u\n",
                      __func__, channel, ch->size, src_width_bytes);
        return false;
    }

    if (mode == M2M_TRANS_MODE_REPEAT &&
        (ch->rpt & MRPT_TRANS_RPT_MASK) == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %u cannot start repeat mode with "
                      "TRANS_RPT=0\n",
                      __func__, channel);
        return false;
    }

    src_addr = nuclei_udma_compose_addr(s, ch->src_addr, ch->src_addr_hi);
    dst_addr = nuclei_udma_compose_addr(s, ch->dst_addr, ch->dst_addr_hi);
    if ((src_addr % src_width_bytes) || (dst_addr % dst_width_bytes)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %u start address misalignment "
                      "(src=0x%" PRIx64 " dst=0x%" PRIx64 " width=%u)\n",
                      __func__, channel, src_addr, dst_addr, src_width_bytes);
        return false;
    }

    return true;
}

static bool nuclei_udma_validate_pam_start(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];
    uint32_t width_code;
    uint32_t width_bytes;
    uint64_t src_addr;
    uint64_t dst_addr;
    uint32_t mode;

    mode = nuclei_udma_extract_field(ch->ctrl, PAM_CTRL_TRANS_MODE_MASK,
                                     PAM_CTRL_TRANS_MODE_SHIFT);
    if (mode > PAM_TRANS_MODE_CONTINUOUS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: pa-mem channel %u has reserved mode %u\n",
                      __func__, channel, mode);
        return false;
    }

    if ((ch->size & MSIZE_TSIZE_MASK) == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: pa-mem channel %u cannot start with TSIZE=0\n",
                      __func__, channel);
        return false;
    }

    width_code = nuclei_udma_extract_field(ch->ctrl, PAM_CTRL_MWIDTH_MASK,
                                           PAM_CTRL_MWIDTH_SHIFT);
    if (!nuclei_udma_pam_width_code_to_bytes(width_code, &width_bytes)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: pa-mem channel %u has unsupported MWIDTH=%u\n",
                      __func__, channel, width_code);
        return false;
    }

    if ((ch->size & MSIZE_TSIZE_MASK) % width_bytes) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: pa-mem channel %u TSIZE=0x%x is not aligned to "
                      "MWIDTH=%u\n",
                      __func__, channel, ch->size, width_bytes);
        return false;
    }

    src_addr = nuclei_udma_compose_addr(s, ch->src_addr, ch->src_addr_hi);
    dst_addr = nuclei_udma_compose_addr(s, ch->dst_addr, ch->dst_addr_hi);
    if ((src_addr % width_bytes) || (dst_addr % width_bytes)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: pa-mem channel %u start address misalignment "
                      "(src=0x%" PRIx64 " dst=0x%" PRIx64 " width=%u)\n",
                      __func__, channel, src_addr, dst_addr, width_bytes);
        return false;
    }

    return true;
}

static void nuclei_udma_start_m2m_channel(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];
    uint32_t mode;

    if (!nuclei_udma_validate_m2m_start(s, channel)) {
        ch->ctrl &= ~MCTRL_TRANS_EN;
        ch->active = false;
        ch->queued = false;
        ch->linked_list_active = false;
        ch->stop_pending = false;
        return;
    }

    nuclei_udma_prime_m2m_channel(s, channel);
    mode = nuclei_udma_m2m_mode(ch);
    ch->active = false;
    ch->queued = true;
    ch->linked_list_active = (mode != M2M_TRANS_MODE_CONTINUOUS) &&
                             (ch->cur_lla_addr != 0);

    nuclei_udma_kick_m2m(s);
}

static void nuclei_udma_disable_m2m_channel(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    if (ch->active) {
        ch->stop_pending = true;
        return;
    }

    nuclei_udma_stop_m2m_channel(s, channel, false);
}

static int nuclei_udma_select_m2m_channel(NucleiUDMAState *s)
{
    int best = -1;
    uint32_t best_prio = 0;
    uint32_t ch;

    for (ch = 0; ch < s->m2m_channels; ch++) {
        NucleiUDMAM2MChannel *chan = &s->m2m[ch];
        bool runnable = (chan->active || chan->queued) && chan->cur_tsize;

        if (!runnable) {
            continue;
        }

        if (chan->repeat_wait_irq_clear) {
            continue;
        }

        if (!chan->active && !(chan->ctrl & MCTRL_TRANS_EN)) {
            continue;
        }

        if (best < 0) {
            best = ch;
            best_prio = nuclei_udma_extract_field(chan->run_ctrl,
                                                  MCTRL_PRIORITY_MASK,
                                                  MCTRL_PRIORITY_SHIFT);
            continue;
        }

        if (nuclei_udma_extract_field(chan->run_ctrl, MCTRL_PRIORITY_MASK,
                                      MCTRL_PRIORITY_SHIFT) > best_prio) {
            best = ch;
            best_prio = nuclei_udma_extract_field(chan->run_ctrl,
                                                  MCTRL_PRIORITY_MASK,
                                                  MCTRL_PRIORITY_SHIFT);
        }
    }

    return best;
}

static void nuclei_udma_kick_m2m(NucleiUDMAState *s)
{
    if (s->m2m_bh) {
        qemu_bh_schedule(s->m2m_bh);
    }
}

static void nuclei_udma_run_m2m_burst(NucleiUDMAState *s, uint32_t channel)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];
    uint32_t src_width_code;
    uint32_t dst_width_code;
    uint32_t src_width_bytes;
    uint32_t dst_width_bytes;
    uint32_t burst_beats;
    uint32_t step_bytes;
    uint32_t processed = 0;
    uint32_t mode;
    bool src_fixed;
    bool dst_fixed;

    mode = nuclei_udma_m2m_mode(ch);
    src_width_code = nuclei_udma_extract_field(ch->run_ctrl, MCTRL_MSWIDTH_MASK,
                                               MCTRL_MSWIDTH_SHIFT);
    dst_width_code = nuclei_udma_extract_field(ch->run_ctrl, MCTRL_MDWIDTH_MASK,
                                               MCTRL_MDWIDTH_SHIFT);
    if (!nuclei_udma_width_code_to_bytes(src_width_code, &src_width_bytes) ||
        !nuclei_udma_width_code_to_bytes(dst_width_code, &dst_width_bytes) ||
        src_width_bytes != dst_width_bytes) {
        nuclei_udma_fail_m2m_channel(s, channel);
        return;
    }

    burst_beats = MIN(nuclei_udma_extract_field(ch->run_ctrl,
                                                MCTRL_MDBURST_MASK,
                                                MCTRL_MDBURST_SHIFT),
                      nuclei_udma_extract_field(ch->run_ctrl,
                                                MCTRL_MSBURST_MASK,
                                                MCTRL_MSBURST_SHIFT)) + 1;
    step_bytes = MIN(ch->cur_tsize, burst_beats * src_width_bytes);
    src_fixed = !!(ch->run_ctrl & MCTRL_MSNA);
    dst_fixed = !!(ch->run_ctrl & MCTRL_MDNA);

    while (processed < step_bytes) {
        uint8_t beat_buf[16];
        MemTxResult result;

        result = dma_memory_read(s->dma_as, ch->cur_src_addr, beat_buf,
                                 src_width_bytes, MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            nuclei_udma_fail_m2m_channel(s, channel);
            return;
        }

        result = dma_memory_write(s->dma_as, ch->cur_dst_addr, beat_buf,
                                  dst_width_bytes, MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            nuclei_udma_fail_m2m_channel(s, channel);
            return;
        }

        processed += src_width_bytes;
        ch->cur_tsize -= src_width_bytes;
        ch->size = ch->cur_tsize;

        if (!src_fixed) {
            ch->cur_src_addr += src_width_bytes;
        }
        if (!dst_fixed) {
            ch->cur_dst_addr += dst_width_bytes;
        }

        if (!ch->half_irq_fired &&
            ch->cur_tsize <= (ch->initial_tsize / 2)) {
            ch->half_irq_fired = true;
            if (nuclei_udma_m2m_should_raise_transfer_irq(ch)) {
                nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_HTRANS);
            }
        }
    }

    if (ch->cur_tsize == 0) {
        bool stop_requested = ch->stop_pending || !(ch->ctrl & MCTRL_TRANS_EN);
        bool raise_full_irq = nuclei_udma_m2m_should_raise_transfer_irq(ch);
        bool linked_list_done = ch->linked_list_active && !stop_requested &&
                                ch->cur_lla_addr == 0;

        if (mode == M2M_TRANS_MODE_CONTINUOUS && !stop_requested) {
            nuclei_udma_complete_m2m_channel(s, channel, false,
                                             raise_full_irq);
            nuclei_udma_reload_continuous_m2m_channel(s, channel);
            nuclei_udma_kick_m2m(s);
            return;
        }

        if (mode == M2M_TRANS_MODE_REPEAT && !stop_requested &&
            nuclei_udma_advance_repeat_m2m_channel(s, channel)) {
            if (raise_full_irq) {
                nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_FTRANS);
            }
            if (ch->repeat_pause_armed) {
                ch->repeat_pause_armed = false;
                ch->repeat_wait_irq_clear = true;
                return;
            }
            nuclei_udma_kick_m2m(s);
            return;
        }

        if (!stop_requested && mode != M2M_TRANS_MODE_CONTINUOUS &&
            ch->cur_lla_addr != 0) {
            NucleiUDMALLAResult lla_result;

            if (raise_full_irq) {
                nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_FTRANS);
            }

            lla_result = nuclei_udma_load_m2m_lla_descriptor(s, channel);
            if (lla_result == NUCLEI_UDMA_LLA_OK) {
                nuclei_udma_kick_m2m(s);
            } else if (lla_result == NUCLEI_UDMA_LLA_STOP) {
                nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_LLA_FTRANS);
            }
            return;
        }

        nuclei_udma_complete_m2m_channel(s, channel, !stop_requested,
                                         raise_full_irq);
        if (linked_list_done) {
            nuclei_udma_raise_m2m_irq(s, channel, M2M_IRQ_LLA_FTRANS);
        }
        return;
    }

    if (mode == M2M_TRANS_MODE_SINGLE &&
        (ch->stop_pending || !(ch->ctrl & MCTRL_TRANS_EN))) {
        nuclei_udma_stop_m2m_channel(s, channel, false);
        return;
    }

    nuclei_udma_kick_m2m(s);
}

static void nuclei_udma_run_m2m_bh(void *opaque)
{
    NucleiUDMAState *s = opaque;
    int selected;
    uint32_t ch;

    selected = nuclei_udma_select_m2m_channel(s);
    if (selected < 0) {
        return;
    }

    for (ch = 0; ch < s->m2m_channels; ch++) {
        NucleiUDMAM2MChannel *chan = &s->m2m[ch];

        if (ch == selected) {
            chan->active = true;
            chan->queued = false;
        } else if (chan->active) {
            chan->active = false;
            if (chan->cur_tsize &&
                ((chan->ctrl & MCTRL_TRANS_EN) || chan->stop_pending)) {
                chan->queued = true;
            }
        }
    }

    nuclei_udma_run_m2m_burst(s, selected);
}

static uint32_t nuclei_udma_m2m_irq_chal_stat(const NucleiUDMAState *s)
{
    uint32_t val = 0;
    uint32_t ch;

    for (ch = 0; ch < s->m2m_channels; ch++) {
        if (s->m2m[ch].irq_stat & M2M_IRQ_MASK) {
            val |= BIT(ch);
        }
    }

    return val;
}

static uint32_t nuclei_udma_pam_irq_chal_stat(const NucleiUDMAState *s)
{
    uint32_t val = 0;
    uint32_t ch;

    for (ch = 0; ch < s->pamem_channels; ch++) {
        if (s->pamem[ch].irq_stat & PAM_IRQ_MASK) {
            val |= BIT(ch);
        }
    }

    return val;
}

static void nuclei_udma_update_irq(NucleiUDMAState *s)
{
    bool pending = false;
    uint32_t ch;

    for (ch = 0; ch < s->m2m_channels; ch++) {
        if (s->m2m[ch].irq_stat & s->m2m[ch].irq_en & M2M_IRQ_MASK) {
            pending = true;
            break;
        }
    }

    for (ch = 0; !pending && ch < s->pamem_channels; ch++) {
        if (s->pamem[ch].irq_stat & s->pamem[ch].irq_en & PAM_IRQ_MASK) {
            pending = true;
        }
    }

    qemu_set_irq(s->irq, pending);
}

static bool nuclei_udma_m2m_cfg_decode(hwaddr offset, uint32_t *channel,
                                       hwaddr *regoff)
{
    hwaddr rel;

    if (offset < DMA_M2M_CFG_BASE ||
        offset >= DMA_M2M_CFG_BASE +
                  DMA_M2M_CH_STRIDE * NUCLEI_UDMA_MAX_M2M_CHANNELS) {
        return false;
    }

    rel = offset - DMA_M2M_CFG_BASE;
    *channel = rel / DMA_M2M_CH_STRIDE;
    *regoff = rel % DMA_M2M_CH_STRIDE;

    return true;
}

static bool nuclei_udma_m2m_irq_decode(hwaddr offset, uint32_t *channel,
                                       hwaddr *regoff)
{
    hwaddr rel;

    if (offset < DMA_M2M_IRQ_BASE ||
        offset >= DMA_M2M_IRQ_BASE +
                  DMA_M2M_IRQ_CH_STRIDE * NUCLEI_UDMA_MAX_M2M_CHANNELS) {
        return false;
    }

    rel = offset - DMA_M2M_IRQ_BASE;
    *channel = rel / DMA_M2M_IRQ_CH_STRIDE;
    *regoff = rel % DMA_M2M_IRQ_CH_STRIDE;

    return true;
}

static bool nuclei_udma_pam_cfg_decode(hwaddr offset, uint32_t *channel,
                                       hwaddr *regoff)
{
    hwaddr rel;

    if (offset < DMA_PAM_CFG_BASE ||
        offset >= DMA_PAM_CFG_BASE +
                  DMA_PAM_CH_STRIDE * NUCLEI_UDMA_MAX_PAMEM_CHANNELS) {
        return false;
    }

    rel = offset - DMA_PAM_CFG_BASE;
    *channel = rel / DMA_PAM_CH_STRIDE;
    *regoff = rel % DMA_PAM_CH_STRIDE;

    return true;
}

static bool nuclei_udma_pam_irq_decode(hwaddr offset, uint32_t *channel,
                                       hwaddr *regoff)
{
    hwaddr rel;

    if (offset < DMA_PAM_IRQ_BASE ||
        offset >= DMA_PAM_IRQ_BASE +
                  DMA_PAM_IRQ_CH_STRIDE * NUCLEI_UDMA_MAX_PAMEM_CHANNELS) {
        return false;
    }

    rel = offset - DMA_PAM_IRQ_BASE;
    *channel = rel / DMA_PAM_IRQ_CH_STRIDE;
    *regoff = rel % DMA_PAM_IRQ_CH_STRIDE;

    return true;
}

static uint32_t nuclei_udma_compose_mctrl(const NucleiUDMAM2MChannel *ch)
{
    uint32_t val = ch->ctrl & ~MCTRL_TRANS_STAT;

    if (ch->active) {
        val |= MCTRL_TRANS_STAT;
    }

    return val;
}

static uint32_t nuclei_udma_compose_pam_ctrl(const NucleiUDMAPAChannel *ch)
{
    uint32_t val = ch->ctrl & ~PAM_CTRL_TRANS_STAT;

    if (ch->active) {
        val |= PAM_CTRL_TRANS_STAT;
    }

    return val;
}

static uint32_t nuclei_udma_read_m2m_cfg(NucleiUDMAState *s, uint32_t channel,
                                         hwaddr regoff)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    switch (regoff) {
    case DMA_M2M_CFG_MSRCADDR:
        return ch->src_addr;
    case DMA_M2M_CFG_MDSTADDR:
        return ch->dst_addr;
    case DMA_M2M_CFG_MCTRL:
        return nuclei_udma_compose_mctrl(ch);
    case DMA_M2M_CFG_MRPT:
        return ch->rpt;
    case DMA_M2M_CFG_MSIZE:
        return ch->size;
    case DMA_M2M_CFG_MSRCADDR_H:
        return ch->src_addr_hi;
    case DMA_M2M_CFG_MDSTADDR_H:
        return ch->dst_addr_hi;
    case DMA_M2M_CFG_MRAU:
        return ch->rau;
    case DMA_M2M_CFG_MLLA:
        return ch->lla;
    case DMA_M2M_CFG_MLLA_H:
        return ch->lla_hi;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid m2m cfg register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        return 0;
    }
}

static void nuclei_udma_write_m2m_cfg(NucleiUDMAState *s, uint32_t channel,
                                      hwaddr regoff, uint32_t value)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    switch (regoff) {
    case DMA_M2M_CFG_MSRCADDR:
        ch->src_addr = value;
        break;
    case DMA_M2M_CFG_MDSTADDR:
        ch->dst_addr = value;
        break;
    case DMA_M2M_CFG_MCTRL: {
        bool old_enable = !!(ch->ctrl & MCTRL_TRANS_EN);
        bool new_enable;

        ch->ctrl &= ~MCTRL_WRITABLE_MASK;
        ch->ctrl |= value & MCTRL_WRITABLE_MASK;
        new_enable = !!(ch->ctrl & MCTRL_TRANS_EN);

        if (!old_enable && new_enable) {
            if (ch->active) {
                ch->stop_pending = false;
            } else {
                nuclei_udma_start_m2m_channel(s, channel);
            }
        } else if (old_enable && !new_enable) {
            nuclei_udma_disable_m2m_channel(s, channel);
        }
        break;
    }
    case DMA_M2M_CFG_MRPT:
        ch->rpt = value & MRPT_WRITABLE_MASK;
        break;
    case DMA_M2M_CFG_MSIZE:
        ch->size = value & MSIZE_TSIZE_MASK;
        ch->initial_tsize = ch->size;
        ch->cur_tsize = ch->size;
        break;
    case DMA_M2M_CFG_MSRCADDR_H:
        ch->src_addr_hi = value & ADDR_H_MASK;
        break;
    case DMA_M2M_CFG_MDSTADDR_H:
        ch->dst_addr_hi = value & ADDR_H_MASK;
        break;
    case DMA_M2M_CFG_MRAU:
        ch->rau = value;
        break;
    case DMA_M2M_CFG_MLLA:
        ch->lla = value;
        break;
    case DMA_M2M_CFG_MLLA_H:
        ch->lla_hi = value & ADDR_H_MASK;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid m2m cfg register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        break;
    }
}

static uint32_t nuclei_udma_read_m2m_irq(NucleiUDMAState *s, uint32_t channel,
                                         hwaddr regoff)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    switch (regoff) {
    case DMA_M2M_IRQ_EN:
        return ch->irq_en;
    case DMA_M2M_IRQ_STAT:
        return ch->irq_stat;
    case DMA_M2M_IRQ_CLR:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid m2m irq register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        return 0;
    }
}

static void nuclei_udma_write_m2m_irq(NucleiUDMAState *s, uint32_t channel,
                                      hwaddr regoff, uint32_t value)
{
    NucleiUDMAM2MChannel *ch = &s->m2m[channel];

    switch (regoff) {
    case DMA_M2M_IRQ_EN:
        ch->irq_en = value & M2M_IRQ_MASK;
        break;
    case DMA_M2M_IRQ_STAT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only DMA_CHi_IRQ_STAT channel %u\n",
                      __func__, channel);
        return;
    case DMA_M2M_IRQ_CLR:
        ch->irq_stat &= ~(value & M2M_IRQ_MASK);
        if (ch->repeat_wait_irq_clear &&
            (ch->irq_stat & (M2M_IRQ_HTRANS | M2M_IRQ_FTRANS)) == 0) {
            ch->repeat_wait_irq_clear = false;
            if (ch->stop_pending || !(ch->ctrl & MCTRL_TRANS_EN)) {
                nuclei_udma_stop_m2m_channel(s, channel, false);
            } else {
                nuclei_udma_kick_m2m(s);
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid m2m irq register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        return;
    }

    nuclei_udma_update_irq(s);
}

static uint32_t nuclei_udma_read_pam_cfg(NucleiUDMAState *s, uint32_t channel,
                                         hwaddr regoff)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    switch (regoff) {
    case DMA_PAM_CFG_MSRCADDR:
        return ch->src_addr;
    case DMA_PAM_CFG_MDSTADDR:
        return ch->dst_addr;
    case DMA_PAM_CFG_MCTRL:
        return nuclei_udma_compose_pam_ctrl(ch);
    case DMA_PAM_CFG_MSIZE:
        return ch->size & MSIZE_TSIZE_MASK;
    case DMA_PAM_CFG_MSRCADDR_H:
        return ch->src_addr_hi & ADDR_H_MASK;
    case DMA_PAM_CFG_MDSTADDR_H:
        return ch->dst_addr_hi & ADDR_H_MASK;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid pa-mem cfg register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        return 0;
    }
}

static void nuclei_udma_write_pam_cfg(NucleiUDMAState *s, uint32_t channel,
                                      hwaddr regoff, uint32_t value)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    switch (regoff) {
    case DMA_PAM_CFG_MSRCADDR:
        ch->src_addr = value;
        break;
    case DMA_PAM_CFG_MDSTADDR:
        ch->dst_addr = value;
        break;
    case DMA_PAM_CFG_MCTRL: {
        bool old_enable = !!(ch->ctrl & PAM_CTRL_TRANS_EN);
        bool new_enable;

        ch->ctrl &= ~PAM_CTRL_WRITABLE_MASK;
        ch->ctrl |= value & PAM_CTRL_WRITABLE_MASK;
        new_enable = !!(ch->ctrl & PAM_CTRL_TRANS_EN);

        if (!old_enable && new_enable) {
            if (!nuclei_udma_validate_pam_start(s, channel)) {
                ch->ctrl &= ~PAM_CTRL_TRANS_EN;
                nuclei_udma_pam_clear_request_state(ch);
                break;
            }

            nuclei_udma_prime_pam_channel(s, channel);
        } else if (old_enable && !new_enable) {
            nuclei_udma_stop_pam_channel(s, channel, false);
        }
        break;
    }
    case DMA_PAM_CFG_MSIZE:
        ch->size = value & MSIZE_TSIZE_MASK;
        ch->initial_tsize = ch->size;
        ch->cur_tsize = ch->size;
        break;
    case DMA_PAM_CFG_MSRCADDR_H:
        ch->src_addr_hi = value & ADDR_H_MASK;
        break;
    case DMA_PAM_CFG_MDSTADDR_H:
        ch->dst_addr_hi = value & ADDR_H_MASK;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid pa-mem cfg register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        break;
    }
}

static uint32_t nuclei_udma_read_pam_irq(NucleiUDMAState *s, uint32_t channel,
                                         hwaddr regoff)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    switch (regoff) {
    case DMA_PAM_IRQ_EN:
        return ch->irq_en;
    case DMA_PAM_IRQ_STAT:
        return ch->irq_stat;
    case DMA_PAM_IRQ_CLR:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid pa-mem irq register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        return 0;
    }
}

static void nuclei_udma_write_pam_irq(NucleiUDMAState *s, uint32_t channel,
                                      hwaddr regoff, uint32_t value)
{
    NucleiUDMAPAChannel *ch = &s->pamem[channel];

    switch (regoff) {
    case DMA_PAM_IRQ_EN:
        ch->irq_en = value & PAM_IRQ_MASK;
        break;
    case DMA_PAM_IRQ_STAT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only DMA_PA_CHi_IRQ_STAT channel %u\n",
                      __func__, channel);
        return;
    case DMA_PAM_IRQ_CLR:
        ch->irq_stat &= ~(value & PAM_IRQ_MASK);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid pa-mem irq register offset 0x%" HWADDR_PRIx
                      " for channel %u\n",
                      __func__, regoff, channel);
        return;
    }

    nuclei_udma_update_irq(s);
}

static uint64_t nuclei_udma_read(void *opaque, hwaddr offset, unsigned size)
{
    NucleiUDMAState *s = opaque;
    uint32_t channel;
    hwaddr regoff;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported access size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, offset);
        return 0;
    }

    switch (offset) {
    case DMA_CFG_PCTRL:
        return s->pctrl;
    case DMA_CFG_EVENT:
        return s->event;
    case DMA_M2M_CHAL_IRQ_STAT:
        return nuclei_udma_m2m_irq_chal_stat(s);
    case DMA_PAM_CHAL_IRQ_STAT:
        return nuclei_udma_pam_irq_chal_stat(s);
    case DMA_VERSION:
        return s->version;
    default:
        break;
    }

    if (nuclei_udma_m2m_cfg_decode(offset, &channel, &regoff)) {
        if (channel >= s->m2m_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from unavailable m2m channel %u\n",
                          __func__, channel);
            return 0;
        }
        return nuclei_udma_read_m2m_cfg(s, channel, regoff);
    }

    if (nuclei_udma_m2m_irq_decode(offset, &channel, &regoff)) {
        if (channel >= s->m2m_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from unavailable m2m irq channel %u\n",
                          __func__, channel);
            return 0;
        }
        return nuclei_udma_read_m2m_irq(s, channel, regoff);
    }

    if (nuclei_udma_pam_cfg_decode(offset, &channel, &regoff)) {
        if (channel >= s->pamem_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from unavailable pa-mem channel %u\n",
                          __func__, channel);
            return 0;
        }
        return nuclei_udma_read_pam_cfg(s, channel, regoff);
    }

    if (nuclei_udma_pam_irq_decode(offset, &channel, &regoff)) {
        if (channel >= s->pamem_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from unavailable pa-mem irq channel %u\n",
                          __func__, channel);
            return 0;
        }
        return nuclei_udma_read_pam_irq(s, channel, regoff);
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: read from unimplemented register 0x%" HWADDR_PRIx "\n",
                  __func__, offset);
    return 0;
}

static void nuclei_udma_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    NucleiUDMAState *s = opaque;
    uint32_t channel;
    hwaddr regoff;
    uint32_t val32 = value;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported access size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, offset);
        return;
    }

    switch (offset) {
    case DMA_CFG_PCTRL:
        s->pctrl = val32;
        return;
    case DMA_CFG_EVENT:
        s->event = val32;
        return;
    case DMA_M2M_CHAL_IRQ_STAT:
        /*
         * Keep the aggregate CHAL view level-derived from per-channel
         * IRQ_STAT. Guest software should clear the channel source bits via
         * DMA_CHi_IRQ_CLR first; accepting the CHAL write without clearing the
         * source avoids losing a freshly re-latched repeat IRQ that arrives
         * between those two guest MMIO writes.
         */
        nuclei_udma_update_irq(s);
        return;
    case DMA_PAM_CHAL_IRQ_STAT:
        nuclei_udma_update_irq(s);
        return;
    case DMA_VERSION:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only DMA_VERSION ignored\n",
                      __func__);
        return;
    default:
        break;
    }

    if (nuclei_udma_m2m_cfg_decode(offset, &channel, &regoff)) {
        if (channel >= s->m2m_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to unavailable m2m channel %u\n",
                          __func__, channel);
            return;
        }
        nuclei_udma_write_m2m_cfg(s, channel, regoff, val32);
        return;
    }

    if (nuclei_udma_m2m_irq_decode(offset, &channel, &regoff)) {
        if (channel >= s->m2m_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to unavailable m2m irq channel %u\n",
                          __func__, channel);
            return;
        }
        nuclei_udma_write_m2m_irq(s, channel, regoff, val32);
        return;
    }

    if (nuclei_udma_pam_cfg_decode(offset, &channel, &regoff)) {
        if (channel >= s->pamem_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to unavailable pa-mem channel %u\n",
                          __func__, channel);
            return;
        }
        nuclei_udma_write_pam_cfg(s, channel, regoff, val32);
        return;
    }

    if (nuclei_udma_pam_irq_decode(offset, &channel, &regoff)) {
        if (channel >= s->pamem_channels) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to unavailable pa-mem irq channel %u\n",
                          __func__, channel);
            return;
        }
        nuclei_udma_write_pam_irq(s, channel, regoff, val32);
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "%s: write to unimplemented register 0x%" HWADDR_PRIx
                  " = 0x%08" PRIx32 "\n",
                  __func__, offset, val32);
}

static const MemoryRegionOps nuclei_udma_ops = {
    .read = nuclei_udma_read,
    .write = nuclei_udma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void nuclei_udma_reset(DeviceState *dev)
{
    NucleiUDMAState *s = NUCLEI_UDMA(dev);

    if (s->m2m_bh) {
        qemu_bh_cancel(s->m2m_bh);
    }

    memset(s->m2m, 0, sizeof(s->m2m));
    memset(s->pamem, 0, sizeof(s->pamem));
    memset(s->per, 0, sizeof(s->per));

    s->pctrl = 0;
    s->event = DMA_CFG_EVENT_RESET;

    nuclei_udma_update_irq(s);
}

bool nuclei_udma_pa_req(NucleiUDMAState *s, uint32_t channel,
                        NucleiUDMAPADirection direction, uint32_t req_len)
{
    NucleiUDMAPAChannel *ch;
    uint32_t width_code;
    uint32_t width_bytes;
    uint32_t req_beats;
    uint32_t available_beats;

    if (!s || channel >= s->pamem_channels) {
        return false;
    }

    ch = &s->pamem[channel];
    if (!(ch->ctrl & PAM_CTRL_TRANS_EN) || !ch->active || ch->cur_tsize == 0) {
        return false;
    }

    width_code = nuclei_udma_extract_field(ch->ctrl, PAM_CTRL_MWIDTH_MASK,
                                           PAM_CTRL_MWIDTH_SHIFT);
    if (!nuclei_udma_pam_width_code_to_bytes(width_code, &width_bytes) ||
        nuclei_udma_pam_req_queue_full(s, ch) ||
        !nuclei_udma_pam_req_direction_compatible(ch, direction)) {
        return false;
    }

    available_beats = nuclei_udma_pam_available_request_beats(ch, width_bytes);
    req_beats = MIN(MAX(1u, req_len), available_beats);
    if (req_beats == 0) {
        return false;
    }

    return nuclei_udma_pam_req_queue_push(s, ch, direction, req_beats);
}

bool nuclei_udma_pa_tx_pull_data(NucleiUDMAState *s, uint32_t channel,
                                 uint32_t *data, uint8_t *width_bytes)
{
    NucleiUDMAPAChannel *ch;
    uint32_t width_code;
    uint32_t width32;
    uint8_t beat_buf[4] = { 0 };
    uint32_t value = 0;
    uint32_t i;

    if (!s || channel >= s->pamem_channels || !data || !width_bytes) {
        return false;
    }

    ch = &s->pamem[channel];
    if (!nuclei_udma_pam_direction_ready(ch, NUCLEI_UDMA_PA_DIR_TX)) {
        return false;
    }

    width_code = nuclei_udma_extract_field(ch->ctrl, PAM_CTRL_MWIDTH_MASK,
                                           PAM_CTRL_MWIDTH_SHIFT);
    if (!nuclei_udma_pam_width_code_to_bytes(width_code, &width32)) {
        return false;
    }

    if (dma_memory_read(s->dma_as, ch->cur_src_addr, beat_buf, width32,
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        nuclei_udma_fail_pam_channel(s, channel);
        return false;
    }

    for (i = 0; i < width32; i++) {
        value |= (uint32_t)beat_buf[i] << (i * 8);
    }

    *data = value;
    *width_bytes = width32;
    if (!(ch->ctrl & PAM_CTRL_MSNA)) {
        ch->cur_src_addr = nuclei_udma_adjust_addr(s, ch->cur_src_addr,
                                                   width32, false);
    }
    nuclei_udma_complete_pam_beat(s, channel, width32);
    return true;
}

bool nuclei_udma_pa_rx_push_data(NucleiUDMAState *s, uint32_t channel,
                                 uint32_t data, uint8_t width_bytes)
{
    NucleiUDMAPAChannel *ch;
    uint32_t width_code;
    uint32_t expected_width;
    uint8_t beat_buf[4];
    uint32_t i;

    if (!s || channel >= s->pamem_channels) {
        return false;
    }

    ch = &s->pamem[channel];
    if (!nuclei_udma_pam_direction_ready(ch, NUCLEI_UDMA_PA_DIR_RX)) {
        return false;
    }

    width_code = nuclei_udma_extract_field(ch->ctrl, PAM_CTRL_MWIDTH_MASK,
                                           PAM_CTRL_MWIDTH_SHIFT);
    if (!nuclei_udma_pam_width_code_to_bytes(width_code, &expected_width) ||
        width_bytes != expected_width) {
        return false;
    }

    for (i = 0; i < expected_width; i++) {
        beat_buf[i] = (data >> (i * 8)) & 0xff;
    }

    if (dma_memory_write(s->dma_as, ch->cur_dst_addr, beat_buf, expected_width,
                         MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        nuclei_udma_fail_pam_channel(s, channel);
        return false;
    }

    if (!(ch->ctrl & PAM_CTRL_MDNA)) {
        ch->cur_dst_addr = nuclei_udma_adjust_addr(s, ch->cur_dst_addr,
                                                   expected_width, false);
    }
    nuclei_udma_complete_pam_beat(s, channel, expected_width);
    return true;
}

void nuclei_udma_pa_stop(NucleiUDMAState *s, uint32_t channel)
{
    if (!s || channel >= s->pamem_channels) {
        return;
    }

    nuclei_udma_stop_pam_channel(s, channel, true);
}

static void nuclei_udma_init(Object *obj)
{
    NucleiUDMAState *s = NUCLEI_UDMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->mmio, obj, &nuclei_udma_ops, s,
                          TYPE_NUCLEI_UDMA, NUCLEI_UDMA_REG_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void nuclei_udma_realize(DeviceState *dev, Error **errp)
{
    NucleiUDMAState *s = NUCLEI_UDMA(dev);

    if (s->addr_width < 32 || s->addr_width > 48) {
        error_setg(errp, "nuclei_udma: addr-width must be in [32, 48]");
        return;
    }
    if (s->m2m_channels == 0 ||
        s->m2m_channels > NUCLEI_UDMA_MAX_M2M_CHANNELS) {
        error_setg(errp,
                   "nuclei_udma: m2m-channels must be in [1, %u]",
                   NUCLEI_UDMA_MAX_M2M_CHANNELS);
        return;
    }
    if (s->pamem_channels > NUCLEI_UDMA_MAX_PAMEM_CHANNELS) {
        error_setg(errp,
                   "nuclei_udma: pamem-channels must be in [0, %u]",
                   NUCLEI_UDMA_MAX_PAMEM_CHANNELS);
        return;
    }
    if (s->per_channels > NUCLEI_UDMA_MAX_PER_CHANNELS) {
        error_setg(errp,
                   "nuclei_udma: per-channels must be in [0, %u]",
                   NUCLEI_UDMA_MAX_PER_CHANNELS);
        return;
    }
    if (s->pamem_request_queue_depth == 0 ||
        s->pamem_request_queue_depth > NUCLEI_UDMA_PAM_MAX_REQ_QUEUE_DEPTH) {
        error_setg(errp,
                   "nuclei_udma: pamem-request-queue-depth must be in [1, %u]",
                   NUCLEI_UDMA_PAM_MAX_REQ_QUEUE_DEPTH);
        return;
    }

    if (!s->mem_mr || s->mem_mr == get_system_memory()) {
        s->mem_mr = get_system_memory();
        s->dma_as = &address_space_memory;
        s->dma_as_uses_system_memory = true;
    } else {
        address_space_init(&s->dma_as_storage, s->mem_mr,
                           memory_region_name(s->mem_mr));
        s->dma_as = &s->dma_as_storage;
        s->dma_as_uses_system_memory = false;
    }

    s->m2m_bh = qemu_bh_new(nuclei_udma_run_m2m_bh, s);
}

static void nuclei_udma_unrealize(DeviceState *dev)
{
    NucleiUDMAState *s = NUCLEI_UDMA(dev);

    if (s->m2m_bh) {
        qemu_bh_delete(s->m2m_bh);
        s->m2m_bh = NULL;
    }

    if (!s->dma_as_uses_system_memory && s->dma_as) {
        address_space_destroy(&s->dma_as_storage);
    }

    s->dma_as = NULL;
    s->mem_mr = NULL;
    s->dma_as_uses_system_memory = false;
}

static Property nuclei_udma_properties[] = {
    DEFINE_PROP_UINT32("addr-width", NucleiUDMAState, addr_width, 48),
    DEFINE_PROP_UINT32("m2m-channels", NucleiUDMAState, m2m_channels,
                       NUCLEI_UDMA_MAX_M2M_CHANNELS),
    DEFINE_PROP_UINT32("pamem-channels", NucleiUDMAState, pamem_channels,
                       NUCLEI_UDMA_MAX_PAMEM_CHANNELS),
    DEFINE_PROP_UINT32("per-channels", NucleiUDMAState, per_channels,
                       NUCLEI_UDMA_MAX_PER_CHANNELS),
    DEFINE_PROP_UINT32("pamem-request-queue-depth", NucleiUDMAState,
                       pamem_request_queue_depth,
                       NUCLEI_UDMA_DEFAULT_PAM_REQ_QUEUE_DEPTH),
    DEFINE_PROP_UINT32("version", NucleiUDMAState, version,
                       NUCLEI_UDMA_DEFAULT_VERSION),
    DEFINE_PROP_LINK("memory", NucleiUDMAState, mem_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_udma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nuclei_udma_realize;
    dc->unrealize = nuclei_udma_unrealize;
    dc->reset = nuclei_udma_reset;
    device_class_set_props(dc, nuclei_udma_properties);
}

static const TypeInfo nuclei_udma_info = {
    .name = TYPE_NUCLEI_UDMA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiUDMAState),
    .instance_init = nuclei_udma_init,
    .class_init = nuclei_udma_class_init,
};

static void nuclei_udma_register_types(void)
{
    type_register_static(&nuclei_udma_info);
}

type_init(nuclei_udma_register_types)
