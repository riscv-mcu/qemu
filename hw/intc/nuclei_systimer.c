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
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "target/riscv/cpu.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

int hart_numbers = 0;

static inline int nuclei_systimer_get_current_cpu(NucLeiSYSTIMERState *s)
{
    if (s->num_harts > 1)
    {
        return current_cpu ? current_cpu->cpu_index : 0;
    }
    return 0;
}

static uint64_t nuclei_cpu_riscv_read_rtc(void *opaque)
{
    uint64_t timebase_freq = *(uint64_t*)opaque;
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
        timebase_freq, NANOSECONDS_PER_SECOND);
}

static void nuclei_timer_update_compare(NucLeiSYSTIMERState *s)
{
    size_t hartid = nuclei_systimer_get_current_cpu(s);
    CPUState *cpu = qemu_get_cpu(hartid);
    CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;
    uint64_t cmp, real_time;
    int64_t diff;

    real_time = s->mtime_lo | ((uint64_t)s->mtime_hi << 32);


    cmp = (uint64_t)s->mtimecmp_lo | ((uint64_t)s->mtimecmp_hi <<32);
    env->mtimecmp =  cmp;
    env->timecmp =  cmp;

    diff = cmp - real_time;

    if ( real_time >= cmp) {
        qemu_set_irq(*(s->timer_irq[hartid]), 1);
    }
    else {
            qemu_set_irq(*(s->timer_irq[hartid]), 0);

            if (s->mtimecmp_hi != 0xffffffff) {
                // set up future timer interrupt
                uint64_t next_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                    muldiv64(diff, NANOSECONDS_PER_SECOND, s->timebase_freq);
                timer_mod(env->mtimer, next_ns);
            }
    }
}

/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
static void sifive_clint_write_timecmp(RISCVCPU *cpu, uint64_t value,
                                       uint32_t timebase_freq)
{
    uint64_t next;
    uint64_t diff;

    uint64_t w_timebase_freq = timebase_freq;

    uint64_t rtc_r = nuclei_cpu_riscv_read_rtc(&w_timebase_freq);

    cpu->env.timecmp = value;
    if (cpu->env.timecmp <= rtc_r) {
        /* if we're setting an MTIMECMP value in the "past",
           immediately raise the timer interrupt */
        riscv_cpu_update_mip(&cpu->env, MIP_MTIP, BOOL_TO_MASK(1));
        return;
    }

    /* otherwise, set up the future timer interrupt */
    riscv_cpu_update_mip(&cpu->env, MIP_MTIP, BOOL_TO_MASK(0));
    diff = cpu->env.timecmp - rtc_r;
    /* back to ns (note args switched in muldiv64) */
    next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        muldiv64(diff, NANOSECONDS_PER_SECOND, timebase_freq);
    timer_mod(cpu->env.mtimer, next);
}

/*
 * Callback used when the timer set using timer_mod expires.
 * Should raise the timer interrupt line
 */
static void sifive_clint_timer_cb(void *opaque)
{
    RISCVCPU *cpu = opaque;
    riscv_cpu_update_mip(&cpu->env, MIP_MTIP, BOOL_TO_MASK(1));
}

/* CPU wants to read rtc or timecmp register */
static uint64_t nuclei_clint_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t timebase_f = 0;
    NucLeiSYSTIMERState *clint = NUCLEI_SYSTIMER(opaque);

    if (addr >= clint->msip_base &&
        addr < clint->msip_base + (clint->num_harts << 2)) {
        size_t hartid = clint->hartid_base + ((addr - clint->msip_base) >> 2);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;
        if (!env) {
            error_report("clint: invalid timecmp hartid: %zu", hartid);
        } else if ((addr & 0x3) == 0) {
            return (env->mip & MIP_MSIP) > 0;
        } else {
            error_report("clint: invalid read: %08x", (uint32_t)addr);
            return 0;
        }
    } else if (addr >= clint->mtimecmp_base &&
        addr < clint->mtimecmp_base + (clint->num_harts << 3)) {
        size_t hartid = clint->hartid_base +
            ((addr - clint->mtimecmp_base) >> 3);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;
        if (!env) {
            error_report("clint: invalid timecmp hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            /* timecmp_lo */
            uint64_t timecmp = env->timecmp;
            return timecmp & 0xFFFFFFFF;
        } else if ((addr & 0x7) == 4) {
            /* timecmp_hi */
            uint64_t timecmp = env->timecmp;
            return (timecmp >> 32) & 0xFFFFFFFF;
        } else {
            error_report("clint: invalid read: %08x", (uint32_t)addr);
            return 0;
        }
    } else if (addr == clint->mtime_base) {
        /* time_lo */
        timebase_f = clint->timebase_freq;
        return nuclei_cpu_riscv_read_rtc(&timebase_f) & 0xFFFFFFFF;
    } else if (addr == clint->mtime_base + 4) {
        /* time_hi */
        timebase_f = (clint->timebase_freq);
        return (nuclei_cpu_riscv_read_rtc(&(timebase_f))>> 32) & 0xFFFFFFFF;
    }

    error_report("clint: invalid read: %08x", (uint32_t)addr);
    return 0;
}

/* CPU wrote to rtc or timecmp register */
static void nuclei_clint_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    NucLeiSYSTIMERState *clint = NUCLEI_SYSTIMER(opaque);

    if (addr >= clint->msip_base &&
        addr < clint->msip_base + (clint->num_harts << 2)) {
        size_t hartid = clint->hartid_base + ((addr - clint->msip_base) >> 2);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;
        if (!env) {
            error_report("clint: invalid timecmp hartid: %zu", hartid);
        } else if ((addr & 0x3) == 0) {
            if(clint->eclic != NULL)
            {
                clint->msip = value;
                if ((clint->msip & 0x1) == 1) {
                    qemu_set_irq(*(clint->soft_irq[hartid]), 1);
                }else{
                    qemu_set_irq(*(clint->soft_irq[hartid]), 0);
                }
            }
            else
            {
                riscv_cpu_update_mip(env, MIP_MSIP, BOOL_TO_MASK(value));
            }
        } else {
            error_report("clint: invalid sip write: %08x", (uint32_t)addr);
        }
        return;
    } else if (addr >= clint->mtimecmp_base &&
        addr < clint->mtimecmp_base + (clint->num_harts << 3)) {
        size_t hartid = clint->hartid_base +
            ((addr - clint->mtimecmp_base) >> 3);
        CPUState *cpu = qemu_get_cpu(hartid);
        CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;
       if (!env) {
            error_report("clint: invalid timecmp hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            /* timecmp_lo */
            uint64_t timecmp_hi = env->timecmp >> 32;
            sifive_clint_write_timecmp(RISCV_CPU(cpu),
                timecmp_hi << 32 | (value & 0xFFFFFFFF), clint->timebase_freq);
            return;
        } else if ((addr & 0x7) == 4) {
            /* timecmp_hi */
            uint64_t timecmp_lo = env->timecmp;
            sifive_clint_write_timecmp(RISCV_CPU(cpu),
                value << 32 | (timecmp_lo & 0xFFFFFFFF), clint->timebase_freq);
        } else {
            error_report("clint: invalid timecmp write: %08x", (uint32_t)addr);
        }
        return;
    } else if (addr == clint->mtime_base) {
        /* time_lo */
        error_report("clint: time_lo write not implemented");
        return;
    } else if (addr == clint->mtime_base + 4) {
        /* time_hi */
        error_report("clint: time_hi write not implemented");
        return;
    }

    error_report("clint: invalid write: %08x", (uint32_t)addr);
}

static void nuclei_timer_reset(DeviceState *dev)
{
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);
    s->mtime_lo = 0x0;
    s->mtime_hi = 0x0;
    s->mtimecmp_lo = 0xFFFFFFFF;
    s->mtimecmp_hi = 0xFFFFFFFF;
    s->mtime_srw_ctrl = 0x0;
    s->msftrst = 0x0;
    s->mtimectl = 0x0;
    s->msip = 0x0;
}

static uint64_t nuclei_timer_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    uint64_t timebase_f = 0;
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(opaque);

    if(s->prv_s && (s->mtime_srw_ctrl & 0x1))
        return 0;

    if(offset >= NUCLEI_SYSTIMER_CLINT_MSIP_HART0)
    {
        return nuclei_clint_read(opaque, offset, size);
    }
    CPUState *cpu = qemu_get_cpu(nuclei_systimer_get_current_cpu(s));
    CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;
    uint64_t value = 0;

    switch (offset) {
    case NUCLEI_SYSTIMER_REG_MTIMELO:
        if(s->mtimectl)
        {
            value = 0;
        }
        else
        {
            timebase_f = s->timebase_freq;
            value = nuclei_cpu_riscv_read_rtc(&timebase_f);
            s->mtime_lo = value & 0xffffffff;
            s->mtime_hi = (value >> 32) & 0xffffffff;
            value = s->mtime_lo;
        }
        break;
    case NUCLEI_SYSTIMER_REG_MTIMEHI:
        if(s->mtimectl)
        {
            value = 0;
        }
        else
        {
            value = s->mtime_hi;
        }
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPLO:
        s->mtimecmp_lo = (env->mtimecmp) & 0xFFFFFFFF;
        value = s->mtimecmp_lo;
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPHI:
        s->mtimecmp_hi = (env->mtimecmp >> 32) & 0xFFFFFFFF;
        value = s->mtimecmp_hi;
        break;
    case NUCLEI_SYSTIMER_REG_MTIMER_SRW_CTRL:
        value = s->mtime_srw_ctrl;
        break;
    case NUCLEI_SYSTIMER_REG_MSFTRST:
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECTL:
        value = s->mtimectl;
        break;
    case NUCLEI_SYSTIMER_REG_MSIP:
        value = s->msip;
        break;
    default:
        break;
    }

    return (value & 0xFFFFFFFF);
}

static void nuclei_timer_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(opaque);
    size_t hartid = nuclei_systimer_get_current_cpu(s);
    CPUState *cpu = qemu_get_cpu(hartid);
    CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;

    if(s->prv_s && (s->mtime_srw_ctrl & 0x1))
        return;

    if(offset >= NUCLEI_SYSTIMER_CLINT_MSIP_HART0)
    {
        return nuclei_clint_write(opaque, offset, value, size);
    }
    
    value = value & 0xFFFFFFFF;
    switch (offset) {
    case NUCLEI_SYSTIMER_REG_MTIMELO:
        s->mtime_lo = value;
        env->mtimer->expire_time &= 0xFFFFFFFF00000000ULL;
        env->mtimer->expire_time |= (value & 0xFFFFFFFF);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMEHI:
        s->mtime_hi = value;
        env->mtimer->expire_time &= 0x00000000FFFFFFFFULL;
        env->mtimer->expire_time |= (value << 32);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPLO:
        s->mtimecmp_lo = value;
        //s->mtimecmp_hi = 0xFFFFFFFF;
        //env->mtimecmp  |= (value &0xFFFFFFFF);
        nuclei_timer_update_compare(s);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECMPHI:
        s->mtimecmp_hi = value;
        //env->mtimecmp  |= ((value << 32)&0xFFFFFFFF);
        nuclei_timer_update_compare(s);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMER_SRW_CTRL:
        s->mtime_srw_ctrl = value;
        break;
    case NUCLEI_SYSTIMER_REG_MSFTRST:
        if (value == 0x80000a5f)
            nuclei_timer_reset((DeviceState *)s);
        break;
    case NUCLEI_SYSTIMER_REG_MTIMECTL:
        s->mtimectl = value;
        break;
    case NUCLEI_SYSTIMER_REG_MSIP:
        s->msip = value;
        if ((s->msip & 0x1) == 1) {
            qemu_set_irq(*(s->soft_irq[hartid]), 1);
        }else{
            qemu_set_irq(*(s->soft_irq[hartid]), 0);
        }

        break;
    default:
        break;
    }
}

static const MemoryRegionOps nuclei_timer_ops = {
    .read = nuclei_timer_read,
    .write = nuclei_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static Property nuclei_systimer_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", NucLeiSYSTIMERState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", NucLeiSYSTIMERState, num_harts, 0),
    DEFINE_PROP_UINT32("msip-base", NucLeiSYSTIMERState, msip_base, 0),
    DEFINE_PROP_UINT32("mtimecmp-base", NucLeiSYSTIMERState, mtimecmp_base, 0),
    DEFINE_PROP_UINT32("mtime-base", NucLeiSYSTIMERState, mtime_base, 0),
    DEFINE_PROP_UINT32("ssip-base", NucLeiSYSTIMERState, ssip_base, 0),
    DEFINE_PROP_UINT32("aperture-size", NucLeiSYSTIMERState, aperture_size, 0),
    DEFINE_PROP_UINT64("timebase-freq", NucLeiSYSTIMERState, timebase_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_timer_realize(DeviceState *dev, Error **errp)
{
    NucLeiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);

    if( s->aperture_size == 0)
         s->aperture_size = 0x10000;

    if( s->hartid_base == 0)
         s->hartid_base = 0;

    if( s->msip_base == 0)
         s->msip_base = NUCLEI_MSIP_BASE;

    if( s->mtimecmp_base == 0)
         s->mtimecmp_base = NUCLEI_MTIMECMP_BASE;

    if( s->mtime_base == 0)
         s->mtime_base = NUCLEI_MTIME_BASE;

    if( s->ssip_base == 0)
         s->ssip_base = NUCLEI_SSIP_BASE;

    if( s->num_harts == 0)
    {
        s->num_harts = hart_numbers;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &nuclei_timer_ops,
                          s,TYPE_NUCLEI_SYSTIMER, s->aperture_size);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void nuclei_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = nuclei_timer_realize;
    dc->reset = nuclei_timer_reset;
    dc->desc = "NucLei Systimer Timer";
    device_class_set_props(dc, nuclei_systimer_properties);
}

static const TypeInfo nuclei_timer_info = {
    .name = TYPE_NUCLEI_SYSTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucLeiSYSTIMERState),
    .class_init = nuclei_timer_class_init,
};

static void nuclei_timer_register_types(void)
{
    type_register_static(&nuclei_timer_info);
}
type_init(nuclei_timer_register_types);

static void nuclei_mtimecmp_cb(void *opaque) {
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(nuclei_systimer_get_current_cpu(opaque)));
    CPURISCVState *env = &cpu->env;

    nuclei_eclic_systimer_cb(cpu->env.eclic);
    timer_del(env->mtimer);
}

DeviceState *nuclei_systimer_create(hwaddr addr, hwaddr size, uint32_t hartid_base,
                        uint32_t num_harts, DeviceState *eclic, uint32_t timebase_freq)
{
    hart_numbers = num_harts;

    DeviceState *dev = qdev_new(TYPE_NUCLEI_SYSTIMER);
    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "msip-base", NUCLEI_MSIP_BASE);
    qdev_prop_set_uint32(dev, "mtimecmp-base", NUCLEI_MTIMECMP_BASE);
    qdev_prop_set_uint32(dev, "mtime-base", NUCLEI_MTIME_BASE);
    qdev_prop_set_uint32(dev, "ssip-base", NUCLEI_SSIP_BASE);
    qdev_prop_set_uint32(dev, "aperture-size", size);
    qdev_prop_set_uint32(dev, "timebase-freq", timebase_freq);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    return dev;
}
