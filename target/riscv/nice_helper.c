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
#include "internals.h"

uint32_t nice_buf[3] = {0};

static inline uint32_t nice_ld32(CPURISCVState *env, target_ulong addr,
                                 uintptr_t ra)
{
    return mo_endian_env(env) == MO_BE ?
           cpu_ldl_be_data_ra(env, addr, ra) :
           cpu_ldl_le_data_ra(env, addr, ra);
}

static inline void nice_st32(CPURISCVState *env, target_ulong addr,
                             uint32_t value, uintptr_t ra)
{
    if (mo_endian_env(env) == MO_BE) {
        cpu_stl_be_data_ra(env, addr, value, ra);
    } else {
        cpu_stl_le_data_ra(env, addr, value, ra);
    }
}

void HELPER(lbuf)(CPURISCVState *env, target_ulong rs1)
{
#ifndef CONFIG_USER_ONLY
    nice_buf[0] = nice_ld32(env, rs1, GETPC());
    nice_buf[1] = nice_ld32(env, rs1 + 4, GETPC());
    nice_buf[2] = nice_ld32(env, rs1 + 8, GETPC());
#endif
}

void HELPER(sbuf)(CPURISCVState *env, target_ulong rs1)
{
#ifndef CONFIG_USER_ONLY
    nice_st32(env, rs1, nice_buf[0], GETPC());
    nice_st32(env, rs1 + 4, nice_buf[1], GETPC());
    nice_st32(env, rs1 + 8, nice_buf[2], GETPC());
#endif
}

target_ulong HELPER(rowsum)(CPURISCVState *env, target_ulong rs1)
{
#ifndef CONFIG_USER_ONLY
    uint32_t temp_buf[3] = {0};

    temp_buf[0] = nice_ld32(env, rs1, GETPC());
    nice_buf[0] = nice_buf[0] + temp_buf[0];
    temp_buf[1] = nice_ld32(env, rs1 + 4, GETPC());
    nice_buf[1] = nice_buf[1] + temp_buf[1];
    temp_buf[2] = nice_ld32(env, rs1 + 8, GETPC());
    nice_buf[2] = nice_buf[2] + temp_buf[2];

    return temp_buf[0] + temp_buf[1] + temp_buf[2];
#endif
    return 0;
}

