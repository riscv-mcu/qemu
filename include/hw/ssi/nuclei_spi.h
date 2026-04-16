/*
 * Nuclei QSPI Controller.
 *
 * Implement Nuclei spi spec V1.2.8.
 *
 * Copyright (c) 2026 Nucleisys, Inc.
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

#define NUCLEI_SPI_REG_NUM  (0x88 / 4)

/* Nuclei QSPI Regs */
#define NUCLEI_SPI_SCKDIV        (0x00 / 4)
#define NUCLEI_SPI_SCKMODE       (0x04 / 4)
#define NUCLEI_SPI_DDR_SCKSAMPLE (0x08 / 4)
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
#define NUCLEI_SPI_SDR_SCKSAMPLE (0x80 / 4)
#define NUCLEI_SPI_CR            (0x84 / 4)

#define FMT_PROTO_MASK  0x3
#define FMT_ENDIAN      (1 << 2)
#define FMT_DIR         (1 << 3)
#define FMT_PROTO_HI    (1 << 4)
#define FMT_LEN_SHIFT   16
#define FMT_LEN_MASK    (0x3f << FMT_LEN_SHIFT)

#define FORCE_EN        (1 << 0)
#define FORCE_WP        (1 << 1)

#define CSMODE_AUTO     0
#define CSMODE_HOLD     2
#define CSMODE_OFF      3

#define TXDATA_FULL     (1 << 31)
#define RXDATA_EMPTY    (1 << 31)

#define IE_TXWM         (1 << 0)
#define IE_RXWM         (1 << 1)
#define IE_TXUDR        (1 << 2)
#define IE_RXOVR        (1 << 3)
#define IE_RXUDR        (1 << 4)
#define IE_TXOVR        (1 << 5)
#define IE_DONE         (1 << 7)
#define IE_TXDONE       (1 << 11)
#define IE_RXDONE       (1 << 12)
#define IE_CFGERR       (1 << 14)
#define IE_MASK         (IE_TXWM | IE_RXWM | IE_TXUDR | IE_RXOVR | IE_RXUDR | \
                         IE_TXOVR | IE_DONE | IE_TXDONE | IE_RXDONE | IE_CFGERR)

#define IP_TXWM         (1 << 0)
#define IP_RXWM         (1 << 1)
#define IP_MASK         (IP_TXWM | IP_RXWM)

#define STATUS_BUSY     (1 << 0)
#define STATUS_OVR      (1 << 2)
#define STATUS_UDR      (1 << 3)
#define STATUS_TXFULL   (1 << 4)
#define STATUS_RXEMPTY  (1 << 5)
#define STATUS_RXUDR    (1 << 9)
#define STATUS_TXOVR    (1 << 10)
#define STATUS_TXEMPTY  (1 << 11)
#define STATUS_RXFULL   (1 << 12)
#define STATUS_DONE     (1 << 13)
#define STATUS_TXDONE   (1 << 17)
#define STATUS_RXDONE   (1 << 18)
#define STATUS_CFGERR   (1 << 20)
#define STATUS_W1C_MASK (STATUS_OVR | STATUS_UDR | STATUS_RXUDR | STATUS_TXOVR | \
                         STATUS_DONE | STATUS_TXDONE | STATUS_RXDONE | STATUS_CFGERR)
#define STATUS_IRQ_MASK (STATUS_OVR | STATUS_UDR | STATUS_RXUDR | STATUS_TXOVR | \
                         STATUS_DONE | STATUS_TXDONE | STATUS_RXDONE | STATUS_CFGERR)

#define CR_MSTR         (1 << 0)
#define CR_DMA_EN       (1 << 1)
#define CR_DDR_EN       (1 << 2)
#define CR_CSI          (1 << 3)
#define CR_CSOE         (1 << 4)
#define CR_SSM          (1 << 5)
#define CR_ST_DMA_TX_EN (1 << 8)
#define CR_ST_DMA_RX_EN (1 << 9)
#define CR_ST_DMA_RCONT (1 << 10)
#define CR_ST_DMA_TCONT (1 << 11)
#define CR_RXFIFO_EN    (1 << 13)
#define CR_DQS_MODE     (1 << 23)
#define CR_DQS_DM_EN    (1 << 25)
#define CR_MASK         (CR_MSTR | CR_DMA_EN | CR_DDR_EN | CR_CSI | CR_CSOE | \
                         CR_SSM | CR_ST_DMA_TX_EN | CR_ST_DMA_RX_EN | \
                         CR_ST_DMA_RCONT | CR_ST_DMA_TCONT | CR_RXFIFO_EN | \
                         CR_DQS_MODE | CR_DQS_DM_EN)

#define FCTRL_FLASH_EN  (1 << 0)
#define FCTRL_WMASK_EN  (1 << 1)
#define FCTRL_FLASH_WEN (1 << 2)
#define FCTRL_BURST_EN  (1 << 3)
#define FCTRL_WRAP_EN   (1 << 4)
#define FCTRL_MASK      (FCTRL_FLASH_EN | FCTRL_WMASK_EN | FCTRL_FLASH_WEN | \
                         FCTRL_BURST_EN | FCTRL_WRAP_EN)

#define FFMT_CMD_EN           (1 << 0)
#define FFMT_ADDR_LEN_MASK    (0x7 << 1)
#define FFMT_PAD_CNT_MASK     (0xf << 4)
#define FFMT_CMD_PROTO_MASK   (0x3 << 8)
#define FFMT_ADDR_PROTO_MASK  (0x3 << 10)
#define FFMT_DATA_PROTO_MASK  (0x3 << 12)
#define FFMT_ENDIAN_F         (1 << 14)
#define FFMT_DATA_PROTO_HI    (1 << 15)
#define FFMT_CMD_CODE_MASK    (0xff << 16)
#define FFMT_PAD_CODE_MASK    (0xffu << 24)
#define FFMT_MASK             (FFMT_CMD_EN | FFMT_ADDR_LEN_MASK | FFMT_PAD_CNT_MASK | \
                               FFMT_CMD_PROTO_MASK | FFMT_ADDR_PROTO_MASK | \
                               FFMT_DATA_PROTO_MASK | FFMT_ENDIAN_F | \
                               FFMT_DATA_PROTO_HI | FFMT_CMD_CODE_MASK | \
                               FFMT_PAD_CODE_MASK)

#define FFMT1_WCMD_CODE_MASK  (0xff)
#define FFMT1_WPAD_CNT_MASK   (0x1f << 8)
#define FFMT1_PAD_CNT_H       (1 << 13)
#define FFMT1_DDR_EN_MASK     (0xf << 14)
#define FFMT1_MODE_PROTO_MASK (0x3 << 18)
#define FFMT1_MODE_CODE_MASK  (0xffu << 20)
#define FFMT1_MODE_CNT_MASK   (0xfu << 28)
#define FFMT1_MASK            (FFMT1_WCMD_CODE_MASK | FFMT1_WPAD_CNT_MASK | \
                               FFMT1_PAD_CNT_H | FFMT1_DDR_EN_MASK | \
                               FFMT1_MODE_PROTO_MASK | FFMT1_MODE_CODE_MASK | \
                               FFMT1_MODE_CNT_MASK)

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
    bool cs_active;

    uint32_t regs[NUCLEI_SPI_REG_NUM];
} NucleiSPIState;

#endif /* HW_NUCLEI_SPI_H */
