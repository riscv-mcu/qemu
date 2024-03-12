/*
 * NUCLEI SMPCC (SMP and Cluster Cache)
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
#include "sysemu/qtest.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/smpcc/nuclei_smpcc.h"
#include "hw/loader.h"


static uint64_t nuclei_smpcc_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void nuclei_smpcc_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size)
{

}

static void nuclei_smpcc_reset(DeviceState *dev)
{

}

static const MemoryRegionOps nuclei_smpcc_ops = {
    .read = nuclei_smpcc_read,
    .write = nuclei_smpcc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8
    }
};

static Property nuclei_smpcc_properties[] = {
    DEFINE_PROP_UINT64("msmpccbase", NucLeiSMPCCState, msmpccbase, 0),
    DEFINE_PROP_UINT32("aperture-size", NucLeiSMPCCState, aperture_size, 0),
    DEFINE_PROP_UINT32("smp-ver", NucLeiSMPCCState, smp_ver, 0),
    DEFINE_PROP_UINT32("smp-cfg", NucLeiSMPCCState, smp_cfg, 0),
    DEFINE_PROP_UINT32("cc-cfg", NucLeiSMPCCState, cc_cfg, 0),
    DEFINE_PROP_UINT64("clm-addr-base", NucLeiSMPCCState, clm_addr_base, 0),
    DEFINE_PROP_UINT32("cc-size", NucLeiSMPCCState, cc_size, 0),
    DEFINE_PROP_UINT32("clm-way-en", NucLeiSMPCCState, clm_way_en, 0),

    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_smpcc_realize(DeviceState *dev, Error **errp)
{
    NucLeiSMPCCState *smpcc = NUCLEI_SMPCC(dev);

    memory_region_init_io(&smpcc->mmio, OBJECT(dev), &nuclei_smpcc_ops, smpcc,
                          TYPE_NUCLEI_SMPCC, smpcc->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &smpcc->mmio);
}

static void nuclei_smpcc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nuclei_smpcc_properties);
    dc->realize = nuclei_smpcc_realize;
    dc->reset = nuclei_smpcc_reset;
    dc->desc = "nuclei type: smpcc";
}

static const TypeInfo nuclei_smpcc_info = {
    .name = TYPE_NUCLEI_SMPCC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucLeiSMPCCState),
    .class_init = nuclei_smpcc_class_init,
};

static void nuclei_smpcc_register_types(void)
{
    type_register_static(&nuclei_smpcc_info);
}

type_init(nuclei_smpcc_register_types);

/*
 * Create Nuclei smpcc device.
 */
DeviceState *nuclei_smpcc_create(hwaddr addr, uint32_t aperture_size,
                                NucLeiSMPCCInit *smpcc)
{
    DeviceState *dev = qdev_new(TYPE_NUCLEI_SMPCC);

    qdev_prop_set_uint64(dev, "msmpccbase", addr);
    qdev_prop_set_uint32(dev, "aperture-size", aperture_size);
    qdev_prop_set_uint32(dev, "smp-ver", smpcc->smp_ver);
    qdev_prop_set_uint32(dev, "smp-cfg", smpcc->smp_cfg);
    qdev_prop_set_uint32(dev, "cc-cfg", smpcc->cc_cfg);
    qdev_prop_set_uint64(dev, "clm-addr-base", smpcc->clm_addr_base);
    qdev_prop_set_uint32(dev, "cc-size", smpcc->cc_size);
    qdev_prop_set_uint32(dev, "clm-way-en", smpcc->clm_way_en);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    return dev;
}

