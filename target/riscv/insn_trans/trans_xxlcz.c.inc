/*
 * RISC-V translation routines for the Nuclei Xxlcz Extension.
 *
 * Copyright (c) 2021-2022 Nuclei
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

#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"

#define REQUIRE_XXLCZ(ctx) do {   \
    if (!ctx->cfg_ptr->ext_xxlcz) \
        return false;             \
} while (0)

static bool gen_load_pstinc(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_ZERO);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv addr = get_address(ctx, a->rs1, 0);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, memop);
    tcg_gen_addi_tl(src1, src1, a->imm);
    gen_set_gpr(ctx, a->rs1, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_xl_lb(DisasContext *ctx, arg_xl_lb *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_load_pstinc(ctx, a, MO_SB);
}

static bool trans_xl_lbu(DisasContext *ctx, arg_xl_lbu *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_load_pstinc(ctx, a, MO_UB);
}

static bool trans_xl_lh(DisasContext *ctx, arg_xl_lh *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_load_pstinc(ctx, a, MO_TESW);
}

static bool trans_xl_lhu(DisasContext *ctx, arg_xl_lhu *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_load_pstinc(ctx, a, MO_TEUW);
}

static bool trans_xl_lw(DisasContext *ctx, arg_xl_lw *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_load_pstinc(ctx, a, MO_TESL);
}
//RV64 
static bool trans_xl_lwu(DisasContext *ctx, arg_xl_lwu *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_load_pstinc(ctx, a, MO_TEUL);
}
//RV64
static bool trans_xl_ld(DisasContext *ctx, arg_xl_ld *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_load_pstinc(ctx, a, MO_TESQ);
}

static bool gen_store_pstinc(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv addr = get_address(ctx, a->rs1, 0);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);
    tcg_gen_addi_tl(src1, src1, a->imm);
    gen_set_gpr(ctx, a->rs1, src1);
    return true;
}

static bool trans_xl_sb(DisasContext *ctx, arg_xl_sb *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_store_pstinc(ctx, a, MO_SB);
}

static bool trans_xl_sh(DisasContext *ctx, arg_xl_sh *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_store_pstinc(ctx, a, MO_TESW);
}

static bool trans_xl_sw(DisasContext *ctx, arg_xl_sw *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_store_pstinc(ctx, a, MO_TESL);
}

static bool trans_xl_sd(DisasContext *ctx, arg_xl_sd *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_store_pstinc(ctx, a, MO_TESQ);
}

static bool gen_load_gp(DisasContext *ctx, arg_xl_lgp *a, int shift, MemOp memop)
{
    REQUIRE_XXLCZ(ctx);
    TCGv t0 = get_address(ctx, 3, a->imm << shift);
    TCGv t1 = tcg_temp_new();
    
    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, t1);
    return true;
}

static bool trans_xl_lgp_b(DisasContext *ctx, arg_xl_lgp_b *a)
{
    return gen_load_gp(ctx, a, 0, MO_SB);
}

static bool trans_xl_lgp_bu(DisasContext *ctx, arg_xl_lgp_bu *a)
{
    return gen_load_gp(ctx, a, 0, MO_UB);
}

static bool trans_xl_lgp_h(DisasContext *ctx, arg_xl_lgp_h *a)
{
    return gen_load_gp(ctx, a, 1, MO_SW);
}

static bool trans_xl_lgp_hu(DisasContext *ctx, arg_xl_lgp_hu *a)
{
    return gen_load_gp(ctx, a, 1, MO_UW);
}

static bool trans_xl_lgp_w(DisasContext *ctx, arg_xl_lgp_w *a)
{
    return gen_load_gp(ctx, a, 2, MO_SL);
}
//rv64 ONLY
static bool trans_xl_lgp_wu(DisasContext *ctx, arg_xl_lgp_wu *a)
{
    return gen_load_gp(ctx, a, 2, MO_UL);
}

//rv64 ONLY
static bool trans_xl_lgp_d(DisasContext *ctx, arg_xl_lgp_d *a)
{
    return gen_load_gp(ctx, a, 3, MO_SQ);
}

static bool gen_store_gp(DisasContext *ctx, arg_xl_sgp *a, int shift, MemOp memop)
{
    REQUIRE_XXLCZ(ctx);
    TCGv t0 = get_address(ctx, 3, a->imm << shift);
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);

    tcg_gen_qemu_st_tl(data, t0, ctx->mem_idx, MO_SB);
    return true;
}

static bool trans_xl_sgp_b(DisasContext *ctx, arg_xl_sgp_b *a)
{
    return gen_store_gp(ctx, a, 0, MO_SB);
}

static bool trans_xl_sgp_h(DisasContext *ctx, arg_xl_sgp_h *a)
{
    return gen_store_gp(ctx, a, 1, MO_SW);
}

static bool trans_xl_sgp_w(DisasContext *ctx, arg_xl_sgp_w *a)
{
    return gen_store_gp(ctx, a, 2, MO_SL);
}
//RV64
static bool trans_xl_sgp_d(DisasContext *ctx, arg_xl_sgp_d *a)
{
    return gen_store_gp(ctx, a, 3, MO_SQ);
}

static bool trans_xl_muli(DisasContext *ctx, arg_xl_muli *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    TCGv dest = dest_gpr(ctx, a->rd);
    
    tcg_gen_muli_tl(dest, src1, a->imm);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_xl_abs(DisasContext *ctx, arg_xl_abs *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_abs_tl);
}

static bool gen_bimm(DisasContext *ctx, arg_xl_brib *a, TCGCond cond)
{
    TCGLabel *l = gen_new_label();
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_ZERO);
    TCGv src2 = tcg_constant_tl(a->imm);

    tcg_gen_brcond_tl(cond, src1, src2, l);
    gen_goto_tb(ctx, 1, ctx->pc_succ_insn);

    gen_set_label(l); /* branch taken */

    if (!has_ext(ctx, RVC) && ((ctx->base.pc_next + a->ofst) & 0x1)) {
        /* misaligned */
        gen_exception_inst_addr_mis(ctx);
    } else {
        gen_goto_tb(ctx, 0, ctx->base.pc_next + (a->ofst));
    }
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_xl_beqi(DisasContext *ctx, arg_xl_beqi *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_bimm(ctx, a, TCG_COND_EQ);
}
static bool trans_xl_bnei(DisasContext *ctx, arg_xl_bnei *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_bimm(ctx, a, TCG_COND_NE);
}

static bool trans_xl_decbnez(DisasContext *ctx, arg_xl_decbnez *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv dest = dest_gpr(ctx, a->rd);
    gen_helper_xl_decbnez(cpu_pc, dest, tcg_constant_i32(a->scale), tcg_constant_i32(a->imm << 1), tcg_constant_i32(ctx->base.pc_next));
    tcg_gen_lookup_and_goto_ptr();

    tcg_gen_sub_tl(dest, dest, a->scale);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}


static bool trans_xl_addibne(DisasContext *ctx, arg_xl_addibne *a)
{
    REQUIRE_XXLCZ(ctx);
    //get rd
    TCGv dest = dest_gpr(ctx, a->rd);
    //get rs1
    TCGv rs1 = get_gpr(ctx, a->rs1, EXT_ZERO);

    uint32_t scale = a->scale;

    switch (scale)
    {
        case 0: scale = 1; break;
        case 1: scale = 2; break;
        case 2: scale = 4; break;
        case 3: scale = 8; break;
        default:
        return true;
    }
    gen_helper_xl_addibne(cpu_pc, dest, rs1, tcg_constant_i32(scale), tcg_constant_i32(a->imm << 1), tcg_constant_i32(ctx->base.pc_next));
    tcg_gen_addi_tl(dest, dest, scale);
    gen_set_gpr(ctx, a->rd, dest);
   
    tcg_gen_lookup_and_goto_ptr();
    return true;
    
}

static bool gen_slet(DisasContext *ctx, arg_r *a, TCGCond cond)
{
    REQUIRE_XXLCZ(ctx);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_ZERO);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_ZERO);
    TCGv dest = dest_gpr(ctx, a->rd);

    tcg_gen_setcond_tl(cond, dest, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_xl_slet(DisasContext *ctx, arg_xl_slet *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_slet(ctx, a, TCG_COND_LE);
}

static bool trans_xl_sletu(DisasContext *ctx, arg_xl_sletu *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_slet(ctx, a, TCG_COND_LEU);
}

/* xlcz bitop */
static bool trans_xl_extract(DisasContext *ctx, arg_xl_extract *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = tcg_constant_tl(a->imm1);
    TCGv src3 = tcg_constant_tl(a->imm2);

    gen_helper_xl_extract(dest, src1, src2, src3);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_xl_extractr(DisasContext *ctx, arg_xl_extractr *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_xl_extractr, NULL);
}

static bool trans_xl_extractu(DisasContext *ctx, arg_xl_extractu *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = tcg_constant_tl(a->imm1);
    TCGv src3 = tcg_constant_tl(a->imm2);

    gen_helper_xl_extractu(dest, src1, src2, src3);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_xl_extractur(DisasContext *ctx, arg_xl_extractur *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_xl_extractur, NULL);
}

static bool trans_xl_insert(DisasContext *ctx, arg_xl_insert *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv result = tcg_temp_new();
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    gen_helper_xl_insert(result, src1, tcg_constant_tl(a->imm1), tcg_constant_tl(a->imm2), dest);
    gen_set_gpr(ctx, a->rd, result);
    return true;
}

static bool trans_xl_insertr(DisasContext *ctx, arg_xl_insertr *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv result = tcg_temp_new();
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_xl_insertr(result, src1, src2, dest);
    gen_set_gpr(ctx, a->rd, result);
    return true;
}

static bool trans_xl_bset(DisasContext *ctx, arg_xl_bset *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = tcg_constant_tl(a->imm1);
    TCGv src3 = tcg_constant_tl(a->imm2);

    gen_helper_xl_bset(dest, src1, src2, src3);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_xl_bsetr(DisasContext *ctx, arg_xl_bsetr *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_xl_bset, NULL);
}

static bool trans_xl_bclr(DisasContext *ctx, arg_xl_bclr *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = tcg_constant_tl(a->imm1);
    TCGv src3 = tcg_constant_tl(a->imm2);

    gen_helper_xl_bclr(dest, src1, src2, src3);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_xl_bclrr(DisasContext *ctx, arg_xl_bclrr *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_xl_bclr, NULL);
}

static bool trans_xl_clb(DisasContext *ctx, arg_xl_clb *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_xl_clb);
}

static bool trans_xl_fl1(DisasContext *ctx, arg_xl_fl1 *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_xl_fl1);
}

static bool trans_xl_ff1(DisasContext *ctx, arg_xl_ff1 *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_ctz);
}

static bool trans_xl_ff0(DisasContext *ctx, arg_xl_ff0 *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_xl_ff0);
}

static bool trans_xl_fl0(DisasContext *ctx, arg_xl_fl0 *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_xl_fl0);
}

static bool trans_xl_bitrev(DisasContext *ctx, arg_xl_bitrev *a)
{
    REQUIRE_XXLCZ(ctx);
    /* Is3 can be either 0 (radix-2), 1 (radix-4) or 2 (radix-8) */
    target_ulong is3 = (a->imm >> 5) & 0x3;

    if (is3 == 3) {
        return false;
    }
    return gen_arith_imm_tl(ctx, a, EXT_NONE, gen_helper_xl_bitrev, NULL);
}

static bool trans_xl_addrchk(DisasContext *ctx, arg_xl_addrchk *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv source1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv source2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_helper_xl_addrchk(cpu_pc, source1, source2, tcg_constant_i32(a->imm), tcg_constant_i32(ctx->base.pc_next));
    tcg_gen_lookup_and_goto_ptr();

    return true;
}

static bool trans_xl_bezm(DisasContext *ctx, arg_xl_bezm *a)
{
    REQUIRE_XXLCZ(ctx);
    TCGv result = dest_gpr(ctx, 31);
    TCGv source1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv source2 = get_gpr(ctx, a->rs2, EXT_NONE);

    tcg_gen_sub_tl(result, source1, source2);
    gen_set_gpr(ctx, 31, result);
    gen_helper_xl_bezm(cpu_pc, source1, source2, tcg_constant_i32(a->imm), tcg_constant_i32(ctx->base.pc_next));

    tcg_gen_lookup_and_goto_ptr();

    return true;
}

static bool trans_xl_nzmsk(DisasContext *ctx, arg_xl_nzmsk *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_xl_nzmsk);
}

static bool trans_xl_ffnz(DisasContext *ctx, arg_xl_ffnz *a)
{
    REQUIRE_XXLCZ(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_xl_ffnz);
}
