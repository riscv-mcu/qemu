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

/**
 * CI_CALL_VEC: handler signature  void fn(qemu_plugin_nice_info_t *info)
 *
 * The handler accesses vector state directly through info pointers:
 *   info->rd_vreg              vd register data (writable, info->vlenb bytes)
 *   info->rs1_vreg/rs2_vreg   vs1/vs2 source data (valid when xs1/xs2=1)
 *   info->v0_vreg              v0 mask register (check info->vm: 0=masked)
 *   info->vl/vsew/vlmul/vta/vma/vstart  current vector execution context
 *   info->vlenb                runtime bytes per vector register
 *
 * All vreg pointers are NULL when the V extension is not enabled.
 * result_is_vec is set automatically by nice.c — the handler need not set it.
 *
 * xd/xs1/xs2 keep their standard meanings:
 *   xd=1   rd carries the vd destination register index
 *   xs1=1  rs1 carries the vs1 source register index
 *   xs2=1  rs2 carries the vs2 source register index
 *
 * vm encoding convention: funct7[0]=1 → unmasked, funct7[0]=0 → masked (v0).
 *
 * vsew_enc in nice_def_t is a bitmask of accepted element widths.
 * Use NICE_VSEW_E* constants (or CI_ANY for any width).
 * The handler reads info->vsew to determine the actual element width.
 *
 * Recommended opcode: custom-2 (0x5b) for vector NICE instructions.
 */
#define CI_CALL_VEC  3

/* -----------------------------------------------------------------------
 * Sentinel / wildcard constants
 * ----------------------------------------------------------------------- */

/** CI_TABLE_END: place in opcode_type to terminate nice_defs[]. */
#define CI_TABLE_END  0xFF

/**
 * CI_ANY: use in rs1_enc, rs2_enc, or vsew_enc to skip matching on that field.
 * (Valid register indices are 0–31 and valid vsew values are 0–3, so 0xFF is
 * always a safe wildcard for both.)
 */
#define CI_ANY  0xFF

/* -----------------------------------------------------------------------
 * vsew_enc bitmask constants (CI_CALL_VEC only)
 *
 * Set the corresponding bit to allow that element width in vsew_enc.
 * Multiple bits = handler accepts multiple widths.
 * CI_ANY (0xFF) = match any SEW (all bits set).
 *
 * Example:
 *   { 0x5b, 0x01, CI_ANY, CI_ANY, 7, handler_i32, CI_CALL_VEC, NICE_VSEW_E32 }
 *   { 0x5b, 0x01, CI_ANY, CI_ANY, 7, handler_any, CI_CALL_VEC, CI_ANY }
 * ----------------------------------------------------------------------- */
#define NICE_VSEW_E8   (1u << 0)  /* SEW = 8-bit  */
#define NICE_VSEW_E16  (1u << 1)  /* SEW = 16-bit */
#define NICE_VSEW_E32  (1u << 2)  /* SEW = 32-bit */
#define NICE_VSEW_E64  (1u << 3)  /* SEW = 64-bit */

/* -----------------------------------------------------------------------
 * qemu_plugin_nice_info_t — decoded context passed to all NICE handlers
 *
 * This is the authoritative definition, shared by:
 *   - user handler libraries (include nice_api.h only; no glib needed)
 *   - qemu-plugin.h (includes this header, so QEMU internals see the same type)
 *
 * Fields populated by QEMU before calling the handler:
 *
 *   Scalar (all call types):
 *     opcode_type  — full 7-bit opcode (0x0b/0x2b/0x5b)
 *     funct7       — funct7 field of the instruction
 *     rd/rs1/rs2   — register indices
 *     xd/xs1/xs2   — operand-enable flags
 *     is_mac/is_fpu/is_pair — type flags from funct7[6:4]
 *     rs1_val/rs2_val/rd_val — GPR or FPR values
 *     rs1_val_hi/rs2_val_hi/rd_val_hi — high halves for PAIR type
 *
 *   Vector (CI_CALL_VEC only; NULL / 0 when V extension is not enabled):
 *     rd_vreg   — vd  destination register data (writable, vlenb bytes)
 *     rs1_vreg  — vs1 source register data (valid when xs1=1)
 *     rs2_vreg  — vs2 source register data (valid when xs2=1)
 *     v0_vreg   — v0  mask register data (valid when vm=0)
 *     vl        — current vector length
 *     vsew      — element width: 0=e8 1=e16 2=e32 3=e64
 *     vlmul     — LMUL (signed, -3..+3)
 *     vta/vma   — tail/mask agnostic flags
 *     vstart    — first active element
 *     vlenb     — bytes per vector register
 *
 *   Output (CI_CALL_VEC handler writes to vreg pointers directly;
 *           CI_CALL_INT/F32/F64 handler writes to info->result):
 *     result        — scalar result (written to rd if xd=1)
 *     result_hi     — high half for PAIR type
 *     result_is_vec — set automatically by nice.c; do NOT set manually
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t insn;
    uint8_t  opcode_type;
    uint8_t  funct7;
    uint8_t  rd;
    uint8_t  rs1;
    uint8_t  rs2;
    uint8_t  xd;
    uint8_t  xs1;
    uint8_t  xs2;
    uint8_t  is_mac;
    uint8_t  is_fpu;
    uint8_t  is_pair;
    uint64_t rs1_val;
    uint64_t rs2_val;
    uint64_t rd_val;
    uint64_t rs1_val_hi;
    uint64_t rs2_val_hi;
    uint64_t rd_val_hi;
    uint64_t result;
    uint64_t result_hi;
    uint8_t   result_is_vec;
    uint8_t   vm;
    uint8_t   vsew;
    int8_t    vlmul;
    uint8_t   vta;
    uint8_t   vma;
    uint8_t   vill;
    uint32_t  vlenb;
    uint64_t  vl;
    uint64_t  vtype;
    uint64_t  vstart;
    uint64_t *rs1_vreg;
    uint64_t *rs2_vreg;
    uint64_t *rd_vreg;
    uint64_t *v0_vreg;
} qemu_plugin_nice_info_t;

/* -----------------------------------------------------------------------
 * Instruction definition table entry
 *
 * Matching key: opcode_type + funct7 + (optional) rs1_enc + rs2_enc + funct3
 *               + (CI_CALL_VEC only) vsew_enc bitmask
 *
 *   opcode_type  Full 7-bit opcode: 0x0b=custom-0, 0x2b=custom-1,
 *                0x5b=custom-2, 0x7b=custom-3
 *                CI_TABLE_END (0xFF) = end-of-table sentinel
 *
 *   funct7       Exact match of the 7-bit funct7 field.
 *                Scalar type bits: bit6=is_mac, bit5=is_fpu, bit4=is_pair
 *                Vector convention: bit0=vm (1=unmasked, 0=masked with v0)
 *
 *   rs1_enc      When xs1=0, the rs1 bit-field [19:15] is free as extra encoding.
 *                Set to the expected value (0–31) to match, or CI_ANY to skip.
 *
 *   rs2_enc      Same as rs1_enc but for rs2 [24:20] when xs2=0.
 *
 *   funct3       Actual instruction funct3 = xd[2] | xs1[1] | xs2[0].
 *                  bit2=xd  bit1=xs1  bit0=xs2
 *
 *   fn           Function pointer.  Cast determined by call_type (see below).
 *
 *   call_type    CI_CALL_INT (0, default): uint64_t fn(uint64_t...)
 *                CI_CALL_F32 (1):          float    fn(float...)
 *                CI_CALL_F64 (2):          double   fn(double...)
 *                CI_CALL_VEC (3):          void     fn(qemu_plugin_nice_info_t *)
 *                May be omitted; C zero-initialises the field → CI_CALL_INT.
 *
 *   vsew_enc     CI_CALL_VEC only: bitmask of accepted SEW values (NICE_VSEW_E*).
 *                CI_ANY (0xFF) = accept any SEW.  Ignored for scalar call_types.
 *                May be omitted (C zero-init = 0 = only match e8).
 * ----------------------------------------------------------------------- */
typedef struct {
    uint8_t  opcode_type;
    uint8_t  funct7;
    uint8_t  rs1_enc;
    uint8_t  rs2_enc;
    uint8_t  funct3;
    void    *fn;
    uint8_t  call_type;  /* CI_CALL_INT / CI_CALL_F32 / CI_CALL_F64 / CI_CALL_VEC */
    uint8_t  vsew_enc;   /* CI_CALL_VEC: bitmask of accepted SEW widths (NICE_VSEW_E*) */
} nice_def_t;

/**
 * Your shared library MUST export this symbol.
 * Terminate the array with an entry whose opcode_type == CI_TABLE_END.
 */
extern nice_def_t nice_defs[];

#endif /* NICE_API_H */
