/*
 * nice_api.h — Interface between the nice QEMU plugin and
 *                     user-provided instruction handler libraries.
 *
 * Users include this header in their own .c file, define their handler
 * functions, fill the nice_defs[] table, and compile to a .so:
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NICE_API_H
#define NICE_API_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Data type usage guide
 *
 * Integer types (int, long, int32_t, uint32_t, etc.):
 *   No special handling needed.  All integers are passed as uint64_t and the
 *   handler casts internally:
 *     static uint64_t insn_sadd(uint64_t a, uint64_t b) {
 *         return (uint64_t)(int32_t)((int32_t)a + (int32_t)b);
 *     }
 *   Table entry: { opcode, funct7, rs1_enc, rs2_enc, funct3, fn }
 *   (call_type omitted → defaults to CI_CALL_INT = 0)
 *
 * Float (FPU registers, is_fpu=1, funct7 bit5=1):
 *   Set call_type = CI_CALL_F32.  Write the handler with native float types;
 *   the framework converts FPR bit-patterns ↔ float automatically:
 *     static float insn_fadd(float a, float b) { return a + b; }
 *   Table entry: { opcode, funct7, rs1_enc, rs2_enc, funct3, fn, CI_CALL_F32 }
 *
 * Double (FPU registers, 64-bit):
 *   Set call_type = CI_CALL_F64.  Same pattern with double:
 *     static double insn_dadd(double a, double b) { return a + b; }
 *   Table entry: { ..., fn, CI_CALL_F64 }
 * ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * call_type constants — select handler calling convention
 * ----------------------------------------------------------------------- */

/** CI_CALL_INT: handler signature uint64_t fn(uint64_t...) — default (0). */
#define CI_CALL_INT  0

/** CI_CALL_F32: handler signature float fn(float...).
 *  The framework extracts float values from FPR bit-patterns before the call
 *  and NaN-boxes the float result before writing back to the FPR. */
#define CI_CALL_F32  1

/** CI_CALL_F64: handler signature double fn(double...). */
#define CI_CALL_F64  2

/* -----------------------------------------------------------------------
 * Sentinel / wildcard constants
 * ----------------------------------------------------------------------- */

/** CI_TABLE_END: place in opcode_type to terminate nice_defs[]. */
#define CI_TABLE_END  0xFF

/**
 * CI_ANY: use in rs1_enc or rs2_enc to skip that field during matching.
 * (Valid register indices are 0–31, so 0xFF is always a safe wildcard.)
 */
#define CI_ANY  0xFF

/* -----------------------------------------------------------------------
 * Instruction definition table entry
 *
 * Matching key: opcode_type + funct7 + (optional) rs1_enc + rs2_enc + funct3
 *
 *   opcode_type  Full 7-bit opcode: 0x0b=custom-0, 0x2b=custom-1,
 *                0x5b=custom-2, 0x7b=custom-3
 *                CI_TABLE_END (0xFF) = end-of-table sentinel
 *
 *   funct7       Exact match of the 7-bit funct7 field.
 *                Type bits: bit6=is_mac, bit5=is_fpu, bit4=is_pair
 *
 *   rs1_enc      When xs1=0, the rs1 bit-field [19:15] is free as extra encoding.
 *                Set to the expected value (0–31) to match, or CI_ANY to skip.
 *
 *   rs2_enc      Same as rs1_enc but for rs2 [24:20] when xs2=0.
 *
 *   funct3       Actual instruction funct3 = xd[2] | xs1[1] | xs2[0].
 *                Used both as a match filter and to determine arg count:
 *                  bit2=xd  bit1=xs1  bit0=xs2
 *                  CI_NARGS_VEC (0xFE) → vector mode, fn is ci_fn_vec_t
 *
 *   fn           Function pointer.  Cast determined by call_type (see below).
 *
 *   call_type    CI_CALL_INT (0, default): uint64_t fn(uint64_t...)
 *                CI_CALL_F32 (1):          float    fn(float...)
 *                CI_CALL_F64 (2):          double   fn(double...)
 *                May be omitted; C zero-initialises the field → CI_CALL_INT.
 * ----------------------------------------------------------------------- */
typedef struct {
    uint8_t  opcode_type;
    uint8_t  funct7;
    uint8_t  rs1_enc;
    uint8_t  rs2_enc;
    uint8_t  funct3;
    void    *fn;
    uint8_t  call_type;   /* CI_CALL_INT / CI_CALL_F32 / CI_CALL_F64 */
} nice_def_t;

/**
 * Your shared library MUST export this symbol.
 * Terminate the array with an entry whose opcode_type == CI_TABLE_END.
 */
extern nice_def_t nice_defs[];

#endif /* NICE_API_H */
