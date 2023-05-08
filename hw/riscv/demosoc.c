/*
 * QEMU RISC-V VirtIO Board
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * RISC-V machine with 16550a UART and VirtIO MMIO
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
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/char/nuclei_uart.h"
#include "hw/riscv/demosoc.h"

static const struct MemmapEntry
{
    hwaddr base;
    hwaddr size;
}  demosoc_memmap[] = {
    [DEMOSOC_DEBUG] = { 0x0,           0x1000 },
    [DEMOSOC_MROM]  = { 0x1000,        0x2000 },
    [DEMOSOC_TEST]  = { 0x100000,      0x1000 },
    [DEMOSOC_TIMER] = { 0x2000000,     0x1000 },
    [DEMOSOC_ECLIC] = { 0xc000000,     0x10000 },
    [DEMOSOC_GPIO]  = { 0x10012000,    0x1000 },
    [DEMOSOC_UART0] = { 0x10013000,    0x1000 },
    [DEMOSOC_QSPI0] = { 0x10014000,    0x1000 },
    [DEMOSOC_UART1] = { 0x10023000,    0x1000 },
    [DEMOSOC_QSPI1] = { 0x10024000,    0x1000 },
    [DEMOSOC_QSPI2] = { 0x10034000,    0x1000 },
    [DEMOSOC_SMP]   = { 0x12000000,    0x00001000 },
    [DEMOSOC_XIP]   = { 0x20000000,    0x20000000},
    [DEMOSOC_ILM]   = { 0x80000000,    0x2000000 },
    [DEMOSOC_DLM]   = { 0x90000000,    0x2000000 },
    [DEMOSOC_DDR]   = { 0xA0000000,    0x10000000 },
};

static void demosoc_machine_init(MachineState *machine)
{
    uint32_t start_addr = 0;
    const struct MemmapEntry *memmap = demosoc_memmap;
    DemoSoCState *s = RISCV_DEMOSOC_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    int i;

    /* TODO: Add qtest support */
    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_DEMOSOC_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    memory_region_init_ram(&s->soc.ilm, NULL, "riscv.demosoc.ram.ilm",
        memmap[DEMOSOC_ILM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[DEMOSOC_ILM].base, &s->soc.ilm);

    memory_region_init_ram(&s->soc.dlm, NULL, "riscv.demosoc.ram.dlm",
        memmap[DEMOSOC_DLM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[DEMOSOC_DLM].base, &s->soc.dlm);

    start_addr = memmap[DEMOSOC_ILM].base;

    if(s->download == NULL)
    {

    }else if(!strcmp(s->download, "flash"))
    {
        start_addr = memmap[DEMOSOC_XIP].base;
    }else if(!strcmp(s->download, "flashxip"))
    {
        start_addr = memmap[DEMOSOC_XIP].base;
    }else if(!strcmp(s->download, "ddr"))
    {
        start_addr = memmap[DEMOSOC_DDR].base;
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297, /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593, /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573, /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283, /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283, /*     ld     t0, 24(t0) */
#endif
        0x00028067, /*     jr     t0 */
        0x00000000,
        start_addr, /* start: .dword DRAM_BASE */
        0x00000000,
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++)
    {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
        memmap[DEMOSOC_MROM].base, &address_space_memory);
    /* boot rom */
    if (machine->kernel_filename) {
       riscv_load_kernel(machine->kernel_filename, 
            start_addr, NULL);
    }
}

static void demosoc_machine_instance_init(Object *obj)
{

}

static char* demosoc_machine_get_download(Object *obj, Error **errp)
{
    DemoSoCState *s = RISCV_DEMOSOC_MACHINE(obj);
    return g_strdup(s->download);
}

static void demosoc_machine_set_download(Object *obj, const char *value, Error **errp)
{
    DemoSoCState *s = RISCV_DEMOSOC_MACHINE(obj);
    s->download = g_strdup(value);
}

static void demosoc_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nuclei RISC-V DemoSoC, support Nuclei RISC-V 200/300/600/900 series processors";
    mc->init = demosoc_machine_init;
    mc->max_cpus = 16;
    mc->is_default = false;
    mc->default_cpu_type = DEMOSOC_CPU;

    object_class_property_add_str(oc, "download",
                                   demosoc_machine_get_download,
                                   demosoc_machine_set_download);
    object_class_property_set_description(oc, "download",
                                          "Set on to tell QEMU's ROM to jump to "
                                          "download modes. Otherwise QEMU will jump to DRAM "
                                          "nuclei support three download modes(flashxip,flash,ilm,ddr)");

}

static const TypeInfo demosoc_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("demosoc"),
    .parent     = TYPE_MACHINE,
    .class_init = demosoc_machine_class_init,
    .instance_init = demosoc_machine_instance_init,
    .instance_size = sizeof(DemoSoCState),
};

static void demosoc_machine_init_register_types(void)
{
    type_register_static(&demosoc_machine_typeinfo);
}

type_init(demosoc_machine_init_register_types)



static void riscv_demosoc_soc_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    DemoSoCSoCState *s = RISCV_DEMOSOC_SOC(obj);
    object_initialize_child(obj, "cpus", &s->cpus,TYPE_RISCV_HART_ARRAY);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", ms->smp.cpus,
                            &error_abort);

    object_initialize_child(obj, "riscv.demosoc.gpio",
                            &s->gpio, TYPE_SIFIVE_GPIO);
}

static void riscv_demosoc_soc_realize(DeviceState *dev, Error **errp)
{
    const struct MemmapEntry *memmap = demosoc_memmap;
    MachineState *ms = MACHINE(qdev_get_machine());
    DemoSoCSoCState *s = RISCV_DEMOSOC_SOC(dev);
    MemoryRegion *sys_mem = get_system_memory();
    Error *err = NULL;
    int i = 0;

    object_property_set_str(OBJECT(&s->cpus),  "cpu-type", ms->cpu_type,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    /* Mask ROM */
    memory_region_init_rom(&s->internal_rom, OBJECT(dev), "riscv.demosoc.irom",
                           memmap[DEMOSOC_MROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[DEMOSOC_MROM].base, &s->internal_rom);

    s->eclic = nuclei_eclic_create(memmap[DEMOSOC_ECLIC].base, 
                                    memmap[DEMOSOC_ECLIC].size, 
                                    false, false, true,
                                    ms->smp.cpus,
                                    DEMOSOC_INT_MAX,
                                    DEMOSOC_CLIC_INTCTLBITS);

    s->timer = nuclei_systimer_create(memmap[DEMOSOC_TIMER].base,
                memmap[DEMOSOC_TIMER].size, 0, ms->smp.cpus,
                 s->eclic,
                DEMOSOC_TIMEBASE_FREQ);

    /* GPIO */
    sysbus_realize(SYS_BUS_DEVICE(&s->gpio), &err);
    if (err)
    {
        error_propagate(errp, err);
        return;
    }

    /* Map GPIO registers */
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[DEMOSOC_GPIO].base);
    /* Pass all GPIOs to the SOC layer so they are available to the board */

    /* Create and connect UART interrupts to the ECLIC */
    nuclei_uart_create(sys_mem,
                    memmap[DEMOSOC_UART0].base,
                    memmap[DEMOSOC_UART0].size,
                    serial_hd(0),
                    nuclei_eclic_get_irq(DEVICE(s->eclic),
                    DEMOSOC_INT22_IRQn));

    /* Flash memory */
    memory_region_init_rom(&s->xip_mem, OBJECT(dev), "riscv.demosoc.xip",
                        memmap[DEMOSOC_XIP].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
                        memmap[DEMOSOC_XIP].base, &s->xip_mem);
    /* DDR */
    memory_region_init_ram(&s->ddr, OBJECT(dev), "riscv.demosoc.ddr",
                        memmap[DEMOSOC_DDR].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
                        memmap[DEMOSOC_DDR].base, &s->ddr);

    /* SMP */
    memory_region_init_ram(&s->smp, OBJECT(dev), "riscv.demosoc.smp",
                        memmap[DEMOSOC_SMP].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
                        memmap[DEMOSOC_SMP].base, &s->smp);

    for (i = 0; i < ms->smp.cpus; i ++) {
        s->cpus.harts[i].env.msmpcfg_info = (memmap[DEMOSOC_SMP].base & ~(1<<10)) | 0xF;
    }

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[DEMOSOC_TEST].base);
}

static void riscv_demosoc_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = riscv_demosoc_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo riscv_demosoc_soc_type_info = {
    .name = TYPE_DEMOSOC_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(DemoSoCSoCState),
    .instance_init = riscv_demosoc_soc_init,
    .class_init = riscv_demosoc_soc_class_init,
};

static void riscv_demosoc_soc_register_types(void)
{
    type_register_static(&riscv_demosoc_soc_type_info);
}

type_init(riscv_demosoc_soc_register_types)
