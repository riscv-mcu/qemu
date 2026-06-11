/*
 * NUCLEI ECLIC(Enhanced Core Local Interrupt Controller)
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This provides a parameterizable interrupt controller based on Nuclei's ECLIC.
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "hw/pci/msi.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "target/riscv/cpu.h"
#include "sysemu/sysemu.h"
#include "hw/intc/nuclei_eclic.h"
#include "qapi/error.h"

#define RISCV_DEBUG_ECLIC 0
#define NUCLEI_ECLIC_VERSION 0x2

static inline uint8_t nuclei_eclic_get_ctlbits(const NucleiECLICState *eclic)
{
    return eclic->eclicintctlbits ? eclic->eclicintctlbits :
           NUCLEI_ECLIC_DEFAULT_INTCTLBITS;
}

/*
 * cliccfg.nlbits is software-visible state; values above CLICINTCTLBITS are
 * preserved and only the implemented effective portion participates in
 * level/priority decoding.
 */
static inline int nuclei_eclic_get_nlbits_field(const NucleiECLICState *eclic,
                                                int hartid)
{
    return (eclic->cliccfg[hartid] >> 1) & 0xf;
}

static inline int nuclei_eclic_get_nlbits(const NucleiECLICState *eclic,
                                          int hartid)
{
    return MIN(nuclei_eclic_get_nlbits_field(eclic, hartid),
               nuclei_eclic_get_ctlbits(eclic));
}

static inline uint8_t nuclei_eclic_get_nmbits(const NucleiECLICState *eclic,
                                              int hartid)
{
    RISCVCPU *cpu;

    if (hartid < 0 || hartid >= eclic->num_harts) {
        return 0;
    }

    cpu = RISCV_CPU(qemu_get_cpu(hartid));
    if (!cpu) {
        return 0;
    }

    /*
     * Nuclei exposes cliccfg.nmbits as a read-only capability bitfield:
     * it reads as 1 when supervisor-level interrupts are supported and 0
     * otherwise.
     */
    return riscv_has_ext(&cpu->env, RVS) ? 1 : 0;
}

static inline uint8_t nuclei_eclic_read_cfg(const NucleiECLICState *eclic,
                                            int hartid)
{
    return (nuclei_eclic_get_nlbits_field(eclic, hartid) << 1) |
           (nuclei_eclic_get_nmbits(eclic, hartid) << 5) | 0x1;
}

static inline bool nuclei_eclic_hart_has_smode(const NucleiECLICState *eclic,
                                                int hartid)
{
    return nuclei_eclic_get_nmbits(eclic, hartid) != 0;
}

static inline hwaddr nuclei_eclic_m_window_end(const NucleiECLICState *eclic,
                                               int hartid)
{
    uint32_t max_sources = eclic->num_sources;

    if (nuclei_eclic_hart_has_smode(eclic, hartid)) {
        max_sources = MIN(max_sources, 1024u);
    }

    return NUCLEI_ECLIC_REG_CLICINTIP_BASE + (hwaddr)max_sources * 4;
}

static inline hwaddr nuclei_eclic_s_window_end(const NucleiECLICState *eclic)
{
    uint32_t max_sources = MIN(eclic->num_sources, 1024u);

    return NUCLEI_ECLIC_REG_CLICINTIP_BASE_S + (hwaddr)max_sources * 4;
}

static inline uint8_t nuclei_eclic_decode_level(const NucleiECLICState *eclic,
                                                int hartid, uint8_t intctl)
{
    int nlbits = nuclei_eclic_get_nlbits(eclic, hartid);
    uint8_t mask_level;
    uint8_t mask_padding;

    if (nlbits == 0) {
        return UINT8_MAX;
    }

    mask_level = ((1u << nlbits) - 1u) << (8 - nlbits);
    mask_padding = (1u << (8 - nlbits)) - 1u;

    return (intctl & mask_level) | mask_padding;
}

static inline uint8_t nuclei_eclic_decode_priority(const NucleiECLICState *eclic,
                                                   int hartid, uint8_t intctl)
{
    int nlbits = nuclei_eclic_get_nlbits(eclic, hartid);

    if (nlbits >= nuclei_eclic_get_ctlbits(eclic)) {
        return 0;
    }

    /*
     * Keep the priority portion left-justified so a simple numeric compare
     * matches the ordering implied by the raw clicintctl encoding.
     */
    return (uint8_t)(((uint32_t)intctl << nlbits) | ((1u << nlbits) - 1u));
}

static inline uint8_t nuclei_eclic_read_intattr(uint8_t intattr)
{
    return intattr & ~0x38;
}

static inline uint8_t nuclei_eclic_read_intie(uint8_t intie)
{
    return intie & 0x1;
}

static inline uint8_t nuclei_eclic_read_intctl(const NucleiECLICState *eclic,
                                               uint8_t intctl)
{
    uint8_t ctlbits = nuclei_eclic_get_ctlbits(eclic);

    if (ctlbits >= 8) {
        return intctl;
    }

    return intctl | ((1u << (8 - ctlbits)) - 1u);
}

static inline bool nuclei_eclic_ip_writable_from_software(int trigger)
{
    /* Spec: software writes to IP are ignored for level-triggered sources. */
    return trigger & 0x1;
}

static void nuclei_eclic_update_intmth(NucleiECLICState *eclic, int irq, int hartid, int mth);
static void nuclei_eclic_update_irq_input(NucleiECLICState *eclic, int irq,
                                          int hartid, int new_signal);
static void nuclei_eclic_write_intip(NucleiECLICState *eclic, int irq,
                                     int hartid, int new_intip);
static void nuclei_eclic_update_intie(NucleiECLICState *eclic, int irq,
                                      int hartid, int new_intie);
static void nuclei_eclic_update_intattr(NucleiECLICState *eclic, int irq,
                                        int hartid, int new_intattr);
static void nuclei_eclic_update_intattr_s_view(NucleiECLICState *eclic, int irq,
                                               int hartid, int new_intattr);
static void nuclei_eclic_update_intctl(NucleiECLICState *eclic, int irq,
                                       int hartid, int new_intctl);
static void eclic_insert_pending_list(NucleiECLICState *eclic, int irq, int hartid);
static void eclic_remove_pending_list(NucleiECLICState *eclic, int irq, int hartid);
static void update_eclic_int_info(NucleiECLICState *eclic, int irq, int hartid);
static void nuclei_eclic_update_intsth(NucleiECLICState *eclic, int irq, int hartid, int sth);
static void nuclei_eclic_update_pending_state(NucleiECLICState *eclic, int irq,
                                              int hartid, bool pending);

struct NucleiECLICExternalRoute {
    DeviceState *dev;   /* Owning ECLIC device. */
    uint32_t irq;       /* Shared external source ID. */
    int asserted_hart;  /* Hart currently seeing the asserted level, or -1. */
    bool level;         /* Last sampled raw external level. */
    NucleiECLICExternalRoute *next;
};

/*
 * Evalsoc distinguishes its local interrupt controller model through the
 * low mtvec bits: 0b000011 selects ECLIC, other values keep the legacy CLINT
 * interpretation.
 */
bool riscv_intc_is_clic_mode(CPUArchState *env)
{
    /* Current evalsoc wiring marks ECLIC mode through the low mtvec pattern. */
    return env->eclic && ((env->mtvec & 0x3F) == 3);
}

bool riscv_intc_is_eclicv2_mode(CPUArchState *env)
{
    return env->eclic && (env->mmisc_ctl & (1U << 21));
}

void shadow_gpr_push(CPUArchState *env, uint8_t gpr_grp, uint8_t fpr_grp,
                     uint8_t frame_restore_mask, bool tsp_swapped)
{
    RISCVEclicShadowState *shadow = &env->eclic_shadow;

    /* Record which shadow group was active and which parts of the frame were
     * stack-saved so popxret() can later unwind the nesting chain precisely.
     */
    if (shadow->grp_stack_top < (int)ARRAY_SIZE(shadow->grp_stack) - 1) {
        shadow->grp_stack_top++;
        shadow->grp_stack[shadow->grp_stack_top].gpr_grp_index = gpr_grp;
        shadow->grp_stack[shadow->grp_stack_top].fpr_grp_index = fpr_grp;
        shadow->grp_stack[shadow->grp_stack_top].frame_restore_mask =
            frame_restore_mask;
        shadow->grp_stack[shadow->grp_stack_top].tsp_swapped = tsp_swapped;
    } else {
        error_report("ECLIC trap context stack overflow\n");
        exit(1);
    }
}

void shadow_gpr_pop(CPUArchState *env)
{
    RISCVEclicShadowState *shadow = &env->eclic_shadow;
    if (shadow->grp_stack_top >= 0) {
        shadow->grp_stack_top--;
    }
}

int get_shadow_gpr_stack_size(CPUArchState *env)
{
    return env->eclic_shadow.grp_stack_top + 1;
}

static void riscv_shadow_save_gpr_bank(CPUArchState *env, uint8_t grp_index)
{
    RISCVEclicShadowState *shadow = &env->eclic_shadow;

    for (int i = 0; i < SHADOW_GPR_COUNT; i++) {
        uint8_t reg_idx = context_regs[i];

        shadow->gpr_banks[grp_index][reg_idx] = env->gpr[reg_idx];
    }
}

static void riscv_shadow_load_gpr_bank(CPUArchState *env, uint8_t grp_index)
{
    RISCVEclicShadowState *shadow = &env->eclic_shadow;

    for (int i = 0; i < SHADOW_GPR_COUNT; i++) {
        uint8_t reg_idx = context_regs[i];

        /* RV32E still stores a full bank image; only the inactive
         * architectural registers remain ignored by the core itself.
         */
        env->gpr[reg_idx] = shadow->gpr_banks[grp_index][reg_idx];
    }
}

static void riscv_shadow_save_fpr_bank(CPUArchState *env, uint8_t grp_index)
{
    RISCVEclicShadowState *shadow = &env->eclic_shadow;

    for (int i = 0; i < SHADOW_FPR_COUNT; i++) {
        uint8_t reg_idx = fpu_context_regs[i];

        shadow->fpr_banks[grp_index][reg_idx] = env->fpr[reg_idx];
    }
    shadow->fcsr_banks[grp_index] =
        (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT) |
        (env->frm << FSR_RD_SHIFT);
}

static void riscv_shadow_load_fpr_bank(CPUArchState *env, uint8_t grp_index)
{
    RISCVEclicShadowState *shadow = &env->eclic_shadow;
    target_ulong fcsr = shadow->fcsr_banks[grp_index];

    for (int i = 0; i < SHADOW_FPR_COUNT; i++) {
        uint8_t reg_idx = fpu_context_regs[i];

        env->fpr[reg_idx] = shadow->fpr_banks[grp_index][reg_idx];
    }
    env->frm = (fcsr & FSR_RD) >> FSR_RD_SHIFT;
    riscv_cpu_set_fflags(env, (fcsr & FSR_AEXC) >> FSR_AEXC_SHIFT);
}

/* Switch to the specified register group */
void riscv_shadow_gpr_switch_grp(CPUArchState *env, uint8_t gpr_grp_index,
                                 uint8_t fpr_grp_index)
{
    RISCVEclicShadowState *shadow;
    uint8_t old_gpr_grp, old_fpr_grp;

    if (!env) {
        return;
    }
    shadow = &env->eclic_shadow;
    old_gpr_grp = shadow->current_gpr_grp;
    old_fpr_grp = shadow->current_fpr_grp;

    if (gpr_grp_index == old_gpr_grp && fpr_grp_index == old_fpr_grp) {
        return;
    }

    if (gpr_grp_index != old_gpr_grp) {
        riscv_shadow_save_gpr_bank(env, old_gpr_grp);
    }
    if (fpr_grp_index != old_fpr_grp) {
        riscv_shadow_save_fpr_bank(env, old_fpr_grp);
    }
    if (gpr_grp_index != old_gpr_grp) {
        riscv_shadow_load_gpr_bank(env, gpr_grp_index);
    }
    if (fpr_grp_index != old_fpr_grp) {
        riscv_shadow_load_fpr_bank(env, fpr_grp_index);
    }
    shadow->current_gpr_grp = gpr_grp_index;
    shadow->current_fpr_grp = fpr_grp_index;
    shadow->current_grp = MAX(gpr_grp_index, fpr_grp_index);
}

/* Backup shadow gpr for interrupt return use */
void riscv_backup_shadow_gpr(CPUArchState *env, uint8_t gpr_grp_index,
                             uint8_t fpr_grp_index)
{
    if (gpr_grp_index < TOTAL_GPR_GROUPS) {
        riscv_shadow_save_gpr_bank(env, gpr_grp_index);
    }
    if (fpr_grp_index < TOTAL_GPR_GROUPS) {
        riscv_shadow_save_fpr_bank(env, fpr_grp_index);
    }
}

qemu_irq nuclei_eclic_get_irq(DeviceState *dev, int irq, int hartid)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);

    return qdev_get_gpio_in(dev, hartid * eclic->num_sources + irq);
}

bool nuclei_eclic_irq_enabled(DeviceState *dev, uint32_t irq, int hartid)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);

    if (hartid < 0 || hartid >= eclic->num_harts || irq >= eclic->num_sources) {
        return false;
    }

    return eclic->clicintie[hartid][irq] & 0x1;
}

static int nuclei_eclic_select_hart(DeviceState *dev, uint32_t irq)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);
    int hartid;

    for (hartid = 0; hartid < eclic->num_harts; hartid++) {
        if (nuclei_eclic_irq_enabled(dev, irq, hartid)) {
            return hartid;
        }
    }

    return -1;
}

static void nuclei_eclic_external_route_sync(NucleiECLICExternalRoute *route)
{
    int new_hart = -1;

    /*
     * In SMP topologies without CIDU, a level-sensitive external source may
     * need to move between harts while the line stays asserted.
     */
    if (route->level) {
        new_hart = nuclei_eclic_select_hart(route->dev, route->irq);
    }

    if (new_hart == route->asserted_hart) {
        return;
    }

    if (route->asserted_hart >= 0) {
        qemu_set_irq(nuclei_eclic_get_irq(route->dev, route->irq,
                                          route->asserted_hart), 0);
        route->asserted_hart = -1;
    }

    if (new_hart >= 0) {
        qemu_set_irq(nuclei_eclic_get_irq(route->dev, route->irq, new_hart), 1);
        route->asserted_hart = new_hart;
    }
}

static void nuclei_eclic_sync_external_routes(NucleiECLICState *eclic, int irq)
{
    NucleiECLICExternalRoute *route;

    if (irq < 0 || irq >= eclic->num_sources) {
        return;
    }

    for (route = eclic->external_routes[irq]; route; route = route->next) {
        nuclei_eclic_external_route_sync(route);
    }
}

static void nuclei_eclic_external_irq_handler(void *opaque, int n, int level)
{
    NucleiECLICExternalRoute *route = opaque;
    (void)n;

    route->level = !!level;
    nuclei_eclic_external_route_sync(route);
}

qemu_irq nuclei_eclic_get_external_irq(DeviceState *dev, int irq)
{
    NucleiECLICExternalRoute *route;
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);

    if (irq < 0 || irq >= eclic->num_sources ||
        irq >= INTERRUPT_SOURCE_MAX_ID) {
        error_report("%s: invalid external irq %d (num_sources=%u)",
                     __func__, irq, eclic->num_sources);
        return NULL;
    }

    route = g_new0(NucleiECLICExternalRoute, 1);
    route->dev = dev;
    route->irq = irq;
    route->asserted_hart = -1;
    route->next = eclic->external_routes[irq];
    eclic->external_routes[irq] = route;

    return qemu_allocate_irq(nuclei_eclic_external_irq_handler, route, 0);
}

static inline int nuclei_eclic_get_current_cpu(NucleiECLICState *eclic)
{
    /* ECLIC register banks are per-hart. During reset-time accesses, default
     * to hart 0 if QEMU has not yet established current_cpu.
     */
    if (eclic->num_harts > 1)
    {
        return current_cpu ? current_cpu->cpu_index : 0;
    }
    return 0;
}

static void nuclei_eclic_sync_cpu_thresholds(NucleiECLICState *eclic, int hartid)
{
    RISCVCPU *cpu;

    if (hartid < 0 || hartid >= eclic->num_harts) {
        return;
    }

    cpu = RISCV_CPU(qemu_get_cpu(hartid));
    if (!cpu) {
        return;
    }

    /* Keep the CPU-side CSR mirrors synchronized even when software programs
     * thresholds through the ECLIC MMIO window instead of CSR writes.
     */
    cpu->env.mintthresh = eclic->mth[hartid];
    cpu->env.sintthresh = eclic->sth[hartid];
}

static inline int nuclei_eclic_irq_mode(const NucleiECLICState *eclic,
                                        int hartid, int irq)
{
    uint8_t mode = (eclic->clicintattr[hartid][irq] >> 6) & 0x3;

    /* Only M and S delivery modes are architecturally visible; unsupported or
     * reserved encodings collapse back to M-mode ownership.
     */
    if (mode == PRV_S && nuclei_eclic_get_nmbits(eclic, hartid) != 0) {
        return PRV_S;
    }

    return PRV_M;
}

static inline bool nuclei_eclic_s_view_access_allowed(const NucleiECLICState *eclic,
                                                      int hartid, int irq)
{
    /* The S-window only exposes interrupts currently owned by S-mode. */
    return nuclei_eclic_irq_mode(eclic, hartid, irq) == PRV_S;
}

static uint64_t nuclei_eclic_read(void *opaque, hwaddr offset, unsigned size)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(opaque);
    uint64_t value = 0;
    uint32_t irq = 0;
    uint32_t shift = 0;
    uint32_t hartid = nuclei_eclic_get_current_cpu(eclic);
    bool s_view = false;
    bool s_visible = false;

    if (nuclei_eclic_hart_has_smode(eclic, hartid) &&
        offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE_S &&
        offset < nuclei_eclic_s_window_end(eclic)) {
        s_view = true;
        shift = offset & 0x3;
        irq = (offset - shift - 0x3000) / 4;
        offset = offset - 4 * irq;
    } else if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE &&
               offset < nuclei_eclic_m_window_end(eclic, hartid)) {
        shift = offset & 0x3;
        irq = (offset - shift - 0x1000) / 4;
        offset = offset - 4 * irq;
    }

    if (s_view) {
        s_visible = nuclei_eclic_s_view_access_allowed(eclic, hartid, irq);
    }

    switch (offset) {
    case NUCLEI_ECLIC_REG_CLICCFG:
        value = nuclei_eclic_read_cfg(eclic, hartid);
        break;
    case NUCLEI_ECLIC_REG_CLICINFO:
        /*
         * The ECLIC model already exposes v2-only capabilities such as
         * shadow register groups, so report version 2 to software.
         */
        value = (eclic->shadow_gpr_num << 25) |
                (nuclei_eclic_get_ctlbits(eclic) << 21) |
                (NUCLEI_ECLIC_VERSION << 13) | eclic->num_sources;
        break;
    case NUCLEI_ECLIC_REG_MINTTHRESH:
        value = ((uint32_t)eclic->sth[hartid] << 8) |
                ((uint32_t)eclic->mth[hartid] << 24);
        break;
    case NUCLEI_ECLIC_REG_MINTTHRESH_HI:
        value = (uint32_t)eclic->mth[hartid] << 8;
        break;
    case NUCLEI_ECLIC_REG_STH:
        value = eclic->sth[hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_MTH:
        value = eclic->mth[hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE:
        if (size == 4) {
            value = (uint32_t)eclic->clicintip[hartid][irq] |
                    ((uint32_t)nuclei_eclic_read_intie(
                        eclic->clicintie[hartid][irq]) << 8) |
                    ((uint32_t)nuclei_eclic_read_intattr(
                        eclic->clicintattr[hartid][irq]) << 16) |
                    ((uint32_t)nuclei_eclic_read_intctl(
                        eclic, eclic->clicintctl[hartid][irq]) << 24);
        } else if (size == 2) {
            value = (uint32_t)eclic->clicintip[hartid][irq] |
                    ((uint32_t)nuclei_eclic_read_intie(
                        eclic->clicintie[hartid][irq]) << 8);
        } else {
            value = eclic->clicintip[hartid][irq] & 0xFF;
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE:
        value = nuclei_eclic_read_intie(eclic->clicintie[hartid][irq]);
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE:
        if (size == 2) {
            value = ((uint32_t)nuclei_eclic_read_intattr(
                        eclic->clicintattr[hartid][irq])) |
                    ((uint32_t)nuclei_eclic_read_intctl(
                        eclic, eclic->clicintctl[hartid][irq]) << 8);
        } else {
            value = nuclei_eclic_read_intattr(eclic->clicintattr[hartid][irq]);
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE:
        value = nuclei_eclic_read_intctl(eclic, eclic->clicintctl[hartid][irq]);
        break;
    case NUCLEI_ECLIC_REG_SINTTHRESH:
        value = ((uint32_t)eclic->sth[hartid] << 8);
        break;
    case NUCLEI_ECLIC_REG_SSTH:
        value = eclic->sth[hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE_S:
        if (size == 4) {
            value = (s_visible ? (uint32_t)eclic->clicintip[hartid][irq] : 0) |
                    (s_visible ? ((uint32_t)nuclei_eclic_read_intie(
                        eclic->clicintie[hartid][irq]) << 8) : 0) |
                    (s_visible ? ((uint32_t)nuclei_eclic_read_intattr(
                        eclic->clicintattr[hartid][irq]) << 16) : 0) |
                    (s_visible ? ((uint32_t)nuclei_eclic_read_intctl(
                        eclic, eclic->clicintctl[hartid][irq]) << 24) : 0);
        } else if (size == 2) {
            value = (s_visible ? (uint32_t)eclic->clicintip[hartid][irq] : 0) |
                    (s_visible ? ((uint32_t)nuclei_eclic_read_intie(
                        eclic->clicintie[hartid][irq]) << 8) : 0);
        } else {
            value = s_visible ? (eclic->clicintip[hartid][irq] & 0xFF) : 0;
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE_S:
        value = s_visible ? nuclei_eclic_read_intie(
                                eclic->clicintie[hartid][irq]) : 0;
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE_S:
        if (size == 2) {
            value = s_visible ?
                    ((uint32_t)nuclei_eclic_read_intattr(
                        eclic->clicintattr[hartid][irq])) |
                    ((uint32_t)nuclei_eclic_read_intctl(
                        eclic, eclic->clicintctl[hartid][irq]) << 8) : 0;
        } else {
            value = s_visible ? nuclei_eclic_read_intattr(
                                    eclic->clicintattr[hartid][irq]) : 0;
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE_S:
        value = s_visible ? nuclei_eclic_read_intctl(
                                eclic, eclic->clicintctl[hartid][irq]) : 0;
        break;
    default:
        break;
    }

    return value;
}

/*
 * ECLIC MMIO accesses first normalize source-window addresses back to the
 * corresponding IP/IE/ATTR/CTL lane base. M-window and S-window then update
 * the same backing arrays, with the S-window additionally filtered by IRQ
 * mode visibility.
 */
static void nuclei_eclic_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(opaque);
    uint32_t irq = 0;
    uint32_t hartid = nuclei_eclic_get_current_cpu(eclic);
    uint32_t shift = 0;
    bool s_view = false;
    bool s_allowed = false;

    if (nuclei_eclic_hart_has_smode(eclic, hartid) &&
        offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE_S &&
        offset < nuclei_eclic_s_window_end(eclic)) {
        s_view = true;
        shift = offset & 0x3;
        irq = (offset - shift - 0x3000) / 4;
        offset = offset - 4 * irq;
    } else if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE &&
               offset < nuclei_eclic_m_window_end(eclic, hartid)) {
        shift = offset & 0x3;
        irq = (offset - shift - 0x1000) / 4;
        offset = offset - 4 * irq;
    }
    if (s_view) {
        s_allowed = nuclei_eclic_s_view_access_allowed(eclic, hartid, irq);
    }

    switch (offset) {
    case NUCLEI_ECLIC_REG_CLICCFG:
        eclic->cliccfg[hartid] = ((value >> 1) & 0xF) << 1;
        for (irq = 0; irq < eclic->num_sources; irq++) {
            update_eclic_int_info(eclic, irq, hartid);
        }
        break;
    case NUCLEI_ECLIC_REG_MINTTHRESH:
        if (size == 4) {
            nuclei_eclic_update_intsth(eclic, irq, hartid, (value >> 8) & 0xFF);
            nuclei_eclic_update_intmth(eclic, irq, hartid, (value >> 24) & 0xFF);
        } else if (size == 2) {
            nuclei_eclic_update_intsth(eclic, irq, hartid, (value >> 8) & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_MINTTHRESH_HI:
        if (size == 2) {
            nuclei_eclic_update_intmth(eclic, irq, hartid, (value >> 8) & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_STH:
        nuclei_eclic_update_intsth(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_MTH:
        nuclei_eclic_update_intmth(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE:
        /* Handle aligned source-window writes (IP/IE[/ATTR/CTL]) symmetrically
         * with the composite readback format. */
        nuclei_eclic_write_intip(eclic, irq, hartid, value & 0x1);
        if (size >= 2) {
            nuclei_eclic_update_intie(eclic, irq, hartid, (value >> 8) & 0xFF);
        }
        if (size == 4) {
            nuclei_eclic_update_intattr(eclic, irq, hartid, (value >> 16) & 0xFF);
            nuclei_eclic_update_intctl(eclic, irq, hartid, (value >> 24) & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE:
        nuclei_eclic_update_intie(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE:
        nuclei_eclic_update_intattr(eclic, irq, hartid, value & 0xFF);
        if (size == 2) {
            nuclei_eclic_update_intctl(eclic, irq, hartid, (value >> 8) & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE:
        nuclei_eclic_update_intctl(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_SINTTHRESH:
        if (size == 4 || size == 2) {
            nuclei_eclic_update_intsth(eclic, irq, hartid, (value >> 8) & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_SSTH:
        nuclei_eclic_update_intsth(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE_S:
        if (s_allowed) {
            nuclei_eclic_write_intip(eclic, irq, hartid, value & 0x1);
            if (size >= 2) {
                nuclei_eclic_update_intie(eclic, irq, hartid,
                                          (value >> 8) & 0xFF);
            }
            if (size == 4) {
                nuclei_eclic_update_intattr_s_view(eclic, irq, hartid,
                                                   (value >> 16) & 0xFF);
                nuclei_eclic_update_intctl(eclic, irq, hartid,
                                           (value >> 24) & 0xFF);
            }
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE_S:
        if (s_allowed) {
            nuclei_eclic_update_intie(eclic, irq, hartid, value & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE_S:
        if (s_allowed) {
            nuclei_eclic_update_intattr_s_view(eclic, irq, hartid,
                                               value & 0xFF);
            if (size == 2) {
                nuclei_eclic_update_intctl(eclic, irq, hartid,
                                           (value >> 8) & 0xFF);
            }
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE_S:
        if (s_allowed) {
            nuclei_eclic_update_intctl(eclic, irq, hartid, value & 0xFF);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps nuclei_eclic_ops = {
    .read = nuclei_eclic_read,
    .write = nuclei_eclic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static Property nuclei_eclic_properties[] = {
    DEFINE_PROP_BOOL("vector", NucleiECLICState, nvbits, false),
    DEFINE_PROP_UINT32("num-harts", NucleiECLICState, num_harts, 0),
    DEFINE_PROP_UINT32("eclicintctlbits", NucleiECLICState, eclicintctlbits,
                       NUCLEI_ECLIC_DEFAULT_INTCTLBITS),
    DEFINE_PROP_UINT32("aperture-size", NucleiECLICState, aperture_size, 0),
    DEFINE_PROP_UINT32("num-sources", NucleiECLICState, num_sources, 0),
    DEFINE_PROP_UINT64("mclicbase", NucleiECLICState, mclicbase, 0),
    DEFINE_PROP_UINT32("shadow-gpr-num", NucleiECLICState, shadow_gpr_num, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static int level_compare(const ECLICPendingInterrupt *irq1,
                         const ECLICPendingInterrupt *irq2)
{
    /* Spec order: mode > level > priority > irq id. The pending list keeps
     * this global order, while threshold and current privilege are checked
     * later when the hart asks for the first deliverable interrupt.
     */
    if (irq1->mode != irq2->mode) {
        return irq1->mode > irq2->mode ? -1 : 1;
    }

    if (irq1->level != irq2->level) {
        return irq1->level > irq2->level ? -1 : 1;
    }

    if (irq1->prio != irq2->prio) {
        return irq1->prio > irq2->prio ? -1 : 1;
    }

    if (irq1->irq != irq2->irq) {
        return irq1->irq > irq2->irq ? -1 : 1;
    }

    return 0;
}

static void eclic_remove_pending_list(NucleiECLICState *eclic, int irq,
                                      int hartid)
{
    ECLICPendingInterrupt *entry = &eclic->clicintlist[hartid][irq];

    if (!entry->pending) {
        return;
    }

    QLIST_REMOVE(entry, next);
    entry->pending = false;
}

static void update_eclic_int_info(NucleiECLICState *eclic, int irq, int hartid)
{
    ECLICPendingInterrupt *entry = &eclic->clicintlist[hartid][irq];
    bool was_pending = entry->pending;

    if (was_pending) {
        eclic_remove_pending_list(eclic, irq, hartid);
    }

    entry->mode = nuclei_eclic_irq_mode(eclic, hartid, irq);
    entry->level = nuclei_eclic_decode_level(eclic, hartid,
                                             eclic->clicintctl[hartid][irq]);
    entry->prio = nuclei_eclic_decode_priority(eclic, hartid,
                                               eclic->clicintctl[hartid][irq]);
    entry->enable = eclic->clicintie[hartid][irq] & 0x1;
    /* 0/2: level triggered, 1: rising edge, 3: falling edge */
    entry->trigger = (eclic->clicintattr[hartid][irq] >> 1) & 0x3;

    /* Unified M/S storage means any visible attribute change can reshuffle an
     * already-pending source inside the single mode-aware queue.
     */
    if (was_pending && eclic->clicintip[hartid][irq]) {
        eclic_insert_pending_list(eclic, irq, hartid);
    }
}

bool nuclei_eclic_shv_interrupt(void *opaque, int hartid, int irq)
{
    NucleiECLICState *eclic = (NucleiECLICState *)opaque;

    return eclic->clicintattr[hartid][irq] & 0x1;
}

bool nuclei_eclic_edge_triggered(void *opaque, int hartid, int irq)
{
    NucleiECLICState *eclic = (NucleiECLICState *)opaque;

    return (eclic->clicintattr[hartid][irq] >> 1) & 0x1;
}

void nuclei_eclic_clean_pending(void *opaque, int hartid, int irq)
{
    NucleiECLICState *eclic = (NucleiECLICState *)opaque;
    eclic->clicintip[hartid][irq] = 0;
    eclic_remove_pending_list(eclic, irq, hartid);
}

static ECLICPendingInterrupt *eclic_first_deliverable(NucleiECLICState *eclic,
                                                      int current_priv,
                                                      int hartid)
{
    ECLICPendingInterrupt *active;
    uint8_t threshold;

    QLIST_FOREACH(active, &eclic->pending_list[hartid], next) {
        /*
         * Spec 19.3.5: a supervisor-level interrupt occurring while the hart
         * executes in M-mode cannot be taken. Keep it pending until the hart
         * later drops below M-mode.
         */
        if (current_priv == PRV_M && active->mode == PRV_S) {
            continue;
        }

        /* Threshold selection follows the target delivery mode of the entry,
         * not the hart's current privilege.
         */
        threshold = (active->mode <= PRV_S) ?
                    eclic->sth[hartid] : eclic->mth[hartid];
        if (active->enable && active->level > threshold) {
            return active;
        }
    }

    return NULL;
}

void nuclei_eclic_next_interrupt(void *eclic_ptr, int hartid)
{
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(hartid));
    NucleiECLICState *eclic = (NucleiECLICState *)eclic_ptr;
    ECLICPendingInterrupt *best;
    int exccode;

    /* Re-evaluate from the ordered queue every time. That keeps the delivery
     * path mode-aware even though pending state itself is stored only once.
     */
    best = eclic_first_deliverable(eclic, cpu->env.priv, hartid);

    if (best) {
        exccode = best->irq | best->mode << 12 | best->level << 14;
        eclic->exccode[hartid] = exccode;
        riscv_cpu_eclic_interrupt(cpu, exccode);
        return;
    }

    eclic->exccode[hartid] = 0;
    riscv_cpu_eclic_interrupt(cpu, -1);
}

uint8_t nuclei_eclic_get_threshold(void *opaque, int mode, int hartid)
{
    NucleiECLICState *eclic = opaque;

    if (!eclic || hartid < 0 || hartid >= eclic->num_harts) {
        return 0;
    }

    return (mode <= PRV_S) ? eclic->sth[hartid] : eclic->mth[hartid];
}

void nuclei_eclic_set_threshold(void *opaque, int mode, int hartid,
                                uint8_t threshold)
{
    NucleiECLICState *eclic = opaque;

    if (!eclic || hartid < 0 || hartid >= eclic->num_harts) {
        return;
    }

    if (mode <= PRV_S) {
        nuclei_eclic_update_intsth(eclic, 0, hartid, threshold);
    } else {
        nuclei_eclic_update_intmth(eclic, 0, hartid, threshold);
    }
}

void riscv_cpu_eclic_int_handler_start(void *eclic_ptr, int irq, int hartid)
{
    NucleiECLICState *eclic = (NucleiECLICState *)eclic_ptr;

    /* Edge-triggered sources self-clear once the hart has committed to taking
     * the interrupt; level-triggered ones remain coupled to the input signal.
     */
    if ((eclic->clicintlist[hartid][irq].trigger & 0x1) != 0) {
        eclic->clicintip[hartid][irq] = 0;
        eclic_remove_pending_list(eclic, irq, hartid);
    }
    nuclei_eclic_next_interrupt(eclic, hartid);
}


static void nuclei_eclic_set_irq(void *opaque, int n, int level)
{
    NucleiECLICState *eclic = opaque;
    int hartid = n / eclic->num_sources;
    int id = n % eclic->num_sources;

    nuclei_eclic_update_irq_input(eclic, id, hartid, level);
}

void nuclei_eclic_irq_request(void *opaque, int id, int new_intip)
{
    CPURISCVState *env = (CPURISCVState *)opaque;
    NucleiECLICState *eclic = env->eclic;
    CPUState *cpu = env_cpu(env);

    nuclei_eclic_update_irq_input(eclic, id, cpu->cpu_index, new_intip);
}

static void nuclei_eclic_update_intmth(NucleiECLICState *eclic, int irq, int hartid, int mth)
{
    (void)irq;
    eclic->mth[hartid] = mth;
    nuclei_eclic_sync_cpu_thresholds(eclic, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void nuclei_eclic_update_intsth(NucleiECLICState *eclic, int irq, int hartid, int sth)
{
    (void)irq;
    eclic->sth[hartid] = sth;
    nuclei_eclic_sync_cpu_thresholds(eclic, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void eclic_insert_pending_list(NucleiECLICState *eclic, int irq, int hartid)
{
    ECLICPendingInterrupt *entry = &eclic->clicintlist[hartid][irq];
    ECLICPendingInterrupt *node;

    if (entry->pending) {
        eclic_remove_pending_list(eclic, irq, hartid);
    }

    if (QLIST_EMPTY(&eclic->pending_list[hartid])) {
        QLIST_INSERT_HEAD(&eclic->pending_list[hartid], entry, next);
        entry->pending = true;
        return;
    }

    QLIST_FOREACH(node, &eclic->pending_list[hartid], next) {
        if (level_compare(node, entry) > 0) {
            QLIST_INSERT_BEFORE(node, entry, next);
            entry->pending = true;
            return;
        }
        if (node->next.le_next == NULL) {
            QLIST_INSERT_AFTER(node, entry, next);
            entry->pending = true;
            return;
        }
    }
}

static void nuclei_eclic_update_pending_state(NucleiECLICState *eclic, int irq,
                                              int hartid, bool pending)
{
    ECLICPendingInterrupt *entry = &eclic->clicintlist[hartid][irq];
    bool old_pending = eclic->clicintip[hartid][irq] != 0;
    bool was_queued = entry->pending;

    /* clicintip is the architectural pending bit; entry->pending only tracks
     * whether the source currently sits inside the ordered software queue.
     */
    if (pending) {
        eclic->clicintip[hartid][irq] = 1;
        if (!entry->pending) {
            eclic_insert_pending_list(eclic, irq, hartid);
        }
    } else {
        eclic->clicintip[hartid][irq] = 0;
        if (entry->pending) {
            eclic_remove_pending_list(eclic, irq, hartid);
        }
    }

    if (old_pending != pending || was_queued != entry->pending) {
        nuclei_eclic_next_interrupt(eclic, hartid);
    }
}

static void nuclei_eclic_update_irq_input(NucleiECLICState *eclic, int irq,
                                          int hartid, int new_signal)
{
    ECLICPendingInterrupt *entry = &eclic->clicintlist[hartid][irq];
    int old_signal = entry->sig;
    bool pending = eclic->clicintip[hartid][irq] != 0;

    new_signal = !!new_signal;

    /* Hardware input sampling is edge/level dependent, but once an event has
     * become pending it feeds the common pending-state update path.
     */
    switch (entry->trigger) {
    case 1:
        if (!old_signal && new_signal) {
            pending = true;
        }
        break;
    case 3:
        if (old_signal && !new_signal) {
            pending = true;
        }
        break;
    case 0:
    case 2:
    default:
        pending = new_signal;
        break;
    }

    entry->sig = new_signal;
    nuclei_eclic_update_pending_state(eclic, irq, hartid, pending);
}

static void nuclei_eclic_write_intip(NucleiECLICState *eclic, int irq,
                                     int hartid, int new_intip)
{
    ECLICPendingInterrupt *entry = &eclic->clicintlist[hartid][irq];

    if (!nuclei_eclic_ip_writable_from_software(entry->trigger)) {
        return;
    }

    /*
     * Spec 15.7/15.8: software writes operate on the latched pending bit
     * directly for edge-triggered sources. They do not represent a sampled
     * external signal transition, so keep the edge detector state separate.
     */
    nuclei_eclic_update_pending_state(eclic, irq, hartid, !!new_intip);
}

static void nuclei_eclic_update_intie(NucleiECLICState *eclic, int irq, int hartid,
                                      int new_intie)
{
    eclic->clicintie[hartid][irq] = nuclei_eclic_read_intie(new_intie);
    update_eclic_int_info(eclic, irq, hartid);
    nuclei_eclic_sync_external_routes(eclic, irq);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void nuclei_eclic_update_intattr(NucleiECLICState *eclic, int irq, int hartid,
                                        int new_intattr)
{
    uint8_t old_attr = eclic->clicintattr[hartid][irq];
    uint8_t new_mode = (new_intattr >> 6) & 0x3;
    bool supervisor_supported = nuclei_eclic_get_nmbits(eclic, hartid) != 0;

    if ((new_mode != PRV_M && new_mode != PRV_S) ||
        (new_mode == PRV_S && !supervisor_supported)) {
        new_intattr = (new_intattr & 0x3F) | (old_attr & 0xC0);
    }

    eclic->clicintattr[hartid][irq] = new_intattr & ~0x38;
    update_eclic_int_info(eclic, irq, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void nuclei_eclic_update_intattr_s_view(NucleiECLICState *eclic, int irq,
                                               int hartid, int new_intattr)
{
    /* The S-view cannot rewrite the mode field; it only sees the SHV/trigger
     * bits of interrupts already delegated to S-mode.
     */
    uint8_t merged = (eclic->clicintattr[hartid][irq] & 0xC0) |
                     (new_intattr & 0x07);

    eclic->clicintattr[hartid][irq] = merged;
    update_eclic_int_info(eclic, irq, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void nuclei_eclic_update_intctl(NucleiECLICState *eclic, int irq, int hartid,
                                       int new_intctl)
{
    /* Store intctl in normalized form so later comparisons always use the
     * implemented CLICINTCTLBITS width.
     */
    eclic->clicintctl[hartid][irq] =
        nuclei_eclic_read_intctl(eclic, new_intctl);
    update_eclic_int_info(eclic, irq, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void nuclei_eclic_shadow_gpr_init(CPURISCVState *env)
{
    RISCVEclicShadowState *shadow =  &env->eclic_shadow;

    if (!shadow) {
        error_report("Failed to allocate shadow GPR system");
        return;
    }
    memset(shadow->gpr_banks, 0, sizeof(shadow->gpr_banks));
    memset(shadow->fpr_banks, 0, sizeof(shadow->fpr_banks));
    memset(shadow->fcsr_banks, 0, sizeof(shadow->fcsr_banks));
    memset(shadow->shadow_grp_used, 0, sizeof(shadow->shadow_grp_used));
    memset(shadow->grp_stack, 0, sizeof(shadow->grp_stack));
    /* The basic GPR group (Bank 0) is used by default. */
    shadow->current_grp = 0;
    shadow->current_gpr_grp = 0;
    shadow->current_fpr_grp = 0;
    shadow->grp_stack_top = -1;
}

static void nuclei_eclic_reset(DeviceState *dev)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);
    NucleiECLICExternalRoute *route;
    int hartid;
    int id;
    int irq;

    for (irq = 0; irq < eclic->num_sources; irq++) {
        for (route = eclic->external_routes[irq]; route; route = route->next) {
            if (route->asserted_hart >= 0) {
                qemu_set_irq(nuclei_eclic_get_irq(route->dev, route->irq,
                                                  route->asserted_hart), 0);
            }
            route->asserted_hart = -1;
            route->level = false;
        }
    }

    for (hartid = 0; hartid < eclic->num_harts; hartid++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(hartid));

        eclic->cliccfg[hartid] = 0;
        eclic->clicinfo[hartid] = 0;
        eclic->mth[hartid] = 0;
        eclic->sth[hartid] = 0;
        eclic->exccode[hartid] = 0;
        memset(eclic->clicintip[hartid], 0, sizeof(eclic->clicintip[hartid]));
        memset(eclic->clicintie[hartid], 0, sizeof(eclic->clicintie[hartid]));
        memset(eclic->clicintattr[hartid], 0,
               sizeof(eclic->clicintattr[hartid]));
        memset(eclic->clicintctl[hartid], 0,
               sizeof(eclic->clicintctl[hartid]));
        memset(eclic->clicintlist[hartid], 0,
               sizeof(eclic->clicintlist[hartid]));
        QLIST_INIT(&eclic->pending_list[hartid]);

        for (id = 0; id < eclic->num_sources; id++) {
            eclic->clicintlist[hartid][id].irq = id;
            eclic->clicintattr[hartid][id] = PRV_M << 6;
            update_eclic_int_info(eclic, id, hartid);
        }

        if (!cpu) {
            continue;
        }

        cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_ECLIC);
        nuclei_eclic_sync_cpu_thresholds(eclic, hartid);
        nuclei_eclic_shadow_gpr_init(&cpu->env);
    }
}

static void nuclei_eclic_realize(DeviceState *dev, Error **errp)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);
    int id;

    memory_region_init_io(&eclic->mmio, OBJECT(dev), &nuclei_eclic_ops, eclic,
                          TYPE_NUCLEI_ECLIC, eclic->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &eclic->mmio);
    qdev_init_gpio_in(dev, nuclei_eclic_set_irq,
                      eclic->num_harts * eclic->num_sources);

    eclic->exccode = g_new0(uint32_t, eclic->num_harts);

    for (int i = 0; i < eclic->num_harts; i++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(i));

        QLIST_INIT(&eclic->pending_list[i]);
        for (id = 0; id < eclic->num_sources; id++) {
            /* Reset default is an M-mode, disabled, non-pending source. */
            eclic->clicintlist[i][id].irq = id;
            eclic->clicintattr[i][id] = PRV_M << 6;
            update_eclic_int_info(eclic, id, i);
        }

        cpu->env.eclic = eclic;
        nuclei_eclic_sync_cpu_thresholds(eclic, i);
        nuclei_eclic_shadow_gpr_init(&cpu->env);
    }
}

static void nuclei_eclic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nuclei_eclic_properties);
    dc->realize = nuclei_eclic_realize;
    dc->reset = nuclei_eclic_reset;
    dc->desc = "nuclei type: eclic";
}

static const TypeInfo nuclei_eclic_info = {
    .name = TYPE_NUCLEI_ECLIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiECLICState),
    .class_init = nuclei_eclic_class_init,
};

static void nuclei_eclic_register_types(void)
{
    type_register_static(&nuclei_eclic_info);
}

type_init(nuclei_eclic_register_types);

void nuclei_eclic_systimer_cb(void *opaque)
{
    CPURISCVState *env = (CPURISCVState *)opaque;
    nuclei_eclic_irq_request(env, Internal_SysTimer_IRQn, 1);
}

DeviceState *nuclei_eclic_create(hwaddr addr, uint32_t aperture_size, bool vector,
                                 uint32_t num_harts, uint32_t num_sources,
                                 uint8_t clicintctlbits, uint32_t shadow_gpr_num)
{
    DeviceState *dev = qdev_new(TYPE_NUCLEI_ECLIC);

    assert(num_sources <= 4096);
    assert(num_harts <= ECLIC_MAX_HARTS);
    assert(clicintctlbits >= 2 && clicintctlbits <= 8);

    qdev_prop_set_bit(dev, "vector", vector);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    /* clicintctlbits is board-visible configuration, not a fixed constant. */
    qdev_prop_set_uint32(dev, "eclicintctlbits", clicintctlbits);
    qdev_prop_set_uint64(dev, "mclicbase", addr);
    qdev_prop_set_uint32(dev, "aperture-size", aperture_size);
    qdev_prop_set_uint32(dev, "shadow-gpr-num", shadow_gpr_num);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
