/*
 * Nuclei QSPI Controller.
 *
 * Copyright (c) 2024 Nucleisys, Inc.
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

#ifndef HW_NUCLEI_SPI_H
#define HW_NUCLEI_SPI_H

#include "qemu/fifo8.h"
#include "hw/sysbus.h"

#define TYPE_NUCLEI_SPI "nuclei.spi"
#define NUCLEI_SPI(obj) OBJECT_CHECK(NucleiSPIState, (obj), TYPE_NUCLEI_SPI)

#define NUCLEI_SPI_REG_NUM  (0xCC / 4)

/* Nuclei QSPI Regs */
#define NUCLEI_SPI_SCKDIV        (0x00 / 4)
#define NUCLEI_SPI_SCKMODE       (0x04 / 4)
#define NUCLEI_SPI_SCKSAMPLE     (0x08 / 4)
#define NUCLEI_SPI_FORCE         (0x0C / 4)
#define NUCLEI_SPI_CSID          (0x10 / 4)
#define NUCLEI_SPI_CSDEF         (0x14 / 4)
#define NUCLEI_SPI_CSMODE        (0x18 / 4)
#define NUCLEI_SPI_VERSION       (0x1C / 4)
#define NUCLEI_SPI_ADDR_WRAP     (0x20 / 4)
#define NUCLEI_SPI_BOUNDARY_CFG  (0x24 / 4)
#define NUCLEI_SPI_DELAY0        (0x28 / 4)
#define NUCLEI_SPI_DELAY1        (0x2C / 4)
#define NUCLEI_SPI_FIFO_NUM      (0x30 / 4)
#define NUCLEI_SPI_TSIZE         (0x34 / 4)
#define NUCLEI_SPI_RSIZE         (0x38 / 4)
#define NUCLEI_SPI_FMT           (0x40 / 4)
#define NUCLEI_SPI_TXDATA        (0x48 / 4)
#define NUCLEI_SPI_RXDATA        (0x4C / 4)
#define NUCLEI_SPI_TX_MARK       (0x50 / 4)
#define NUCLEI_SPI_RX_MARK       (0x54 / 4)
#define NUCLEI_SPI_FCTRL         (0x60 / 4)
#define NUCLEI_SPI_FFMT          (0x64 / 4)
#define NUCLEI_SPI_IE            (0x70 / 4)
#define NUCLEI_SPI_IP            (0x74 / 4)
#define NUCLEI_SPI_FFMT1         (0x78 / 4)
#define NUCLEI_SPI_STATUS        (0x7C / 4)
#define NUCLEI_SPI_RXEDGE        (0x80 / 4)
#define NUCLEI_SPI_CR            (0x84 / 4)
#define NUCLEI_SPI_CRC_POLY      (0xC0 / 4)
#define NUCLEI_SPI_CRC_TX_VALUE  (0xC4 / 4)
#define NUCLEI_SPI_CRC_RX_VALUE  (0xC8 / 4)

#define FMT_DIR         (1 << 3)

#define TXDATA_FULL     (1 << 31)
#define RXDATA_EMPTY    (1 << 31)

#define IE_TXWM         (1 << 0)
#define IE_RXWM         (1 << 1)

#define IP_TXWM         (1 << 0)
#define IP_RXWM         (1 << 1)

#define FIFO_CAPACITY   8

typedef struct NucleiSPIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t num_cs;
    qemu_irq *cs_lines;

    SSIBus *spi;

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;

    uint32_t regs[NUCLEI_SPI_REG_NUM];
} NucleiSPIState;

#endif /* HW_NUCLEI_SPI_H */
