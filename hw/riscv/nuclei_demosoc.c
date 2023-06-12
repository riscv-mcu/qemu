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
#include "hw/char/sifive_uart.h"
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
#include "hw/riscv/nuclei_demosoc.h"
#include "hw/ssi/ssi.h"

#define OTP_SERIAL 1

#if defined(TARGET_RISCV32)
#define BIOS_FILENAME "opensbi-riscv32-generic-fw_dynamic.bin"
#else
#define BIOS_FILENAME "opensbi-riscv64-generic-fw_dynamic.bin"
#endif

static const struct MemmapEntry
{
    hwaddr base;
    hwaddr size;
}  demosoc_memmap[] = {
    [DEMOSOC_DEBUG] = { 0x0,           0x1000 },
    [DEMOSOC_MROM]  = { 0x1000,        0xf000 },
    [DEMOSOC_TEST]  = { 0x100000,      0x10000 },
    [DEMOSOC_TIMER] = { 0x2000000,     0x10000 },
    [DEMOSOC_PLIC]  = { 0x8000000,     0x4000000},
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

static void create_fdt(DemoSoCState *s, const struct MemmapEntry *memmap,
                       uint64_t mem_size, const char *cmdline)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    uint32_t plic_phandle, uart_phandle, gpio_phandle, phandle = 1;
    uint32_t hfclk_phandle,test_phandle;

    if (ms->dtb)
    {
        fdt = s->fdt = load_device_tree(ms->dtb, &s->fdt_size);
        if (!fdt)
        {
            error_report("load_device_tree() failed");
            exit(1);
        }
        goto update_bootargs;
    }
    else
    {
        fdt = s->fdt = create_device_tree(&s->fdt_size);
        if (!fdt)
        {
            error_report("create_device_tree() failed");
            exit(1);
        }
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "nuclei,ux600");
    qemu_fdt_setprop_string(fdt, "/", "compatible",
                            "nuclei,ux600");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);


    qemu_fdt_add_subnode(fdt, "/console");
    qemu_fdt_setprop_string(fdt, "/console", "compatible", "sbi,console");

    hfclk_phandle = phandle++;
    nodename = g_strdup_printf("/hfclk");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "clock-output-names", "hfclk");
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
                          DEMOSOC_HFCLK_FREQ);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, nodename, "#clock-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/hfclk", "phandle", hfclk_phandle);
    g_free(nodename);

    nodename = g_strdup_printf("/memory@%lx",
                               (long)memmap[DEMOSOC_DDR].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           memmap[DEMOSOC_DDR].base >> 32, memmap[DEMOSOC_DDR].base,
                           mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    // qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
    //                       SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = 0; cpu < ms->smp.cpus; cpu ++)
    {
        int cpu_phandle = phandle++;
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa;
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv39");
        isa = riscv_isa_string(&s->soc.cpus.harts[cpu]);
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", cpu_phandle);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    cells = g_new0(uint32_t, ms->smp.cpus * 4);
    for (cpu = 0; cpu < ms->smp.cpus; cpu++)
    {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        g_free(nodename);
    }

    nodename = g_strdup_printf("/soc/clint@%lx",
                               (long)memmap[DEMOSOC_TIMER].base + 0x1000);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    // qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
    //                  cells, ms->smp.cpus * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/timer@%lx",
                               (long)memmap[DEMOSOC_TIMER].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,timer0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[DEMOSOC_TIMER].base,
                           0x0, memmap[DEMOSOC_TIMER].size);
    g_free(nodename);

    cells = g_new0(uint32_t, ms->smp.cpus * 4);
    for (cpu = 0; cpu < ms->smp.cpus; cpu++)
    {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        g_free(nodename);
    }

    plic_phandle = phandle++;
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
                               (long)memmap[DEMOSOC_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
                     cells, (ms->smp.cpus * 4 ) * sizeof(uint32_t));
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[DEMOSOC_PLIC].base,
                           0x0, memmap[DEMOSOC_PLIC].size);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", 0x35);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", plic_phandle);
    g_free(cells);
    g_free(nodename);

    test_phandle = phandle++;
    nodename = g_strdup_printf("/soc/test@%lx",
        (long)memmap[DEMOSOC_TEST].base);
    qemu_fdt_add_subnode(fdt, nodename);
    {
        static const char * const compat[3] = {
            "sifive,test1", "sifive,test0", "syscon"
        };
        qemu_fdt_setprop_string_array(fdt, nodename, "compatible", (char **)&compat,
                                      ARRAY_SIZE(compat));
    }
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[DEMOSOC_TEST].base,
        0x0, memmap[DEMOSOC_TEST].size);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", test_phandle);
    test_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/reboot");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(fdt, nodename, "regmap", test_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "offset", 0x0);
    qemu_fdt_setprop_cell(fdt, nodename, "value", FINISHER_RESET);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/poweroff");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "syscon-poweroff");
    qemu_fdt_setprop_cell(fdt, nodename, "regmap", test_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "offset", 0x0);
    qemu_fdt_setprop_cell(fdt, nodename, "value", FINISHER_PASS);
    g_free(nodename);

    gpio_phandle = phandle++;
    nodename = g_strdup_printf("/soc/gpio@%lx",
                               (long)memmap[DEMOSOC_GPIO].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 2);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(fdt, nodename, "#gpio-cells", 2);
    qemu_fdt_setprop(fdt, nodename, "gpio-controller", NULL, 0);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[DEMOSOC_GPIO].base,
                           0x0, memmap[DEMOSOC_GPIO].size);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", DEMOSOC_GPIO_IRQ0,
                           DEMOSOC_GPIO_IRQ1, DEMOSOC_GPIO_IRQ2, DEMOSOC_GPIO_IRQ3,
                           DEMOSOC_GPIO_IRQ4, DEMOSOC_GPIO_IRQ5, DEMOSOC_GPIO_IRQ6,
                           DEMOSOC_GPIO_IRQ7, DEMOSOC_GPIO_IRQ8, DEMOSOC_GPIO_IRQ9,
                           DEMOSOC_GPIO_IRQ10, DEMOSOC_GPIO_IRQ11, DEMOSOC_GPIO_IRQ12,
                           DEMOSOC_GPIO_IRQ13, DEMOSOC_GPIO_IRQ14, DEMOSOC_GPIO_IRQ15,
                           DEMOSOC_GPIO_IRQ16, DEMOSOC_GPIO_IRQ17, DEMOSOC_GPIO_IRQ18,
                           DEMOSOC_GPIO_IRQ19, DEMOSOC_GPIO_IRQ20, DEMOSOC_GPIO_IRQ21,
                           DEMOSOC_GPIO_IRQ22, DEMOSOC_GPIO_IRQ23, DEMOSOC_GPIO_IRQ24,
                           DEMOSOC_GPIO_IRQ25, DEMOSOC_GPIO_IRQ26, DEMOSOC_GPIO_IRQ27,
                           DEMOSOC_GPIO_IRQ28, DEMOSOC_GPIO_IRQ29, DEMOSOC_GPIO_IRQ30,
                           DEMOSOC_GPIO_IRQ31);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,gpio0");
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", gpio_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx",
                               memmap[DEMOSOC_QSPI0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,spi0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[DEMOSOC_QSPI0].base,
                           0x0, memmap[DEMOSOC_QSPI0].size,
                           0x0, 0x20000000,
                           0x0, 0x10000000);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", DEMOSOC_SPI0_IRQ);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 0);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx/flash@0",
                               (long)memmap[DEMOSOC_QSPI0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "jedec,spi-nor");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0x0);
    qemu_fdt_setprop_cells(fdt, nodename, "spi-max-frequency", 1000000);
    // qemu_fdt_setprop_cells(fdt, nodename, "m25p,fast-read");
    qemu_fdt_setprop_cells(fdt, nodename, "#spi-tx-bus-width", 0x1);
    qemu_fdt_setprop_cells(fdt, nodename, "#spi-rx-bus-width", 0x1);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx",
                               (long)memmap[DEMOSOC_QSPI2].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,spi0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[DEMOSOC_QSPI2].base,
                           0x0, memmap[DEMOSOC_QSPI2].size);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", DEMOSOC_SPI2_IRQ);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 0);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx/mmc@0",
                               (long)memmap[DEMOSOC_QSPI2].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "mmc-spi-slot");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0x0);
    qemu_fdt_setprop_cells(fdt, nodename, "spi-max-frequency", 20000000);
    qemu_fdt_setprop_cells(fdt, nodename, "voltage-ranges", 3300, 3300);
    qemu_fdt_setprop_cells(fdt, nodename, "disable-wp");
    g_free(nodename);

    uart_phandle = phandle++;
    qemu_fdt_add_subnode(fdt, "/aliases");
    nodename = g_strdup_printf("/soc/serial@%lx",
                               (long)memmap[DEMOSOC_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,uart0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[DEMOSOC_UART0].base,
                           0x0, memmap[DEMOSOC_UART0].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", DEMOSOC_UART0_IRQ);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", uart_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", nodename);
    g_free(nodename);

    uart_phandle = phandle++;
    nodename = g_strdup_printf("/soc/serial@%lx",
                               (long)memmap[DEMOSOC_UART1].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,uart0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[DEMOSOC_UART1].base,
                           0x0, memmap[DEMOSOC_UART1].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", DEMOSOC_UART1_IRQ);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", uart_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
    qemu_fdt_setprop_string(fdt, "/aliases", "serial1", nodename);

    qemu_fdt_add_subnode(fdt, "/chosen");
    // set stdout-path for opensbi
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", "serial0");
    g_free(nodename);

update_bootargs:
    if (cmdline)
    {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
}

static void demosoc_machine_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = demosoc_memmap;
    target_ulong start_addr = memmap[DEMOSOC_DDR].base;
    DemoSoCState *s = RISCV_DEMOSOC_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    uint32_t start_addr_hi32 = 0x00000000;
    uint32_t fdt_load_addr;
    uint64_t kernel_entry;
    target_ulong firmware_end_addr, kernel_start_addr;
    int i;
    DriveInfo *dinfo;
    BlockBackend *blk;
    DeviceState *flash_dev, *sd_dev, *card_dev;
    qemu_irq flash_cs, sd_cs;

    /* TODO: Add qtest support */
    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_DEMOSOC_SOC);
    object_property_set_uint(OBJECT(&s->soc), "serial", s->serial,
                             &error_abort);

    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    memory_region_init_ram(&s->soc.ddr, NULL, "riscv.demosoc.ram.ddr",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[DEMOSOC_DDR].base,
                                &s->soc.ddr);

    memory_region_init_ram(&s->soc.ilm, NULL, "riscv.demosoc.ram.ilm",
        memmap[DEMOSOC_ILM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[DEMOSOC_ILM].base, &s->soc.ilm);

    memory_region_init_ram(&s->soc.dlm, NULL, "riscv.demosoc.ram.dlm",
        memmap[DEMOSOC_DLM].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[DEMOSOC_DLM].base, &s->soc.dlm);


    memory_region_init_ram(&s->soc.smp, NULL, "riscv.demosoc.ram.smp",
        memmap[DEMOSOC_SMP].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[DEMOSOC_SMP].base, &s->soc.smp);

    memory_region_init_ram(&s->soc.xip_mem, NULL, "riscv.demosoc.flashxip",
        memmap[DEMOSOC_XIP].size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        memmap[DEMOSOC_XIP].base, &s->soc.xip_mem);

    for (i = 0; i < machine->smp.cpus; i ++) {
        s->soc.cpus.harts[i].env.msmpcfg_info = (memmap[DEMOSOC_SMP].base & ~(1<<10)) | 0xF;
    }
    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);


    if(s->download == NULL){
        start_addr = memmap[DEMOSOC_XIP].base;
    } else if (!strcmp(s->download, "ilm")) {
        start_addr = memmap[DEMOSOC_ILM].base;
    } else if (!strcmp(s->download, "ddr")) {
        start_addr = memmap[DEMOSOC_DDR].base;
    } else if (!strcmp(s->download, "sram")) {
        // sram mode use ddr mode base address
        start_addr = memmap[DEMOSOC_DDR].base;
    } else {
        start_addr = memmap[DEMOSOC_XIP].base;
    }

    if (machine->firmware) {
        firmware_end_addr = riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                                     start_addr, NULL);
    } else {
        firmware_end_addr = (target_ulong)(-1);
    }

    if (machine->kernel_filename)
    {
        if (firmware_end_addr != (target_ulong)(-1)) {
            kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc.cpus,
                                                         firmware_end_addr);
        } else {
            kernel_start_addr = start_addr;
        }

        kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                         kernel_start_addr, NULL);

        if (machine->initrd_filename)
        {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    }
    else
    {
        /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;
    }
    /* Compute the fdt load address in dram */
    fdt_load_addr = riscv_load_fdt(memmap[DEMOSOC_DDR].base,
                                   machine->ram_size, s->fdt);

#if defined(TARGET_RISCV64)
    start_addr_hi32 = start_addr >> 32;
#endif
    /* reset vector */
    uint32_t reset_vec[11] = {
        s->msel,    /* MSEL pin state */
        0x00000297, /* 1:  auipc  t0, %pcrel_hi(fw_dyn) */
        0x02828613, /*     addi   a2, t0, %pcrel_lo(1b) */
        0xf1402573, /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0202a583, /*     lw     a1, 32(t0) */
        0x0182a283, /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0202b583, /*     ld     a1, 32(t0) */
        0x0182b283, /*     ld     t0, 24(t0) */
#endif
        0x00028067, /*     jr     t0 */
        start_addr, /* start: .dword */
        start_addr_hi32,
        fdt_load_addr, /* fdt_laddr: .dword */
        0x00000000,
        /* fw_dyn: */
    };

     /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < ARRAY_SIZE(reset_vec); i++)
    {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[DEMOSOC_MROM].base, &address_space_memory);

    riscv_rom_copy_firmware_info(machine, memmap[DEMOSOC_MROM].base,
                                 memmap[DEMOSOC_MROM].size,
                                 sizeof(reset_vec), kernel_entry);

     /* Connect an SPI flash to SPI0 */
    flash_dev = qdev_new("is25wp256");
    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo)
    {
        qdev_prop_set_drive_err(flash_dev, "drive",
                                blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }
    qdev_realize_and_unref(flash_dev, BUS(s->soc.spi0.spi), &error_fatal);

    flash_cs = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->soc.spi0), 1, flash_cs);

    /* Connect an SD card to SPI2 */
    sd_dev = ssi_create_peripheral(s->soc.spi2.spi, "ssi-sd");

    sd_cs = qdev_get_gpio_in_named(sd_dev, SSI_GPIO_CS, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->soc.spi2), 1, sd_cs);

    dinfo = drive_get(IF_SD, 0, 0);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    card_dev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(card_dev, "drive", blk, &error_fatal);
    qdev_prop_set_bit(card_dev, "spi", true);
    qdev_realize_and_unref(card_dev,
                           qdev_get_child_bus(sd_dev, "sd-bus"),
                           &error_fatal);

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
    mc->min_cpus = 1;
    mc->default_cpu_type = DEMOSOC_CPU;
    mc->default_cpus = mc->min_cpus;

    object_class_property_add_str(oc, "download",
                                   demosoc_machine_get_download,
                                   demosoc_machine_set_download);
    object_class_property_set_description(oc, "download",
                                          "Set on to tell QEMU's ROM to jump to "
                                          "download modes. Otherwise QEMU will jump to flash base address, aka download=flashxip"
                                          "nuclei support these download modes(flashxip,flash,ilm,ddr,sram)");

}

static const TypeInfo demosoc_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("nuclei_demosoc"),
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
    DemoSoCSoCState *s = RISCV_DEMOSOC_SOC(obj);

    object_initialize_child(obj, "u-cluster", &s->u_cluster, TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->u_cluster), "cluster-id", 1);

    object_initialize_child(OBJECT(&s->u_cluster), "cpus", &s->cpus,
                            TYPE_RISCV_HART_ARRAY);

    object_initialize_child(obj, "gpio", &s->gpio, TYPE_SIFIVE_GPIO);
    object_initialize_child(obj, "spi0", &s->spi0, TYPE_SIFIVE_SPI);
    object_initialize_child(obj, "spi2", &s->spi2, TYPE_SIFIVE_SPI);
    object_initialize_child(obj, "timer", &s->timer, TYPE_NUCLEI_SYSTIMER);
}

static void riscv_demosoc_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    DemoSoCSoCState *s = RISCV_DEMOSOC_SOC(dev);
    const struct MemmapEntry *memmap = demosoc_memmap;
    MemoryRegion *sys_mem = get_system_memory();
    int i = 0;
    char *plic_hart_config;
    size_t plic_hart_config_len;


    qdev_prop_set_uint32(DEVICE(&s->cpus), "num-harts", ms->smp.cpus);
    qdev_prop_set_uint32(DEVICE(&s->cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->cpus), "cpu-type", s->cpu_type);
    qdev_prop_set_uint64(DEVICE(&s->cpus), "resetvec", 0x1004);

    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    /*
     * The cluster must be realized after the RISC-V hart array container,
     * as the container's CPU object is only created on realize, and the
     * CPU must exist and have been parented into the cluster before the
     * cluster is realized.
     */
    qdev_realize(DEVICE(&s->u_cluster), NULL, &error_abort);

    /* Mask ROM */
    memory_region_init_rom(&s->internal_rom, OBJECT(dev), "riscv.demosoc.irom",
                           memmap[DEMOSOC_MROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[DEMOSOC_MROM].base, &s->internal_rom);


    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(DEMOSOC_PLIC_HART_CONFIG) + 1) *
                           ms->smp.cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < ms->smp.cpus; i++)
    {
        if (i != 0) {
            strncat(plic_hart_config, ",", plic_hart_config_len);
        }
        strncat(plic_hart_config, DEMOSOC_PLIC_HART_CONFIG,
                plic_hart_config_len);
        plic_hart_config_len -= (strlen(DEMOSOC_PLIC_HART_CONFIG) + 1);
    }
    /* MMIO */
    s->plic = sifive_plic_create(memmap[DEMOSOC_PLIC].base,
                                 plic_hart_config, ms->smp.cpus, 0,
                                 DEMOSOC_PLIC_NUM_SOURCES,
                                 DEMOSOC_PLIC_NUM_PRIORITIES,
                                 DEMOSOC_PLIC_PRIORITY_BASE,
                                 DEMOSOC_PLIC_PENDING_BASE,
                                 DEMOSOC_PLIC_ENABLE_BASE,
                                 DEMOSOC_PLIC_ENABLE_STRIDE,
                                 DEMOSOC_PLIC_CONTEXT_BASE,
                                 DEMOSOC_PLIC_CONTEXT_STRIDE,
                                 memmap[DEMOSOC_PLIC].size);
    g_free(plic_hart_config);

    s->eclic = nuclei_eclic_create(memmap[DEMOSOC_ECLIC].base,
                                    memmap[DEMOSOC_ECLIC].size,
                                    false, false, true,
                                    ms->smp.cpus,
                                    DEMOSOC_INT_MAX,
                                    DEMOSOC_CLIC_INTCTLBITS);

    if(ms->kernel_filename)
    {
        /* Create and connect UART interrupts to the ECLIC */
        nuclei_uart_create(sys_mem,
                        memmap[DEMOSOC_UART0].base,
                        memmap[DEMOSOC_UART0].size,
                        serial_hd(0),
                        nuclei_eclic_get_irq(DEVICE(s->eclic),
                        DEMOSOC_INT22_IRQn));

        nuclei_systimer_create(memmap[DEMOSOC_TIMER].base,
                memmap[DEMOSOC_TIMER].size, 0, ms->smp.cpus, s->eclic, DEMOSOC_TIMEBASE_FREQ);
    }
    else
    {
        sifive_uart_create(sys_mem, memmap[DEMOSOC_UART0].base,
                       serial_hd(0), qdev_get_gpio_in(DEVICE(s->plic), DEMOSOC_UART0_IRQ));

        sifive_uart_create(sys_mem, memmap[DEMOSOC_UART1].base,
                        serial_hd(1), qdev_get_gpio_in(DEVICE(s->plic), DEMOSOC_UART1_IRQ));

        nuclei_systimer_create(memmap[DEMOSOC_TIMER].base,
                memmap[DEMOSOC_TIMER].size, 0, ms->smp.cpus, NULL, DEMOSOC_TIMEBASE_FREQ);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer), errp))
    {
        return;
    }

    qdev_prop_set_uint32(DEVICE(&s->gpio), "ngpio", 32);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp))
    {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[DEMOSOC_GPIO].base);

    /* Pass all GPIOs to the SOC layer so they are available to the board */
    qdev_pass_gpios(DEVICE(&s->gpio), dev, NULL);

    /* Connect GPIO interrupts to the PLIC */
    for (i = 0; i < 32; i++)
    {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), i,
                           qdev_get_gpio_in(DEVICE(s->plic),
                                            DEMOSOC_GPIO_IRQ0 + i));
    }

    sysbus_realize(SYS_BUS_DEVICE(&s->spi0), errp);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi0), 0,
                    memmap[DEMOSOC_QSPI0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi0), 0,
                       qdev_get_gpio_in(DEVICE(s->plic), DEMOSOC_SPI0_IRQ));

    sysbus_realize(SYS_BUS_DEVICE(&s->spi2), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi2), 0,
                    memmap[DEMOSOC_QSPI2].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi2), 0,
                       qdev_get_gpio_in(DEVICE(s->plic), DEMOSOC_SPI2_IRQ));

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[DEMOSOC_TEST].base);

}

static Property demosoc_soc_props[] = {
    DEFINE_PROP_UINT32("serial", DemoSoCSoCState, serial, OTP_SERIAL),
    DEFINE_PROP_STRING("cpu-type", DemoSoCSoCState, cpu_type),
    DEFINE_PROP_END_OF_LIST()
};

static void riscv_demosoc_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_props(dc, demosoc_soc_props);
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
