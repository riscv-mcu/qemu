/*
 *  NUCLEI TIMER (Timer Unit) interface
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This provides a parameterizable timer controller based on NucLei's Systimer.
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
#ifndef HW_NUCLEI_SYSTIMER_H
#define HW_NUCLEI_SYSTIMER_H

#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/nuclei_eclic.h"

#define TYPE_NUCLEI_SYSTIMER "riscv.nuclei.systimer"

#define NUCLEI_SYSTIMER(obj) \
    OBJECT_CHECK(NucLeiSYSTIMERState, (obj), TYPE_NUCLEI_SYSTIMER)

#define NUCLEI_SYSTIMER_REG_MTIMELO             (0x0000)
#define NUCLEI_SYSTIMER_REG_MTIMEHI             (0x0004)
#define NUCLEI_SYSTIMER_REG_MTIMECMPLO          (0x0008)
#define NUCLEI_SYSTIMER_REG_MTIMECMPHI          (0x000C)
#define NUCLEI_SYSTIMER_REG_MSFTRST             (0xFF0)
#define NUCLEI_SYSTIMER_REG_MSTOP               (0xFF8)
#define NUCLEI_SYSTIMER_REG_MSIP                (0xFFC)

#define NUCLEI_SYSTIMER_CLINT_MSIP_HART0        (0x1000)

typedef struct NucLeiSYSTIMERState
{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq *timer_irq;
    qemu_irq *soft_irq;

    DeviceState *eclic;

    uint32_t mtime_lo;
    uint32_t mtime_hi;
    uint32_t mtimecmp_lo;
    uint32_t mtimecmp_hi;
    uint32_t mstop;
    uint32_t msip;

    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t sip_base;
    uint32_t timecmp_base;
    uint32_t time_base;
    uint32_t aperture_size;
    uint32_t timebase_freq;

} NucLeiSYSTIMERState;

#define  NUCLEI_GD32_TIMEBASE_FREQ  (108000000/4)
#define  NUCLEI_N_TIMEBASE_FREQ     (32768)
#define  NUCLEI_U_TIMEBASE_FREQ     (32768)
#define  DEMOSOC_TIMEBASE_FREQ     (32768)
#define  EVALSOC_TIMEBASE_FREQ     (32768)

enum {
    NUCLEI_SIP_BASE     = 0x1000,
    NUCLEI_TIMECMP_BASE = 0x5000,
    NUCLEI_TIME_BASE    = 0xCFF8
};

DeviceState *nuclei_systimer_create(hwaddr addr, hwaddr size, uint32_t hartid_base, uint32_t num_harts,
                                    DeviceState *eclic, uint32_t timebase_freq);

#endif