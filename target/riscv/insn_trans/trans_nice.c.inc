/*
 * RISC-V translation routines for Nuclei Custom Nice.
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

static bool trans_lbuf(DisasContext *ctx, arg_lbuf *a)
{
    gen_helper_lbuf(tcg_env, cpu_gpr[a->rs1]);
    return true;
}

static bool trans_sbuf(DisasContext *ctx, arg_sbuf *a)
{
    gen_helper_sbuf(tcg_env, cpu_gpr[a->rs1]);
    return true;
}

static bool trans_rowsum(DisasContext *ctx, arg_rowsum *a)
{
    gen_helper_rowsum(cpu_gpr[a->rd], tcg_env, cpu_gpr[a->rs1]);
    return true;
}

static bool vext_check_vnice_load(DisasContext *s, int vd, int nf, uint8_t eew)
{
    int8_t emul = eew - s->sew + s->lmul;
    return (emul >= -3 && emul <= 3) &&
            require_align(vd, emul) &&
            require_nf(vd, nf, emul);
}

static bool vnice_ld_us_check(DisasContext *s, arg_vnl32* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_vnice_load(s, a->rd, 1, eew);
}

static bool vnice_ld_us_op(DisasContext *s, arg_vnl32 *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;

    fn =  gen_helper_vnl32;
    if (fn == NULL) {
        return false;
    }

    /*
     * Vector load/store instructions have the EEW encoded
     * directly in the instructions. The maximum vector size is
     * calculated with EMUL rather than LMUL.
     */
    uint8_t emul = vext_get_emul(s, eew);
    // data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, 1);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    return ldst_us_trans(a->rd, a->rs1, data, fn, s, false);
}

static bool trans_vnl32(DisasContext *s, arg_vnl32 *a)
{
    if (vnice_ld_us_check(s, a, MO_32))
    {
        return vnice_ld_us_op(s, a, MO_32);
    }
    return false; 
}

static bool vnice_st_us_check(DisasContext *s, arg_vns32* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_store(s, a->rs2, 1, eew);
}

static bool vnice_st_us_op(DisasContext *s, arg_vns32*a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;

    fn =  gen_helper_vns32;
    if (fn == NULL) {
        return false;
    }

    uint8_t emul = vext_get_emul(s, eew);
    data = FIELD_DP32(data, VDATA, VM, 1);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, 1);
    return ldst_us_trans(a->rs2, a->rs1, data, fn, s, true);
}

static bool trans_vns32(DisasContext *s, arg_vns32 *a)
{
    if (vnice_st_us_check(s, a, MO_32))
    {
        return vnice_st_us_op(s, a, MO_32);
    }
    return false;
}

#define GEN_VN_CMPX_MUL_TRANS(NAME, VM_VALUE)                    \
static bool trans_##NAME(DisasContext *s, arg_r *a)              \
{                                                                \
    if (require_rvv(s) && vext_check_isa_ill(s)) {               \
        uint32_t data = 0;                                       \
                                                                 \
        data = FIELD_DP32(data, VDATA, VM, VM_VALUE);             \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);           \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);             \
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),   \
                           vreg_ofs(s, a->rs1),                   \
                           vreg_ofs(s, a->rs2), tcg_env,          \
                           s->cfg_ptr->vlenb,                     \
                           s->cfg_ptr->vlenb, data,               \
                           gen_helper_##NAME);                    \
        finalize_rvv_inst(s);                                    \
        return true;                                             \
    }                                                            \
    return false;                                                \
}

GEN_VN_CMPX_MUL_TRANS(vn_cmpx_mul, 1)
GEN_VN_CMPX_MUL_TRANS(vn_cmpx_mul_m, 0)
