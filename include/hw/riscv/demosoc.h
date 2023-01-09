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

#include "hw/riscv/riscv_hart.h"
#include "hw/char/nuclei_uart.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/sysbus.h"

#define TYPE_DEMOSOC_SOC "riscv.demosoc.soc"
#define RISCV_DEMOSOC_SOC(obj) \
    OBJECT_CHECK(DemoSoCSoCState, (obj), TYPE_DEMOSOC_SOC)

typedef struct DemoSoCSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;

    DeviceState *eclic;
    MemoryRegion ilm;
    MemoryRegion dlm;
    MemoryRegion internal_rom;
    MemoryRegion xip_mem;
    MemoryRegion ddr;
    MemoryRegion smp;

    DeviceState *timer;
    // NucLeiSYSTIMERState *timer;
    NucLeiUARTState uart;
    SIFIVEGPIOState gpio;

} DemoSoCSoCState;

typedef struct
{
    /*< private >*/
    SysBusDevice parent_obj;

    const char *download;
    /*< public >*/
    DemoSoCSoCState soc;
} DemoSoCState;

#define TYPE_RISCV_DEMOSOC_MACHINE MACHINE_TYPE_NAME("demosoc")
#define RISCV_DEMOSOC_MACHINE(obj) \
    OBJECT_CHECK(DemoSoCState, (obj), TYPE_RISCV_DEMOSOC_MACHINE)

enum {
    DEMOSOC_DEBUG,
    DEMOSOC_MROM,
    DEMOSOC_TEST,
    DEMOSOC_TIMER,
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

#if defined(TARGET_RISCV32)
#define DEMOSOC_CPU TYPE_RISCV_CPU_NUCLEI_N307FD
#elif defined(TARGET_RISCV64)
#define DEMOSOC_CPU TYPE_RISCV_CPU_NUCLEI_NX600FD
#endif

#endif
