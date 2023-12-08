/*
 * NUCLEI ECLIC (Enhanced Core Local Interrupt Controller) interface
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This provides a parameterizable interrupt controller based on NucLei's ECLIC.
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

typedef struct NucLeiECLICState NucLeiECLICState;
DECLARE_INSTANCE_CHECKER(NucLeiECLICState, NUCLEI_ECLIC,
                         TYPE_NUCLEI_ECLIC)

typedef struct ECLICPendingInterrupt
{
    int irq;
    int prio;
    int level;
    int enable;
    int trigger;
    int sig;
    QLIST_ENTRY(ECLICPendingInterrupt)
    next;
} ECLICPendingInterrupt;

#define NUCLEI_ECLIC_REG_CLICCFG 0x0000
#define NUCLEI_ECLIC_REG_CLICINFO 0x0004
#define NUCLEI_ECLIC_REG_MTH 0x000b
#define NUCLEI_ECLIC_REG_CLICINTIP_BASE 0x1000
#define NUCLEI_ECLIC_REG_CLICINTIE_BASE 0x1001
#define NUCLEI_ECLIC_REG_CLICINTATTR_BASE 0x1002
#define NUCLEI_ECLIC_REG_CLICINTCTL_BASE 0x1003

#define CLICINTCTLBITS 0x6

typedef struct NucLeiECLICState
{
    /*< private >*/
    SysBusDevice parent_obj;

    bool prv_s;
    bool prv_u;
    bool nvbits;

    /*< public >*/
    MemoryRegion mmio;

    uint32_t num_harts;
    uint32_t num_sources; /* 4-1024 */
    uint32_t eclicintctlbits;
    uint32_t eclic_mmode_base;
    uint64_t mclicbase;
    /* config */
    uint8_t cliccfg[32];   /*  nlbits(1~4) */
    uint32_t clicinfo[32]; /*  NUM_INTERRUPT(0~12)  VERSION(13~20) CLICINTCTLBITS(21~24) */
    uint8_t mth[32];       /* mth(0~7) */
    uint8_t clicintip[4096][32];
    uint8_t clicintie[4096][32];
    uint8_t clicintattr[4096][32]; /* shv(0) trig(1~2)*/
    uint8_t clicintctl[4096][32];  /*  level (cliccfg.nlbits) priority( (CLICINTCTLBITS - cliccfg.nlbits)*/
    ECLICPendingInterrupt clicintlist[4096][32];

    uint32_t *exccode;
    uint32_t aperture_size;

    QLIST_HEAD(, ECLICPendingInterrupt)
    pending_list[32];
    size_t active_count;

    /* ECLIC IRQ handlers */
    qemu_irq irqs[4096][32];

} NucLeiECLICState;

enum
{
    Internal_Reserved0_IRQn = 0,     /*!<  Internal reserved */
    Internal_Reserved1_IRQn = 1,     /*!<  Internal reserved */
    Internal_Reserved2_IRQn = 2,     /*!<  Internal reserved */
    Internal_SysTimerSW_IRQn = 3,    /*!<  System Timer SW interrupt */
    Internal_Reserved3_IRQn = 4,     /*!<  Internal reserved */
    Internal_Reserved4_IRQn = 5,     /*!<  Internal reserved */
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

DeviceState *nuclei_eclic_create(hwaddr addr, uint32_t aperture_size, bool prv_s, bool prv_u, bool vector,
                               uint32_t num_harts, uint32_t num_sources,
                               uint8_t clicintctlbits);
qemu_irq nuclei_eclic_get_irq(DeviceState *dev, int irq, int hartid);
void nuclei_eclic_systimer_cb(DeviceState *dev);
void riscv_cpu_eclic_int_handler_start(void *eclic_ptr, int irq, int hartid);

#endif

