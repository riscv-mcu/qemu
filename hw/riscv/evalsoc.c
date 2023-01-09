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
#include "hw/riscv/evalsoc.h"

static const MemMapEntry evalsoc_memmap[] = {
    [EVALSOC_DEBUG] = { 0x0,           0x1000 },
    [EVALSOC_MROM]  = { 0x1000,        0x2000 },
    [EVALSOC_TEST]  = { 0x100000,      0x1000 },
    [EVALSOC_TIMER] = { 0x2000000,     0x1000 },
    [EVALSOC_ECLIC] = { 0xc000000,     0x10000 },
    [EVALSOC_GPIO]  = { 0x10012000,    0x1000 },
    [EVALSOC_UART0] = { 0x10013000,    0x1000 },
    [EVALSOC_QSPI0] = { 0x10014000,    0x1000 },
    [EVALSOC_UART1] = { 0x10023000,    0x1000 },
    [EVALSOC_QSPI1] = { 0x10024000,    0x1000 },
    [EVALSOC_QSPI2] = { 0x10034000,    0x1000 },
    [EVALSOC_SMP]   = { 0x12000000,    0x00001000 },
    [EVALSOC_XIP]   = { 0x20000000,    0x20000000},
    [EVALSOC_ILM]   = { 0x80000000,    0x2000000 },
    [EVALSOC_DLM]   = { 0x90000000,    0x2000000 },
    [EVALSOC_DDR]   = { 0xA0000000,    0x4000000 },
};

static void evalsoc_machine_init(MachineState *machine)
{
}

static void evalsoc_machine_instance_init(Object *obj)
{
}

static void evalsoc_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nuclei RISC-V EvalSoC, support Nuclei RISC-V 200/300/600/900 series processors";
    mc->init = evalsoc_machine_init;
    mc->max_cpus = 16;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
}

static const TypeInfo evalsoc_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("evalsoc"),
    .parent     = TYPE_MACHINE,
    .class_init = evalsoc_machine_class_init,
    .instance_init = evalsoc_machine_instance_init,
    .instance_size = sizeof(EvalSoCState),
};

static void evalsoc_machine_init_register_types(void)
{
    type_register_static(&evalsoc_machine_typeinfo);
}

type_init(evalsoc_machine_init_register_types)
