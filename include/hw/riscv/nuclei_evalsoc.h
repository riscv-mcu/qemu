/*
 * Nuclei U series  SOC machine interface
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
#include "hw/intc/nuclei_systimer.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/intc/nuclei_eclic.h"
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
    MemoryRegion ilm;
    MemoryRegion dlm;
    MemoryRegion internal_rom;
    MemoryRegion xip_mem;
    MemoryRegion ddr;
    MemoryRegion smp;

    //DeviceState *timer;
    NucLeiSYSTIMERState timer;
 //   NucLeiUARTState uart;
   // NucLeiSYSTIMERState timer;
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
    /*< public >*/
    EvalSoCSoCState soc;
    uint32_t msel;
    uint32_t serial;
} EvalSoCState;

#define TYPE_RISCV_EVALSOC_MACHINE MACHINE_TYPE_NAME("evalsoc")
#define RISCV_EVALSOC_MACHINE(obj) \
    OBJECT_CHECK(EvalSoCState, (obj), TYPE_RISCV_EVALSOC_MACHINE)

enum {
    EVALSOC_DEBUG,
    EVALSOC_MROM,
    EVALSOC_TEST,
    EVALSOC_TIMER,
    EVALSOC_PLIC,
    EVALSOC_ECLIC,
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
    EVALSOC_DDR
};

enum
{
    EVALSOC_GPIO_IRQ0 = 1,
    EVALSOC_GPIO_IRQ1 = 2,
    EVALSOC_GPIO_IRQ2 = 3,
    EVALSOC_GPIO_IRQ3 = 4,
    EVALSOC_GPIO_IRQ4 = 5,
    EVALSOC_GPIO_IRQ5 = 6,
    EVALSOC_GPIO_IRQ6 = 7,
    EVALSOC_GPIO_IRQ7 = 8,
    EVALSOC_GPIO_IRQ8 = 9,
    EVALSOC_GPIO_IRQ9 = 10,
    EVALSOC_GPIO_IRQ10 = 11,
    EVALSOC_GPIO_IRQ11 = 12,
    EVALSOC_GPIO_IRQ12 = 13,
    EVALSOC_GPIO_IRQ13 = 14,
    EVALSOC_GPIO_IRQ14 = 15,
    EVALSOC_GPIO_IRQ15 = 16,
    EVALSOC_GPIO_IRQ16 = 17,
    EVALSOC_GPIO_IRQ17 = 18,
    EVALSOC_GPIO_IRQ18 = 19,
    EVALSOC_GPIO_IRQ19 = 20,
    EVALSOC_GPIO_IRQ20 = 21,
    EVALSOC_GPIO_IRQ21 = 22,
    EVALSOC_GPIO_IRQ22 = 23,
    EVALSOC_GPIO_IRQ23 = 24,
    EVALSOC_GPIO_IRQ24 = 25,
    EVALSOC_GPIO_IRQ25 = 26,
    EVALSOC_GPIO_IRQ26 = 27,
    EVALSOC_GPIO_IRQ27 = 28,
    EVALSOC_GPIO_IRQ28 = 29,
    EVALSOC_GPIO_IRQ29 = 30,
    EVALSOC_GPIO_IRQ30 = 31,
    EVALSOC_GPIO_IRQ31 = 32,
    EVALSOC_UART0_IRQ = 33,
    EVALSOC_UART1_IRQ = 34,
    EVALSOC_SPI0_IRQ = 35,
    EVALSOC_SPI1_IRQ = 36,
    EVALSOC_SPI2_IRQ = 37,
    EVALSOC_DEV_INT_MAX
};

enum {
    EVALSOC_INT19_IRQn           = 19,                /*!< Device Interrupt */
    EVALSOC_INT20_IRQn           = 20,                /*!< Device Interrupt */
    EVALSOC_INT21_IRQn           = 21,                /*!< Device Interrupt */
    EVALSOC_INT22_IRQn           = 22,                /*!< Device Interrupt */
    EVALSOC_INT23_IRQn           = 23,                /*!< Device Interrupt */
    EVALSOC_INT24_IRQn           = 24,                /*!< Device Interrupt */
    EVALSOC_INT25_IRQn           = 25,                /*!< Device Interrupt */
    EVALSOC_INT26_IRQn           = 26,                /*!< Device Interrupt */
    EVALSOC_INT27_IRQn           = 27,                /*!< Device Interrupt */
    EVALSOC_INT28_IRQn           = 28,                /*!< Device Interrupt */
    EVALSOC_INT29_IRQn           = 29,                /*!< Device Interrupt */
    EVALSOC_INT30_IRQn           = 30,                /*!< Device Interrupt */
    EVALSOC_INT31_IRQn           = 31,                /*!< Device Interrupt */
    EVALSOC_INT32_IRQn           = 32,                /*!< Device Interrupt */
    EVALSOC_INT33_IRQn           = 33,                /*!< Device Interrupt */
    EVALSOC_INT34_IRQn           = 34,                /*!< Device Interrupt */
    EVALSOC_INT35_IRQn           = 35,                /*!< Device Interrupt */
    EVALSOC_INT36_IRQn           = 36,                /*!< Device Interrupt */
    EVALSOC_INT37_IRQn           = 37,                /*!< Device Interrupt */
    EVALSOC_INT38_IRQn           = 38,                /*!< Device Interrupt */
    EVALSOC_INT39_IRQn           = 39,                /*!< Device Interrupt */
    EVALSOC_INT40_IRQn           = 40,                /*!< Device Interrupt */
    EVALSOC_INT41_IRQn           = 41,                /*!< Device Interrupt */
    EVALSOC_INT42_IRQn           = 42,                /*!< Device Interrupt */
    EVALSOC_INT43_IRQn           = 43,                /*!< Device Interrupt */
    EVALSOC_INT44_IRQn           = 44,                /*!< Device Interrupt */
    EVALSOC_INT45_IRQn           = 45,                /*!< Device Interrupt */
    EVALSOC_INT46_IRQn           = 46,                /*!< Device Interrupt */
    EVALSOC_INT47_IRQn           = 47,                /*!< Device Interrupt */
    EVALSOC_INT48_IRQn           = 48,                /*!< Device Interrupt */
    EVALSOC_INT49_IRQn           = 49,                /*!< Device Interrupt */
    EVALSOC_INT50_IRQn           = 50,                /*!< Device Interrupt */
    EVALSOC_INT_MAX,
};

enum
{
    EVALSOC_HFCLK_FREQ = 80000,
    EVALSOC_RTCCLK_FREQ = 80000
};


#define EVALSOC_MANAGEMENT_CPU_COUNT 1
#define EVALSOC_COMPUTE_CPU_COUNT 16

#define EVALSOC_PLIC_HART_CONFIG "MS"
#define EVALSOC_PLIC_NUM_SOURCES 54
#define EVALSOC_PLIC_NUM_PRIORITIES 7
#define EVALSOC_PLIC_PRIORITY_BASE 0x04
#define EVALSOC_PLIC_PENDING_BASE 0x1000
#define EVALSOC_PLIC_ENABLE_BASE 0x2000
#define EVALSOC_PLIC_ENABLE_STRIDE 0x80
#define EVALSOC_PLIC_CONTEXT_BASE 0x200000
#define EVALSOC_PLIC_CONTEXT_STRIDE 0x1000

#if defined(TARGET_RISCV32)
#define EVALSOC_CPU TYPE_RISCV_CPU_NUCLEI_N300FD
#elif defined(TARGET_RISCV64)
#define EVALSOC_CPU TYPE_RISCV_CPU_NUCLEI_NX900FD
#endif

#endif
