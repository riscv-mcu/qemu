/*
 * QEMU RISC-V sMPU (Supervisor Physical Memory Protection)
 *
 * This provides a RISC-V Supervisor Physical Memory Protection implementation
 * following the Nuclei Ssmpu Extension v0.9.0 specification.
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

#ifndef RISCV_SMPU_H
#define RISCV_SMPU_H

#include "cpu.h"

#define MAX_RISCV_SMPUS 64
#define MAX_RISCV_SMPU_CFGS 16

typedef enum {
    SMPU_READ  = 1 << 0,
    SMPU_WRITE = 1 << 1,
    SMPU_EXEC  = 1 << 2,
    SMPU_AMATCH = (3 << 3),
    SMPU_S     = 1 << 7
} smpu_priv_t;

typedef enum {
    SMPU_AMATCH_OFF,   /* 00: Null (off) */
    SMPU_AMATCH_TOR,   /* 01: Top of Range (Nuclei not supported) */
    SMPU_AMATCH_NA4,   /* 10: Naturally aligned four-byte region */
    SMPU_AMATCH_NAPOT  /* 11: Naturally aligned power-of-two region */
} smpu_am_t;

typedef struct {
    target_ulong addr_reg;
    uint8_t  cfg_reg;
} smpu_entry_t;

typedef struct {
    hwaddr sa;
    hwaddr ea;
} smpu_addr_t;

typedef struct {
    smpu_entry_t smpu[MAX_RISCV_SMPUS];
    smpu_addr_t  addr[MAX_RISCV_SMPUS];
    uint32_t num_rules;
} smpu_table_t;

void smpucfg_csr_write(CPURISCVState *env, uint32_t reg_index,
                       target_ulong val);
target_ulong smpucfg_csr_read(CPURISCVState *env, uint32_t reg_index);

void smpuaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
                        target_ulong val);
target_ulong smpuaddr_csr_read(CPURISCVState *env, uint32_t addr_index);

void smpuswitch_csr_write(CPURISCVState *env, uint32_t index,
                          target_ulong val);
target_ulong smpuswitch_csr_read(CPURISCVState *env, uint32_t index);

bool smpu_hart_has_privs(CPURISCVState *env, hwaddr addr,
                         target_ulong size, smpu_priv_t privs,
                         smpu_priv_t *allowed_privs, int mmu_idx);

void smpu_update_rule_addr(CPURISCVState *env, uint32_t smpu_index);
void smpu_update_rule_nums(CPURISCVState *env);
uint32_t smpu_get_num_rules(CPURISCVState *env);
target_ulong smpu_get_tlb_size(CPURISCVState *env, hwaddr addr);

#endif
