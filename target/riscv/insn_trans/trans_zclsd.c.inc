/*
 * RISC-V translation routines for the Zclsd Extension.
 */

#define REQUIRE_ZCLSD(ctx) do {    \
    if (!ctx->cfg_ptr->ext_zclsd)  \
        return false;               \
} while (0)

static bool gen_load_ld(DisasContext *ctx, arg_lb *a, MemOp memop, int flag)
{
    REQUIRE_ZCLSD(ctx);
    REQUIRE_32BIT(ctx);

    if (flag&&a->rd==0){
        return false;
    }

    TCGv dest_low= dest_gpr(ctx, a->rd);
    TCGv addr = get_address(ctx, a->rs1, a->imm);
    tcg_gen_qemu_ld_tl(dest_low, addr, ctx->mem_idx, memop);

    TCGv dest_high= dest_gpr(ctx, a->rd+1);
    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_ld_tl(dest_high, addr, ctx->mem_idx, memop);

    gen_set_gpr(ctx, a->rd, dest_low);
    gen_set_gpr(ctx, a->rd+1, dest_high);

    return true;
}

// zclsd c.ldsp 
static bool trans_zclsd_ldsp(DisasContext *ctx, arg_zclsd_ldsp *a)
{
    return gen_load_ld(ctx, a, MO_TESL, 1);
}

//zclsd c.ld  
static bool trans_zclsd_ld(DisasContext *ctx, arg_zclsd_ld *a)
{
    return gen_load_ld(ctx, a, MO_TESL, 0);
}

static bool gen_store_sd(DisasContext *ctx, arg_sb *a, MemOp memop, int flag)
{
    REQUIRE_ZCLSD(ctx);
    REQUIRE_32BIT(ctx);

    if(flag&&a->rs2==0){
        TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
        TCGv addr = get_address(ctx, a->rs1, a->imm);
        tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);

        tcg_gen_addi_tl(addr, addr, 4);
        tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);  

        return true;
    }

    TCGv data_low = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data_high = get_gpr(ctx, a->rs2+1, EXT_NONE);


    TCGv addr = get_address(ctx, a->rs1, a->imm);
    tcg_gen_qemu_st_tl(data_low, addr, ctx->mem_idx, memop);

    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_st_tl(data_high, addr, ctx->mem_idx, memop);  
    return true;
}

//zclsd c.sdsp
static bool trans_zclsd_sdsp(DisasContext *ctx, arg_zclsd_sdsp *a)
{
    return gen_store_sd(ctx, a, MO_TESL, 1);
}

//zclsd c.sd
static bool trans_zclsd_sd(DisasContext *ctx, arg_zclsd_sd *a)
{
    return gen_store_sd(ctx, a, MO_TESL, 0);
}
