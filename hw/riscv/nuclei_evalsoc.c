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
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/nuclei_test.h"
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
#include "elf.h"

#define OTP_SERIAL 1

uint32_t debug_flag = 0;

#define DEBUGF(fmt, args...)    {if(debug_flag)printf(fmt ,##args);}

#if defined(TARGET_RISCV32)
#define BIOS_FILENAME "opensbi-riscv32-generic-fw_dynamic.bin"
#else
#define BIOS_FILENAME "opensbi-riscv64-generic-fw_dynamic.bin"
#endif

#define IREGION_BASE_ADDR   (0x18000000)
#define IREGION_MAX_SIZE    (0x8000000)
#define IREGION_MIN_SIZE    (0x400000)

static const struct MemmapEntry
{
    hwaddr base;
    hwaddr size;
    const char *name;
}  evalsoc_memmap[] = {
    [EVALSOC_IINFO]   = { IREGION_IINFO_OFS,              IREGION_IINFO_SIZE,   "IINFO"},
    [EVALSOC_MROM]    = { EVALSOC_MROM_BASE,              EVALSOC_MROM_SIZE,    "MROM" },
    [EVALSOC_TEST]    = { EVALSOC_TEST_BASE,              EVALSOC_TEST_SIZE,    "TEST" },
    [EVALSOC_GPIO]    = { EVALSOC_GPIO_BASE,              EVALSOC_GPIO_SIZE,    "GPIO" },
    [EVALSOC_UART0]   = { EVALSOC_UART0_BASE,             EVALSOC_UART0_SIZE,   "UART0"},
    [EVALSOC_UART1]   = { EVALSOC_UART1_BASE,             EVALSOC_UART1_SIZE,   "UART1"},
    [EVALSOC_QSPI0]   = { EVALSOC_QSPI0_BASE,             EVALSOC_QSPI0_SIZE,   "QSPI0"},
    [EVALSOC_QSPI1]   = { EVALSOC_QSPI1_BASE,             EVALSOC_QSPI1_SIZE,   "QSPI1"},
    [EVALSOC_QSPI2]   = { EVALSOC_QSPI2_BASE,             EVALSOC_QSPI2_SIZE,   "QSPI2"},
    [EVALSOC_XIP]     = { EVALSOC_XIP_BASE,               EVALSOC_XIP_SIZE,     "XIP"  },
    [EVALSOC_DEBUG]   = { IREGION_DEBUG_OFS,              IREGION_DEBUG_SIZE,   "DEBUG"},
    [EVALSOC_TIMER]   = { IREGION_TIMER_OFS,              IREGION_TIMER_SIZE,   "TIMER"},
    [EVALSOC_PLIC]    = { IREGION_PLIC_OFS,               IREGION_PLIC_SIZE,    "PLIC" },
    [EVALSOC_APLIC_M] = { EVALSOC_APLIC_M_BASE,           EVALSOC_APLIC_M_SIZE, "APLIC_M"},
    [EVALSOC_APLIC_S] = { EVALSOC_APLIC_S_BASE,           EVALSOC_APLIC_S_SIZE, "APLIC_S"},
    [EVALSOC_IMSIC_M] = { EVALSOC_IMSIC_M_BASE,           EVALSOC_IMSIC_MINTF_SIZE, "IMSIC_M"},
    [EVALSOC_IMSIC_S] = { EVALSOC_IMSIC_S_BASE,           EVALSOC_IMSIC_SINTF_SIZE, "IMSIC_S"},
    [EVALSOC_ECLIC]   = { IREGION_ECLIC_OFS,              IREGION_ECLIC_SIZE,   "ECLIC"},
    [EVALSOC_CIDU]    = { IREGION_IDU_OFS,                IREGION_IDU_SIZE,     "CIDU"},
    [EVALSOC_SMP]     = { IREGION_SMP_OFS,                IREGION_SMP_SIZE,     "SMP"},
    [EVALSOC_DDR]     = { EVALSOC_DDR_BASE,               EVALSOC_DDR_SIZE,     "DDR"},
    [EVALSOC_ILM]     = { EVALSOC_ILM_BASE,               EVALSOC_ILM_SIZE,     "ILM"},
    [EVALSOC_DLM]     = { EVALSOC_DLM_BASE,               EVALSOC_DLM_SIZE,     "DLM"},
    [EVALSOC_SRAM]    = { EVALSOC_SRAM_BASE,              EVALSOC_SRAM_SIZE,    "CLINT"},
    [EVALSOC_CLINT]   = { IREGION_TIMER_OFS + 0x1000,     0xF000 },//MTIME in CLINT mode
};

static bool evalsoc_has_eclic(const EvalSoCState *s)
{
    return s->iregion.eclic_en;
}

static bool evalsoc_has_plic(const EvalSoCState *s)
{
    return (s->aia_type == EVALSOC_AIA_TYPE_NONE) && s->iregion.plic_en;
}

static target_ulong evalsoc_compose_mcfg_info(const EvalSoCState *s,
                                              target_ulong mcfg_info)
{
    mcfg_info |= EVALSOC_MCFG_INFO;
    if (evalsoc_has_eclic(s)) {
        mcfg_info |= EVALSOC_MCFG_INFO_ECLIC;
    }
    if (evalsoc_has_plic(s)) {
        mcfg_info |= EVALSOC_MCFG_INFO_PLIC;
    }

    return mcfg_info;
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
    uint32_t aplic_m_phandle, aplic_s_phandle;
    uint32_t msi_m_phandle, msi_s_phandle;
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
        (hwaddr)s->ddr.base >> 32, (hwaddr)s->ddr.base,
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
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv39");
        riscv_isa_write_fdt(&s->soc.cpus.harts[cpu], fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", cpu_phandle);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
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
                               (long)((long)memmap[EVALSOC_CLINT].base + s->iregion.base));
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    // qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
    //                  cells, ms->smp.cpus * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/timer@%lx",
                               (long)((long)memmap[EVALSOC_TIMER].base + s->iregion.base));
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,timer0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[EVALSOC_TIMER].base + s->iregion.base,
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

    // M-level IMSIC node
    msi_m_phandle = phandle++;
    nodename = g_strdup_printf("/soc/imsics@%lx",
                                (long)memmap[EVALSOC_IMSIC_M].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,imsics");
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 0);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "msi-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
                    cells, ms->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                        0x0, memmap[EVALSOC_IMSIC_M].base,
                        0x0, memmap[EVALSOC_IMSIC_M].size * ms->smp.cpus);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,num-ids",
                            EVALSOC_IRQCHIP_NUM_MSIS);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", msi_m_phandle);
    g_free(nodename);

    // S-level IMSIC node
    msi_s_phandle = phandle++;
    nodename = g_strdup_printf("/soc/imsics@%lx",
                                (long)memmap[EVALSOC_IMSIC_S].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,imsics");
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 0);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "msi-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
                    cells, ms->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                        0x0, memmap[EVALSOC_IMSIC_S].base,
                        0x0, memmap[EVALSOC_IMSIC_S].size * ms->smp.cpus * (1 + s->aia_guests));
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,num-ids",
                            EVALSOC_IRQCHIP_NUM_MSIS);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,guest-index-bits",
                            EVALSOC_IRQCHIP_GUEST_INDEX_BITS);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", msi_s_phandle);
    g_free(nodename);

    plic_phandle = phandle++;
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
                               (long)((long)memmap[EVALSOC_PLIC].base + s->iregion.base));
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
                     cells, (ms->smp.cpus * 4 ) * sizeof(uint32_t));
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, memmap[EVALSOC_PLIC].base + s->iregion.base,
                           0x0, memmap[EVALSOC_PLIC].size);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", 0x35);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", plic_phandle);
    g_free(cells);
    g_free(nodename);

    aplic_m_phandle = phandle++;
    aplic_s_phandle = phandle++;
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
                            (long)memmap[EVALSOC_APLIC_M].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 2);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
                     cells, (ms->smp.cpus * 4 ) * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                            0x0, memmap[EVALSOC_APLIC_M].base,
                            0x0, memmap[EVALSOC_APLIC_M].size);
    // qemu_fdt_setprop_cell(fdt, nodename, "riscv,num-sources",
    //     VIRT_IRQCHIP_NUM_SOURCES);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,children",
        aplic_s_phandle);
    // qemu_fdt_setprop_cells(fdt, nodename, "riscv,delegate",
    //     aplic_s_phandle, 0x1, VIRT_IRQCHIP_NUM_SOURCES);
    // riscv_socket_fdt_write_id(ms, fdt, nodename, socket);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", aplic_m_phandle);
    g_free(nodename);

    // aplic_s_phandle = phandle++;
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
                                (long)memmap[EVALSOC_APLIC_S].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 2);
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
                     cells, (ms->smp.cpus * 4 ) * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                            0x0, memmap[EVALSOC_APLIC_S].base,
                            0x0, memmap[EVALSOC_APLIC_S].size);
    // qemu_fdt_setprop_cell(fdt, nodename, "riscv,num-sources",
    //     VIRT_IRQCHIP_NUM_SOURCES);
    // riscv_socket_fdt_write_id(ms, mc->fdt, nodename, socket);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", aplic_s_phandle);
    g_free(nodename);


    test_phandle = phandle++;
    nodename = g_strdup_printf("/soc/test@%lx",
        (long)memmap[EVALSOC_TEST].base);
    qemu_fdt_add_subnode(fdt, nodename);
    {
        static const char * const compat[3] = {
            "nuclei,test1", "nuclei,test0", "syscon"
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
                               (long)s->qspi0.base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,spi0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, (hwaddr)s->qspi0.base,
                           0x0, memmap[EVALSOC_QSPI0].size,
                           0x0, 0x20000000,
                           0x0, 0x10000000);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", s->qspi0.irq);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 0);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx/flash@0",
                               (long)s->qspi0.base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "spi-rx-bus-width", 4);
    qemu_fdt_setprop_cell(fdt, nodename, "spi-tx-bus-width", 4);
    qemu_fdt_setprop(fdt, nodename, "m25p,fast-read", NULL, 0);
    qemu_fdt_setprop_cell(fdt, nodename, "spi-max-frequency", 50000000);
    qemu_fdt_setprop_cell(fdt, nodename, "reg", 0);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "jedec,spi-nor");

    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx",
                               (long)s->qspi2.base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,spi0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, (hwaddr)s->qspi2.base,
                           0x0, memmap[EVALSOC_QSPI2].size);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cells(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", s->qspi2.irq);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 0);
    qemu_fdt_setprop_string(fdt, nodename, "status", "disabled");
    g_free(nodename);

    nodename = g_strdup_printf("/soc/spi@%lx/mmc@0",
                               (long)s->qspi2.base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "mmc-spi-slot");
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0x0);
    qemu_fdt_setprop_cells(fdt, nodename, "spi-max-frequency", 20000000);
    qemu_fdt_setprop_cells(fdt, nodename, "voltage-ranges", 3300, 3300);
    qemu_fdt_setprop_cells(fdt, nodename, "disable-wp", 0);
    g_free(nodename);

    uart_phandle = phandle++;
    qemu_fdt_add_subnode(fdt, "/aliases");
    nodename = g_strdup_printf("/soc/serial@%lx",
                               (long)s->uart0.base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "nuclei,uart0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
                           0x0, s->uart0.base,
                           0x0, memmap[EVALSOC_UART0].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clocks", hfclk_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", s->uart0.irq);
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
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", s->uart1.irq);
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

static uint64_t riscv_imsic_dummy_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}

static void riscv_imsic_dummy_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
}

static const MemoryRegionOps riscv_imsic_dummy_ops = {
    .read = riscv_imsic_dummy_read,
    .write = riscv_imsic_dummy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {},
};

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

static uint32_t get_irq_number_alignment(uint64_t n)
{
    uint32_t result = 1;
    g_assert(n <= EVALSOC_ECLIC_NUM_SOURCES);
    if (n < 16) {
        return 16;
    }
    while (result < n) {
        result <<= 1;
    }
    return (n == result) ? n : result;
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

static void parse_json_keys_and_values(QDict *qdict, const JsonFieldMapping *key_map,
                                        uint8_t num_keys)
{
    if (!qdict || !key_map || (num_keys == 0))
        return;

    for (size_t i = 0; i < num_keys; i++) {
        const JsonFieldMapping *mapping = &key_map[i];
        QObject *obj = qdict_get(qdict, mapping->key);
        if (!obj) {
            continue;
        }
        const char *str_val = qstring_get_str(qobject_to(QString, obj));
        if (!g_strcmp0(str_val, "")) {
            continue;
        }
        *(uint64_t *)mapping->field_ptr = string_to_uint64(str_val);
    }
}


static void parse_json_config(MachineState *machine)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(machine);

    const char* json_filename = s->soccfg;
    QDict *options_page0 = NULL,*options_page1 = NULL,*options_page2 = NULL;
    const QDictEntry *page0,*page1;
    GError *err = NULL;
    gchar *content = NULL;
    gsize len;
    Error *local_err = NULL;

    const JsonFieldMapping ddr_mappings[] = {
        {"base", &s->ddr.base, "ddr.base"},
        {"size", &s->ddr.size, "ddr.size"},
    };
    const JsonFieldMapping ilm_mappings[] = {
        {"base", &s->ilm.base, "ilm.base"},
        {"size", &s->ilm.size, "ilm.size"},
    };
    const JsonFieldMapping dlm_mappings[] = {
        {"base", &s->dlm.base, "dlm.base"},
        {"size", &s->dlm.size, "dlm.size"},
    };
    const JsonFieldMapping sram_mappings[] = {
        {"base", &s->sram.base, "sram.base"},
        {"size", &s->sram.size, "sram.size"},
    };
    const JsonFieldMapping norflash_mappings[] = {
        {"base", &s->norflash.base, "norflash.base"},
        {"size", &s->norflash.size, "norflash.size"},
    };

    // Peripherals mapping
    const JsonFieldMapping iregion_mappings[] = {
        {"base",  &s->iregion.base,     "iregion.base"},
        {"size",  &s->iregion.size,     "iregion.size"},
        {"debug", &s->iregion.debug_en, "iregion.debug"},
        {"eclic", &s->iregion.eclic_en, "iregion.eclic"},
        {"smpcc", &s->iregion.smpcc_en, "iregion.smpcc"},
        {"cidu",  &s->iregion.cidu_en,  "iregion.cidu"},
        {"plic",  &s->iregion.plic_en,  "iregion.plic"},
    };
    const JsonFieldMapping mrom_mappings[] = {
        {"base",    &s->mrom.base,     "mrom.base"},
        {"size",    &s->mrom.size,     "mrom.size"},
    };
    const JsonFieldMapping test_mappings[] = {
        {"base",    &s->test.base,     "test.base"},
        {"size",    &s->test.size,     "test.size"},
        {"enable",  &s->test.enable,   "test.enable"},
    };
    const JsonFieldMapping gpio_mappings[] = {
        {"base",    &s->gpio.base,     "gpio.base"},
        {"size",    &s->gpio.size,     "gpio.size"},
        {"enable",  &s->gpio.enable,   "gpio.enable"},
    };
    const JsonFieldMapping uart0_mappings[] = {
        {"base",    &s->uart0.base,     "uart0.base"},
        {"size",    &s->uart0.size,     "uart0.size"},
        {"irq",     &s->uart0.irq,      "uart0.irq"},
        {"enable",  &s->uart0.enable,   "uart0.enable"},
    };
    const JsonFieldMapping uart1_mappings[] = {
        {"base",    &s->uart1.base,     "uart1.base"},
        {"size",    &s->uart1.size,     "uart1.size"},
        {"irq",     &s->uart1.irq,      "uart1.irq"},
        {"enable",  &s->uart1.enable,   "uart1.enable"},
    };
    const JsonFieldMapping qspi0_mappings[] = {
        {"base",    &s->qspi0.base,     "qspi0.base"},
        {"size",    &s->qspi0.size,     "qspi0.size"},
        {"irq",     &s->qspi0.irq,      "qspi0.irq"},
        {"enable",  &s->qspi0.enable,   "qspi0.enable"},
    };
    const JsonFieldMapping qspi1_mappings[] = {
        {"base",    &s->qspi1.base,     "qspi1.base"},
        {"size",    &s->qspi1.size,     "qspi1.size"},
        {"irq",     &s->qspi1.irq,      "qspi1.irq"},
        {"enable",  &s->qspi1.enable,   "qspi1.enable"},
    };
    const JsonFieldMapping qspi2_mappings[] = {
        {"base",    &s->qspi2.base,     "qspi2.base"},
        {"size",    &s->qspi2.size,     "qspi2.size"},
        {"irq",     &s->qspi2.irq,      "qspi2.irq"},
        {"enable",  &s->qspi2.enable,   "qspi2.enable"},
    };

    // Download mapping
    const JsonFieldMapping ilm_download_mappings[] = {
        {"startaddr", &s->ilm.startup_addr, "ilm.startup_addr"},
    };
    const JsonFieldMapping flashxip_download_mappings[] = {
        {"startaddr", &s->norflash.startup_addr, "norflash.startup_addr"},
    };
    const JsonFieldMapping flash_download_mappings[] = {
        {"startaddr", &s->flash.startup_addr, "flash.startup_addr"},
    };
    const JsonFieldMapping sram_download_mappings[] = {
        {"startaddr", &s->sram.startup_addr, "sram.startup_addr"},
    };
    const JsonFieldMapping ddr_download_mappings[] = {
        {"startaddr", &s->ddr.startup_addr, "ddr.startup_addr"},
    };

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
                            if(!strcmp(page1->key, "timer_freq")) {
                                const char *val = qstring_get_str(qobject_to(QString, page1->value));
                                if (g_strcmp0(val, "")) s->timer_freq = string_to_uint64(val);
                            } else if(!strcmp(page1->key, "irqmax")) {
                                const char *val = qstring_get_str(qobject_to(QString, page1->value));
                                if (g_strcmp0(val, "")) {
                                    if (string_to_uint64(val) < EVALSOC_PLIC_INT_MAX) {
                                        error_report("irqmax is less than the default supported irq number: %d!", EVALSOC_PLIC_INT_MAX);
                                        exit(1);
                                    }
                                    s->irqmax = string_to_uint64(val);
                                }
                            } else if (!strcmp(page1->key, "cpu_freq")) {
                                const char *val = qstring_get_str(qobject_to(QString, page1->value));
                                if (g_strcmp0(val, "")) s->cpu_freq = string_to_uint64(val);
                            } else if (!strcmp(page1->key, "ddr")) {
                                parse_json_keys_and_values(options_page2, ddr_mappings, ARRAY_SIZE(ddr_mappings));
                            } else if (!strcmp(page1->key, "ilm")) {
                                parse_json_keys_and_values(options_page2, ilm_mappings, ARRAY_SIZE(ilm_mappings));
                            } else if (!strcmp(page1->key, "dlm")) {
                                parse_json_keys_and_values(options_page2, dlm_mappings, ARRAY_SIZE(dlm_mappings));
                            } else if (!strcmp(page1->key, "sram")) {
                                parse_json_keys_and_values(options_page2, sram_mappings, ARRAY_SIZE(sram_mappings));
                            } else if (!strcmp(page1->key, "norflash")) {
                                parse_json_keys_and_values(options_page2, norflash_mappings, ARRAY_SIZE(norflash_mappings));
                            } else if (!strcmp(page1->key, "iregion")) {
                                parse_json_keys_and_values(options_page2, iregion_mappings, ARRAY_SIZE(iregion_mappings));
                                s->iregion.size = (s->iregion.plic_en) ? IREGION_MAX_SIZE : IREGION_MIN_SIZE;
                            } else if (!strcmp(page1->key, "mrom")) {
                                parse_json_keys_and_values(options_page2, mrom_mappings, ARRAY_SIZE(mrom_mappings));
                            } else if (!strcmp(page1->key, "test")) {
                                parse_json_keys_and_values(options_page2, test_mappings, ARRAY_SIZE(test_mappings));
                            } else if (!strcmp(page1->key, "gpio")) {
                                parse_json_keys_and_values(options_page2, gpio_mappings, ARRAY_SIZE(gpio_mappings));
                            } else if (!strcmp(page1->key, "uart0")) {
                                parse_json_keys_and_values(options_page2, uart0_mappings, ARRAY_SIZE(uart0_mappings));
                            } else if (!strcmp(page1->key, "uart1")) {
                                parse_json_keys_and_values(options_page2, uart1_mappings, ARRAY_SIZE(uart1_mappings));
                            } else if (!strcmp(page1->key, "qspi0")) {
                                parse_json_keys_and_values(options_page2, qspi0_mappings, ARRAY_SIZE(qspi0_mappings));
                            } else if (!strcmp(page1->key, "qspi1")) {
                                parse_json_keys_and_values(options_page2, qspi1_mappings, ARRAY_SIZE(qspi1_mappings));
                            } else if (!strcmp(page1->key, "qspi2")) {
                                parse_json_keys_and_values(options_page2, qspi2_mappings, ARRAY_SIZE(qspi2_mappings));
                            }
                        }
                    }
                    else if(options_page1 != NULL && !strcmp(page0->key, "download"))
                    {
                        //"evalsoc": ("ilm", "flash", "flashxip", "ddr", "sram")
                        for (page1 = qdict_first(options_page1); page1; page1 = qdict_next(options_page1, page1))
                        {
                            options_page2 = qobject_to(QDict, page1->value);
                            if (!strcmp(page1->key, "ilm")) {
                                parse_json_keys_and_values(options_page2, ilm_download_mappings, ARRAY_SIZE(ilm_download_mappings));
                            } else if (!strcmp(page1->key, "flashxip")) {
                                parse_json_keys_and_values(options_page2, flashxip_download_mappings, ARRAY_SIZE(flashxip_download_mappings));
                            } else if (!strcmp(page1->key, "flash")) {
                                parse_json_keys_and_values(options_page2, flash_download_mappings, ARRAY_SIZE(flash_download_mappings));
                            } else if (!strcmp(page1->key, "sram")) {
                                parse_json_keys_and_values(options_page2, sram_download_mappings, ARRAY_SIZE(sram_download_mappings));
                            } else if (!strcmp(page1->key, "ddr")) {
                                parse_json_keys_and_values(options_page2, ddr_download_mappings, ARRAY_SIZE(ddr_download_mappings));
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
    memoryRegion[EVALSOC_ILM].base = s->ilm.base;
    memoryRegion[EVALSOC_ILM].size = s->ilm.size;
    memoryRegion[EVALSOC_DLM].base = s->dlm.base;
    memoryRegion[EVALSOC_DLM].size = s->dlm.size;
    memoryRegion[EVALSOC_SRAM].base = s->sram.base;
    memoryRegion[EVALSOC_SRAM].size = s->sram.size;
    memoryRegion[EVALSOC_DDR].base = s->ddr.base;
    memoryRegion[EVALSOC_DDR].size = s->ddr.size;
    memoryRegion[EVALSOC_XIP].base = s->norflash.base;
    memoryRegion[EVALSOC_XIP].size = s->norflash.size;
    memoryRegion[EVALSOC_MROM].base = s->mrom.base;
    memoryRegion[EVALSOC_MROM].size = s->mrom.size;
    memoryRegion[EVALSOC_TEST].base = s->test.base;
    memoryRegion[EVALSOC_TEST].size = s->test.size;
    memoryRegion[EVALSOC_GPIO].base = s->gpio.base;
    memoryRegion[EVALSOC_GPIO].size = s->gpio.size;
    memoryRegion[EVALSOC_UART0].base = s->uart0.base;
    memoryRegion[EVALSOC_UART0].size = s->uart0.size;
    memoryRegion[EVALSOC_UART1].base = s->uart1.base;
    memoryRegion[EVALSOC_UART1].size = s->uart1.size;
    memoryRegion[EVALSOC_QSPI0].base = s->qspi0.base;
    memoryRegion[EVALSOC_QSPI0].size = s->qspi0.size;
    memoryRegion[EVALSOC_QSPI1].base = s->qspi1.base;
    memoryRegion[EVALSOC_QSPI1].size = s->qspi1.size;
    memoryRegion[EVALSOC_QSPI2].base = s->qspi2.base;
    memoryRegion[EVALSOC_QSPI2].size = s->qspi2.size;
    //iregion offset
    memoryRegion[EVALSOC_IINFO].base = memmap[EVALSOC_IINFO].base + s->iregion.base;
    memoryRegion[EVALSOC_DEBUG].base = memmap[EVALSOC_DEBUG].base + s->iregion.base;
    memoryRegion[EVALSOC_TIMER].base = memmap[EVALSOC_TIMER].base + s->iregion.base;
    memoryRegion[EVALSOC_PLIC].base = memmap[EVALSOC_PLIC].base + s->iregion.base;
    memoryRegion[EVALSOC_ECLIC].base = memmap[EVALSOC_ECLIC].base + s->iregion.base;
    memoryRegion[EVALSOC_SMP].base = memmap[EVALSOC_SMP].base + s->iregion.base;

    for (size_t i = 0; i < EVALSOC_DEV_END; ++i) {
        if (i == EVALSOC_CLINT || i == EVALSOC_ILM || i == EVALSOC_DLM || i == EVALSOC_SRAM) continue;

        if (i == EVALSOC_DEBUG
            || (i == EVALSOC_ECLIC && !s->iregion.eclic_en)
            || (i == EVALSOC_SMP && !s->iregion.smpcc_en)
            || (i == EVALSOC_CIDU && !s->iregion.cidu_en)
            || (i == EVALSOC_PLIC && !s->iregion.plic_en)
            || (i == EVALSOC_TEST && !s->test.enable)
            || (i == EVALSOC_GPIO && !s->gpio.enable)
            || (i == EVALSOC_UART0 && !s->uart0.enable)
            || (i == EVALSOC_UART1 && !s->uart1.enable)
            || (i == EVALSOC_QSPI0 && !s->qspi0.enable)
            || (i == EVALSOC_QSPI1 && !s->qspi1.enable)
            || (i == EVALSOC_QSPI2 && !s->qspi2.enable))
            continue;
        hwaddr start1 = memoryRegion[i].base;
        hwaddr end1 = start1 + memoryRegion[i].size;

        for (size_t j = 0; j < EVALSOC_DEV_END; ++j) {
            if (i == j || j == EVALSOC_CLINT || j == EVALSOC_ILM || j == EVALSOC_DLM || j == EVALSOC_SRAM) continue; // Skip comparing with itself
            if (j == EVALSOC_DEBUG
                || (j == EVALSOC_ECLIC && !s->iregion.eclic_en)
                || (j == EVALSOC_SMP && !s->iregion.smpcc_en)
                || (j == EVALSOC_CIDU && !s->iregion.cidu_en)
                || (j == EVALSOC_PLIC && !s->iregion.plic_en)
                || (j == EVALSOC_TEST && !s->test.enable)
                || (j == EVALSOC_GPIO && !s->gpio.enable)
                || (j == EVALSOC_UART0 && !s->uart0.enable)
                || (j == EVALSOC_UART1 && !s->uart1.enable)
                || (j == EVALSOC_QSPI0 && !s->qspi0.enable)
                || (j == EVALSOC_QSPI1 && !s->qspi1.enable)
                || (j == EVALSOC_QSPI2 && !s->qspi2.enable))
                continue;

            hwaddr start2 = memoryRegion[j].base;
            hwaddr end2 = start2 + memoryRegion[j].size;

            // If there is overlap, return true
            if (!(end1 <= start2 || end2 <= start1)) {
                printf("memory is overlap, %s: [%lx:%lx] and %s: [%lx:%lx]\n", memoryRegion[i].name, (long)start1, (long)end1, memoryRegion[j].name, (long)start2, (long)end2);
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
    uint64_t kernel_entry_point;
    int i;
    DriveInfo *dinfo;
    BlockBackend *blk;
    DeviceState *flash_dev, *sd_dev, *card_dev;
    qemu_irq flash_cs, sd_cs;

    parse_json_config(machine);

    if(s->ddr.base == -1)
    {
        s->ddr.base = memmap[EVALSOC_DDR].base;
        s->ddr.startup_addr = (s->ddr.startup_addr == -1) ? 0xA0000000 : s->ddr.startup_addr;
    }
    else
    {
        s->ddr.startup_addr = (s->ddr.startup_addr == -1) ? s->ddr.base : s->ddr.startup_addr;
    }
    s->ilm.startup_addr = (s->ilm.startup_addr == -1) ? s->ilm.base : s->ilm.startup_addr;
    s->sram.startup_addr = (s->sram.startup_addr == -1) ? s->sram.base : s->sram.startup_addr;
    s->norflash.startup_addr = (s->norflash.startup_addr == -1) ? s->norflash.base : s->norflash.startup_addr;
    s->dlm.startup_addr = (s->dlm.startup_addr == -1) ? s->dlm.base : s->dlm.startup_addr;

    /*if flash startup_addr not set, use flashxip startup_addr*/
    s->flash.startup_addr = (s->flash.startup_addr == -1) ? s->norflash.startup_addr: s->flash.startup_addr;

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

    //ilm
    memory_region_init_ram(&s->soc.ilm, NULL, "riscv.evalsoc.ram.ilm",
                           s->ilm.size, &error_fatal);
    memory_region_add_subregion(system_memory, s->ilm.base,
                                &s->soc.ilm);
    //dlm
    memory_region_init_ram(&s->soc.dlm, NULL, "riscv.evalsoc.ram.dlm",
                           s->dlm.size, &error_fatal);
    memory_region_add_subregion(system_memory, s->dlm.base,
                                &s->soc.dlm);
    //sram
    memory_region_init_ram(&s->soc.sram, NULL, "riscv.evalsoc.ram.sram",
                           s->sram.size, &error_fatal);
    memory_region_add_subregion(system_memory, s->sram.base,
                                &s->soc.sram);

    // ddr
    //if -m 128M or no -m,s->ddr.size is first json,then EVALSOC_DDR_SIZE
    if(machine->ram_size != 128 * MiB)
    {
        s->ddr.size = machine->ram_size;
    }

    memory_region_init_ram(&s->soc.ddr, NULL, "riscv.evalsoc.ram.ddr",
                           s->ddr.size, &error_fatal);
    memory_region_add_subregion(system_memory, s->ddr.base,
                                &s->soc.ddr);

    memory_region_init_ram(&s->soc.xip_mem, NULL, "riscv.evalsoc.flashxip",
        s->norflash.size, &error_fatal);
    memory_region_add_subregion(system_memory,
        s->norflash.base, &s->soc.xip_mem);

    // Evalsoc custom csr info init
    for (i = 0; i < machine->smp.cpus; i ++) {
        s->soc.cpus.harts[i].env.milm_ctl |= s->ilm.base & 0x1;
        s->soc.cpus.harts[i].env.mdlm_ctl |= s->dlm.base & 0x1;
        s->soc.cpus.harts[i].env.mstack_bound = EVALSOC_MSTACK_BOUND;
        s->soc.cpus.harts[i].env.mstack_base = EVALSOC_MSTACK_BASE;
        s->soc.cpus.harts[i].env.mcache_ctl = EVALSOC_MCACHE_CTL;
        s->soc.cpus.harts[i].env.mcfg_info =
            evalsoc_compose_mcfg_info(s, s->soc.cpus.harts[i].env.mcfg_info);
        s->soc.cpus.harts[i].env.micfg_info = EVALSOC_MICFG_INFO | (s->ilm.size << 16);
        s->soc.cpus.harts[i].env.mdcfg_info = EVALSOC_MDCFG_INFO | (s->dlm.size << 16);
        s->soc.cpus.harts[i].env.mtlbcfg_info = EVALSOC_MTLBCFG_INFO;
        s->soc.cpus.harts[i].env.mppicfg_info = EVALSOC_MPPICFG_INFO;
        s->soc.cpus.harts[i].env.mfiocfg_info = EVALSOC_MFIOCFG_INFO;
        s->soc.cpus.harts[i].env.mecc_ctrl = EVALSOC_MECC_CTRL;
        s->soc.cpus.harts[i].env.mecc_status = EVALSOC_MECC_STATUS;
        s->soc.cpus.harts[i].env.mtlb_ctl = EVALSOC_MTLB_CTL;
        s->soc.cpus.harts[i].env.mmisc_ctl1 = EVALSOC_MMISC_CTL1;

        s->soc.cpus.harts[i].env.msmpcfg_info = ((memmap[EVALSOC_SMP].base + s->iregion.base) & ~(1<<10)) | 0xF;

        // note: The iregion function is optional and cannot be forced to be set.
        s->soc.cpus.harts[i].env.mirgb_info = (s->iregion.base & ~(1<<10)) | 0xF;
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

    if (s->download == NULL)
    {
        start_addr = s->norflash.startup_addr;
    }
    else if (!strcmp(s->download, "ilm"))
    {
        start_addr = s->ilm.startup_addr;
    }
    else if (!strcmp(s->download, "ddr"))
    {
        // For cpu release after 2023.06, the DDR base changed from 0xA0000000 to 0x80000000
        // But we want to keep DOWNLOAD=ddr still use old 0xA0000000 base
        start_addr = s->ddr.startup_addr; // sram mode = ddr mode base address
    }
    else if (!strcmp(s->download, "sram"))
    {
        start_addr = s->sram.startup_addr;
    }
    else if (!strcmp(s->download, "flash"))
    {
        start_addr = s->flash.startup_addr;
    }
    else
    {
        start_addr = s->norflash.startup_addr;
    }
    DEBUGF("download mode is %s\n", s->download);
    DEBUGF("ddr     : base:0x%lx, size:0x%lx, startup_addr:0x%lx\n", (long)s->ddr.base,(long)s->ddr.size,(long)s->ddr.startup_addr);
    DEBUGF("ilm     : base:0x%lx, size:0x%lx, startup_addr:0x%lx\n", (long)s->ilm.base,(long)s->ilm.size,(long)s->ilm.startup_addr);
    DEBUGF("sram    : base:0x%lx, size:0x%lx, startup_addr:0x%lx\n", (long)s->sram.base,(long)s->sram.size,(long)s->sram.startup_addr);
    DEBUGF("norflash: base:0x%lx, size:0x%lx, startup_addr:0x%lx\n", (long)s->norflash.base,(long)s->norflash.size,(long)s->norflash.startup_addr);
    DEBUGF("flash   : base:0x%lx, size:0x%lx, startup_addr:0x%lx\n", (long)s->flash.base,(long)s->flash.size,(long)s->flash.startup_addr);
    DEBUGF("dlm     : base:0x%lx, size:0x%lx\n", (long)s->dlm.base,(long)s->dlm.size);
    DEBUGF("mrom    : base:0x%lx, size:0x%lx\n", (long)s->mrom.base,(long)s->mrom.size);
    DEBUGF("test    : base:0x%lx, size:0x%lx\n", (long)s->test.base,(long)s->test.size);
    DEBUGF("gpio    : base:0x%lx, size:0x%lx\n", (long)s->gpio.base,(long)s->gpio.size);
    DEBUGF("uart0   : base:0x%lx, size:0x%lx, irq:%d\n", (long)s->uart0.base, (long)s->uart0.size, (int)s->uart0.irq);
    DEBUGF("uart1   : base:0x%lx, size:0x%lx, irq:%d\n", (long)s->uart1.base, (long)s->uart1.size, (int)s->uart1.irq);
    DEBUGF("qspi0   : base:0x%lx, size:0x%lx, irq:%d\n", (long)s->qspi0.base, (long)s->qspi0.size, (int)s->qspi0.irq);
    DEBUGF("qspi1   : base:0x%lx, size:0x%lx, irq:%d\n", (long)s->qspi1.base, (long)s->qspi1.size, (int)s->qspi1.irq);
    DEBUGF("qspi2   : base:0x%lx, size:0x%lx, irq:%d\n", (long)s->qspi2.base, (long)s->qspi2.size, (int)s->qspi2.irq);
    DEBUGF("iregion : base:0x%lx, size:0x%lx\n", (long)s->iregion.base,(long)s->iregion.size);
    DEBUGF("irqmax  : %d\n", (int)s->irqmax);
    DEBUGF("timer_freq : %d\n", (int)s->timer_freq);
    DEBUGF("firmware startup addr:0x%lx\n", (long)start_addr);

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
            if (strstr(s->soc.cpu_type, "n100")) {
                load_elf_ram_sym(machine->kernel_filename, NULL, NULL, NULL,
                         &kernel_entry_point, NULL, NULL, NULL, 0,
                         EM_RISCV, 1, 0, NULL, true, NULL);
                start_addr = kernel_entry_point;
            } else {
                kernel_entry = riscv_load_kernel(machine, &s->soc.cpus,
                                            kernel_start_addr, true, NULL);
            }
        }
    }
    else
    {
        /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;

        fdt_load_addr = riscv_compute_fdt_addr(s->ddr.base,
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
    flash_dev = qdev_new("w25q512jv");
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
    card_dev = qdev_new(TYPE_SD_CARD_SPI);
    qdev_prop_set_drive_err(card_dev, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card_dev,
                        qdev_get_child_bus(sd_dev, "sd-bus"),
                        &error_fatal);

    bool is_32_bit = riscv_is_32bit(&s->soc.cpus);

    nuclei_iregion_create(memmap[EVALSOC_IINFO].base + s->iregion.base, is_32_bit);
}

static void evalsoc_machine_instance_init(Object *obj)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);
    const struct MemmapEntry *memmap = evalsoc_memmap;

    s->cpu_freq = -1; //qemu no use
    s->timer_freq = EVALSOC_TIMEBASE_FREQ;
    /* irqmax: max irq number for external irq
            1.eclic core irq:irq[0~18] external irq:irq[19...4095]
            2.plic  irq 0: wire 0 external irq:irq[1...1023]
    */
    s->irqmax = EVALSOC_PLIC_INT_MAX;
    s->iregion.base = IREGION_BASE_ADDR;
    s->iregion.size = IREGION_MAX_SIZE;
    s->iregion.debug_en = 1;
    s->iregion.eclic_en = 1;
    s->iregion.smpcc_en = 1;
    s->iregion.cidu_en = 1;
    s->iregion.plic_en = 1;
    s->ddr.base = -1;
    s->ddr.size = memmap[EVALSOC_DDR].size;
    s->ddr.startup_addr = -1;
    s->sram.base = memmap[EVALSOC_SRAM].base;
    s->sram.size = memmap[EVALSOC_SRAM].size;
    s->sram.startup_addr = -1;
    s->ilm.base = memmap[EVALSOC_ILM].base;
    s->ilm.size = memmap[EVALSOC_ILM].size;
    s->ilm.startup_addr = -1;
    s->dlm.base = memmap[EVALSOC_DLM].base;
    s->dlm.size = memmap[EVALSOC_DLM].size;
    s->dlm.startup_addr = -1;
    s->flash.base = memmap[EVALSOC_XIP].base;
    s->flash.size = memmap[EVALSOC_XIP].size;
    s->flash.startup_addr = -1;
    s->norflash.base = memmap[EVALSOC_XIP].base;
    s->norflash.size = memmap[EVALSOC_XIP].size;
    s->norflash.startup_addr = -1;
    s->mrom.base = memmap[EVALSOC_MROM].base;
    s->mrom.size = memmap[EVALSOC_MROM].size;
    s->test.base = memmap[EVALSOC_TEST].base;
    s->test.size = memmap[EVALSOC_TEST].size;
    s->test.enable = 1;
    s->gpio.base = memmap[EVALSOC_GPIO].base;
    s->gpio.size = memmap[EVALSOC_GPIO].size;
    s->gpio.enable = 1;
    s->uart0.base = memmap[EVALSOC_UART0].base;
    s->uart0.size = memmap[EVALSOC_UART0].size;
    s->uart0.irq = EVALSOC_PLIC_UART0_IRQ;
    s->uart0.enable = 1;
    s->uart1.base = memmap[EVALSOC_UART1].base;
    s->uart1.size = memmap[EVALSOC_UART1].size;
    s->uart1.irq = EVALSOC_PLIC_UART1_IRQ;
    s->uart1.enable = 1;
    s->qspi0.base = memmap[EVALSOC_QSPI0].base;
    s->qspi0.size = memmap[EVALSOC_QSPI0].size;
    s->qspi0.irq = EVALSOC_PLIC_SPI0_IRQ;
    s->qspi0.enable = 1;
    s->qspi1.base = memmap[EVALSOC_QSPI1].base;
    s->qspi1.size = memmap[EVALSOC_QSPI1].size;
    s->qspi1.irq = EVALSOC_PLIC_SPI1_IRQ;
    s->qspi1.enable = 1;
    s->qspi2.base = memmap[EVALSOC_QSPI2].base;
    s->qspi2.size = memmap[EVALSOC_QSPI2].size;
    s->qspi2.irq = EVALSOC_PLIC_SPI2_IRQ;
    s->qspi2.enable = 1;

    object_property_add_uint64_ptr(obj, "iregion", &s->iregion.base,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "iregion",
                                    "Set iregion");

    object_property_add_uint32_ptr(obj, "debug", &debug_flag,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_set_description(obj, "debug",
                                    "debug nuclei evalsoc");
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

static char *evalsoc_machine_get_aia(Object *obj, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);
    const char *val;

    switch (s->aia_type) {
    case EVALSOC_AIA_TYPE_APLIC:
        val = "aplic";
        break;
    case EVALSOC_AIA_TYPE_APLIC_IMSIC:
        val = "aplic-imsic";
        break;
    default:
        val = "none";
        break;
    };

    return g_strdup(val);
}

static void evalsoc_machine_set_aia(Object *obj, const char *val, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);

    if (!strcmp(val, "none")) {
        s->aia_type = EVALSOC_AIA_TYPE_NONE;
    } else if (!strcmp(val, "aplic")) {
        s->aia_type = EVALSOC_AIA_TYPE_APLIC;
    } else if (!strcmp(val, "aplic-imsic")) {
        s->aia_type = EVALSOC_AIA_TYPE_APLIC_IMSIC;
    } else {
        error_setg(errp, "Invalid AIA interrupt controller type");
        error_append_hint(errp, "Valid values are none, aplic, and "
                          "aplic-imsic.\n");
    }
}

static char *evalsoc_machine_get_aia_guests(Object *obj, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);
    char val[32];

    sprintf(val, "%d", s->aia_guests);
    return g_strdup(val);
}

static void evalsoc_machine_set_aia_guests(Object *obj, const char *val, Error **errp)
{
    EvalSoCState *s = RISCV_EVALSOC_MACHINE(obj);

    s->aia_guests = atoi(val);
    if (s->aia_guests < 0 || s->aia_guests > EVALSOC_IRQCHIP_MAX_GUESTS) {
        error_setg(errp, "Invalid number of AIA IMSIC guests");
        error_append_hint(errp, "Valid values be between 0 and %d.\n",
                          EVALSOC_IRQCHIP_MAX_GUESTS);
    }
}

static void evalsoc_machine_class_init(ObjectClass *oc, void *data)
{
    char str[128];
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Nuclei RISC-V EvalSoC, support Nuclei RISC-V 200/300/600/900 series processors";
    mc->init = evalsoc_machine_init;
    mc->max_cpus = 64;
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
    object_class_property_add_str(oc, "aia",
                                  evalsoc_machine_get_aia,
                                  evalsoc_machine_set_aia);
    object_class_property_set_description(oc, "aia",
                                          "Set type of AIA interrupt "
                                          "controller. Valid values are "
                                          "none, aplic and aplic-imsic.");
    object_class_property_add_str(oc, "aia-guests",
                                  evalsoc_machine_get_aia_guests,
                                  evalsoc_machine_set_aia_guests);
    sprintf(str, "Set number of guest MMIO pages for AIA IMSIC. Valid value "
                 "should be between 0 and %d.", EVALSOC_IRQCHIP_MAX_GUESTS);
    object_class_property_set_description(oc, "aia-guests", str);
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

    object_initialize_child(obj, "gpio", &s->gpio, TYPE_NUCLEI_GPIO);
    object_initialize_child(obj, "spi0", &s->spi0, TYPE_NUCLEI_SPI);
    object_initialize_child(obj, "spi2", &s->spi2, TYPE_NUCLEI_SPI);
    object_initialize_child(obj, "timer", &s->timer, TYPE_NUCLEI_SYSTIMER);
}

static void riscv_evalsoc_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    EvalSoCState *mst = RISCV_EVALSOC_MACHINE(ms);
    EvalSoCSoCState *s = RISCV_EVALSOC_SOC(dev);
    const struct MemmapEntry *memmap = evalsoc_memmap;
    MemoryRegion *sys_mem = get_system_memory();
    NucleiSMPCCInit smpcc_cfg = {EVALSOC_SMP_VER,
                                EVALSOC_SMP_CFG | ((ms->smp.cpus - 1) << 1),
                                EVALSOC_CC_CFG,
                                EVALSOC_CLM_BASE_ADDR,
                                EVALSOC_CLUSTER_CACHE_SIZE,
                                EVALSOC_CLM_WAY_EN
                                };

    int i = 0;
    char *plic_hart_config;
    size_t plic_hart_config_len;
    bool msimode;
    hwaddr msi_addr;
    uint32_t guest_bits;

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

    if (mst->aia_type == EVALSOC_AIA_TYPE_NONE) {
        if (mst->iregion.plic_en) {
            s->irqchip = sifive_plic_create(memmap[EVALSOC_PLIC].base + mst->iregion.base,
                                        plic_hart_config, ms->smp.cpus, 0,
                                        mst->irqmax > EVALSOC_PLIC_NUM_SOURCES ? EVALSOC_PLIC_NUM_SOURCES : mst->irqmax,
                                        EVALSOC_PLIC_NUM_PRIORITIES,
                                        EVALSOC_PLIC_PRIORITY_BASE,
                                        EVALSOC_PLIC_PENDING_BASE,
                                        EVALSOC_PLIC_ENABLE_BASE,
                                        EVALSOC_PLIC_ENABLE_STRIDE,
                                        EVALSOC_PLIC_CONTEXT_BASE,
                                        EVALSOC_PLIC_CONTEXT_STRIDE,
                                        memmap[EVALSOC_PLIC].size);
            g_free(plic_hart_config);
        }
    } else {
        msimode = (mst->aia_type == EVALSOC_AIA_TYPE_APLIC_IMSIC) ? true : false;
        if (msimode) {
            /* M-level IMSICs */
            msi_addr = memmap[EVALSOC_IMSIC_M].base;
            for (i = 0; i < EVALSOC_IMSIC_DEFAULT_HARTS; i++) {
                if (i < ms->smp.cpus) {
                    riscv_imsic_create(msi_addr + i * memmap[EVALSOC_IMSIC_M].size,
                                        i, true, 1, EVALSOC_IRQCHIP_NUM_MSIS);
                } else {
                    MemoryRegion *mr = g_new(MemoryRegion, 1);
                    char *ram_block_name = g_strdup_printf("imsic-M-reserved-ram-hart%d", i);
                    memory_region_init_io(mr, NULL, &riscv_imsic_dummy_ops, NULL,
                                        ram_block_name, memmap[EVALSOC_IMSIC_M].size);
                    memory_region_add_subregion(get_system_memory(), msi_addr + i * memmap[EVALSOC_IMSIC_M].size, mr);
                }
            }
            /* S-level IMSICs */
            guest_bits = imsic_num_bits(mst->aia_guests + 1);
            msi_addr = memmap[EVALSOC_IMSIC_S].base;
            for (i = 0; i < EVALSOC_IMSIC_DEFAULT_HARTS; i++) {
                if (i < ms->smp.cpus) {
                    riscv_imsic_create(msi_addr + i * IMSIC_HART_SIZE(guest_bits),
                                    i, false, 1 + mst->aia_guests,
                                    EVALSOC_IRQCHIP_NUM_MSIS);
                } else {
                    MemoryRegion *mr = g_new(MemoryRegion, 1);
                    char *ram_block_name = g_strdup_printf("imsic-S-reserved-ram-hart%d", i);
                    memory_region_init_io(mr, NULL, &riscv_imsic_dummy_ops, NULL,
                                        ram_block_name, IMSIC_HART_SIZE(guest_bits));
                    memory_region_add_subregion(get_system_memory(), msi_addr + i * IMSIC_HART_SIZE(guest_bits), mr);
                }
            }
        }
        
        /* M-level APLIC */
        s->irqchip = riscv_aplic_create(
            memmap[EVALSOC_APLIC_M].base,
            memmap[EVALSOC_APLIC_M].size,
            0, 
            (msimode) ? 0 : ms->smp.cpus,
            mst->irqmax > EVALSOC_PLIC_NUM_SOURCES ? EVALSOC_PLIC_NUM_SOURCES : mst->irqmax,
            VIRT_IRQCHIP_NUM_PRIO_BITS,
            msimode, true, NULL);

        if (s->irqchip) {
            /* S-level APLIC */
            riscv_aplic_create(
                memmap[EVALSOC_APLIC_S].base,
                memmap[EVALSOC_APLIC_S].size,
                0, 
                (msimode) ? 0 : ms->smp.cpus,
                mst->irqmax > EVALSOC_PLIC_NUM_SOURCES ? EVALSOC_PLIC_NUM_SOURCES : mst->irqmax,
                VIRT_IRQCHIP_NUM_PRIO_BITS,
                msimode, false, s->irqchip);
        }
    }

    s->eclic = (mst->iregion.eclic_en) ?
                nuclei_eclic_create(memmap[EVALSOC_ECLIC].base + mst->iregion.base,
                    memmap[EVALSOC_ECLIC].size,
                    false, false, true,
                    ms->smp.cpus,
                    get_irq_number_alignment(PLIC_IRQ_TO_ECLIC_IRQ(mst->irqmax)),
                    EVALSOC_CLIC_INTCTLBITS,
                    SHADOW_GPR_GROUPS) : NULL;

    s->smpcc = (mst->iregion.smpcc_en) ?
                nuclei_smpcc_create(memmap[EVALSOC_SMP].base + mst->iregion.base,
                    memmap[EVALSOC_SMP].size,
                    &smpcc_cfg) : NULL;

    s->cidu = (mst->iregion.cidu_en) ?
                nuclei_cidu_create(memmap[EVALSOC_CIDU].base + mst->iregion.base,
                    memmap[EVALSOC_CIDU].size,
                    ms->smp.cpus,
                    EVALSOC_ECLIC_NUM_SOURCES - CIDU_EXT_INT_OFST,
                    s->eclic) : NULL;

    if (ms->firmware == NULL)
    {
        if (mst->uart0.enable) {
            /* Create and connect UART interrupts to the ECLIC */
            nuclei_uart_create(sys_mem,
                            mst->uart0.base,
                            memmap[EVALSOC_UART0].size,
                            serial_hd(0),
                            PLIC_IRQ_TO_ECLIC_IRQ(mst->uart0.irq),
                            s->cidu,
                            s->eclic,
                            NULL);
        }

        nuclei_systimer_create(memmap[EVALSOC_TIMER].base + mst->iregion.base,
                memmap[EVALSOC_TIMER].size, 0, ms->smp.cpus, s->eclic, mst->timer_freq);
    }
    else
    {
        if (mst->uart0.enable) {
            nuclei_uart_create(sys_mem,
                            mst->uart0.base,
                            memmap[EVALSOC_UART0].size,
                            serial_hd(0),
                            0,
                            NULL,
                            NULL,
                            s->irqchip ?
                            qdev_get_gpio_in(DEVICE(s->irqchip), mst->uart0.irq) :
                            NULL);
        }

        if (mst->uart1.enable) {
            nuclei_uart_create(sys_mem,
                            mst->uart1.base,
                            memmap[EVALSOC_UART1].size,
                            serial_hd(1),
                            0,
                            NULL,
                            NULL,
                            s->irqchip ?
                            qdev_get_gpio_in(DEVICE(s->irqchip), mst->uart1.irq) :
                            NULL);
        }

        nuclei_systimer_create(memmap[EVALSOC_TIMER].base + mst->iregion.base,
                memmap[EVALSOC_TIMER].size, 0, ms->smp.cpus, NULL, mst->timer_freq);
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
    if (mst->gpio.enable) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[EVALSOC_GPIO].base);

        /* Pass all GPIOs to the SOC layer so they are available to the board */
        qdev_pass_gpios(DEVICE(&s->gpio), dev, NULL);
        /* Connect GPIO interrupts to the PLIC */
        if (s->irqchip) {
            for (i = 0; i < 32; i++)
            {
                sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), i,
                                qdev_get_gpio_in(DEVICE(s->irqchip),
                                                    EVALSOC_PLIC_GPIO_IRQ0 + i));
            }
        }
    }

    sysbus_realize(SYS_BUS_DEVICE(&s->spi0), errp);
    if (mst->qspi0.enable) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi0), 0,
                        mst->qspi0.base);
        if (s->irqchip)
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi0), 0,
                        qdev_get_gpio_in(DEVICE(s->irqchip), mst->qspi0.irq));
    }

    sysbus_realize(SYS_BUS_DEVICE(&s->spi2), errp);
    if (mst->qspi2.enable) {
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi2), 0,
                        mst->qspi2.base);
        if (s->irqchip)
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi2), 0,
                            qdev_get_gpio_in(DEVICE(s->irqchip), mst->qspi2.irq));
    }

    /* Nuclei Test MMIO device */
    if (mst->test.enable)
        nuclei_test_create(memmap[EVALSOC_TEST].base);
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
