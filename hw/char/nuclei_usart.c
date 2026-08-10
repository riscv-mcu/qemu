/*
 * NUCLEI USART v3.1.0 interface
 *
 * Copyright (c) 2024 Nuclei.
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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/char/nuclei_usart.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"

/*
 * Not yet implemented:
 *
 * - Full device-internal DIV-driven timing model beyond RX timeout scaling
 *
 * Explicitly deferred in the current phase because they would likely require
 * broader host-side or cross-subsystem work:
 *
 * - Host-side 9-bit TX data path expansion
 * - DMA / sync / SPI-slave / smartcard / RS485 semantics
 * - New chardev/QMP/backend control interfaces for richer serial sideband
 */
#define NUCLEI_USART_DATA_MASK                  0x000001FFU
#define NUCLEI_USART_LEGACY_TXFIFO_FULL         0x80000000U
#define NUCLEI_USART_CTRL_RMASK                 0x001F0003U
#define NUCLEI_USART_CTRL_ENABLE                0x00000001U
#define NUCLEI_USART_CTRL_CLEAR                 0x00000004U
#define NUCLEI_USART_INT_EN_MASK                0x001FFFFFU
#define NUCLEI_USART_DIV_MASK                   0x001FFFFFU
#define NUCLEI_USART_DIV_INT_MASK               0x0007FFFFU
#define NUCLEI_USART_SETUP_MASK                 0xFFFFFFF7U
#define NUCLEI_USART_SETUP_PARITY_EN            0x00000001U
#define NUCLEI_USART_SETUP_BIT_LENGTH_SHIFT     4U
#define NUCLEI_USART_SETUP_BIT_LENGTH_MASK      0x00000070U
#define NUCLEI_USART_SETUP_FRACTION_BAUD        0x00080000U
#define NUCLEI_USART_SETUP_MSBFIRST             0x02000000U
#define NUCLEI_USART_SETUP_DATAINV              0x04000000U
#define NUCLEI_USART_SETUP_CFG_STOP_BIT_SHIFT   30U
#define NUCLEI_USART_SETUP_CFG_STOP_BIT_MASK    0xC0000000U
#define NUCLEI_USART_SIZE_MASK                  0x000FFFFFU
#define NUCLEI_USART_SPI_SLAVE_MASK             0x00000003U
#define NUCLEI_USART_DATASIZE_MASK              0x00000003U
#define NUCLEI_USART_SMARTCARD_SETUP_MASK       0x0000001FU
#define NUCLEI_USART_SMARTCARD_TIMING_MASK      0x0000FFFFU
#define NUCLEI_USART_ADVANCED_SETUP_MASK        0x0078FFFFU
#define NUCLEI_USART_ADVANCED_SETUP_RESET       0x00400000U
#define NUCLEI_USART_ADVANCED_SETUP_LIN_EN      0x00000800U
#define NUCLEI_USART_ADVANCED_SETUP_RTU_EN      0x00008000U
#define NUCLEI_USART_ADVANCED_SETUP_ABR_EN      0x00080000U
#define NUCLEI_USART_ADVANCED_SETUP_ABR_MOD_SHIFT 20U
#define NUCLEI_USART_ADVANCED_SETUP_ABR_MOD_MASK  0x00300000U
#define NUCLEI_USART_ADVANCED_SETUP_IDLE_TOUT_FIFO_GATED 0x00400000U
#define NUCLEI_USART_ADVANCED_STATUS_RW_MASK    0x00000001U
#define NUCLEI_USART_ADVANCED_STATUS_W1S_MASK   0x00000002U
#define NUCLEI_USART_ADVANCED_STATUS_W1C_MASK   0x0000006CU

#define NUCLEI_USART_STATUS_TXIP                0x00000001U
#define NUCLEI_USART_STATUS_RXIP                0x00000002U
#define NUCLEI_USART_STATUS_TX_BUSY             0x00000004U
#define NUCLEI_USART_STATUS_RX_BUSY             0x00000008U
#define NUCLEI_USART_STATUS_RX_ERROR_OVER_FLOW  0x00000010U
#define NUCLEI_USART_STATUS_RX_ERROR_PARITY     0x00000020U
#define NUCLEI_USART_STATUS_CTS_RISE_FLAG       0x00000040U
#define NUCLEI_USART_STATUS_CTS_FALL_FLAG       0x00000080U
#define NUCLEI_USART_STATUS_STOP_BIT_ERR_FLAG   0x00000100U
#define NUCLEI_USART_STATUS_RX_IDLE_TOUT        0x00001000U
#define NUCLEI_USART_STATUS_RX_WM_TOUT          0x00002000U
#define NUCLEI_USART_STATUS_TX_FIFO_FULL        0x00004000U
#define NUCLEI_USART_STATUS_RX_FIFO_EMPTY       0x00008000U
#define NUCLEI_USART_STATUS_TX_EOT              0x00010000U
#define NUCLEI_USART_STATUS_RX_EOT              0x00020000U
#define NUCLEI_USART_STATUS_SPI_SLV_UNDER_RUN   0x00040000U
#define NUCLEI_USART_STATUS_SPI_SLV_OVER_RUN    0x00080000U
#define NUCLEI_USART_STATUS_TX_FIFO_EMPTY       0x00200000U
#define NUCLEI_USART_STATUS_RX_FIFO_FULL        0x00400000U
#define NUCLEI_USART_STATUS_FRAME_ERR_FLAG      0x00800000U
#define NUCLEI_USART_STATUS_NE_FLAG             0x01000000U
#define NUCLEI_USART_STATUS_W1C_MASK            0x018F31F0U

#define NUCLEI_USART_ADV_STATUS_LIN_SBKF        0x00000002U
#define NUCLEI_USART_ADV_STATUS_LIN_LBDF        0x00000004U
#define NUCLEI_USART_ADV_STATUS_LFCMF           0x00000008U
#define NUCLEI_USART_ADV_STATUS_ABRF            0x00000020U
#define NUCLEI_USART_ADV_STATUS_ABRE            0x00000040U
#define NUCLEI_USART_MODBUS_ASCII_LF            0x0000000AU
#define NUCLEI_USART_LIN_BREAK_SYMBOLS          15U
/*
 * RX timeout demos currently rely on a coarse functional delay model, so keep
 * timeout counters on the historical scale for now. Frame-level busy windows
 * use a separate 1 ns-per-divider-tick approximation to keep TX/RX completion
 * behavior in a practical range.
 */
#define NUCLEI_USART_TIMEOUT_SCALE_NS           1000ULL
#define NUCLEI_USART_FRAME_SCALE_NS             1ULL
#define NUCLEI_USART_TX_BUSY_OBSERVE_MIN_NS     (20000LL)
#define NUCLEI_USART_TX_BUSY_CHAIN_MIN_NS       (1LL)
#define NUCLEI_USART_FIFO_COUNT_MASK            0x0000001FU
#define NUCLEI_USART_BUSY_OBSERVE_MIN_NS        (2000000LL)
static void nuclei_usart_mark_tx_busy(NucleiUSARTState *s, uint32_t half_bits,
                                      bool chained);
static void nuclei_usart_mark_rx_busy(NucleiUSARTState *s, uint32_t half_bits);
static uint32_t nuclei_usart_irq_pending(NucleiUSARTState *s);
static void nuclei_usart_update_irq(NucleiUSARTState *s);
static int64_t nuclei_usart_timeout_deadline(NucleiUSARTState *s,
                                             uint32_t count);
static uint32_t nuclei_usart_data_bits(NucleiUSARTState *s);

static uint32_t nuclei_usart_rx_watermark(NucleiUSARTState *s)
{
    return (s->rxctrl >> 16) & 0x1f;
}

static uint32_t nuclei_usart_tx_watermark(NucleiUSARTState *s)
{
    return (s->txctrl >> 16) & 0x1f;
}

static bool nuclei_usart_tx_enabled(NucleiUSARTState *s)
{
    return (s->txctrl & NUCLEI_USART_CTRL_ENABLE) != 0;
}

static bool nuclei_usart_rx_enabled(NucleiUSARTState *s)
{
    return (s->rxctrl & NUCLEI_USART_CTRL_ENABLE) != 0;
}

static bool nuclei_usart_rx_overflow_latched(NucleiUSARTState *s)
{
    return (s->usart_status & NUCLEI_USART_STATUS_RX_ERROR_OVER_FLOW) != 0;
}

/*
 * Keep the FIFO and chardev path byte-oriented for now. DATASIZE=word only
 * changes the TX_SIZE/RX_SIZE accounting contract, and only for <= 8-bit data
 * widths where one quota unit can be approximated as four transferred bytes.
 */
static bool nuclei_usart_word_datasize_enabled(NucleiUSARTState *s,
                                               uint32_t datasize)
{
    return ((datasize & NUCLEI_USART_DATASIZE_MASK) == 0x2U) &&
           nuclei_usart_data_bits(s) <= 8U;
}

static uint32_t nuclei_usart_tx_size_unit_bytes(NucleiUSARTState *s)
{
    return nuclei_usart_word_datasize_enabled(s, s->usart_tx_datasize) ? 4U : 1U;
}

static uint32_t nuclei_usart_rx_size_unit_bytes(NucleiUSARTState *s)
{
    return nuclei_usart_word_datasize_enabled(s, s->usart_rx_datasize) ? 4U : 1U;
}

static uint64_t nuclei_usart_timeout_divider(NucleiUSARTState *s)
{
    uint32_t raw_div;

    if (s->setup & NUCLEI_USART_SETUP_FRACTION_BAUD) {
        raw_div = s->div & NUCLEI_USART_DIV_MASK;
    } else {
        raw_div = s->div & NUCLEI_USART_DIV_INT_MASK;
    }

    return (uint64_t)raw_div + 1ULL;
}

static uint32_t nuclei_usart_data_bits(NucleiUSARTState *s)
{
    switch ((s->setup & NUCLEI_USART_SETUP_BIT_LENGTH_MASK) >>
            NUCLEI_USART_SETUP_BIT_LENGTH_SHIFT) {
    case 0:
        return 5;
    case 1:
        return 6;
    case 2:
        return 7;
    case 3:
        return 8;
    case 4:
        return 9;
    default:
        return 8;
    }
}

static uint16_t nuclei_usart_data_mask(NucleiUSARTState *s)
{
    return (1U << nuclei_usart_data_bits(s)) - 1U;
}

static uint16_t nuclei_usart_reverse_data_bits(uint16_t value, uint32_t width)
{
    uint16_t reversed = 0;
    uint32_t i;

    for (i = 0; i < width; i++) {
        reversed <<= 1;
        reversed |= value & 1U;
        value >>= 1;
    }

    return reversed;
}

static uint16_t nuclei_usart_apply_data_path_transforms(NucleiUSARTState *s,
                                                        uint16_t value)
{
    uint32_t width = nuclei_usart_data_bits(s);
    uint16_t mask = nuclei_usart_data_mask(s);

    value &= mask;

    if (s->setup & NUCLEI_USART_SETUP_MSBFIRST) {
        value = nuclei_usart_reverse_data_bits(value, width);
    }

    if (s->setup & NUCLEI_USART_SETUP_DATAINV) {
        value ^= mask;
    }

    return value & mask;
}

static uint32_t nuclei_usart_stop_half_bits(NucleiUSARTState *s)
{
    switch ((s->setup & NUCLEI_USART_SETUP_CFG_STOP_BIT_MASK) >>
            NUCLEI_USART_SETUP_CFG_STOP_BIT_SHIFT) {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
        return 3;
    default:
        return 4;
    }
}

static uint32_t nuclei_usart_frame_half_bits(NucleiUSARTState *s)
{
    uint32_t half_bits = 2U + (nuclei_usart_data_bits(s) * 2U) +
                         nuclei_usart_stop_half_bits(s);

    if (s->setup & NUCLEI_USART_SETUP_PARITY_EN) {
        half_bits += 2U;
    }

    return half_bits;
}

static int64_t nuclei_usart_frame_half_bits_duration_ns(NucleiUSARTState *s,
                                                        uint32_t half_bits)
{
    uint64_t divider = nuclei_usart_timeout_divider(s);
    uint64_t delta = MAX((uint64_t)half_bits, 1ULL);

    if (delta > UINT64_MAX / divider) {
        delta = UINT64_MAX;
    } else {
        delta *= divider;
    }

    if (delta > UINT64_MAX / NUCLEI_USART_FRAME_SCALE_NS) {
        delta = UINT64_MAX;
    } else {
        delta *= NUCLEI_USART_FRAME_SCALE_NS;
    }

    delta = (delta + 1ULL) / 2ULL;
    return MIN(delta, (uint64_t)INT64_MAX);
}

static int64_t nuclei_usart_busy_visible_interval_ns(NucleiUSARTState *s,
                                                     uint32_t half_bits)
{
    return MAX(nuclei_usart_frame_half_bits_duration_ns(s, half_bits),
               NUCLEI_USART_BUSY_OBSERVE_MIN_NS);
}

static int64_t nuclei_usart_tx_busy_interval_ns(NucleiUSARTState *s,
                                                uint32_t half_bits,
                                                bool chained)
{
    int64_t min_visible = chained ? NUCLEI_USART_TX_BUSY_CHAIN_MIN_NS :
                          NUCLEI_USART_TX_BUSY_OBSERVE_MIN_NS;

    return MAX(nuclei_usart_frame_half_bits_duration_ns(s, half_bits),
               min_visible);
}

static int64_t nuclei_usart_add_ns_saturating(int64_t base, int64_t delta)
{
    if (delta > INT64_MAX - base) {
        return INT64_MAX;
    }

    return base + delta;
}

static bool nuclei_usart_modbus_event_enabled(NucleiUSARTState *s)
{
    /*
     * The current model does not distinguish full MODBUS RTU/ASCII framing.
     * Reuse RTU_EN as the minimal guest-visible gate before latching the
     * ASCII line-feed match event.
     */
    return (s->usart_advanced_setup & NUCLEI_USART_ADVANCED_SETUP_RTU_EN) != 0;
}

static bool nuclei_usart_abr_enabled(NucleiUSARTState *s)
{
    return (s->usart_advanced_setup & NUCLEI_USART_ADVANCED_SETUP_ABR_EN) != 0;
}

static uint32_t nuclei_usart_abr_mode(NucleiUSARTState *s)
{
    return (s->usart_advanced_setup & NUCLEI_USART_ADVANCED_SETUP_ABR_MOD_MASK) >>
           NUCLEI_USART_ADVANCED_SETUP_ABR_MOD_SHIFT;
}

static uint32_t nuclei_usart_abr_raw_width(NucleiUSARTState *s)
{
    return MIN(nuclei_usart_data_bits(s), 8U);
}

static bool nuclei_usart_abr_match_raw_start_bit(NucleiUSARTState *s,
                                                 uint8_t value)
{
    uint32_t width = nuclei_usart_abr_raw_width(s);
    uint8_t mask;

    if (s->setup & NUCLEI_USART_SETUP_MSBFIRST) {
        mask = 1U << (width - 1U);
    } else {
        mask = 0x01U;
    }

    return (value & mask) != 0;
}

static bool nuclei_usart_abr_match_raw_negedge(NucleiUSARTState *s,
                                               uint8_t value)
{
    uint32_t width = nuclei_usart_abr_raw_width(s);
    uint8_t mask;
    uint8_t expected;

    if (s->setup & NUCLEI_USART_SETUP_MSBFIRST) {
        mask = 0x03U << (width - 2U);
        expected = 0x02U << (width - 2U);
    } else {
        mask = 0x03U;
        expected = 0x01U;
    }

    return (value & mask) == expected;
}

static uint8_t nuclei_usart_abr_expected_raw_byte(NucleiUSARTState *s,
                                                  uint8_t logical_value)
{
    if (s->setup & NUCLEI_USART_SETUP_MSBFIRST) {
        return nuclei_usart_reverse_data_bits(logical_value, 8U);
    }

    return logical_value;
}

static bool nuclei_usart_abr_match_raw_byte(NucleiUSARTState *s, uint8_t value)
{
    switch (nuclei_usart_abr_mode(s)) {
    case 0:
        return nuclei_usart_abr_match_raw_start_bit(s, value);
    case 1:
        return nuclei_usart_abr_match_raw_negedge(s, value);
    case 2:
        if (nuclei_usart_data_bits(s) == 7U) {
            return false;
        }
        return value == nuclei_usart_abr_expected_raw_byte(s, 0x7fU);
    case 3:
        if (nuclei_usart_data_bits(s) == 7U) {
            return false;
        }
        return value == nuclei_usart_abr_expected_raw_byte(s, 0x55U);
    default:
        return false;
    }
}

static void nuclei_usart_maybe_latch_abr(NucleiUSARTState *s, uint8_t value)
{
    if (!nuclei_usart_abr_enabled(s) ||
        (s->usart_advanced_status &
         (NUCLEI_USART_ADV_STATUS_ABRF | NUCLEI_USART_ADV_STATUS_ABRE))) {
        return;
    }

    /*
     * Keep auto-baud in the current device-internal envelope: latch the
     * pattern-detect success/error flags from the first raw RX byte that
     * reaches the USART.  The current model interprets ABR_MOD[0:1] against
     * the serial start pattern in the active bit order, while ABR_MOD[2:3]
     * continue to use fixed raw-byte matches before RX data transforms.  It
     * does not attempt divider learning or host-side timing reconstruction.
     */
    if (nuclei_usart_abr_match_raw_byte(s, value)) {
        s->usart_advanced_status |= NUCLEI_USART_ADV_STATUS_ABRF;
    } else {
        s->usart_advanced_status |= NUCLEI_USART_ADV_STATUS_ABRE;
    }
}

static bool nuclei_usart_lin_mode_enabled(NucleiUSARTState *s)
{
    return (s->usart_advanced_setup & NUCLEI_USART_ADVANCED_SETUP_LIN_EN) != 0;
}

static bool nuclei_usart_lin_break_send_enabled(NucleiUSARTState *s)
{
    return nuclei_usart_lin_mode_enabled(s) && nuclei_usart_tx_enabled(s);
}

static bool nuclei_usart_lin_send_break_requested(NucleiUSARTState *s)
{
    return (s->usart_advanced_status & NUCLEI_USART_ADV_STATUS_LIN_SBKF) != 0;
}

static bool nuclei_usart_lin_send_break_active(NucleiUSARTState *s)
{
    return s->lin_break_timer && timer_pending(s->lin_break_timer);
}

static bool nuclei_usart_idle_timeout_requires_rx_data(NucleiUSARTState *s)
{
    return (s->usart_advanced_setup &
            NUCLEI_USART_ADVANCED_SETUP_IDLE_TOUT_FIFO_GATED) != 0;
}

static void nuclei_usart_tx_fifo_reset(NucleiUSARTState *s)
{
    s->tx_fifo_head = 0;
    s->tx_fifo_tail = 0;
    s->tx_fifo_len = 0;
}

static bool nuclei_usart_tx_fifo_push(NucleiUSARTState *s, uint16_t value)
{
    if (s->tx_fifo_len >= NUCLEI_USART_FIFO_DEPTH) {
        return false;
    }

    s->tx_fifo[s->tx_fifo_tail] = value & nuclei_usart_data_mask(s);
    s->tx_fifo_tail = (s->tx_fifo_tail + 1) % NUCLEI_USART_FIFO_DEPTH;
    s->tx_fifo_len++;
    return true;
}

static uint16_t nuclei_usart_tx_fifo_pop(NucleiUSARTState *s)
{
    uint16_t value;

    g_assert(s->tx_fifo_len != 0);
    value = s->tx_fifo[s->tx_fifo_head];
    s->tx_fifo_head = (s->tx_fifo_head + 1) % NUCLEI_USART_FIFO_DEPTH;
    s->tx_fifo_len--;
    return value;
}

static void nuclei_usart_rx_fifo_reset(NucleiUSARTState *s)
{
    s->rx_fifo_head = 0;
    s->rx_fifo_tail = 0;
    s->rx_fifo_len = 0;
}

static bool nuclei_usart_rx_fifo_push(NucleiUSARTState *s, uint16_t value)
{
    if (s->rx_fifo_len >= NUCLEI_USART_FIFO_DEPTH) {
        return false;
    }

    s->rx_fifo[s->rx_fifo_tail] = value & nuclei_usart_data_mask(s);
    s->rx_fifo_tail = (s->rx_fifo_tail + 1) % NUCLEI_USART_FIFO_DEPTH;
    s->rx_fifo_len++;
    return true;
}

static uint16_t nuclei_usart_rx_fifo_pop(NucleiUSARTState *s)
{
    uint16_t value;

    g_assert(s->rx_fifo_len != 0);
    value = s->rx_fifo[s->rx_fifo_head];
    s->rx_fifo_head = (s->rx_fifo_head + 1) % NUCLEI_USART_FIFO_DEPTH;
    s->rx_fifo_len--;
    return value;
}

static unsigned int nuclei_usart_rx_capacity(NucleiUSARTState *s)
{
    unsigned int free_entries;

    if (!nuclei_usart_rx_enabled(s)) {
        return 0;
    }

    free_entries = NUCLEI_USART_FIFO_DEPTH - s->rx_fifo_len;
    if (free_entries != 0) {
        if (!nuclei_usart_rx_overflow_latched(s)) {
            free_entries++;
        }
        return free_entries;
    }

    return nuclei_usart_rx_overflow_latched(s) ? 0 : 1;
}

static void nuclei_usart_complete_tx_word(NucleiUSARTState *s)
{
    uint32_t unit_bytes;

    if (s->usart_tx_size > 0) {
        unit_bytes = nuclei_usart_tx_size_unit_bytes(s);
        s->tx_size_progress++;
        if (s->tx_size_progress >= unit_bytes) {
            s->tx_size_progress = 0;
            s->usart_tx_size--;
            if (s->usart_tx_size == 0) {
                s->tx_eot_pending = true;
            }
        }
    }
}

static void nuclei_usart_write_rx_size(NucleiUSARTState *s, uint32_t value)
{
    s->usart_rx_size = value & NUCLEI_USART_SIZE_MASK;
    s->rx_size_progress = 0;

    /*
     * Programming a new non-zero receive quota starts a fresh completion
     * window, so an older RX_EOT latch must not leak into the next transfer.
     */
    if (s->usart_rx_size != 0) {
        s->usart_status &= ~NUCLEI_USART_STATUS_RX_EOT;
    }
}

static void nuclei_usart_write_tx_size(NucleiUSARTState *s, uint32_t value)
{
    s->usart_tx_size = value & NUCLEI_USART_SIZE_MASK;
    s->tx_size_progress = 0;

    /*
     * Re-arming TX_SIZE for a new tracked transmission must also withdraw any
     * earlier completion state, including the deferred post-busy TX_EOT path.
     */
    if (s->usart_tx_size != 0) {
        s->usart_status &= ~NUCLEI_USART_STATUS_TX_EOT;
        s->tx_eot_pending = false;
    }
}

/*
 * Drain one or more already-staged TX words into the host chardev immediately.
 * The guest-visible transmission lifetime is modeled separately by TX_BUSY and
 * TX_EOT, so this helper only advances FIFO/TXDATA/TX_SIZE state.
 */
static unsigned int nuclei_usart_drain_tx_words(NucleiUSARTState *s,
                                                unsigned int max_words)
{
    unsigned int drained = 0;

    while (drained < max_words && s->tx_fifo_len != 0) {
        uint16_t encoded_value;
        uint16_t value;
        unsigned char ch;

        value = nuclei_usart_tx_fifo_pop(s);
        encoded_value = nuclei_usart_apply_data_path_transforms(s, value);
        ch = encoded_value & 0xffU;
        s->txdata = value;
        qemu_chr_fe_write(&s->chr, &ch, 1);
        nuclei_usart_complete_tx_word(s);
        drained++;
    }

    return drained;
}

static bool nuclei_usart_tx_busy(NucleiUSARTState *s)
{
    return (s->usart_status & NUCLEI_USART_STATUS_TX_BUSY) != 0;
}

static void nuclei_usart_start_lin_send_break(NucleiUSARTState *s)
{
    int break_enable = 1;

    /*
     * Serialize active send-break against the existing TX path: only start the
     * break field once the current modeled transmitter state is fully idle, so
     * BREAK does not overlap with a queued data frame in the minimal model.
     */
    if (!s->lin_break_timer ||
        !nuclei_usart_lin_send_break_requested(s) ||
        !nuclei_usart_lin_break_send_enabled(s) ||
        nuclei_usart_lin_send_break_active(s) ||
        nuclei_usart_tx_busy(s) ||
        s->tx_fifo_len != 0 ||
        s->tx_eot_pending) {
        return;
    }

    s->usart_status |= NUCLEI_USART_STATUS_TX_BUSY;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK, &break_enable);
    timer_mod(s->lin_break_timer,
              nuclei_usart_timeout_deadline(s, NUCLEI_USART_LIN_BREAK_SYMBOLS));
}

static void nuclei_usart_start_tx_word(NucleiUSARTState *s, bool chained)
{
    if (!nuclei_usart_tx_enabled(s) || s->tx_fifo_len == 0 ||
        nuclei_usart_tx_busy(s)) {
        return;
    }

    if (nuclei_usart_drain_tx_words(s, 1) != 0) {
        nuclei_usart_mark_tx_busy(s, nuclei_usart_frame_half_bits(s), chained);
    }
}

static void nuclei_usart_kick_tx(NucleiUSARTState *s)
{
    nuclei_usart_start_lin_send_break(s);
    nuclei_usart_start_tx_word(s, false);
}

static int64_t nuclei_usart_timeout_deadline(NucleiUSARTState *s,
                                             uint32_t count)
{
    uint64_t ticks = MAX((uint64_t)count, 1ULL);
    uint64_t divider = nuclei_usart_timeout_divider(s);
    uint64_t delta;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (ticks > UINT64_MAX / divider) {
        delta = UINT64_MAX;
    } else {
        delta = ticks * divider;
    }

    if (delta > UINT64_MAX / NUCLEI_USART_TIMEOUT_SCALE_NS) {
        delta = UINT64_MAX;
    } else {
        delta *= NUCLEI_USART_TIMEOUT_SCALE_NS;
    }

    if (delta > (uint64_t)(INT64_MAX - now)) {
        return INT64_MAX;
    }

    return now + (int64_t)delta;
}

static void nuclei_usart_cancel_rx_idle_timer(NucleiUSARTState *s)
{
    if (s->rx_idle_timer) {
        timer_del(s->rx_idle_timer);
    }
}

static void nuclei_usart_cancel_rx_wm_timer(NucleiUSARTState *s)
{
    if (s->rx_wm_timer) {
        timer_del(s->rx_wm_timer);
    }
}

static void nuclei_usart_cancel_tx_busy_timer(NucleiUSARTState *s)
{
    if (s->tx_busy_timer) {
        timer_del(s->tx_busy_timer);
    }
    s->tx_busy_deadline = 0;
}

static void nuclei_usart_cancel_rx_busy_timer(NucleiUSARTState *s)
{
    if (s->rx_busy_timer) {
        timer_del(s->rx_busy_timer);
    }
    s->rx_busy_deadline = 0;
}

static void nuclei_usart_cancel_lin_break_timer(NucleiUSARTState *s)
{
    if (s->lin_break_timer) {
        timer_del(s->lin_break_timer);
    }
}

static void nuclei_usart_clear_tx_busy(NucleiUSARTState *s)
{
    nuclei_usart_cancel_tx_busy_timer(s);
    s->usart_status &= ~NUCLEI_USART_STATUS_TX_BUSY;
    s->tx_eot_pending = false;
    s->tx_size_progress = 0;
}

static void nuclei_usart_clear_rx_busy(NucleiUSARTState *s)
{
    nuclei_usart_cancel_rx_busy_timer(s);
    s->usart_status &= ~NUCLEI_USART_STATUS_RX_BUSY;
    s->rx_busy_interval_ns = 0;
    s->rx_busy_bonus_pending = false;
}

static void nuclei_usart_clear_tx_latched_status(NucleiUSARTState *s)
{
    s->usart_status &= ~NUCLEI_USART_STATUS_TX_EOT;
    s->tx_eot_pending = false;
    s->tx_size_progress = 0;
}

static void nuclei_usart_clear_rx_latched_status(NucleiUSARTState *s)
{
    s->usart_status &= ~(NUCLEI_USART_STATUS_RX_ERROR_OVER_FLOW |
                         NUCLEI_USART_STATUS_RX_ERROR_PARITY |
                         NUCLEI_USART_STATUS_STOP_BIT_ERR_FLAG |
                         NUCLEI_USART_STATUS_RX_IDLE_TOUT |
                         NUCLEI_USART_STATUS_RX_WM_TOUT |
                         NUCLEI_USART_STATUS_RX_EOT |
                         NUCLEI_USART_STATUS_SPI_SLV_UNDER_RUN |
                         NUCLEI_USART_STATUS_SPI_SLV_OVER_RUN |
                         NUCLEI_USART_STATUS_FRAME_ERR_FLAG |
                         NUCLEI_USART_STATUS_NE_FLAG);
    s->usart_advanced_status &= ~(NUCLEI_USART_ADV_STATUS_LIN_LBDF |
                                  NUCLEI_USART_ADV_STATUS_LFCMF |
                                  NUCLEI_USART_ADV_STATUS_ABRF |
                                  NUCLEI_USART_ADV_STATUS_ABRE);
    s->rx_size_progress = 0;
}

static void nuclei_usart_mark_tx_busy(NucleiUSARTState *s, uint32_t half_bits,
                                      bool chained)
{
    int64_t now;
    int64_t base;
    int64_t delta;

    if (!s->tx_busy_timer) {
        return;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    base = MAX(now, s->tx_busy_deadline);
    delta = nuclei_usart_tx_busy_interval_ns(s, half_bits, chained);
    if (delta > INT64_MAX - base) {
        s->tx_busy_deadline = INT64_MAX;
    } else {
        s->tx_busy_deadline = base + delta;
    }

    s->usart_status |= NUCLEI_USART_STATUS_TX_BUSY;
    timer_mod(s->tx_busy_timer, s->tx_busy_deadline);
}

static void nuclei_usart_mark_rx_busy(NucleiUSARTState *s, uint32_t half_bits)
{
    int64_t now;
    int64_t base;
    int64_t delta;

    if (!s->rx_busy_timer) {
        return;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    base = MAX(now, s->rx_busy_deadline);
    delta = nuclei_usart_busy_visible_interval_ns(s, half_bits);
    if (delta > INT64_MAX - base) {
        s->rx_busy_deadline = INT64_MAX;
    } else {
        s->rx_busy_deadline = base + delta;
    }

    s->usart_status |= NUCLEI_USART_STATUS_RX_BUSY;
    s->rx_busy_interval_ns = delta;
    s->rx_busy_bonus_pending = true;
    timer_mod(s->rx_busy_timer, s->rx_busy_deadline);
}

static void nuclei_usart_maybe_cancel_rx_timers(NucleiUSARTState *s)
{
    if (!nuclei_usart_rx_enabled(s)) {
        nuclei_usart_cancel_rx_idle_timer(s);
        nuclei_usart_cancel_rx_wm_timer(s);
        return;
    }

    if (s->rx_fifo_len == 0 && nuclei_usart_idle_timeout_requires_rx_data(s)) {
        nuclei_usart_cancel_rx_idle_timer(s);
    }

    if (s->rx_fifo_len == 0) {
        nuclei_usart_cancel_rx_wm_timer(s);
        return;
    }

    if (s->rx_fifo_len <= nuclei_usart_rx_watermark(s)) {
        nuclei_usart_cancel_rx_wm_timer(s);
    }
}

static void nuclei_usart_arm_rx_idle_timer(NucleiUSARTState *s)
{
    if (!s->rx_idle_timer) {
        return;
    }

    if (!nuclei_usart_rx_enabled(s) ||
        (s->rx_fifo_len == 0 && nuclei_usart_idle_timeout_requires_rx_data(s)) ||
        (s->usart_status & NUCLEI_USART_STATUS_RX_IDLE_TOUT)) {
        nuclei_usart_cancel_rx_idle_timer(s);
        return;
    }

    timer_mod(s->rx_idle_timer,
              nuclei_usart_timeout_deadline(s, s->usart_rx_idle));
}

static void nuclei_usart_arm_rx_wm_timer(NucleiUSARTState *s)
{
    if (!s->rx_wm_timer) {
        return;
    }

    if (!nuclei_usart_rx_enabled(s) ||
        s->rx_fifo_len <= nuclei_usart_rx_watermark(s) ||
        (s->usart_status & NUCLEI_USART_STATUS_RX_WM_TOUT)) {
        nuclei_usart_cancel_rx_wm_timer(s);
        return;
    }

    timer_mod(s->rx_wm_timer,
              nuclei_usart_timeout_deadline(s, s->usart_rx_wm));
}

static void nuclei_usart_rearm_rx_timeout_timers(NucleiUSARTState *s)
{
    nuclei_usart_maybe_cancel_rx_timers(s);

    if (!nuclei_usart_rx_enabled(s)) {
        return;
    }

    nuclei_usart_arm_rx_idle_timer(s);
    nuclei_usart_arm_rx_wm_timer(s);
}

static void nuclei_usart_stop_lin_send_break(NucleiUSARTState *s)
{
    int break_enable = 0;
    bool was_active = nuclei_usart_lin_send_break_active(s);

    if (!was_active && !nuclei_usart_lin_send_break_requested(s)) {
        return;
    }

    s->usart_advanced_status &= ~NUCLEI_USART_ADV_STATUS_LIN_SBKF;

    if (!was_active) {
        return;
    }

    nuclei_usart_cancel_lin_break_timer(s);
    s->usart_status &= ~NUCLEI_USART_STATUS_TX_BUSY;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK, &break_enable);
}

static void nuclei_usart_maybe_abort_lin_send_break(NucleiUSARTState *s)
{
    if (!nuclei_usart_lin_break_send_enabled(s)) {
        nuclei_usart_stop_lin_send_break(s);
    }
}

static uint32_t nuclei_usart_status(NucleiUSARTState *s)
{
    uint32_t status = s->usart_status;

    if (s->tx_fifo_len <= nuclei_usart_tx_watermark(s)) {
        status |= NUCLEI_USART_STATUS_TXIP;
    }
    if (s->rx_fifo_len > nuclei_usart_rx_watermark(s)) {
        status |= NUCLEI_USART_STATUS_RXIP;
    }
    if (s->tx_fifo_len >= NUCLEI_USART_FIFO_DEPTH) {
        status |= NUCLEI_USART_STATUS_TX_FIFO_FULL;
    }
    if (s->tx_fifo_len == 0) {
        status |= NUCLEI_USART_STATUS_TX_FIFO_EMPTY;
    }
    if (s->rx_fifo_len == 0) {
        status |= NUCLEI_USART_STATUS_RX_FIFO_EMPTY;
    }
    if (s->rx_fifo_len >= NUCLEI_USART_FIFO_DEPTH) {
        status |= NUCLEI_USART_STATUS_RX_FIFO_FULL;
    }

    return status;
}

static uint32_t nuclei_usart_irq_pending(NucleiUSARTState *s)
{
    uint32_t pending = 0;
    uint32_t status = nuclei_usart_status(s);
    uint32_t adv_status = s->usart_advanced_status;

    if (status & NUCLEI_USART_STATUS_TXIP) {
        pending |= 1U << 0;
    }
    if (status & NUCLEI_USART_STATUS_RXIP) {
        pending |= 1U << 1;
    }
    if (status & NUCLEI_USART_STATUS_RX_ERROR_OVER_FLOW) {
        pending |= 1U << 2;
    }
    if (status & NUCLEI_USART_STATUS_RX_ERROR_PARITY) {
        pending |= 1U << 3;
    }
    if (status & NUCLEI_USART_STATUS_CTS_RISE_FLAG) {
        pending |= 1U << 4;
    }
    if (status & NUCLEI_USART_STATUS_CTS_FALL_FLAG) {
        pending |= 1U << 5;
    }
    if (status & NUCLEI_USART_STATUS_STOP_BIT_ERR_FLAG) {
        pending |= 1U << 6;
    }
    if (status & NUCLEI_USART_STATUS_SPI_SLV_UNDER_RUN) {
        pending |= 1U << 7;
    }
    if (status & NUCLEI_USART_STATUS_SPI_SLV_OVER_RUN) {
        pending |= 1U << 8;
    }
    if (status & NUCLEI_USART_STATUS_RX_IDLE_TOUT) {
        pending |= 1U << 9;
    }
    if (status & NUCLEI_USART_STATUS_RX_WM_TOUT) {
        pending |= 1U << 10;
    }
    if (status & NUCLEI_USART_STATUS_TX_EOT) {
        pending |= 1U << 11;
    }
    if (status & NUCLEI_USART_STATUS_RX_EOT) {
        pending |= 1U << 12;
    }
    if (status & NUCLEI_USART_STATUS_TX_FIFO_EMPTY) {
        pending |= 1U << 13;
    }
    if (status & NUCLEI_USART_STATUS_TX_FIFO_FULL) {
        pending |= 1U << 14;
    }
    if (status & NUCLEI_USART_STATUS_RX_FIFO_EMPTY) {
        pending |= 1U << 15;
    }
    if (status & NUCLEI_USART_STATUS_RX_FIFO_FULL) {
        pending |= 1U << 16;
    }
    if (status & NUCLEI_USART_STATUS_FRAME_ERR_FLAG) {
        pending |= 1U << 17;
    }
    if (adv_status & NUCLEI_USART_ADV_STATUS_LIN_LBDF) {
        pending |= 1U << 18;
    }
    if (nuclei_usart_modbus_event_enabled(s) &&
        (status & NUCLEI_USART_STATUS_RX_IDLE_TOUT)) {
        pending |= 1U << 19;
    }
    if (adv_status & NUCLEI_USART_ADV_STATUS_LFCMF) {
        pending |= 1U << 20;
    }

    return pending;
}

static void nuclei_usart_update_irq(NucleiUSARTState *s)
{
    uint32_t pending = nuclei_usart_irq_pending(s);

    if (s->usart_int_en & pending) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void nuclei_usart_rx_idle_timeout(void *opaque)
{
    NucleiUSARTState *s = opaque;

    if (!nuclei_usart_rx_enabled(s)) {
        return;
    }

    if (s->rx_fifo_len == 0 && nuclei_usart_idle_timeout_requires_rx_data(s)) {
        return;
    }

    s->usart_status |= NUCLEI_USART_STATUS_RX_IDLE_TOUT;
    nuclei_usart_update_irq(s);
}

static void nuclei_usart_tx_busy_done(void *opaque)
{
    NucleiUSARTState *s = opaque;

    s->tx_busy_deadline = 0;
    s->usart_status &= ~NUCLEI_USART_STATUS_TX_BUSY;

    /*
     * The first launched word keeps the FIFO/busy relationship guest-visible.
     * Once that window has completed, collapse any already-queued follow-on
     * bytes into a single transmitter-busy interval so SDK console traffic does
     * not pay one QEMU timer callback per character. TX_EOT is still only
     * exposed after the modeled transmitter becomes fully idle again.
     */
    if (nuclei_usart_tx_enabled(s) && s->tx_fifo_len != 0) {
        unsigned int queued_words = s->tx_fifo_len;
        unsigned int drained_words;

        drained_words = nuclei_usart_drain_tx_words(s, queued_words);
        if (drained_words != 0) {
            nuclei_usart_mark_tx_busy(s,
                                      nuclei_usart_frame_half_bits(s) *
                                      drained_words,
                                      true);
            nuclei_usart_update_irq(s);
            return;
        }
    }

    if (s->tx_eot_pending && s->tx_fifo_len == 0) {
        s->usart_status |= NUCLEI_USART_STATUS_TX_EOT;
        s->tx_eot_pending = false;
    }
    nuclei_usart_start_lin_send_break(s);
    nuclei_usart_update_irq(s);
}

static void nuclei_usart_rx_busy_done(void *opaque)
{
    NucleiUSARTState *s = opaque;

    /*
     * If the guest never sampled RX_BUSY during the initial window, keep one
     * extra functional interval so the bit is still observable later. This
     * bonus is single-use: once consumed, RX_BUSY must self-clear even if the
     * guest keeps ignoring STATUS.
     */
    if ((s->usart_status & NUCLEI_USART_STATUS_RX_BUSY) &&
        s->rx_busy_bonus_pending && s->rx_busy_interval_ns > 0) {
        s->rx_busy_bonus_pending = false;
        s->rx_busy_deadline =
            nuclei_usart_add_ns_saturating(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                                           s->rx_busy_interval_ns);
        timer_mod(s->rx_busy_timer, s->rx_busy_deadline);
        return;
    }

    s->rx_busy_deadline = 0;
    s->rx_busy_interval_ns = 0;
    s->usart_status &= ~NUCLEI_USART_STATUS_RX_BUSY;
    nuclei_usart_update_irq(s);
}

static void nuclei_usart_rx_wm_timeout(void *opaque)
{
    NucleiUSARTState *s = opaque;

    if (!nuclei_usart_rx_enabled(s) ||
        s->rx_fifo_len <= nuclei_usart_rx_watermark(s)) {
        return;
    }

    s->usart_status |= NUCLEI_USART_STATUS_RX_WM_TOUT;
    nuclei_usart_update_irq(s);
}

static void nuclei_usart_lin_break_done(void *opaque)
{
    NucleiUSARTState *s = opaque;
    int break_enable = 0;

    s->usart_advanced_status &= ~NUCLEI_USART_ADV_STATUS_LIN_SBKF;
    s->usart_status &= ~NUCLEI_USART_STATUS_TX_BUSY;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK, &break_enable);
    nuclei_usart_kick_tx(s);
    nuclei_usart_update_irq(s);
}

static uint64_t nuclei_usart_read(void *opaque, hwaddr offset,
                                  unsigned int size)
{
    NucleiUSARTState *s = opaque;
    uint16_t fifo_val;

    switch (offset) {
    case NUCLEI_USART_REG_TXDATA:
        return (s->txdata & NUCLEI_USART_DATA_MASK) |
               (s->tx_fifo_len >= NUCLEI_USART_FIFO_DEPTH ?
                NUCLEI_USART_LEGACY_TXFIFO_FULL : 0U);
    case NUCLEI_USART_REG_RXDATA:
        if (s->rx_fifo_len == 0) {
            return 0;
        }

        fifo_val = nuclei_usart_rx_fifo_pop(s);
        s->rxdata = fifo_val;
        qemu_chr_fe_accept_input(&s->chr);
        nuclei_usart_maybe_cancel_rx_timers(s);
        nuclei_usart_update_irq(s);
        return fifo_val & NUCLEI_USART_DATA_MASK;
    case NUCLEI_USART_REG_TXCTRL:
        return s->txctrl & NUCLEI_USART_CTRL_RMASK;
    case NUCLEI_USART_REG_RXCTRL:
        return s->rxctrl & NUCLEI_USART_CTRL_RMASK;
    case NUCLEI_USART_REG_INT_EN:
        return s->usart_int_en;
    case NUCLEI_USART_REG_STATUS:
        if (s->usart_status & NUCLEI_USART_STATUS_RX_BUSY) {
            /*
             * STATUS sampling itself is enough to count RX_BUSY as observed;
             * do not keep the backup window armed once software has seen it.
             */
            s->rx_busy_bonus_pending = false;
        }
        return nuclei_usart_status(s);
    case NUCLEI_USART_REG_DIV:
        return s->div & NUCLEI_USART_DIV_MASK;
    case NUCLEI_USART_REG_SETUP:
        return s->setup;
    case NUCLEI_USART_REG_RX_SIZE:
        return s->usart_rx_size;
    case NUCLEI_USART_REG_TX_SIZE:
        return s->usart_tx_size;
    case NUCLEI_USART_REG_SPI_SLAVE:
        return s->usart_spi_slave;
    case NUCLEI_USART_REG_RX_IDLE:
        return s->usart_rx_idle;
    case NUCLEI_USART_REG_RX_WM:
        return s->usart_rx_wm;
    case NUCLEI_USART_REG_RX_FIFO_LEFT_ENTRY:
        return s->rx_fifo_len & NUCLEI_USART_FIFO_COUNT_MASK;
    case NUCLEI_USART_REG_TX_FIFO_LEFT_ENTRY:
        return s->tx_fifo_len & NUCLEI_USART_FIFO_COUNT_MASK;
    case NUCLEI_USART_REG_TX_DATASIZE:
        return s->usart_tx_datasize;
    case NUCLEI_USART_REG_RX_DATASIZE:
        return s->usart_rx_datasize;
    case NUCLEI_USART_REG_SMARTCARD_SETUP:
        return s->usart_smartcard_setup;
    case NUCLEI_USART_REG_SMARTCARD_TIMING:
        return s->usart_smartcard_timing;
    case NUCLEI_USART_REG_IP_VERSION:
        return s->version;
    case NUCLEI_USART_REG_ADVANCED_SETUP:
        return s->usart_advanced_setup;
    case NUCLEI_USART_REG_ADVANCED_STATUS:
        return s->usart_advanced_status;
    default:
        return 0;
    }
}

static void nuclei_usart_receive_decoded_word(NucleiUSARTState *s, uint16_t value)
{
    uint16_t fifo_value;
    uint32_t unit_bytes;

    if (!nuclei_usart_rx_enabled(s)) {
        return;
    }

    nuclei_usart_mark_rx_busy(s, nuclei_usart_frame_half_bits(s));

    fifo_value = value & nuclei_usart_data_mask(s);
    if (!nuclei_usart_rx_fifo_push(s, fifo_value)) {
        if (!nuclei_usart_rx_overflow_latched(s)) {
            s->usart_status |= NUCLEI_USART_STATUS_RX_ERROR_OVER_FLOW;
            nuclei_usart_update_irq(s);
            printf("WARNING: UART dropped char.\n");
        }
        return;
    }

    s->rxdata = fifo_value;

    if (nuclei_usart_modbus_event_enabled(s) &&
        ((value & 0xffU) == NUCLEI_USART_MODBUS_ASCII_LF)) {
        s->usart_advanced_status |= NUCLEI_USART_ADV_STATUS_LFCMF;
    }

    if (s->usart_rx_size > 0) {
        unit_bytes = nuclei_usart_rx_size_unit_bytes(s);
        s->rx_size_progress++;
        if (s->rx_size_progress >= unit_bytes) {
            s->rx_size_progress = 0;
            s->usart_rx_size--;
            if (s->usart_rx_size == 0) {
                s->usart_status |= NUCLEI_USART_STATUS_RX_EOT;
            }
        }
    }

    nuclei_usart_arm_rx_idle_timer(s);
    nuclei_usart_arm_rx_wm_timer(s);
    nuclei_usart_update_irq(s);
}

static void nuclei_usart_latch_break(NucleiUSARTState *s)
{
    if (!nuclei_usart_rx_enabled(s)) {
        return;
    }

    /*
     * A host BREAK keeps RX low through what would otherwise be the stop-bit
     * interval, so it always qualifies for the stop-bit error latch. The spec
     * also describes this low-stop-bit line condition as a receive framing
     * error, so the minimal closed-loop model latches FRAME_ERR_FLAG for any
     * received BREAK. LIN mode then further promotes that same line condition
     * to the richer LIN break contract: LBDF + synthetic 0x00 data.
     */
    s->usart_status |= NUCLEI_USART_STATUS_STOP_BIT_ERR_FLAG;
    s->usart_status |= NUCLEI_USART_STATUS_FRAME_ERR_FLAG;

    if (!nuclei_usart_lin_mode_enabled(s)) {
        nuclei_usart_update_irq(s);
        return;
    }

    s->usart_advanced_status |= NUCLEI_USART_ADV_STATUS_LIN_LBDF;
    nuclei_usart_receive_decoded_word(s, 0);
    nuclei_usart_update_irq(s);
}

static void nuclei_usart_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned int size)
{
    NucleiUSARTState *s = opaque;
    uint32_t reg = value;
    bool rx_was_enabled;

    switch (offset) {
    case NUCLEI_USART_REG_TXDATA:
        if (!nuclei_usart_tx_fifo_push(s, reg)) {
            nuclei_usart_update_irq(s);
            break;
        }

        s->txdata = reg & nuclei_usart_data_mask(s);
        nuclei_usart_kick_tx(s);
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_TXCTRL:
        s->txctrl = reg & NUCLEI_USART_CTRL_RMASK;
        if (reg & NUCLEI_USART_CTRL_CLEAR) {
            s->txctrl &= ~NUCLEI_USART_CTRL_CLEAR;
            nuclei_usart_stop_lin_send_break(s);
            s->txdata = 0;
            nuclei_usart_tx_fifo_reset(s);
            nuclei_usart_clear_tx_busy(s);
            nuclei_usart_clear_tx_latched_status(s);
        }
        nuclei_usart_maybe_abort_lin_send_break(s);
        nuclei_usart_kick_tx(s);
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_RXCTRL:
        rx_was_enabled = nuclei_usart_rx_enabled(s);
        s->rxctrl = reg & NUCLEI_USART_CTRL_RMASK;
        if (reg & NUCLEI_USART_CTRL_CLEAR) {
            s->rxctrl &= ~NUCLEI_USART_CTRL_CLEAR;
            nuclei_usart_rx_fifo_reset(s);
            s->rxdata = 0;
            nuclei_usart_clear_rx_busy(s);
            nuclei_usart_clear_rx_latched_status(s);
            /*
             * If RXCTRL.CLR also re-enables reception, release any previously
             * backpressured host input only after the old guest-visible state
             * has been fully cleared. Otherwise a newly delivered byte could
             * be received first and then have its RX_BUSY/RX_EOT-style latches
             * wiped by the tail of the clear path.
             */
            if (nuclei_usart_rx_enabled(s)) {
                qemu_chr_fe_accept_input(&s->chr);
            }
        } else if (!rx_was_enabled && nuclei_usart_rx_enabled(s)) {
            qemu_chr_fe_accept_input(&s->chr);
        } else if (!nuclei_usart_rx_enabled(s)) {
            nuclei_usart_clear_rx_busy(s);
        }
        nuclei_usart_rearm_rx_timeout_timers(s);
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_INT_EN:
        s->usart_int_en = reg & NUCLEI_USART_INT_EN_MASK;
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_STATUS:
        s->usart_status &= ~(reg & NUCLEI_USART_STATUS_W1C_MASK);
        if (reg & NUCLEI_USART_STATUS_RX_IDLE_TOUT) {
            nuclei_usart_arm_rx_idle_timer(s);
        }
        if (reg & NUCLEI_USART_STATUS_RX_WM_TOUT) {
            nuclei_usart_arm_rx_wm_timer(s);
        }
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_DIV:
        s->div = reg & NUCLEI_USART_DIV_MASK;
        nuclei_usart_rearm_rx_timeout_timers(s);
        break;
    case NUCLEI_USART_REG_SETUP:
        s->setup = reg & NUCLEI_USART_SETUP_MASK;
        nuclei_usart_rearm_rx_timeout_timers(s);
        nuclei_usart_kick_tx(s);
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_RX_SIZE:
        nuclei_usart_write_rx_size(s, reg);
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_TX_SIZE:
        nuclei_usart_write_tx_size(s, reg);
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_SPI_SLAVE:
        s->usart_spi_slave = reg & NUCLEI_USART_SPI_SLAVE_MASK;
        break;
    case NUCLEI_USART_REG_RX_IDLE:
        s->usart_rx_idle = reg;
        nuclei_usart_rearm_rx_timeout_timers(s);
        break;
    case NUCLEI_USART_REG_RX_WM:
        s->usart_rx_wm = reg;
        nuclei_usart_rearm_rx_timeout_timers(s);
        break;
    case NUCLEI_USART_REG_TX_DATASIZE:
        s->usart_tx_datasize = reg & NUCLEI_USART_DATASIZE_MASK;
        s->tx_size_progress = 0;
        break;
    case NUCLEI_USART_REG_RX_DATASIZE:
        s->usart_rx_datasize = reg & NUCLEI_USART_DATASIZE_MASK;
        s->rx_size_progress = 0;
        break;
    case NUCLEI_USART_REG_SMARTCARD_SETUP:
        s->usart_smartcard_setup = reg & NUCLEI_USART_SMARTCARD_SETUP_MASK;
        break;
    case NUCLEI_USART_REG_SMARTCARD_TIMING:
        s->usart_smartcard_timing = reg & NUCLEI_USART_SMARTCARD_TIMING_MASK;
        break;
    case NUCLEI_USART_REG_ADVANCED_SETUP:
        s->usart_advanced_setup = reg & NUCLEI_USART_ADVANCED_SETUP_MASK;
        nuclei_usart_maybe_abort_lin_send_break(s);
        nuclei_usart_rearm_rx_timeout_timers(s);
        nuclei_usart_kick_tx(s);
        nuclei_usart_update_irq(s);
        break;
    case NUCLEI_USART_REG_ADVANCED_STATUS:
        s->usart_advanced_status &= ~NUCLEI_USART_ADVANCED_STATUS_RW_MASK;
        s->usart_advanced_status |= reg & NUCLEI_USART_ADVANCED_STATUS_RW_MASK;
        if (reg & NUCLEI_USART_ADVANCED_STATUS_W1S_MASK) {
            s->usart_advanced_status |= NUCLEI_USART_ADVANCED_STATUS_W1S_MASK;
            nuclei_usart_start_lin_send_break(s);
        }
        s->usart_advanced_status &= ~(reg &
                                      NUCLEI_USART_ADVANCED_STATUS_W1C_MASK);
        nuclei_usart_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps nuclei_usart_ops[3] = {
    [DEVICE_NATIVE_ENDIAN] = {
        .read = nuclei_usart_read,
        .write = nuclei_usart_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
        .valid = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    },
    [DEVICE_BIG_ENDIAN] = {
        .read = nuclei_usart_read,
        .write = nuclei_usart_write,
        .endianness = DEVICE_BIG_ENDIAN,
        .valid = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    },
    [DEVICE_LITTLE_ENDIAN] = {
        .read = nuclei_usart_read,
        .write = nuclei_usart_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid = {
            .min_access_size = 4,
            .max_access_size = 4,
        },
    },
};

static void nuclei_usart_rx_data(NucleiUSARTState *s, const uint8_t *buf)
{
    nuclei_usart_maybe_latch_abr(s, *buf);
    nuclei_usart_receive_decoded_word(
        s, nuclei_usart_apply_data_path_transforms(s, *buf));
}

static int nuclei_usart_can_rx(void *opaque)
{
    NucleiUSARTState *s = opaque;

    return nuclei_usart_rx_capacity(s);
}

static void nuclei_usart_rx(void *opaque, const uint8_t *buf, int size)
{
    NucleiUSARTState *s = opaque;
    int i;

    for (i = 0; i < size; i++) {
        if (nuclei_usart_rx_enabled(s) && nuclei_usart_rx_capacity(s) == 0) {
            break;
        }
        nuclei_usart_rx_data(s, &buf[i]);
    }
}

static void nuclei_usart_event(void *opaque, QEMUChrEvent event)
{
    NucleiUSARTState *s = opaque;

    switch (event) {
    case CHR_EVENT_BREAK:
        nuclei_usart_latch_break(s);
        break;
    default:
        break;
    }
}

static int nuclei_usart_be_change(void *opaque)
{
    NucleiUSARTState *s = opaque;

    qemu_chr_fe_set_handlers(&s->chr, nuclei_usart_can_rx, nuclei_usart_rx,
                             nuclei_usart_event, nuclei_usart_be_change,
                             s, NULL, true);
    return 0;
}

static void nuclei_usart_reset(DeviceState *dev)
{
    NucleiUSARTState *s = NUCLEI_USART(dev);

    nuclei_usart_stop_lin_send_break(s);
    nuclei_usart_cancel_tx_busy_timer(s);
    nuclei_usart_cancel_rx_busy_timer(s);
    nuclei_usart_cancel_rx_idle_timer(s);
    nuclei_usart_cancel_rx_wm_timer(s);
    nuclei_usart_cancel_lin_break_timer(s);
    nuclei_usart_tx_fifo_reset(s);
    nuclei_usart_rx_fifo_reset(s);
    s->txdata = 0;
    s->rxdata = 0;
    s->txctrl = 0;
    s->rxctrl = 0;
    s->div = 0;
    s->setup = 0xc0050030;
    s->usart_int_en = 0;
    s->usart_status = 0;
    s->usart_rx_size = 0;
    s->usart_tx_size = 0;
    s->usart_spi_slave = 0;
    s->usart_rx_idle = 0xffff;
    s->usart_rx_wm = 0xffff;
    s->usart_tx_datasize = 0;
    s->usart_rx_datasize = 0;
    s->usart_smartcard_setup = 0;
    s->usart_smartcard_timing = 0;
    s->usart_advanced_setup = NUCLEI_USART_ADVANCED_SETUP_RESET;
    s->usart_advanced_status = 0;
    s->tx_eot_pending = false;
    s->rx_busy_interval_ns = 0;
    s->rx_busy_bonus_pending = false;
    s->tx_size_progress = 0;
    s->rx_size_progress = 0;
    qemu_irq_lower(s->irq);
}

static void nuclei_usart_unrealize(DeviceState *dev)
{
    NucleiUSARTState *s = NUCLEI_USART(dev);

    nuclei_usart_stop_lin_send_break(s);
    timer_free(s->tx_busy_timer);
    s->tx_busy_timer = NULL;
    timer_free(s->rx_busy_timer);
    s->rx_busy_timer = NULL;
    timer_free(s->rx_idle_timer);
    s->rx_idle_timer = NULL;
    timer_free(s->rx_wm_timer);
    s->rx_wm_timer = NULL;
    timer_free(s->lin_break_timer);
    s->lin_break_timer = NULL;
}

static void nuclei_usart_realize(DeviceState *dev, Error **errp)
{
    NucleiUSARTState *s = NUCLEI_USART(dev);

    if (s->version != NUCLEI_USART_VERSION_3_1_0) {
        error_setg(errp,
                   "unsupported Nuclei USART version 0x%08x "
                   "(expected 0x%08x)",
                   s->version, NUCLEI_USART_VERSION_3_1_0);
        return;
    }

    warn_report_once("Nuclei USART version 0x%08x selected: register map is "
                     "enabled, but FIFO depth / timing / advanced USART "
                     "behavior remain partially modeled",
                     s->version);

    s->tx_busy_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    nuclei_usart_tx_busy_done, s);
    s->rx_busy_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    nuclei_usart_rx_busy_done, s);
    s->rx_idle_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    nuclei_usart_rx_idle_timeout, s);
    s->rx_wm_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  nuclei_usart_rx_wm_timeout, s);
    s->lin_break_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                      nuclei_usart_lin_break_done, s);
    qemu_chr_fe_set_handlers(&s->chr, nuclei_usart_can_rx, nuclei_usart_rx,
                             nuclei_usart_event, nuclei_usart_be_change,
                             s, NULL, true);
}

static Property nuclei_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", NucleiUSARTState, chr),
    DEFINE_PROP_UINT32("version", NucleiUSARTState, version,
                       NUCLEI_USART_VERSION_3_1_0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_usart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nuclei_usart_properties);
    dc->reset = nuclei_usart_reset;
    dc->realize = nuclei_usart_realize;
    dc->unrealize = nuclei_usart_unrealize;
    dc->desc = "Nuclei USART";
}

static const TypeInfo nuclei_usart_info = {
    .name = TYPE_NUCLEI_USART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiUSARTState),
    .class_init = nuclei_usart_class_init,
};

static void nuclei_usart_register_types(void)
{
    type_register_static(&nuclei_usart_info);
}

type_init(nuclei_usart_register_types);

NucleiUSARTState *nuclei_usart_create(hwaddr base, uint64_t size,
                      Chardev *chr, qemu_irq irq, uint32_t version,
                      bool big_endian)
{
    DeviceState *dev;
    NucleiUSARTState *s;
    SysBusDevice *sbd;

    dev = qdev_new(TYPE_NUCLEI_USART);
    sbd = SYS_BUS_DEVICE(dev);
    s = NUCLEI_USART(dev);

    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_prop_set_uint32(dev, "version", version);
    memory_region_init_io(&s->mmio, OBJECT(s),
                          &nuclei_usart_ops[big_endian ?
                                             DEVICE_BIG_ENDIAN :
                                             DEVICE_LITTLE_ENDIAN],
                          s,
                          TYPE_NUCLEI_USART, size);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
    sysbus_connect_irq(sbd, 0, irq);

    return s;
}
