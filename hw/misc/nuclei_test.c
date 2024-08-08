/*
 * NUCLEI Test Finisher interface
 *
 * Copyright (c) 2024 Nucleisys, Inc.
 *
 * Test finisher memory mapped device used to exit simulation
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
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "hw/misc/nuclei_test.h"

static uint64_t nuclei_test_read(void *opaque, hwaddr addr, unsigned int size)
{
    return 0;
}

static void nuclei_test_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    if (addr == 0) {
        int status = val64 & 0xffff;
        int code = (val64 >> 16) & 0xffff;
        switch (status) {
        case FINISHER_FAIL:
            exit(code);
        case FINISHER_PASS:
            exit(0);
        case FINISHER_RESET:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return;
        default:
            break;
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%x val=0x%016" PRIx64 "\n",
                  __func__, (int)addr, val64);
}

static const MemoryRegionOps nuclei_test_ops = {
    .read = nuclei_test_read,
    .write = nuclei_test_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 4
    }
};

static void nuclei_test_init(Object *obj)
{
    NucleiTestState *s = NUCLEI_TEST(obj);

    memory_region_init_io(&s->mmio, obj, &nuclei_test_ops, s,
                          TYPE_NUCLEI_TEST, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const TypeInfo nuclei_test_info = {
    .name          = TYPE_NUCLEI_TEST,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NucleiTestState),
    .instance_init = nuclei_test_init,
};

static void nuclei_test_register_types(void)
{
    type_register_static(&nuclei_test_info);
}

type_init(nuclei_test_register_types)


/*
 * Create Test device.
 */
DeviceState *nuclei_test_create(hwaddr addr)
{
    DeviceState *dev = qdev_new(TYPE_NUCLEI_TEST);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    return dev;
}
