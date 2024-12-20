/*
 * NUCLEI SMPCC (SMP and Cluster Cache)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NUCLEI_SMPCC_H
#define HW_NUCLEI_SMPCC_H

#include "hw/sysbus.h"

#define TYPE_NUCLEI_SMPCC "riscv.nuclei.smpcc"
typedef struct NucleiSMPCCState NucleiSMPCCState;
DECLARE_INSTANCE_CHECKER(NucleiSMPCCState, NUCLEI_SMPCC,
                        TYPE_NUCLEI_SMPCC)

typedef struct NucleiSMPCCInit
{
    uint32_t smp_ver;
    uint32_t smp_cfg;
    uint32_t cc_cfg;
    uint64_t clm_addr_base;
    uint32_t cc_size;
    uint32_t clm_way_en;

} NucleiSMPCCInit;

typedef struct NucleiSMPCCState
{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    MemoryRegion clm;

    /* Implementaion parameters */
    uint64_t msmpccbase;
    uint32_t aperture_size;
    uint32_t cc_size;

    /* Registers configuration */
    uint32_t smp_ver;
    uint32_t smp_cfg;
    uint32_t cc_cfg;
    uint32_t smp_enb;
    uint32_t cc_ctrl;
    uint32_t cc_mcmd;
    uint32_t cc_err_inj;
    uint32_t cc_recv_cnt;
    uint32_t cc_fatal_cnt;
    uint32_t cc_recv_thv;
    uint32_t cc_fatal_thv;
    uint64_t cc_bus_err_addr;
    uint32_t clint_err_status[32];
    uint32_t cc_scmd;
    uint32_t cc_ucmd;
    uint32_t snoop_pending;
    uint32_t trans_pending;
    uint64_t clm_addr_base;
    uint32_t clm_way_en;
    uint32_t cc_invalid_all;
    uint64_t ns_rg[16];
    uint32_t smp_pmon_sel[16];
    uint64_t smp_pmon_cnt[16];
    uint64_t client_err_addr[32];
    uint32_t client_way_mask[32];

} NucleiSMPCCState;

DeviceState *nuclei_smpcc_create(hwaddr addr, uint32_t aperture_size,
                                NucleiSMPCCInit *smpcc);
#endif
