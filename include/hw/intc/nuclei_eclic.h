/*
 * NUCLEI ECLIC (Enhanced Core Local Interrupt Controller) interface
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
#ifndef HW_NUCLEI_ECLIC_H
#define HW_NUCLEI_ECLIC_H

#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_NUCLEI_ECLIC "riscv.nuclei.eclic"

#define INTERRUPT_SOURCE_MIN_ID (18)
#define INTERRUPT_SOURCE_MAX_ID (4096)

typedef struct NucleiECLICState NucleiECLICState;
typedef struct NucleiECLICExternalRoute NucleiECLICExternalRoute;
DECLARE_INSTANCE_CHECKER(NucleiECLICState, NUCLEI_ECLIC,
                         TYPE_NUCLEI_ECLIC)

typedef struct ECLICPendingInterrupt
{
    int irq;        /* Source ID within the per-hart ECLIC bank. */
    int mode;       /* Resolved delivery mode: PRV_M or PRV_S. */
    int prio;       /* Decoded priority compare key, kept left-justified. */
    int level;      /* Decoded level compare key after nlbits padding. */
    int enable;     /* Cached IE bit for deliverability checks. */
    int trigger;    /* 0/2: level, 1: rising edge, 3: falling edge. */
    int sig;        /* Last sampled input level used for edge detection. */
    bool pending;   /* True while the entry is linked into pending_list[]. */
    QLIST_ENTRY(ECLICPendingInterrupt)
    next;
} ECLICPendingInterrupt;

#define NUCLEI_ECLIC_REG_CLICCFG     0x0000
#define NUCLEI_ECLIC_REG_CLICINFO    0x0004
#define NUCLEI_ECLIC_REG_MINTTHRESH  0x0008
#define NUCLEI_ECLIC_REG_MINTTHRESH_HI 0x000a
#define NUCLEI_ECLIC_REG_STH         0x0009
#define NUCLEI_ECLIC_REG_MTH         0x000b
#define NUCLEI_ECLIC_REG_CLICINTIP_BASE 0x1000
#define NUCLEI_ECLIC_REG_CLICINTIE_BASE 0x1001
#define NUCLEI_ECLIC_REG_CLICINTATTR_BASE 0x1002
#define NUCLEI_ECLIC_REG_CLICINTCTL_BASE 0x1003
#define NUCLEI_ECLIC_REG_SINTTHRESH     0x2008
#define NUCLEI_ECLIC_REG_SINTTHRESH_HI  0x200a
#define NUCLEI_ECLIC_REG_SSTH           0x2009
#define NUCLEI_ECLIC_REG_CLICINTIP_BASE_S 0x3000
#define NUCLEI_ECLIC_REG_CLICINTIE_BASE_S 0x3001
#define NUCLEI_ECLIC_REG_CLICINTATTR_BASE_S 0x3002
#define NUCLEI_ECLIC_REG_CLICINTCTL_BASE_S 0x3003

#define NUCLEI_ECLIC_DEFAULT_INTCTLBITS 0x6

#define ECLIC_MAX_HARTS 64

typedef struct NucleiECLICState
{
    /*< private >*/
    SysBusDevice parent_obj;

    bool nvbits;

    /*< public >*/
    MemoryRegion mmio;

    uint32_t num_harts;
    uint32_t num_sources; /* 4-1024 */
    uint32_t eclicintctlbits; /* Board-configurable CLICINFO.CLICINTCTLBITS. */
    uint32_t eclic_mmode_base;
    uint64_t mclicbase;
    uint32_t shadow_gpr_num;  /* Implemented shadow caller-saved bank count. */
    /* config */
    uint8_t cliccfg[ECLIC_MAX_HARTS];   /* nlbits(1~4), nmbits(5~6 RO on read) */
    uint32_t clicinfo[ECLIC_MAX_HARTS]; /*  NUM_INTERRUPT(0~12)  VERSION(13~20) CLICINTCTLBITS(21~24) */
    uint8_t mth[ECLIC_MAX_HARTS];       /* mth(0~7) */
    uint8_t clicintip[ECLIC_MAX_HARTS][INTERRUPT_SOURCE_MAX_ID];
    uint8_t clicintie[ECLIC_MAX_HARTS][INTERRUPT_SOURCE_MAX_ID];
    uint8_t clicintattr[ECLIC_MAX_HARTS][INTERRUPT_SOURCE_MAX_ID];
    uint8_t clicintctl[ECLIC_MAX_HARTS][INTERRUPT_SOURCE_MAX_ID];
    ECLICPendingInterrupt clicintlist[ECLIC_MAX_HARTS][INTERRUPT_SOURCE_MAX_ID];

    uint8_t sth[ECLIC_MAX_HARTS];  /* Per-hart supervisor threshold mirror. */
    uint32_t *exccode;             /* Encoded IRQ selection handed to each CPU. */
    uint32_t aperture_size;        /* MMIO window size configured by the board. */
    NucleiECLICExternalRoute *external_routes[INTERRUPT_SOURCE_MAX_ID];
    /* Raw external source fanout used when an SMP topology wires ECLIC
     * directly without an intermediate CIDU distributor.
     */

    /*
     * Each hart keeps one pending queue ordered by mode/level/priority/ID.
     * Delivery eligibility is decided when the queue is scanned, rather than
     * by splitting M/S-mode interrupts into separate lists.
     */
    QLIST_HEAD(, ECLICPendingInterrupt)
    pending_list[ECLIC_MAX_HARTS];
} NucleiECLICState;

static inline uint8_t nuclei_eclic_effective_ctlbits(
    const NucleiECLICState *eclic)
{
    return eclic->eclicintctlbits ? eclic->eclicintctlbits :
           NUCLEI_ECLIC_DEFAULT_INTCTLBITS;
}

/* cliccfg.nlbits is software-visible, but only the implemented ctlbits slice
 * participates in level/priority decoding.
 */
static inline uint8_t nuclei_eclic_effective_nlbits(
    const NucleiECLICState *eclic, int hartid)
{
    return MIN((eclic->cliccfg[hartid] >> 1) & 0xf,
               nuclei_eclic_effective_ctlbits(eclic));
}

/* Return the raw MMIO byte pattern that represents logical interrupt level 0
 * for the current nlbits setting.
 */
static inline uint8_t nuclei_eclic_level_zero_encoding(
    const NucleiECLICState *eclic, int hartid)
{
    uint8_t nlbits;

    if (!eclic || hartid < 0 || hartid >= eclic->num_harts) {
        return 0;
    }

    nlbits = nuclei_eclic_effective_nlbits(eclic, hartid);
    if (nlbits >= 8) {
        return 0;
    }

    return (1u << (8 - nlbits)) - 1u;
}

/* Convert the raw encoded level byte back to the logical level value used by
 * shadow-group matching and trap-state bookkeeping.
 */
static inline uint8_t nuclei_eclic_level_decode_logical(
    const NucleiECLICState *eclic, int hartid, uint8_t encoded_level)
{
    uint8_t nlbits;

    if (!eclic || hartid < 0 || hartid >= eclic->num_harts) {
        return encoded_level;
    }

    nlbits = nuclei_eclic_effective_nlbits(eclic, hartid);
    if (nlbits == 0) {
        return 0;
    }

    return encoded_level >> (8 - nlbits);
}

enum
{
    Internal_Reserved0_IRQn = 0,     /*!<  Internal reserved */
    Internal_SysTimerSW_S_IRQn = 1,  /*!<  System Timer supervisor mode SW interrupt triggered by ssip */
    Internal_Reserved2_IRQn = 2,     /*!<  Internal reserved */
    Internal_SysTimerSW_IRQn = 3,    /*!<  System Timer SW interrupt */
    Internal_Reserved3_IRQn = 4,     /*!<  Internal reserved */
    Internal_SysTimer_S_IRQn = 5,    /*!<  System Timer supervisor mode interrupt triggered by stimecmp csr */
    Internal_Reserved5_IRQn = 6,     /*!<  Internal reserved */
    Internal_SysTimer_IRQn = 7,      /*!<  System Timer Interrupt */
    Internal_Reserved6_IRQn = 8,     /*!<  Internal reserved */
    Internal_Reserved7_IRQn = 9,     /*!<  Internal reserved */
    Internal_Reserved8_IRQn = 10,    /*!<  Internal reserved */
    Internal_Reserved9_IRQn = 11,    /*!<  Internal reserved */
    Internal_Reserved10_IRQn = 12,   /*!<  Internal reserved */
    Internal_Reserved11_IRQn = 13,   /*!<  Internal reserved */
    Internal_Reserved12_IRQn = 14,   /*!<  Internal reserved */
    Internal_Reserved13_IRQn = 15,   /*!<  Internal reserved */
    Internal_Reserved14_IRQn = 16,   /*!<  Internal reserved */
    Internal_BusError_IRQn = 17,     /*!<  Bus Error interrupt */
    Internal_PerfMon_IRQn = 18,      /*!<  Performance Monitor */
    Internal_Reserved_Max_IRQn = 19, /*!<  Internal reserved  Max */
};

DeviceState *nuclei_eclic_create(hwaddr addr, uint32_t aperture_size, bool vector,
                               uint32_t num_harts, uint32_t num_sources,
                               uint8_t clicintctlbits, uint32_t shadow_gpr_num);
qemu_irq nuclei_eclic_get_irq(DeviceState *dev, int irq, int hartid);
qemu_irq nuclei_eclic_get_external_irq(DeviceState *dev, int irq);
bool nuclei_eclic_irq_enabled(DeviceState *dev, uint32_t irq, int hartid);
void nuclei_eclic_systimer_cb(void *opaque);
void riscv_cpu_eclic_int_handler_start(void *eclic_ptr, int irq, int hartid);
bool riscv_intc_is_clic_mode(CPUArchState *env);
bool riscv_intc_is_eclicv2_mode(CPUArchState *env);
void shadow_gpr_push(CPUArchState *env, uint8_t gpr_grp, uint8_t fpr_grp,
                     uint8_t frame_restore_mask, bool tsp_swapped);
void shadow_gpr_pop(CPUArchState *env);
int get_shadow_gpr_stack_size(CPUArchState *env);
void riscv_shadow_gpr_switch_grp(CPUArchState *env, uint8_t gpr_grp_index,
                                 uint8_t fpr_grp_index);
void riscv_backup_shadow_gpr(CPUArchState *env, uint8_t gpr_grp_index,
                             uint8_t fpr_grp_index);
void nuclei_eclic_next_interrupt(void *eclic, int hartid);
uint8_t nuclei_eclic_get_threshold(void *opaque, int mode, int hartid);
void nuclei_eclic_set_threshold(void *opaque, int mode, int hartid,
                                uint8_t threshold);
bool nuclei_eclic_shv_interrupt(void *opaque, int hartid, int irq);
bool nuclei_eclic_edge_triggered(void *opaque, int hartid, int irq);
void nuclei_eclic_clean_pending(void *opaque, int hartid, int irq);
void nuclei_eclic_irq_request(void *opaque, int id, int new_intip);

#endif
