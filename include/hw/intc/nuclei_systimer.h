/*
 *  NUCLEI TIMER (Timer Unit) interface
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
 *
 * This provides a parameterizable timer controller based on Nuclei's Systimer.
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

#define TYPE_NUCLEI_SYSTIMER "riscv.nuclei.systimer"

#define NUCLEI_SYSTIMER(obj) \
    OBJECT_CHECK(NucleiSYSTIMERState, (obj), TYPE_NUCLEI_SYSTIMER)

#define NUCLEI_SYSTIMER_MAX_HARTS        64

enum {
    REG_MTIME_LO        = 0x0000,
    REG_MTIME_HI        = 0x0004,
    REG_MTIMECMP_LO     = 0x0008,
    REG_MTIMECMP_HI     = 0x000C,
    REG_MTIME_SRW_CTRL  = 0x0FEC,
    REG_MSFTRST         = 0x0FF0,
    REG_SSIP            = 0x0FF4,
    REG_MTIMECTL        = 0x0FF8,
    REG_MSIP            = 0x0FFC,
    REG_MSIP_BASE       = 0x1000,
    REG_MTIMECMP_BASE   = 0x5000,
    REG_MTIME           = 0xCFF8,
    REG_SSIP_BASE       = 0xD000,
};

#define MTIMECTL_CMPCLREN   (1 << 1)
#define MTIMECTL_TIMESTOP   (1 << 0)
#define MTIMECTL_HDBG       (1 << 3)
#define MTIMECTL_RW_MASK    (MTIMECTL_CMPCLREN | MTIMECTL_TIMESTOP | \
                             MTIMECTL_HDBG)

#define MSFTRST_MAGIC       0x80000a5f

typedef struct NucleiSYSTIMERState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t aperture_size;
    uint64_t timebase_freq;

    /*
     * Shared MTIME model:
     * - time_delta follows ACLINT style: visible MTIME while running is
     *   raw_rtc + time_delta, so software writes retarget the counter by
     *   adjusting the delta against the raw virtual-time base.
     * - time_stop holds the frozen visible MTIME value while TIMESTOP=1.
     */
    uint64_t time_delta;
    uint64_t time_stop;
    uint32_t mtime_srw_ctrl;
    uint32_t msftrst;
    uint32_t mtimectl;

    uint32_t *msip;
    uint32_t *ssip;
    /* Per-hart interrupt outputs driven by the timer unit. */
    qemu_irq *timer_irq;
    qemu_irq *m_soft_irq;
    qemu_irq *s_soft_irq;

    DeviceState *eclic;
} NucleiSYSTIMERState;

#define EVALSOC_TIMEBASE_FREQ      (32768)

DeviceState *nuclei_systimer_create(hwaddr addr, hwaddr size,
                                    uint32_t hartid_base, uint32_t num_harts,
                                    DeviceState *eclic,
                                    uint32_t timebase_freq);

#endif
