/*
 * Nuclei Ethernet Controller (XEC).
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

#include "qemu/osdep.h"
#include <zlib.h> /* For crc32 */

#include "hw/irq.h"
#include "hw/net/nuclei_xec.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/dma.h"
#include "net/checksum.h"
#include "net/eth.h"

/* Nuclei XEC MAC_BLOCK regs */
REG32(MAC_CTRL_REG, 0x0)
REG32(MAC_IPG_REG, 0x4)
REG32(MAC_HALF_CTRL_REG, 0x8)
REG32(MAC_MTU_REG, 0xC)
REG32(MAC_STAD_LO_REG, 0x10)
REG32(MAC_STAD_HI_REG, 0x14)
REG32(MAC_LPI_REG, 0x18)
REG32(RX_HASH_TABLE1_REG, 0x1C)
REG32(RX_HASH_TABLE2_REG, 0x20)
REG32(SRAM_CTRL_REG_1, 0x24)
REG32(SRAM_CTRL_REG_2, 0x28)
REG32(SRAM_CTRL_REG_3, 0x2C)
REG32(SRAM_CTRL_REG_4, 0x30)
REG32(SRAM_CTRL_REG_5, 0x34)
REG32(SRAM_CTRL_REG_6, 0x38)
REG32(DESCRIPTOR_CTRL_REG_2, 0x40)
REG32(DESCRIPTOR_CTRL_REG_3, 0x44)
REG32(DESCRIPTOR_CTRL_REG_4, 0x48)
REG32(DESCRIPTOR_CTRL_REG_5, 0x4C)
REG32(DESCRIPTOR_CTRL_REG_7, 0x54)
REG32(CMB_BASE_ADDR_LO, 0x5C)
REG32(DMA_BURST_CTRL_1, 0x60)
REG32(DMA_BURST_CTRL_2, 0x64)
REG32(FC_WATERMARK_REG, 0x68)
REG32(CMB_CTR_REG, 0x6C)
REG32(MAILBOX_REG_1, 0x70)
REG32(MAILBOX_REG_2, 0x74)
REG32(INT_STA_REG, 0x78)
REG32(INT_MSK_REG, 0x7C)
REG32(INTR_TIMER2_REG, 0x80)
REG32(MAC_RX_OK, 0x84)
REG32(MAC_RX_BCAST, 0x88)
REG32(MAC_RX_MCAST, 0x8C)
REG32(MAC_RX_PAUSE, 0x90)
REG32(MAC_RX_LLDP, 0x94)
REG32(MAC_RX_FCS_ERR, 0x98)
REG32(MAC_RX_LEN_ERR, 0x9C)
REG32(MAC_RX_BYTE_CNT, 0xA0)
REG32(MAC_RX_RUNT, 0xA4)
REG32(MAC_RX_FRAGMENT, 0xA8)
REG32(MAC_RX_SIZE_64, 0xAC)
REG32(MAC_RX_SIZE_65TO127, 0xB0)
REG32(MAC_RX_SIZE_128TO255, 0xB4)
REG32(MAC_RX_SIZE_256TO511, 0xB8)
REG32(MAC_RX_SIZE_512TO1023, 0xBC)
REG32(MAC_RX_SIZE_1024TO1518, 0xC0)
REG32(MAC_RX_SIZE_1519TOMAX, 0xC4)
REG32(MAC_RX_OVERSIZE, 0xC8)
REG32(MAC_RX_OVERFLOW, 0xCC)
REG32(MAC_RX_NIBBLE_ERR, 0xD0)
REG32(MAC_RX_BCAST_BYTE_CNT, 0xD4)
REG32(MAC_RX_MCAST_BYTE_CNT, 0xD8)
REG32(MAC_RX_ADDR_ERR, 0xDC)
REG32(MAC_TX_OK, 0xE0)
REG32(MAC_TX_BCAST, 0xE4)
REG32(MAC_TX_MCAST, 0xE8)
REG32(MAC_TX_PAUSE, 0xEC)
REG32(MAC_TX_DEFFER, 0xF0)
REG32(MAC_TX_BYTE_CNT, 0xF4)
REG32(MAC_TX_SIZE_64B, 0xF8)
REG32(MAC_TX_SIZE_65TO127, 0xFC)
REG32(MAC_TX_SIZE_128TO255, 0x100)
REG32(MAC_TX_SIZE_256TO511, 0x104)
REG32(MAC_TX_SIZE_512TO1023, 0x108)
REG32(MAC_TX_SIZE_1024TO1518, 0x10C)
REG32(MAC_TX_SIZE_1519TOMAX, 0x110)
REG32(MAC_TX_SINGLE_COL, 0x114)
REG32(MAC_TX_MULT_COL, 0x118)
REG32(MAC_TX_LATE_COL, 0x11C)
REG32(MAC_TX_ABORT_COL, 0x120)
REG32(MAC_TX_UNDERRUN, 0x124)
REG32(MAC_TX_LENGTH_ERR, 0x128)
REG32(MAC_TX_TRUNC, 0x12C)
REG32(MAC_TX_BCAST_BYTE_CNT, 0x130)
REG32(MAC_TX_MCAST_BYTE_CNT, 0x134)
REG32(MDIO_CTRL_REG_1, 0x138)
REG32(MDIO_CTRL_REG_2, 0x13C)
REG32(MDIO_STATUS_REG, 0x140)
REG32(DMA_BURST_REG, 0x144)
REG32(WATCHDOG_REG, 0x148)
REG32(LOW_POWER_CTRL, 0x14C)
REG32(XEC_STATUS, 0x150)
REG32(DELAY_SEL, 0x154)
REG32(SVLAN, 0x158)
REG32(DBG_TEST_REG, 0x15C)
REG32(IP_VERSION, 0x174)

/* PHY address we will emulate a device at */
#define BOARD_PHY_ADDRESS       2
/* Generic MII PHY registers. */
#define MII_BMCR            0x00
#define MII_BMSR            0x01
#define MII_PHYSID1         0x02
#define MII_PHYSID2         0x03
#define MII_ADVERTISE       0x04
#define MII_LPA             0x05
#define MII_EXPANSION       0x06
#define MII_CTRL1000        0x09
#define MII_STAT1000        0x0a
#define	MII_MMD_CTRL        0x0d
#define	MII_MMD_DATA        0x0e
#define MII_ESTATUS         0x0f
#define MII_DCOUNTER        0x12
#define MII_FCSCOUNTER      0x13
#define MII_NWAYTEST        0x14
#define MII_RERRCOUNTER     0x15
#define MII_SREVISION       0x16
#define MII_RESV1           0x17
#define MII_LBRERROR        0x18
#define MII_PHYADDR         0x19
#define MII_RESV2           0x1a
#define MII_TPISTATUS       0x1b
#define MII_NCONFIG         0x1c

/* Basic mode control register. */
#define BMCR_RESV           0x003f
#define BMCR_SPEED1000      0x0040
#define BMCR_CTST           0x0080
#define BMCR_FULLDPLX       0x0100
#define BMCR_ANRESTART      0x0200
#define BMCR_ISOLATE        0x0400
#define BMCR_PDOWN          0x0800
#define BMCR_ANENABLE       0x1000
#define BMCR_SPEED100       0x2000
#define BMCR_LOOPBACK       0x4000
#define BMCR_RESET          0x8000
#define BMCR_SPEED10        0x0000

/* Basic mode status register. */
#define BMSR_ERCAP          0x0001
#define BMSR_JCD            0x0002
#define BMSR_LSTATUS        0x0004
#define BMSR_ANEGCAPABLE    0x0008
#define BMSR_RFAULT         0x0010
#define BMSR_ANEGCOMPLETE   0x0020
#define BMSR_RESV           0x00c0
#define BMSR_ESTATEN        0x0100
#define BMSR_100HALF2       0x0200
#define BMSR_100FULL2       0x0400
#define BMSR_10HALF         0x0800
#define BMSR_10FULL         0x1000
#define BMSR_100HALF        0x2000
#define BMSR_100FULL        0x4000
#define BMSR_100BASE4       0x8000


static void nuclei_xec_update_irq(NucleiXECState *s)
{
    if (!!(s->regs[R_MAC_CTRL_REG] >> 31)) {
        /* when the MAGIC_FRAME_EN is 1, hardware will trigger 
         * the top_intr only for MAGIC_FRAME_INT
         */
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, !!s->regs[R_INT_STA_REG]);
    }
}

/*
 * Raise or lower interrupt based on current status.
 */
static void nuclei_xec_update_int_status(NucleiXECState *s, uint32_t flag)
{
    s->regs[R_INT_STA_REG] |= (1 << flag) & (~(s->regs[R_INT_MSK_REG]));
}

/* The broadcast MAC address: 0xFFFFFFFFFFFF */
static const uint8_t broadcast_addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };


/*
 * Get the MAC Address bit from the specified position
 */
static uint32_t get_bit(const uint8_t *mac, uint32_t bit)
{
    unsigned byte;

    byte = mac[bit / 8];
    byte >>= (bit & 0x7);
    byte &= 1;

    return byte;
}

/*
 * Calculate a GEM MAC Address hash index
 */
static uint32_t nuclei_xec_calc_mac_hash(const uint8_t *mac)
{
    int index_bit, mac_bit;
    unsigned hash_index;

    hash_index = 0;
    mac_bit = 5;
    for (index_bit = 5; index_bit >= 0; index_bit--) {
        // nuclei crc poly???
        hash_index |= (get_bit(mac,  mac_bit) ^
                               get_bit(mac, mac_bit + 6) ^
                               get_bit(mac, mac_bit + 12) ^
                               get_bit(mac, mac_bit + 18) ^
                               get_bit(mac, mac_bit + 24) ^
                               get_bit(mac, mac_bit + 30) ^
                               get_bit(mac, mac_bit + 36) ^
                               get_bit(mac, mac_bit + 42)) << index_bit;
        mac_bit--;
    }

    return hash_index;
}

/**
 * nuclei_xec_mac_address_filter:
 * XEC dose destination address filter for the incoming packet.
 * There are three types of destination addresses:
 *  Unicast address: Defined by the register STAD;
 *  Broadcast address: The destination address is FF-FF-FF-FF-FF-FF;
 *  Multicast address: The destination address’s first byte’s LSB bit is 1
 */
static int nuclei_xec_mac_address_filter(NucleiXECState *s, const uint8_t *packet)
{
    // Unicast
    s->mac[0] = (s->regs[R_MAC_STAD_HI_REG] >> 8) & 0xff;
    s->mac[1] = s->regs[R_MAC_STAD_HI_REG] & 0xff;
    s->mac[2] = (s->regs[R_MAC_STAD_LO_REG] >> 24) & 0xff;
    s->mac[3] = (s->regs[R_MAC_STAD_LO_REG] >> 16) & 0xff;
    s->mac[4] = (s->regs[R_MAC_STAD_LO_REG] >> 8) & 0xff;
    s->mac[5] = s->regs[R_MAC_STAD_LO_REG] & 0xff;

    if (!memcmp(packet, s->mac, 6)) {
        return XEC_RX_PACKET_ADDR_VALID;
    }

    // Broadcast
    if (!memcmp(packet, broadcast_addr, 6)) {
        if ((s->regs[R_MAC_CTRL_REG] >> 26) & 0X1) {    // BROAD_EN enable
            return XEC_RX_PACKET_ADDR_VALID;
        }
        return XEC_RX_PACKET_ADDR_INVALID;
    }

    // Multicast
    if (packet[0] & 0x1) {                                      // is multicast address
        if ((s->regs[R_MAC_CTRL_REG] >> 24) & 0X1) {            // MULTI_ALL enable
            return XEC_RX_PACKET_ADDR_VALID;
        } else if (!((s->regs[R_MAC_CTRL_REG] >> 24) & 0X1) &&  // MULTI_ALL disable
                    ((s->regs[R_MAC_CTRL_REG] >> 25) & 0X1)) {  // RX_HASH_EN enable
            // to do : check hash
            uint64_t buckets;
            uint32_t hash_index;

            hash_index = nuclei_xec_calc_mac_hash(packet);
            buckets = ((uint64_t)s->regs[R_RX_HASH_TABLE2_REG] << 32) | s->regs[R_RX_HASH_TABLE1_REG];
            if ((buckets >> hash_index) & 1) {
                return XEC_RX_PACKET_ADDR_VALID;
            }
        }
    }

    return XEC_RX_PACKET_ADDR_INVALID;
}

static int nuclei_xec_receiver_packet_drop(NetClientState *nc, const uint8_t *packet, size_t size)
{
    NucleiXECState*s = qemu_get_nic_opaque(nc);
    bool is_pause_frame = false;

    /* The MAC destination address don’t match */
    if (nuclei_xec_mac_address_filter(s, packet) == XEC_RX_PACKET_ADDR_INVALID) {
        return XEC_RX_PACKET_DROP;
    }

    /* The received packet is a PAUSE frame, to do.*/
    if (is_pause_frame) {
        return XEC_RX_PACKET_DROP;
    }

    /* The received packet’s byte is less than 60 bytes or larger than MTU */
    if (size < 60 || size > (s->regs[R_MAC_MTU_REG] & 0xffff)) {
        return XEC_RX_PACKET_DROP;
    }

    return XEC_RX_PACKET_ACCEPT;
}

static bool nuclei_xec_can_receive(NetClientState *nc)
{
    NucleiXECState *s = qemu_get_nic_opaque(nc);

    //MAC rx disable 
    if (!(s->regs[R_MAC_CTRL_REG] & 0x2)) {
        return false;
    }
    return true;
}

/*
 * gem_receive_updatestats:
 * Increment receive statistics.
 */
static void nuclei_xec_receive_updatestats(NucleiXECState *s, unsigned bytes)
{
    s->regs[R_MAC_RX_OK]++;

    if (bytes <= 64) {
        s->regs[R_MAC_RX_SIZE_64]++;
    } else if (bytes <= 127) {
        s->regs[R_MAC_RX_SIZE_65TO127]++;
    } else if (bytes <= 255) {
        s->regs[R_MAC_RX_SIZE_128TO255]++;
    } else if (bytes <= 511) {
        s->regs[R_MAC_RX_SIZE_256TO511]++;
    } else if (bytes <= 1023) {
        s->regs[R_MAC_RX_SIZE_512TO1023]++;
    } else if (bytes <= 1518) {
        s->regs[R_MAC_RX_SIZE_1024TO1518]++;
    } else if (bytes <= s->regs[R_MAC_MTU_REG]) {
        s->regs[R_MAC_RX_SIZE_1519TOMAX]++;
    } else {
        s->regs[R_MAC_RX_OVERSIZE]++;
    }
}

static ssize_t nuclei_xec_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    NucleiXECState *s = qemu_get_nic_opaque(nc);
    int recv_pkt_drop;
    uint32_t rfd_addr, rrd_addr;
    uint32_t rfd_desc[2], rrd_desc[2];
    uint32_t crc_val;

    /* packet drop filter */
    recv_pkt_drop = nuclei_xec_receiver_packet_drop(nc, buf, size);
    if (recv_pkt_drop == XEC_RX_PACKET_DROP) {
        return -1;
    }

    if (!nuclei_xec_can_receive(nc)) {
        return -1;
    }

    s->rfd_prod_idx = s->regs[R_MAILBOX_REG_1] & 0xfff;
    s->rfd_cons_idx = (s->regs[R_MAILBOX_REG_1] >> 15) & 0xfff;
    s->rfd_base_addr = s->regs[R_DESCRIPTOR_CTRL_REG_2];
    s->rrd_base_addr = s->regs[R_DESCRIPTOR_CTRL_REG_3];

    // Check if RFD is available
    if (s->rfd_prod_idx == s->rfd_cons_idx) {
        qemu_log_mask(LOG_GUEST_ERROR, "No RFD available\n");  // change to RXF_OF_INT?
        return -1;
    }

    // Get current RFD address
    rfd_addr = s->rfd_base_addr + s->rfd_cons_idx * sizeof(uint32_t) * 2;
    rrd_addr = s->rrd_base_addr + s->rfd_cons_idx * sizeof(uint32_t) * 2;

    // Get RFD descriptor from RFD ring
    address_space_read(&s->dma_as, rfd_addr, MEMTXATTRS_UNSPECIFIED,
                        rfd_desc, sizeof(uint32_t) * 2);

    uint64_t recv_buf_addr = rfd_desc[0] | (uint64_t)rfd_desc[1] << 32;

    // CRC support
        // Ensure the validity of the uploaded frame length.
    if (size < 60) {
        size = 60;
    }
    if (size > MAX_FRAME_SIZE - sizeof(crc_val)) {
        size = MAX_FRAME_SIZE - sizeof(crc_val);
    }
    /* Calculate the FCS field */
    memcpy(s->rx_packet, buf, size);
    memset(s->rx_packet + size, 0, MAX_FRAME_SIZE - size);
    crc_val = cpu_to_le32(crc32(0, s->rx_packet, MAX(size, 60)));
    memcpy(s->rx_packet + size, &crc_val, sizeof(crc_val));
    size += 4;

    // Copy data to receive buffer
    address_space_write(&s->dma_as, (hwaddr)recv_buf_addr, MEMTXATTRS_UNSPECIFIED,
                        buf, size);

    rrd_desc[0] |= 1 << 12;
    rrd_desc[1] |=  1 << 31;
    rrd_desc[1] = (rrd_desc[1] & ~(0x3FFF << 16)) | ((size & 0x3FFF) << 16);

    // Set RRD descriptor to RRD ring
    address_space_write(&s->dma_as, rrd_addr, MEMTXATTRS_UNSPECIFIED,
                        rrd_desc, sizeof(uint32_t) * 2);


    // Update RFD consumer index
    s->rfd_cons_idx++;
    if (s->rfd_cons_idx >= (s->regs[R_DESCRIPTOR_CTRL_REG_4] & 0xFFF))
    {
        s->rfd_cons_idx = 0;
    }
    s->regs[R_MAILBOX_REG_1] = (s->rfd_cons_idx << 15) | (s->regs[R_MAILBOX_REG_1] & 0x7fff);

    nuclei_xec_receive_updatestats(s, size);

    // Generate RX_PKT_INT interrupt
    nuclei_xec_update_int_status(s, RX_PKT_INT);
    nuclei_xec_update_irq(s);

    return size;
}

/**
 * Increment transmit statistics.
 */
static void nuclei_xec_transmit_updatestats(NucleiXECState *s, unsigned bytes)
{
    s->regs[R_MAC_TX_OK]++;

    if (bytes <= 64) {
        s->regs[R_MAC_TX_SIZE_64B]++;
    } else if (bytes <= 127) {
        s->regs[R_MAC_TX_SIZE_65TO127]++;
    } else if (bytes <= 255) {
        s->regs[R_MAC_TX_SIZE_128TO255]++;
    } else if (bytes <= 511) {
        s->regs[R_MAC_TX_SIZE_256TO511]++;
    } else if (bytes <= 1023) {
        s->regs[R_MAC_TX_SIZE_512TO1023]++;
    } else if (bytes <= 1518) {
        s->regs[R_MAC_TX_SIZE_1024TO1518]++;
    } else {
        s->regs[R_MAC_TX_SIZE_1519TOMAX]++;
    }
}

static void nuclei_xec_transmit(NucleiXECState *s)
{
    uint32_t tpd_addr;
    uint32_t tx_buf_len;
    uint32_t tpd_desc[4];
    uint32_t tpd_ring_size;
    uint32_t tx_buffer_size;

    s->tpd_prod_idx = s->regs[R_MAILBOX_REG_2] & 0xffff; // It has auto-incremented by 1 in the driver.
    s->tpd_base_addr =  s->regs[R_DESCRIPTOR_CTRL_REG_7];
    tpd_ring_size = s->regs[R_DESCRIPTOR_CTRL_REG_5] >> 16;
    tx_buffer_size = (s->regs[R_DESCRIPTOR_CTRL_REG_4] >> 16) & 0xfff;

    s->tpd_cons_idx++;
    if (s->tpd_cons_idx >= (s->regs[R_DESCRIPTOR_CTRL_REG_5] >> 16))
    {
        s->tpd_cons_idx = 0;
    }
    s->regs[R_MAILBOX_REG_2] = (s->tpd_cons_idx << 16) | (s->regs[R_MAILBOX_REG_2] & 0xffff);

    // Check if TPD is available
    if (s->tpd_prod_idx == s->tpd_cons_idx) {
        printf("No TPD available\n");
        return;
    }

    if (s->tpd_prod_idx == 0)
        s->tpd_prod_idx = tpd_ring_size;

    // Get current TPD address, Assuming TPD is 16 bytes
    tpd_addr = s->tpd_base_addr + (s->tpd_prod_idx - 1) * sizeof(uint32_t) * 4;

    // Get TPD descriptor
    address_space_read(&s->dma_as, tpd_addr, MEMTXATTRS_UNSPECIFIED,
                        tpd_desc, sizeof(uint32_t) * 4);

    tx_buf_len = tpd_desc[0] & 0xffff;

    address_space_read(&s->dma_as, ((hwaddr)tpd_desc[2] | (uint64_t)tpd_desc[3] << 32), MEMTXATTRS_UNSPECIFIED,
                    s->tx_packet, tx_buf_len);

    qemu_send_packet(qemu_get_queue(s->nic), s->tx_packet, tx_buf_len);

    // Update statistics
    nuclei_xec_transmit_updatestats(s, tx_buffer_size);
    nuclei_xec_update_int_status(s, TX_PKT_INT);
    nuclei_xec_update_irq(s);

}

static void nuclei_xec_phy_update_link(NucleiXECState *s)
{
    /* Autonegotiation status mirrors link status.  */
    if (qemu_get_queue(s->nic)->link_down) {
        s->phy_regs[MII_BMSR] &= ~(BMSR_ANEGCOMPLETE | BMSR_LSTATUS);
    } else {
        s->phy_regs[MII_BMSR] |= (BMSR_ANEGCOMPLETE | BMSR_LSTATUS);
    }
}

static void nuclei_xec_phy_reset(NucleiXECState *s)
{
    memset(&s->phy_regs[0], 0, sizeof(s->phy_regs));
    nuclei_xec_phy_update_link(s);
}

static uint16_t nuclei_xec_phy_read(NucleiXECState*s, uint8_t reg_num)
{
    return s->phy_regs[reg_num];
}

static void nuclei_xec_phy_write(NucleiXECState *s, uint8_t reg_num, uint16_t val)
{
    switch (reg_num) {
    case MII_BMCR:
        if (val & BMCR_RESET) {
            /* Phy reset */
            nuclei_xec_phy_reset(s);
            val &= ~(BMCR_RESET | BMCR_LOOPBACK);
            s->phy_loop = 0;
        }
        if (val & BMCR_ANENABLE) {
            /* Complete autonegotiation immediately */
            val &= ~(BMCR_ANENABLE | BMCR_ANRESTART);
            s->phy_regs[MII_BMSR] |= BMSR_ANEGCOMPLETE;
        }
        if (val & BMCR_LOOPBACK) {
            s->phy_loop = 1;
        } else {
            s->phy_loop = 0;
        }
        break;
    }
    s->phy_regs[reg_num] = val;
}

static void nuclei_xec_handle_phy_access(NucleiXECState *s)
{
    uint16_t mdio_data;
    uint32_t phy_addr, phy_reg_num;
    uint8_t mdio_op;

    mdio_data = s->regs[R_MDIO_CTRL_REG_1] & 0xffff;
    phy_reg_num = (s->regs[R_MDIO_CTRL_REG_1] >> 16) & 0x1f;
    phy_addr = (s->regs[R_MDIO_CTRL_REG_2] >> 26) & 0x1f;
    mdio_op = (s->regs[R_MDIO_CTRL_REG_1] >> 21) & 0x1;

    if (phy_addr != s->phy_addr) {
        /* no phy at this address */
        return;
    }

    switch (mdio_op) {
    case 0:
        nuclei_xec_phy_write(s, phy_reg_num, mdio_data);
        break;
    case 1:
        s->regs[R_MDIO_STATUS_REG] = (s->regs[R_MDIO_STATUS_REG] & 0xffff0000)
                                    | nuclei_xec_phy_read(s, phy_reg_num);
        break;

    default:
        break; /* only clause 22 operations are supported */
    }
}

/**
 * Set masks to identify which register bits have magical clear properties
 */
static void nuclei_xec_init_register_masks(NucleiXECState *s)
{
    /* Mask of register bits which are read only */
    memset(&s->regs_ro[0], 0, sizeof(s->regs_ro));
    s->regs_ro[R_MAC_LPI_REG] = 0x3000000;
    s->regs_ro[R_DESCRIPTOR_CTRL_REG_2] = 0x7;
    s->regs_ro[R_DESCRIPTOR_CTRL_REG_3] = 0x7;
    s->regs_ro[R_DESCRIPTOR_CTRL_REG_4] = 0x70000;
    s->regs_ro[R_DESCRIPTOR_CTRL_REG_7] = 0xf;
    s->regs_ro[R_CMB_BASE_ADDR_LO] = 0x1;
    s->regs_ro[R_MAILBOX_REG_1] = 0x7ff8000;
    s->regs_ro[R_MAILBOX_REG_2] = 0xffff0000;
    s->regs_ro[R_MDIO_STATUS_REG] = 0x1ffff;
    s->regs_ro[R_XEC_STATUS] = 0x1;
    s->regs_ro[R_IP_VERSION] = 0xffffffff;

    /* Mask of register bits which are clear on read */
    memset(&s->regs_rc[0], 0, sizeof(s->regs_rc));
    s->regs_rc[R_MAC_RX_OK] = 0xffffff;
    s->regs_rc[R_MAC_RX_BCAST] = 0xffffff;
    s->regs_rc[R_MAC_RX_MCAST] = 0xffffff;
    s->regs_rc[R_MAC_RX_PAUSE] = 0xffffff;
    s->regs_rc[R_MAC_RX_LLDP] = 0xffffff;
    s->regs_rc[R_MAC_RX_FCS_ERR] = 0xffffff;
    s->regs_rc[R_MAC_RX_LEN_ERR] = 0xffffff;
    s->regs_rc[R_MAC_RX_BYTE_CNT] = 0xffffffff;
    s->regs_rc[R_MAC_RX_RUNT] = 0xffffff;
    s->regs_rc[R_MAC_RX_FRAGMENT] = 0xffffff;
    s->regs_rc[R_MAC_RX_SIZE_64] = 0xffffff;
    s->regs_rc[R_MAC_RX_SIZE_65TO127] = 0xffffff;
    s->regs_rc[R_MAC_RX_SIZE_128TO255] = 0xffffff;
    s->regs_rc[R_MAC_RX_SIZE_256TO511] = 0xffffff;
    s->regs_rc[R_MAC_RX_SIZE_512TO1023] = 0xffffff;
    s->regs_rc[R_MAC_RX_SIZE_1024TO1518] = 0xffffff;
    s->regs_rc[R_MAC_RX_SIZE_1519TOMAX] = 0xffffff;
    s->regs_rc[R_MAC_RX_OVERSIZE] = 0xffffff;
    s->regs_rc[R_MAC_RX_OVERFLOW] = 0xffff;
    s->regs_rc[R_MAC_RX_NIBBLE_ERR] = 0xffffff;
    s->regs_rc[R_MAC_RX_BCAST_BYTE_CNT] = 0xffffffff;
    s->regs_rc[R_MAC_RX_MCAST_BYTE_CNT] = 0xffffffff;
    s->regs_rc[R_MAC_RX_ADDR_ERR] = 0xffffff;
    s->regs_rc[R_MAC_TX_OK] = 0xffffff;
    s->regs_rc[R_MAC_TX_BCAST] = 0xffffff;
    s->regs_rc[R_MAC_TX_MCAST] = 0xffffff;
    s->regs_rc[R_MAC_TX_PAUSE] = 0xffffff;
    s->regs_rc[R_MAC_TX_DEFFER] = 0xffffff;
    s->regs_rc[R_MAC_TX_BYTE_CNT] = 0xffffffff;
    s->regs_rc[R_MAC_TX_SIZE_64B] = 0xffffff;
    s->regs_rc[R_MAC_TX_SIZE_65TO127] = 0xffffff;
    s->regs_rc[R_MAC_TX_SIZE_128TO255] = 0xffffff;
    s->regs_rc[R_MAC_TX_SIZE_256TO511] = 0xffffff;
    s->regs_rc[R_MAC_TX_SIZE_512TO1023] = 0xffffff;
    s->regs_rc[R_MAC_TX_SIZE_1024TO1518] = 0xffffff;
    s->regs_rc[R_MAC_TX_SIZE_1519TOMAX] = 0xffffff;
    s->regs_rc[R_MAC_TX_SINGLE_COL] = 0xffffff;
    s->regs_rc[R_MAC_TX_MULT_COL] = 0xffffff;
    s->regs_rc[R_MAC_TX_LATE_COL] = 0xffffff;
    s->regs_rc[R_MAC_TX_ABORT_COL] = 0xffffff;
    s->regs_rc[R_MAC_TX_UNDERRUN] = 0xffff;
    s->regs_rc[R_MAC_TX_LENGTH_ERR] = 0xffff;
    s->regs_rc[R_MAC_TX_TRUNC] = 0xffff;
    s->regs_rc[R_MAC_TX_BCAST_BYTE_CNT] = 0xffffffff;
    s->regs_rc[R_MAC_TX_MCAST_BYTE_CNT] = 0xffffffff;

    /* Mask of register bits which are write 1 to clear */
    memset(&s->regs_w1c[0], 0, sizeof(s->regs_w1c));
    s->regs_w1c[R_INT_STA_REG] = 0x1fff;

}

static uint64_t nuclei_xec_read(void *opaque, hwaddr offset, unsigned size) {
    NucleiXECState *s = opaque;
    uint32_t ret_val;

    offset >>= 2;
    ret_val = s->regs[offset];
    /* Squash read to clear bits */
    s->regs[offset] &= ~(s->regs_rc[offset]);

    return ret_val;
}

static void nuclei_xec_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    NucleiXECState *s = opaque;
    uint32_t readonly;

    offset >>= 2;

    /* Squash bits which are read only in write value */
    val &= ~(s->regs_ro[offset]);
    /* Preserve (only) bits which are read only and wtc in register */
    readonly = s->regs[offset] & (s->regs_ro[offset] | s->regs_w1c[offset]);

    /* Copy register write to backing store */
    s->regs[offset] = (val & ~s->regs_w1c[offset]) | readonly;

    /* do w1c */
    s->regs[offset] &= ~(s->regs_w1c[offset] & val);

    switch (offset) {
    case R_MAC_CTRL_REG:
        if (nuclei_xec_can_receive(qemu_get_queue(s->nic))) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    case R_SRAM_CTRL_REG_6:
        // set 1 to reset the SRAM pointer and then clear to 0
        break;
    case R_MDIO_CTRL_REG_1:
        /* Write 1 to initiate the MDIO operation. And this
         * bit is self cleared after one cycle.
         */
        if ((s->regs[R_MDIO_CTRL_REG_1] >> 23) & 0x1) {
            nuclei_xec_handle_phy_access(s);
        }
        s->regs[R_MDIO_CTRL_REG_1] &= ~(1 << 23);
        break;
    case R_DESCRIPTOR_CTRL_REG_5:
        s->tpd_cons_idx = s->regs[R_DESCRIPTOR_CTRL_REG_5] >> 16;
        break;
    case R_MAILBOX_REG_2:
        if ((s->regs[R_MAC_CTRL_REG] & 0x1) && ((s->regs[R_MAC_CTRL_REG] >> 2) & 0x1)){
            nuclei_xec_transmit(s);
        }
        break;
    }
}

static const MemoryRegionOps nuclei_xec_ops = {
    .read = nuclei_xec_read,
    .write = nuclei_xec_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void nuclei_xec_set_link(NetClientState *nc)
{
    NucleiXECState *s = qemu_get_nic_opaque(nc);

    nuclei_xec_phy_update_link(s);
    nuclei_xec_update_irq(s);
}

static NetClientInfo net_nuclei_xec_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = nuclei_xec_can_receive,
    .receive = nuclei_xec_receive,
    .link_status_changed = nuclei_xec_set_link,
};

static void nuclei_xec_realize(DeviceState *dev, Error **errp)
{
    NucleiXECState *s = NUCLEI_XEC(dev);

    address_space_init(&s->dma_as,
                       s->dma_mr ? s->dma_mr : get_system_memory(), "dma");

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_nuclei_xec_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);

}

static void nuclei_xec_reset(DeviceState *dev)
{
    NucleiXECState *s = NUCLEI_XEC(dev);

    /* Set post reset register values */
    memset(&s->regs[0], 0, sizeof(s->regs));
    s->regs[R_MAC_CTRL_REG] = 0x6587a7ec;
    s->regs[R_MAC_IPG_REG] = 0x500860;
    s->regs[R_MAC_HALF_CTRL_REG] = 0x1a;
    s->regs[R_MAC_MTU_REG] = 0x600;
    s->regs[R_MAC_LPI_REG] = 0x150015;
    s->regs[R_SRAM_CTRL_REG_1] = 0x2027f260;
    s->regs[R_SRAM_CTRL_REG_2] = 0x203ff3e0;
    s->regs[R_SRAM_CTRL_REG_3] = 0x25f000;
    s->regs[R_SRAM_CTRL_REG_4] = 0x3df280;
    s->regs[R_SRAM_CTRL_REG_5] = 0x160260;
    s->regs[R_DESCRIPTOR_CTRL_REG_4] = 0x500800;
    s->regs[R_DESCRIPTOR_CTRL_REG_5] = 0x8000000;
    s->regs[R_DMA_BURST_CTRL_1] = 0x5;
    s->regs[R_DMA_BURST_CTRL_2] = 0x8;
    s->regs[R_FC_WATERMARK_REG] = 0x1000180;
    s->regs[R_CMB_CTR_REG] = 0x7fff0100;
    s->regs[R_INTR_TIMER2_REG] = 0xffff0008;
    s->regs[R_DMA_BURST_REG] = 0xf000f;
    s->regs[R_WATCHDOG_REG] = 0x1f40;
    s->regs[R_LOW_POWER_CTRL] = 0x1;
    s->regs[R_SVLAN] = 0x88a8;
    s->regs[R_IP_VERSION] = 0x00010000;

    nuclei_xec_phy_reset(s);
    nuclei_xec_update_irq(s);
}

static void nuclei_xec_init(Object *obj)
{
    NucleiXECState *s = NUCLEI_XEC(obj);
    DeviceState *dev = DEVICE(obj);

    nuclei_xec_init_register_masks(s);
    memory_region_init_io(&s->mmio, OBJECT(s), &nuclei_xec_ops, s,
                          TYPE_NUCLEI_XEC, sizeof(s->regs));

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    qemu_configure_nic_device(dev, true, TYPE_NUCLEI_XEC);
}

static const VMStateDescription vmstate_nuclei_xec = {
    .name = TYPE_NUCLEI_XEC,
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, NucleiXECState, XEC_MAXREG),
        VMSTATE_UINT16_ARRAY(phy_regs, NucleiXECState, 32),
        VMSTATE_UINT8(phy_loop, NucleiXECState),
        VMSTATE_END_OF_LIST(),
    }
};

static Property nuclei_xec_properties[] = {
    DEFINE_NIC_PROPERTIES(NucleiXECState, conf),
    DEFINE_PROP_UINT32("revision", NucleiXECState, revision,
                       0x10000),
    DEFINE_PROP_UINT8("phy-addr", NucleiXECState, phy_addr, BOARD_PHY_ADDRESS),
    DEFINE_PROP_UINT16("jumbo-max-len", NucleiXECState,
                       jumbo_max_len, 4096),
    DEFINE_PROP_LINK("dma", NucleiXECState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_xec_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nuclei_xec_realize;
    dc->desc = "Nuclei Ethernet Controller (XEC)";
    device_class_set_props(dc, nuclei_xec_properties);
    dc->vmsd = &vmstate_nuclei_xec;
    dc->reset = nuclei_xec_reset;
}

static const TypeInfo nuclei_xec_info = {
    .name  = TYPE_NUCLEI_XEC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(NucleiXECState),
    .instance_init = nuclei_xec_init,
    .class_init = nuclei_xec_class_init,
};

static void nuclei_xec_register_types(void)
{
    type_register_static(&nuclei_xec_info);
}

type_init(nuclei_xec_register_types)

