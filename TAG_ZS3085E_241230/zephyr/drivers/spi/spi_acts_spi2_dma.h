/* SPI2 V5 validation path. Included by spi_acts.c after spi_acts_configure().
 * Uses the SDK PDMA allocator; leaves other SPI instances and the PIO path alone.
 * DMA moves the bytes; the calling thread still polls completion (IRQ enabled).
 */
#ifndef SPI_ACTS_SPI2_DMA_H_
#define SPI_ACTS_SPI2_DMA_H_

#if defined(CONFIG_SOC_SERIES_LEOPARD) && defined(CONFIG_PDMA_ACTS)
#define SPI2_DMA_BYTES 512u /* maximum; legacy 256-byte diagnostics remain supported */
#define SPI2_DMA_TIMEOUT_US 40000u
/* PDMA register offsets: SDK pdma_acts.c / ATS3085E DMA register table. */
#define SPI2_DMA_REG(ch, off) (DMA_REG_BASE + ((uint32_t)(ch) + 1u) * 0x100u + (off))
static uint8_t spi2_dma_tx[SPI2_DMA_BYTES] __aligned(4);
static uint8_t spi2_dma_rx[2u * SPI2_DMA_BYTES] __aligned(4); /* V11 RX reload ring */
/* A failed STOP quarantines both buffers and channels until reboot. */
static bool spi2_dma_faulted;
static bool spi2_dma_reported;

static bool spi2_dma_internal_ram(const void *p, size_t n)
{
    uintptr_t a = (uintptr_t)p;
    return a >= CONFIG_SRAM_BASE_ADDRESS &&
        a + n >= a && a + n <= CONFIG_SRAM_BASE_ADDRESS + CONFIG_SRAM_SIZE * 1024u;
}

static int spi2_dma_config_channel(const struct device *dma,
                                  struct acts_spi_controller *spi,
                                  int channel, bool tx, size_t len)
{
    struct dma_block_config block = {0};
    struct dma_config config = {0};
    block.block_size = len;
    block.source_address = tx ? (uint32_t)spi2_dma_tx : (uint32_t)&spi->rxdat;
    block.dest_address = tx ? (uint32_t)&spi->txdat : (uint32_t)spi2_dma_rx;
    config.block_count = 1;
    config.head_block = &block;
    config.dma_slot = CONFIG_SPI_2_DMA_ID;
    config.channel_direction = tx ? MEMORY_TO_PERIPHERAL : PERIPHERAL_TO_MEMORY;
    config.source_data_size = config.dest_data_size = 1;
    config.source_burst_length = config.dest_burst_length = 8;
    /* No callback/IRQ: observe hardware START, RC and SPI wire-idle ourselves. */
    return dma_config(dma, channel, &config);
}

static int spi2_dma_stop_channel(const struct device *dma, int channel)
{
    uint32_t start;
    int ret;
    if (channel < 0) return 0;
    ret = dma_stop(dma, channel);
    if (ret) return ret;
    start = k_cycle_get_32();
    while (sys_read32(SPI2_DMA_REG(channel, 0x04u)) & 1u) {
        if (k_cyc_to_us_floor32(k_cycle_get_32() - start) >= 2000u)
            return -ETIMEDOUT;
    }
    return 0;
}
#endif

int spi_acts_spi2_transceive_dma(const struct device *dev,
                               const struct spi_config *config,
                               const uint8_t *tx, uint8_t *rx, size_t len)
{
#if defined(CONFIG_SOC_SERIES_LEOPARD) && defined(CONFIG_PDMA_ACTS)
    const struct acts_spi_config *cfg;
    struct acts_spi_data *data;
    struct acts_spi_controller *spi;
    const struct device *dma;
    struct dma_status rs = {0}, ts = {0};
    int rxch = -1, txch = -1, ret = 0, rxstop, txstop;
    uint32_t start, status = 0u, rxrem = (uint32_t)len, txrem = (uint32_t)len;
    bool selected = false, configured = false;
    const uint32_t operation = SPI_OP_MODE_MASTER | SPI_MODE_CPOL |
                               SPI_MODE_CPHA | SPI_WORD_SET(8);

    if (!dev || !config || !tx || !rx || (len != 256u && len != SPI2_DMA_BYTES) ||
        config->operation != operation || config->cs || config->slave ||
        config->frequency < 100000u || config->frequency > 8000000u)
        return -EINVAL;
    cfg = DEV_CFG(dev);
    data = DEV_DATA(dev);
    spi = cfg->spi;
    if (cfg->clock_id != CLOCK_ID_SPI2 || cfg->spi_3wire || cfg->flag_use_dma)
        return -ENOTSUP;
    if (!spi2_dma_internal_ram(spi2_dma_tx, len) ||
        !spi2_dma_internal_ram(spi2_dma_rx, len)) return -ENOTSUP;
    dma = device_get_binding(CONFIG_DMA_0_NAME);
    if (!dma) return -ENODEV;
    spi_context_lock(&data->ctx, false, NULL);
    if (spi2_dma_faulted) { ret = -EIO; goto release; }

    rxch = dma_request(dma, 0xffu);
    if (rxch < 0) { ret = rxch; goto release; }
    txch = dma_request(dma, 0xffu);
    if (txch < 0) { ret = txch; goto cleanup; }
    /* The RAM bounce buffers survive even a failed stop; no external-memory
     * cache maintenance or lifetime assumptions are imposed on callers. */
    memcpy(spi2_dma_tx, tx, len);
    memset(spi2_dma_rx, 0xa5, len);
    data->ctx.config = NULL; /* Do not reuse a possibly stale stack config. */
    ret = spi_acts_configure(cfg, data, config);
    if (ret) goto cleanup;
    configured = true;
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_CLK_SEL_MASK |
                   SPI_CTL_DMS_MASK | SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN |
                   SPI_CTL_TX_FIFO_EN | SPI_CTL_RX_FIFO_EN)) | SPI_CTL_SS;
    spi->status = SPI_STATUS_ERR_MASK;
    ret = spi2_dma_config_channel(dma, spi, rxch, false, len);
    if (ret) goto cleanup;
    ret = spi2_dma_config_channel(dma, spi, txch, true, len);
    if (ret) goto cleanup;
    spi->bc = len; /* Length is enforced by the two DMA counters in RW mode. */
    spi->ctrl |= SPI_CTL_CLK_SEL_DMA | SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;
    __asm__ volatile ("dmb" ::: "memory");
    ret = dma_start(dma, rxch);
    if (ret) goto cleanup;
    ret = dma_start(dma, txch);
    if (ret) goto cleanup;
    /* Both channels are armed before CS or any transmit request/clock. */
    spi->ctrl &= ~SPI_CTL_SS;
    selected = true;
    start = k_cycle_get_32();
    spi->ctrl |= SPI_CTL_RX_DRQ_EN | SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_READ_WRITE;
    for (;;) {
        ret = dma_get_status(dma, rxch, &rs);
        if (ret) break;
        ret = dma_get_status(dma, txch, &ts);
        if (ret) break;
        status = spi->status;
        if (status & SPI_STATUS_ERR_MASK) { ret = -EIO; break; }
        if (!rs.busy && !ts.busy) {
            /* SDK get_status reports pending_length=0 whenever stopped,
             * even after an early stop. Read RC independently before cleanup. */
            rxrem = sys_read32(SPI2_DMA_REG(rxch, 0x1cu)) & 0xfffffu;
            txrem = sys_read32(SPI2_DMA_REG(txch, 0x1cu)) & 0xfffffu;
            if (rxrem || txrem) { ret = -EIO; break; }
            if ((status & SPI_STATUS_TX_EMPTY) && !(status & SPI_STATUS_BUSY)) {
                if (!(status & SPI_STATUS_RX_EMPTY)) ret = -EIO;
                break;
            }
        }
        if (k_cyc_to_us_floor32(k_cycle_get_32() - start) >= SPI2_DMA_TIMEOUT_US) {
            ret = -ETIMEDOUT;
            break;
        }
    }
cleanup:
    /* Close peripheral requests before stopping/releasing DMA. */
    if (configured) spi->ctrl &= ~(SPI_CTL_RX_DRQ_EN | SPI_CTL_TX_DRQ_EN);
    if (selected) spi->ctrl |= SPI_CTL_SS;
    rxstop = spi2_dma_stop_channel(dma, rxch);
    txstop = spi2_dma_stop_channel(dma, txch);
    __asm__ volatile ("dmb" ::: "memory");
    if (rxstop || txstop) {
        spi2_dma_faulted = true; /* Keep reservations and static storage. */
        if (!ret) ret = rxstop ? rxstop : txstop;
    } else {
        if (rxch >= 0) dma_free(dma, rxch);
        if (txch >= 0) dma_free(dma, txch);
    }
    if (configured) {
        spi->ctrl &= ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_CLK_SEL_MASK |
                       SPI_CTL_TX_FIFO_EN | SPI_CTL_RX_FIFO_EN);
        if (ret) {
            acts_reset_peripheral(cfg->reset_id);
            spi->ctrl = SPI_CTL_SS | SPI_CTL_MODE_CPOL | SPI_CTL_MODE_CPHA;
        }
    }
    if (!ret) {
        memcpy(rx, spi2_dma_rx, len);
        if (!spi2_dma_reported) {
            printk("SPI2 DMA FIRST_OK bytes=%u rx_ch=%d tx_ch=%d rx_rem=%u tx_rem=%u sta=%08x\n",
                   (unsigned int)len, rxch, txch, rxrem, txrem, status);
            spi2_dma_reported = true;
        }
    }
    if (ret) printk("SPI2 DMA fail ret=%d rxch=%d txch=%d sta=%08x rx_rem=%u tx_rem=%u stop=%d/%d\n",
                    ret, rxch, txch, status, rxrem, txrem, rxstop, txstop);
release:
    data->ctx.config = NULL;
    /* This entry rejects LOCK/HOLD/async. Release directly after clearing the
     * cached stack pointer: SPI_SLAVE's generic release dereferences config. */
    k_sem_give(&data->ctx.lock);
    return ret;
#else
    ARG_UNUSED(dev); ARG_UNUSED(config); ARG_UNUSED(tx); ARG_UNUSED(rx); ARG_UNUSED(len);
    return -ENOTSUP;
#endif
}
#endif
