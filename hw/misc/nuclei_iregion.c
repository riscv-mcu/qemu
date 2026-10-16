/*
 * NUCLEI IREGION
 *
 * Copyright (c) 2023 Nuclei, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "hw/pci/msi.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "hw/misc/nuclei_iregion.h"
#include "qapi/error.h"
#include "target/riscv/cpu.h"

typedef struct {
    uint32_t reg;
    uint32_t bit;
    uint32_t ext_offset;
} IregionIsaSupport;

typedef struct {
    hwaddr offset;
    uint32_t reset;
    uint32_t writable_mask;
} IregionRegisterInfo;

static const IregionRegisterInfo iregion_registers[] = {
    { IREGION_MPASIZE,            0x00000000, 0x00000000 },
    { IREGION_CMO_INFO,           0x00000000, 0x00000000 },
    { IREGION_SEC_BASE_ADDR_LO,   0x00000000, 0x00000000 },
    { IREGION_SEC_BASE_ADDR_HI,   0x00000000, 0x00000000 },
    { IREGION_SEC_CFG_INFO,       0x00000000, 0x00000000 },
    { IREGION_MVLM_CFG_LO,        0x00000000, 0x00000000 },
    { IREGION_MVLM_CFG_HI,        0x00000000, 0x00000000 },
    { IREGION_FLASH_BASE_ADDR_LO, 0x00000000, 0x00000000 },
    { IREGION_FLASH_BASE_ADDR_HI, 0x00000000, 0x00000000 },
    { IREGION_VPU_CFG_INFO,       0x00000000, 0x00000000 },
    { IREGION_MEM_REGION0_CFG_LO, 0x00000200, 0x00000200 },
    { IREGION_MEM_REGION0_CFG_HI, 0x00000000, 0x00000000 },
    { IREGION_MEM_REGION1_CFG_LO, 0x00000200, 0x00000200 },
    { IREGION_MEM_REGION1_CFG_HI, 0x00000000, 0x00000000 },
    { IREGION_BUS_INFO0,          0x00000001, 0x00000000 },
    { IREGION_BUS_INFO1,          0x00000000, 0x00000000 },
    { IREGION_ISA_SUPPORT0,       0x00000001, 0x00000000 },
    { IREGION_ISA_SUPPORT1,       0x00000001, 0x00000000 },
    { IREGION_MCPPI_CFG_LO,       0x00000200, 0x00000200 },
    { IREGION_MCPPI_CFG_HI,       0x00000000, 0x00000000 },
    { IREGION_MPFTCTL,            0x00000000, 0x0000001f },
    { IREGION_PERFORMANCE_CFG0,   0x00000001, 0x00000000 },
    { IREGION_PERFORMANCE_CFG1,   0x00000001, 0x00000000 },
    { IREGION_PFL1DCTRL1,         0x000021bf, 0x00007fff },
    { IREGION_PFL1DCTRL2,         0x00000187, 0x000fffff },
    { IREGION_MERGEL1DCTRL,       0x000003ff, 0x01ff0fff },
    { IREGION_SAFETY_CTRL,        0x00000001, 0x00000003 },
    { IREGION_ACCESS_CTRL,        0x00000000, 0x0000000e },
    { IREGION_PFL1DCTRL3,         0x01030206, 0x0fef0fef },
    { IREGION_PFL1DCTRL4,         0x00000002, 0x00000003 },
    { IREGION_PFL1INFO,           0x00000000, 0x00000000 },
    { IREGION_CRC_RF0,            0x00000000, UINT32_MAX },
    { IREGION_CRC_RF1,            0x00000000, UINT32_MAX },
    { IREGION_CRC_FP0,            0x00000000, UINT32_MAX },
    { IREGION_ETRACE_INFO,        0x00000000, 0x00000000 },
    { IREGION_ECC_INJ_ADDR_LO,    0x00000000, UINT32_MAX },
    { IREGION_ECC_INJ_ADDR_HI,    0x00000000, UINT32_MAX },
    { IREGION_ECC_INJ_WAY,        0x00000000, 0x400000ff },
    { IREGION_MEM_CRC_X22_LO,     0x00000000, UINT32_MAX },
    { IREGION_MEM_CRC_X22_HI,     0x00000000, UINT32_MAX },
    { IREGION_MEM_CRC_X23_LO,     0x00000000, UINT32_MAX },
    { IREGION_MEM_CRC_X23_HI,     0x00000000, UINT32_MAX },
    { IREGION_MEM_CRC_F23_LO,     0x00000000, UINT32_MAX },
    { IREGION_MEM_CRC_F23_HI,     0x00000000, UINT32_MAX },
};

static const IregionIsaSupport iregion_isa_support[] = {
    /* ISA_SUPPORT0[29:4] */
    { IREGION_ISA_SUPPORT0, 29, CPU_CFG_OFFSET(ext_zawrs) },
    { IREGION_ISA_SUPPORT0, 28, CPU_CFG_OFFSET(ext_zicntr) },
    { IREGION_ISA_SUPPORT0, 27, CPU_CFG_OFFSET(ext_smcntrpmf) },
    { IREGION_ISA_SUPPORT0, 26, CPU_CFG_OFFSET(ext_zihpm) },
    { IREGION_ISA_SUPPORT0, 25, CPU_CFG_OFFSET(ext_smrnmi) },
    { IREGION_ISA_SUPPORT0, 24, CPU_CFG_OFFSET(ext_zvfhmin) },
    { IREGION_ISA_SUPPORT0, 23, CPU_CFG_OFFSET(ext_zvfh) },
    { IREGION_ISA_SUPPORT0, 22, CPU_CFG_OFFSET(ext_zihintpause) },
    { IREGION_ISA_SUPPORT0, 21, CPU_CFG_OFFSET(ext_zihintntl) },
    { IREGION_ISA_SUPPORT0, 20, CPU_CFG_OFFSET(ext_zicond) },
    { IREGION_ISA_SUPPORT0, 19, CPU_CFG_OFFSET(ext_zcmop) },
    { IREGION_ISA_SUPPORT0, 18, CPU_CFG_OFFSET(ext_zimop) },
    { IREGION_ISA_SUPPORT0, 17, CPU_CFG_OFFSET(ext_zve64d) },
    { IREGION_ISA_SUPPORT0, 16, CPU_CFG_OFFSET(ext_zve64f) },
    { IREGION_ISA_SUPPORT0, 15, CPU_CFG_OFFSET(ext_zve64x) },
    { IREGION_ISA_SUPPORT0, 14, CPU_CFG_OFFSET(ext_zve32f) },
    { IREGION_ISA_SUPPORT0, 13, CPU_CFG_OFFSET(ext_zve32x) },
    { IREGION_ISA_SUPPORT0, 12, CPU_CFG_OFFSET(ext_zfbfmin) },
    { IREGION_ISA_SUPPORT0, 11, CPU_CFG_OFFSET(ext_svinval) },
    { IREGION_ISA_SUPPORT0, 10, CPU_CFG_OFFSET(ext_svpbmt) },
    { IREGION_ISA_SUPPORT0, 9, CPU_CFG_OFFSET(ext_svnapot) },
    { IREGION_ISA_SUPPORT0, 8, CPU_CFG_OFFSET(ext_zfa) },
    { IREGION_ISA_SUPPORT0, 7, CPU_CFG_OFFSET(ext_zfhmin) },
    { IREGION_ISA_SUPPORT0, 6, CPU_CFG_OFFSET(ext_zfh) },
    { IREGION_ISA_SUPPORT0, 5, CPU_CFG_OFFSET(ext_sscofpmf) },
    { IREGION_ISA_SUPPORT0, 4, CPU_CFG_OFFSET(ext_smepmp) },
    /* ISA_SUPPORT0[3:0]: Vector-K, Vector-B, Vector, EXIST */

    /* ISA_SUPPORT1[24:0] */
    { IREGION_ISA_SUPPORT1, 24, CPU_CFG_OFFSET(ext_zbc) },
    /* ISA_SUPPORT1[23]: Zba_Zbb_Zbs */
    { IREGION_ISA_SUPPORT1, 22, CPU_CFG_OFFSET(ext_zhinx) },
    { IREGION_ISA_SUPPORT1, 21, CPU_CFG_OFFSET(ext_zdinx) },
    { IREGION_ISA_SUPPORT1, 20, CPU_CFG_OFFSET(ext_zfinx) },
    { IREGION_ISA_SUPPORT1, 19, CPU_CFG_OFFSET(ext_zibi) },
    /* ISA_SUPPORT1[18]: RVA23 is not represented by a QEMU CPU option. */
    { IREGION_ISA_SUPPORT1, 17, CPU_CFG_OFFSET(ext_svadu) },
    /* ISA_SUPPORT1[16:15]: Sscsrind and Smcsrind are not implemented. */
    /* ISA_SUPPORT1[14]: Sstateen is not implemented. */
    { IREGION_ISA_SUPPORT1, 13, CPU_CFG_OFFSET(ext_smstateen) },
    /* ISA_SUPPORT1[12:9]: Ssnpm, Smnpm, Smmpm and Smcdeleg are not implemented. */
    { IREGION_ISA_SUPPORT1, 8, CPU_CFG_OFFSET(ext_ssdbltrp) },
    { IREGION_ISA_SUPPORT1, 7, CPU_CFG_OFFSET(ext_smdbltrp) },
    /* ISA_SUPPORT1[6]: Zabha is not implemented. */
    { IREGION_ISA_SUPPORT1, 5, CPU_CFG_OFFSET(ext_zacas) },
    /* ISA_SUPPORT1[4]: Smctr is not implemented. */
    { IREGION_ISA_SUPPORT1, 3, CPU_CFG_OFFSET(ext_zicfiss) },
    { IREGION_ISA_SUPPORT1, 2, CPU_CFG_OFFSET(ext_zicfilp) },
    /* ISA_SUPPORT1[1]: Ssqosid is not implemented. */
    /* ISA_SUPPORT1[0]: Active is always set when this register exists. */
};

static uint32_t iregion_isa_support_value(RISCVCPU *cpu, uint32_t reg)
{
    CPURISCVState *env = &cpu->env;
    RISCVCPUConfig *cfg = &cpu->cfg;
    uint32_t value = 0;

    for (size_t i = 0; i < ARRAY_SIZE(iregion_isa_support); i++) {
        const IregionIsaSupport *entry = &iregion_isa_support[i];

        if (entry->reg == reg && isa_ext_is_enabled(cpu, entry->ext_offset)) {
            value |= 1U << entry->bit;
        }
    }

    if (reg == IREGION_ISA_SUPPORT0) {
        value |= (env->misa_ext_mask & RVV) ? (1U << 1) : 0;
        value |= (cfg->ext_zvbb && cfg->ext_zvbc) ? (1U << 2) : 0;
        value |= (cfg->ext_zvkb && cfg->ext_zvkg && cfg->ext_zvkned &&
                  cfg->ext_zvknha && cfg->ext_zvknhb && cfg->ext_zvksed &&
                  cfg->ext_zvksh && cfg->ext_zvkt) ? (1U << 3) : 0;
        value |= 1U;
    } else if (reg == IREGION_ISA_SUPPORT1) {
        value |= 1U;
        if (cfg->ext_zba && cfg->ext_zbb && cfg->ext_zbs) {
            value |= 1U << 23;
        }
    }

    return value;
}

static const IregionRegisterInfo *iregion_register_info(hwaddr offset)
{
    for (size_t i = 0; i < ARRAY_SIZE(iregion_registers); i++) {
        if (iregion_registers[i].offset == offset) {
            return &iregion_registers[i];
        }
    }

    return NULL;
}

static uint32_t nuclei_iregion_read_reg(NucleiIregionState *s, hwaddr addr)
{
    if (!iregion_register_info(addr)) {
        return 0;
    }

    return s->regs[addr / 4];
}

static uint64_t nuclei_iregion_read(void *opaque, hwaddr addr, unsigned int size)
{
    NucleiIregionState *s = opaque;
    uint32_t value = nuclei_iregion_read_reg(s, addr & ~3ULL);
    unsigned int offset = addr & 3;

    if (s->big_endian) {
        offset = 4 - size - offset;
    }
    return extract32(value, offset * 8, size * 8);
}

static void nuclei_iregion_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    NucleiIregionState *s = opaque;
    hwaddr reg_addr = addr & ~3ULL;
    const IregionRegisterInfo *info = iregion_register_info(reg_addr);
    uint32_t old_value;
    uint32_t new_value;
    unsigned int offset = addr & 3;

    if (!info || !info->writable_mask) {
        return;
    }

    if (s->big_endian) {
        offset = 4 - size - offset;
    }

    old_value = s->regs[reg_addr / 4];
    new_value = deposit32(old_value, offset * 8, size * 8,
                          (uint32_t)val64);
    s->regs[reg_addr / 4] = (old_value & ~info->writable_mask) |
                           (new_value & info->writable_mask);
}

static void nuclei_iregion_reset(DeviceState *dev)
{
    NucleiIregionState *s = NUCLEI_IREGION(dev);

    memset(s->regs, 0, sizeof(s->regs));
    for (size_t i = 0; i < ARRAY_SIZE(iregion_registers); i++) {
        const IregionRegisterInfo *info = &iregion_registers[i];

        s->regs[info->offset / 4] = info->reset;
    }

    s->regs[IREGION_MPASIZE / 4] = riscv_cpu_is_32bit(s->cpu) ? 32 : 40;
    s->regs[IREGION_ISA_SUPPORT0 / 4] =
        iregion_isa_support_value(s->cpu, IREGION_ISA_SUPPORT0);
    s->regs[IREGION_ISA_SUPPORT1 / 4] =
        iregion_isa_support_value(s->cpu, IREGION_ISA_SUPPORT1);
}

static const MemoryRegionOps nuclei_iregion_ops[3] = {
    [DEVICE_NATIVE_ENDIAN] = {
        .read = nuclei_iregion_read,
        .write = nuclei_iregion_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
        .valid = {
            .min_access_size = 1,
            .max_access_size = 8,
        },
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
    },
    [DEVICE_BIG_ENDIAN] = {
        .read = nuclei_iregion_read,
        .write = nuclei_iregion_write,
        .endianness = DEVICE_BIG_ENDIAN,
        .valid = {
            .min_access_size = 1,
            .max_access_size = 8,
        },
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
    },
    [DEVICE_LITTLE_ENDIAN] = {
        .read = nuclei_iregion_read,
        .write = nuclei_iregion_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
        .valid = {
            .min_access_size = 1,
            .max_access_size = 8,
        },
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
    },
};

static void nuclei_iregion_realize(DeviceState *dev, Error **errp)
{
    NucleiIregionState *s = NUCLEI_IREGION(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev),
                          &nuclei_iregion_ops[s->big_endian ?
                                               DEVICE_BIG_ENDIAN :
                                               DEVICE_LITTLE_ENDIAN],
                          s, TYPE_NUCLEI_IREGION, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static Property nuclei_iregion_properties[] = {
    DEFINE_PROP_LINK("cpu", NucleiIregionState, cpu, TYPE_RISCV_CPU,
                     struct ArchCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_iregion_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nuclei_iregion_properties);
    dc->realize = nuclei_iregion_realize;
    dc->reset = nuclei_iregion_reset;
}

static const TypeInfo nuclei_iregion_info = {
    .name          = TYPE_NUCLEI_IREGION,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiIregionState),
    .class_init    = nuclei_iregion_class_init,
};

static void nuclei_iregion_register_types(void)
{
    type_register_static(&nuclei_iregion_info);
}

type_init(nuclei_iregion_register_types)


DeviceState *nuclei_iregion_create(hwaddr addr, struct ArchCPU *cpu,
                                   bool big_endian)
{
    DeviceState *dev = qdev_new(TYPE_NUCLEI_IREGION);
    NucleiIregionState *s = NUCLEI_IREGION(dev);

    s->big_endian = big_endian;
    object_property_set_link(OBJECT(dev), "cpu", OBJECT(cpu),
                             &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
