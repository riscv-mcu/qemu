/*
 * NUCLEI MISC
 *
 * Copyright (c) 2026 Nucleisys, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef HW_NUCLEI_MISC_H
#define HW_NUCLEI_MISC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_NUCLEI_MISC "riscv.nuclei.misc"
OBJECT_DECLARE_SIMPLE_TYPE(NucleiMiscState, NUCLEI_MISC)

#define NUCLEI_MISC_SIZE               0x1000

#define NUCLEI_MISC_REG_COUNTER0_IRQ   0x000
#define NUCLEI_MISC_REG_COUNTER1_IRQ   0x004
#define NUCLEI_MISC_REG_IOCP_R         0x020
#define NUCLEI_MISC_REG_IOCP_W         0x024
#define NUCLEI_MISC_REG_VLM_LATENCY    0x028
#define NUCLEI_MISC_REG_MEM_LATENCY    0x02c
#define NUCLEI_MISC_REG_NMI            0x100
#define NUCLEI_MISC_REG_EVENT          0x104
#define NUCLEI_MISC_REG_DEBUG_CTRL     0x108
#define NUCLEI_MISC_REG_MISC_CTRL      0x10c
#define NUCLEI_MISC_REG_VERSION        0xf00

/*
 * The integration guide uses all-ones as the software stop/clear value for
 * the four MISC countdown sources.
 */
#define NUCLEI_MISC_COUNTER_STOP       0xffffffffu
#define NUCLEI_MISC_VLM_LATENCY_MASK   0x1f
#define NUCLEI_MISC_MEM_LATENCY_MASK   0x1ff
#define NUCLEI_MISC_DEBUG_CTRL_MASK    0x1f
#define NUCLEI_MISC_MISC_CTRL_MASK     0x1

typedef struct NucleiMiscCounter {
    QEMUTimer *timer;
    NucleiMiscState *owner;
    uint64_t start_ns;
    uint32_t load;
    uint8_t id;
    /* running==false and asserted==true represents an expired level output. */
    bool running;
    bool asserted;
} NucleiMiscCounter;

struct NucleiMiscState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    /* Counter0/1 expose board-visible IRQs; NMI/Event are named GPIO lines. */
    qemu_irq irq[2];
    qemu_irq nmi_irq;
    qemu_irq event_irq;
    qemu_irq sdio_enable_irq;

    uint32_t tick_hz;
    uint32_t version;

    uint32_t iocp_r;
    uint32_t iocp_w;
    uint32_t vlm_latency;
    uint32_t mem_latency;
    uint32_t debug_ctrl;
    uint32_t misc_ctrl;

    NucleiMiscCounter counters[4];
};

#endif
