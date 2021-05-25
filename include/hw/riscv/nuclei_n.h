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
#ifndef HW_RISCV_NUCLEI_N_H
#define HW_RISCV_NUCLEI_N_H

#include "hw/riscv/riscv_hart.h"
#include "hw/char/nuclei_uart.h"
#include "hw/intc/nuclei_systimer.h"
#include "hw/gpio/sifive_gpio.h"
#include "hw/intc/nuclei_eclic.h"
#include "hw/sysbus.h"

#define TYPE_NUCLEI_N_SOC "riscv.nuclei.n.soc"
#define RISCV_NUCLEI_N_SOC(obj) \
    OBJECT_CHECK(NucleiNSoCState, (obj), TYPE_NUCLEI_N_SOC)

typedef struct NucleiNSoCState {
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

    DeviceState *timer;
    // NucLeiSYSTIMERState *timer;
    NucLeiUARTState uart;
    SIFIVEGPIOState gpio;

} NucleiNSoCState;

typedef struct
{
    /*< private >*/
    SysBusDevice parent_obj;

    const char *download;
    /*< public >*/
    NucleiNSoCState soc;
} NucleiNState;

#define TYPE_RISCV_NUCLEI_N_MACHINE MACHINE_TYPE_NAME("nuclei_n")
#define RISCV_NUCLEI_N_MACHINE(obj) \
    OBJECT_CHECK(NucleiNState, (obj), TYPE_RISCV_NUCLEI_N_MACHINE)

enum {
    NUCLEI_N_DEV_DEBUG,
    NUCLEI_N_DEV_ROM,
    NUCLEI_N_DEV_TIMER,
    NUCLEI_N_DEV_ECLIC,
    NUCLEI_N_DEV_GPIO,
    NUCLEI_N_DEV_UART0,
    NUCLEI_N_DEV_QSPI0,
    NUCLEI_N_DEV_PWM0,
    NUCLEI_N_DEV_UART1,
    NUCLEI_N_DEV_QSPI1,
    NUCLEI_N_DEV_PWM1,
    NUCLEI_N_DEV_QSPI2,
    NUCLEI_N_DEV_PWM2,
    NUCLEI_N_DEV_XIP,
    NUCLEI_N_DEV_ILM,
    NUCLEI_N_DEV_DLM,
    NUCLEI_N_DEV_DDR
};

enum {
    NUCLEI_N_INT19_IRQn           = 19,                /*!< Device Interrupt */
    NUCLEI_N_INT20_IRQn           = 20,                /*!< Device Interrupt */
    NUCLEI_N_INT21_IRQn           = 21,                /*!< Device Interrupt */
    NUCLEI_N_INT22_IRQn           = 22,                /*!< Device Interrupt */
    NUCLEI_N_INT23_IRQn           = 23,                /*!< Device Interrupt */
    NUCLEI_N_INT24_IRQn           = 24,                /*!< Device Interrupt */
    NUCLEI_N_INT25_IRQn           = 25,                /*!< Device Interrupt */
    NUCLEI_N_INT26_IRQn           = 26,                /*!< Device Interrupt */
    NUCLEI_N_INT27_IRQn           = 27,                /*!< Device Interrupt */
    NUCLEI_N_INT28_IRQn           = 28,                /*!< Device Interrupt */
    NUCLEI_N_INT29_IRQn           = 29,                /*!< Device Interrupt */
    NUCLEI_N_INT30_IRQn           = 30,                /*!< Device Interrupt */
    NUCLEI_N_INT31_IRQn           = 31,                /*!< Device Interrupt */
    NUCLEI_N_INT32_IRQn           = 32,                /*!< Device Interrupt */
    NUCLEI_N_INT33_IRQn           = 33,                /*!< Device Interrupt */
    NUCLEI_N_INT34_IRQn           = 34,                /*!< Device Interrupt */
    NUCLEI_N_INT35_IRQn           = 35,                /*!< Device Interrupt */
    NUCLEI_N_INT36_IRQn           = 36,                /*!< Device Interrupt */
    NUCLEI_N_INT37_IRQn           = 37,                /*!< Device Interrupt */
    NUCLEI_N_INT38_IRQn           = 38,                /*!< Device Interrupt */
    NUCLEI_N_INT39_IRQn           = 39,                /*!< Device Interrupt */
    NUCLEI_N_INT40_IRQn           = 40,                /*!< Device Interrupt */
    NUCLEI_N_INT41_IRQn           = 41,                /*!< Device Interrupt */
    NUCLEI_N_INT42_IRQn           = 42,                /*!< Device Interrupt */
    NUCLEI_N_INT43_IRQn           = 43,                /*!< Device Interrupt */
    NUCLEI_N_INT44_IRQn           = 44,                /*!< Device Interrupt */
    NUCLEI_N_INT45_IRQn           = 45,                /*!< Device Interrupt */
    NUCLEI_N_INT46_IRQn           = 46,                /*!< Device Interrupt */
    NUCLEI_N_INT47_IRQn           = 47,                /*!< Device Interrupt */
    NUCLEI_N_INT48_IRQn           = 48,                /*!< Device Interrupt */
    NUCLEI_N_INT49_IRQn           = 49,                /*!< Device Interrupt */
    NUCLEI_N_INT50_IRQn           = 50,                /*!< Device Interrupt */
    NUCLEI_N_INT_MAX,
};

#if defined(TARGET_RISCV32)
#define NUCLEI_N_CPU TYPE_RISCV_CPU_NUCLEI_N307FD
#elif defined(TARGET_RISCV64)
#define NUCLEI_N_CPU TYPE_RISCV_CPU_NUCLEI_NX600FD
#endif

#endif