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

#include "hw/riscv/riscv_hart.h"
#include "hw/char/nuclei_uart.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/sysbus.h"

#define TYPE_EVALSOC_SOC "riscv.evalsoc.soc"
#define RISCV_EVALSOC_SOC(obj) \
    OBJECT_CHECK(EvalSoCSoCState, (obj), TYPE_EVALSOC_SOC)

typedef struct EvalSoCSoCState {
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

} EvalSoCSoCState;

typedef struct
{
    /*< private >*/
    SysBusDevice parent_obj;

    const char *download;
    /*< public >*/
    EvalSoCSoCState soc;
} EvalSoCState;

#define TYPE_RISCV_EVALSOC_MACHINE MACHINE_TYPE_NAME("evalsoc")
#define RISCV_EVALSOC_MACHINE(obj) \
    OBJECT_CHECK(EvalSoCState, (obj), TYPE_RISCV_EVALSOC_MACHINE)

enum {
    EVALSOC_DEBUG,
    EVALSOC_MROM,
    EVALSOC_TEST,
    EVALSOC_TIMER,
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

#if defined(TARGET_RISCV32)
#define EVALSOC_CPU TYPE_RISCV_CPU_NUCLEI_N307FD
#elif defined(TARGET_RISCV64)
#define EVALSOC_CPU TYPE_RISCV_CPU_NUCLEI_NX600FD
#endif

#endif
