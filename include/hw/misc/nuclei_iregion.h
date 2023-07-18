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

struct NucleiIregionState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    uint32_t mpasize;
    uint32_t cmo_info;
    uint32_t sec_base_addr_lo;
    uint32_t sec_base_addr_hi;
    uint32_t sec_cfg_info;
    uint32_t mcppi_cfg_lo;
    uint32_t mcppi_cfg_hi;
    uint32_t spfl1dctrl1;
    uint32_t spfl1dctrl2;
    uint32_t mergel1dctrl;
};

#define  IREGION_MPASIZE            0x0000
#define  IREGION_CMO_INFO           0x0004
#define  IREGION_SEC_BASE_ADDR_LO   0x0008
#define  IREGION_SEC_BASE_ADDR_HI   0x000c
#define  IREGION_SEC_CFG_INFO       0x0010
#define  IREGION_MCPPI_CFG_LO       0x0080
#define  IREGION_MCPPI_CFG_HI       0x0084
#define  IREGION_SPFL1DCTRL1        0x0100
#define  IREGION_SPFL1DCTRL2        0x0104
#define  IREGION_MERGEL1DCTRL       0x0108

DeviceState *nuclei_iregion_create(hwaddr addr, bool is_32_bit);

#endif
