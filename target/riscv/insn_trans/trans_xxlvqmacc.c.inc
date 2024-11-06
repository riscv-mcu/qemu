/*
 * RISC-V translation routines for Nuclei Xxlvqmacc Extension.
 */

#define REQUIRE_XXLVQMACC(ctx) do {   \
    if (!ctx->cfg_ptr->ext_xxlvqmacc) \
        return false;                 \
} while (0)

#define GEN_TRANS_MATRIX(NAME)                                     \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    REQUIRE_XXLVQMACC(s);                                          \
    if (require_rvv(s) &&                                          \
        vext_check_isa_ill(s)) {                                   \
        uint32_t data = 0;                                         \
        gen_helper_gvec_4_ptr *fn = gen_helper_##NAME;             \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data =                                                     \
            FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);\
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs1),                    \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data, fn);           \
        finalize_rvv_inst(s);                                      \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_TRANS_MATRIX(xl_vqmaccu_vv)
GEN_TRANS_MATRIX(xl_vqmacc_vv)
GEN_TRANS_MATRIX(xl_vqmaccus_vv)
GEN_TRANS_MATRIX(xl_vqmaccsu_vv)
