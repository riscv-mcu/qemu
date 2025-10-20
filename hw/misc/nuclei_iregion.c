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

bool cpu_is_32_bit;
static uint64_t nuclei_iregion_read(void *opaque, hwaddr addr, unsigned int size)
{
    int ret = 0;
    switch (addr)
    {
        case IREGION_MPASIZE:
            if(cpu_is_32_bit)
            {
                ret = 32;
            }
            else
            {
                ret = 40;
            }
            break;
        case IREGION_CMO_INFO:
            break;
        case IREGION_SEC_BASE_ADDR_LO:
            break;
        case IREGION_SEC_BASE_ADDR_HI:
            break;
        case IREGION_SEC_CFG_INFO:
            break;
        case IREGION_MCPPI_CFG_LO:
            break;
        case IREGION_MCPPI_CFG_HI:
            break;
        case IREGION_SPFL1DCTRL1:
            break;
        case IREGION_SPFL1DCTRL2:
            break;
        case IREGION_MERGEL1DCTRL:
            break;
    }
    return ret;
}

static void nuclei_iregion_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{

}

static const MemoryRegionOps nuclei_iregion_ops = {
    .read = nuclei_iregion_read,
    .write = nuclei_iregion_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8
    }
};

static void nuclei_iregion_init(Object *obj)
{
    NucleiIregionState *s = NUCLEI_IREGION(obj);

    memory_region_init_io(&s->mmio, obj, &nuclei_iregion_ops, s,
                          TYPE_NUCLEI_IREGION, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const TypeInfo nuclei_iregion_info = {
    .name          = TYPE_NUCLEI_IREGION,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiIregionState),
    .instance_init = nuclei_iregion_init,
};

static void nuclei_iregion_register_types(void)
{
    type_register_static(&nuclei_iregion_info);
}

type_init(nuclei_iregion_register_types)


DeviceState *nuclei_iregion_create(hwaddr addr, bool is_32_bit)
{
    cpu_is_32_bit = is_32_bit;
    DeviceState *dev = qdev_new(TYPE_NUCLEI_IREGION);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
