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
#include "hw/riscv/nuclei_evalsoc.h"
#include "hw/misc/nuclei_iregion.h"
#include "hw/ssi/ssi.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

#define OTP_SERIAL 1

#if defined(TARGET_RISCV32)
#define BIOS_FILENAME "opensbi-riscv32-generic-fw_dynamic.bin"
#else
#define BIOS_FILENAME "opensbi-riscv64-generic-fw_dynamic.bin"
#endif

#define IREGION_BASE_ADDR   (0x18000000)

static const struct MemmapEntry
{
    hwaddr base;
    hwaddr size;
}  evalsoc_memmap[] = {
    [EVALSOC_IINFO] = { EVALSOC_IINFO_BASE,             EVALSOC_IINFO_SIZE },
    [EVALSOC_MROM]  = { EVALSOC_MROM_BASE,              EVALSOC_MROM_SIZE  },
    [EVALSOC_TEST]  = { EVALSOC_TEST_BASE,              EVALSOC_TEST_SIZE  },
    [EVALSOC_GPIO]  = { EVALSOC_GPIO_BASE,              EVALSOC_GPIO_SIZE  },
    [EVALSOC_UART0] = { EVALSOC_UART0_BASE,             EVALSOC_UART0_SIZE },
    [EVALSOC_UART1] = { EVALSOC_UART1_BASE,             EVALSOC_UART1_SIZE },
    [EVALSOC_QSPI0] = { EVALSOC_QSPI0_BASE,             EVALSOC_QSPI0_SIZE },
    [EVALSOC_QSPI1] = { EVALSOC_QSPI1_BASE,             EVALSOC_QSPI1_SIZE },
    [EVALSOC_QSPI2] = { EVALSOC_QSPI2_BASE,             EVALSOC_QSPI2_SIZE },
    [EVALSOC_XIP]   = { EVALSOC_XIP_BASE,               EVALSOC_XIP_SIZE   },
    [EVALSOC_DEBUG] = { IREGION_DEBUG_OFS,              IREGION_DEBUG_SIZE },
    [EVALSOC_TIMER] = { IREGION_TIMER_OFS,              IREGION_TIMER_SIZE },
    [EVALSOC_PLIC]  = { IREGION_PLIC_OFS,               IREGION_PLIC_SIZE  },
    [EVALSOC_ECLIC] = { IREGION_ECLIC_OFS,              IREGION_ECLIC_SIZE },
    [EVALSOC_SMP]   = { IREGION_SMP_OFS,                IREGION_SMP_SIZE   },
    [EVALSOC_DDR]   = { EVALSOC_DDR_BASE,               EVALSOC_DDR_SIZE   },
    [EVALSOC_CLINT] = { IREGION_TIMER_OFS + 0x1000,     0xF000 },//MTIME in CLINT mode
};

static void riscv_load_initrd(MachineState *machine, uint64_t kernel_entry)
{
    const char *filename = machine->initrd_filename;
    uint64_t mem_size = machine->ram_size;
    void *fdt = machine->fdt;
    hwaddr start, end;
    ssize_t size;

    g_assert(filename != NULL);

    /*
     * We want to put the initrd far enough into RAM that when the
     * kernel is uncompressed it will not clobber the initrd. However
     * on boards without much RAM we must ensure that we still leave
     * enough room for a decent sized initrd, and on boards with large
     * amounts of RAM we must avoid the initrd being so far up in RAM
     * that it is outside lowmem and inaccessible to the kernel.
     * So for boards with less  than 256MB of RAM we put the initrd
     * halfway into RAM, and for boards with 256MB of RAM or more we put
     * the initrd at 128MB.
     */
    start = kernel_entry + MIN(mem_size / 2, 128 * MiB);

    size = load_ramdisk(filename, start, mem_size - start);
    if (size == -1) {
        size = load_image_targphys(filename, start, mem_size - start);
        if (size == -1) {
            error_report("could not load ramdisk '%s'", filename);
            exit(1);
        }
    }

    /* Some RISC-V machines (e.g. opentitan) don't have a fdt. */
    if (fdt) {
        end = start + size;
        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start", start);
        qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end", end);
    }
}

static void create_fdt(EvalSoCState *s, const struct MemmapEntry *memmap,
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
        fdt = ms->fdt = s->fdt = load_device_tree(ms->dtb, &s->fdt_size);
        if (!fdt)
        {
            error_report("load_device_tree() failed");
            exit(1);
        }
        goto update_bootargs;
    }
    else
    {
        fdt = ms->fdt = s->fdt = create_device_tree(&s->fdt_size);
        if (!fdt)
        {
            error_report("create_device_tree() failed");
            exit(1);
        }
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "nuclei,evalsoc");
    qemu_fdt_setprop_string(fdt, "/", "compatible",
                            "nuclei,evalsoc");
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
                          EVALSOC_HFCLK_FREQ);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, nodename, "#clock-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/hfclk", "phandle", hfclk_phandle);
    g_free(nodename);

    nodename = g_strdup_printf("/memory@%lx",
                               (long)EVALSOC_DDR_BASE);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        (hwaddr)s->ddr_base >> 32, (hwaddr)s->ddr_base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          CLINT_TIMEBASE_FREQ);
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
                               (long)((long)memmap[EVALSOC_CLINT].base + s->iregion));
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    // qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
    //                  cells, ms->smp.cpus * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/timer@%lx",
                               (long)((long)memmap[EVALSOC_TIMER].base + s->iregion));
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,timer0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[EVALSOC_TIMER].base + s->iregion,
                           0x0, memmap[EVALSOC_TIMER].size);
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
                               (long)((long)memmap[EVALSOC_PLIC].base + s->iregion));
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
                     cells, (ms->smp.cpus * 4 ) * sizeof(uint32_t));
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[EVALSOC_PLIC].base + s->iregion,
                           0x0, memmap[EVALSOC_PLIC].size);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", 0x35);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", plic_phandle);
    g_free(cells);
    g_free(nodename);

    test_phandle = phandle++;
    nodename = g_strdup_printf("/soc/test@%lx",
        (long)memmap[EVALSOC_TEST].base);
    qemu_fdt_add_subnode(fdt, nodename);
    {
        static const char * const compat[3] = {
            "sifive,test1", "sifive,test0", "syscon"
        };
        qemu_fdt_setprop_string_array(fdt, nodename, "compatible", (char **)&compat,
                                      ARRAY_SIZE(compat));
    }
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[EVALSOC_TEST].base,
        0x0, memmap[EVALSOC_TEST].size);
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
                               (long)memmap[EVALSOC_GPIO].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 2);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(fdt, nodename, "#gpio-cells", 2);
    qemu_fdt_setprop(fdt, nodename, "gpio-controller", NULL, 0);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[EVALSOC_GPIO].base,
                           0x0, memmap[EVALSOC_GPIO].size);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", EVALSOC_PLIC_GPIO_IRQ0,
                           EVALSOC_PLIC_GPIO_IRQ1, EVALSOC_PLIC_GPIO_IRQ2, EVALSOC_PLIC_GPIO_IRQ3,
                           EVALSOC_PLIC_GPIO_IRQ4, EVALSOC_PLIC_GPIO_IRQ5, EVALSOC_PLIC_GPIO_IRQ6,
                           EVALSOC_PLIC_GPIO_IRQ7, EVALSOC_PLIC_GPIO_IRQ8, EVALSOC_PLIC_GPIO_IRQ9,
                           EVALSOC_PLIC_GPIO_IRQ10, EVALSOC_PLIC_GPIO_IRQ11, EVALSOC_PLIC_GPIO_IRQ12,
                           EVALSOC_PLIC_GPIO_IRQ13, EVALSOC_PLIC_GPIO_IRQ14, EVALSOC_PLIC_GPIO_IRQ15,
                           EVALSOC_PLIC_GPIO_IRQ16, EVALSOC_PLIC_GPIO_IRQ17, EVALSOC_PLIC_GPIO_IRQ18,
                           EVALSOC_PLIC_GPIO_IRQ19, EVALSOC_PLIC_GPIO_IRQ20, EVALSOC_PLIC_GPIO_IRQ21,
                           EVALSOC_PLIC_GPIO_IRQ22, EVALSOC_PLIC_GPIO_IRQ23, EVALSOC_PLIC_GPIO_IRQ24,
                           EVALSOC_PLIC_GPIO_IRQ25, EVALSOC_PLIC_GPIO_IRQ26, EVALSOC_PLIC_GPIO_IRQ27,
                           EVALSOC_PLIC_GPIO_IRQ28, EVALSOC_PLIC_GPIO_IRQ29, EVALSOC_PLIC_GPIO_IRQ30,
                           EVALSOC_PLIC_GPIO_IRQ31);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,gpio0");
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", gpio_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx",
                               (long)s->qspi0_base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,spi0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, (hwaddr)s->qspi0_base,
                           0x0, memmap[EVALSOC_QSPI0].size,
                           0x0, 0x20000000,
                           0x0, 0x10000000);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", s->qspi0_irq);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 0);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx/flash@0",
                               (long)s->qspi0_base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "spi-rx-bus-width", 4);
    qemu_fdt_setprop_cell(fdt, nodename, "spi-tx-bus-width", 4);
    qemu_fdt_setprop(fdt, nodename, "m25p,fast-read", NULL, 0);
    qemu_fdt_setprop_cell(fdt, nodename, "spi-max-frequency", 50000000);
    qemu_fdt_setprop_cell(fdt, nodename, "reg", 0);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "jedec,spi-nor");

    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx",
                               (long)s->qspi2_base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,spi0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, (hwaddr)s->qspi2_base,
                           0x0, memmap[EVALSOC_QSPI2].size);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", s->qspi2_irq);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 0);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx/mmc@0",
                               (long)s->qspi2_base);
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
                               (long)s->uart0_base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,uart0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, s->uart0_base,
                           0x0, memmap[EVALSOC_UART0].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", s->uart0_irq);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", uart_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", nodename);
    g_free(nodename);

    uart_phandle = phandle++;
    nodename = g_strdup_printf("/soc/serial@%lx",
                               (long)memmap[EVALSOC_UART1].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,uart0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[EVALSOC_UART1].base,
                           0x0, memmap[EVALSOC_UART1].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", s->uart1_irq);
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

static QDict *parse_json_filename(const char *filename, Error **errp)
{
    QObject *options_obj;
    QDict *options;
    GLOBAL_STATE_CODE();

    options_obj = qobject_from_json(filename, errp);
    if (!options_obj) {
        error_prepend(errp, "Could not parse the JSON options: ");
        return NULL;
    }

    options = qobject_to(QDict, options_obj);
    if (!options) {
        qobject_unref(options_obj);
        return NULL;
    }
    return options;
}

static unsigned long string_to_uint64(const char *str)
{
    char *end;
    unsigned long num;

    num = strtoul(str, &end, 0);

    if (end == str) {
        printf("No digits were found:%s\n", str);
        return -1;  
    }

    if (*end == 'G' || *end == 'g') {
        num = num * 1024 * 1024 * 1024;
    } 
    else if(*end == 'M' || *end == 'm') {
        num = num * 1024 * 1024;
    }
    return (unsigned long)num;
}

static void parse_json_config(MachineState *machine)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(machine);

    const char* json_filename = s->soccfg;
    QDict *options_page0 = NULL,*options_page1 = NULL,*options_page2 = NULL;
    const QDictEntry *page0,*page1,*page2;
    GError *err = NULL;
    gchar *content = NULL;
    gsize len;
    Error *local_err = NULL;

    if(json_filename != NULL)
    {
        if (!g_file_get_contents(json_filename, &content, &len, &err))
        {
            printf("Unable to read json file %s\n", json_filename);
        }
        else
        {
            options_page0 = parse_json_filename(content, &local_err);
            if(options_page0 != NULL)
            {
                for (page0 = qdict_first(options_page0); page0; page0 = qdict_next(options_page0, page0))
                {    
                    options_page1 = qobject_to(QDict, page0->value);
                    if(options_page1 != NULL && !strcmp(page0->key, "general_config"))
                    {
                        for (page1 = qdict_first(options_page1); page1; page1 = qdict_next(options_page1, page1))
                        {
                            options_page2 = qobject_to(QDict, page1->value);
                            if(!strcmp(page1->key, "timer_freq"))//timer_freq
                            {
                                s->timer_freq = string_to_uint64(qstring_get_str(qobject_to(QString, page1->value)));
                            }
                            else if(!strcmp(page1->key, "cpu_freq"))//cpu_freq
                            {
                                s->cpu_freq = string_to_uint64(qstring_get_str(qobject_to(QString, page1->value)));
                            }
                            else if(!strcmp(page1->key, "ddr"))//ddr
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))//ddr base
                                {
                                    s->ddr_base = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                                page2 = qdict_next(options_page2, page2);
                                if(!strcmp(page2->key, "size"))//ddr size
                                {
                                    s->ddr_size = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                            }
                            else if(!strcmp(page1->key, "norflash"))//norflash
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))//norflash base
                                {
                                    s->norflash_base = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                                page2 = qdict_next(options_page2, page2);
                                if(!strcmp(page2->key, "size"))//norflash size
                                {
                                    s->norflash_size = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                            }
                            else if(!strcmp(page1->key, "iregion"))//iregion
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))
                                {  
                                     s->iregion = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));

                                }
                            }
                            else if(!strcmp(page1->key, "uart0"))//uart0
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))//uart0 base
                                {
                                    s->uart0_base = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                                page2 = qdict_next(options_page2, page2);
                                if(!strcmp(page2->key, "irq"))//uart0 irq
                                {
                                    s->uart0_irq = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                            }
                            else if(!strcmp(page1->key, "uart1"))//uart1
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))//uart1 base
                                {
                                    s->uart1_base = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                                page2 = qdict_next(options_page2, page2);
                                if(!strcmp(page2->key, "irq"))//uart1 irq
                                {
                                    s->uart1_irq = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                            }
                            else if(!strcmp(page1->key, "qspi0"))//qspi0
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))//qspi0 base
                                {
                                    s->qspi0_base = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                                page2 = qdict_next(options_page2, page2);
                                if(!strcmp(page2->key, "irq"))//qspi0 irq
                                {
                                    s->qspi0_irq = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                            }
                            else if(!strcmp(page1->key, "qspi1"))//qspi1
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))//qspi1 base
                                {
                                    s->qspi1_base = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                                page2 = qdict_next(options_page2, page2);
                                if(!strcmp(page2->key, "irq"))//qspi1 irq
                                {
                                    s->qspi1_irq = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                            }
                            else if(!strcmp(page1->key, "qspi2"))//qspi2
                            {
                                page2 = qdict_first(options_page2);
                                if(!strcmp(page2->key, "base"))//qspi2 base
                                {
                                    s->qspi2_base = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                                page2 = qdict_next(options_page2, page2);
                                if(!strcmp(page2->key, "irq"))//qspi2 irq
                                {
                                    s->qspi2_irq = string_to_uint64(qstring_get_str(qobject_to(QString, page2->value)));
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                printf("Format error for json %s\n", json_filename);
            }
        }
    }
}

static bool is_iregion_addr_overlap(const struct MemmapEntry *memmap, EvalSoCState *s)
{
    struct MemmapEntry *memoryRegion = g_new0(struct MemmapEntry, EVALSOC_DEV_END);
    memcpy(memoryRegion, evalsoc_memmap, sizeof(struct MemmapEntry) * EVALSOC_DEV_END);
    //json config
    memoryRegion[EVALSOC_DDR].base = s->ddr_base;
    memoryRegion[EVALSOC_DDR].size = s->ddr_size;
    memoryRegion[EVALSOC_XIP].base = s->norflash_base;
    memoryRegion[EVALSOC_XIP].size = s->norflash_size;
    memoryRegion[EVALSOC_UART0].base = s->uart0_base;
    memoryRegion[EVALSOC_UART1].base = s->uart1_base;
    memoryRegion[EVALSOC_QSPI0].base = s->qspi0_base;
    memoryRegion[EVALSOC_QSPI1].base = s->qspi1_base;
    memoryRegion[EVALSOC_QSPI2].base = s->qspi2_base;
    //iregion offset
    memoryRegion[EVALSOC_DEBUG].base = memmap[EVALSOC_DEBUG].base + s->iregion;
    memoryRegion[EVALSOC_TIMER].base = memmap[EVALSOC_TIMER].base + s->iregion;
    memoryRegion[EVALSOC_PLIC].base = memmap[EVALSOC_PLIC].base + s->iregion;
    memoryRegion[EVALSOC_ECLIC].base = memmap[EVALSOC_ECLIC].base + s->iregion;
    memoryRegion[EVALSOC_SMP].base = memmap[EVALSOC_SMP].base + s->iregion;
    memoryRegion[EVALSOC_CLINT].base = memmap[EVALSOC_CLINT].base + s->iregion;

    for (size_t i = 0; i < EVALSOC_DEV_END; ++i) {
        if (i == EVALSOC_CLINT) continue;
        hwaddr start1 = memoryRegion[i].base;
        hwaddr end1 = start1 + memoryRegion[i].size;
  
        for (size_t j = 0; j < EVALSOC_DEV_END; ++j) {
            if (i == j || j == EVALSOC_CLINT) continue; // Skip comparing with itself
  
            hwaddr start2 = memoryRegion[j].base;
            hwaddr end2 = start2 + memoryRegion[j].size;
  
            // If there is overlap, return true
            if (!(end1 <= start2 || end2 <= start1)) {
                printf("memory is overlap, [%lx:%lx] and [%lx:%lx]\n", (long)start1, (long)end1, (long)start2, (long)end2);
                return true;
            }
        }
    }
    g_free(memoryRegion);

    // If no overlap is found, return false
    return false;
}

static void evalsoc_machine_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = evalsoc_memmap;
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(machine);
    target_ulong start_addr;
    MemoryRegion *system_memory = get_system_memory();
    uint32_t start_addr_hi32 = 0x00000000;
    uint32_t fdt_load_addr = 0;
    uint64_t kernel_entry = 0;
    target_ulong firmware_end_addr, kernel_start_addr;
    int i;
    DriveInfo *dinfo;
    BlockBackend *blk;
    DeviceState *flash_dev, *sd_dev, *card_dev;
    qemu_irq flash_cs, sd_cs;

    parse_json_config(machine);

    if(s->timer_freq == -1)
    {
        s->timer_freq = EVALSOC_TIMEBASE_FREQ;
    }

    if(s->iregion == -1)
    {
        s->iregion = IREGION_BASE_ADDR;
    }

    if(s->ddr_base == -1)
    {
        s->ddr_base = memmap[EVALSOC_DDR].base;
    }

    start_addr = s->ddr_base;

    if(s->ddr_size == -1)
    {
        s->ddr_size = memmap[EVALSOC_DDR].size;
    }

    if(s->norflash_base == -1)
    {
        s->norflash_base = memmap[EVALSOC_XIP].base;
    }

    if(s->norflash_size == -1)
    {
        s->norflash_size = memmap[EVALSOC_XIP].size;
    }

    if(s->uart0_base == -1)
    {
        s->uart0_base = memmap[EVALSOC_UART0].base;
    }

    if(s->uart0_irq == -1)
    {
        if (machine->firmware == NULL)
        {
            s->uart0_irq = UART0_IRQn;
        }
        else
        {
            s->uart0_irq = EVALSOC_PLIC_UART0_IRQ;
        }
    }

    if(s->uart1_base == -1)
    {
        s->uart1_base = memmap[EVALSOC_UART1].base;
    }

    if(s->uart1_irq == -1)
    {
        if (machine->firmware == NULL)
        {
            s->uart1_irq = UART1_IRQn;
        }
        else
        {
            s->uart1_irq = EVALSOC_PLIC_UART1_IRQ;
        }
    }

    if(s->qspi0_base == -1)
    {
        s->qspi0_base = memmap[EVALSOC_QSPI0].base;
    }

    if(s->qspi0_irq == -1)
    {
        if (machine->firmware == NULL)
        {
            s->qspi0_irq = QSPI0_IRQn;
        }
        else
        {
            s->qspi0_irq = EVALSOC_PLIC_SPI0_IRQ;
        }
    }

    if(s->qspi1_base == -1)
    {
        s->qspi1_base = memmap[EVALSOC_QSPI1].base;
    }

    if(s->qspi1_irq == -1)
    {
        if (machine->firmware == NULL)
        {
            s->qspi1_irq = QSPI1_IRQn;
        }
        else
        {
            s->qspi1_irq = EVALSOC_PLIC_SPI1_IRQ;
        }
    }

    if(s->qspi2_base == -1)
    {
        s->qspi2_base = memmap[EVALSOC_QSPI2].base;
    }

    if(s->qspi2_irq == -1)
    {
        if (machine->firmware == NULL)
        {
            s->qspi2_irq = QSPI2_IRQn;
        }
        else
        {
            s->qspi2_irq = EVALSOC_PLIC_SPI2_IRQ;
        }
    }

    if(is_iregion_addr_overlap(memmap, s) == true)
    {
        error_report("is_iregion_addr_overlap() failed");
        exit(1);
    }
    /* TODO: Add qtest support */
    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_EVALSOC_SOC);
    object_property_set_uint(OBJECT(&s->soc), "serial", s->serial,
                             &error_abort);

    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    memory_region_init_ram(&s->soc.ddr, NULL, "riscv.evalsoc.ram.ddr",
                           s->ddr_size, &error_fatal);
    memory_region_add_subregion(system_memory, s->ddr_base,
                                &s->soc.ddr);

    memory_region_init_ram(&s->soc.xip_mem, NULL, "riscv.evalsoc.flashxip",
        s->norflash_size, &error_fatal);
    memory_region_add_subregion(system_memory, 
        s->norflash_base, &s->soc.xip_mem);

    for (i = 0; i < machine->smp.cpus; i ++) {
        s->soc.cpus.harts[i].env.msmpcfg_info = ((memmap[EVALSOC_SMP].base + s->iregion) & ~(1<<10)) | 0xF;
    }

    //iregion
    for (i = 0; i < machine->smp.cpus; i ++) {
        if(machine->smp.cpus > 1)
        {
            s->soc.cpus.harts[i].env.mcfg_info |= 1 << 11;
        }
        // note: The iregion function is optional and cannot be forced to be set.
        s->soc.cpus.harts[i].env.mcfg_info |= 1 << 16;
        s->soc.cpus.harts[i].env.mirgb_info = (s->iregion & ~(1<<10)) | 0xF;;
    }
    /* load/create device tree */
    if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    } else {
        create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);
    }

    if (s->download == NULL) {
        start_addr = s->norflash_base;
    } else if (!strcmp(s->download, "ilm")) {
        start_addr = EVALSOC_ILM_ADDR;
    } else if(!strcmp(s->download, "ddr")) {
        // For cpu release after 2023.06, the DDR base changed from 0xA0000000 to 0x80000000
        // But we want to keep DOWNLOAD=ddr still use old 0xA0000000 base
        start_addr = EVALSOC_DDR_MODE_ADDR;
    } else if (!strcmp(s->download, "sram")) { // sram mode = ddr mode base address
        start_addr = EVALSOC_DDR_MODE_ADDR;
    } else {
        start_addr = s->norflash_base;
    }

    if (machine->firmware) {
        firmware_end_addr = riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                                     start_addr, NULL);
    } else {
        firmware_end_addr = (target_ulong)(-1);
    }

    if (machine->firmware == NULL)
    {
        if (firmware_end_addr != (target_ulong)(-1)) {
            kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc.cpus,
                                                         firmware_end_addr);
        } else {
            kernel_start_addr = start_addr;
        }

        if(machine->kernel_filename)
        {
            kernel_entry = riscv_load_kernel(machine, &s->soc.cpus,
                                         kernel_start_addr, true, NULL);
        }

        if (machine->initrd_filename) {
            riscv_load_initrd(machine, kernel_entry);
        }

    }
    else
    {
        /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;

        fdt_load_addr = riscv_compute_fdt_addr(s->ddr_base,
                                            machine->ram_size,
                                           machine);
        riscv_load_fdt(fdt_load_addr, machine->fdt);
    }

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
                          memmap[EVALSOC_MROM].base, &address_space_memory);

    riscv_rom_copy_firmware_info(machine, memmap[EVALSOC_MROM].base,
                                 memmap[EVALSOC_MROM].size,
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

    bool is_32_bit = riscv_is_32bit(&s->soc.cpus);

    nuclei_iregion_create(memmap[EVALSOC_IINFO].base + s->iregion, is_32_bit);
}

static void evalsoc_machine_instance_init(Object *obj)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);

    s->cpu_freq = -1;
    s->timer_freq = -1;
    s->iregion = -1;
    s->ddr_base = -1;
    s->ddr_size = -1;
    s->norflash_base = -1;
    s->norflash_size = -1;
    s->uart0_base = -1;
    s->uart0_irq = -1;
    s->uart1_base = -1;
    s->uart1_irq = -1;
    s->qspi0_base = -1;
    s->qspi0_irq = -1;
    s->qspi1_base = -1;
    s->qspi1_irq = -1;
    s->qspi2_base = -1;
    s->qspi2_irq = -1;

    object_property_add_uint64_ptr(obj, "iregion", &s->iregion,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "iregion",
                                    "Set iregion");
}

static char* evalsoc_machine_get_download(Object *obj, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);
    return g_strdup(s->download);
}

static void evalsoc_machine_set_download(Object *obj, const char *value, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);
    s->download = g_strdup(value);
}

static char* evalsoc_machine_get_soccfg(Object *obj, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);
    return g_strdup(s->soccfg);
}

static void evalsoc_machine_set_soccfg(Object *obj, const char *value, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);
    s->soccfg = g_strdup(value);
}

static void evalsoc_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nuclei RISC-V EvalSoC, support Nuclei RISC-V 200/300/600/900 series processors";
    mc->init = evalsoc_machine_init;
    mc->max_cpus = 16;
    mc->min_cpus = 1;
    mc->default_cpu_type = EVALSOC_CPU;
    mc->default_cpus = mc->min_cpus;

    object_class_property_add_str(oc, "soc-cfg",                                    
                                   evalsoc_machine_get_soccfg,
                                   evalsoc_machine_set_soccfg);
    object_class_property_set_description(oc, "soc-cfg",
                                    "load json config");

    object_class_property_add_str(oc, "download",
                                   evalsoc_machine_get_download,
                                   evalsoc_machine_set_download);
    object_class_property_set_description(oc, "download",
                                          "Set on to tell QEMU's ROM to jump to "
                                          "download mode. Otherwise QEMU will jump to flash base address, aka download=flashxip"
                                          "nuclei support these download modes(flashxip,flash,ilm,ddr,sram)");
}

static const TypeInfo evalsoc_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("nuclei_evalsoc"),
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



static void riscv_evalsoc_soc_init(Object *obj)
{
    EvalSoCSoCState *s = RISCV_EVALSOC_SOC(obj);

    object_initialize_child(obj, "u-cluster", &s->u_cluster, TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->u_cluster), "cluster-id", 1);

    object_initialize_child(OBJECT(&s->u_cluster), "cpus", &s->cpus,
                            TYPE_RISCV_HART_ARRAY);

    object_initialize_child(obj, "gpio", &s->gpio, TYPE_SIFIVE_GPIO);
    object_initialize_child(obj, "spi0", &s->spi0, TYPE_SIFIVE_SPI);
    object_initialize_child(obj, "spi2", &s->spi2, TYPE_SIFIVE_SPI);
    object_initialize_child(obj, "timer", &s->timer, TYPE_NUCLEI_SYSTIMER);
}

static void riscv_evalsoc_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    EvalSoCState *mst = RISCV_EVALSOC_MACHINE(ms);
    EvalSoCSoCState *s = RISCV_EVALSOC_SOC(dev);
    const struct MemmapEntry *memmap = evalsoc_memmap;
    MemoryRegion *sys_mem = get_system_memory();
    NucLeiSMPCCInit smpcc_cfg = {EVALSOC_SMP_VER,
                                EVALSOC_SMP_CFG | ((ms->smp.cpus - 1) << 1),
                                EVALSOC_CC_CFG,
                                EVALSOC_CLM_BASE_ADDR,
                                EVALSOC_CLUSTER_CACHE_SIZE,
                                EVALSOC_CLM_WAY_EN
                                };

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
    memory_region_init_rom(&s->internal_rom, OBJECT(dev), "riscv.evalsoc.irom",
                           memmap[EVALSOC_MROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[EVALSOC_MROM].base, &s->internal_rom);


    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(EVALSOC_PLIC_HART_CONFIG) + 1) *
                           ms->smp.cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < ms->smp.cpus; i++)
    {
        if (i != 0) {
            strncat(plic_hart_config, ",", plic_hart_config_len);
        }
        strncat(plic_hart_config, EVALSOC_PLIC_HART_CONFIG,
                plic_hart_config_len);
        plic_hart_config_len -= (strlen(EVALSOC_PLIC_HART_CONFIG) + 1);
    }
    /* MMIO */
    s->plic = sifive_plic_create(memmap[EVALSOC_PLIC].base + mst->iregion,
                                 plic_hart_config, ms->smp.cpus, 0,
                                 EVALSOC_PLIC_NUM_SOURCES,
                                 EVALSOC_PLIC_NUM_PRIORITIES,
                                 EVALSOC_PLIC_PRIORITY_BASE,
                                 EVALSOC_PLIC_PENDING_BASE,
                                 EVALSOC_PLIC_ENABLE_BASE,
                                 EVALSOC_PLIC_ENABLE_STRIDE,
                                 EVALSOC_PLIC_CONTEXT_BASE,
                                 EVALSOC_PLIC_CONTEXT_STRIDE,
                                 memmap[EVALSOC_PLIC].size);
    g_free(plic_hart_config);

    s->eclic = nuclei_eclic_create(memmap[EVALSOC_ECLIC].base + mst->iregion,
                                   memmap[EVALSOC_ECLIC].size,
                                   false, false, true,
                                   ms->smp.cpus,
                                   EVALSOC_ECLIC_NUM_SOURCES,
                                   EVALSOC_CLIC_INTCTLBITS);

    s->smpcc = nuclei_smpcc_create(memmap[EVALSOC_SMP].base + mst->iregion,
                               memmap[EVALSOC_SMP].size,
                               &smpcc_cfg);

    if (ms->firmware == NULL)
    {
        /* Create and connect UART interrupts to the ECLIC */
        nuclei_uart_create(sys_mem,
                        mst->uart0_base,
                        memmap[EVALSOC_UART0].size,
                        serial_hd(0),
                        nuclei_eclic_get_irq(DEVICE(s->eclic),
                        mst->uart0_irq, ms->smp.cpus));
        
        nuclei_systimer_create(memmap[EVALSOC_TIMER].base + mst->iregion,
                memmap[EVALSOC_TIMER].size, false, 0, ms->smp.cpus, s->eclic, mst->timer_freq);
    }
    else
    {
        sifive_uart_create(sys_mem, mst->uart0_base,
                       serial_hd(0), qdev_get_gpio_in(DEVICE(s->plic), mst->uart0_irq));

        sifive_uart_create(sys_mem, mst->uart1_base,
                        serial_hd(1), qdev_get_gpio_in(DEVICE(s->plic), mst->uart1_irq));

        nuclei_systimer_create(memmap[EVALSOC_TIMER].base + mst->iregion,
                memmap[EVALSOC_TIMER].size, false, 0, ms->smp.cpus, NULL, mst->timer_freq);
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
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[EVALSOC_GPIO].base);

    /* Pass all GPIOs to the SOC layer so they are available to the board */
    qdev_pass_gpios(DEVICE(&s->gpio), dev, NULL);

    /* Connect GPIO interrupts to the PLIC */
    for (i = 0; i < 32; i++)
    {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), i,
                           qdev_get_gpio_in(DEVICE(s->plic),
                                            EVALSOC_PLIC_GPIO_IRQ0 + i));
    }

    sysbus_realize(SYS_BUS_DEVICE(&s->spi0), errp);

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi0), 0,
                    mst->qspi0_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi0), 0,
                       qdev_get_gpio_in(DEVICE(s->plic), mst->qspi0_irq));

    sysbus_realize(SYS_BUS_DEVICE(&s->spi2), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi2), 0,
                    mst->qspi2_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi2), 0,
                       qdev_get_gpio_in(DEVICE(s->plic), mst->qspi2_irq));

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[EVALSOC_TEST].base);
}

static Property evalsoc_soc_props[] = {
    DEFINE_PROP_UINT32("serial", EvalSoCSoCState, serial, OTP_SERIAL),
    DEFINE_PROP_STRING("cpu-type", EvalSoCSoCState, cpu_type),
    DEFINE_PROP_END_OF_LIST()
};

static void riscv_evalsoc_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_props(dc, evalsoc_soc_props);
    dc->realize = riscv_evalsoc_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo riscv_evalsoc_soc_type_info = {
    .name = TYPE_EVALSOC_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(EvalSoCSoCState),
    .instance_init = riscv_evalsoc_soc_init,
    .class_init = riscv_evalsoc_soc_class_init,
};

static void riscv_evalsoc_soc_register_types(void)
{
    type_register_static(&riscv_evalsoc_soc_type_info);
}

type_init(riscv_evalsoc_soc_register_types)
