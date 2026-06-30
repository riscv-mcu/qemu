/*
 * NUCLEI USART v3.1.0 interface
 *
 * Copyright (c) 2024 Nuclei.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NUCLEI_USART_H
#define HW_NUCLEI_USART_H

#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_NUCLEI_USART "riscv.nuclei.usart"
OBJECT_DECLARE_SIMPLE_TYPE(NucleiUSARTState, NUCLEI_USART)

#define NUCLEI_USART_VERSION_3_1_0           0x00030100U

#define NUCLEI_USART_REG_TXDATA              0x000
#define NUCLEI_USART_REG_RXDATA              0x004
#define NUCLEI_USART_REG_TXCTRL              0x008
#define NUCLEI_USART_REG_RXCTRL              0x00C
#define NUCLEI_USART_REG_INT_EN              0x010
#define NUCLEI_USART_REG_STATUS              0x014
#define NUCLEI_USART_REG_DIV                 0x018
#define NUCLEI_USART_REG_SETUP               0x01C
#define NUCLEI_USART_REG_RX_SIZE             0x024
#define NUCLEI_USART_REG_TX_SIZE             0x030
#define NUCLEI_USART_REG_SPI_SLAVE           0x034
#define NUCLEI_USART_REG_RX_IDLE             0x044
#define NUCLEI_USART_REG_RX_WM               0x048
#define NUCLEI_USART_REG_RX_FIFO_LEFT_ENTRY  0x04C
#define NUCLEI_USART_REG_TX_FIFO_LEFT_ENTRY  0x050
#define NUCLEI_USART_REG_TX_DATASIZE         0x054
#define NUCLEI_USART_REG_RX_DATASIZE         0x058
#define NUCLEI_USART_REG_SMARTCARD_SETUP     0x060
#define NUCLEI_USART_REG_SMARTCARD_TIMING    0x064
#define NUCLEI_USART_REG_IP_VERSION          0x068
#define NUCLEI_USART_REG_ADVANCED_SETUP      0x07C
#define NUCLEI_USART_REG_ADVANCED_STATUS     0x080
#define NUCLEI_USART_FIFO_DEPTH              16U

typedef struct NucleiUSARTState
{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    qemu_irq irq;
    MemoryRegion mmio;
    CharBackend chr;
    QEMUTimer *tx_busy_timer;
    QEMUTimer *rx_busy_timer;
    QEMUTimer *rx_idle_timer;
    QEMUTimer *rx_wm_timer;
    QEMUTimer *lin_break_timer;
    int64_t tx_busy_deadline;
    int64_t rx_busy_deadline;
    int64_t rx_busy_interval_ns;
    uint8_t tx_eot_pending;
    uint8_t rx_busy_bonus_pending;
    uint8_t tx_size_progress;
    uint8_t rx_size_progress;
    uint16_t tx_fifo[NUCLEI_USART_FIFO_DEPTH];
    uint8_t tx_fifo_head;
    uint8_t tx_fifo_tail;
    unsigned int tx_fifo_len;
    uint16_t rx_fifo[NUCLEI_USART_FIFO_DEPTH];
    uint8_t rx_fifo_head;
    uint8_t rx_fifo_tail;
    unsigned int rx_fifo_len;

    uint32_t txdata;
    uint32_t rxdata;
    uint32_t txctrl;
    uint32_t rxctrl;
    uint32_t div;
    uint32_t setup;
    uint32_t usart_int_en;
    uint32_t usart_status;
    uint32_t usart_rx_size;
    uint32_t usart_tx_size;
    uint32_t usart_spi_slave;
    uint32_t usart_rx_idle;
    uint32_t usart_rx_wm;
    uint32_t usart_tx_datasize;
    uint32_t usart_rx_datasize;
    uint32_t usart_smartcard_setup;
    uint32_t usart_smartcard_timing;
    uint32_t usart_advanced_setup;
    uint32_t usart_advanced_status;
    uint32_t version;
} NucleiUSARTState;

NucleiUSARTState *nuclei_usart_create(hwaddr base, uint64_t size,
                      Chardev *chr, qemu_irq irq, uint32_t version);

#endif
