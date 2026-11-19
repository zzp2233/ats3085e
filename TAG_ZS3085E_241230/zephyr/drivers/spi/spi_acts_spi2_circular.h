/* V11: persistent RX hardware RELOAD, finite TX owns the exact clock count.
 * SPI2_BC only limits READ mode, so TX must NOT reload in full-duplex mode.
 * Included after spi_acts_spi2_dma.h. One caller owns the bus until close().
 */
#ifndef SPI_ACTS_SPI2_CIRCULAR_H_
#define SPI_ACTS_SPI2_CIRCULAR_H_
#if defined(CONFIG_SOC_SERIES_LEOPARD) && defined(CONFIG_PDMA_ACTS)
#define SPI2_CIRC_BYTES 512u
#define SPI2_CIRC_RING_BYTES (2u * SPI2_CIRC_BYTES)
#define SPI2_CIRC_TIMEOUT_US 100000u
#define SPI2_CIRC_RELOAD_GRACE_US 20u
#define SPI2_CIRC_RELOAD (1u << 18)
static struct {
    const struct device *dev, *dma;
    int rxch, txch;
    uint32_t blocks, rx_starts;
    bool active, failed;
} spi2_circ;

static uint32_t spi2_circ_rc(int ch)
{
    return sys_read32(SPI2_DMA_REG(ch, 0x1cu)) & 0xfffffu;
}

int spi_acts_spi2_circular_close(const struct device *dev)
{
    struct acts_spi_data *data;
    struct acts_spi_controller *spi;
    int r, t;
    if (!spi2_circ.active || dev != spi2_circ.dev) return -EINVAL;
    data = DEV_DATA(dev);
    spi = DEV_CFG(dev)->spi;
    spi->ctrl &= ~(SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN | SPI_CTL_WR_MODE_MASK);
    spi->ctrl |= SPI_CTL_SS;
    r = spi2_dma_stop_channel(spi2_circ.dma, spi2_circ.rxch);
    t = spi2_dma_stop_channel(spi2_circ.dma, spi2_circ.txch);
    if (r || t) spi2_dma_faulted = true; /* Keep both reservations/storage. */
    else {
        if (spi2_circ.rxch >= 0) dma_free(spi2_circ.dma, spi2_circ.rxch);
        if (spi2_circ.txch >= 0) dma_free(spi2_circ.dma, spi2_circ.txch);
    }
    spi->ctrl &= ~(SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN | SPI_CTL_CLK_SEL_MASK);
    printk("SPI11 DMA close blocks=%u stop=%d/%d rx_starts=%u\n",
           spi2_circ.blocks, r, t, spi2_circ.rx_starts);
    data->ctx.config = NULL;
    spi2_circ.active = false;
    k_sem_give(&data->ctx.lock);
    return r ? r : t;
}

int spi_acts_spi2_circular_open(const struct device *dev, const struct spi_config *config)
{
    const struct acts_spi_config *cfg;
    struct acts_spi_data *data;
    struct acts_spi_controller *spi;
    struct dma_config dc = {0};
    struct dma_block_config block = {0};
    int ret;
    if (!dev || !config || config->operation !=
        (SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8)) ||
        config->cs || config->slave || config->frequency < 100000u ||
        config->frequency > 8000000u) return -EINVAL;
    if (spi2_circ.active) return -EBUSY;
    cfg = DEV_CFG(dev);
    data = DEV_DATA(dev);
    spi = cfg->spi;
    if (cfg->clock_id != CLOCK_ID_SPI2 || cfg->spi_3wire || cfg->flag_use_dma ||
        !spi2_dma_internal_ram(spi2_dma_tx, SPI2_CIRC_BYTES) ||
        !spi2_dma_internal_ram(spi2_dma_rx, SPI2_CIRC_RING_BYTES)) return -ENOTSUP;
    spi2_circ.dma = device_get_binding(CONFIG_DMA_0_NAME);
    if (!spi2_circ.dma) return -ENODEV;
    spi_context_lock(&data->ctx, false, NULL);
    if (spi2_dma_faulted) { k_sem_give(&data->ctx.lock); return -EIO; }
    spi2_circ.dev = dev;
    spi2_circ.active = true;
    spi2_circ.failed = false;
    spi2_circ.blocks = 0;
    spi2_circ.rx_starts = 0;
    spi2_circ.rxch = spi2_circ.txch = -1;
    /* Establish a safe, clock-free state before allocation can fail. */
    spi->ctrl &= ~(SPI_CTL_RX_DRQ_EN | SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_MASK);
    spi->ctrl |= SPI_CTL_SS;
    spi2_circ.rxch = dma_request(spi2_circ.dma, 0xffu);
    if (spi2_circ.rxch < 0) { ret = spi2_circ.rxch; goto fail; }
    spi2_circ.txch = dma_request(spi2_circ.dma, 0xffu);
    if (spi2_circ.txch < 0) { ret = spi2_circ.txch; goto fail; }
    data->ctx.config = NULL;
    ret = spi_acts_configure(cfg, data, config);
    if (ret) goto fail;
    memset(spi2_dma_rx, 0xa5, SPI2_CIRC_RING_BYTES);
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK |
        SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN | SPI_CTL_RX_FIFO_EN |
        SPI_CTL_TX_FIFO_EN)) | SPI_CTL_SS | SPI_CTL_CLK_SEL_DMA;
    spi->status = SPI_STATUS_ERR_MASK;
    block.source_address = (uint32_t)&spi->rxdat;
    block.dest_address = (uint32_t)spi2_dma_rx;
    block.block_size = SPI2_CIRC_RING_BYTES;
    block.dest_reload_en = 1;
    dc.dma_slot = CONFIG_SPI_2_DMA_ID;
    dc.channel_direction = PERIPHERAL_TO_MEMORY;
    dc.source_data_size = dc.dest_data_size = 1;
    dc.source_burst_length = dc.dest_burst_length = 8;
    dc.block_count = 1;
    dc.head_block = &block;
    /* IRQ disabled for these allocated channels; poll latched HF/TC events. */
    ret = dma_config(spi2_circ.dma, spi2_circ.rxch, &dc);
    if (ret) goto fail;
    if (!(sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 0)) & SPI2_CIRC_RELOAD)) {
        ret = -EIO; goto fail;
    }
    spi->ctrl |= SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;
    ret = dma_start(spi2_circ.dma, spi2_circ.rxch);
    if (ret) goto fail;
    ++spi2_circ.rx_starts;
    spi->ctrl |= SPI_CTL_RX_DRQ_EN;
    printk("SPI11 DMA open rx_reload=1 ring=1024 tx_finite=512 rxch=%d txch=%d rev=11a-wrap\n",
           spi2_circ.rxch, spi2_circ.txch);
    return 0;
fail:
    (void)spi_acts_spi2_circular_close(dev);
    return ret;
}

int spi_acts_spi2_circular_transfer(const struct device *dev, const uint8_t *tx, uint8_t *rx)
{
    struct acts_spi_controller *spi;
    struct dma_status ts = {0};
    uint32_t start, status, pending, expected, mask, remaining, txrem, rxrun;
    uint32_t reload_start = 0;
    bool checking_reload = false;
    unsigned int slot;
    const char *stage = "PRECHECK";
    int ret = 0;
    if (!spi2_circ.active || spi2_circ.failed || dev != spi2_circ.dev) return -EPERM;
    if (!tx || !rx) return -EINVAL;
    spi = DEV_CFG(dev)->spi;
    slot = spi2_circ.blocks & 1u;
    mask = (1u << spi2_circ.rxch) | (1u << (16u + spi2_circ.rxch));
    expected = 1u << (spi2_circ.rxch + (slot ? 0u : 16u));
    pending = sys_read32(DMA_REG_BASE) & mask;
    remaining = spi2_circ_rc(spi2_circ.rxch);
    txrem = spi2_circ_rc(spi2_circ.txch);
    status = spi->status;
    rxrun = sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 4)) & 1u;
    if (pending || !(spi->ctrl & SPI_CTL_SS) ||
        !(sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 0)) & SPI2_CIRC_RELOAD) ||
        !rxrun ||
        (status & (SPI_STATUS_BUSY | SPI_STATUS_ERR_MASK)) ||
        !(status & SPI_STATUS_RX_EMPTY) || !(status & SPI_STATUS_TX_EMPTY)) {
        ret = -EIO; goto fail;
    }
    memcpy(spi2_dma_tx, tx, SPI2_CIRC_BYTES);
    stage = "TX_CONFIG";
    ret = spi2_dma_config_channel(spi2_circ.dma, spi, spi2_circ.txch, true, SPI2_CIRC_BYTES);
    if (ret) goto fail;
    stage = "TX_MODE";
    if (sys_read32(SPI2_DMA_REG(spi2_circ.txch, 0)) & SPI2_CIRC_RELOAD) {
        ret = -EIO; goto fail;
    }
    __asm__ volatile ("dmb" ::: "memory");
    stage = "TX_START";
    ret = dma_start(spi2_circ.dma, spi2_circ.txch);
    if (ret) goto fail;
    spi->ctrl &= ~SPI_CTL_SS;
    start = k_cycle_get_32();
    spi->ctrl |= SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_READ_WRITE;
    for (;;) {
        ret = dma_get_status(spi2_circ.dma, spi2_circ.txch, &ts);
        if (ret) { stage = "TX_STATUS"; break; }
        pending = sys_read32(DMA_REG_BASE) & mask;
        remaining = spi2_circ_rc(spi2_circ.rxch);
        txrem = spi2_circ_rc(spi2_circ.txch);
        /* V11A: sample FIFO state AFTER RX completion/count. The final RX
         * burst can drain between CPU reads. An earlier !RX_EMPTY sample
         * must not be combined with a later HF/TC event as an extra byte. */
        status = spi->status;
        rxrun = sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 4)) & 1u;
        if ((status & SPI_STATUS_ERR_MASK) || (pending & ~expected)) {
            stage = "RX_STATE"; ret = -EIO; break;
        }
        if (!rxrun) {
            uint32_t now = k_cycle_get_32();
            /* The RX channel can be observed between completion and reload.
             * Permit a short recheck only at the ring wrap with TX exhausted. */
            if (!checking_reload && slot && !txrem &&
                (remaining <= SPI_DMA_STRANFER_MIN_LEN ||
                 (pending == expected && remaining == SPI2_CIRC_RING_BYTES))) {
                checking_reload = true;
                reload_start = now;
            }
            if (!checking_reload ||
                k_cyc_to_us_floor32(now - reload_start) >= SPI2_CIRC_RELOAD_GRACE_US) {
                stage = "RX_STATE"; ret = -EIO; break;
            }
        }
        if (!ts.busy && txrem) { stage = "TX_COUNT"; ret = -EIO; break; }
        if (rxrun && !ts.busy && pending == expected &&
            remaining == (slot ? SPI2_CIRC_RING_BYTES : SPI2_CIRC_BYTES) &&
            !(status & SPI_STATUS_BUSY) && (status & SPI_STATUS_TX_EMPTY)) {
            if (!(status & SPI_STATUS_RX_EMPTY)) { stage = "RX_FIFO"; ret = -EIO; }
            break;
        }
        if (k_cyc_to_us_floor32(k_cycle_get_32() - start) >= SPI2_CIRC_TIMEOUT_US) {
            stage = "TIMEOUT"; ret = -ETIMEDOUT; break;
        }
    }
    if (ret) goto fail;
    spi->ctrl &= ~(SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_MASK);
    spi->ctrl |= SPI_CTL_SS;
    /* Clear only this RX channel's observed event; never another client's IRQ. */
    sys_write32(expected, DMA_REG_BASE);
    __asm__ volatile ("dmb" ::: "memory");
    memcpy(rx, spi2_dma_rx + slot * SPI2_CIRC_BYTES, SPI2_CIRC_BYTES);
    ++spi2_circ.blocks;
    return 0;
fail:
    spi2_circ.failed = true;
    spi->ctrl &= ~(SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN | SPI_CTL_WR_MODE_MASK);
    spi->ctrl |= SPI_CTL_SS;
    /* Failure only: no formatting/UART work on the successful block path.
     * "sample" is the last decision input; "halted" is after request gating. */
    printk("SPI11 DMA fail rev=11a-wrap stage=%s ret=%d block=%u slot=%u\n",
           stage, ret, spi2_circ.blocks, slot);
    printk("SPI11 DMA sample sta=%08x pending=%08x expect=%08x rxrem=%u txrem=%u rxrun=%u\n",
           status, pending, expected, remaining, txrem, rxrun);
    printk("SPI11 DMA halted ctrl=%08x sta=%08x rxrun=%u txrun=%u rxrc=%u txrc=%u\n",
           spi->ctrl, spi->status,
           sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 4)) & 1u,
           sys_read32(SPI2_DMA_REG(spi2_circ.txch, 4)) & 1u,
           spi2_circ_rc(spi2_circ.rxch), spi2_circ_rc(spi2_circ.txch));
    return ret;
}
#else
int spi_acts_spi2_circular_open(const struct device *d, const struct spi_config *c)
{ ARG_UNUSED(d); ARG_UNUSED(c); return -ENOTSUP; }
int spi_acts_spi2_circular_transfer(const struct device *d, const uint8_t *t, uint8_t *r)
{ ARG_UNUSED(d); ARG_UNUSED(t); ARG_UNUSED(r); return -ENOTSUP; }
int spi_acts_spi2_circular_close(const struct device *d)
{ ARG_UNUSED(d); return -ENOTSUP; }
#endif
#endif
