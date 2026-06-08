/*
 * Nuclei Ethernet Controller (XEC).
 *
 * Copyright (c) 2025 Nucleisys, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef NUCLEI_XEC_H
#define NUCLEI_XEC_H
#include "qom/object.h"

#define TYPE_NUCLEI_XEC "nuclei.xec"
OBJECT_DECLARE_SIMPLE_TYPE(NucleiXECState, NUCLEI_XEC)

#include "net/net.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"

#define XEC_MAXREG              (0x174 / 4 + 1) /* Last valid XEC reg address */

#define DESC_MAX_NUM_WORDS

#define MAX_PRIORITY_QUEUES         8

#define MAX_DMA_RING_INDEX          8

#define MAX_FRAME_SIZE              0x3FFF

enum {
    XEC_RX_PACKET_ADDR_VALID = -2,
    XEC_RX_PACKET_ADDR_INVALID = -1
};

enum {
    XEC_RX_PACKET_ACCEPT = -2,
    XEC_RX_PACKET_DROP = -1
};

// INT_STA_REG bit filed
enum {
    RXF_OF_INT = 0,
    RFD_UR_INT,
    TXF_UR_INT,
    DMAR_TO_INT,
    DMAW_TO_INT,
    TX_PKT_INT,
    RX_PKT_INT,
    DMAW_BUS_ERR_INT,
    DMAR_BUS_ERR_INT,
    CMB_INT,
    MAGIC_FRAME_INT,
    RX_PTP_EVENT_INT,
    TX_PTP_EVENT_INT
};

struct NucleiXECState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    NICState *nic;
    NetClientState *drop_peer;
    NICConf conf;
    qemu_irq irq;
    uint8_t mac[6];
    uint32_t revision;

    uint16_t jumbo_max_len;

    /* GEM registers backing store */
    uint32_t regs[XEC_MAXREG];
    /* Mask of register bits which are read only */
    uint32_t regs_ro[XEC_MAXREG];
    /* Mask of register bits which are clear on read */
    uint32_t regs_rc[XEC_MAXREG];
    /* Mask of register bits which are write 1 to clear */
    uint32_t regs_w1c[XEC_MAXREG];

    uint8_t phy_addr;
    uint16_t phy_regs[32];
    uint8_t phy_loop; /* Are we in phy loopback? */

    uint32_t rfd_base_addr;
    uint32_t rrd_base_addr;
    uint32_t tpd_base_addr;

    uint32_t rrd_prod_idx;
    uint32_t rrd_cons_idx;
    uint32_t rfd_prod_idx;
    uint32_t rfd_cons_idx;
    uint32_t tpd_prod_idx;
    uint32_t tpd_cons_idx;

    uint8_t tx_packet[MAX_FRAME_SIZE];
    uint8_t rx_packet[MAX_FRAME_SIZE];

};

#endif
