/*
 * Nuclei Logic General Purpose Input/Output Controller.
 *
 * Copyright (c) 2024 Nucleisys, Inc.
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

#ifndef NUCLEI_GPIO_H
#define NUCLEI_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_NUCLEI_GPIO "nuclei.gpio"
typedef struct NucleiGPIOState NucleiGPIOState;
DECLARE_INSTANCE_CHECKER(NucleiGPIOState, NUCLEI_GPIO,
                         TYPE_NUCLEI_GPIO)

#define NUCLEI_GPIO_PINS 32

#define NUCLEI_GPIO_SIZE 0x100

#define NUCLEI_GPIO_REG_VALUE      0x000
#define NUCLEI_GPIO_REG_INPUT_EN   0x004
#define NUCLEI_GPIO_REG_OUTPUT_EN  0x008
#define NUCLEI_GPIO_REG_PORT       0x00C
#define NUCLEI_GPIO_REG_PUE        0x010
#define NUCLEI_GPIO_REG_DS         0x014
#define NUCLEI_GPIO_REG_RISE_IE    0x018
#define NUCLEI_GPIO_REG_RISE_IP    0x01C
#define NUCLEI_GPIO_REG_FALL_IE    0x020
#define NUCLEI_GPIO_REG_FALL_IP    0x024
#define NUCLEI_GPIO_REG_HIGH_IE    0x028
#define NUCLEI_GPIO_REG_HIGH_IP    0x02C
#define NUCLEI_GPIO_REG_LOW_IE     0x030
#define NUCLEI_GPIO_REG_LOW_IP     0x034
#define NUCLEI_GPIO_REG_IOF_EN     0x038
#define NUCLEI_GPIO_REG_IOF_SEL    0x03C
#define NUCLEI_GPIO_REG_OUT_XOR    0x040

struct NucleiGPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    qemu_irq irq[NUCLEI_GPIO_PINS];
    qemu_irq output[NUCLEI_GPIO_PINS];

    uint32_t value;             /* Actual value of the pin */
    uint32_t input_en;
    uint32_t output_en;
    uint32_t port;              /* Pin value requested by the user */
    uint32_t pue;
    uint32_t ds;
    uint32_t rise_ie;
    uint32_t rise_ip;
    uint32_t fall_ie;
    uint32_t fall_ip;
    uint32_t high_ie;
    uint32_t high_ip;
    uint32_t low_ie;
    uint32_t low_ip;
    uint32_t iof_en;
    uint32_t iof_sel;
    uint32_t out_xor;
    uint32_t in;
    uint32_t in_mask;

    /* config */
    uint32_t ngpio;
};

#endif /* NUCLEI_GPIO_H */
