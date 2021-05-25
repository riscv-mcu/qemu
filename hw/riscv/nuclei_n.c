/*
 * Nuclei N series  SOC machine interface
 *
 * Copyright (c) 2020 Gao ZhiYuan <alapha23@gmail.com>
 * Copyright (c) 2020-2021 PLCT Lab.All rights reserved.
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
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/misc/unimp.h"
#include "hw/char/riscv_htif.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/char/nuclei_uart.h"
#include "hw/riscv/nuclei_n.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/qtest.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#include <libfdt.h>

static const struct MemmapEntry
{
    hwaddr base;
    hwaddr size;
} nuclei_n_memmap[] = {
    [NUCLEI_N_DEV_DEBUG] = {        0x0,     0x1000 },
    [NUCLEI_N_DEV_ROM]   = {     0x1000,     0x1000 },
    [NUCLEI_N_DEV_TIMER] = {  0x2000000,     0x1000 },
    [NUCLEI_N_DEV_ECLIC] = {  0xc000000,    0x10000 },
    [NUCLEI_N_DEV_GPIO]  = { 0x10012000,     0x1000 },
    [NUCLEI_N_DEV_UART0] = { 0x10013000,     0x1000 },
    [NUCLEI_N_DEV_QSPI0] = { 0x10014000,     0x1000 },
    [NUCLEI_N_DEV_PWM0]  = { 0x10015000,     0x1000 },
    [NUCLEI_N_DEV_UART1] = { 0x10023000,     0x1000 },
    [NUCLEI_N_DEV_QSPI1] = { 0x10024000,     0x1000 },
    [NUCLEI_N_DEV_PWM1]  = { 0x10025000,     0x1000 },
    [NUCLEI_N_DEV_QSPI2] = { 0x10034000,     0x1000 },
    [NUCLEI_N_DEV_PWM2]  = { 0x10035000,     0x1000 },
    [NUCLEI_N_DEV_XIP]   = { 0x20000000,  0x10000000},
    [NUCLEI_N_DEV_ILM]   = { 0x80000000,  0x2000000 },
    [NUCLEI_N_DEV_DLM]   = { 0x90000000,  0x2000000 },
    [NUCLEI_N_DEV_DDR]   = { 0xA0000000,  0x4000000 },
};

static void riscv_nuclei_n_machine_init(MachineState *machine)
{
    uint32_t start_addr = 0;
    const struct MemmapEntry *memmap = nuclei_n_memmap;
    NucleiNState *s = RISCV_NUCLEI_N_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    target_ulong start_addr = memmap[HBIRD_ILM].base;
    int i;

    /* TODO: Add qtest support */
    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_NUCLEI_N_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    memory_region_init_ram(&s->soc.ilm, NULL, "riscv.nuclei.n.ram.ilm",
        memmap[NUCLEI_N_DEV_ILM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[NUCLEI_N_DEV_ILM].base, &s->soc.ilm);

    memory_region_init_ram(&s->soc.dlm, NULL, "riscv.nuclei.n.ram.dlm",
        memmap[NUCLEI_N_DEV_DLM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[NUCLEI_N_DEV_DLM].base, &s->soc.dlm);

    start_addr = memmap[NUCLEI_N_DEV_ILM].base;

    if(s->download == NULL)
    {

    }else if(!strcmp(s->download, "flash"))
    {
        start_addr = memmap[NUCLEI_N_DEV_XIP].base;
    }else if(!strcmp(s->download, "flashxip"))
    {
        start_addr = memmap[NUCLEI_N_DEV_XIP].base;
    }else if(!strcmp(s->download, "ddr"))
    {
        start_addr = memmap[NUCLEI_N_DEV_DDR].base;
    }

    switch (s->msel)
    {
    case MSEL_ILM:
        start_addr = memmap[HBIRD_ILM].base;
        break;
    case MSEL_FLASH:
        start_addr = memmap[HBIRD_XIP].base;
        break;
    case MSEL_FLASHXIP:
        start_addr = memmap[HBIRD_XIP].base;
        break;
    case MSEL_DDR:
        start_addr = memmap[HBIRD_DRAM].base;
        break;
    default:
        start_addr = memmap[HBIRD_ILM].base;
        break;
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
        memmap[NUCLEI_N_DEV_ROM].base, &address_space_memory);
    /* boot rom */
    if (machine->kernel_filename) {
       riscv_load_kernel(machine->kernel_filename, 
            start_addr, NULL);
    }
}

static void riscv_nuclei_n_soc_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    NucleiNSoCState *s = RISCV_NUCLEI_N_SOC(obj);
    object_initialize_child(obj, "cpus", &s->cpus,TYPE_RISCV_HART_ARRAY);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", ms->smp.cpus,
                            &error_abort);

    // object_initialize_child(obj, "timer",
    //                       &s->timer, TYPE_NUCLEI_SYSTIMER);

    object_initialize_child(obj, "riscv.nuclei.gpio",
                            &s->gpio, TYPE_SIFIVE_GPIO);
}

static void riscv_nuclei_n_soc_realize(DeviceState *dev, Error **errp)
{
    const struct MemmapEntry *memmap = nuclei_n_memmap;
    MachineState *ms = MACHINE(qdev_get_machine());
    NucleiNSoCState *s = RISCV_NUCLEI_N_SOC(dev);
    MemoryRegion *sys_mem = get_system_memory();
    Error *err = NULL;

    object_property_set_str(OBJECT(&s->cpus), "cpu-type", ms->cpu_type,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    /* Mask ROM */
    memory_region_init_rom(&s->internal_rom, OBJECT(dev), "riscv.nuclei.n.irom",
                           memmap[NUCLEI_N_DEV_ROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[NUCLEI_N_DEV_ROM].base, &s->internal_rom);

    /* MMIO */
    s->eclic = nuclei_eclic_create(memmap[NUCLEI_N_DEV_ECLIC].base,
        memmap[NUCLEI_N_DEV_ECLIC].size, NUCLEI_N_INT_MAX);

    s->timer = nuclei_systimer_create(memmap[NUCLEI_N_DEV_TIMER].base,
                memmap[NUCLEI_N_DEV_TIMER].size,
                 s->eclic,
                NUCLEI_N_TIMEBASE_FREQ);

    /* GPIO */
    sysbus_realize(SYS_BUS_DEVICE(&s->gpio), &err);
    if (err)
    {
        error_propagate(errp, err);
        return;
    }

    /* Map GPIO registers */
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[NUCLEI_N_DEV_GPIO].base);
    /* Pass all GPIOs to the SOC layer so they are available to the board */

    /* Create and connect UART interrupts to the ECLIC */
    nuclei_uart_create(sys_mem,
                    memmap[NUCLEI_N_DEV_UART0].base,
                    memmap[NUCLEI_N_DEV_UART0].size,
                    serial_hd(0),
                    nuclei_eclic_get_irq(DEVICE(s->eclic),
                    NUCLEI_N_INT22_IRQn));

    /* Flash memory */
    memory_region_init_rom(&s->xip_mem, OBJECT(dev), "riscv.nuclei.n.xip",
                        memmap[NUCLEI_N_DEV_XIP].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
                        memmap[NUCLEI_N_DEV_XIP].base, &s->xip_mem);
    /* DDR */
    memory_region_init_ram(&s->ddr, OBJECT(dev), "riscv.nuclei.n.ddr",
                        memmap[NUCLEI_N_DEV_DDR].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
                        memmap[NUCLEI_N_DEV_DDR].base, &s->ddr);
}

static char* nuclei_n_machine_get_download(Object *obj, Error **errp)
{
    NucleiNState *s = RISCV_NUCLEI_N_MACHINE(obj);
    return g_strdup(s->download);
}

static void nuclei_n_machine_set_download(Object *obj, const char *value, Error **errp)
{
    NucleiNState *s = RISCV_NUCLEI_N_MACHINE(obj);
    s->download = g_strdup(value);
}

static void riscv_nuclei_n_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nuclei RISC-V demosoc on Kit(MCU200T/DDR200T), support Nuclei N/NX class processor";
    mc->init = riscv_nuclei_n_machine_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = NUCLEI_N_CPU;

    object_class_property_add_str(oc, "download",
                                   nuclei_n_machine_get_download,
                                   nuclei_n_machine_set_download);
    object_class_property_set_description(oc, "download",
                                          "Set on to tell QEMU's ROM to jump to "
                                          "download modes. Otherwise QEMU will jump to DRAM "
                                          "nuclei support three download modes(flashxip,flash,ilm,ddr)");
}

static void riscv_nuclei_n_machine_instance_init(Object *obj)
{
    //todo
}

static const TypeInfo riscv_nuclei_n_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("nuclei_n"),
    .parent     = TYPE_MACHINE,
    .class_init = riscv_nuclei_n_machine_class_init,
    .instance_init = riscv_nuclei_n_machine_instance_init,
    .instance_size = sizeof(NucleiNState),
};

static void riscv_nuclei_n_machine_init_register_types(void)
{
    type_register_static(&riscv_nuclei_n_machine_typeinfo);
}

static void riscv_nuclei_n_machine_instance_init(Object *obj)
{
    //todo
}

static const TypeInfo riscv_nuclei_n_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("nuclei_n"),
    .parent     = TYPE_MACHINE,
    .class_init = riscv_nuclei_n_machine_class_init,
    .instance_init = riscv_nuclei_n_machine_instance_init,
    .instance_size = sizeof(NucleiNState),
};

static void riscv_nuclei_n_machine_init_register_types(void)
{
    type_register_static(&riscv_nuclei_n_machine_typeinfo);
}

type_init(riscv_nuclei_n_machine_init_register_types)

static void riscv_nuclei_n_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = riscv_nuclei_n_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo riscv_nuclei_n_soc_type_info = {
    .name = TYPE_NUCLEI_N_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NucleiNSoCState),
    .instance_init = riscv_nuclei_n_soc_init,
    .class_init = riscv_nuclei_n_soc_class_init,
};

static void riscv_nuclei_n_soc_register_types(void)
{
    type_register_static(&riscv_nuclei_n_soc_type_info);
}

type_init(riscv_nuclei_n_soc_register_types)
