/*
 * NUCLEI CIDU(Cluster Interrupt Distribution Unit) interface. 
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

#ifndef HW_NUCLEI_CIDU_H
#define HW_NUCLEI_CIDU_H

#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/nuclei_eclic.h"

#define TYPE_NUCLEI_CIDU "riscv.nuclei.cidu"
typedef struct NucleiCIDUState NucleiCIDUState;
DECLARE_INSTANCE_CHECKER(NucleiCIDUState, NUCLEI_CIDU,
                        TYPE_NUCLEI_CIDU)

#define CIDU_MAX_SUPPORT_CORE_NUM         (32)
#define CIDU_MAX_EXTERNAL_INT_NUM         (4096)
#define CIDU_MAX_INTER_COER_INT_NUM       (16)
#define CIDU_MAX_SEMAPHORE_NUM            (32)


#define CIDU_REG_COREN_INT_STATUS_BASE   0x0
#define CIDU_REG_SEMAPHORE_BASE          0x80
#define CIDU_REG_ICI_SHADOW              0x3ffc
#define CIDU_REG_INTN_INDICATOR_BASE     0x4000
#define CIDU_REG_INTN_MASK_BASE          0x8000
#define CIDU_REG_CORE_NUM                0xc084
#define CIDU_REG_INT_NUM                 0xc090

#define CIDU_EXT_INT_OFST                (19)

typedef struct NucleiCIDUState
{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    qemu_irq soft_irq[32];
    qemu_irq external_irq[4096];

    DeviceState *eclic;

    uint32_t num_harts;
    uint32_t num_sources;
    uint64_t mcidubase;
    uint32_t aperture_size;

    uint32_t coren_int_status[32];
    uint32_t intn_indicator[4096];
    uint32_t intn_mask[4096];
    uint32_t semaphore[32];
    uint32_t ici_shadow_reg;
    uint32_t core_num;
    uint32_t int_num;

} NucleiCIDUState;

DeviceState *nuclei_cidu_create(hwaddr addr, uint32_t aperture_size,
                                uint32_t num_harts, uint32_t num_sources, DeviceState *eclic);


#endif
