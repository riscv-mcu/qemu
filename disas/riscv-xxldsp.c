/*
 * QEMU RISC-V Disassembler for nuclei.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/riscv.h"
#include "disas/riscv-xxldsp.h"

typedef enum {
    /* 0 is reserved for rv_op_illegal. */
    rv_op_add16 = 1,
    rv_op_radd16,
    rv_op_uradd16,
    rv_op_kadd16,
    rv_op_ukadd16,
    rv_op_sub16,
    rv_op_rsub16,
    rv_op_ursub16,
    rv_op_ksub16,
    rv_op_uksub16,
    rv_op_cras16,
    rv_op_rcras16,
    rv_op_urcras16,
    rv_op_kcras16,
    rv_op_ukcras16,
    rv_op_crsa16,
    rv_op_rcrsa16,
    rv_op_urcrsa16,
    rv_op_kcrsa16,
    rv_op_ukcrsa16,
    rv_op_stas16,
    rv_op_rstas16,
    rv_op_urstas16,
    rv_op_kstas16,
    rv_op_ukstas16,
    rv_op_stsa16,
    rv_op_rstsa16,
    rv_op_urstsa16,
    rv_op_kstsa16,
    rv_op_ukstsa16,
    rv_op_add8,
    rv_op_radd8,
    rv_op_uradd8,
    rv_op_kadd8,
    rv_op_ukadd8,
    rv_op_sub8,
    rv_op_rsub8,
    rv_op_ursub8,
    rv_op_ksub8,
    rv_op_uksub8,
    rv_op_sra16,
    rv_op_sra16_u,
    rv_op_srai16,
    rv_op_srai16_u,
    rv_op_srl16,
    rv_op_srl16_u,
    rv_op_srli16,
    rv_op_srli16_u,
    rv_op_sll16,
    rv_op_slli16,
    rv_op_ksll16,
    rv_op_kslli16,
    rv_op_kslra16,
    rv_op_kslra16_u,
    rv_op_sra8,
    rv_op_sra8_u,
    rv_op_srai8,
    rv_op_srai8_u,
    rv_op_srl8,
    rv_op_srl8_u,
    rv_op_srli8,
    rv_op_srli8_u,
    rv_op_sll8,
    rv_op_slli8,
    rv_op_ksll8,
    rv_op_kslli8,
    rv_op_kslra8,
    rv_op_kslra8_u,
    rv_op_cmpeq16,
    rv_op_scmplt16,
    rv_op_scmple16,
    rv_op_ucmplt16,
    rv_op_ucmple16,
    rv_op_cmpeq8,
    rv_op_scmplt8,
    rv_op_scmple8,
    rv_op_ucmplt8,
    rv_op_ucmple8,
    rv_op_smul16,
    rv_op_smulx16,
    rv_op_umul16,
    rv_op_umulx16,
    rv_op_khm16,
    rv_op_khmx16,
    rv_op_smul8,
    rv_op_smulx8,
    rv_op_umul8,
    rv_op_umulx8,
    rv_op_khm8,
    rv_op_khmx8,
    rv_op_smin16,
    rv_op_umin16,
    rv_op_smax16,
    rv_op_umax16,
    rv_op_sclip16,
    rv_op_uclip16,
    rv_op_kabs16,
    rv_op_clrs16,
    rv_op_clz16,
    rv_op_clo16,
    rv_op_smin8,
    rv_op_umin8,
    rv_op_smax8,
    rv_op_umax8,
    rv_op_sclip8,
    rv_op_uclip8,
    rv_op_kabs8,
    rv_op_clrs8,
    rv_op_clz8,
    rv_op_clo8,
    rv_op_swap8,
    rv_op_swap16,
    rv_op_sunpkd810,
    rv_op_sunpkd820,
    rv_op_sunpkd830,
    rv_op_sunpkd831,
    rv_op_sunpkd832,
    rv_op_zunpkd810,
    rv_op_zunpkd820,
    rv_op_zunpkd830,
    rv_op_zunpkd831,
    rv_op_zunpkd832,
    rv_op_pkbb16,
    rv_op_pkbt16,
    rv_op_pktt16,
    rv_op_pktb16,
    rv_op_smmul,
    rv_op_smmul_u,
    rv_op_kmmac,
    rv_op_kmmac_u,
    rv_op_kmmsb,
    rv_op_kmmsb_u,
    rv_op_kwmmul,
    rv_op_kwmmul_u,
    rv_op_smmwb,
    rv_op_smmwb_u,
    rv_op_smmwt,
    rv_op_smmwt_u,
    rv_op_kmmawb,
    rv_op_kmmawb_u,
    rv_op_kmmawt,
    rv_op_kmmawt_u,
    rv_op_kmmwb2,
    rv_op_kmmwb2_u,
    rv_op_kmmwt2,
    rv_op_kmmwt2_u,
    rv_op_kmmawb2,
    rv_op_kmmawb2_u,
    rv_op_kmmawt2,
    rv_op_kmmawt2_u,
    rv_op_smbb16,
    rv_op_smbt16,
    rv_op_smtt16,
    rv_op_kmda,
    rv_op_kmxda,
    rv_op_smds,
    rv_op_smdrs,
    rv_op_smxds,
    rv_op_kmabb,
    rv_op_kmabt,
    rv_op_kmatt,
    rv_op_kmada,
    rv_op_kmaxda,
    rv_op_kmads,
    rv_op_kmadrs,
    rv_op_kmaxds,
    rv_op_kmsda,
    rv_op_kmsxda,
    rv_op_smal,
    rv_op_sclip32,
    rv_op_uclip32,
    rv_op_clrs32,
    rv_op_clz32,
    rv_op_clo32,
    rv_op_pbsad,
    rv_op_pbsada,
    rv_op_smaqa,
    rv_op_umaqa,
    rv_op_smaqa_su,
    rv_op_add64,
    rv_op_radd64,
    rv_op_uradd64,
    rv_op_kadd64,
    rv_op_ukadd64,
    rv_op_sub64,
    rv_op_rsub64,
    rv_op_ursub64,
    rv_op_ksub64,
    rv_op_uksub64,
    rv_op_smar64,
    rv_op_smsr64,
    rv_op_umar64,
    rv_op_umsr64,
    rv_op_kmar64,
    rv_op_kmsr64,
    rv_op_ukmar64,
    rv_op_ukmsr64,
    rv_op_smalbb,
    rv_op_smalbt,
    rv_op_smaltt,
    rv_op_smalda,
    rv_op_smalxda,
    rv_op_smalds,
    rv_op_smaldrs,
    rv_op_smalxds,
    rv_op_smslda,
    rv_op_smslxda,
    rv_op_kaddh,
    rv_op_ksubh,
    rv_op_khmbb,
    rv_op_khmbt,
    rv_op_khmtt,
    rv_op_ukaddh,
    rv_op_uksubh,
    rv_op_kaddw,
    rv_op_ukaddw,
    rv_op_ksubw,
    rv_op_uksubw,
    rv_op_kdmbb,
    rv_op_kdmbt,
    rv_op_kdmtt,
    rv_op_kslraw,
    rv_op_kslraw_u,
    rv_op_ksllw,
    rv_op_kslliw,
    rv_op_kdmabb,
    rv_op_kdmabt,
    rv_op_kdmatt,
    rv_op_kabsw,
    rv_op_raddw,
    rv_op_uraddw,
    rv_op_rsubw,
    rv_op_ursubw,
    rv_op_maxw,
    rv_op_minw,
    rv_op_mulr64,
    rv_op_mulsr64,
    rv_op_ave,
    rv_op_sra_u,
    rv_op_srai_u,
    rv_op_bitrev,
    rv_op_bitrevi,
    rv_op_wext,
    rv_op_wexti,
    rv_op_bpick,
    rv_op_insb,
    rv_op_maddr32,
    rv_op_msubr32,
    rv_op_add32,
    rv_op_radd32,
    rv_op_uradd32,
    rv_op_kadd32,
    rv_op_ukadd32,
    rv_op_sub32,
    rv_op_rsub32,
    rv_op_ursub32,
    rv_op_ksub32,
    rv_op_uksub32,
    rv_op_cras32,
    rv_op_rcras32,
    rv_op_urcras32,
    rv_op_kcras32,
    rv_op_ukcras32,
    rv_op_crsa32,
    rv_op_rcrsa32,
    rv_op_urcrsa32,
    rv_op_kcrsa32,
    rv_op_ukcrsa32,
    rv_op_stas32,
    rv_op_rstas32,
    rv_op_urstas32,
    rv_op_kstas32,
    rv_op_ukstas32,
    rv_op_stsa32,
    rv_op_rstsa32,
    rv_op_urstsa32,
    rv_op_kstsa32,
    rv_op_ukstsa32,
    rv_op_sra32,
    rv_op_sra32_u,
    rv_op_srai32,
    rv_op_srai32_u,
    rv_op_srl32,
    rv_op_srl32_u,
    rv_op_srli32,
    rv_op_srli32_u,
    rv_op_sll32,
    rv_op_slli32,
    rv_op_ksll32,
    rv_op_kslli32,
    rv_op_kslra32,
    rv_op_kslra32_u,
    rv_op_smin32,
    rv_op_umin32,
    rv_op_smax32,
    rv_op_umax32,
    rv_op_kabs32,
    rv_op_khmbb16,
    rv_op_khmbt16,
    rv_op_khmtt16,
    rv_op_kdmbb16,
    rv_op_kdmbt16,
    rv_op_kdmtt16,
    rv_op_kdmabb16,
    rv_op_kdmabt16,
    rv_op_kdmatt16,
    rv_op_smbt32,
    rv_op_smtt32,
    rv_op_kmabb32,
    rv_op_kmabt32,
    rv_op_kmatt32,
    rv_op_kmda32,
    rv_op_kmxda32,
    rv_op_kmaxda32,
    rv_op_kmads32,
    rv_op_kmadrs32,
    rv_op_kmaxds32,
    rv_op_kmsda32,
    rv_op_kmsxda32,
    rv_op_smds32,
    rv_op_smdrs32,
    rv_op_smxds32,
    rv_op_sraiw_u,
    rv_op_pkbb32,
    rv_op_pkbt32,
    rv_op_pktt32,
    rv_op_pktb32,
    rv_op_expd80,
    rv_op_expd81,
    rv_op_expd82,
    rv_op_expd83,
    rv_op_expd84,
    rv_op_expd85,
    rv_op_expd86,
    rv_op_expd87,
    rv_op_dkhm8,
    rv_op_dkhm16,
    rv_op_dkabs8,
    rv_op_dkabs16,
    rv_op_dkslra8,
    rv_op_dkslra16,
    rv_op_dkadd8,
    rv_op_dkadd16,
    rv_op_dksub8,
    rv_op_dksub16,
    rv_op_dkhmx8,
    rv_op_dkhmx16,
    rv_op_dsmmul,
    rv_op_dsmmul_u,
    rv_op_dkwmmul,
    rv_op_dkwmmul_u,
    rv_op_dkabs32,
    rv_op_dkslra32,
    rv_op_dkadd32,
    rv_op_dksub32,
    rv_op_dkmmac,
    rv_op_dkmmac_u,
    rv_op_dkmmsb,
    rv_op_dkmmsb_u,
    rv_op_dkmada,
    rv_op_dkmaxda,
    rv_op_dkmads,
    rv_op_dkmadrs,
    rv_op_dkmaxds,
    rv_op_dkmsda,
    rv_op_dkmsxda,
    rv_op_dsmaqa,
    rv_op_dsmaqa_su,
    rv_op_dumaqa,
    rv_op_dkmda32,
    rv_op_dkmxda32,
    rv_op_dkmada32,
    rv_op_dkmaxda32,
    rv_op_dkmads32,
    rv_op_dkmadrs32,
    rv_op_dkmaxds32,
    rv_op_dkmsda32,
    rv_op_dkmsxda32,
    rv_op_dsmds32,
    rv_op_dsmdrs32,
    rv_op_dsmxds32,
    rv_op_dsmalda,
    rv_op_dsmalxda,
    rv_op_dsmalds,
    rv_op_dsmaldrs,
    rv_op_dsmalxds,
    rv_op_dsmslda,
    rv_op_dsmslxda,
    rv_op_ddsmaqa,
    rv_op_ddsmaqa_su,
    rv_op_ddumaqa,
    rv_op_dradd16,
    rv_op_dsub16,
    rv_op_dradd32,
    rv_op_dsub32,
    rv_op_dkmda,
    rv_op_dkmxda,
    rv_op_dsmdrs,
    rv_op_dsmxds,
    rv_op_dsmbb32,
    rv_op_dsmbb32_sra14,
    rv_op_dsmbb32_sra32,
    rv_op_dsmbt32,
    rv_op_dsmbt32_sra14,
    rv_op_dsmbt32_sra32,
    rv_op_dsmtt32,
    rv_op_dsmtt32_sra14,
    rv_op_dsmtt32_sra32,
    rv_op_dpkbb32,
    rv_op_dpkbt32,
    rv_op_dpack32,
    rv_op_dpktt32,
    rv_op_dpktb32,
    rv_op_dpktb16,
    rv_op_dpkbb16,
    rv_op_dpkbt16,
    rv_op_dpktt16,
    rv_op_dsra16,
    rv_op_dadd16,
    rv_op_dadd32,
    rv_op_dsmbb16,
    rv_op_dsmbt16,
    rv_op_dsmtt16,
    rv_op_drcrsa16,
    rv_op_drcras16,
    rv_op_dkcrsa16,
    rv_op_dkcras16,
    rv_op_drsub16,
    rv_op_drsub32,
    rv_op_dstsa32,
    rv_op_dstas32,
    rv_op_dkcras32,
    rv_op_dkcrsa32,
    rv_op_dcrsa32,
    rv_op_dcras32,
    rv_op_dkstsa16,
    rv_op_dkstas16,
    rv_op_dsclip8,
    rv_op_dsclip16,
    rv_op_dsclip32,
    rv_op_drcrsa32,
    rv_op_drcras32,
    rv_op_dkclip64,
    rv_op_dmsr16,
    rv_op_dmsr17,
    rv_op_dmsr33,
    rv_op_dmxsr33,
    rv_op_dsmada16,
    rv_op_dsmaxda16,
    rv_op_dksms32_u,
    rv_op_dmada32,
    rv_op_dredas16,
    rv_op_dredsa16,
    rv_op_dsunpkd810,
    rv_op_dsunpkd820,
    rv_op_dsunpkd830,
    rv_op_dsunpkd831,
    rv_op_dsunpkd832,
    rv_op_dzunpkd810,
    rv_op_dzunpkd820,
    rv_op_dzunpkd830,
    rv_op_dzunpkd831,
    rv_op_dzunpkd832,
    rv_op_dsma32_u,
    rv_op_dsmxs32_u,
    rv_op_dsmxa32_u,
    rv_op_dsms32_u,
    rv_op_dsmalbb,
    rv_op_dsmalbt,
    rv_op_dsmaltt,
    rv_op_dkmabb32,
    rv_op_dkmabt32,
    rv_op_dkmatt32,
} rv_xxldsp_op;

const rv_opcode_data xxldsp_opcode_data[] = {
    { "illegal", rv_codec_illegal, rv_fmt_none, NULL, 0, 0, 0 },
    { "add16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "radd16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uradd16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kadd16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukadd16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rsub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ursub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ksub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uksub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "cras16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rcras16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urcras16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kcras16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukcras16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "crsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rcrsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urcrsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kcrsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukcrsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "stas16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rstas16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urstas16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kstas16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukstas16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "stsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rstsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urstsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kstsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukstsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "add8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "radd8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uradd8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kadd8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukadd8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sub8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rsub8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ursub8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ksub8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uksub8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra16_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srai16", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srai16_u", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srl16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srl16_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srli16", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srli16_u", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "sll16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "slli16", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "ksll16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslli16", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "kslra16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslra16_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra8_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srai8", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srai8_u", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srl8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srl8_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srli8", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srli8_u", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "sll8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "slli8", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "ksll8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslli8", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "kslra8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslra8_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "cmpeq16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "scmplt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "scmple16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ucmplt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ucmple16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "cmpeq8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "scmplt8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "scmple8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ucmplt8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ucmple8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smul16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smulx16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umul16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umulx16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khm16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khmx16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smul8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smulx8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umul8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umulx8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khm8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khmx8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smin16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umin16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smax16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umax16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sclip16", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "uclip16", rv_codec_i_sh4, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "kabs16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clrs16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clz16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clo16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "smin8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umin8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smax8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umax8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sclip8", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "uclip8", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "kabs8", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clrs8", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clz8", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clo8", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "swap8", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "swap16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sunpkd810", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sunpkd820", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sunpkd830", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sunpkd831", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sunpkd832", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "zunpkd810", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "zunpkd820", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "zunpkd830", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "zunpkd831", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "zunpkd832", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "pkbb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "pkbt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "pktt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "pktb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smmul", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smmul_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmac", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmac_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmsb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmsb_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kwmmul", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kwmmul_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smmwb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smmwb_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smmwt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smmwt_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawb_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawt_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmwb2", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmwb2_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmwt2", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmwt2_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawb2", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawb2_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawt2", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmmawt2_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smbb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smbt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smtt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smdrs", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smxds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmabb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmabt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmatt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmada", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmaxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmads", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmadrs", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmaxds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmsda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmsxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smal", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sclip32", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "uclip32", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "clrs32", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clz32", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clo32", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "pbsad", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "pbsada", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smaqa", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umaqa", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smaqa_su", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "add64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "radd64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uradd64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kadd64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukadd64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sub64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rsub64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ursub64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ksub64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uksub64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smar64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smsr64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umar64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umsr64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmar64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmsr64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukmar64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukmsr64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smalbb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smalbt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smaltt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smalda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smalxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smalds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smaldrs", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smalxds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smslda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smslxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kaddh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ksubh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khmbb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khmbt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khmtt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukaddh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uksubh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kaddw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukaddw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ksubw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uksubw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmbb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmbt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmtt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslraw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslraw_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ksllw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslliw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "kdmabb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmabt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmatt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kabsw", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "raddw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uraddw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rsubw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ursubw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "maxw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "minw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "mulr64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "mulsr64", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ave", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srai_u", rv_codec_i_sh6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "bitrev", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "bitrevi", rv_codec_i_sh6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "wext", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "wexti", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "bpick", rv_codec_r4, rv_fmt_rd_rs1_rs2_rs3, NULL, 0, 0, 0 },
    { "insb", rv_codec_i_sh3, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "maddr32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "msubr32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "add32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "radd32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uradd32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kadd32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukadd32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sub32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rsub32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ursub32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ksub32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "uksub32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "cras32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rcras32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urcras32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kcras32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukcras32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "crsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rcrsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urcrsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kcrsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukcrsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "stas32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rstas32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urstas32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kstas32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukstas32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "stsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rstsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "urstsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kstsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ukstsa32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra32",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra32_u",- rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srai32", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srai32_u", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srl32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srl32_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srli32", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srli32_u", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "sll32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "slli32", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "ksll32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslli32", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "kslra32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kslra32_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smin32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umin32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smax32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "umax32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kabs32", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "khmbb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khmbt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "khmtt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmbb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmbt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmtt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmabb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmabt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kdmatt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smbt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smtt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmabb32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmabt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmatt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmxda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmaxda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmads32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmadrs32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmaxds32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmsda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "kmsxda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smds32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smdrs32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "smxds32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sraiw_u", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "pkbb32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "pkbt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "pktt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "pktb32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "expd80", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "expd81", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "expd82", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "expd83", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "expd84", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "expd85", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "expd86", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "expd87", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dkhm8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkhm16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkabs8", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dkabs16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dkslra8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkslra16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkadd8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkadd16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dksub8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dksub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkhmx8", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkhmx16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmmul", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmmul_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkwmmul", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkwmmul_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkabs32", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dkslra32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkadd32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dksub32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmmac", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmmac_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmmsb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmmsb_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmada", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmaxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmads", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmadrs", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmaxds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmsda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmsxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmaqa", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmaqa_su", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dumaqa", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmxda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmada32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmaxda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmads32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmadrs32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmaxds32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmsda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmsxda32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmds32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmdrs32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmxds32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmalda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmalxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmalds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmaldrs", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmalxds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmslda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmslxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ddsmaqa", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ddsmaqa_su", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ddumaqa", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dradd16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dradd32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsub32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmxda", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmdrs", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmxds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbb32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbb32_sra14", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbb32_sra32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbt32_sra14", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbt32_sra32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmtt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmtt32_sra14", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmtt32_sra32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpkbb32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpkbt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpack32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpktt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpktb32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpktb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpkbb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpkbt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dpktt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsra16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dadd16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dadd32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbb16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmbt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmtt16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "drcrsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "drcras16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkcrsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkcras16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "drsub16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "drsub32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dstsa32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dstas32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkcras32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkcrsa32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dcrsa32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dcras32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkstsa16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkstas16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsclip8", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "dsclip16", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "dsclip32", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "drcrsa32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "drcras32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkclip64", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dmsr16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dmsr17", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dmsr33", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dmxsr33", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmada16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmaxda16", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dksms32_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dmada32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dredas16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dredsa16", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dsunpkd810", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dsunpkd820", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dsunpkd830", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dsunpkd831", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dsunpkd832", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dzunpkd810", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dzunpkd820", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dzunpkd830", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dzunpkd831", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dzunpkd832", rv_codec_r2, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "dsma32_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmxs32_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmxa32_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsms32_u", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmalbb", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmalbt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dsmaltt", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmabb32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmabt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "dkmatt32", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
};

void decode_xxldsp(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch ((inst >> 0) & 0b11) { /* opcode[1:0] */
    case 3:
        switch (((inst >> 2) & 0b11111)) { /* opcode[6:2] */
        case 0b11111:
            switch ((inst >> 12) & 0b111) { /* function 3 */
            case 0:
                switch ((inst >> 25) & 0b1111111) { /* function 7 */
                case 0b0100000: op = rv_op_add16; break;
                case 0b0000000: op = rv_op_radd16; break;
                case 0b0010000: op = rv_op_uradd16; break;
                case 0b0001000: op = rv_op_kadd16; break;
                case 0b0011000: op = rv_op_ukadd16; break;
                case 0b0100001: op = rv_op_sub16; break;
                case 0b0000001: op = rv_op_rsub16; break;
                case 0b0010001: op = rv_op_ursub16; break;
                case 0b0001001: op = rv_op_ksub16; break;
                case 0b0011001: op = rv_op_uksub16; break;
                case 0b0100010: op = rv_op_cras16; break;
                case 0b0000010: op = rv_op_rcras16; break;
                case 0b0010010: op = rv_op_urcras16; break;
                case 0b0001010: op = rv_op_kcras16; break;
                case 0b0011010: op = rv_op_ukcras16; break;
                case 0b0100011: op = rv_op_crsa16; break;
                case 0b0000011: op = rv_op_rcrsa16; break;
                case 0b0010011: op = rv_op_urcrsa16; break;
                case 0b0001011: op = rv_op_kcrsa16; break;
                case 0b0011011: op = rv_op_ukcrsa16; break;
                case 0b0100100: op = rv_op_add8; break;
                case 0b0000100: op = rv_op_radd8; break;
                case 0b0010100: op = rv_op_uradd8; break;
                case 0b0001100: op = rv_op_kadd8; break;
                case 0b0011100: op = rv_op_ukadd8; break;
                case 0b0100101: op = rv_op_sub8; break;
                case 0b0000101: op = rv_op_rsub8; break;
                case 0b0010101: op = rv_op_ursub8; break;
                case 0b0001101: op = rv_op_ksub8; break;
                case 0b0011101: op = rv_op_uksub8; break;
                case 0b0101000: op = rv_op_sra16; break;
                case 0b0110000: op = rv_op_sra16_u; break;
                case 0b0111000:
                    switch ((inst >> 24) & 0b1) {
                    case 0: op = rv_op_srai16; break;
                    case 1: op = rv_op_srai16_u; break;
                    }
                    break;
                case 0b0101001: op = rv_op_srl16; break;
                case 0b0110001: op = rv_op_srl16_u; break;
                case 0b0111001:
                    switch ((inst >> 24) & 0b1) {
                    case 0: op = rv_op_srli16; break;
                    case 1: op = rv_op_srli16_u; break;
                    }
                    break;
                case 0b0101010: op = rv_op_sll16; break;
                case 0b0110010: op = rv_op_ksll16; break;
                case 0b0111010:
                    switch ((inst >> 24) & 0b1) {
                    case 0: op = rv_op_slli16; break;
                    case 1: op = rv_op_kslli16; break;
                    }
                    break;
                case 0b0101011: op = rv_op_kslra16; break;
                case 0b0110011: op = rv_op_kslra16_u; break;
                case 0b0101100: op = rv_op_sra8; break;
                case 0b0110100: op = rv_op_sra8_u; break;
                case 0b0111100:
                    switch ((inst >> 23) & 0b11) {
                    case 0: op = rv_op_srai8; break;
                    case 1: op = rv_op_srai8_u; break;
                    }
                    break;
                case 0b0101101: op = rv_op_srl8; break;
                case 0b0110101: op = rv_op_srl8_u; break;
                case 0b0111101:
                    switch ((inst >> 23) & 0b11) {
                    case 0: op = rv_op_srli8; break;
                    case 1: op = rv_op_srli8_u; break;
                    }
                    break;
                case 0b0101110: op = rv_op_sll8; break;
                case 0b0111110:
                    switch ((inst >> 23) & 0b11) {
                    case 0: op = rv_op_slli8; break;
                    case 1: op = rv_op_kslli8; break;
                    }
                    break;
                case 0b0110110: op = rv_op_ksll8; break;
                case 0b0101111: op = rv_op_kslra8; break;
                case 0b0110111: op = rv_op_kslra8_u; break;
                case 0b0100110: op = rv_op_cmpeq16; break;
                case 0b0000110: op = rv_op_scmplt16; break;
                case 0b0001110: op = rv_op_scmple16; break;
                case 0b0010110: op = rv_op_ucmplt16; break;
                case 0b0011110: op = rv_op_ucmple16; break;
                case 0b0100111: op = rv_op_cmpeq8; break;
                case 0b0000111: op = rv_op_scmplt8; break;
                case 0b0001111: op = rv_op_scmple8; break;
                case 0b0010111: op = rv_op_ucmplt8; break;
                case 0b0011111: op = rv_op_ucmple8; break;
                case 0b1010000: op = rv_op_smul16; break;
                case 0b1010001: op = rv_op_smulx16; break;
                case 0b1011000: op = rv_op_umul16; break;
                case 0b1011001: op = rv_op_umulx16; break;
                case 0b1000011: op = rv_op_khm16; break;
                case 0b1001011: op = rv_op_khmx16; break;
                case 0b1010100: op = rv_op_smul8; break;
                case 0b1010101: op = rv_op_smulx8; break;
                case 0b1011100: op = rv_op_umul8; break;
                case 0b1011101: op = rv_op_umulx8; break;
                case 0b1000111: op = rv_op_khm8; break;
                case 0b1001111: op = rv_op_khmx8; break;
                case 0b1000000: op = rv_op_smin16; break;
                case 0b1001000: op = rv_op_umin16; break;
                case 0b1000001: op = rv_op_smax16; break;
                case 0b1001001: op = rv_op_umax16; break;
                case 0b1000010:
                    switch ((inst >> 24) & 0b1) {
                    case 0: op = rv_op_sclip16; break;
                    case 1: op = rv_op_uclip16; break;
                    }
                    break;
                case 0b1010110:
                    switch ((inst >> 23) & 0b11) {
                    case 0: op = rv_op_insb; break;
                    case 1:
                        switch ((inst >> 20) & 0b111) {
                        case 0: op = rv_op_sunpkd810; break;
                        case 1: op = rv_op_sunpkd820; break;
                        case 2: op = rv_op_sunpkd830; break;
                        case 3: op = rv_op_sunpkd831; break;
                        case 4: op = rv_op_zunpkd810; break;
                        case 5: op = rv_op_zunpkd820; break;
                        case 6: op = rv_op_zunpkd830; break;
                        case 7: op = rv_op_zunpkd831; break;
                        }
                        break;
                    case 2:
                        switch ((inst >> 20) & 0b111) {
                        case 0: op = rv_op_kabs8; break;
                        case 1: op = rv_op_kabs16; break;
                        case 2: op = rv_op_kabs32; break;
                        case 3: op = rv_op_sunpkd832; break;
                        case 4: op = rv_op_kabsw; break;
                        case 7: op = rv_op_zunpkd832; break;
                        }
                        break;
                    case 3:
                        switch ((inst >> 20) & 0b111) {
                        case 0: op = rv_op_swap8; break;
                        case 1: op = rv_op_swap16; break;
                        }
                        break;
                    }
                    break;
                case 0b1010111:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b01000: op = rv_op_clrs16; break;
                    case 0b01001: op = rv_op_clz16; break;
                    case 0b01011: op = rv_op_clo16; break;
                    case 0b00000: op = rv_op_clrs8; break;
                    case 0b00001: op = rv_op_clz8; break;
                    case 0b00011: op = rv_op_clo8; break;
                    case 0b11000: op = rv_op_clrs32; break;
                    case 0b11001: op = rv_op_clz32; break;
                    case 0b11011: op = rv_op_clo32; break;
                    }
                    break;
                case 0b1000100: op = rv_op_smin8; break;
                case 0b1001100: op = rv_op_umin8; break;
                case 0b1000101: op = rv_op_smax8; break;
                case 0b1001101: op = rv_op_umax8; break;
                case 0b1000110:
                    switch ((inst >> 23) & 0b11) {
                    case 0: op = rv_op_sclip8; break;
                    case 2: op = rv_op_uclip8; break;
                    }
                    break;
                case 0b1110010: op = rv_op_sclip32; break;
                case 0b1111010: op = rv_op_uclip32; break;
                case 0b1111110: op = rv_op_pbsad; break;
                case 0b1111111: op = rv_op_pbsada; break;
                case 0b1100100: op = rv_op_smaqa; break;
                case 0b1100110: op = rv_op_umaqa; break;
                case 0b1100101: op = rv_op_smaqa_su; break;
                case 0b1111001: op = rv_op_maxw; break;
                case 0b1111000: op = rv_op_minw; break;
                case 0b1110000: op = rv_op_ave; break;
                case 0b1110011: op = rv_op_bitrev; break;
                case 0b1100111: op = rv_op_wext; break;
                case 0b1101111: op = rv_op_wexti; break;
                case 0b1110100:
                case 0b1110101: op = rv_op_bitrevi; break;
                }
                break;
            case 1:
                switch ((inst >> 25) & 0b1111111) { /* function 7 */
                case 0b0000111: op = rv_op_pkbb16; break;
                case 0b0001111: op = rv_op_pkbt16; break;
                case 0b0010111: op = rv_op_pktt16; break;
                case 0b0011111: op = rv_op_pktb16; break;
                case 0b0100000: op = rv_op_smmul; break;
                case 0b0101000: op = rv_op_smmul_u; break;
                case 0b0110000: op = rv_op_kmmac; break;
                case 0b0111000: op = rv_op_kmmac_u; break;
                case 0b0100001: op = rv_op_kmmsb; break;
                case 0b0101001: op = rv_op_kmmsb_u; break;
                case 0b0110001: op = rv_op_kwmmul; break;
                case 0b0111001: op = rv_op_kwmmul_u; break;
                case 0b0100010: op = rv_op_smmwb; break;
                case 0b0101010: op = rv_op_smmwb_u; break;
                case 0b0110010: op = rv_op_smmwt; break;
                case 0b0111010: op = rv_op_smmwt_u; break;
                case 0b0100011: op = rv_op_kmmawb; break;
                case 0b0101011: op = rv_op_kmmawb_u; break;
                case 0b0110011: op = rv_op_kmmawt; break;
                case 0b0111011: op = rv_op_kmmawt_u; break;
                case 0b1000111: op = rv_op_kmmwb2; break;
                case 0b1001111: op = rv_op_kmmwb2_u; break;
                case 0b1010111: op = rv_op_kmmwt2; break;
                case 0b1011111: op = rv_op_kmmwt2_u; break;
                case 0b1100111: op = rv_op_kmmawb2; break;
                case 0b1101111: op = rv_op_kmmawb2_u; break;
                case 0b1110111: op = rv_op_kmmawt2; break;
                case 0b1111111: op = rv_op_kmmawt2_u; break;
                case 0b0000100: op = rv_op_smbb16; break;
                case 0b0001100: op = rv_op_smbt16; break;
                case 0b0010100: op = rv_op_smtt16; break;
                case 0b0011100: op = rv_op_kmda; break;
                case 0b0011101: op = rv_op_kmxda; break;
                case 0b0101100: op = rv_op_smds; break;
                case 0b0110100: op = rv_op_smdrs; break;
                case 0b0111100: op = rv_op_smxds; break;
                case 0b0101101: op = rv_op_kmabb; break;
                case 0b0110101: op = rv_op_kmabt; break;
                case 0b0111101: op = rv_op_kmatt; break;
                case 0b0100100: op = rv_op_kmada; break;
                case 0b0100101: op = rv_op_kmaxda; break;
                case 0b0101110: op = rv_op_kmads; break;
                case 0b0110110: op = rv_op_kmadrs; break;
                case 0b0111110: op = rv_op_kmaxds; break;
                case 0b0100110: op = rv_op_kmsda; break;
                case 0b0100111: op = rv_op_kmsxda; break;
                case 0b0101111: op = rv_op_smal; break;
                case 0b1100000: op = rv_op_add64; break;
                case 0b1000000: op = rv_op_radd64; break;
                case 0b1010000: op = rv_op_uradd64; break;
                case 0b1001000: op = rv_op_kadd64; break;
                case 0b1011000: op = rv_op_ukadd64; break;
                case 0b1100001: op = rv_op_sub64; break;
                case 0b1000001: op = rv_op_rsub64; break;
                case 0b1010001: op = rv_op_ursub64; break;
                case 0b1001001: op = rv_op_ksub64; break;
                case 0b1011001: op = rv_op_uksub64; break;
                case 0b1000010: op = rv_op_smar64; break;
                case 0b1000011: op = rv_op_smsr64; break;
                case 0b1010010: op = rv_op_umar64; break;
                case 0b1010011: op = rv_op_umsr64; break;
                case 0b1001010: op = rv_op_kmar64; break;
                case 0b1001011: op = rv_op_kmsr64; break;
                case 0b1011010: op = rv_op_ukmar64; break;
                case 0b1011011: op = rv_op_ukmsr64; break;
                case 0b1000100: op = rv_op_smalbb; break;
                case 0b1001100: op = rv_op_smalbt; break;
                case 0b1010100: op = rv_op_smaltt; break;
                case 0b1000110: op = rv_op_smalda; break;
                case 0b1001110: op = rv_op_smalxda; break;
                case 0b1000101: op = rv_op_smalds; break;
                case 0b1001101: op = rv_op_smaldrs; break;
                case 0b1010101: op = rv_op_smalxds; break;
                case 0b1010110: op = rv_op_smslda; break;
                case 0b1011110: op = rv_op_smslxda; break;
                case 0b0000010: op = rv_op_kaddh; break;
                case 0b0000011: op = rv_op_ksubh; break;
                case 0b0000110: op = rv_op_khmbb; break;
                case 0b0001110: op = rv_op_khmbt; break;
                case 0b0010110: op = rv_op_khmtt; break;
                case 0b0001010: op = rv_op_ukaddh; break;
                case 0b0001011: op = rv_op_uksubh; break;
                case 0b0000000: op = rv_op_kaddw; break;
                case 0b0001000: op = rv_op_ukaddw; break;
                case 0b0000001: op = rv_op_ksubw; break;
                case 0b0001001: op = rv_op_uksubw; break;
                case 0b0000101: op = rv_op_kdmbb; break;
                case 0b0001101: op = rv_op_kdmbt; break;
                case 0b0010101: op = rv_op_kdmtt; break;
                case 0b0110111: op = rv_op_kslraw; break;
                case 0b0111111: op = rv_op_kslraw_u; break;
                case 0b0010011: op = rv_op_ksllw; break;
                case 0b0011011: op = rv_op_kslliw; break;
                case 0b1101001: op = rv_op_kdmabb; break;
                case 0b1110001: op = rv_op_kdmabt; break;
                case 0b1111001: op = rv_op_kdmatt; break;
                case 0b0010000: op = rv_op_raddw; break;
                case 0b0011000: op = rv_op_uraddw; break;
                case 0b0010001: op = rv_op_rsubw; break;
                case 0b0011001: op = rv_op_ursubw; break;
                case 0b1111000: op = rv_op_mulr64; break;
                case 0b1110000: op = rv_op_mulsr64; break;
                case 0b0010010: op = rv_op_sra_u; break;
                case 0b1101010:
                case 0b1101011: op = rv_op_srai_u; break;
                case 0b1101110: op = rv_op_khmbb16; break;
                case 0b1110110: op = rv_op_khmbt16; break;
                case 0b1111110: op = rv_op_khmtt16; break;
                case 0b1101101: op = rv_op_kdmbb16; break;
                case 0b1110101: op = rv_op_kdmbt16; break;
                case 0b1111101: op = rv_op_kdmtt16; break;
                case 0b1101100: op = rv_op_kdmabb16; break;
                case 0b1110100: op = rv_op_kdmabt16; break;
                case 0b1111100: op = rv_op_kdmatt16; break;
                case 0b0011010: op = rv_op_sraiw_u; break;
                case 0b1100010: op = rv_op_maddr32; break;
                case 0b1100011: op = rv_op_msubr32; break;
                }
                break;
            case 2:
                switch ((inst >> 25) & 0b1111111) { /* function 7 */
                case 0b1111010: op = rv_op_stas16; break;
                case 0b1011010: op = rv_op_rstas16; break;
                case 0b1101010: op = rv_op_urstas16; break;
                case 0b1100010: op = rv_op_kstas16; break;
                case 0b1110010: op = rv_op_ukstas16; break;
                case 0b1111011: op = rv_op_stsa16; break;
                case 0b1011011: op = rv_op_rstsa16; break;
                case 0b1101011: op = rv_op_urstsa16; break;
                case 0b1100011: op = rv_op_kstsa16; break;
                case 0b1110011: op = rv_op_ukstsa16; break;
                case 0b0100000: op = rv_op_add32; break;
                case 0b0000000: op = rv_op_radd32; break;
                case 0b0010000: op = rv_op_uradd32; break;
                case 0b0001000: op = rv_op_kadd32; break;
                case 0b0011000: op = rv_op_ukadd32; break;
                case 0b0100001: op = rv_op_sub32; break;
                case 0b0000001: op = rv_op_rsub32; break;
                case 0b0010001: op = rv_op_ursub32; break;
                case 0b0001001: op = rv_op_ksub32; break;
                case 0b0011001: op = rv_op_uksub32; break;
                case 0b0100010: op = rv_op_cras32; break;
                case 0b0000010: op = rv_op_rcras32; break;
                case 0b0010010: op = rv_op_urcras32; break;
                case 0b0001010: op = rv_op_kcras32; break;
                case 0b0011010: op = rv_op_ukcras32; break;
                case 0b0100011: op = rv_op_crsa32; break;
                case 0b0000011: op = rv_op_rcrsa32; break;
                case 0b0010011: op = rv_op_urcrsa32; break;
                case 0b0001011: op = rv_op_kcrsa32; break;
                case 0b0011011: op = rv_op_ukcrsa32; break;
                case 0b1111000: op = rv_op_stas32; break;
                case 0b1011000: op = rv_op_rstas32; break;
                case 0b1101000: op = rv_op_urstas32; break;
                case 0b1100000: op = rv_op_kstas32; break;
                case 0b1110000: op = rv_op_ukstas32; break;
                case 0b1111001: op = rv_op_stsa32; break;
                case 0b1011001: op = rv_op_rstsa32; break;
                case 0b1101001: op = rv_op_urstsa32; break;
                case 0b1100001: op = rv_op_kstsa32; break;
                case 0b1110001: op = rv_op_ukstsa32; break;
                case 0b0101000: op = rv_op_sra32; break;
                case 0b0110000: op = rv_op_sra32_u; break;
                case 0b0111000: op = rv_op_srai32; break;
                case 0b1000000: op = rv_op_srai32_u; break;
                case 0b0101001: op = rv_op_srl32; break;
                case 0b0110001: op = rv_op_srl32_u; break;
                case 0b0111001: op = rv_op_srli32; break;
                case 0b1000001: op = rv_op_srli32_u; break;
                case 0b0101010: op = rv_op_sll32; break;
                case 0b0111010: op = rv_op_slli32; break;
                case 0b0110010: op = rv_op_ksll32; break;
                case 0b1000010: op = rv_op_kslli32; break;
                case 0b0101011: op = rv_op_kslra32; break;
                case 0b0110011: op = rv_op_kslra32_u; break;
                case 0b1001000: op = rv_op_smin32; break;
                case 0b1010000: op = rv_op_umin32; break;
                case 0b1001001: op = rv_op_smax32; break;
                case 0b1010001: op = rv_op_umax32; break;
                case 0b0001100: op = rv_op_smbt32; break;
                case 0b0010100: op = rv_op_smtt32; break;
                case 0b0101101: op = rv_op_kmabb32; break;
                case 0b0110101: op = rv_op_kmabt32; break;
                case 0b0111101: op = rv_op_kmatt32; break;
                case 0b0011100: op = rv_op_kmda32; break;
                case 0b0011101: op = rv_op_kmxda32; break;
                case 0b0100101: op = rv_op_kmaxda32; break;
                case 0b0101110: op = rv_op_kmads32; break;
                case 0b0110110: op = rv_op_kmadrs32; break;
                case 0b0111110: op = rv_op_kmaxds32; break;
                case 0b0100110: op = rv_op_kmsda32; break;
                case 0b0100111: op = rv_op_kmsxda32; break;
                case 0b0101100: op = rv_op_smds32; break;
                case 0b0110100: op = rv_op_smdrs32; break;
                case 0b0111100: op = rv_op_smxds32; break;
                case 0b0000111: op = rv_op_pkbb32; break;
                case 0b0001111: op = rv_op_pkbt32; break;
                case 0b0010111: op = rv_op_pktt32; break;
                case 0b0011111: op = rv_op_pktb32; break;
                }
                break;
            case 3: op = rv_op_bpick; break;
            case 7:
                switch ((inst >> 25) & 0b1111111) { /* function 7 */
                case 0b0010010:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b00000: op = rv_op_expd80; break;
                    case 0b00001: op = rv_op_expd81; break;
                    case 0b00010: op = rv_op_expd82; break;
                    case 0b00011: op = rv_op_expd83; break;
                    case 0b00100: op = rv_op_expd84; break;
                    case 0b00101: op = rv_op_expd85; break;
                    case 0b00110: op = rv_op_expd86; break;
                    case 0b00111: op = rv_op_expd87; break;
                    }
                    break;
                case 0b1000111: op = rv_op_dkhm8; break;
                case 0b1000011: op = rv_op_dkhm16; break;
                case 0b1010110:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b10000: op = rv_op_dkabs8; break;
                    case 0b10001: op = rv_op_dkabs16; break;
                    }
                    break;
                case 0b0101111: op = rv_op_dkslra8; break;
                case 0b0101011: op = rv_op_dkslra16; break;
                case 0b0001100: op = rv_op_dkadd8; break;
                case 0b0001000: op = rv_op_dkadd16; break;
                case 0b0001101: op = rv_op_dksub8; break;
                case 0b0001001: op = rv_op_dksub16; break;
                }
                break;
            }
            break;
        case 0b11110:
            switch ((inst >> 12) & 0b111) { /* function 3 */
            case 0:
                switch ((inst >> 25) & 0b1111111) { /* function 7 */
                case 0b0000000: op = rv_op_dkhmx8; break;
                case 0b0000001: op = rv_op_dkhmx16; break;
                case 0b0000010: op = rv_op_dsmmul; break;
                case 0b0000011: op = rv_op_dsmmul_u; break;
                case 0b0000100: op = rv_op_dkwmmul; break;
                case 0b0000101: op = rv_op_dkwmmul_u; break;
                case 0b0000110:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b00000: op = rv_op_dkabs32; break;
                    case 0b00001: op = rv_op_dkclip64; break;
                    case 0b00010: op = rv_op_dredas16; break;
                    case 0b00011: op = rv_op_dredsa16; break;
                    case 0b00100: op = rv_op_dsunpkd810; break;
                    case 0b00101: op = rv_op_dsunpkd820; break;
                    case 0b00110: op = rv_op_dsunpkd830; break;
                    case 0b00111: op = rv_op_dsunpkd831; break;
                    case 0b01000: op = rv_op_dsunpkd832; break;
                    case 0b01001: op = rv_op_dzunpkd810; break;
                    case 0b01010: op = rv_op_dzunpkd820; break;
                    case 0b01011: op = rv_op_dzunpkd830; break;
                    case 0b01100: op = rv_op_dzunpkd831; break;
                    case 0b01101: op = rv_op_dzunpkd832; break;
                    }
                    break;
                case 0b0000111: op = rv_op_dkslra32; break;
                case 0b0001000: op = rv_op_dkadd32; break;
                case 0b0001001: op = rv_op_dksub32; break;
                case 0b0001010: op = rv_op_dkmmac; break;
                case 0b0001011: op = rv_op_dkmmac_u; break;
                case 0b0001100: op = rv_op_dkmmsb; break;
                case 0b0001101: op = rv_op_dkmmsb_u; break;
                case 0b0001110: op = rv_op_dkmada; break;
                case 0b0001111: op = rv_op_dkmaxda; break;
                case 0b0010000: op = rv_op_dkmads; break;
                case 0b0010001: op = rv_op_dkmadrs; break;
                case 0b0010010: op = rv_op_dkmaxds; break;
                case 0b0010011: op = rv_op_dkmsda; break;
                case 0b0010100: op = rv_op_dkmsxda; break;
                case 0b0010101: op = rv_op_dsmaqa; break;
                case 0b0010110: op = rv_op_dsmaqa_su; break;
                case 0b0010111: op = rv_op_dumaqa; break;
                case 0b0011000: op = rv_op_dkmda32; break;
                case 0b0011001: op = rv_op_dkmxda32; break;
                case 0b0011010: op = rv_op_dkmada32; break;
                case 0b0011011: op = rv_op_dkmaxda32; break;
                case 0b0011100: op = rv_op_dkmads32; break;
                case 0b0011101: op = rv_op_dkmadrs32; break;
                case 0b0011110: op = rv_op_dkmaxds32; break;
                case 0b0011111: op = rv_op_dkmsda32; break;
                case 0b0100000: op = rv_op_dkmsxda32; break;
                case 0b0100001: op = rv_op_dsmds32; break;
                case 0b0100010: op = rv_op_dsmdrs32; break;
                case 0b0100011: op = rv_op_dsmxds32; break;
                case 0b0100100: op = rv_op_dsmalda; break;
                case 0b0100101: op = rv_op_dsmalxda; break;
                case 0b0100110: op = rv_op_dsmalds; break;
                case 0b0100111: op = rv_op_dsmaldrs; break;
                case 0b0101000: op = rv_op_dsmalxds; break;
                case 0b0101001: op = rv_op_dsmslda; break;
                case 0b0101010: op = rv_op_dsmslxda; break;
                case 0b0101011: op = rv_op_ddsmaqa; break;
                case 0b0101100: op = rv_op_ddsmaqa_su; break;
                case 0b0101101: op = rv_op_ddumaqa; break;
                case 0b0101110: op = rv_op_dradd16; break;
                case 0b0101111: op = rv_op_dsub16; break;
                case 0b0110000: op = rv_op_dradd32; break;
                case 0b0110001: op = rv_op_dsub32; break;
                case 0b0110010: op = rv_op_dkmda; break;
                case 0b0110011: op = rv_op_dkmxda; break;
                case 0b0110100: op = rv_op_dsmdrs; break;
                case 0b0110101: op = rv_op_dsmxds; break;
                case 0b0110110: op = rv_op_dsmbb32; break;
                case 0b0110111: op = rv_op_dsmbb32_sra14; break;
                case 0b0111000: op = rv_op_dsmbb32_sra32; break;
                case 0b0111001: op = rv_op_dsmbt32; break;
                case 0b0111010: op = rv_op_dsmbt32_sra14; break;
                case 0b0111011: op = rv_op_dsmbt32_sra32; break;
                case 0b0111100: op = rv_op_dsmtt32; break;
                case 0b0111101: op = rv_op_dsmtt32_sra14; break;
                case 0b0111110: op = rv_op_dsmtt32_sra32; break;
                case 0b0111111: op = rv_op_dpkbb32; break;
                case 0b1000000: op = rv_op_dpkbt32; break;
                case 0b1100110: op = rv_op_dpack32; break;
                case 0b1000001: op = rv_op_dpktt32; break;
                case 0b1000010: op = rv_op_dpktb32; break;
                case 0b1000011: op = rv_op_dpktb16; break;
                case 0b1000100: op = rv_op_dpkbb16; break;
                case 0b1000101: op = rv_op_dpkbt16; break;
                case 0b1000110: op = rv_op_dpktt16; break;
                case 0b1000111: op = rv_op_dsra16; break;
                case 0b1001000: op = rv_op_dadd16; break;
                case 0b1001001: op = rv_op_dadd32; break;
                case 0b1001010: op = rv_op_dsmbb16; break;
                case 0b1001011: op = rv_op_dsmbt16; break;
                case 0b1001100: op = rv_op_dsmtt16; break;
                case 0b1001101: op = rv_op_drcrsa16; break;
                case 0b1001110: op = rv_op_drcras16; break;
                case 0b1001111: op = rv_op_dkcrsa16; break;
                case 0b1010000: op = rv_op_dkcras16; break;
                case 0b1010001: op = rv_op_drsub16; break;
                case 0b1010010: op = rv_op_drsub32; break;
                case 0b1010011: op = rv_op_dstsa32; break;
                case 0b1010100: op = rv_op_dstas32; break;
                case 0b1010101: op = rv_op_dkcras32; break;
                case 0b1010110: op = rv_op_dkcrsa32; break;
                case 0b1010111: op = rv_op_dcrsa32; break;
                case 0b1011000: op = rv_op_dcras32; break;
                case 0b1011001: op = rv_op_dkstsa16; break;
                case 0b1011010: op = rv_op_dkstas16; break;
                case 0b1011011: op = rv_op_dsclip8; break;
                case 0b1011100: op = rv_op_dsclip16; break;
                case 0b1011101: op = rv_op_dsclip32; break;
                case 0b1011110: op = rv_op_drcrsa32; break;
                case 0b1110010: op = rv_op_drcras32; break;
                case 0b1011111: op = rv_op_dmsr16; break;
                case 0b1100000: op = rv_op_dmsr17; break;
                case 0b1100001: op = rv_op_dmsr33; break;
                case 0b1110011: op = rv_op_dmxsr33; break;
                case 0b1100010: op = rv_op_dsmada16; break;
                case 0b1100011: op = rv_op_dsmaxda16; break;
                case 0b1100100: op = rv_op_dksms32_u; break;
                case 0b1100101: op = rv_op_dmada32; break;
                case 0b1101000: op = rv_op_dsma32_u; break;
                case 0b1101001: op = rv_op_dsmxs32_u; break;
                case 0b1101010: op = rv_op_dsmxa32_u; break;
                case 0b1101011: op = rv_op_dsms32_u; break;
                case 0b1101100: op = rv_op_dsmalbb; break;
                case 0b1101101: op = rv_op_dsmalbt; break;
                case 0b1101110: op = rv_op_dsmaltt; break;
                case 0b1101111: op = rv_op_dkmabb32; break;
                case 0b1110000: op = rv_op_dkmabt32; break;
                case 0b1110001: op = rv_op_dkmatt32; break;

                }
                break;
            }
            break;
        }
        break;
    }

    dec->op = op;
}
