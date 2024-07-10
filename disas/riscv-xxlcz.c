/*
 * QEMU RISC-V Disassembler for nuclei.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/riscv.h"
#include "disas/riscv-xxlcz.h"

typedef enum {
    /* 0 is reserved for rv_op_illegal. */
    rv_op_xl_lb = 1,
    rv_op_xl_lbu,
    rv_op_xl_lh,
    rv_op_xl_lhu,
    rv_op_xl_lw,
    rv_op_xl_sb,
    rv_op_xl_sh,
    rv_op_xl_sw,
    rv_op_xl_lwu,
    rv_op_xl_ld,
    rv_op_xl_sd,
    rv_op_xl_flh,
    rv_op_xl_flw,
    rv_op_xl_fld,
    rv_op_xl_fsh,
    rv_op_xl_fsw,
    rv_op_xl_fsd,
    rv_op_xl_lgp_b,
    rv_op_xl_lgp_bu,
    rv_op_xl_lgp_h,
    rv_op_xl_lgp_hu,
    rv_op_xl_lgp_w,
    rv_op_xl_sgp_b,
    rv_op_xl_sgp_h,
    rv_op_xl_sgp_w,
    rv_op_xl_lgp_wu,
    rv_op_xl_lgp_d,
    rv_op_xl_sgp_d,
    rv_op_xl_muli,
    rv_op_xl_slet,
    rv_op_xl_sletu,
    rv_op_xl_extract,
    rv_op_xl_extractr,
    rv_op_xl_extractu,
    rv_op_xl_extractur,
    rv_op_xl_insert,
    rv_op_xl_bset,
    rv_op_xl_bsetr,
    rv_op_xl_bclr,
    rv_op_xl_bclrr,
    rv_op_xl_clb,
    rv_op_xl_fl1,
    rv_op_xl_ff1,
    rv_op_xl_fl0,
    rv_op_xl_ff0,
    rv_op_xl_beqi,
    rv_op_xl_bnei,
    rv_op_xl_bitrev,
    rv_op_xl_addrchk,
    rv_op_xl_bezm,
    rv_op_xl_nzmsk,
    rv_op_xl_ffnz,
    rv_op_xl_addibne,
} rv_xxlcz_op;

const rv_opcode_data xxlcz_opcode_data[] = {
    { "illegal", rv_codec_illegal, rv_fmt_none, NULL, 0, 0, 0 },
    { "xl.lb", rv_codec_i8, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.lbu", rv_codec_i8, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.lh", rv_codec_i8_sh1, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.lhu", rv_codec_i8_sh1, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.lw", rv_codec_i8_sh2, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.sb", rv_codec_s8, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "xl.sh", rv_codec_s8_sh1, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "xl.sw", rv_codec_s8_sh2, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "xl.lwu", rv_codec_i8_sh2, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.ld", rv_codec_i8_sh3, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.sd", rv_codec_s8_sh3, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "xl.flh", rv_codec_i8_sh1, rv_fmt_frd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.flw", rv_codec_i8_sh2, rv_fmt_frd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.fld", rv_codec_i8_sh3, rv_fmt_frd_offset_rs1, NULL, 0, 0, 0 },
    { "xl.fsh", rv_codec_s8_sh1, rv_fmt_frs2_offset_rs1, NULL, 0, 0, 0 },
    { "xl.fsw", rv_codec_s8_sh2, rv_fmt_frs2_offset_rs1, NULL, 0, 0, 0 },
    { "xl.fsd", rv_codec_s8_sh3, rv_fmt_frs2_offset_rs1, NULL, 0, 0, 0 },
    { "xl.lgp.b", rv_codec_xxlcz_lgp16, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "xl.lgp.bu", rv_codec_xxlcz_lgp16, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "xl.lgp.h", rv_codec_xxlcz_lgp15_sh1, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "xl.lgp.hu", rv_codec_xxlcz_lgp15_sh1, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "xl.lgp.w", rv_codec_xxlcz_lgp15_sh2, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "xl.sgp.b", rv_codec_xxlcz_sgp16, rv_fmt_r2_imm, NULL, 0, 0, 0 },
    { "xl.sgp.h", rv_codec_xxlcz_sgp15_sh1, rv_fmt_r2_imm, NULL, 0, 0, 0 },
    { "xl.sgp.w", rv_codec_xxlcz_sgp15_sh2, rv_fmt_r2_imm, NULL, 0, 0, 0 },
    { "xl.lgp.wu", rv_codec_xxlcz_lgp15_sh2, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "xl.lgp.d", rv_codec_xxlcz_lgp15_sh3, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "xl.sgp.d", rv_codec_xxlcz_sgp15_sh3, rv_fmt_r2_imm, NULL, 0, 0, 0 },
    { "xl.muli", rv_codec_xxlcz_mac, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "xl.slet", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "xl.sletu", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "xl.extract", rv_codec_xxlcz_bitop, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "xl.extractr", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "xl.extractu", rv_codec_xxlcz_bitop, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "xl.extractur", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "xl.insert", rv_codec_xxlcz_bitop, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "xl.bset", rv_codec_xxlcz_bitop, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "xl.bsetr", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "xl.bclr", rv_codec_xxlcz_bitop, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "xl.bclrr", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "xl.clb", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xl.fl1", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xl.ff1", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xl.fl0", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xl.ff0", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xl.beqi", rv_codec_xxlcz_brib, rv_fmt_rs1_imm_offset, NULL, 0, 0, 0 },
    { "xl.bnei", rv_codec_xxlcz_brib, rv_fmt_rs1_imm_offset, NULL, 0, 0, 0 },
    { "xl.bitrev", rv_codec_xxlcz_bitrev, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
    { "xl.addrchk", rv_codec_xxlcz_b12, rv_fmt_rs1_rs2_offset, NULL, 0, 0, 0 },
    { "xl.bezm", rv_codec_xxlcz_b12, rv_fmt_rs1_rs2_offset, NULL, 0, 0, 0 },
    { "xl.nzmsk", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xl.ffnz", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xl.addibne", rv_codec_xxlcz_addib, rv_fmt_rd_rs1_immh_imml, NULL, 0, 0, 0 },
};

void decode_xxlcz(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch ((inst >> 0) & 0b11) { /* opcode[1:0] */
    case 3:
        switch (((inst >> 2) & 0b11111)) { /* opcode[6:2] */
        case 0b10110:
            switch ((inst >> 12) & 0b11) { /* function 3 */
            case 0:
                switch ((inst >> 30) & 0b11) {
                case 0: op = rv_op_xl_lgp_b; break;
                case 1: op = rv_op_xl_lgp_bu; break;
                case 3: op = rv_op_xl_sgp_b; break;
                }
                break;
            case 1:
                switch ((inst >> 14) & 0b1) {
                case 0:
                    switch ((inst >> 30) & 0b11) {
                    case 0: op = rv_op_xl_lgp_h; break;
                    case 2: op = rv_op_xl_sgp_h; break;
                    }
                    break;
                case 1: op = rv_op_xl_lgp_hu; break;
                }
                break;
            case 2:
                switch ((inst >> 14) & 0b1) {
                case 0:
                    switch ((inst >> 30) & 0b11) {
                    case 0: op = rv_op_xl_lgp_w; break;
                    case 2: op = rv_op_xl_sgp_w; break;
                    }
                    break;
                case 1: op = rv_op_xl_lgp_wu; break;
                }
                break;
            case 3:
                switch ((inst >> 30) & 0b11) {
                case 0: op = rv_op_xl_lgp_d; break;
                case 2: op = rv_op_xl_sgp_d; break;
                }
                break;
            }
            break;
        case 0b11110:
            switch ((inst >> 12) & 0b111) {
            case 1:
                switch ((inst >> 28) & 0b1111) {
                case 0b0000: op = rv_op_xl_lb; break;
                case 0b0001: op = rv_op_xl_lbu; break;
                case 0b0010: op = rv_op_xl_lh; break;
                case 0b0011: op = rv_op_xl_lhu; break;
                case 0b0100: op = rv_op_xl_lw; break;
                case 0b0101: op = rv_op_xl_sb; break;
                case 0b0110: op = rv_op_xl_sh; break;
                case 0b0111: op = rv_op_xl_sw; break;
                case 0b1000: op = rv_op_xl_lwu; break;
                case 0b1001: op = rv_op_xl_ld; break;
                case 0b1010: op = rv_op_xl_sd; break;
                case 0b1100:
                case 0b1101: op = rv_op_xl_muli; break;
                case 0b1110:
                    switch ((inst >> 25) & 0b111) {
                    case 0: op = rv_op_xl_nzmsk; break;
                    case 1: op = rv_op_xl_ffnz; break;
                    case 3: op = rv_op_xl_slet; break;
                    case 4: op = rv_op_xl_sletu; break;
                    case 5: op = rv_op_xl_clb; break;
                    case 6: op = rv_op_xl_fl1; break;
                    case 7: op = rv_op_xl_ff1; break;
                    }
                    break;
                case 0b1011: op = rv_op_xl_bitrev; break;
                case 0b1111:
                    switch ((inst >> 25) & 0b111) {
                    case 0: op = rv_op_xl_fl0; break;
                    case 1: op = rv_op_xl_ff0; break;
                    }
                    break;
                }
                break;
            case 2: op = rv_op_xl_beqi; break;
            case 3:
                switch ((inst >> 31) & 0b1) {
                case 0: op = rv_op_xl_addrchk; break;
                case 1: op = rv_op_xl_bezm; break;
                }
                break;
            case 4: op = rv_op_xl_bnei; break;
            case 5:
                switch ((inst >> 30) & 0b11) {
                case 0: op = rv_op_xl_extract; break;
                case 1:
                    switch ((inst >> 28) & 0b11) {
                    case 0: op = rv_op_xl_extractr; break;
                    case 1: op = rv_op_xl_flh; break;
                    case 2: op = rv_op_xl_flw; break;
                    case 3: op = rv_op_xl_fld; break;
                    }
                    break;
                case 2: op = rv_op_xl_extractu; break;
                case 3:
                    switch ((inst >> 28) & 0b11) {
                    case 0: op = rv_op_xl_extractur; break;
                    case 1: op = rv_op_xl_fsh; break;
                    case 2: op = rv_op_xl_fsw; break;
                    case 3: op = rv_op_xl_fsd; break;
                    }
                    break;
                }
                break;
            case 6:
                switch ((inst >> 30) & 0b11) {
                case 0: op = rv_op_xl_insert; break;
                case 2: op = rv_op_xl_bset; break;
                case 3: op = rv_op_xl_bsetr; break;
                }
                break;
            case 7:
                switch ((inst >> 30) & 0b11) {
                case 0: op = rv_op_xl_bclr; break;
                case 1: op = rv_op_xl_bclrr; break;
                case 2:
                case 3: op = rv_op_xl_addibne; break;
                }
                break;
            }
            break;
        }
        break;
    }

    dec->op = op;
}
