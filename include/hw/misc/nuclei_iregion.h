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

#ifndef HW_NUCLEI_IREGION_H
#define HW_NUCLEI_IREGION_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_NUCLEI_IREGION "riscv.nuclei.iregion"

typedef struct NucleiIregionState NucleiIregionState;
DECLARE_INSTANCE_CHECKER(NucleiIregionState, NUCLEI_IREGION,
                         TYPE_NUCLEI_IREGION)

enum {
    IREGION_MPASIZE              = 0x0000,
    IREGION_CMO_INFO             = 0x0004,
    IREGION_SEC_BASE_ADDR_LO     = 0x0008,
    IREGION_SEC_BASE_ADDR_HI     = 0x000c,
    IREGION_SEC_CFG_INFO         = 0x0010,
    IREGION_MVLM_CFG_LO          = 0x0024,
    IREGION_MVLM_CFG_HI          = 0x0028,
    IREGION_FLASH_BASE_ADDR_LO   = 0x002c,
    IREGION_FLASH_BASE_ADDR_HI   = 0x0030,
    IREGION_VPU_CFG_INFO         = 0x0050,
    IREGION_MEM_REGION0_CFG_LO   = 0x0054,
    IREGION_MEM_REGION0_CFG_HI   = 0x0058,
    IREGION_MEM_REGION1_CFG_LO   = 0x005c,
    IREGION_MEM_REGION1_CFG_HI   = 0x0060,
    IREGION_BUS_INFO0            = 0x0064,
    IREGION_BUS_INFO1            = 0x0068,
    IREGION_ISA_SUPPORT0         = 0x0070,
    IREGION_ISA_SUPPORT1         = 0x0074,
    IREGION_MCPPI_CFG_LO         = 0x0080,
    IREGION_MCPPI_CFG_HI         = 0x0084,
    IREGION_MPFTCTL              = 0x0088,
    IREGION_PERFORMANCE_CFG0     = 0x0090,
    IREGION_PERFORMANCE_CFG1     = 0x0094,
    IREGION_PFL1DCTRL1           = 0x0100,
    IREGION_PFL1DCTRL2           = 0x0104,
    IREGION_MERGEL1DCTRL         = 0x0108,
    IREGION_SAFETY_CTRL          = 0x0110,
    IREGION_ACCESS_CTRL          = 0x0114,
    IREGION_PFL1DCTRL3           = 0x0120,
    IREGION_PFL1DCTRL4           = 0x0124,
    IREGION_PFL1INFO             = 0x0128,
    IREGION_CRC_RF0              = 0x0198,
    IREGION_CRC_RF1              = 0x019c,
    IREGION_CRC_FP0              = 0x01a0,
    IREGION_ETRACE_INFO          = 0x01a4,
    IREGION_ECC_INJ_ADDR_LO      = 0x01a8,
    IREGION_ECC_INJ_ADDR_HI      = 0x01ac,
    IREGION_ECC_INJ_WAY          = 0x01b0,
    IREGION_MEM_CRC_X22_LO       = 0x0300,
    IREGION_MEM_CRC_X22_HI       = 0x0304,
    IREGION_MEM_CRC_X23_LO       = 0x0308,
    IREGION_MEM_CRC_X23_HI       = 0x030c,
    IREGION_MEM_CRC_F23_LO       = 0x0310,
    IREGION_MEM_CRC_F23_HI       = 0x0314,
    IREGION_REG_COUNT            = IREGION_MEM_CRC_F23_HI / 4 + 1,
};

struct NucleiIregionState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    struct ArchCPU *cpu;
    bool big_endian;

    uint32_t regs[IREGION_REG_COUNT];
};

DeviceState *nuclei_iregion_create(hwaddr addr, struct ArchCPU *cpu,
                                   bool big_endian);

#endif
