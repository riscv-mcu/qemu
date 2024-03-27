/*
 * nuclei evalsoc
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
#ifndef HW_NUCLEI_EVALSOC_H
#define HW_NUCLEI_EVALSOC_H

#include "hw/dma/sifive_pdma.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_cpu.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/misc/sifive_u_otp.h"
#include "hw/misc/sifive_u_prci.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/sd/sd.h"
#include "hw/ssi/sifive_spi.h"

#include "hw/cpu/cluster.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/char/nuclei_uart.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/intc/nuclei_cidu.h"
#include "hw/smpcc/nuclei_smpcc.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "hw/ssi/sifive_spi.h"

#include "hw/riscv/sifive_cpu.h"
#include "hw/misc/sifive_u_otp.h"
#include "hw/misc/sifive_u_prci.h"
#include "hw/dma/sifive_pdma.h"

/* CLINT timebase frequency */
#define CLINT_TIMEBASE_FREQ 1000000

#define EVALSOC_CLIC_INTCTLBITS 3
//#define NUCLEI_U_ECLIC_INTCTLBITS 3

#define TYPE_EVALSOC_SOC "riscv.evalsoc.soc"
//#define TYPE_NUCLEI_SYSTIMER "riscv.nuclei.systimer"

#define RISCV_EVALSOC_SOC(obj) \
    OBJECT_CHECK(EvalSoCSoCState, (obj), TYPE_EVALSOC_SOC)

typedef struct EvalSoCSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    CPUClusterState u_cluster;
    RISCVHartArrayState cpus;

    DeviceState *plic;
    DeviceState *eclic;
    DeviceState *cidu;
    DeviceState *smpcc;
    MemoryRegion ilm;
    MemoryRegion dlm;
    MemoryRegion internal_rom;
    MemoryRegion xip_mem;
    MemoryRegion ddr;
    MemoryRegion smp;

    NucLeiSYSTIMERState timer;
    
    SIFIVEGPIOState gpio;
    SiFiveSPIState spi0;
    SiFiveSPIState spi2;

    uint32_t serial;
    char *cpu_type;

} EvalSoCSoCState;

typedef struct
{
    /*< private >*/
    SysBusDevice parent_obj;

    void *fdt;
    int fdt_size;

    const char *download;
    const char *soccfg;
    uint64_t iregion;
    uint64_t ddr_base;
    uint64_t ddr_size;
    uint64_t norflash_base;
    uint64_t norflash_size;
    uint64_t uart0_base;
    uint64_t uart0_irq;
    uint64_t uart1_base;
    uint64_t uart1_irq;
    uint64_t qspi0_base;
    uint64_t qspi0_irq;
    uint64_t qspi1_base;
    uint64_t qspi1_irq;
    uint64_t qspi2_base;
    uint64_t qspi2_irq;
    uint64_t cpu_freq;
    uint64_t timer_freq;
    /*< public >*/
    EvalSoCSoCState soc;
    uint32_t msel;
    uint32_t serial;
} EvalSoCState;

#define TYPE_RISCV_EVALSOC_MACHINE MACHINE_TYPE_NAME("evalsoc")
#define RISCV_EVALSOC_MACHINE(obj) \
    OBJECT_CHECK(EvalSoCState, (obj), TYPE_RISCV_EVALSOC_MACHINE)

enum {
    EVALSOC_IINFO,
    EVALSOC_DEBUG,
    EVALSOC_MROM,
    EVALSOC_TEST,
    EVALSOC_TIMER,
    EVALSOC_CLINT,
    EVALSOC_PLIC,
    EVALSOC_ECLIC,
    EVALSOC_CIDU,
    EVALSOC_GPIO,
    EVALSOC_UART0,
    EVALSOC_QSPI0,
    EVALSOC_UART1,
    EVALSOC_QSPI1,
    EVALSOC_QSPI2,
    EVALSOC_SMP,
    EVALSOC_XIP,
    EVALSOC_ILM,
    EVALSOC_DLM,
    EVALSOC_DDR,
    EVALSOC_DEV_END
};

enum
{
    EVALSOC_PLIC_GPIO_IRQ0 = 1,
    EVALSOC_PLIC_GPIO_IRQ1 = 2,
    EVALSOC_PLIC_GPIO_IRQ2 = 3,
    EVALSOC_PLIC_GPIO_IRQ3 = 4,
    EVALSOC_PLIC_GPIO_IRQ4 = 5,
    EVALSOC_PLIC_GPIO_IRQ5 = 6,
    EVALSOC_PLIC_GPIO_IRQ6 = 7,
    EVALSOC_PLIC_GPIO_IRQ7 = 8,
    EVALSOC_PLIC_GPIO_IRQ8 = 9,
    EVALSOC_PLIC_GPIO_IRQ9 = 10,
    EVALSOC_PLIC_GPIO_IRQ10 = 11,
    EVALSOC_PLIC_GPIO_IRQ11 = 12,
    EVALSOC_PLIC_GPIO_IRQ12 = 13,
    EVALSOC_PLIC_GPIO_IRQ13 = 14,
    EVALSOC_PLIC_GPIO_IRQ14 = 15,
    EVALSOC_PLIC_GPIO_IRQ15 = 16,
    EVALSOC_PLIC_GPIO_IRQ16 = 17,
    EVALSOC_PLIC_GPIO_IRQ17 = 18,
    EVALSOC_PLIC_GPIO_IRQ18 = 19,
    EVALSOC_PLIC_GPIO_IRQ19 = 20,
    EVALSOC_PLIC_GPIO_IRQ20 = 21,
    EVALSOC_PLIC_GPIO_IRQ21 = 22,
    EVALSOC_PLIC_GPIO_IRQ22 = 23,
    EVALSOC_PLIC_GPIO_IRQ23 = 24,
    EVALSOC_PLIC_GPIO_IRQ24 = 25,
    EVALSOC_PLIC_GPIO_IRQ25 = 26,
    EVALSOC_PLIC_GPIO_IRQ26 = 27,
    EVALSOC_PLIC_GPIO_IRQ27 = 28,
    EVALSOC_PLIC_GPIO_IRQ28 = 29,
    EVALSOC_PLIC_GPIO_IRQ29 = 30,
    EVALSOC_PLIC_GPIO_IRQ30 = 31,
    EVALSOC_PLIC_GPIO_IRQ31 = 32,
    EVALSOC_PLIC_UART0_IRQ = 33,
    EVALSOC_PLIC_UART1_IRQ = 34,
    EVALSOC_PLIC_SPI0_IRQ = 35,
    EVALSOC_PLIC_SPI1_IRQ = 36,
    EVALSOC_PLIC_SPI2_IRQ = 37,
    EVALSOC_PLIC_INT_MAX = 1023,
};

enum {
    EVALSOC_ECLIC_INT19_IRQn           = 19,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT20_IRQn           = 20,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT21_IRQn           = 21,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT22_IRQn           = 22,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT23_IRQn           = 23,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT24_IRQn           = 24,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT25_IRQn           = 25,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT26_IRQn           = 26,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT27_IRQn           = 27,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT28_IRQn           = 28,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT29_IRQn           = 29,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT30_IRQn           = 30,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT31_IRQn           = 31,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT32_IRQn           = 32,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT33_IRQn           = 33,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT34_IRQn           = 34,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT35_IRQn           = 35,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT36_IRQn           = 36,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT37_IRQn           = 37,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT38_IRQn           = 38,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT39_IRQn           = 39,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT40_IRQn           = 40,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT41_IRQn           = 41,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT42_IRQn           = 42,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT43_IRQn           = 43,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT44_IRQn           = 44,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT45_IRQn           = 45,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT46_IRQn           = 46,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT47_IRQn           = 47,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT48_IRQn           = 48,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT49_IRQn           = 49,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT50_IRQn           = 50,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT51_IRQn           = 51,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT52_IRQn           = 52,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT53_IRQn           = 53,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT54_IRQn           = 54,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT55_IRQn           = 55,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT56_IRQn           = 56,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT57_IRQn           = 57,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT58_IRQn           = 58,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT59_IRQn           = 59,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT60_IRQn           = 60,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT61_IRQn           = 61,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT62_IRQn           = 62,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT63_IRQn           = 63,                /*!< Device Interrupt */
    EVALSOC_ECLIC_INT_MAX              = 4095,
};


/* UART0 Interrupt */
#define UART0_IRQn                                 EVALSOC_ECLIC_INT51_IRQn
#define UART1_IRQn                                 EVALSOC_ECLIC_INT52_IRQn
/* QSPI Interrupt */
#define QSPI0_IRQn                                 EVALSOC_ECLIC_INT53_IRQn
#define QSPI1_IRQn                                 EVALSOC_ECLIC_INT54_IRQn
#define QSPI2_IRQn                                 EVALSOC_ECLIC_INT55_IRQn

enum
{
    EVALSOC_HFCLK_FREQ = 80000,
    EVALSOC_RTCCLK_FREQ = 80000
};

#define EVALSOC_SMP_VER             0
#define EVALSOC_SMP_CFG             0x1
#define EVALSOC_CC_CFG              0
#define EVALSOC_CLM_BASE_ADDR       0
#define EVALSOC_CLM_WAY_EN          0
#define EVALSOC_CLUSTER_CACHE_SIZE  0x40000

#define EVALSOC_ECLIC_NUM_SOURCES 4096

#define EVALSOC_MANAGEMENT_CPU_COUNT 1
#define EVALSOC_COMPUTE_CPU_COUNT 16

#define EVALSOC_PLIC_HART_CONFIG "MS"
#define EVALSOC_PLIC_NUM_SOURCES 1024
#define EVALSOC_PLIC_NUM_PRIORITIES 7
#define EVALSOC_PLIC_PRIORITY_BASE 0x00
#define EVALSOC_PLIC_PENDING_BASE 0x1000
#define EVALSOC_PLIC_ENABLE_BASE 0x2000
#define EVALSOC_PLIC_ENABLE_STRIDE 0x80
#define EVALSOC_PLIC_CONTEXT_BASE 0x200000
#define EVALSOC_PLIC_CONTEXT_STRIDE 0x1000

#define EVALSOC_IINFO_BASE          (0)
#define EVALSOC_IINFO_SIZE          (0x1000)
#define EVALSOC_MROM_BASE           (0x1000)
#define EVALSOC_MROM_SIZE           (0xf000)
#define EVALSOC_TEST_BASE           (0x100000)
#define EVALSOC_TEST_SIZE           (0x10000)
#define EVALSOC_GPIO_BASE           (0x10012000)
#define EVALSOC_GPIO_SIZE           (0x1000)
#define EVALSOC_UART0_BASE          (0x10013000)
#define EVALSOC_UART0_SIZE          (0x1000)
#define EVALSOC_UART1_BASE          (0x10023000)
#define EVALSOC_UART1_SIZE          (0x1000)
#define EVALSOC_QSPI0_BASE          (0x10014000)
#define EVALSOC_QSPI0_SIZE          (0x1000)
#define EVALSOC_QSPI1_BASE          (0x10024000)
#define EVALSOC_QSPI1_SIZE          (0x1000)
#define EVALSOC_QSPI2_BASE          (0x10034000)
#define EVALSOC_QSPI2_SIZE          (0x1000)

#define EVALSOC_XIP_BASE            (0x20000000)
#define EVALSOC_XIP_SIZE            (0x20000000)
#define EVALSOC_DDR_BASE            (0x80000000)
#define EVALSOC_DDR_SIZE            (0x80000000)
#define EVALSOC_ILM_ADDR            (0x80000000)
#define EVALSOC_DLM_ADDR            (0x90000000)
#define EVALSOC_DDR_MODE_ADDR       (0xA0000000)

/* IREGION Offsets */
#define IREGION_IINFO_OFS           (0x0)
#define IREGION_DEBUG_OFS           (0x10000)
#define IREGION_DEBUG_SIZE          (0x1000)
#define IREGION_ECLIC_OFS           (0x20000)
#define IREGION_ECLIC_SIZE          (0x10000)
#define IREGION_TIMER_OFS           (0x30000)
#define IREGION_TIMER_SIZE          (0x10000)
#define IREGION_SMP_OFS             (0x40000)
#define IREGION_SMP_SIZE            (0x1000)
#define IREGION_IDU_OFS             (0x50000)
#define IREGION_IDU_SIZE            (0x10000)
#define IREGION_PL2_OFS             (0x60000)
#define IREGION_DPREFETCH_OFS       (0x70000)
#define IREGION_PLIC_OFS            (0x4000000)
#define IREGION_PLIC_SIZE           (0x4000000)
#define EVALSOC_PPI_ADDR            (0xB0000000)
#define EVALSOC_FIO_ADDR            (0xC0000000)

#define EVALSOC_MSTACK_BOUND        0xffffffff
#define EVALSOC_MSTACK_BASE         0xffffffff
#define EVALSOC_MCACHE_CTL          0x20004
#define EVALSOC_MCFG_INFO           0x810fcc
#define EVALSOC_MICFG_INFO          0xd0137
#define EVALSOC_MDCFG_INFO          0xd0137
#define EVALSOC_MTLBCFG_INFO        0x2d02b4
#define EVALSOC_MPPICFG_INFO        (EVALSOC_PPI_ADDR | 0x1e)
#define EVALSOC_MFIOCFG_INFO        (EVALSOC_FIO_ADDR | 0x1e)
#define EVALSOC_MECC_CTRL           0x3ff
#define EVALSOC_MECC_STATUS         0
#define EVALSOC_MTLB_CTL            0
#define EVALSOC_MFP16MODE           0


#if defined(TARGET_RISCV32)
#define EVALSOC_CPU TYPE_RISCV_CPU_NUCLEI_N300FD
#elif defined(TARGET_RISCV64)
#define EVALSOC_CPU TYPE_RISCV_CPU_NUCLEI_NX900FD
#endif

#endif

