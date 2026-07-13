#ifndef HW_DMA_NUCLEI_UDMA_H
#define HW_DMA_NUCLEI_UDMA_H

#include "hw/sysbus.h"

#define TYPE_NUCLEI_UDMA "riscv.nuclei.udma"
#define NUCLEI_UDMA(obj) OBJECT_CHECK(NucleiUDMAState, (obj), TYPE_NUCLEI_UDMA)

#define NUCLEI_UDMA_REG_SIZE              0x1000
#define NUCLEI_UDMA_MAX_M2M_CHANNELS      8
#define NUCLEI_UDMA_MAX_PAMEM_CHANNELS    8
#define NUCLEI_UDMA_MAX_PER_CHANNELS      6
#define NUCLEI_UDMA_VERSION_2_5_0         0x00020500
#define NUCLEI_UDMA_DEFAULT_VERSION       NUCLEI_UDMA_VERSION_2_5_0

#define DMA_CFG_PCTRL                   0x000
#define DMA_CFG_EVENT                   0x004

#define DMA_M2M_CFG_BASE                0x008
#define DMA_M2M_CH_STRIDE               0x028
#define DMA_M2M_CFG_MSRCADDR            0x00
#define DMA_M2M_CFG_MDSTADDR            0x04
#define DMA_M2M_CFG_MCTRL              0x08
#define DMA_M2M_CFG_MRPT               0x0c
#define DMA_M2M_CFG_MSIZE              0x10
#define DMA_M2M_CFG_MSRCADDR_H         0x14
#define DMA_M2M_CFG_MDSTADDR_H         0x18
#define DMA_M2M_CFG_MRAU               0x1c
#define DMA_M2M_CFG_MLLA               0x20
#define DMA_M2M_CFG_MLLA_H             0x24

#define DMA_M2M_IRQ_BASE                0x800
#define DMA_M2M_IRQ_CH_STRIDE           0x00c
#define DMA_M2M_IRQ_EN                  0x00
#define DMA_M2M_IRQ_STAT                0x04
#define DMA_M2M_IRQ_CLR                 0x08
#define DMA_M2M_CHAL_IRQ_STAT           0x8f0

#define DMA_PAM_CFG_BASE                0x400
#define DMA_PAM_CH_STRIDE               0x018
#define DMA_PAM_CFG_MSRCADDR            0x00
#define DMA_PAM_CFG_MDSTADDR            0x04
#define DMA_PAM_CFG_MCTRL               0x08
#define DMA_PAM_CFG_MSIZE               0x0c
#define DMA_PAM_CFG_MSRCADDR_H          0x10
#define DMA_PAM_CFG_MDSTADDR_H          0x14

#define DMA_PAM_IRQ_BASE                0xa00
#define DMA_PAM_IRQ_CH_STRIDE           0x00c
#define DMA_PAM_IRQ_EN                  0x00
#define DMA_PAM_IRQ_STAT                0x04
#define DMA_PAM_IRQ_CLR                 0x08
#define DMA_PAM_CHAL_IRQ_STAT           0xaf0

#define DMA_VERSION                     0xffc

#define DMA_CFG_EVENT_RESET             0xffffffffU
#define GENMASK32(h, l)                 ((uint32_t)MAKE_64BIT_MASK((l), ((h) - (l) + 1)))

#define MCTRL_TRANS_EN                  BIT(0)
#define MCTRL_TRANS_STAT                BIT(1)
#define MCTRL_OUTS_CTRL_EN              BIT(2)
#define MCTRL_BST_BND                   BIT(5)
#define MCTRL_TRANS_MODE_SHIFT          6
#define MCTRL_TRANS_MODE_MASK           GENMASK32(7, 6)
#define MCTRL_PRIORITY_SHIFT            8
#define MCTRL_PRIORITY_MASK             GENMASK32(9, 8)
#define MCTRL_MDNA                      BIT(12)
#define MCTRL_MSNA                      BIT(13)
#define MCTRL_MDWIDTH_SHIFT             16
#define MCTRL_MDWIDTH_MASK              GENMASK32(18, 16)
#define MCTRL_MSWIDTH_SHIFT             21
#define MCTRL_MSWIDTH_MASK              GENMASK32(23, 21)
#define MCTRL_MDBURST_SHIFT             24
#define MCTRL_MDBURST_MASK              GENMASK32(27, 24)
#define MCTRL_MSBURST_SHIFT             28
#define MCTRL_MSBURST_MASK              GENMASK32(31, 28)
#define MCTRL_WRITABLE_MASK             (MCTRL_TRANS_EN | \
                                         MCTRL_OUTS_CTRL_EN | \
                                         MCTRL_BST_BND | \
                                         MCTRL_TRANS_MODE_MASK | \
                                         MCTRL_PRIORITY_MASK | \
                                         MCTRL_MDNA | \
                                         MCTRL_MSNA | \
                                         MCTRL_MDWIDTH_MASK | \
                                         MCTRL_MSWIDTH_MASK | \
                                         MCTRL_MDBURST_MASK | \
                                         MCTRL_MSBURST_MASK)

#define MRPT_WRITABLE_MASK              GENMASK32(17, 0)
#define MRPT_TRANS_RPT_MASK             GENMASK32(11, 0)
#define MRPT_RPT_IRQ_EN                 BIT(12)
#define MRPT_RPT_SAUM                   BIT(16)
#define MRPT_RPT_DAUM                   BIT(17)
#define MSIZE_TSIZE_MASK                GENMASK32(24, 0)
#define ADDR_H_MASK                     GENMASK32(15, 0)
#define M2M_IRQ_MASK                    GENMASK32(4, 0)
#define MRAU_RSAU_MASK                  GENMASK32(15, 0)
#define MRAU_RDAU_MASK                  GENMASK32(31, 16)
#define MRAU_RDAU_SHIFT                 16

#define M2M_IRQ_FTRANS                  BIT(0)
#define M2M_IRQ_HTRANS                  BIT(1)
#define M2M_IRQ_RSP_ERR                 BIT(2)
#define M2M_IRQ_LLA_ERR                 BIT(3)
#define M2M_IRQ_LLA_FTRANS              BIT(4)

#define PAM_CTRL_TRANS_EN               BIT(0)
#define PAM_CTRL_TRANS_STAT             BIT(1)
#define PAM_CTRL_TRANS_MODE_SHIFT       6
#define PAM_CTRL_TRANS_MODE_MASK        GENMASK32(7, 6)
#define PAM_CTRL_MDNA                   BIT(12)
#define PAM_CTRL_MSNA                   BIT(13)
#define PAM_CTRL_MWIDTH_SHIFT           16
#define PAM_CTRL_MWIDTH_MASK            GENMASK32(18, 16)
#define PAM_CTRL_TRANS_PER_SEL_SHIFT    24
#define PAM_CTRL_TRANS_PER_SEL_MASK     GENMASK32(26, 24)
#define PAM_CTRL_WRITABLE_MASK          (PAM_CTRL_TRANS_EN | \
                                         PAM_CTRL_TRANS_MODE_MASK | \
                                         PAM_CTRL_MDNA | \
                                         PAM_CTRL_MSNA | \
                                         PAM_CTRL_MWIDTH_MASK | \
                                         PAM_CTRL_TRANS_PER_SEL_MASK)
#define PAM_IRQ_MASK                    GENMASK32(2, 0)

#define PAM_IRQ_FTRANS                  BIT(0)
#define PAM_IRQ_HTRANS                  BIT(1)
#define PAM_IRQ_RSP_ERR                 BIT(2)

#define PAM_TRANS_MODE_SINGLE           0
#define PAM_TRANS_MODE_CONTINUOUS       1
#define NUCLEI_UDMA_PAM_MAX_REQ_QUEUE_DEPTH 64
/*
 * The current QSPI0 + uDMA regression matrix only relies on a small logical
 * queue depth to force backpressure and drop-queued behavior.
 */
#define NUCLEI_UDMA_DEFAULT_PAM_REQ_QUEUE_DEPTH 2

#define M2M_TRANS_MODE_SINGLE           0
#define M2M_TRANS_MODE_CONTINUOUS       1
#define M2M_TRANS_MODE_REPEAT           2
#define M2M_LLA_DESC_WORDS              10
#define M2M_LLA_DESC_SIZE               (M2M_LLA_DESC_WORDS * sizeof(uint32_t))

typedef enum NucleiUDMALLAResult {
    NUCLEI_UDMA_LLA_ERROR = -1,
    NUCLEI_UDMA_LLA_STOP = 0,
    NUCLEI_UDMA_LLA_OK = 1,
} NucleiUDMALLAResult;

enum {
    M2M_LLA_DESC_SRCADDR = 0,
    M2M_LLA_DESC_DSTADDR,
    M2M_LLA_DESC_MCTRL,
    M2M_LLA_DESC_MRPT,
    M2M_LLA_DESC_MSIZE,
    M2M_LLA_DESC_MRAU,
    M2M_LLA_DESC_MLLA,
    M2M_LLA_DESC_SRCADDR_H,
    M2M_LLA_DESC_DSTADDR_H,
    M2M_LLA_DESC_MLLA_H,
};

typedef enum NucleiUDMAPADirection {
    NUCLEI_UDMA_PA_DIR_TX = 0,
    NUCLEI_UDMA_PA_DIR_RX = 1,
} NucleiUDMAPADirection;

typedef struct NucleiUDMAPARequest {
    uint32_t req_len_beats;
    uint32_t remaining_beats;
    uint32_t seq;
    NucleiUDMAPADirection direction;
} NucleiUDMAPARequest;

typedef struct NucleiUDMAM2MChannel {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t ctrl;
    uint32_t run_ctrl;
    uint32_t rpt;
    uint32_t size;
    uint32_t src_addr_hi;
    uint32_t dst_addr_hi;
    uint32_t rau;
    uint32_t lla;
    uint32_t lla_hi;

    uint32_t irq_en;
    uint32_t irq_stat;

    uint32_t run_rpt;
    uint32_t run_rau;
    uint64_t base_src_addr;
    uint64_t base_dst_addr;
    uint64_t cur_src_addr;
    uint64_t cur_dst_addr;
    uint64_t cur_lla_addr;
    uint32_t initial_tsize;
    uint32_t cur_tsize;
    uint32_t repeat_left;
    bool active;
    bool queued;
    bool error;
    bool half_irq_fired;
    bool linked_list_active;
    bool repeat_pause_armed;
    bool repeat_wait_irq_clear;
    bool stop_pending;
} NucleiUDMAM2MChannel;

typedef struct NucleiUDMAPAChannel {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t ctrl;
    uint32_t size;
    uint32_t src_addr_hi;
    uint32_t dst_addr_hi;

    uint32_t irq_en;
    uint32_t irq_stat;

    NucleiUDMAPARequest req_queue[NUCLEI_UDMA_PAM_MAX_REQ_QUEUE_DEPTH];
    uint32_t req_queue_head;
    uint32_t req_queue_tail;
    uint32_t req_queue_count;
    uint32_t req_pending_beats;
    uint32_t req_next_seq;
    uint64_t base_src_addr;
    uint64_t base_dst_addr;
    uint64_t cur_src_addr;
    uint64_t cur_dst_addr;
    uint32_t initial_tsize;
    uint32_t cur_tsize;
    bool active;
    bool error;
    bool half_irq_fired;
} NucleiUDMAPAChannel;

typedef struct NucleiUDMAPerChannel {
    uint32_t reserved;
} NucleiUDMAPerChannel;

typedef struct NucleiUDMAState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    MemoryRegion *mem_mr;
    AddressSpace *dma_as;
    AddressSpace dma_as_storage;
    bool dma_as_uses_system_memory;
    QEMUBH *m2m_bh;

    uint32_t addr_width;
    uint32_t m2m_channels;
    uint32_t pamem_channels;
    uint32_t per_channels;
    uint32_t version;
    uint32_t pamem_request_queue_depth;

    uint32_t pctrl;
    uint32_t event;

    NucleiUDMAM2MChannel m2m[NUCLEI_UDMA_MAX_M2M_CHANNELS];
    NucleiUDMAPAChannel pamem[NUCLEI_UDMA_MAX_PAMEM_CHANNELS];
    NucleiUDMAPerChannel per[NUCLEI_UDMA_MAX_PER_CHANNELS];
} NucleiUDMAState;

bool nuclei_udma_pa_req(NucleiUDMAState *s, uint32_t channel,
                        NucleiUDMAPADirection direction, uint32_t req_len);
bool nuclei_udma_pa_tx_pull_data(NucleiUDMAState *s, uint32_t channel,
                                 uint32_t *data, uint8_t *width_bytes);
bool nuclei_udma_pa_rx_push_data(NucleiUDMAState *s, uint32_t channel,
                                 uint32_t data, uint8_t width_bytes);
void nuclei_udma_pa_stop(NucleiUDMAState *s, uint32_t channel);

#endif /* HW_DMA_NUCLEI_UDMA_H */
