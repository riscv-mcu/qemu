#define REQUIRE_ZCEA(ctx) do {   \
    if (!ctx->cfg_ptr->ext_zcea) \
        return false;           \
} while (0)

static bool gen_bimm_z(DisasContext *ctx, arg_z_b *a, TCGCond cond)
{
    TCGLabel *l = gen_new_label();
    TCGv src1 = get_gpr(ctx, a->rs2, EXT_ZERO);
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

static bool trans_beqi(DisasContext *ctx, arg_beqi *a)
{
    REQUIRE_ZCEA(ctx);
    return gen_bimm_z(ctx, a, TCG_COND_EQ);
}

static bool trans_bnei(DisasContext *ctx, arg_bnei *a)
{
    REQUIRE_ZCEA(ctx);
    return gen_bimm_z(ctx, a, TCG_COND_NE);
}

static bool trans_muli(DisasContext *ctx, arg_muli *a)
{
    REQUIRE_ZCEA(ctx);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    TCGv dest = dest_gpr(ctx, a->rd);
    
    tcg_gen_muli_tl(dest, src1, a->imm);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_load_z_gp(DisasContext *ctx, arg_z_lgp *a, int shift, MemOp memop)
{
    REQUIRE_ZCEA(ctx);
    TCGv t0 = get_address(ctx, 3, a->imm << shift);
    TCGv t1 = tcg_temp_new();
    
    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, t1);
    return true;
}

static bool trans_lwgp(DisasContext *ctx, arg_lwgp *a)
{
    return gen_load_z_gp(ctx, a, 2, MO_SL);
}

static bool trans_ldgp(DisasContext *ctx, arg_ldgp *a)
{
    return gen_load_z_gp(ctx, a, 3, MO_SQ);
}

static bool gen_store_z_gp(DisasContext *ctx, arg_z_sgp *a, int shift, MemOp memop)
{
    REQUIRE_ZCEA(ctx);
    TCGv t0 = get_address(ctx, 3, a->imm << shift);
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);

    tcg_gen_qemu_st_tl(data, t0, ctx->mem_idx, MO_SB);
    return true;
}

static bool trans_swgp(DisasContext *ctx, arg_swgp *a)
{
    return gen_store_z_gp(ctx, a, 2, MO_SL);
}

static bool trans_sdgp(DisasContext *ctx, arg_sdgp *a)
{
    return gen_store_z_gp(ctx, a, 3, MO_SQ);
}

static bool trans_c_decbnez(DisasContext *ctx, arg_c_decbnez *a)
{
    REQUIRE_ZCEA(ctx);
    if (ctx->cfg_ptr->ext_d)
        return false;
    TCGv t0 = get_gpr(ctx, a->rd, EXT_ZERO);
    TCGv t1 = tcg_temp_new();

    gen_helper_c_decbnez(cpu_pc, t0, tcg_constant_i32(a->scale), tcg_constant_i32(a->imm << 1), tcg_constant_i32(ctx->base.pc_next));
    tcg_gen_lookup_and_goto_ptr();

    tcg_gen_sub_tl(t1, t0, a->scale);
    gen_set_gpr(ctx, a->rd, t1);
    return true;
}