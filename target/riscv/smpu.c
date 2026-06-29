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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "cpu.h"
#include "internals.h"
#include "smpu.h"
#include "trace.h"
#include "exec/exec-all.h"

static bool smpu_write_cfg(CPURISCVState *env, uint32_t addr_index,
                           uint8_t val);
static uint8_t smpu_read_cfg(CPURISCVState *env, uint32_t addr_index);

/*
 * Accessor method to extract address matching type 'a field' from cfg reg
 */
static inline uint8_t smpu_get_a_field(uint8_t cfg)
{
    uint8_t a = cfg >> 3;
    return a & 0x3;
}

static inline bool smpu_entry_enabled(CPURISCVState *env, uint32_t smpu_index)
{
    uint32_t reg = smpu_index / 32;
    uint32_t bit = smpu_index % 32;

    if (reg >= ARRAY_SIZE(env->smpuswitch)) {
        return false;
    }

    return (env->smpuswitch[reg] >> bit) & 0x1;
}

static inline bool smpu_entry_active(CPURISCVState *env, uint32_t smpu_index)
{
    return smpu_entry_enabled(env, smpu_index) &&
           smpu_get_a_field(env->smpu_state.smpu[smpu_index].cfg_reg) !=
           SMPU_AMATCH_OFF;
}

/*
 * Decode the effective permissions from one sMPU entry according to
 * Nuclei_RISC-V_ISA_Spec.pdf, section 19.6, Fig. 19.27 "SMPU Check Rule".
 *
 * The S/R/W/X bits are not a plain RWX bitmap. The same encoded entry can
 * grant different effective permissions in S-mode and U-mode, and some
 * S-mode cases are further gated by SUM.
 *
 * The table below records the intended meaning of each encoding so later
 * maintenance can verify the code path directly against the spec:
 *
 *   S R W X | U-mode                    | S-mode
 *   --------+---------------------------+-------------------------------
 *   0 0 0 0 | no access                 | no access
 *   0 0 0 1 | execute                   | no access
 *   0 0 1 0 | read                      | read/write
 *   0 0 1 1 | read/write                | read/write
 *   0 1 0 0 | read                      | SUM=0: no access, SUM=1: read
 *   0 1 0 1 | read/execute              | SUM=0: no access, SUM=1: read
 *   0 1 1 0 | read/write                | SUM=0: no access, SUM=1: read/write
 *   0 1 1 1 | read/write/execute        | SUM=0: no access, SUM=1: read/write
 *   1 0 0 0 | no access                 | no access
 *   1 0 0 1 | no access                 | execute
 *   1 0 1 0 | execute                   | execute
 *   1 0 1 1 | execute                   | read/execute
 *   1 1 0 0 | no access                 | read
 *   1 1 0 1 | no access                 | read/execute
 *   1 1 1 0 | no access                 | read/write
 *   1 1 1 1 | read                      | read
 */
static smpu_priv_t smpu_decode_privs(uint8_t cfg, int mmu_idx)
{
    bool s = cfg & SMPU_S;
    bool r = cfg & SMPU_READ;
    bool w = cfg & SMPU_WRITE;
    bool x = cfg & SMPU_EXEC;
    bool sum = mmuidx_sum(mmu_idx);
    bool u_mode = mmuidx_priv(mmu_idx) == PRV_U;

    if (u_mode) {
        if (!s && !r && !w && !x) {
            return 0;
        } else if (!s && !r && !w && x) {
            return SMPU_EXEC;
        } else if (!s && !r && w && !x) {
            return SMPU_READ;
        } else if (!s && !r && w && x) {
            return SMPU_READ | SMPU_WRITE;
        } else if (!s && r && !w && !x) {
            return SMPU_READ;
        } else if (!s && r && !w && x) {
            return SMPU_READ | SMPU_EXEC;
        } else if (!s && r && w && !x) {
            return SMPU_READ | SMPU_WRITE;
        } else if (!s && r && w && x) {
            return SMPU_READ | SMPU_WRITE | SMPU_EXEC;
        } else if (s && !r && !w && !x) {
            return 0;
        } else if (s && !r && !w && x) {
            return 0;
        } else if (s && !r && w && !x) {
            return SMPU_EXEC;
        } else if (s && !r && w && x) {
            return SMPU_EXEC;
        } else if (s && r && !w && !x) {
            return 0;
        } else if (s && r && !w && x) {
            return 0;
        } else if (s && r && w && !x) {
            return 0;
        } else if (s && r && w && x) {
            return SMPU_READ;
        }
    } else {
        if (!s && !r && !w && !x) {
            return 0;
        } else if (!s && !r && !w && x) {
            return 0;
        } else if (!s && !r && w && !x) {
            return SMPU_READ | SMPU_WRITE;
        } else if (!s && !r && w && x) {
            return SMPU_READ | SMPU_WRITE;
        } else if (!s && r && !w && !x) {
            return sum ? SMPU_READ : 0;
        } else if (!s && r && !w && x) {
            return sum ? SMPU_READ : 0;
        } else if (!s && r && w && !x) {
            return sum ? (SMPU_READ | SMPU_WRITE) : 0;
        } else if (!s && r && w && x) {
            return sum ? (SMPU_READ | SMPU_WRITE) : 0;
        } else if (s && !r && !w && !x) {
            return 0;
        } else if (s && !r && !w && x) {
            return SMPU_EXEC;
        } else if (s && !r && w && !x) {
            return SMPU_EXEC;
        } else if (s && !r && w && x) {
            return SMPU_READ | SMPU_EXEC;
        } else if (s && r && !w && !x) {
            return SMPU_READ;
        } else if (s && r && !w && x) {
            return SMPU_READ | SMPU_EXEC;
        } else if (s && r && w && !x) {
            return SMPU_READ | SMPU_WRITE;
        } else if (s && r && w && x) {
            return SMPU_READ;
        }
    }

    return 0;
}

/*
 * Count the number of active rules.
 */
uint32_t smpu_get_num_rules(CPURISCVState *env)
{
    return env->smpu_state.num_rules;
}

/*
 * Accessor to get the cfg reg for a specific SMPU/HART
 */
static inline uint8_t smpu_read_cfg(CPURISCVState *env, uint32_t smpu_index)
{
    if (smpu_index < MAX_RISCV_SMPUS) {
        return env->smpu_state.smpu[smpu_index].cfg_reg;
    }
    return 0;
}

/*
 * Accessor to set the cfg reg for a specific SMPU/HART
 */
static bool smpu_write_cfg(CPURISCVState *env, uint32_t smpu_index, uint8_t val)
{
    if (smpu_index < MAX_RISCV_SMPUS) {
        if (env->smpu_state.smpu[smpu_index].cfg_reg != val) {
            env->smpu_state.smpu[smpu_index].cfg_reg = val;
            smpu_update_rule_addr(env, smpu_index);
            smpu_update_rule_nums(env);
            tlb_flush(env_cpu(env));
            return true;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring smpucfg write - out of bounds\n");
    }
    return false;
}

/*
 * Update the rule address range after cfg change
 */
void smpu_update_rule_addr(CPURISCVState *env, uint32_t smpu_index)
{
    target_ulong addr = env->smpu_state.smpu[smpu_index].addr_reg;
    uint8_t cfg = env->smpu_state.smpu[smpu_index].cfg_reg;
    uint8_t a = smpu_get_a_field(cfg);
    hwaddr sa = 0;
    hwaddr ea = 0;

    if (smpu_index >= MAX_RISCV_SMPUS) {
        return;
    }

    switch (a) {
    case SMPU_AMATCH_OFF:
    case SMPU_AMATCH_TOR:
        sa = 0;
        ea = 0;
        break;
    case SMPU_AMATCH_NA4:
        sa = (hwaddr)addr << 2;
        ea = sa + 3;
        break;
    case SMPU_AMATCH_NAPOT:
        addr = ((hwaddr)addr << 2) | 0x3;
        sa = addr & (addr + 1);
        ea = addr | (addr + 1);
        break;
    default:
        break;
    }

    env->smpu_state.addr[smpu_index].sa = sa;
    env->smpu_state.addr[smpu_index].ea = ea;
}

/*
 * Update the number of active rules
 */
void smpu_update_rule_nums(CPURISCVState *env)
{
    int i;
    uint32_t num_rules = 0;

    for (i = 0; i < MAX_RISCV_SMPUS; i++) {
        if (smpu_entry_active(env, i)) {
            num_rules = i + 1;
        }
    }
    env->smpu_state.num_rules = num_rules;
}

/*
 * SMPUCSR write handler - smpucfg
 */
void smpucfg_csr_write(CPURISCVState *env, uint32_t reg_index,
                       target_ulong val)
{
    uint32_t smpu_index;
    uint8_t val_u8;

    if (reg_index >= MAX_RISCV_SMPU_CFGS) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        smpu_index = reg_index * 4 + i;
        if (smpu_index >= MAX_RISCV_SMPUS) {
            break;
        }
        val_u8 = (val >> (i * 8)) & 0xFF;
        smpu_write_cfg(env, smpu_index, val_u8);
    }
}

/*
 * SMPU CSR read handler - smpucfg
 */
target_ulong smpucfg_csr_read(CPURISCVState *env, uint32_t reg_index)
{
    target_ulong val = 0;

    if (reg_index >= MAX_RISCV_SMPU_CFGS) {
        return 0;
    }

    for (int i = 0; i < 4; i++) {
        uint32_t smpu_index = reg_index * 4 + i;
        if (smpu_index >= MAX_RISCV_SMPUS) {
            break;
        }
        val |= ((target_ulong)smpu_read_cfg(env, smpu_index)) << (i * 8);
    }

    return val;
}

/*
 * SMPU CSR write handler - smpuaddr
 */
void smpuaddr_csr_write(CPURISCVState *env, uint32_t addr_index,
                        target_ulong val)
{
    if (addr_index < MAX_RISCV_SMPUS) {
        env->smpu_state.smpu[addr_index].addr_reg = val;
        smpu_update_rule_addr(env, addr_index);
        tlb_flush(env_cpu(env));
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring smpuaddr write - out of bounds\n");
    }
}

/*
 * SMPU CSR read handler - smpuaddr
 */
target_ulong smpuaddr_csr_read(CPURISCVState *env, uint32_t addr_index)
{
    if (addr_index < MAX_RISCV_SMPUS) {
        return env->smpu_state.smpu[addr_index].addr_reg;
    }
    return 0;
}

/*
 * SMPU CSR write handler - smpuswitch
 */
void smpuswitch_csr_write(CPURISCVState *env, uint32_t index,
                          target_ulong val)
{
    if (index < 2) {
        if (env->smpuswitch[index] != val) {
            env->smpuswitch[index] = val;
            smpu_update_rule_nums(env);
            tlb_flush(env_cpu(env));
        }
    }
}

/*
 * SMPU CSR read handler - smpuswitch
 */
target_ulong smpuswitch_csr_read(CPURISCVState *env, uint32_t index)
{
    if (index < 2) {
        return env->smpuswitch[index];
    }
    return 0;
}

/*
 * Check if SMPU is applicable for the given mode
 * According to spec:
 * - S-mode and U-mode access is always subject to SMPU
 * - M-mode access with MPRV=1 and MPP!=M is subject to SMPU
 * - M-mode ordinary access is NOT subject to SMPU
 *
 * This helper works on the already-derived effective MMU index, so ordinary
 * M-mode accesses remain PRV_M while MPRV/SUM-adjusted accesses inherit the
 * lower-privilege view from riscv_env_mmu_index().
 */
static inline bool smpu_is_applicable(int mmu_idx)
{
    return mmuidx_priv(mmu_idx) != PRV_M;
}

/*
 * Check if the SMPU entry matches the address range
 */
static inline bool smpu_entry_match(CPURISCVState *env, int smpu_index,
                                    hwaddr addr, target_ulong size)
{
    if (!smpu_entry_active(env, smpu_index)) {
        return false;
    }

    hwaddr sa = env->smpu_state.addr[smpu_index].sa;
    hwaddr ea = env->smpu_state.addr[smpu_index].ea;

    /* Check if the access is within the entry's range */
    if (addr >= sa && (addr + size - 1) <= ea) {
        return true;
    }

    return false;
}

/*
 * Main SMPU access check function
 */
bool smpu_hart_has_privs(CPURISCVState *env, hwaddr addr,
                          target_ulong size, smpu_priv_t privs,
                          smpu_priv_t *allowed_privs, int mmu_idx)
{
    int i;
    smpu_priv_t allowed = 0;

    /* If SMPU is not applicable, it doesn't restrict access */
    if (!smpu_is_applicable(mmu_idx)) {
        *allowed_privs = (SMPU_READ | SMPU_WRITE | SMPU_EXEC);
        return true;
    }

    /*
     * Iterate through all SMPU entries
     * Entry with smallest index has highest priority
     */
    for (i = 0; i < MAX_RISCV_SMPUS; i++) {
        if (smpu_entry_match(env, i, addr, size)) {
            /* Entry matches - check permissions */
            allowed = smpu_decode_privs(env->smpu_state.smpu[i].cfg_reg,
                                        mmu_idx);
            *allowed_privs = allowed;
            return (privs & ~allowed) == 0;
        }
    }

    /*
     * No matching entry found:
     * - If any active SMPU entry exists, U-mode access without a match is
     *   denied, matching the spec's U-mode default-deny rule.
     * - S-mode currently falls back to allow on no-match. This is an
     *   implementation choice pending tighter hardware alignment.
     */
    if (mmuidx_priv(mmu_idx) == PRV_U && smpu_get_num_rules(env) > 0) {
        *allowed_privs = 0;
        return false;
    }

    /* No active rule matched, fall back to allow for non-U accesses. */
    *allowed_privs = (SMPU_READ | SMPU_WRITE | SMPU_EXEC);
    return true;
}

target_ulong smpu_get_tlb_size(CPURISCVState *env, hwaddr addr)
{
    hwaddr smpu_sa;
    hwaddr smpu_ea;
    hwaddr tlb_sa = addr & ~(TARGET_PAGE_SIZE - 1);
    hwaddr tlb_ea = tlb_sa + TARGET_PAGE_SIZE - 1;
    int i;

    if (!riscv_cpu_cfg(env)->smpu || !smpu_get_num_rules(env)) {
        return TARGET_PAGE_SIZE;
    }

    for (i = 0; i < MAX_RISCV_SMPUS; i++) {
        if (!smpu_entry_active(env, i)) {
            continue;
        }

        smpu_sa = env->smpu_state.addr[i].sa;
        smpu_ea = env->smpu_state.addr[i].ea;

        if (smpu_sa <= tlb_sa && smpu_ea >= tlb_ea) {
            return TARGET_PAGE_SIZE;
        } else if ((smpu_sa >= tlb_sa && smpu_sa <= tlb_ea) ||
                   (smpu_ea >= tlb_sa && smpu_ea <= tlb_ea)) {
            return 1;
        }
    }

    return TARGET_PAGE_SIZE;
}
