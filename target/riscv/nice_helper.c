/*
 * RISC-V Nuclei Custom Nice instructinos Helper for QEMU.
 *
 * Copyright (c) 2024 Nuclei
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/cpu-common.h"

target_ulong nice_buf[3] = {0};

void HELPER(lbuf)(target_ulong rs1)
{
#ifndef CONFIG_USER_ONLY
    cpu_physical_memory_rw(rs1, &nice_buf[0], 4, 0);
    cpu_physical_memory_rw(rs1 + 4, &nice_buf[1], 4, 0);
    cpu_physical_memory_rw(rs1 + 8,  &nice_buf[2], 4, 0);
#endif
}

void HELPER(sbuf)(target_ulong rs1)
{
#ifndef CONFIG_USER_ONLY
    cpu_physical_memory_rw(rs1, &nice_buf[0], 4, 1);
    cpu_physical_memory_rw(rs1 + 4, &nice_buf[1], 4, 1);
    cpu_physical_memory_rw(rs1 + 8,  &nice_buf[2], 4, 1);
#endif
}

target_ulong HELPER(rowsum)(target_ulong rs1)
{
#ifndef CONFIG_USER_ONLY
    target_ulong temp_buf[3] = {0};

    cpu_physical_memory_rw(rs1, &temp_buf[0], 4, 0);
    nice_buf[0] = nice_buf[0] + temp_buf[0];
    cpu_physical_memory_rw(rs1 + 4, &temp_buf[1], 4, 0);
    nice_buf[1] = nice_buf[1] + temp_buf[1];
    cpu_physical_memory_rw(rs1 + 8,  &temp_buf[2], 4, 0);
    nice_buf[2] = nice_buf[2] + temp_buf[2];

    return temp_buf[0] + temp_buf[1] + temp_buf[2];
#endif
    return 0;
}

