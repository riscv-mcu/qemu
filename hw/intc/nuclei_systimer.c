/*
 *  NUCLEI SYSTIMER interface
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
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "target/riscv/cpu.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/qdev-properties.h"
#include "sysemu/runstate.h"

static inline uint32_t current_hartid(NucleiSYSTIMERState *s)
{
    if (s->num_harts > 1 && current_cpu) {
        uint32_t idx = current_cpu->cpu_index - s->hartid_base;

        if (idx < s->num_harts) {
            return idx;
        }
    }

    return 0;
}

static inline CPURISCVState *get_env_by_hartid(NucleiSYSTIMERState *s,
                                               uint32_t hart_idx)
{
    CPUState *cpu = qemu_get_cpu(s->hartid_base + hart_idx);

    return cpu ? cpu_env(cpu) : NULL;
}

static uint64_t nuclei_cpu_riscv_read_rtc_raw(uint32_t timebase_freq)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                    timebase_freq, NANOSECONDS_PER_SECOND);
}

static uint64_t nuclei_cpu_riscv_read_rtc(void *opaque)
{
    NucleiSYSTIMERState *s = opaque;

    return nuclei_cpu_riscv_read_rtc_raw(s->timebase_freq) + s->time_delta;
}

static void nuclei_systimer_set_mtime(NucleiSYSTIMERState *s, uint64_t value)
{
    uint64_t rtc_r = nuclei_cpu_riscv_read_rtc_raw(s->timebase_freq);

    s->time_delta = value - rtc_r;
    if (s->mtimectl & MTIMECTL_TIMESTOP) {
        s->time_stop = value;
    }
}

static uint64_t nuclei_systimer_get_mtime(NucleiSYSTIMERState *s)
{
    /*
     * TIMESTOP freezes the visible MTIME value. Otherwise MTIME follows the
     * ACLINT-style raw_rtc + time_delta model while the counter is running.
     */
    if (s->mtimectl & MTIMECTL_TIMESTOP) {
        return s->time_stop;
    }

    return nuclei_cpu_riscv_read_rtc_raw(s->timebase_freq) + s->time_delta;
}

static void nuclei_systimer_set_timer_irq(NucleiSYSTIMERState *s,
                                          uint32_t hart_idx, bool level)
{
    CPURISCVState *env = get_env_by_hartid(s, hart_idx);

    if (!env) {
        return;
    }

    if (riscv_intc_is_clic_mode(env)) {
        qemu_set_irq(s->timer_irq[hart_idx], level);
    } else {
        riscv_cpu_update_mip(env, MIP_MTIP, BOOL_TO_MASK(level));
    }
}

static void set_mtimecmp(NucleiSYSTIMERState *s, uint32_t hart_idx,
                         uint64_t value)
{
    CPURISCVState *env = get_env_by_hartid(s, hart_idx);
    uint32_t timebase_freq = s->timebase_freq;
    uint64_t next;
    uint64_t diff;

    if (!env) {
        return;
    }

    uint64_t rtc = nuclei_cpu_riscv_read_rtc(s);

    env->mtimecmp = value;
    if (env->mtimecmp <= rtc) {
        /*
         * If we're setting an MTIMECMP value in the "past",
         * immediately raise the timer interrupt
         */
        nuclei_systimer_set_timer_irq(s, hart_idx, true);
        return;
    }

    /* otherwise, set up the future timer interrupt */
    nuclei_systimer_set_timer_irq(s, hart_idx, false);
    diff = env->mtimecmp - rtc;
    /* back to ns (note args switched in muldiv64) */
    uint64_t ns_diff = muldiv64(diff, NANOSECONDS_PER_SECOND, timebase_freq);

    /*
     * check if ns_diff overflowed and check if the addition would potentially
     * overflow
     */
    if ((NANOSECONDS_PER_SECOND > timebase_freq && ns_diff < diff) ||
        ns_diff > INT64_MAX) {
        next = INT64_MAX;
    } else {
        /*
         * as it is very unlikely qemu_clock_get_ns will return a value
         * greater than INT64_MAX, no additional check is needed for an
         * unsigned integer overflow.
         */
        next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns_diff;
        /*
         * if ns_diff is INT64_MAX next may still be outside the range
         * of a signed integer.
         */
        next = MIN(next, INT64_MAX);
    }

    timer_mod(env->mtimer, next);
}

static void nuclei_systimer_update_all_timers(NucleiSYSTIMERState *s)
{
    uint64_t mtime = nuclei_systimer_get_mtime(s);
    bool cmpclren_hit = false;

    /*
     * Re-check every hart against one shared MTIME snapshot whenever software
     * rewrites the shared timer state.
     */
    for (uint32_t i = 0; i < s->num_harts; i++) {
        CPURISCVState *env = get_env_by_hartid(s, i);

        if (!env) {
            continue;
        }

        if (env->mtimer) {
            timer_del(env->mtimer);
        }

        if (env->mtimecmp == UINT64_MAX) {
            nuclei_systimer_set_timer_irq(s, i, false);
            continue;
        }

        if (mtime >= env->mtimecmp) {
            nuclei_systimer_set_timer_irq(s, i, true);
            if (s->mtimectl & MTIMECTL_CMPCLREN) {
                cmpclren_hit = true;
            }
        } else {
            nuclei_systimer_set_timer_irq(s, i, false);
        }
    }

    if (cmpclren_hit) {
        nuclei_systimer_set_mtime(s, 0);
    }

    if (s->mtimectl & MTIMECTL_TIMESTOP) {
        return;
    }

    for (uint32_t i = 0; i < s->num_harts; i++) {
        CPURISCVState *env = get_env_by_hartid(s, i);

        if (!env || env->mtimecmp == UINT64_MAX || mtime >= env->mtimecmp) {
            continue;
        }

        set_mtimecmp(s, i, env->mtimecmp);
    }
}

static void update_ssip(NucleiSYSTIMERState *s, uint32_t hart_idx,
                        uint32_t level)
{
    CPURISCVState *env = get_env_by_hartid(s, hart_idx);

    if (!env) {
        return;
    }

    s->ssip[hart_idx] = level;
    if (riscv_intc_is_clic_mode(env)) {
        qemu_set_irq(s->s_soft_irq[hart_idx], level);
    } else {
        riscv_cpu_update_mip(env, MIP_SSIP, BOOL_TO_MASK(level));
    }
}

static void update_msip(NucleiSYSTIMERState *s, uint32_t hart_idx, bool level)
{
    CPURISCVState *env = get_env_by_hartid(s, hart_idx);

    if (!env) {
        return;
    }

    s->msip[hart_idx] = level ? 1 : 0;
    if (riscv_intc_is_clic_mode(env)) {
        qemu_set_irq(s->m_soft_irq[hart_idx], level);
    } else {
        riscv_cpu_update_mip(env, MIP_MSIP, BOOL_TO_MASK(level));
    }
}

static uint64_t nuclei_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    NucleiSYSTIMERState *s = NUCLEI_SYSTIMER(opaque);
    uint32_t hart_idx = current_hartid(s);
    CPURISCVState *env = get_env_by_hartid(s, hart_idx);
    CPURISCVState *hart0_env = get_env_by_hartid(s, 0);
    bool srw_blocked;

    if (!env) {
        return 0;
    }

    srw_blocked = env->priv == PRV_U ||
                  (env->priv == PRV_S && s->mtime_srw_ctrl == 1);

    /* Array windows expose per-hart MSIP/MTIMECMP/SSIP words. */
    if (offset >= REG_MSIP_BASE && offset < REG_MSIP_BASE + s->num_harts * 4) {
        uint32_t idx = (offset - REG_MSIP_BASE) / 4;

        if (srw_blocked || idx >= s->num_harts) {
            return 0;
        }
        return s->msip[idx];
    }

    if (offset >= REG_MTIMECMP_BASE &&
        offset < REG_MTIMECMP_BASE + s->num_harts * 8) {
        uint32_t idx = (offset - REG_MTIMECMP_BASE) / 8;
        CPURISCVState *h_env;

        if (srw_blocked || idx >= s->num_harts) {
            return 0;
        }

        h_env = get_env_by_hartid(s, idx);
        if (!h_env) {
            return 0;
        }

        if ((offset & 0x7) == 0) {
            return h_env->mtimecmp & 0xffffffffu;
        }
        return (h_env->mtimecmp >> 32) & 0xffffffffu;
    }

    if (offset >= REG_SSIP_BASE && offset < REG_SSIP_BASE + s->num_harts * 4) {
        uint32_t idx = (offset - REG_SSIP_BASE) / 4;

        if (env->priv == PRV_U || idx >= s->num_harts) {
            return 0;
        }
        return s->ssip[idx];
    }

    switch (offset) {
    case REG_MTIME_LO:
        return srw_blocked ? 0 : (nuclei_systimer_get_mtime(s) & 0xffffffffu);
    case REG_MTIME_HI:
        return srw_blocked ? 0 :
               ((nuclei_systimer_get_mtime(s) >> 32) & 0xffffffffu);
    case REG_MTIMECMP_LO:
        /*
         * Spec 13.1 defines the local MTIMECMP shadow window as the 1st-hart
         * copy in CLINT mode, not a current-hart alias.
         */
        return (srw_blocked || !hart0_env) ? 0 :
               (hart0_env->mtimecmp & 0xffffffffu);
    case REG_MTIMECMP_HI:
        return (srw_blocked || !hart0_env) ? 0 :
               ((hart0_env->mtimecmp >> 32) & 0xffffffffu);
    case REG_MTIME_SRW_CTRL:
        return env->priv == PRV_M ? s->mtime_srw_ctrl : 0;
    case REG_MSFTRST:
        return srw_blocked ? 0 : s->msftrst;
    case REG_SSIP:
        /* The single-register SSIP alias is the hart0 shadow copy. */
        return env->priv == PRV_U ? 0 : s->ssip[0];
    case REG_MTIMECTL:
        return srw_blocked ? 0 : s->mtimectl;
    case REG_MSIP:
        /* The single-register MSIP alias is the hart0 shadow copy. */
        return srw_blocked ? 0 : s->msip[0];
    /* 0xcff8 is the shared MTIME alias defined by the Nuclei timer window. */
    case REG_MTIME:
        return srw_blocked ? 0 : (nuclei_systimer_get_mtime(s) & 0xffffffffu);
    case REG_MTIME + 4:
        return srw_blocked ? 0 :
               ((nuclei_systimer_get_mtime(s) >> 32) & 0xffffffffu);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "systimer: read at invalid offset 0x%" HWADDR_PRIx "\n",
                      offset);
        return 0;
    }
}

static void nuclei_timer_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    NucleiSYSTIMERState *s = NUCLEI_SYSTIMER(opaque);
    uint32_t hart_idx = current_hartid(s);
    CPURISCVState *env = get_env_by_hartid(s, hart_idx);
    CPURISCVState *hart0_env = get_env_by_hartid(s, 0);
    bool srw_blocked;

    if (!env) {
        return;
    }

    value &= 0xffffffffu;
    srw_blocked = env->priv == PRV_U ||
                  (env->priv == PRV_S && s->mtime_srw_ctrl == 1);

    /* Array windows expose per-hart MSIP/MTIMECMP/SSIP words. */
    if (offset >= REG_MSIP_BASE && offset < REG_MSIP_BASE + s->num_harts * 4) {
        uint32_t idx = (offset - REG_MSIP_BASE) / 4;

        if (!srw_blocked && idx < s->num_harts) {
            update_msip(s, idx, value & 0x1);
        }
        return;
    }

    if (offset >= REG_MTIMECMP_BASE &&
        offset < REG_MTIMECMP_BASE + s->num_harts * 8) {
        uint32_t idx = (offset - REG_MTIMECMP_BASE) / 8;
        CPURISCVState *h_env;
        uint64_t cmp;

        if (srw_blocked || idx >= s->num_harts) {
            return;
        }

        h_env = get_env_by_hartid(s, idx);
        if (!h_env) {
            return;
        }

        cmp = h_env->mtimecmp;
        if ((offset & 0x7) == 0) {
            cmp = (cmp & 0xffffffff00000000ULL) | value;
        } else {
            cmp = (cmp & 0xffffffffULL) | (value << 32);
        }
        set_mtimecmp(s, idx, cmp);
        return;
    }

    if (offset >= REG_SSIP_BASE && offset < REG_SSIP_BASE + s->num_harts * 4) {
        uint32_t idx = (offset - REG_SSIP_BASE) / 4;

        if (env->priv != PRV_U && idx < s->num_harts) {
            update_ssip(s, idx, value & 0x1);
        }
        return;
    }

    switch (offset) {
    case REG_MTIME_LO:
    {
        uint64_t mtime;

        if (srw_blocked) {
            return;
        }

        /*
         * MTIME is a shared 64-bit register exposed as two 32-bit words, so
         * merge the incoming half with the current visible MTIME value.
         */
        mtime = nuclei_systimer_get_mtime(s);
        mtime = (mtime & 0xffffffff00000000ULL) | value;
        nuclei_systimer_set_mtime(s, mtime);
        nuclei_systimer_update_all_timers(s);
        return;
    }
    case REG_MTIME_HI:
    {
        uint64_t mtime;

        if (srw_blocked) {
            return;
        }

        mtime = nuclei_systimer_get_mtime(s);
        mtime = (mtime & 0xffffffffULL) | (value << 32);
        nuclei_systimer_set_mtime(s, mtime);
        nuclei_systimer_update_all_timers(s);
        return;
    }
    case REG_MTIMECMP_LO:
    {
        uint64_t cmp;

        if (srw_blocked || !hart0_env) {
            return;
        }

        /*
         * Spec 13.1 defines the local MTIMECMP shadow window as the 1st-hart
         * copy in CLINT mode, so local writes always target hart0.
         */
        cmp = hart0_env->mtimecmp;
        cmp = (cmp & 0xffffffff00000000ULL) | value;
        set_mtimecmp(s, 0, cmp);
        return;
    }
    case REG_MTIMECMP_HI:
    {
        uint64_t cmp;

        if (srw_blocked || !hart0_env) {
            return;
        }

        cmp = hart0_env->mtimecmp;
        cmp = (cmp & 0xffffffffULL) | (value << 32);
        set_mtimecmp(s, 0, cmp);
        return;
    }
    case REG_MTIME_SRW_CTRL:
        if (env->priv == PRV_M) {
            s->mtime_srw_ctrl = value & 0x1;
        }
        return;
    case REG_MSFTRST:
        if (!srw_blocked && value == MSFTRST_MAGIC) {
            s->msftrst = 0x80000000u;
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        return;
    case REG_SSIP:
        if (env->priv != PRV_U) {
            update_ssip(s, 0, value & 0x1);
        }
        return;
    case REG_MTIMECTL:
        if (!srw_blocked) {
            uint32_t new_mtimectl = MTIMECTL_HDBG |
                                    (value & MTIMECTL_RW_MASK);
            uint64_t mtime;

            if ((s->mtimectl ^ new_mtimectl) & MTIMECTL_TIMESTOP) {
                /*
                 * Freeze the currently visible MTIME on stop, or resume from
                 * that same visible value when TIMESTOP is cleared.
                 */
                mtime = nuclei_systimer_get_mtime(s);
                if (new_mtimectl & MTIMECTL_TIMESTOP) {
                    s->time_stop = mtime;
                    s->mtimectl = new_mtimectl;
                } else {
                    s->mtimectl = new_mtimectl;
                    nuclei_systimer_set_mtime(s, mtime);
                }
            } else {
                s->mtimectl = new_mtimectl;
            }
            nuclei_systimer_update_all_timers(s);
        }
        return;
    case REG_MSIP:
        if (!srw_blocked) {
            update_msip(s, 0, value & 0x1);
        }
        return;
    /* 0xcff8 is the shared MTIME alias and still only accepts WORD writes. */
    case REG_MTIME:
    {
        uint64_t mtime;

        if (srw_blocked) {
            return;
        }

        mtime = nuclei_systimer_get_mtime(s);
        mtime = (mtime & 0xffffffff00000000ULL) | value;
        nuclei_systimer_set_mtime(s, mtime);
        nuclei_systimer_update_all_timers(s);
        return;
    }
    case REG_MTIME + 4:
    {
        uint64_t mtime;

        if (srw_blocked) {
            return;
        }

        mtime = nuclei_systimer_get_mtime(s);
        mtime = (mtime & 0xffffffffULL) | (value << 32);
        nuclei_systimer_set_mtime(s, mtime);
        nuclei_systimer_update_all_timers(s);
        return;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "systimer: write at invalid offset 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n",
                      offset, value);
        return;
    }
}

static const MemoryRegionOps nuclei_timer_ops = {
    .read = nuclei_timer_read,
    .write = nuclei_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void nuclei_systimer_cb(void *opaque)
{
    CPUState *cs = opaque;
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    NucleiSYSTIMERState *s = NUCLEI_SYSTIMER(env->systimer);
    uint32_t hart_idx = cs->cpu_index - s->hartid_base;

    if (hart_idx >= s->num_harts) {
        return;
    }

    if (s->mtimectl & MTIMECTL_CMPCLREN) {
        nuclei_systimer_update_all_timers(s);
        return;
    }

    nuclei_systimer_set_timer_irq(s, hart_idx, true);
}

static void nuclei_systimer_reset(DeviceState *dev)
{
    NucleiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);

    s->mtime_srw_ctrl = 0;
    s->msftrst = 0;
    s->mtimectl = MTIMECTL_HDBG;
    s->time_delta = 0;
    s->time_stop = 0;
    nuclei_systimer_set_mtime(s, 0);

    for (uint32_t i = 0; i < s->num_harts; i++) {
        CPURISCVState *env = get_env_by_hartid(s, i);

        s->msip[i] = 0;
        s->ssip[i] = 0;

        if (!env) {
            continue;
        }

        /*
         * Keep the reset-time local compare inactive until software programs
         * a real value. This matches the SDK expectation that timer demos arm
         * MTIMECMP explicitly instead of taking an interrupt immediately after
         * global interrupt enable.
         */
        env->mtimecmp = UINT64_MAX;
        if (env->mtimer) {
            timer_del(env->mtimer);
        }
        nuclei_systimer_set_timer_irq(s, i, false);

        if (riscv_intc_is_clic_mode(env)) {
            qemu_set_irq(s->m_soft_irq[i], 0);
            qemu_set_irq(s->s_soft_irq[i], 0);
        } else {
            riscv_cpu_update_mip(env, MIP_MSIP, BOOL_TO_MASK(0));
            riscv_cpu_update_mip(env, MIP_SSIP, BOOL_TO_MASK(0));
        }
    }
}

static void nuclei_systimer_realize(DeviceState *dev, Error **errp)
{
    NucleiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);
    int i;

    s->msip = g_new0(uint32_t, s->num_harts);
    s->ssip = g_new0(uint32_t, s->num_harts);
    s->timer_irq = g_new0(qemu_irq, s->num_harts);
    s->m_soft_irq = g_new0(qemu_irq, s->num_harts);
    s->s_soft_irq = g_new0(qemu_irq, s->num_harts);

    memory_region_init_io(&s->iomem, OBJECT(dev), &nuclei_timer_ops,
                          s, TYPE_NUCLEI_SYSTIMER, 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    for (i = 0; i < s->num_harts; i++) {
        CPUState *cpu = qemu_get_cpu(s->hartid_base + i);
        CPURISCVState *env;

        if (!cpu) {
            continue;
        }

        env = cpu_env(cpu);
        env->mtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &nuclei_systimer_cb,
                                   cpu);
        env->systimer = s;
        env->mtimecmp = UINT64_MAX;

        if (s->eclic) {
            s->timer_irq[i] = nuclei_eclic_get_irq(s->eclic,
                                                   Internal_SysTimer_IRQn, i);
            s->m_soft_irq[i] = nuclei_eclic_get_irq(s->eclic,
                                                    Internal_SysTimerSW_IRQn,
                                                    i);
            s->s_soft_irq[i] = nuclei_eclic_get_irq(s->eclic,
                                                    Internal_SysTimerSW_S_IRQn,
                                                    i);
        }
        riscv_cpu_set_rdtime_fn(env, nuclei_cpu_riscv_read_rtc, s);
    }

    s->mtime_srw_ctrl = 0;
    s->msftrst = 0;
    s->mtimectl = MTIMECTL_HDBG;
    s->time_delta = 0;
    s->time_stop = 0;
    nuclei_systimer_set_mtime(s, 0);
}

static Property nuclei_systimer_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", NucleiSYSTIMERState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", NucleiSYSTIMERState, num_harts, 0),
    DEFINE_PROP_UINT32("aperture-size", NucleiSYSTIMERState, aperture_size, 0),
    DEFINE_PROP_UINT64("timebase-freq", NucleiSYSTIMERState, timebase_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_systimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nuclei_systimer_realize;
    dc->reset = nuclei_systimer_reset;
    dc->desc = "Nuclei Systimer";
    device_class_set_props(dc, nuclei_systimer_properties);
}

static const TypeInfo nuclei_systimer_info = {
    .name = TYPE_NUCLEI_SYSTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiSYSTIMERState),
    .class_init = nuclei_systimer_class_init,
};

static void nuclei_systimer_register_types(void)
{
    type_register_static(&nuclei_systimer_info);
}

type_init(nuclei_systimer_register_types);

DeviceState *nuclei_systimer_create(hwaddr addr, hwaddr size,
                                    uint32_t hartid_base, uint32_t num_harts,
                                    DeviceState *eclic,
                                    uint32_t timebase_freq)
{
    DeviceState *dev = qdev_new(TYPE_NUCLEI_SYSTIMER);
    NucleiSYSTIMERState *s = NUCLEI_SYSTIMER(dev);

    assert(num_harts <= NUCLEI_SYSTIMER_MAX_HARTS);

    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "aperture-size", size);
    qdev_prop_set_uint64(dev, "timebase-freq", timebase_freq);
    if (eclic) {
        s->eclic = eclic;
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    return dev;
}
