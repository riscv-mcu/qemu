/*
 * NUCLEI Test Finisher interface
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

#ifndef HW_NUCLEI_TEST_H
#define HW_NUCLEI_TEST_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_NUCLEI_TEST "riscv.nuclei.test"

typedef struct NucleiTestState NucleiTestState;
DECLARE_INSTANCE_CHECKER(NucleiTestState, NUCLEI_TEST,
                         TYPE_NUCLEI_TEST)

struct NucleiTestState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
};

enum {
    FINISHER_FAIL = 0x3333,
    FINISHER_PASS = 0x5555,
    FINISHER_RESET = 0x7777
};

DeviceState *nuclei_test_create(hwaddr addr);

#endif
