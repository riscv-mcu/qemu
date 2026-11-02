/*
 * RISC-V Nuclei Xxlcz Extension Helpers for QEMU.
 *
 * Copyright (c) 2024 Nuclei.
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

static inline target_ulong xl_low_mask(unsigned int width)
{
    return width == TARGET_LONG_BITS ? ~(target_ulong)0 :
                                      ((target_ulong)1 << width) - 1;
}

static inline target_ulong xl_field_mask(unsigned int width,
                                         unsigned int bottom)
{
    return xl_low_mask(MIN(width, TARGET_LONG_BITS - bottom)) << bottom;
}

static inline target_ulong xl_extract_field(target_ulong value,
                                            unsigned int width,
                                            unsigned int bottom,
                                            bool sign_extend)
{
    target_ulong field;
    target_ulong sign;

    if (width == 0) {
        return 0;
    }

    width = MIN(width, TARGET_LONG_BITS - bottom);
    field = (value >> bottom) & xl_low_mask(width);
    if (!sign_extend) {
        return field;
    }

    sign = (target_ulong)1 << (width - 1);
    return (field ^ sign) - sign;
}

static inline int xl_clz(target_ulong value)
{
#if TARGET_LONG_BITS == 64
    return clz64(value);
#else
    return clz32(value);
#endif
}

static inline int xl_ctz(target_ulong value)
{
#if TARGET_LONG_BITS == 64
    return ctz64(value);
#else
    return ctz32(value);
#endif
}

target_ulong HELPER(xl_extract)(target_ulong a, target_ulong b, target_ulong c)
{
    return xl_extract_field(a, b & 0x1f, c & 0x1f, true);
}

target_ulong HELPER(xl_extractr)(target_ulong a, target_ulong b)
{
    return xl_extract_field(a, (b >> 5) & 0x1f, b & 0x1f, true);
}

target_ulong HELPER(xl_extractu)(target_ulong a, target_ulong b, target_ulong c)
{
    return xl_extract_field(a, b & 0x1f, c & 0x1f, false);
}

target_ulong HELPER(xl_extractur)(target_ulong a, target_ulong b)
{
    return xl_extract_field(a, (b >> 5) & 0x1f, b & 0x1f, false);
}

target_ulong HELPER(xl_insert)(target_ulong a, target_ulong b, target_ulong c,
                               target_ulong d)
{
    unsigned int width = (b & 0x1f) + 1;
    unsigned int bottom = c & 0x1f;
    target_ulong mask = xl_field_mask(width, bottom);

    return (d & ~mask) | ((a << bottom) & mask);
}

target_ulong HELPER(xl_bclr)(target_ulong a, target_ulong b, target_ulong c)
{
    unsigned int bottom = c & 0x1f;
    unsigned int width = (b & 0x1f) + 1;

    return a & ~xl_field_mask(width, bottom);
}

target_ulong HELPER(xl_bclrr)(target_ulong a, target_ulong b)
{
    unsigned int bottom = b & 0x1f;
    unsigned int width = ((b >> 5) & 0x1f) + 1;

    return a & ~xl_field_mask(width, bottom);
}

target_ulong HELPER(xl_bset)(target_ulong a, target_ulong b, target_ulong c)
{
    unsigned int bottom = c & 0x1f;
    unsigned int width = (b & 0x1f) + 1;

    return a | xl_field_mask(width, bottom);
}

target_ulong HELPER(xl_bsetr)(target_ulong a, target_ulong b)
{
    unsigned int bottom = b & 0x1f;
    unsigned int width = ((b >> 5) & 0x1f) + 1;

    return a | xl_field_mask(width, bottom);
}

target_ulong HELPER(xl_clb)(target_ulong a)
{
    if (a == 0) {
        return 0;
    }
    return a >> (TARGET_LONG_BITS - 1) ? xl_clz(~a) : xl_clz(a);
}

target_ulong HELPER(xl_ff1)(target_ulong a)
{
    return a == 0 ? TARGET_LONG_BITS :
                    TARGET_LONG_BITS - 1 - xl_clz(a);
}

target_ulong HELPER(xl_ff0)(target_ulong a)
{
    target_ulong inverted = ~a;

    return inverted == 0 ? TARGET_LONG_BITS :
                           TARGET_LONG_BITS - 1 - xl_clz(inverted);
}

target_ulong HELPER(xl_fl0)(target_ulong a)
{
    return xl_ctz(~a);
}

target_ulong HELPER(xl_bitrev)(target_ulong a, target_ulong b)
{
    unsigned int shift = b & 0x1f;
    unsigned int group_bits = ((b >> 5) & 0x3) + 1;
    unsigned int low_bits = TARGET_LONG_BITS % group_bits;
    unsigned int src = 0;
    unsigned int dst = TARGET_LONG_BITS;
    target_ulong value = a << shift;
    target_ulong result = 0;

    if (low_bits) {
        target_ulong mask = xl_low_mask(low_bits);

        dst -= low_bits;
        result |= (value & mask) << dst;
        src += low_bits;
    }

    while (src < TARGET_LONG_BITS) {
        target_ulong mask = xl_low_mask(group_bits);

        dst -= group_bits;
        result |= ((value >> src) & mask) << dst;
        src += group_bits;
    }
    return result;
}

target_ulong HELPER(xl_addibne)(target_ulong rd, target_ulong rs1,
                                uint32_t increment, uint32_t distance,
                                target_ulong pc)
{
    if (rd + increment != rs1) {
        return pc - distance;
    }
    return pc + 4;
}

target_ulong HELPER(xl_addrchk)(target_ulong a, target_ulong b, int32_t offset,
                                target_ulong pc)
{
    target_ulong val = pc;
    target_ulong mask = TARGET_LONG_BITS == 64 ? 0x7 : 0x3;

    if ((a | b) & mask) {
        val += offset;
    } else {
        val += 4;
    }
    return val;
}

target_ulong HELPER(xl_bezm)(CPURISCVState *env, target_ulong a,
                             target_ulong b, int32_t offset,
                             target_ulong pc)
{
    const target_ulong byte_mask = TARGET_LONG_BITS == 64 ?
        (target_ulong)0x7f7f7f7f7f7f7f7fULL : 0x7f7f7f7f;
    target_ulong masked_a = a & byte_mask;
    target_ulong masked_b = b & byte_mask;
    target_ulong result = 0;
    bool branch = masked_a != masked_b;

    for (int i = 0; i < TARGET_LONG_BITS; i += 8) {
        uint8_t byte_a = masked_a >> i;
        uint8_t byte_b = masked_b >> i;

        result |= (target_ulong)(uint8_t)(byte_a - byte_b) << i;
        branch |= byte_a == 0;
    }
    env->gpr[31] = result;
    return branch ? pc + offset : pc + 4;
}

target_ulong HELPER(xl_nzmsk)(target_ulong a)
{
    target_ulong result = 0;

    for (int i = 0; i < TARGET_LONG_BITS; i += 8) {
        result |= (target_ulong)0xff << i;
        if (((a >> i) & 0xff) == 0) {
            break;
        }
    }
    return result;
}

target_ulong HELPER(xl_ffnz)(target_ulong a)
{
    for (int i = 0; i < TARGET_LONG_BITS; i += 8) {
        target_ulong byte = (a >> i) & 0xff;

        if (byte != 0) {
            return byte;
        }
    }
    return 0;
}
