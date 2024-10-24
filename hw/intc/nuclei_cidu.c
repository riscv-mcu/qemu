/*
 * NUCLEI CIDU(Cluster Interrupt Distribution Unit)
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
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "sysemu/qtest.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/intc/nuclei_cidu.h"
#include "qemu/main-loop.h"

static bool addr_in_range(uint32_t addr, uint32_t base, uint32_t num)
{
    return addr >= base && addr - base < num;
}

static uint64_t nuclei_cidu_read(void *opaque, hwaddr addr, unsigned size)
{
    NucLeiCIDUState *cidu = opaque;

    if (addr_in_range(addr, CIDU_REG_COREN_INT_STATUS_BASE, CIDU_MAX_SUPPORT_CORE_NUM << 2))
    {
        uint32_t core_id = (addr - CIDU_REG_COREN_INT_STATUS_BASE) >> 2;
        return cidu->coren_int_status[core_id];
    }
    else if (addr_in_range(addr, CIDU_REG_SEMAPHORE_BASE, CIDU_MAX_SEMAPHORE_NUM << 2))
    {
        uint32_t semaphore_id = (addr - CIDU_REG_SEMAPHORE_BASE) >> 2;
        return cidu->semaphore[semaphore_id];
    }
    else if (addr_in_range(addr, CIDU_REG_INTN_INDICATOR_BASE, CIDU_MAX_EXTERNAL_INT_NUM << 2))
    {
        uint32_t irq = (addr - CIDU_REG_INTN_INDICATOR_BASE) >> 2;
        return cidu->intn_indicator[irq];
    }
    else if (addr_in_range(addr, CIDU_REG_INTN_MASK_BASE, CIDU_MAX_EXTERNAL_INT_NUM << 2))
    {
        uint32_t irq = (addr - CIDU_REG_INTN_MASK_BASE) >> 2;
        return cidu->intn_mask[irq];
    }
    else if (addr == CIDU_REG_CORE_NUM)
    {
        return cidu->core_num;
    }
    else if (addr == CIDU_REG_INT_NUM)
    {
        return cidu->int_num;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register read 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

uint32_t coren_int_16 = 0;
uint32_t cidu_int_indicator = 0;

static void nuclei_cidu_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size)
{
    NucLeiCIDUState *cidu = opaque;
    uint32_t send_core = 0;
    uint32_t recv_core = 0;

    if (addr_in_range(addr, CIDU_REG_COREN_INT_STATUS_BASE, CIDU_MAX_SUPPORT_CORE_NUM << 2))
    {
        uint32_t core_id = (addr - CIDU_REG_COREN_INT_STATUS_BASE) >> 2;
        cidu->coren_int_status[core_id] &= ~((uint32_t)value);

        qemu_set_irq(cidu->soft_irq[core_id], 0);
    }
    else if (addr_in_range(addr, CIDU_REG_SEMAPHORE_BASE, CIDU_MAX_SEMAPHORE_NUM << 2))
    {
        uint32_t semaphore_id = (addr - CIDU_REG_SEMAPHORE_BASE) >> 2;
        cidu->semaphore[semaphore_id] = value;
    }
    else if (addr == CIDU_REG_ICI_SHADOW)
    {
        cidu->ici_shadow_reg = value;
        send_core = (value >> 16) & 0xffff;
        recv_core = value & 0xffff;
        coren_int_16 = recv_core;

        qemu_set_irq(cidu->soft_irq[recv_core], 1);

        cidu->coren_int_status[recv_core] = 1 << send_core;
    }
    else if (addr_in_range(addr, CIDU_REG_INTN_INDICATOR_BASE, CIDU_MAX_EXTERNAL_INT_NUM << 2))
    {
        uint32_t irq = (addr - CIDU_REG_INTN_INDICATOR_BASE) >> 2;
        cidu->intn_indicator[irq] = value;
        cidu_int_indicator = value;
    }
    else if (addr_in_range(addr, CIDU_REG_INTN_MASK_BASE, CIDU_MAX_EXTERNAL_INT_NUM << 2))
    {
        uint32_t irq = (addr - CIDU_REG_INTN_MASK_BASE) >> 2;
        if(((cidu->intn_mask[irq] & 0xffffffff) == 0xffffffff)
            || (value & 0xffffffff) == 0xffffffff)
        {
            cidu->intn_mask[irq] = value;
        }
    }
    else
    {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid register write 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
}


static const MemoryRegionOps nuclei_cidu_ops = {
    .read = nuclei_cidu_read,
    .write = nuclei_cidu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static Property nuclei_cidu_properties[] = {
    DEFINE_PROP_UINT32("num-harts", NucLeiCIDUState, num_harts, 0),
    DEFINE_PROP_UINT32("num-sources", NucLeiCIDUState, num_sources, 0),
    DEFINE_PROP_UINT64("mcidubase", NucLeiCIDUState, mcidubase, 0),
    DEFINE_PROP_UINT32("aperture-size", NucLeiCIDUState, aperture_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_cidu_realize(DeviceState *dev, Error **errp)
{
    NucLeiCIDUState *cidu = NUCLEI_CIDU(dev);

    memory_region_init_io(&cidu->mmio, OBJECT(dev), &nuclei_cidu_ops, cidu,
                          TYPE_NUCLEI_CIDU, cidu->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &cidu->mmio);
    for (int i = 0; i < 32; i++)
    {
        cidu->semaphore[i] = 0xffffffff;
    }
    for (int i = 0; i < 4096; i++)
    {
        cidu->intn_mask[i] = 0xffffffff;
    }

}

static void nuclei_cidu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nuclei_cidu_properties);
    dc->realize = nuclei_cidu_realize;
    dc->desc = "nuclei type: cidu";
}

static const TypeInfo nuclei_cidu_info = {
    .name = TYPE_NUCLEI_CIDU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucLeiCIDUState),
    .class_init = nuclei_cidu_class_init,
};

static void nuclei_cidu_register_types(void)
{
    type_register_static(&nuclei_cidu_info);
}

type_init(nuclei_cidu_register_types);

/*
 * Create Nuclei CIDU device.
 */
DeviceState *nuclei_cidu_create(hwaddr addr, uint32_t aperture_size,
                                uint32_t num_harts, uint32_t num_sources, DeviceState *eclic)
{
    DeviceState *dev = qdev_new(TYPE_NUCLEI_CIDU);

    assert(num_sources <= CIDU_MAX_EXTERNAL_INT_NUM);
    // assert(num_harts <= CIDU_MAX_SUPPORT_CORE_NUM);

    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint64(dev, "mcidubase", addr);
    qdev_prop_set_uint32(dev, "aperture-size", aperture_size);
    NucLeiCIDUState *s = NUCLEI_CIDU(dev);

    if(eclic != NULL)
    {
        for (int i = 0; i < num_harts; i++) {
            s->soft_irq[i] = NUCLEI_ECLIC(eclic)->irqs[Internal_Reserved14_IRQn][i];

            for(int j = 0; j < num_sources; j++) {
                s->external_irq[j] = NUCLEI_ECLIC(eclic)->irqs[j + CIDU_EXT_INT_OFST][i];
            }
        }
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    return dev;
}
