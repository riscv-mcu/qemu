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
#ifndef HW_NUCLEI_DEMOSOC_H
#define HW_NUCLEI_DEMOSOC_H


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




#define DEMOSOC_CLIC_INTCTLBITS 3
//#define NUCLEI_U_ECLIC_INTCTLBITS 3

#define TYPE_DEMOSOC_SOC "riscv.demosoc.soc"
//#define TYPE_NUCLEI_SYSTIMER "riscv.nuclei.systimer"

#define RISCV_DEMOSOC_SOC(obj) \
    OBJECT_CHECK(DemoSoCSoCState, (obj), TYPE_DEMOSOC_SOC)

typedef struct DemoSoCSoCState {
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

} DemoSoCSoCState;

typedef struct
{
    /*< private >*/
    SysBusDevice parent_obj;

    void *fdt;
    int fdt_size;

    const char *download;
    /*< public >*/
    DemoSoCSoCState soc;
    uint32_t msel;
    uint32_t serial;
} DemoSoCState;

#define TYPE_RISCV_DEMOSOC_MACHINE MACHINE_TYPE_NAME("demosoc")
#define RISCV_DEMOSOC_MACHINE(obj) \
    OBJECT_CHECK(DemoSoCState, (obj), TYPE_RISCV_DEMOSOC_MACHINE)

enum {
    DEMOSOC_DEBUG,
    DEMOSOC_MROM,
    DEMOSOC_TEST,
    DEMOSOC_TIMER,
    DEMOSOC_PLIC,
    DEMOSOC_ECLIC,
    DEMOSOC_GPIO,
    DEMOSOC_UART0,
    DEMOSOC_QSPI0,
    DEMOSOC_UART1,
    DEMOSOC_QSPI1,
    DEMOSOC_QSPI2,
    DEMOSOC_SMP,
    DEMOSOC_XIP,
    DEMOSOC_ILM,
    DEMOSOC_DLM,
    DEMOSOC_DDR
};

enum
{
    DEMOSOC_GPIO_IRQ0 = 1,
    DEMOSOC_GPIO_IRQ1 = 2,
    DEMOSOC_GPIO_IRQ2 = 3,
    DEMOSOC_GPIO_IRQ3 = 4,
    DEMOSOC_GPIO_IRQ4 = 5,
    DEMOSOC_GPIO_IRQ5 = 6,
    DEMOSOC_GPIO_IRQ6 = 7,
    DEMOSOC_GPIO_IRQ7 = 8,
    DEMOSOC_GPIO_IRQ8 = 9,
    DEMOSOC_GPIO_IRQ9 = 10,
    DEMOSOC_GPIO_IRQ10 = 11,
    DEMOSOC_GPIO_IRQ11 = 12,
    DEMOSOC_GPIO_IRQ12 = 13,
    DEMOSOC_GPIO_IRQ13 = 14,
    DEMOSOC_GPIO_IRQ14 = 15,
    DEMOSOC_GPIO_IRQ15 = 16,
    DEMOSOC_GPIO_IRQ16 = 17,
    DEMOSOC_GPIO_IRQ17 = 18,
    DEMOSOC_GPIO_IRQ18 = 19,
    DEMOSOC_GPIO_IRQ19 = 20,
    DEMOSOC_GPIO_IRQ20 = 21,
    DEMOSOC_GPIO_IRQ21 = 22,
    DEMOSOC_GPIO_IRQ22 = 23,
    DEMOSOC_GPIO_IRQ23 = 24,
    DEMOSOC_GPIO_IRQ24 = 25,
    DEMOSOC_GPIO_IRQ25 = 26,
    DEMOSOC_GPIO_IRQ26 = 27,
    DEMOSOC_GPIO_IRQ27 = 28,
    DEMOSOC_GPIO_IRQ28 = 29,
    DEMOSOC_GPIO_IRQ29 = 30,
    DEMOSOC_GPIO_IRQ30 = 31,
    DEMOSOC_GPIO_IRQ31 = 32,
    DEMOSOC_UART0_IRQ = 33,
    DEMOSOC_UART1_IRQ = 34,
    DEMOSOC_SPI0_IRQ = 35,
    DEMOSOC_SPI1_IRQ = 36,
    DEMOSOC_SPI2_IRQ = 37,
    DEMOSOC_DEV_INT_MAX
};

enum {
    DEMOSOC_INT19_IRQn           = 19,                /*!< Device Interrupt */
    DEMOSOC_INT20_IRQn           = 20,                /*!< Device Interrupt */
    DEMOSOC_INT21_IRQn           = 21,                /*!< Device Interrupt */
    DEMOSOC_INT22_IRQn           = 22,                /*!< Device Interrupt */
    DEMOSOC_INT23_IRQn           = 23,                /*!< Device Interrupt */
    DEMOSOC_INT24_IRQn           = 24,                /*!< Device Interrupt */
    DEMOSOC_INT25_IRQn           = 25,                /*!< Device Interrupt */
    DEMOSOC_INT26_IRQn           = 26,                /*!< Device Interrupt */
    DEMOSOC_INT27_IRQn           = 27,                /*!< Device Interrupt */
    DEMOSOC_INT28_IRQn           = 28,                /*!< Device Interrupt */
    DEMOSOC_INT29_IRQn           = 29,                /*!< Device Interrupt */
    DEMOSOC_INT30_IRQn           = 30,                /*!< Device Interrupt */
    DEMOSOC_INT31_IRQn           = 31,                /*!< Device Interrupt */
    DEMOSOC_INT32_IRQn           = 32,                /*!< Device Interrupt */
    DEMOSOC_INT33_IRQn           = 33,                /*!< Device Interrupt */
    DEMOSOC_INT34_IRQn           = 34,                /*!< Device Interrupt */
    DEMOSOC_INT35_IRQn           = 35,                /*!< Device Interrupt */
    DEMOSOC_INT36_IRQn           = 36,                /*!< Device Interrupt */
    DEMOSOC_INT37_IRQn           = 37,                /*!< Device Interrupt */
    DEMOSOC_INT38_IRQn           = 38,                /*!< Device Interrupt */
    DEMOSOC_INT39_IRQn           = 39,                /*!< Device Interrupt */
    DEMOSOC_INT40_IRQn           = 40,                /*!< Device Interrupt */
    DEMOSOC_INT41_IRQn           = 41,                /*!< Device Interrupt */
    DEMOSOC_INT42_IRQn           = 42,                /*!< Device Interrupt */
    DEMOSOC_INT43_IRQn           = 43,                /*!< Device Interrupt */
    DEMOSOC_INT44_IRQn           = 44,                /*!< Device Interrupt */
    DEMOSOC_INT45_IRQn           = 45,                /*!< Device Interrupt */
    DEMOSOC_INT46_IRQn           = 46,                /*!< Device Interrupt */
    DEMOSOC_INT47_IRQn           = 47,                /*!< Device Interrupt */
    DEMOSOC_INT48_IRQn           = 48,                /*!< Device Interrupt */
    DEMOSOC_INT49_IRQn           = 49,                /*!< Device Interrupt */
    DEMOSOC_INT50_IRQn           = 50,                /*!< Device Interrupt */
    DEMOSOC_INT_MAX,
};

enum
{
    DEMOSOC_HFCLK_FREQ = 8000000,
    DEMOSOC_RTCCLK_FREQ = 1000000
};


#define DEMOSOC_MANAGEMENT_CPU_COUNT 1
#define DEMOSOC_COMPUTE_CPU_COUNT 16

#define DEMOSOC_PLIC_HART_CONFIG "MS"
#define DEMOSOC_PLIC_NUM_SOURCES 54
#define DEMOSOC_PLIC_NUM_PRIORITIES 7
#define DEMOSOC_PLIC_PRIORITY_BASE 0x04
#define DEMOSOC_PLIC_PENDING_BASE 0x1000
#define DEMOSOC_PLIC_ENABLE_BASE 0x2000
#define DEMOSOC_PLIC_ENABLE_STRIDE 0x80
#define DEMOSOC_PLIC_CONTEXT_BASE 0x200000
#define DEMOSOC_PLIC_CONTEXT_STRIDE 0x1000

#if defined(TARGET_RISCV32)
#define DEMOSOC_CPU TYPE_RISCV_CPU_NUCLEI_N307FD
#elif defined(TARGET_RISCV64)
#define DEMOSOC_CPU TYPE_RISCV_CPU_NUCLEI_NX600FD
#endif

#endif
