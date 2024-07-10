/*
 * QEMU disassembler -- RISC-V specific header (nuclei*).
 *
 * Copyright (c) 2024 Nucleisys, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_XXLCZ_H
#define DISAS_RISCV_XXLCZ_H

#include "disas/riscv.h"

extern const rv_opcode_data xxlcz_opcode_data[];

void decode_xxlcz(rv_decode*, rv_isa);

#endif /* DISAS_RISCV_XXLCZ_H */
