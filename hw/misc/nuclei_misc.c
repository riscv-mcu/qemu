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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/misc/nuclei_misc.h"

enum {
    NUCLEI_MISC_COUNTER0 = 0,
    NUCLEI_MISC_COUNTER1 = 1,
    NUCLEI_MISC_NMI = 2,
    NUCLEI_MISC_EVENT = 3,
    NUCLEI_MISC_COUNTER_COUNT = 4,
};

static inline uint64_t nuclei_misc_ticks_to_ns(const NucleiMiscState *s,
                                               uint32_t ticks)
{
    return muldiv64(ticks, NANOSECONDS_PER_SECOND, s->tick_hz);
}

static inline uint32_t nuclei_misc_ns_to_ticks(const NucleiMiscState *s,
                                               uint64_t elapsed_ns)
{
    return muldiv64(elapsed_ns, s->tick_hz, NANOSECONDS_PER_SECOND);
}

static void nuclei_misc_update_counter_signal(NucleiMiscState *s, int counter_id)
{
    /*
     * Counter0/1 feed the board-level external interrupt inputs. NMI and
     * Event share the same countdown model but leave the device on dedicated
     * named GPIOs so board code can route them explicitly.
     */
    switch (counter_id) {
    case NUCLEI_MISC_COUNTER0:
    case NUCLEI_MISC_COUNTER1:
        qemu_set_irq(s->irq[counter_id], s->counters[counter_id].asserted);
        break;
    case NUCLEI_MISC_NMI:
        qemu_set_irq(s->nmi_irq, s->counters[counter_id].asserted);
        break;
    case NUCLEI_MISC_EVENT:
        qemu_set_irq(s->event_irq, s->counters[counter_id].asserted);
        break;
    default:
        g_assert_not_reached();
    }
}

static void nuclei_misc_update_sdio_enable(NucleiMiscState *s)
{
    /* Expose bit0 on a dedicated line; board code may choose to consume it later. */
    qemu_set_irq(s->sdio_enable_irq, !!(s->misc_ctrl & NUCLEI_MISC_MISC_CTRL_MASK));
}

static void nuclei_misc_update_outputs(NucleiMiscState *s)
{
    int i;

    for (i = 0; i < NUCLEI_MISC_COUNTER_COUNT; i++) {
        nuclei_misc_update_counter_signal(s, i);
    }

    nuclei_misc_update_sdio_enable(s);
}

static uint32_t nuclei_misc_counter_value(NucleiMiscCounter *counter)
{
    NucleiMiscState *s = counter->owner;
    uint64_t now_ns;
    uint32_t elapsed_ticks;

    if (!counter->running) {
        /*
         * A stopped counter reads back STOP. Once it has expired, the
         * external signal remains asserted and the guest observes a latched 0
         * until software clears or reloads the source.
         */
        return counter->asserted ? 0 : NUCLEI_MISC_COUNTER_STOP;
    }

    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    elapsed_ticks = nuclei_misc_ns_to_ticks(s, now_ns - counter->start_ns);

    if (elapsed_ticks >= counter->load) {
        return 0;
    }

    return counter->load - elapsed_ticks;
}

static void nuclei_misc_counter_stop(NucleiMiscCounter *counter)
{
    counter->running = false;
    counter->load = NUCLEI_MISC_COUNTER_STOP;
    timer_del(counter->timer);
}

static void nuclei_misc_counter_assert(NucleiMiscCounter *counter)
{
    counter->running = false;
    counter->asserted = true;
    timer_del(counter->timer);
    nuclei_misc_update_counter_signal(counter->owner, counter->id);
}

static void nuclei_misc_counter_expire(void *opaque)
{
    NucleiMiscCounter *counter = opaque;

    nuclei_misc_counter_assert(counter);
}

static void nuclei_misc_counter_load(NucleiMiscState *s, int counter_id,
                                     uint32_t value)
{
    NucleiMiscCounter *counter = &s->counters[counter_id];

    /*
     * All four countdown registers share the same reload rule. Counter0/1
     * then fan out into the board interrupt fabric, while NMI/Event keep
     * their own downstream wiring.
     */
    counter->asserted = false;
    nuclei_misc_update_counter_signal(s, counter_id);

    if (value == NUCLEI_MISC_COUNTER_STOP) {
        nuclei_misc_counter_stop(counter);
        return;
    }

    counter->load = value;
    counter->start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (value == 0) {
        nuclei_misc_counter_assert(counter);
        return;
    }

    counter->running = true;
    timer_mod(counter->timer,
              counter->start_ns + nuclei_misc_ticks_to_ns(s, value));
}

static uint64_t nuclei_misc_read(void *opaque, hwaddr offset, unsigned int size)
{
    NucleiMiscState *s = NUCLEI_MISC(opaque);

    (void)size;

    switch (offset) {
    case NUCLEI_MISC_REG_COUNTER0_IRQ:
        return nuclei_misc_counter_value(&s->counters[NUCLEI_MISC_COUNTER0]);
    case NUCLEI_MISC_REG_COUNTER1_IRQ:
        return nuclei_misc_counter_value(&s->counters[NUCLEI_MISC_COUNTER1]);
    case NUCLEI_MISC_REG_IOCP_R:
        return s->iocp_r;
    case NUCLEI_MISC_REG_IOCP_W:
        return s->iocp_w;
    case NUCLEI_MISC_REG_VLM_LATENCY:
        return s->vlm_latency;
    case NUCLEI_MISC_REG_MEM_LATENCY:
        return s->mem_latency;
    case NUCLEI_MISC_REG_NMI:
        return nuclei_misc_counter_value(&s->counters[NUCLEI_MISC_NMI]);
    case NUCLEI_MISC_REG_EVENT:
        return nuclei_misc_counter_value(&s->counters[NUCLEI_MISC_EVENT]);
    case NUCLEI_MISC_REG_DEBUG_CTRL:
        return s->debug_ctrl;
    case NUCLEI_MISC_REG_MISC_CTRL:
        return s->misc_ctrl;
    case NUCLEI_MISC_REG_VERSION:
        /* Maj/Min/Mic version encoding is currently modeled as a property. */
        return s->version & 0x00ffffff;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void nuclei_misc_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
    NucleiMiscState *s = NUCLEI_MISC(opaque);

    (void)size;

    switch (offset) {
    case NUCLEI_MISC_REG_COUNTER0_IRQ:
        nuclei_misc_counter_load(s, NUCLEI_MISC_COUNTER0, value);
        break;
    case NUCLEI_MISC_REG_COUNTER1_IRQ:
        nuclei_misc_counter_load(s, NUCLEI_MISC_COUNTER1, value);
        break;
    case NUCLEI_MISC_REG_IOCP_R:
        s->iocp_r = value;
        break;
    case NUCLEI_MISC_REG_IOCP_W:
        s->iocp_w = value;
        break;
    case NUCLEI_MISC_REG_VLM_LATENCY:
        s->vlm_latency = value & NUCLEI_MISC_VLM_LATENCY_MASK;
        break;
    case NUCLEI_MISC_REG_MEM_LATENCY:
        s->mem_latency = value & NUCLEI_MISC_MEM_LATENCY_MASK;
        break;
    case NUCLEI_MISC_REG_NMI:
        nuclei_misc_counter_load(s, NUCLEI_MISC_NMI, value);
        break;
    case NUCLEI_MISC_REG_EVENT:
        nuclei_misc_counter_load(s, NUCLEI_MISC_EVENT, value);
        break;
    case NUCLEI_MISC_REG_DEBUG_CTRL:
        s->debug_ctrl = value & NUCLEI_MISC_DEBUG_CTRL_MASK;
        break;
    case NUCLEI_MISC_REG_MISC_CTRL:
        s->misc_ctrl = value & NUCLEI_MISC_MISC_CTRL_MASK;
        nuclei_misc_update_sdio_enable(s);
        break;
    case NUCLEI_MISC_REG_VERSION:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: version is read-only\n", __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n",
                      __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps nuclei_misc_ops = {
    .read = nuclei_misc_read,
    .write = nuclei_misc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void nuclei_misc_reset(DeviceState *dev)
{
    NucleiMiscState *s = NUCLEI_MISC(dev);
    int i;

    s->iocp_r = 0;
    s->iocp_w = 0;
    s->vlm_latency = 0;
    s->mem_latency = 0;
    s->debug_ctrl = 0x1;
    s->misc_ctrl = 0;

    for (i = 0; i < NUCLEI_MISC_COUNTER_COUNT; i++) {
        s->counters[i].asserted = false;
        nuclei_misc_counter_stop(&s->counters[i]);
    }

    nuclei_misc_update_outputs(s);
}

static const VMStateDescription vmstate_nuclei_misc_counter = {
    .name = "nuclei-misc-counter",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, NucleiMiscCounter),
        VMSTATE_UINT64(start_ns, NucleiMiscCounter),
        VMSTATE_UINT32(load, NucleiMiscCounter),
        VMSTATE_BOOL(running, NucleiMiscCounter),
        VMSTATE_BOOL(asserted, NucleiMiscCounter),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_nuclei_misc = {
    .name = "nuclei-misc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(tick_hz, NucleiMiscState),
        VMSTATE_UINT32(version, NucleiMiscState),
        VMSTATE_UINT32(iocp_r, NucleiMiscState),
        VMSTATE_UINT32(iocp_w, NucleiMiscState),
        VMSTATE_UINT32(vlm_latency, NucleiMiscState),
        VMSTATE_UINT32(mem_latency, NucleiMiscState),
        VMSTATE_UINT32(debug_ctrl, NucleiMiscState),
        VMSTATE_UINT32(misc_ctrl, NucleiMiscState),
        VMSTATE_STRUCT_ARRAY(counters, NucleiMiscState, NUCLEI_MISC_COUNTER_COUNT, 0,
                             vmstate_nuclei_misc_counter, NucleiMiscCounter),
        VMSTATE_END_OF_LIST()
    }
};

static Property nuclei_misc_props[] = {
    DEFINE_PROP_UINT32("tick-hz", NucleiMiscState, tick_hz, 32768),
    DEFINE_PROP_UINT32("version", NucleiMiscState, version, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_misc_init(Object *obj)
{
    NucleiMiscState *s = NUCLEI_MISC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    memory_region_init_io(&s->mmio, obj, &nuclei_misc_ops, s,
                          TYPE_NUCLEI_MISC, NUCLEI_MISC_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq[0]);
    sysbus_init_irq(sbd, &s->irq[1]);
    /*
     * Keep the non-counter outputs named instead of baking in board
     * semantics here. EvalSoC currently consumes only the NMI line.
     */
    qdev_init_gpio_out_named(DEVICE(obj), &s->nmi_irq, "nmi", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->event_irq, "event", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->sdio_enable_irq,
                             "sdio-enable", 1);

    for (i = 0; i < NUCLEI_MISC_COUNTER_COUNT; i++) {
        s->counters[i].owner = s;
        s->counters[i].id = i;
        s->counters[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                            nuclei_misc_counter_expire,
                                            &s->counters[i]);
    }
}

static void nuclei_misc_finalize(Object *obj)
{
    NucleiMiscState *s = NUCLEI_MISC(obj);
    int i;

    for (i = 0; i < NUCLEI_MISC_COUNTER_COUNT; i++) {
        timer_free(s->counters[i].timer);
    }
}

static void nuclei_misc_realize(DeviceState *dev, Error **errp)
{
    NucleiMiscState *s = NUCLEI_MISC(dev);

    if (s->tick_hz == 0) {
        error_setg(errp, "tick-hz must be non-zero");
        return;
    }
}

static void nuclei_misc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nuclei_misc_realize;
    dc->reset = nuclei_misc_reset;
    dc->vmsd = &vmstate_nuclei_misc;
    dc->desc = "Nuclei Misc";
    device_class_set_props(dc, nuclei_misc_props);
}

static const TypeInfo nuclei_misc_info = {
    .name = TYPE_NUCLEI_MISC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiMiscState),
    .instance_init = nuclei_misc_init,
    .instance_finalize = nuclei_misc_finalize,
    .class_init = nuclei_misc_class_init,
};

static void nuclei_misc_register_types(void)
{
    type_register_static(&nuclei_misc_info);
}

type_init(nuclei_misc_register_types)
