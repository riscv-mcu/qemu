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

static uint32_t nuclei_cidu_hart_mask(NucleiCIDUState *cidu)
{
    if (cidu->num_harts >= 32) {
        return UINT32_MAX;
    }

    return (1U << cidu->num_harts) - 1;
}

static void nuclei_cidu_update_external_source(NucleiCIDUState *cidu, uint32_t irq)
{
    uint32_t desired_mask;
    uint32_t changed_mask;
    uint32_t hartid;

    if (!cidu->eclic || irq >= cidu->num_sources) {
        return;
    }

    desired_mask = cidu->ext_level[irq] ?
                   (cidu->intn_indicator[irq] & nuclei_cidu_hart_mask(cidu)) : 0;
    changed_mask = cidu->delivered_mask[irq] ^ desired_mask;

    if (!changed_mask) {
        return;
    }

    for (hartid = 0; hartid < cidu->num_harts; hartid++) {
        uint32_t bit = 1U << hartid;

        if (changed_mask & bit) {
            qemu_set_irq(nuclei_eclic_get_irq(cidu->eclic,
                                              irq + CIDU_EXT_INT_OFST,
                                              hartid),
                         (desired_mask & bit) ? 1 : 0);
        }
    }

    cidu->delivered_mask[irq] = desired_mask;
}

static uint64_t nuclei_cidu_read(void *opaque, hwaddr addr, unsigned size)
{
    NucleiCIDUState *cidu = opaque;

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

static void nuclei_cidu_external_irq_handler(void *opaque, int irq, int level)
{
    NucleiCIDUState *cidu = opaque;

    if (!cidu->eclic || irq < 0 || irq >= cidu->num_sources) {
        return;
    }

    /*
     * External interrupt routing is level-sensitive. Keep the source level in
     * CIDU so that INTn_INDICATOR / INTn_MASK updates can immediately
     * re-distribute or withdraw a live interrupt without waiting for another
     * device edge.
     */
    cidu->ext_level[irq] = !!level;
    nuclei_cidu_update_external_source(cidu, irq);
}

static void nuclei_cidu_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size)
{
    NucleiCIDUState *cidu = opaque;
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

        qemu_set_irq(cidu->soft_irq[recv_core], 1);

        cidu->coren_int_status[recv_core] = 1 << send_core;
    }
    else if (addr_in_range(addr, CIDU_REG_INTN_INDICATOR_BASE, CIDU_MAX_EXTERNAL_INT_NUM << 2))
    {
        uint32_t irq = (addr - CIDU_REG_INTN_INDICATOR_BASE) >> 2;
        cidu->intn_indicator[irq] = (uint32_t)value & nuclei_cidu_hart_mask(cidu);
        nuclei_cidu_update_external_source(cidu, irq);
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
    DEFINE_PROP_UINT32("num-harts", NucleiCIDUState, num_harts, 0),
    DEFINE_PROP_UINT32("num-sources", NucleiCIDUState, num_sources, 0),
    DEFINE_PROP_UINT64("mcidubase", NucleiCIDUState, mcidubase, 0),
    DEFINE_PROP_UINT32("aperture-size", NucleiCIDUState, aperture_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_cidu_realize(DeviceState *dev, Error **errp)
{
    NucleiCIDUState *cidu = NUCLEI_CIDU(dev);
    uint32_t irq;
    int i;

    if (cidu->num_harts > CIDU_MAX_SUPPORT_CORE_NUM) {
        error_setg(errp, "%s supports at most %u harts", TYPE_NUCLEI_CIDU,
                   CIDU_MAX_SUPPORT_CORE_NUM);
        return;
    }

    memory_region_init_io(&cidu->mmio, OBJECT(dev), &nuclei_cidu_ops, cidu,
                          TYPE_NUCLEI_CIDU, cidu->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &cidu->mmio);

    /*
     * CIDU receives one GPIO input per cluster-level external interrupt
     * source, and redistributes it to one or more per-hart ECLIC instances
     * according to INTn_INDICATOR.
     */
    qdev_init_gpio_in(dev, nuclei_cidu_external_irq_handler, cidu->num_sources);

    cidu->core_num = cidu->num_harts;
    cidu->int_num = cidu->num_sources;

    for (i = 0; i < CIDU_MAX_SEMAPHORE_NUM; i++) {
        cidu->semaphore[i] = 0xffffffff;
    }

    for (irq = 0; irq < cidu->num_sources; irq++) {
        cidu->intn_indicator[irq] = 0x1;
        cidu->intn_mask[irq] = 0xffffffff;
        cidu->delivered_mask[irq] = 0;
        cidu->ext_level[irq] = 0;
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
    .instance_size = sizeof(NucleiCIDUState),
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
    NucleiCIDUState *s = NUCLEI_CIDU(dev);
    int i;

    assert(num_sources <= CIDU_MAX_EXTERNAL_INT_NUM);
    assert(num_harts <= CIDU_MAX_SUPPORT_CORE_NUM);

    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint64(dev, "mcidubase", addr);
    qdev_prop_set_uint32(dev, "aperture-size", aperture_size);
    s->eclic = eclic;

    if (eclic != NULL) {
        for (i = 0; i < num_harts; i++) {
            s->soft_irq[i] = nuclei_eclic_get_irq(eclic, Internal_Reserved14_IRQn, i);
        }
    }

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    return dev;
}
