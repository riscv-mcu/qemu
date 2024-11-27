/*
 * QEMU RISC-V CPU QOM header (target agnostic)
 *
 * Copyright (c) 2023 Ventana Micro Systems Inc.
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

#ifndef RISCV_CPU_QOM_H
#define RISCV_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_RISCV_CPU "riscv-cpu"
#define TYPE_RISCV_DYNAMIC_CPU "riscv-dynamic-cpu"
#define TYPE_RISCV_VENDOR_CPU "riscv-vendor-cpu"
#define TYPE_RISCV_BARE_CPU "riscv-bare-cpu"

#define RISCV_CPU_TYPE_SUFFIX "-" TYPE_RISCV_CPU
#define RISCV_CPU_TYPE_NAME(name) (name RISCV_CPU_TYPE_SUFFIX)

#define TYPE_RISCV_CPU_ANY              RISCV_CPU_TYPE_NAME("any")
#define TYPE_RISCV_CPU_MAX              RISCV_CPU_TYPE_NAME("max")
#define TYPE_RISCV_CPU_BASE32           RISCV_CPU_TYPE_NAME("rv32")
#define TYPE_RISCV_CPU_BASE64           RISCV_CPU_TYPE_NAME("rv64")
#define TYPE_RISCV_CPU_BASE128          RISCV_CPU_TYPE_NAME("x-rv128")
#define TYPE_RISCV_CPU_RV32I            RISCV_CPU_TYPE_NAME("rv32i")
#define TYPE_RISCV_CPU_RV32E            RISCV_CPU_TYPE_NAME("rv32e")
#define TYPE_RISCV_CPU_RV64I            RISCV_CPU_TYPE_NAME("rv64i")
#define TYPE_RISCV_CPU_RV64E            RISCV_CPU_TYPE_NAME("rv64e")
#define TYPE_RISCV_CPU_RVA22U64         RISCV_CPU_TYPE_NAME("rva22u64")
#define TYPE_RISCV_CPU_RVA22S64         RISCV_CPU_TYPE_NAME("rva22s64")
#define TYPE_RISCV_CPU_IBEX             RISCV_CPU_TYPE_NAME("lowrisc-ibex")
#define TYPE_RISCV_CPU_SHAKTI_C         RISCV_CPU_TYPE_NAME("shakti-c")
#define TYPE_RISCV_CPU_SIFIVE_E31       RISCV_CPU_TYPE_NAME("sifive-e31")
#define TYPE_RISCV_CPU_SIFIVE_E34       RISCV_CPU_TYPE_NAME("sifive-e34")
#define TYPE_RISCV_CPU_SIFIVE_E51       RISCV_CPU_TYPE_NAME("sifive-e51")
#define TYPE_RISCV_CPU_SIFIVE_U34       RISCV_CPU_TYPE_NAME("sifive-u34")
#define TYPE_RISCV_CPU_SIFIVE_U54       RISCV_CPU_TYPE_NAME("sifive-u54")
#define TYPE_RISCV_CPU_THEAD_C906       RISCV_CPU_TYPE_NAME("thead-c906")
#define TYPE_RISCV_CPU_VEYRON_V1        RISCV_CPU_TYPE_NAME("veyron-v1")
#define TYPE_RISCV_CPU_HOST             RISCV_CPU_TYPE_NAME("host")
#define TYPE_RISCV_CPU_NUCLEI_N100E     RISCV_CPU_TYPE_NAME("nuclei-n100e")
#define TYPE_RISCV_CPU_NUCLEI_N100EM    RISCV_CPU_TYPE_NAME("nuclei-n100em")
#define TYPE_RISCV_CPU_NUCLEI_N100EZMMUL RISCV_CPU_TYPE_NAME("nuclei-n100ezmmul")
#define TYPE_RISCV_CPU_NUCLEI_N100      RISCV_CPU_TYPE_NAME("nuclei-n100")
#define TYPE_RISCV_CPU_NUCLEI_N100M     RISCV_CPU_TYPE_NAME("nuclei-n100m")
#define TYPE_RISCV_CPU_NUCLEI_N100ZMMUL RISCV_CPU_TYPE_NAME("nuclei-n100zmmul")
#define TYPE_RISCV_CPU_NUCLEI_N200      RISCV_CPU_TYPE_NAME("nuclei-n200")
#define TYPE_RISCV_CPU_NUCLEI_N200E     RISCV_CPU_TYPE_NAME("nuclei-n200e")
#define TYPE_RISCV_CPU_NUCLEI_N201      RISCV_CPU_TYPE_NAME("nuclei-n201")
#define TYPE_RISCV_CPU_NUCLEI_N201E     RISCV_CPU_TYPE_NAME("nuclei-n201e")
#define TYPE_RISCV_CPU_NUCLEI_N202      RISCV_CPU_TYPE_NAME("nuclei-n202")
#define TYPE_RISCV_CPU_NUCLEI_N202E     RISCV_CPU_TYPE_NAME("nuclei-n202e")
#define TYPE_RISCV_CPU_NUCLEI_N203      RISCV_CPU_TYPE_NAME("nuclei-n203")
#define TYPE_RISCV_CPU_NUCLEI_N203E     RISCV_CPU_TYPE_NAME("nuclei-n203e")
#define TYPE_RISCV_CPU_NUCLEI_N205      RISCV_CPU_TYPE_NAME("nuclei-n205")
#define TYPE_RISCV_CPU_NUCLEI_N205E     RISCV_CPU_TYPE_NAME("nuclei-n205e")
#define TYPE_RISCV_CPU_NUCLEI_N300      RISCV_CPU_TYPE_NAME("nuclei-n300")
#define TYPE_RISCV_CPU_NUCLEI_N300F     RISCV_CPU_TYPE_NAME("nuclei-n300f")
#define TYPE_RISCV_CPU_NUCLEI_N300FD    RISCV_CPU_TYPE_NAME("nuclei-n300fd")
#define TYPE_RISCV_CPU_NUCLEI_N305      RISCV_CPU_TYPE_NAME("nuclei-n305")
#define TYPE_RISCV_CPU_NUCLEI_N307      RISCV_CPU_TYPE_NAME("nuclei-n307")
#define TYPE_RISCV_CPU_NUCLEI_N307FD    RISCV_CPU_TYPE_NAME("nuclei-n307fd")
#define TYPE_RISCV_CPU_NUCLEI_N600      RISCV_CPU_TYPE_NAME("nuclei-n600")
#define TYPE_RISCV_CPU_NUCLEI_N600F     RISCV_CPU_TYPE_NAME("nuclei-n600f")
#define TYPE_RISCV_CPU_NUCLEI_N600FD    RISCV_CPU_TYPE_NAME("nuclei-n600fd")
#define TYPE_RISCV_CPU_NUCLEI_U600      RISCV_CPU_TYPE_NAME("nuclei-u600")
#define TYPE_RISCV_CPU_NUCLEI_U600F     RISCV_CPU_TYPE_NAME("nuclei-u600f")
#define TYPE_RISCV_CPU_NUCLEI_U600FD    RISCV_CPU_TYPE_NAME("nuclei-u600fd")
#define TYPE_RISCV_CPU_NUCLEI_NX600     RISCV_CPU_TYPE_NAME("nuclei-nx600")
#define TYPE_RISCV_CPU_NUCLEI_NX600F    RISCV_CPU_TYPE_NAME("nuclei-nx600f")
#define TYPE_RISCV_CPU_NUCLEI_NX600FD   RISCV_CPU_TYPE_NAME("nuclei-nx600fd")
#define TYPE_RISCV_CPU_NUCLEI_UX600     RISCV_CPU_TYPE_NAME("nuclei-ux600")
#define TYPE_RISCV_CPU_NUCLEI_UX600F    RISCV_CPU_TYPE_NAME("nuclei-ux600f")
#define TYPE_RISCV_CPU_NUCLEI_UX600FD   RISCV_CPU_TYPE_NAME("nuclei-ux600fd")
#define TYPE_RISCV_CPU_NUCLEI_N900      RISCV_CPU_TYPE_NAME("nuclei-n900")
#define TYPE_RISCV_CPU_NUCLEI_N900F     RISCV_CPU_TYPE_NAME("nuclei-n900f")
#define TYPE_RISCV_CPU_NUCLEI_N900FD    RISCV_CPU_TYPE_NAME("nuclei-n900fd")
#define TYPE_RISCV_CPU_NUCLEI_U900      RISCV_CPU_TYPE_NAME("nuclei-u900")
#define TYPE_RISCV_CPU_NUCLEI_U900F     RISCV_CPU_TYPE_NAME("nuclei-u900f")
#define TYPE_RISCV_CPU_NUCLEI_U900FD    RISCV_CPU_TYPE_NAME("nuclei-u900fd")
#define TYPE_RISCV_CPU_NUCLEI_NX900     RISCV_CPU_TYPE_NAME("nuclei-nx900")
#define TYPE_RISCV_CPU_NUCLEI_NX900F    RISCV_CPU_TYPE_NAME("nuclei-nx900f")
#define TYPE_RISCV_CPU_NUCLEI_NX900FD   RISCV_CPU_TYPE_NAME("nuclei-nx900fd")
#define TYPE_RISCV_CPU_NUCLEI_NX1000    RISCV_CPU_TYPE_NAME("nuclei-nx1000")
#define TYPE_RISCV_CPU_NUCLEI_NX1000F   RISCV_CPU_TYPE_NAME("nuclei-nx1000f")
#define TYPE_RISCV_CPU_NUCLEI_NX1000FD  RISCV_CPU_TYPE_NAME("nuclei-nx1000fd")
#define TYPE_RISCV_CPU_NUCLEI_UX900     RISCV_CPU_TYPE_NAME("nuclei-ux900")
#define TYPE_RISCV_CPU_NUCLEI_UX900F    RISCV_CPU_TYPE_NAME("nuclei-ux900f")
#define TYPE_RISCV_CPU_NUCLEI_UX900FD   RISCV_CPU_TYPE_NAME("nuclei-ux900fd")
#define TYPE_RISCV_CPU_NUCLEI_UX1000    RISCV_CPU_TYPE_NAME("nuclei-ux1000")
#define TYPE_RISCV_CPU_NUCLEI_UX1000F   RISCV_CPU_TYPE_NAME("nuclei-ux1000f")
#define TYPE_RISCV_CPU_NUCLEI_UX1000FD  RISCV_CPU_TYPE_NAME("nuclei-ux1000fd")

OBJECT_DECLARE_CPU_TYPE(RISCVCPU, RISCVCPUClass, RISCV_CPU)

#endif /* RISCV_CPU_QOM_H */
