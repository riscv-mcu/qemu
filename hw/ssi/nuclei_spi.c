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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/nuclei_spi.h"

static void nuclei_spi_update_status(NucleiSPIState *s)
{
    uint32_t busy = s->regs[NUCLEI_SPI_STATUS] & STATUS_BUSY;
    uint32_t status = s->regs[NUCLEI_SPI_STATUS] &
                      ~(STATUS_BUSY | STATUS_TXFULL | STATUS_RXEMPTY |
                        STATUS_TXEMPTY | STATUS_RXFULL);

    status |= busy;

    if (fifo8_is_full(&s->tx_fifo)) {
        status |= STATUS_TXFULL;
    }
    if (fifo8_is_empty(&s->tx_fifo)) {
        status |= STATUS_TXEMPTY;
    }
    if (fifo8_is_empty(&s->rx_fifo)) {
        status |= STATUS_RXEMPTY;
    }
    if (fifo8_is_full(&s->rx_fifo)) {
        status |= STATUS_RXFULL;
    }

    s->regs[NUCLEI_SPI_STATUS] = status;
    s->regs[NUCLEI_SPI_FIFO_NUM] = fifo8_num_used(&s->tx_fifo) |
                                   (fifo8_num_used(&s->rx_fifo) << 16);
}

static uint32_t nuclei_spi_status_irq_bits(NucleiSPIState *s)
{
    uint32_t ie = s->regs[NUCLEI_SPI_IE];
    uint32_t status = s->regs[NUCLEI_SPI_STATUS];
    uint32_t pending = 0;

    if ((ie & IE_TXUDR) && (status & STATUS_UDR)) {
        pending |= STATUS_UDR;
    }
    if ((ie & IE_RXOVR) && (status & STATUS_OVR)) {
        pending |= STATUS_OVR;
    }
    if ((ie & IE_RXUDR) && (status & STATUS_RXUDR)) {
        pending |= STATUS_RXUDR;
    }
    if ((ie & IE_TXOVR) && (status & STATUS_TXOVR)) {
        pending |= STATUS_TXOVR;
    }
    if ((ie & IE_DONE) && (status & STATUS_DONE)) {
        pending |= STATUS_DONE;
    }
    if ((ie & IE_TXDONE) && (status & STATUS_TXDONE)) {
        pending |= STATUS_TXDONE;
    }
    if ((ie & IE_RXDONE) && (status & STATUS_RXDONE)) {
        pending |= STATUS_RXDONE;
    }
    if ((ie & IE_CFGERR) && (status & STATUS_CFGERR)) {
        pending |= STATUS_CFGERR;
    }

    return pending;
}

static int nuclei_spi_selected_cs(NucleiSPIState *s)
{
    uint32_t csid = s->regs[NUCLEI_SPI_CSID];

    if (csid == 0 || csid > s->num_cs) {
        return -1;
    }

    return csid - 1;
}

static bool nuclei_spi_rx_enabled(NucleiSPIState *s)
{
    if (s->version > NUCLEI_SPI_VERSION_1_1_0) {
        return s->regs[NUCLEI_SPI_CR] & CR_RXFIFO_EN;
    }

    return true;
}

static void nuclei_spi_txfifo_reset(NucleiSPIState *s)
{
    fifo8_reset(&s->tx_fifo);
}

static void nuclei_spi_rxfifo_reset(NucleiSPIState *s)
{
    fifo8_reset(&s->rx_fifo);
}

static void nuclei_spi_update_cs(NucleiSPIState *s)
{
    int selected = nuclei_spi_selected_cs(s);
    bool hw_enabled = (s->regs[NUCLEI_SPI_CR] & CR_CSOE) &&
                      s->regs[NUCLEI_SPI_CSMODE] != CSMODE_OFF;
    int i;

    for (i = 0; i < s->num_cs; i++) {
        int idle = !!(s->regs[NUCLEI_SPI_CSDEF] & (1 << i));
        int level = idle;

        if (hw_enabled && s->cs_active && i == selected) {
            level = !idle;
        }
        qemu_set_irq(s->cs_lines[i], level);
    }
}

static void nuclei_spi_set_cs_active(NucleiSPIState *s, bool active)
{
    s->cs_active = active && nuclei_spi_selected_cs(s) >= 0 &&
                   (s->regs[NUCLEI_SPI_CR] & CR_CSOE) &&
                   s->regs[NUCLEI_SPI_CSMODE] != CSMODE_OFF;
    nuclei_spi_update_cs(s);
}

static void nuclei_spi_update_irq(NucleiSPIState *s)
{
    int level;

    nuclei_spi_update_status(s);

    if (fifo8_num_used(&s->tx_fifo) <= s->regs[NUCLEI_SPI_TX_MARK]) {
        s->regs[NUCLEI_SPI_IP] |= IP_TXWM;
    } else {
        s->regs[NUCLEI_SPI_IP] &= ~IP_TXWM;
    }

    if (fifo8_num_used(&s->rx_fifo) > s->regs[NUCLEI_SPI_RX_MARK]) {
        s->regs[NUCLEI_SPI_IP] |= IP_RXWM;
    } else {
        s->regs[NUCLEI_SPI_IP] &= ~IP_RXWM;
    }

    level = ((s->regs[NUCLEI_SPI_IP] & s->regs[NUCLEI_SPI_IE] & IP_MASK) ||
             nuclei_spi_status_irq_bits(s)) ? 1 : 0;
    qemu_set_irq(s->irq, level);
}

static void nuclei_spi_reset(DeviceState *d)
{
    NucleiSPIState *s = NUCLEI_SPI(d);

    memset(s->regs, 0, sizeof(s->regs));

    /* The v1.2.8 spec defines reset values for up to four CS pins. */
    s->regs[NUCLEI_SPI_CSDEF] = 0xf;

    /* Populate register with their default value */
    s->regs[NUCLEI_SPI_SCKDIV] = 0x04;
    s->regs[NUCLEI_SPI_DDR_SCKSAMPLE] = 0x0;
    s->regs[NUCLEI_SPI_FORCE] = FORCE_EN;
    s->regs[NUCLEI_SPI_CSID] = 0x01;
    s->regs[NUCLEI_SPI_VERSION] = s->version;
    s->regs[NUCLEI_SPI_BOUNDARY_CFG] = 0x3ff;
    s->regs[NUCLEI_SPI_DELAY0] = 0x10001;
    s->regs[NUCLEI_SPI_DELAY1] = 0x03;
    s->regs[NUCLEI_SPI_TSIZE] = 0xffffffff;
    s->regs[NUCLEI_SPI_RSIZE] = 0xffffffff;
    s->regs[NUCLEI_SPI_FMT] = 0x80000;
    s->regs[NUCLEI_SPI_FCTRL] = 0x01;
    s->regs[NUCLEI_SPI_FFMT] = 0x30007;
    s->regs[NUCLEI_SPI_FFMT1] = 0x02;
    s->regs[NUCLEI_SPI_STATUS] = 0;
    s->regs[NUCLEI_SPI_CR] = CR_MSTR | CR_CSOE | CR_RXFIFO_EN;
    s->cs_active = false;

    nuclei_spi_txfifo_reset(s);
    nuclei_spi_rxfifo_reset(s);

    nuclei_spi_update_cs(s);
    nuclei_spi_update_irq(s);
}

static void nuclei_spi_flush_txfifo(NucleiSPIState *s)
{
    uint8_t tx;
    uint8_t rx;
    bool transferred = false;
    bool received = false;

    s->regs[NUCLEI_SPI_STATUS] |= STATUS_BUSY;
    nuclei_spi_set_cs_active(s, true);

    while (!fifo8_is_empty(&s->tx_fifo)) {
        tx = fifo8_pop(&s->tx_fifo);
        rx = ssi_transfer(s->spi, tx);
        transferred = true;

        if (!(s->regs[NUCLEI_SPI_FMT] & FMT_DIR) &&
            nuclei_spi_rx_enabled(s)) {
            if (!fifo8_is_full(&s->rx_fifo)) {
                fifo8_push(&s->rx_fifo, rx);
                received = true;
            } else {
                s->regs[NUCLEI_SPI_STATUS] |= STATUS_OVR;
            }
        }
    }

    if (transferred) {
        s->regs[NUCLEI_SPI_STATUS] |= STATUS_TXDONE;
        if (fifo8_is_empty(&s->tx_fifo)) {
            s->regs[NUCLEI_SPI_STATUS] |= STATUS_DONE;
        }
        if (received) {
            s->regs[NUCLEI_SPI_STATUS] |= STATUS_RXDONE;
        }
    }

    s->regs[NUCLEI_SPI_STATUS] &= ~STATUS_BUSY;
    if (s->regs[NUCLEI_SPI_CSMODE] == CSMODE_AUTO) {
        nuclei_spi_set_cs_active(s, false);
    } else {
        nuclei_spi_update_cs(s);
    }
}

static bool nuclei_spi_is_bad_reg(hwaddr addr, bool allow_reserved)
{
    bool bad;

    switch (addr) {
    /* reserved offsets */
    case 0x3C:
    case 0x44:
    case 0x58:
    case 0x5C:
    case 0x68:
    case 0x6C:
    case 0x88 ... 0xBC:
        bad = allow_reserved ? false : true;
        break;
    default:
        bad = false;
    }

    if (addr >= (NUCLEI_SPI_REG_NUM << 2)) {
        bad = true;
    }

    return bad;
}

static uint64_t nuclei_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    NucleiSPIState *s = opaque;
    uint32_t r;

    if (nuclei_spi_is_bad_reg(addr, true)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read at address 0x%"
                      HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    addr >>= 2;
    switch (addr) {
    case NUCLEI_SPI_TXDATA:
        if (fifo8_is_full(&s->tx_fifo)) {
            r = TXDATA_FULL;
        } else {
            r = 0;
        }
        break;

    case NUCLEI_SPI_RXDATA:
        if (fifo8_is_empty(&s->rx_fifo)) {
            s->regs[NUCLEI_SPI_STATUS] |= STATUS_RXUDR;
            r = RXDATA_EMPTY;
        } else {
            r = fifo8_pop(&s->rx_fifo);
        }
        break;

    default:
        r = s->regs[addr];
        break;
    }

    nuclei_spi_update_irq(s);

    return r;
}

static void nuclei_spi_write(void *opaque, hwaddr addr,
                             uint64_t val64, unsigned int size)
{
    NucleiSPIState *s = opaque;
    uint32_t value = val64;

    if (nuclei_spi_is_bad_reg(addr, false)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write at addr=0x%"
                      HWADDR_PRIx " value=0x%x\n", __func__, addr, value);
        return;
    }

    addr >>= 2;
    switch (addr) {
    case NUCLEI_SPI_SCKMODE:
        s->regs[addr] = value & 0x3;
        break;

    case NUCLEI_SPI_DDR_SCKSAMPLE:
    case NUCLEI_SPI_SDR_SCKSAMPLE:
        s->regs[addr] = value & 0xfff;
        break;

    case NUCLEI_SPI_FORCE:
        s->regs[addr] = value & (FORCE_EN | FORCE_WP);
        break;

    case NUCLEI_SPI_CSID:
        value &= 0x7;
        if (value > s->num_cs) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid csid %d\n",
                          __func__, value);
            s->regs[NUCLEI_SPI_STATUS] |= STATUS_CFGERR;
        } else {
            s->regs[NUCLEI_SPI_CSID] = value;
            nuclei_spi_set_cs_active(s, false);
            nuclei_spi_update_cs(s);
        }
        break;

    case NUCLEI_SPI_CSDEF:
        s->regs[NUCLEI_SPI_CSDEF] = value & 0xf;
        nuclei_spi_update_cs(s);
        break;

    case NUCLEI_SPI_CSMODE:
        value &= 0x3;
        if (value == 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid csmode %x\n",
                          __func__, value);
            s->regs[NUCLEI_SPI_STATUS] |= STATUS_CFGERR;
        } else {
            s->regs[NUCLEI_SPI_CSMODE] = value;
            if (value != CSMODE_HOLD) {
                nuclei_spi_set_cs_active(s, false);
            } else {
                nuclei_spi_update_cs(s);
            }
        }
        break;

    case NUCLEI_SPI_TXDATA:
        if (!fifo8_is_full(&s->tx_fifo)) {
            fifo8_push(&s->tx_fifo, (uint8_t)value);
            nuclei_spi_flush_txfifo(s);
        } else {
            s->regs[NUCLEI_SPI_STATUS] |= STATUS_TXOVR;
        }
        break;

    case NUCLEI_SPI_VERSION:
    case NUCLEI_SPI_FIFO_NUM:
    case NUCLEI_SPI_RXDATA:
    case NUCLEI_SPI_IP:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write to read-only register 0x%"
                      HWADDR_PRIx " with 0x%x\n", __func__, addr << 2, value);
        break;

    case NUCLEI_SPI_TX_MARK:
    case NUCLEI_SPI_RX_MARK:
        if (value >= FIFO_CAPACITY) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid watermark %d\n",
                          __func__, value);
            s->regs[NUCLEI_SPI_STATUS] |= STATUS_CFGERR;
        } else {
            s->regs[addr] = value;
        }
        break;

    case NUCLEI_SPI_FCTRL:
        s->regs[addr] = value & FCTRL_MASK;
        break;

    case NUCLEI_SPI_FFMT:
        s->regs[addr] = value & FFMT_MASK;
        break;

    case NUCLEI_SPI_FFMT1:
        s->regs[addr] = value & FFMT1_MASK;
        break;

    case NUCLEI_SPI_FMT:
        s->regs[addr] = value & (FMT_PROTO_MASK | FMT_ENDIAN | FMT_DIR |
                                 FMT_PROTO_HI | FMT_LEN_MASK);
        break;

    case NUCLEI_SPI_IE:
        s->regs[addr] = value & IE_MASK;
        break;

    case NUCLEI_SPI_BOUNDARY_CFG:
        s->regs[addr] = value & 0x3ff;
        break;

    case NUCLEI_SPI_CR:
        s->regs[addr] = value & CR_MASK;
        if (!(s->regs[addr] & CR_CSOE)) {
            nuclei_spi_set_cs_active(s, false);
        } else {
            nuclei_spi_update_cs(s);
        }
        break;

    case NUCLEI_SPI_STATUS:
        s->regs[NUCLEI_SPI_STATUS] &= ~(value & STATUS_W1C_MASK);
        break;

    default:
        s->regs[addr] = value;
        break;
    }

    nuclei_spi_update_irq(s);
}

static const MemoryRegionOps nuclei_spi_ops = {
    .read = nuclei_spi_read,
    .write = nuclei_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void nuclei_spi_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    NucleiSPIState *s = NUCLEI_SPI(dev);
    int i;

    s->spi = ssi_create_bus(dev, "spi");
    sysbus_init_irq(sbd, &s->irq);

    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    for (i = 0; i < s->num_cs; i++) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &nuclei_spi_ops, s,
                          TYPE_NUCLEI_SPI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);

    fifo8_create(&s->tx_fifo, FIFO_CAPACITY);
    fifo8_create(&s->rx_fifo, FIFO_CAPACITY);
}

static Property nuclei_spi_properties[] = {
    DEFINE_PROP_UINT32("num-cs", NucleiSPIState, num_cs, 1),
    DEFINE_PROP_UINT32("version", NucleiSPIState, version,
                       NUCLEI_SPI_DEFAULT_VERSION),
    DEFINE_PROP_END_OF_LIST(),
};

static void nuclei_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nuclei_spi_properties);
    dc->reset = nuclei_spi_reset;
    dc->realize = nuclei_spi_realize;
}

static const TypeInfo nuclei_spi_info = {
    .name           = TYPE_NUCLEI_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(NucleiSPIState),
    .class_init     = nuclei_spi_class_init,
};

static void nuclei_spi_register_types(void)
{
    type_register_static(&nuclei_spi_info);
}

type_init(nuclei_spi_register_types)
