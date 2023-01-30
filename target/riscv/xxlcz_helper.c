/*
 * RISC-V Nuclei Xxlcz Extension Helpers for QEMU.
 *
 * Copyright (c) 2021 Nuclei.
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

target_ulong HELPER(xl_extract)(target_ulong a, target_ulong b, target_ulong c)
{
    uint32_t is2 = c & 0x1F;
    uint32_t is3 = b & 0x1F;
    uint32_t msb = is2 + is3;
    int32_t ret;

    msb = msb > 31 ? 31 : msb;
    ret = ((int32_t)a << (31 - msb)) >> (31 + is2 - msb);
    return  ret;
}

target_ulong HELPER(xl_extractr)(target_ulong a, target_ulong b)
{
    uint32_t is2 = b & 0x1F;
    uint32_t is3 = (b >> 5) & 0x1F;
    uint32_t msb = is2 + is3;
    int32_t ret;

    msb = msb > 31 ? 31 : msb;
    ret = ((int32_t)a << (31 - msb)) >> (31 + is2 - msb);
    return  ret;
}

target_ulong HELPER(xl_extractu)(target_ulong a, target_ulong b, target_ulong c)
{
    uint32_t is2 = c & 0x1F;
    uint32_t is3 = b & 0x1F;
    uint32_t msb = is2 + is3;
    uint32_t ret;

    msb = msb > 31 ? 31 : msb;
    ret = ((uint32_t)a << (31 - msb)) >> (31 + is2 - msb);
    return  ret;
}

target_ulong HELPER(xl_extractur)(target_ulong a, target_ulong b)
{
    uint32_t is2 = b & 0x1F;
    uint32_t is3 = (b >> 5) & 0x1F;
    uint32_t msb = is2 + is3;
    uint32_t ret;

    msb = msb > 31 ? 31 : msb;
    ret = ((uint32_t)a << (31 - msb)) >> (31 + is2 - msb);
    return  ret;
}

target_ulong HELPER(xl_insert)(target_ulong a, target_ulong b, target_ulong c, target_ulong d)
{
    uint32_t is2 = b & 0x1F;
    uint32_t is3 = c & 0x1F;
    uint32_t lsb = is2 + is3 > 31 ? is2 + is3 - 32 : 0;
    uint32_t mask = (~(0xfffffffe << is3)) << is2;
    uint32_t field = a << (is2 - lsb);
    return (d & ~mask) | (field & mask);
}

target_ulong HELPER(xl_insertr)(target_ulong a, target_ulong b, target_ulong c)
{
    uint32_t is2 = b & 0x1F;
    uint32_t is3 = (b >> 5) & 0x1F;
    uint32_t lsb = is2 + is3 > 31 ? is2 + is3 - 32 : 0;
    uint32_t mask = (~(0xfffffffe << is3)) << is2;
    uint32_t field = a << (is2 - lsb);
    return (c & ~mask) | (field & mask);
}

target_ulong HELPER(xl_bclr)(target_ulong a, target_ulong b, target_ulong c)
{
    uint32_t is2 = c & 0x1F;
    uint32_t is3 = b & 0x1F;
    uint32_t mask = (~(0xfffffffe << is3)) << is2;
    return a & ~mask;
}

target_ulong HELPER(xl_bclrr)(target_ulong a, target_ulong b)
{
    uint32_t is2 = b & 0x1F;
    uint32_t is3 = (b >> 5) & 0x1F;
    uint32_t mask = (~(0xfffffffe << is3)) << is2;
    return a & ~mask;
}

target_ulong HELPER(xl_bset)(target_ulong a, target_ulong b, target_ulong c)
{
    uint32_t is2 = c & 0x1F;
    uint32_t is3 = b & 0x1F;
    uint32_t mask = (~(0xfffffffe << is3)) << is2;
    return a | mask;
}

target_ulong HELPER(xl_bsetr)(target_ulong a, target_ulong b)
{
    uint32_t is2 = b & 0x1F;
    uint32_t is3 = (b >> 5) & 0x1F;
    uint32_t mask = (~(0xfffffffe << is3)) << is2;
    return a | mask;
}

static target_ulong do_clz(target_ulong a)
{
    int i;

    for (i = 0; i < 32; i++) {
        if (a & (1 << (31 - i))) {
            break;
        }
    }
    return i;
}

target_ulong HELPER(xl_clb)(target_ulong a)
{
    target_ulong  t = a & (1 << 31) ? ~a : a;
    return a == 0 ? 0 : do_clz(t) - 1;
}

target_ulong HELPER(xl_fl1)(target_ulong a)
{
    target_ulong t = do_clz(a);
    return t == 32 ? 32 : 31 - t;
}

target_ulong HELPER(xl_fl0)(target_ulong a)
{
    int i;
    target_ulong  t = 32 ? 32 : 31 - do_clz(a);

    for (i = 1; i <= t; i++) {
        if (!(a & (1 << (t - i)))) {
            break;
        }
    }
    return t-i;
}

target_ulong HELPER(xl_ff0)(target_ulong a)
{
    int i;

    for (i = 0; i < 32; i++) {
        if (!(a & (1 << i))) {
            break;
        }
    }
    return i;
}

static uint32_t revpowerbits(uint32_t x, uint32_t shamt)
{
    if (shamt &  1) {
        x = ((x & 0x55555555) <<  1) | ((x & 0xAAAAAAAA) >>  1);
    }

    if (shamt &  2) {
        x = ((x & 0x33333333) <<  2) | ((x & 0xCCCCCCCC) >>  2);
    }

    if (shamt &  4) {
        x = ((x & 0x0F0F0F0F) <<  4) | ((x & 0xF0F0F0F0) >>  4);
    }

    if (shamt &  8) {
        x = ((x & 0x00FF00FF) <<  8) | ((x & 0xFF00FF00) >>  8);
    }

    if (shamt & 16) {
        x = ((x & 0x0000FFFF) << 16) | ((x & 0xFFFF0000) >> 16);
    }
    return x;
}

static uint32_t rev3bits(uint32_t rs1)
{
    uint32_t x = rs1 >> 5;

    x = ((x & 0b111000000111000000111000000LL) >> 6) |
        ((x & 0b000111000000111000000111000LL)) |
        ((x & 0b000000111000000111000000111LL) << 6);
    x = ((x & 0b111111111000000000000000000LL) >> 18) |
        ((x & 0b000000000111111111000000000LL)) |
        ((x & 0b000000000000000000111111111LL) << 18);
    x |= (rs1 & 0x3) << 30;
    x |= (rs1 & 0x1C) << 25;
    return x;
}

target_ulong HELPER(xl_bitrev)(target_ulong a, target_ulong b)
{
    uint32_t is2 = b & 0x1F;
    uint32_t is3 = (b >> 5) & 0x3;
    uint32_t res = a << is2;

    switch (is3) {
    case 0:
        res = revpowerbits(res, 0b11111);
        break;
    case 1:
        res = revpowerbits(res, 0b11110);
        break;
    default:
        res = rev3bits(res);
        break;
    }
    return res;
}

target_ulong HELPER(xl_addrchk)(target_ulong a, target_ulong b)
{
    if((a | b) & 0x3)
        return 1;
    else
        return 0;
}

target_ulong HELPER(xl_bnezm)(target_ulong a, target_ulong b)
{
    uint8_t *p = &a;
    uint8_t i = 0;

    if(a != b)
        return 0;
    while(i < sizeof(target_ulong))
    {
        if(p[i] == 0)
            return 0;
        i++;
    }
    return 1;
}

target_ulong HELPER(xl_nzmsk)(target_ulong a, target_ulong b)
{
    uint8_t *p = &a, *q = &b;
    uint8_t i = 1;
    p[0] = 0xff;
    while(i < 3)
    {
        p[i] = (q[i-1] == 0? 0 : 0xff) & p[i-1];
        i++;
    }
    return a;
}

target_ulong HELPER(xl_ffnz)(target_ulong a, target_ulong b)
{
    uint8_t *p = &b;
    uint8_t i = 0;
    while(i < 4)
    {
        if(p[i] != 0)
        {
            a = (uint32_t)p[i];
            break;
        }
        i++;
    }
    return a;
}