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

static uint64_t nuclei_smpcc_update_cc_ctrl(NucLeiSMPCCState *smpcc, uint64_t value)
{
    /*
     * Check LOCK_ECC_CFG bit.
     * If this bit is set to 1, then CC_ECC_EN & ECC_EXCP_EN and this bit can’t
     * be changed by software anymore, only reset can change to reset value 0.
     */
    if (smpcc->cc_ctrl & 0x8)
    {
        value = (value & (~(uint64_t)0xe)) | (smpcc->cc_ctrl & 0xe);
    }

    /*
     * Check LOCK_ECC_ERR_INJ bit.
     * If this bit is 1, then this bit and register CC_ERR_INJ can’t be
     * changed by software anymore, only reset can change to reset value 0.
     */
    if (smpcc->cc_ctrl & 0x10)
    {
        value |= 0x10;
    }

    return value;

}

static uint64_t nuclei_smpcc_get_clm_size(NucLeiSMPCCState *smpcc)
{
    uint32_t way_to_clm_num = 0;
    uint32_t clm_size = 0;

    for (uint32_t i = 0; i < 16; i++)
    {
        if((smpcc->clm_way_en >> i) & 0x1)
            way_to_clm_num ++;
    }

    clm_size = smpcc->cc_size / 16 * way_to_clm_num;
    return clm_size;
}


static uint64_t nuclei_smpcc_read(void *opaque, hwaddr addr, unsigned size)
{
    NucLeiSMPCCState *smpcc = opaque;
    CPURISCVState *env = current_cpu->env_ptr;

    uint64_t value = 0;

    switch (addr)
    {
    case 0x0:
        value = smpcc->smp_ver;
        break;
    case 0x4:
        value = smpcc->smp_cfg;
        break;
    case 0x8:
        value = smpcc->cc_cfg;
        break;
    case 0xc:
        value = smpcc->smp_enb;
        break;
    case 0x10:
        if (env->priv == PRV_M)
        {
            value = smpcc->cc_ctrl;
        }
        break;
    case 0x14:
        if (env->priv == PRV_M)
        {
            value = smpcc->cc_mcmd;
        }
        break;
    case 0x18:
        value = smpcc->cc_err_inj;
        break;
    case 0x1c:
        value = smpcc->cc_recv_cnt;
        break;
    case 0x20:
        value = smpcc->cc_fatal_cnt;
        break;
    case 0x24:
        value = smpcc->cc_recv_thv;
        break;
    case 0x28:
        value = smpcc->cc_fatal_thv;
        break;
    case 0x2c:
        value = smpcc->cc_bus_err_addr;
        break;
    case 0x40 ... 0xbc:
        value = smpcc->clint_err_status[(addr - 0x40) / 8];
        break;
    case 0xc0:
        if ((smpcc->cc_ctrl & 0x200) && (env->priv == PRV_S))
            value = smpcc->cc_scmd;
        break;
    case 0xc4:
        if ((smpcc->cc_ctrl & 0x400) && (env->priv == PRV_U))
            value = smpcc->cc_ucmd;
        break;
    case 0xc8:
        value = smpcc->snoop_pending;
        break;
    case 0xcc:
        value = smpcc->trans_pending;
        break;
    case 0xd0:
        value = smpcc->clm_addr_base;
        break;
    case 0xd8:
        value = smpcc->clm_way_en;
        break;
    case 0xdc:
        value = smpcc->cc_invalid_all;
        break;
    case 0x100 ... 0x178:
        value = smpcc->ns_rg[(addr - 0x100) / 8];
        break;
    case 0x180 ... 0x1bc:
        if (((smpcc->cc_ctrl & 0x200) && (env->priv == PRV_S))
            || ((smpcc->cc_ctrl & 0x400) && (env->priv == PRV_U)))
            value = smpcc->smp_pmon_sel[(addr - 0x180) / 4];
        break;
    case 0x1c0 ... 0x23c:
        value = smpcc->smp_pmon_cnt[(addr - 0x1c0) / 8];
        break;
    case 0x280 ... 0x378:
        value = smpcc->client_err_addr[(addr - 0x280) / 8];
        break;
    case 0x380 ... 0x3fc:
        value = smpcc->client_way_mask[(addr - 0x380) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "smpcc: invalid read : 0x%" HWADDR_PRIx "\n",addr);
        break;
    }

    return value;
}

static void nuclei_smpcc_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned size)
{
    NucLeiSMPCCState *smpcc = opaque;
    CPURISCVState *env = current_cpu->env_ptr;
    uint32_t clm_size = 0;

    switch (addr)
    {
    case 0x0:
        qemu_log_mask(LOG_GUEST_ERROR,
                "smpcc: write read-only reg.\n");
        break;
    case 0x4:
        qemu_log_mask(LOG_GUEST_ERROR,
                "smpcc: write read-only reg.\n");
        break;
    case 0x8:
        qemu_log_mask(LOG_GUEST_ERROR,
                "smpcc: write read-only reg.\n");
        break;
    case 0xc:
        smpcc->smp_enb = value;
        break;
    case 0x10:
        if (env->priv == PRV_M)
        {
            smpcc->cc_ctrl = nuclei_smpcc_update_cc_ctrl(smpcc, value);
        }
        break;
    case 0x14:
        if (env->priv != PRV_M)
            break;
        smpcc->cc_mcmd = value;
        uint64_t cmd_code = value & 0x1f;
        if (cmd_code == 0x111)
        {
            /* To Do */
            /* Flush all the valid and dirty cachelines, Lock bit is not affected. */
        }
        else if (cmd_code == 0x110)
        {
            /* To Do */
            /* Unlock and Flush and invalid all the valid and dirty cachelines. */
        }

        if (value & 0x800000)
        {
            /* Clear CC_RECV_CNT */
            smpcc->cc_recv_cnt = 0;
        }
        if (value & 1000000)
        {
            /* Clear CC_FATAL_CNT */
            smpcc->cc_fatal_cnt = 0;
        }
        if (value & 2000000)
        {
            /* Clear BUS_ERR_PEND */
            smpcc->cc_ctrl &= ~(uint32_t)(1 << 7);
        }
        break;
    case 0x18:
        if (smpcc->cc_ctrl & 0x10)
            break;
        smpcc->cc_err_inj = value;
        break;
    case 0x1c:
        smpcc->cc_recv_cnt = value;
        break;
    case 0x20:
        smpcc->cc_fatal_cnt = value;
        break;
    case 0x24:
        smpcc->cc_recv_thv = value;
        break;
    case 0x28:
        smpcc->cc_fatal_thv = value;
        break;
    case 0x2c:
        smpcc->cc_bus_err_addr = value;
        break;
    case 0x40 ... 0xbc:
        smpcc->clint_err_status[(addr - 0x40) / 8] = value;
        break;
    case 0xc0:
        if ((smpcc->cc_ctrl & 0x200) && (env->priv == PRV_S))
            smpcc->cc_scmd = value;
        break;
    case 0xc4:
        if ((smpcc->cc_ctrl & 0x400) && (env->priv == PRV_U))
            smpcc->cc_ucmd = value;
        break;
    case 0xc8:
        qemu_log_mask(LOG_GUEST_ERROR,
                "smpcc: write read-only reg.\n");
        break;
    case 0xcc:
        qemu_log_mask(LOG_GUEST_ERROR,
                "smpcc: write read-only reg.\n");
        break;
    case 0xd0:
        smpcc->clm_addr_base = value;
        memory_region_set_address(&smpcc->clm, smpcc->clm_addr_base);
        break;
    case 0xd8:
        smpcc->clm_way_en = value;
        clm_size = nuclei_smpcc_get_clm_size(smpcc);
        memory_region_ram_resize(&smpcc->clm, clm_size, &error_abort);
        break;
    case 0xdc:
        smpcc->cc_invalid_all = value;
        break;
    case 0x100 ... 0x178:
        smpcc->ns_rg[(addr - 0x100) / 8] = value;
        break;
    case 0x180 ... 0x1bc:
        if (((smpcc->cc_ctrl & 0x200) && (env->priv == PRV_S))
            || ((smpcc->cc_ctrl & 0x400) && (env->priv == PRV_U)))
            smpcc->smp_pmon_sel[(addr - 0x180) / 4] = value;
        break;
    case 0x1c0 ... 0x23c:
        smpcc->smp_pmon_cnt[(addr - 0x1c0) / 8] = value;
        break;
    case 0x280 ... 0x378:
        smpcc->client_err_addr[(addr - 0x280) / 8] = value;
        break;
    case 0x380 ... 0x3fc:
        smpcc->client_way_mask[(addr - 0x380) / 4] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "smpcc: invalid write : 0x%" HWADDR_PRIx "\n",addr);
        break;
    }
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

    memory_region_init_resizeable_ram(&smpcc->clm, NULL, "riscv.nuclei.smpcc.clm",
                                        nuclei_smpcc_get_clm_size(smpcc), smpcc->cc_size, 
                                        NULL, &error_fatal);
    memory_region_add_subregion(get_system_memory(), smpcc->clm_addr_base,
                                &smpcc->clm);

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

