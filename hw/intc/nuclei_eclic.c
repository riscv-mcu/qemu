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

static void nuclei_eclic_update_intmth(NucleiECLICState *eclic, int irq, int hartid, int mth);
static void nuclei_eclic_update_intip(NucleiECLICState *eclic, int irq, int hartid, int new_intip);
static void nuclei_eclic_update_intie(NucleiECLICState *eclic, int mode, int irq, int hartid, int new_intie);
static void nuclei_eclic_update_intattr(NucleiECLICState *eclic, int mode, int irq, int hartid, int new_intattr);
static void nuclei_eclic_update_intctl(NucleiECLICState *eclic, int mode, int irq, int hartid, int new_intctl);
static void eclic_insert_pending_list(NucleiECLICState *eclic, int irq, int hartid);
static void eclic_insert_pending_list_s(NucleiECLICState *eclic, int irq, int hartid);
static void eclic_remove_pending_list(NucleiECLICState *eclic, int mode, int irq, int hartid);
static void update_eclic_int_info(NucleiECLICState *eclic, int irq, int hartid);
static void update_eclic_int_info_s(NucleiECLICState *eclic, int irq, int hartid);
static void nuclei_eclic_update_intsth(NucleiECLICState *eclic, int irq, int hartid, int sth);
static void nuclei_eclic_update_intip_s(NucleiECLICState *eclic, int irq, int hartid, int new_intip);

/*
6'b000011:clic
else:     clint
 */
bool riscv_intc_is_clic_mode(CPUArchState *env)
{
    // Currently, eclic is only marked in mtvec.
    return env->eclic && ((env->mtvec & 0x3F) == 3);
}

qemu_irq nuclei_eclic_get_irq(DeviceState *dev, int irq, int hartid)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);
    return eclic->irqs[hartid][irq];
}

static inline int nuclei_eclic_get_current_cpu(NucleiECLICState *eclic)
{
    if (eclic->num_harts > 1)
    {
        return current_cpu ? current_cpu->cpu_index : 0;
    }
    return 0;
}

static uint64_t nuclei_eclic_read(void *opaque, hwaddr offset, unsigned size)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(opaque);
    uint64_t value = 0;
    uint32_t irq = 0;
    uint32_t shift = 0;
    uint32_t hartid = nuclei_eclic_get_current_cpu(eclic);

    if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE_S) {
        shift = offset & 0x3;
        irq = (offset - shift - 0x3000) / 4;
        //返回寄存器列表
        offset = offset - 4 * irq;
    } else if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE) {
        shift = offset & 0x3;
        irq = (offset - shift - 0x1000) / 4;
        //返回寄存器列表
        offset = offset - 4 * irq;
    }

    switch (offset)
    {
    case NUCLEI_ECLIC_REG_CLICCFG:
        value = eclic->cliccfg[hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINFO:
        value = (CLICINTCTLBITS << 21) | (0x1 << 13) | eclic->num_sources;
        break;
    case NUCLEI_ECLIC_REG_MINTTHRESH:
        value = ((uint32_t)eclic->mth[hartid] << 24);
        break;
    case NUCLEI_ECLIC_REG_MTH:
        value = eclic->mth[hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE:
        if (size == 4) {
            value = (uint32_t)eclic->clicintip[hartid][irq] | ((uint32_t)eclic->clicintie[hartid][irq] << 8) | \
                    ((uint32_t)eclic->clicintattr[hartid][irq] << 16) | ((uint32_t)eclic->clicintctl[hartid][irq] << 24);
        } else if (size == 2) {
            value = (uint32_t)eclic->clicintip[hartid][irq] | ((uint32_t)eclic->clicintie[hartid][irq] << 8);
        } else {
            value = eclic->clicintip[hartid][irq] & 0xFF;
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE:
        value = eclic->clicintie[hartid][irq] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE:
        if (size == 2) {
            value = ((uint32_t)eclic->clicintattr[hartid][irq]) | ((uint32_t)eclic->clicintctl[hartid][irq] << 8);
        } else {
            value = eclic->clicintattr[hartid][irq] & 0xFF;
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE:
        value = eclic->clicintctl[hartid][irq] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_SINTTHRESH:
        value = ((uint32_t)eclic->sth[hartid] << 8);
        break;
    case NUCLEI_ECLIC_REG_STH:
        value = eclic->sth[hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE_S:
        if (size == 4) {
            value = (uint32_t)eclic->clicintip_s[hartid][irq] | ((uint32_t)eclic->clicintie_s[hartid][irq] << 8) | \
                    ((uint32_t)eclic->clicintattr_s[hartid][irq] << 16) | ((uint32_t)eclic->clicintctl_s[hartid][irq] << 24);
        } else if (size == 2) {
            value = (uint32_t)eclic->clicintip_s[hartid][irq] | ((uint32_t)eclic->clicintie_s[hartid][irq] << 8);
        } else {
            value = eclic->clicintip_s[hartid][irq] & 0xFF;
        }
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE_S:
        value = eclic->clicintie_s[hartid][irq] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE_S:
        value = eclic->clicintattr_s[hartid][irq] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE_S:
        value = eclic->clicintctl_s[hartid][irq] & 0xFF;
        break;
    default:
        break;
    }

    return value;
}

//eclic

// 0x0000 cliccfg   全局配置寄存器， [4:1] 指定 clicintctl[i]的Level参数
// 0x0004 clicinfo
// 0x000b mth       设置中断的阈值
// 0x1000+4*i clicintip[i] 中断源的等待标志寄存器 IP 0：等待标志
// 0x1001+4*i clicintie[i] 中断源的使能寄存器 IE 0: 使能标志
// 0x1002+4*i clicintattr[i] 中断源的属性寄存器 [2:1] trig 中断边沿寄存器  0 shv 向量模式与非向量模式
// 0x1003+4*i clicintctl[i]  中断源控制寄存器
static void nuclei_eclic_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(opaque);
    uint32_t irq = 0;
    uint32_t hartid = nuclei_eclic_get_current_cpu(eclic);
    uint32_t shift = 0;

    if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE_S) {
        shift = offset & 0x3;
        irq = (offset - shift - 0x3000) / 4;
        //返回寄存器列表
        offset = offset - 4 * irq;
    } else if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE) {
        shift = offset & 0x3;
        irq = (offset - shift - 0x1000) / 4;
        //返回寄存器列表
        offset = offset - 4 * irq;
    }
    switch (offset)
    {
    case NUCLEI_ECLIC_REG_CLICCFG:
        eclic->cliccfg[hartid] = value & 0xFF;
        for (irq = 0; irq < eclic->num_sources; irq++)
        {
            update_eclic_int_info_s(eclic, irq, hartid);
            update_eclic_int_info(eclic, irq, hartid);
        }
        break;
    case NUCLEI_ECLIC_REG_MINTTHRESH:
        if (size == 4) {
            nuclei_eclic_update_intmth(eclic, irq, hartid, (value >> 24) & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_MTH:
        nuclei_eclic_update_intmth(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE:
        if ((eclic->clicintlist[hartid][irq].trigger & 0x1) != 0)
        {
            if ((eclic->clicintip[hartid][irq] == 0) && (value & 0x1) == 1)
            {
                eclic->clicintip[hartid][irq] = 1;
                eclic_insert_pending_list(eclic, irq, hartid);
            }
            else if ((eclic->clicintip[hartid][irq] == 1) && (value & 0x1) == 0)
            {
                eclic->clicintip[hartid][irq] = 0;
                eclic_remove_pending_list(eclic, PRV_M, irq, hartid);
            }
        }
        nuclei_eclic_next_interrupt(eclic, PRV_M, hartid);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE:
        nuclei_eclic_update_intie(eclic, PRV_M, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE:
        nuclei_eclic_update_intattr(eclic, PRV_M, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE:
        nuclei_eclic_update_intctl(eclic, PRV_M, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_SINTTHRESH:
        if (size == 4) {
            nuclei_eclic_update_intsth(eclic, irq, hartid, (value >> 24) & 0xFF);
        }
        break;
    case NUCLEI_ECLIC_REG_STH:
        nuclei_eclic_update_intsth(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE_S:
        if ((eclic->clicintlist_s[hartid][irq].trigger & 0x1) != 0)
        {
            if ((eclic->clicintip_s[hartid][irq] == 0) && (value & 0x1) == 1)
            {
                eclic->clicintip_s[hartid][irq] = 1;
                eclic_insert_pending_list_s(eclic, irq, hartid);
            }
            else if ((eclic->clicintip_s[hartid][irq] == 1) && (value & 0x1) == 0)
            {
                eclic->clicintip_s[hartid][irq] = 0;
                eclic_remove_pending_list(eclic, PRV_S, irq, hartid);
            }
        }
        nuclei_eclic_next_interrupt(eclic, PRV_S, hartid);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE_S:
        nuclei_eclic_update_intie(eclic, PRV_S, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE_S:
        nuclei_eclic_update_intattr(eclic, PRV_S, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE_S:
        nuclei_eclic_update_intctl(eclic, PRV_S, irq, hartid, value & 0xFF);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps nuclei_eclic_ops = {
    .read = nuclei_eclic_read,
    .write = nuclei_eclic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    // .valid = {
    //     .min_access_size = 4,
    //     .max_access_size = 4
    // }
};

static Property nuclei_eclic_properties[] = {
    DEFINE_PROP_BOOL("prv-s", NucleiECLICState, prv_s, false),
    DEFINE_PROP_BOOL("prv-u", NucleiECLICState, prv_u, false),
    DEFINE_PROP_BOOL("vector", NucleiECLICState, nvbits, false),
    DEFINE_PROP_UINT32("num-harts", NucleiECLICState, num_harts, 0),
    DEFINE_PROP_UINT32("eclicintctlbits", NucleiECLICState, eclicintctlbits, 0),
    DEFINE_PROP_UINT32("aperture-size", NucleiECLICState, aperture_size, 0),
    DEFINE_PROP_UINT32("num-sources", NucleiECLICState, num_sources, 0),
    DEFINE_PROP_UINT64("mclicbase", NucleiECLICState, mclicbase, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void update_eclic_int_info(NucleiECLICState *eclic, int irq, int hartid)
{
    int level_width = (eclic->cliccfg[hartid] >> 1) & 0xF; // cliccfg.nlbits
    if (level_width > CLICINTCTLBITS)
        level_width = CLICINTCTLBITS;
    int prio_width = CLICINTCTLBITS - level_width;

    if (level_width == 0)
        eclic->clicintlist[hartid][irq].level = 255;
    else
        eclic->clicintlist[hartid][irq].level = (((eclic->clicintctl[hartid][irq] >> (8 - level_width)) &
                                          ~((char)0x80 >> (8 - level_width)))
                                         << (8 - level_width)) |
                                        (0xff >> level_width);

    // TODO: implement priority decode logic when width > CLICINTCTLBITS or zeros
    if (prio_width == 0)
        eclic->clicintlist[hartid][irq].prio = 0;
    else
        eclic->clicintlist[hartid][irq].prio = (eclic->clicintctl[hartid][irq] >> (8 - level_width)) &
                                       ~(0x80 >> (8 - prio_width));
    eclic->clicintlist[hartid][irq].enable = eclic->clicintie[hartid][irq] & 0x1;
    // 0, level triggered; 2, rising edge; 3, falling edge
    eclic->clicintlist[hartid][irq].trigger = (eclic->clicintattr[hartid][irq] >> 1) & 0x3;
}

bool nuclei_eclic_shv_interrupt(void *opaque, int mode, int hartid, int irq)
{
    NucleiECLICState *eclic = (NucleiECLICState *)opaque;
    int shv;
    shv = ((mode <= PRV_S) ? eclic->clicintattr_s[hartid][irq] : eclic->clicintattr[hartid][irq]) & 0x1;
    return shv;
}

bool nuclei_eclic_edge_triggered(void *opaque, int mode, int hartid, int irq)
{
    NucleiECLICState *eclic = (NucleiECLICState *)opaque;
        return (((mode <= PRV_S) ? eclic->clicintattr_s[hartid][irq]
                : eclic->clicintattr[hartid][irq]) >> 1) & 0x1;
}

void nuclei_eclic_clean_pending(void *opaque, int mode, int hartid, int irq)
{
    NucleiECLICState *eclic = (NucleiECLICState *)opaque;
    if (mode <= PRV_S) {
        eclic->clicintip_s[hartid][irq] = 0;
    } else {
        eclic->clicintip[hartid][irq] = 0;
    }
    eclic_remove_pending_list(eclic, mode, irq, hartid);
}

static void update_eclic_int_info_s(NucleiECLICState *eclic, int irq, int hartid)
{
    int level_width = (eclic->cliccfg[hartid] >> 1) & 0xF; // cliccfg.nlbits
    if (level_width > CLICINTCTLBITS)
        level_width = CLICINTCTLBITS;
    int prio_width = CLICINTCTLBITS - level_width;

    if (level_width == 0)
        eclic->clicintlist_s[hartid][irq].level = 255;
    else
        eclic->clicintlist_s[hartid][irq].level = (((eclic->clicintctl_s[hartid][irq] >> (8 - level_width)) &
                                          ~((char)0x80 >> (8 - level_width)))
                                         << (8 - level_width)) |
                                        (0xff >> level_width);

    // TODO: implement priority decode logic when width > CLICINTCTLBITS or zeros
    if (prio_width == 0)
        eclic->clicintlist_s[hartid][irq].prio = 0;
    else
        eclic->clicintlist_s[hartid][irq].prio = (eclic->clicintctl_s[hartid][irq] >> (8 - level_width)) &
                                       ~(0x80 >> (8 - prio_width));
    eclic->clicintlist_s[hartid][irq].enable = eclic->clicintie_s[hartid][irq] & 0x1;
    // 0, level triggered; 2, rising edge; 3, falling edge
    eclic->clicintlist_s[hartid][irq].trigger = (eclic->clicintattr_s[hartid][irq] >> 1) & 0x3;
}

void nuclei_eclic_next_interrupt(void *eclic_ptr, int mode, int hartid)
{
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(hartid));
    NucleiECLICState *eclic = (NucleiECLICState *)eclic_ptr;
    ECLICPendingInterrupt *active;
    int exccode;

    if (mode <= PRV_S) {
        QLIST_FOREACH(active, &eclic->pending_list_s[hartid], next)
        {
            if (active->enable)
            {
                if (active->level >= eclic->sth[hartid])
                {
                    exccode = active->irq | mode << 12 | active->level << 14;
                    eclic->exccode[0] = exccode;
                    eclic->active_count_s++;
                    riscv_cpu_eclic_interrupt(cpu, exccode);
                    return;
                }
            }
        }
    } else {
        QLIST_FOREACH(active, &eclic->pending_list[hartid], next)
        {
            if (active->enable)
            {
                if (active->level >= eclic->mth[hartid])
                {
                    exccode = active->irq | mode << 12 | active->level << 14;
                    eclic->exccode[0] = exccode;
                    eclic->active_count++;
                    riscv_cpu_eclic_interrupt(cpu, exccode);
                    return;
                }
            }
        }
    }
    riscv_cpu_eclic_interrupt(cpu, -1);
}

void riscv_cpu_eclic_int_handler_start(void *eclic_ptr, int mode, int irq, int hartid)
{
    NucleiECLICState *eclic = (NucleiECLICState *)eclic_ptr;
    if ((eclic->clicintlist[hartid][irq].trigger & 0x1) != 0)
    {
        eclic->clicintip[hartid][irq] = 0;
        eclic_remove_pending_list(eclic, mode, irq, hartid);
    }
    nuclei_eclic_next_interrupt(eclic, mode, hartid);
}

void riscv_cpu_eclic_int_handler_start_s(void *eclic_ptr, int mode, int irq, int hartid)
{
    NucleiECLICState *eclic = (NucleiECLICState *)eclic_ptr;
    if ((eclic->clicintlist_s[hartid][irq].trigger & 0x1) != 0)
    {
        eclic->clicintip_s[hartid][irq] = 0;
        eclic_remove_pending_list(eclic, mode, irq, hartid);
    }
    nuclei_eclic_next_interrupt(eclic, mode, hartid);
}

static int level_compare(NucleiECLICState *eclic, ECLICPendingInterrupt *irq1, ECLICPendingInterrupt *irq2)
{
    if (irq1->level == irq2->level)
    {
        if (irq1->prio == irq2->prio)
        {
            if (irq1->irq >= irq2->irq)
            {
                // put irq2 behind
                return 0;
            }
            else
            {
                // irq2 before irq1
                return 1;
            }
        }
        else if (irq1->prio > irq2->level)
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }
    else if (irq1->level > irq2->level)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}


extern uint32_t coren_int_16;
extern uint32_t cidu_int_indicator;

void nuclei_eclic_irq_request(void *opaque, int id, int new_intip)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(opaque);
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(nuclei_eclic_get_current_cpu(opaque)));
    CPURISCVState *env = &cpu->env;

    if (id < Internal_Reserved_Max_IRQn)
    {
        if (id == Internal_Reserved14_IRQn)
        {
            nuclei_eclic_update_intip(eclic, id, coren_int_16, new_intip);
        }
        else
        {
            for (int i = 0; i < eclic->num_harts; i++)
            {
                if (eclic->clicintie_s[i][id] & 0x1) {
                    nuclei_eclic_update_intip_s(eclic, id, i, new_intip);
                } else {
                    nuclei_eclic_update_intip(eclic, id, i, new_intip);
                }
            }
        }
    }
    else
    {
        for(int i = 0; i < eclic->num_harts; i++)
        {
            if (cidu_int_indicator != 0) {
                if(cidu_int_indicator & (1U << i))
                    nuclei_eclic_update_intip(eclic, id, i, new_intip);
            } else {
                if (env->priv <= PRV_S) {
                    nuclei_eclic_update_intip_s(eclic, id, i, new_intip);
                } else {
                    nuclei_eclic_update_intip(eclic, id, i, new_intip);
                }
            }
        }
    }
}

static void nuclei_eclic_update_intmth(NucleiECLICState *eclic, int irq, int hartid, int mth)
{
    eclic->mth[hartid] = mth;
    nuclei_eclic_next_interrupt(eclic, PRV_M, hartid);
}

static void nuclei_eclic_update_intsth(NucleiECLICState *eclic, int irq, int hartid, int sth)
{
    eclic->sth[hartid] = sth;
    nuclei_eclic_next_interrupt(eclic, PRV_S, hartid);
}

static void eclic_insert_pending_list(NucleiECLICState *eclic, int irq, int hartid)
{
    ECLICPendingInterrupt *node;
    if (QLIST_EMPTY(&eclic->pending_list[hartid]))
    {
        QLIST_INSERT_HEAD(&eclic->pending_list[hartid], &eclic->clicintlist[hartid][irq], next);
    }
    else
    {
        QLIST_FOREACH(node, &eclic->pending_list[hartid], next)
        {
            if (level_compare(eclic, node, &eclic->clicintlist[hartid][irq]))
            {
                QLIST_INSERT_BEFORE(node, &eclic->clicintlist[hartid][irq], next);
                break;
            }
            else if (node->next.le_next == NULL)
            {
                QLIST_INSERT_AFTER(node, &eclic->clicintlist[hartid][irq], next);
                break;
            }
        }
    }
}

static void eclic_insert_pending_list_s(NucleiECLICState *eclic, int irq, int hartid)
{
    ECLICPendingInterrupt *node;
    if (QLIST_EMPTY(&eclic->pending_list_s[hartid]))
    {
        QLIST_INSERT_HEAD(&eclic->pending_list_s[hartid], &eclic->clicintlist_s[hartid][irq], next);
    }
    else
    {
        QLIST_FOREACH(node, &eclic->pending_list_s[hartid], next)
        {
            if (level_compare(eclic, node, &eclic->clicintlist_s[hartid][irq]))
            {
                QLIST_INSERT_BEFORE(node, &eclic->clicintlist_s[hartid][irq], next);
                break;
            }
            else if (node->next.le_next == NULL)
            {
                QLIST_INSERT_AFTER(node, &eclic->clicintlist_s[hartid][irq], next);
                break;
            }
        }
    }
}

static void eclic_remove_pending_list(NucleiECLICState *eclic, int mode, int irq, int hartid)
{
    if (mode <= PRV_S) {
        QLIST_REMOVE(&eclic->clicintlist_s[hartid][irq], next);
    } else {
        QLIST_REMOVE(&eclic->clicintlist[hartid][irq], next);
    }
}

static void nuclei_eclic_update_intip(NucleiECLICState *eclic, int irq, int hartid, int new_intip)
{
    uint32_t current_hart_id;
    current_hart_id = nuclei_eclic_get_current_cpu(eclic);

    int old_intip = eclic->clicintlist[hartid][irq].sig;
    int trigger = (eclic->clicintattr[hartid][irq] >> 1) & 0x3;
    if((old_intip == new_intip) &&  (new_intip != 0))
    {

    }
    else
    {
        if (((trigger == 0) && new_intip) ||
            ((trigger == 1) && !old_intip && new_intip) ||
            ((trigger == 3) && old_intip && !new_intip))
        {
            eclic->clicintip[hartid][irq] = 1;
            eclic->clicintlist[hartid][irq].sig = new_intip;
            eclic_insert_pending_list(eclic, irq, hartid);
        }
        else
        {
            if (eclic->clicintip[current_hart_id][irq])
                eclic_remove_pending_list(eclic, PRV_M, irq, current_hart_id);
            eclic->clicintip[current_hart_id][irq] = 0;
            eclic->clicintlist[current_hart_id][irq].sig = new_intip;
        }
    }
    nuclei_eclic_next_interrupt(eclic, PRV_M, hartid);
}

static void nuclei_eclic_update_intip_s(NucleiECLICState *eclic, int irq, int hartid, int new_intip)
{
    uint32_t current_hart_id;
    current_hart_id = nuclei_eclic_get_current_cpu(eclic);

    int old_intip = eclic->clicintlist_s[hartid][irq].sig;
    int trigger = (eclic->clicintattr_s[hartid][irq] >> 1) & 0x3;
    if((old_intip == new_intip) &&  (new_intip != 0))
    {

    }
    else
    {
        if (((trigger == 0) && new_intip) ||
            ((trigger == 1) && !old_intip && new_intip) ||
            ((trigger == 3) && old_intip && !new_intip))
        {
            eclic->clicintip_s[hartid][irq] = 1;
            eclic->clicintlist_s[hartid][irq].sig = new_intip;
            eclic_insert_pending_list_s(eclic, irq, hartid);
        }
        else
        {
            if (eclic->clicintip_s[current_hart_id][irq])
                eclic_remove_pending_list(eclic, PRV_S, irq, current_hart_id);
            eclic->clicintip_s[current_hart_id][irq] = 0;
            eclic->clicintlist_s[current_hart_id][irq].sig = new_intip;
        }
    }

    nuclei_eclic_next_interrupt(eclic, PRV_S, hartid);
}

static void nuclei_eclic_update_intie(NucleiECLICState *eclic, int mode, int irq, int hartid, int new_intie)
{
    if (mode <= PRV_S) {
        eclic->clicintie_s[hartid][irq] = new_intie;
        update_eclic_int_info_s(eclic, irq, hartid);
    } else {
        eclic->clicintie[hartid][irq] = new_intie;
        update_eclic_int_info(eclic, irq, hartid);
    }
    nuclei_eclic_next_interrupt(eclic, mode, hartid);
}

// TODO: intattr not supposed to be changed during runtime?
static void nuclei_eclic_update_intattr(NucleiECLICState *eclic, int mode, int irq, int hartid, int new_intattr)
{
    if (mode <= PRV_S) {
        eclic->clicintattr_s[hartid][irq] = new_intattr;
        update_eclic_int_info_s(eclic, irq, hartid);
    } else {
        eclic->clicintattr[hartid][irq] = new_intattr;
        update_eclic_int_info(eclic, irq, hartid);
    }
    nuclei_eclic_next_interrupt(eclic, mode, hartid);
}

// TODO: intctl not supposed to be changed during runtime?
static void nuclei_eclic_update_intctl(NucleiECLICState *eclic, int mode, int irq, int hartid, int new_intctl)
{
    if (mode <= PRV_S) {
        eclic->clicintctl_s[hartid][irq] = new_intctl;
        update_eclic_int_info_s(eclic, irq, hartid);
    } else {
        eclic->clicintctl[hartid][irq] = new_intctl;
        update_eclic_int_info(eclic, irq, hartid);
    }
    nuclei_eclic_next_interrupt(eclic, mode, hartid);
}

static void nuclei_eclic_realize(DeviceState *dev, Error **errp)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);
    int id;

    memory_region_init_io(&eclic->mmio, OBJECT(dev), &nuclei_eclic_ops, eclic,
                          TYPE_NUCLEI_ECLIC, eclic->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &eclic->mmio);

    eclic->exccode = g_new0(uint32_t, eclic->num_harts);

    for (int i = 0; i < eclic->num_harts; i++)
    {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(i));

        QLIST_INIT(&eclic->pending_list[i]);
        for (id = 0; id < eclic->num_sources; id++)
        {
            eclic->clicintlist_s[i][id].irq = id;
            update_eclic_int_info_s(eclic, id, i);
            eclic->clicintlist[i][id].irq = id;
            update_eclic_int_info(eclic, id, i);
        }
        eclic->active_count = 0;
        eclic->active_count_s = 0;

        /* Init ECLIC IRQ */
        eclic->irqs[i][Internal_SysTimerSW_S_IRQn] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                                    eclic, Internal_SysTimerSW_S_IRQn);
        eclic->irqs[i][Internal_SysTimer_S_IRQn] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                                    eclic, Internal_SysTimer_S_IRQn);
        eclic->irqs[i][Internal_SysTimerSW_IRQn] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                                     eclic, Internal_SysTimerSW_IRQn);
        eclic->irqs[i][Internal_SysTimer_IRQn] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                                   eclic, Internal_SysTimer_IRQn);

        eclic->irqs[i][Internal_Reserved14_IRQn] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                                    eclic, Internal_Reserved14_IRQn);

        for (id = Internal_Reserved_Max_IRQn; id < eclic->num_sources; id++)
        {
            eclic->irqs[i][id] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                   eclic, id);
        }

        cpu->env.eclic = eclic;

    }
}

static void nuclei_eclic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nuclei_eclic_properties);
    dc->realize = nuclei_eclic_realize;
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

void nuclei_eclic_systimer_cb(DeviceState *dev)
{
    NucleiECLICState *eclic = NUCLEI_ECLIC(dev);
    nuclei_eclic_irq_request(eclic, Internal_SysTimer_IRQn, 1);
}

DeviceState *nuclei_eclic_create(hwaddr addr, uint32_t aperture_size, bool prv_s, bool prv_u, bool vector,
                                 uint32_t num_harts, uint32_t num_sources,
                                 uint8_t clicintctlbits)
{
    DeviceState *dev = qdev_new(TYPE_NUCLEI_ECLIC);

    assert(num_sources <= 4096);
    assert(num_harts <= 1024);
    assert(clicintctlbits <= 8);

    qdev_prop_set_bit(dev, "prv-s", prv_s);
    qdev_prop_set_bit(dev, "prv-u", prv_u);
    qdev_prop_set_bit(dev, "vector", vector);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint32(dev, "eclicintctlbits", clicintctlbits);
    qdev_prop_set_uint64(dev, "mclicbase", addr);
    qdev_prop_set_uint32(dev, "aperture-size", aperture_size);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
