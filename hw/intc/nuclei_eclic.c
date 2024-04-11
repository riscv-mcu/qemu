/*
 * NUCLEI ECLIC(Enhanced Core Local Interrupt Controller)
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

static void nuclei_eclic_update_intmth(NucLeiECLICState *eclic, int irq, int hartid, int mth);
static void nuclei_eclic_update_intip(NucLeiECLICState *eclic, int irq, int hartid, int new_intip);
static void nuclei_eclic_update_intie(NucLeiECLICState *eclic, int irq, int hartid, int new_intie);
static void nuclei_eclic_update_intattr(NucLeiECLICState *eclic, int irq, int hartid, int new_intattr);
static void nuclei_eclic_update_intctl(NucLeiECLICState *eclic, int irq, int hartid, int new_intctl);
static void eclic_insert_pending_list(NucLeiECLICState *eclic, int irq, int hartid);
static void eclic_remove_pending_list(NucLeiECLICState *eclic, int irq, int hartid);
static void update_eclic_int_info(NucLeiECLICState *eclic, int irq, int hartid);
qemu_irq nuclei_eclic_get_irq(DeviceState *dev, int irq, int hartid)
{
    NucLeiECLICState *eclic = NUCLEI_ECLIC(dev);
    return eclic->irqs[irq][hartid];
}

static inline int nuclei_eclic_get_current_cpu(NucLeiECLICState *eclic)
{
    if (eclic->num_harts > 1)
    {
        return current_cpu ? current_cpu->cpu_index : 0;
    }
    return 0;
}

static uint64_t nuclei_eclic_read(void *opaque, hwaddr offset, unsigned size)
{
    NucLeiECLICState *eclic = NUCLEI_ECLIC(opaque);
    uint64_t value = 0;
    uint32_t irq = 0;
    uint32_t hartid = nuclei_eclic_get_current_cpu(eclic);

    if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE)
    {
        if ((offset - 0x1000) % 4 == 0)
        {
            irq = (offset - 0x1000) / 4;
        }
        else if ((offset - 0x1001) % 4 == 0)
        {
            irq = (offset - 0x1001) / 4;
        }
        else if ((offset - 0x1002) % 4 == 0)
        {
            irq = (offset - 0x1002) / 4;
        }
        else if ((offset - 0x1003) % 4 == 0)
        {
            irq = (offset - 0x1003) / 4;
        }
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
    case NUCLEI_ECLIC_REG_MTH:
        value = eclic->mth[hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE:
        value = eclic->clicintip[irq][hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE:
        value = eclic->clicintie[irq][hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE:
        value = eclic->clicintattr[irq][hartid] & 0xFF;
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE:
        value = eclic->clicintctl[irq][hartid] & 0xFF;
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
    NucLeiECLICState *eclic = NUCLEI_ECLIC(opaque);
    uint32_t irq = 0;
    uint32_t hartid = nuclei_eclic_get_current_cpu(eclic);

    if (offset >= NUCLEI_ECLIC_REG_CLICINTIP_BASE)
    {
        if ((offset - 0x1000) % 4 == 0)
        {
            irq = (offset - 0x1000) / 4;
        }
        else if ((offset - 0x1001) % 4 == 0)
        {
            irq = (offset - 0x1001) / 4;
        }
        else if ((offset - 0x1002) % 4 == 0)
        {
            irq = (offset - 0x1002) / 4;
        }
        else if ((offset - 0x1003) % 4 == 0)
        {
            irq = (offset - 0x1003) / 4;
        }
        //返回寄存器列表
        offset = offset - 4 * irq;
    }
    switch (offset)
    {
    case NUCLEI_ECLIC_REG_CLICCFG:
        eclic->cliccfg[hartid] = value & 0xFF;
        for (irq = 0; irq < eclic->num_sources; irq++)
        {
            update_eclic_int_info(eclic, irq, hartid);
        }
        break;
    case NUCLEI_ECLIC_REG_MTH:
        nuclei_eclic_update_intmth(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIP_BASE:
        if ((eclic->clicintlist[irq][hartid].trigger & 0x1) != 0)
        {
            if ((eclic->clicintip[irq][hartid] == 0) && (value & 0x1) == 1)
            {
                eclic->clicintip[irq][hartid] = 1;
                eclic_insert_pending_list(eclic, irq, hartid);
            }
            else if ((eclic->clicintip[irq][hartid] == 1) && (value & 0x1) == 0)
            {
                eclic->clicintip[irq][hartid] = 0;
                eclic_remove_pending_list(eclic, irq, hartid);
            }
        }
        nuclei_eclic_next_interrupt(eclic, hartid);
        break;
    case NUCLEI_ECLIC_REG_CLICINTIE_BASE:
        nuclei_eclic_update_intie(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTATTR_BASE:
        nuclei_eclic_update_intattr(eclic, irq, hartid, value & 0xFF);
        break;
    case NUCLEI_ECLIC_REG_CLICINTCTL_BASE:
        nuclei_eclic_update_intctl(eclic, irq, hartid, value & 0xFF);
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
    DEFINE_PROP_BOOL("prv-s", NucLeiECLICState, prv_s, false),
    DEFINE_PROP_BOOL("prv-u", NucLeiECLICState, prv_u, false),
    DEFINE_PROP_BOOL("vector", NucLeiECLICState, nvbits, false),
    DEFINE_PROP_UINT32("num-harts", NucLeiECLICState, num_harts, 0),
    DEFINE_PROP_UINT32("eclicintctlbits", NucLeiECLICState, eclicintctlbits, 0),
    DEFINE_PROP_UINT32("aperture-size", NucLeiECLICState, aperture_size, 0),
    DEFINE_PROP_UINT32("num-sources", NucLeiECLICState, num_sources, 0),
    DEFINE_PROP_UINT64("mclicbase", NucLeiECLICState, mclicbase, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void update_eclic_int_info(NucLeiECLICState *eclic, int irq, int hartid)
{
    int level_width = (eclic->cliccfg[hartid] >> 1) & 0xF; // cliccfg.nlbits
    if (level_width > CLICINTCTLBITS)
        level_width = CLICINTCTLBITS;
    int prio_width = CLICINTCTLBITS - level_width;

    if (level_width == 0)
        eclic->clicintlist[irq][hartid].level = 255;
    else
        eclic->clicintlist[irq][hartid].level = (((eclic->clicintctl[irq][hartid] >> (8 - level_width)) &
                                          ~((char)0x80 >> (8 - level_width)))
                                         << (8 - level_width)) |
                                        (0xff >> level_width);

    // TODO: implement priority decode logic when width > CLICINTCTLBITS or zeros
    if (prio_width == 0)
        eclic->clicintlist[irq][hartid].prio = 0;
    else
        eclic->clicintlist[irq][hartid].prio = (eclic->clicintctl[irq][hartid] >> (8 - level_width)) &
                                       ~(0x80 >> (8 - prio_width));

    eclic->clicintlist[irq][hartid].enable = eclic->clicintie[irq][hartid] & 0x1;
    // 0, level triggered; 2, rising edge; 3, falling edge
    eclic->clicintlist[irq][hartid].trigger = (eclic->clicintattr[irq][hartid] >> 1) & 0x3;
}

void nuclei_eclic_next_interrupt(void *eclic_ptr, int hartid)
{
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(hartid));
    NucLeiECLICState *eclic = (NucLeiECLICState *)eclic_ptr;
    ECLICPendingInterrupt *active;
    int shv;
    int mode = PRV_M;

    QLIST_FOREACH(active, &eclic->pending_list[hartid], next)
    {
        if (active->enable)
        {
            if (active->level >= eclic->mth[hartid])
            {                  
                eclic->exccode[0] = active->irq | mode << 12 | active->level << 14; 
                shv = eclic->clicintattr[active->irq][hartid] & 0x1;
                eclic->active_count++;
                riscv_cpu_eclic_interrupt(cpu, (active->irq & 0xFFF) | (shv << 12) | (active->level << 13));
                return;
            }
        }
    }
    riscv_cpu_eclic_interrupt(cpu, -1);
}

void riscv_cpu_eclic_int_handler_start(void *eclic_ptr, int irq, int hartid)
{
    NucLeiECLICState *eclic = (NucLeiECLICState *)eclic_ptr;
    if ((eclic->clicintlist[irq][hartid].trigger & 0x1) != 0)
    {
        eclic->clicintip[irq][hartid] = 0;
        eclic_remove_pending_list(eclic, irq, hartid);
    }
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static int level_compare(NucLeiECLICState *eclic, ECLICPendingInterrupt *irq1, ECLICPendingInterrupt *irq2)
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

static void nuclei_eclic_irq_request(void *opaque, int id, int new_intip)
{
    NucLeiECLICState *eclic = NUCLEI_ECLIC(opaque);

    for(int i = 0; i < eclic->num_harts; i++)
    {
        nuclei_eclic_update_intip(eclic, id, i, new_intip);
    }
}

static void nuclei_eclic_update_intmth(NucLeiECLICState *eclic, int irq, int hartid, int mth)
{
    eclic->mth[hartid] = mth;
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void eclic_insert_pending_list(NucLeiECLICState *eclic, int irq, int hartid)
{
    ECLICPendingInterrupt *node;
    if (QLIST_EMPTY(&eclic->pending_list[hartid]))
    {
        QLIST_INSERT_HEAD(&eclic->pending_list[hartid], &eclic->clicintlist[irq][hartid], next);
    }
    else
    {
        QLIST_FOREACH(node, &eclic->pending_list[hartid], next)
        {
            if (level_compare(eclic, node, &eclic->clicintlist[irq][hartid]))
            {
                QLIST_INSERT_BEFORE(node, &eclic->clicintlist[irq][hartid], next);
                break;
            }
            else if (node->next.le_next == NULL)
            {
                QLIST_INSERT_AFTER(node, &eclic->clicintlist[irq][hartid], next);
                break;
            }
        }
    }
}

static void eclic_remove_pending_list(NucLeiECLICState *eclic, int irq, int hartid)
{
    QLIST_REMOVE(&eclic->clicintlist[irq][hartid], next);
}

static void nuclei_eclic_update_intip(NucLeiECLICState *eclic, int irq, int hartid, int new_intip)
{

    int old_intip = eclic->clicintlist[irq][hartid].sig;
    int trigger = (eclic->clicintattr[irq][hartid] >> 1) & 0x3;
    if((old_intip == new_intip) &&  (new_intip != 0))
    {

    }
    else
    {
        if (((trigger == 0) && new_intip) ||
            ((trigger == 1) && !old_intip && new_intip) ||
            ((trigger == 3) && old_intip && !new_intip))
        {
            eclic->clicintip[irq][hartid] = 1;
            eclic->clicintlist[irq][hartid].sig = new_intip;
            eclic_insert_pending_list(eclic, irq, hartid);
        }
        else
        {
            if (eclic->clicintip[irq][hartid])
                eclic_remove_pending_list(eclic, irq, hartid);
            eclic->clicintip[irq][hartid] = 0;
            eclic->clicintlist[irq][hartid].sig = new_intip;
        }

    }

    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void nuclei_eclic_update_intie(NucLeiECLICState *eclic, int irq, int hartid, int new_intie)
{
    eclic->clicintie[irq][hartid] = new_intie;
    update_eclic_int_info(eclic, irq, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

// TODO: intattr not supposed to be changed during runtime?
static void nuclei_eclic_update_intattr(NucLeiECLICState *eclic, int irq, int hartid, int new_intattr)
{
    eclic->clicintattr[irq][hartid] = new_intattr;
    update_eclic_int_info(eclic, irq, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

// TODO: intctl not supposed to be changed during runtime?
static void nuclei_eclic_update_intctl(NucLeiECLICState *eclic, int irq, int hartid, int new_intctl)
{
    eclic->clicintctl[irq][hartid] = new_intctl;
    update_eclic_int_info(eclic, irq, hartid);
    nuclei_eclic_next_interrupt(eclic, hartid);
}

static void nuclei_eclic_realize(DeviceState *dev, Error **errp)
{
    NucLeiECLICState *eclic = NUCLEI_ECLIC(dev);
    int id;

    memory_region_init_io(&eclic->mmio, OBJECT(dev), &nuclei_eclic_ops, eclic,
                          TYPE_NUCLEI_ECLIC, eclic->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &eclic->mmio);

    eclic->exccode = g_new0(uint32_t, eclic->num_harts);

    for (int i = 0; i < eclic->num_harts; i++)
    {

        QLIST_INIT(&eclic->pending_list[i]);
        for (id = 0; id < eclic->num_sources; id++)
        {
            eclic->clicintlist[id][i].irq = id;
            update_eclic_int_info(eclic, id, i);
        }
        eclic->active_count = 0;

        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(i));

        /* Init ECLIC IRQ */
        eclic->irqs[Internal_SysTimerSW_IRQn][i] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                                     eclic, Internal_SysTimerSW_IRQn);
        eclic->irqs[Internal_SysTimer_IRQn][i] = qemu_allocate_irq(nuclei_eclic_irq_request,
                                                                   eclic, Internal_SysTimer_IRQn);

        for (id = Internal_Reserved_Max_IRQn; id < eclic->num_sources; id++)
        {
            eclic->irqs[id][i] = qemu_allocate_irq(nuclei_eclic_irq_request,
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
    .instance_size = sizeof(NucLeiECLICState),
    .class_init = nuclei_eclic_class_init,
};

static void nuclei_eclic_register_types(void)
{
    type_register_static(&nuclei_eclic_info);
}

type_init(nuclei_eclic_register_types);

void nuclei_eclic_systimer_cb(DeviceState *dev)
{
    NucLeiECLICState *eclic = NUCLEI_ECLIC(dev);
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


/*
6'b000011:clic
else:     clint
 */
bool riscv_intc_is_clic_mode(CPURISCVState *env)
{
    target_ulong xtvec = (env->priv == PRV_M) ? env->mtvec : env->stvec;
    return env->eclic && ((xtvec & 0x3F) == 3);
}
