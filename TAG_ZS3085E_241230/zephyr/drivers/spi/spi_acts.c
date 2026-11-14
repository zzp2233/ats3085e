/*
 * Copyright (c) 2018 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief SPI driver for Actions SoC
 */

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(spi_acts);

#include "spi_context.h"
#include <errno.h>
#include <device.h>
#include <drivers/spi.h>
#include <drivers/dma.h>
#include <soc.h>
#include <board_cfg.h>
/* SPI registers macros*/
#define SPI_CTL_CLK_SEL_MASK		(0x1 << 31)
#define SPI_CTL_CLK_SEL_CPU			(0x0 << 31)
#define SPI_CTL_CLK_SEL_DMA			(0x1 << 31)

#define SPI_CTL_FIFO_WIDTH_MASK		(0x1 << 30)
#define SPI_CTL_FIFO_WIDTH_8BIT		(0x0 << 30)
#define SPI_CTL_FIFO_WIDTH_32BIT	(0x1 << 30)

#define SPI_CTL_MODE_MASK		(3 << 28)
#define SPI_CTL_MODE(x)			((x) << 28)
#define SPI_CTL_MODE_CPHA		(1 << 28)
#define SPI_CTL_MODE_CPOL		(1 << 29)

//#define SPI_CTL_DUAL_QUAD_SEL_MASK			(0x0 << 27)
//#define SPI_CTL_DUAL_QUAD_SEL_2x_4x			(0x0 << 27)
//#define SPI_CTL_DUAL_QUAD_SEL_DUAL_QUAL		(0x1 << 27)

#define SPI_CTL_RXW_DELAY_MASK		(0x1 << 26)
#define SPI_CTL_RXW_DELAY_2CYCLE	(0x0 << 26)
#define SPI_CTL_RXW_DELAY_3CYCLE	(0x1 << 26)

#define SPI_CTL_DMS_MASK		(0x1 << 25)
#define SPI_CTL_DMS_BURST8		(0x0 << 25)
#define SPI_CTL_DMS_SINGLE		(0x1 << 25)

#define SPI_CTL_TXCEB_MASK				(0x0 << 24)
#define SPI_CTL_TXCEB_NOT_CONVERT		(0x0 << 24)
#define SPI_CTL_TXCEB_CONVERT			(0x1 << 24)

#define SPI_CTL_RXCEB_MASK				(0x0 << 23)
#define SPI_CTL_RXCEB_NOT_CONVERT		(0x0 << 23)
#define SPI_CTL_RXCEB_CONVERT			(0x1 << 23)

#define SPI_CTL_MS_SEL_MASK			(0x1 << 22)
#define SPI_CTL_MS_SEL_MASTER		(0x0 << 22)
#define SPI_CTL_MS_SEL_SLAVE		(0x1 << 22)

#define SPI_CTL_SB_SEL_MASK		(0x1 << 21)
#define SPI_CTL_SB_SEL_MSB		(0x0 << 21)
#define SPI_CTL_SB_SEL_LSB		(0x1 << 21)

#if defined(CONFIG_SOC_SERIES_LEOPARD)
#define SPI_CTL_DELAYCHAIN_MASK		(0x1f << 15)
#define SPI_CTL_DELAYCHAIN_SHIFT	(15)
#define SPI_CTL_DELAYCHAIN(x)		((x) << 15)

#define SPI_STATUS_RX_HALF_FULL		(1 << 13)
#define SPI_STATUS_TX_HALF_FULL		(1 << 12)
#else
#define SPI_CTL_DELAYCHAIN_MASK		(0xf << 16)
#define SPI_CTL_DELAYCHAIN_SHIFT	(16)
#define SPI_CTL_DELAYCHAIN(x)		((x) << 16)
#endif

#define SPI_CTL_REQ_MASK		(0x0 << 15)
#define SPI_CTL_REQ_DISABLE		(0x0 << 15)
#define SPI_CTL_REQ_ENABLE		(0x1 << 15)

#define SPI_CTL_QPIEN_MASK			(0x0 << 14)
#define SPI_CTL_QPIEN_DISABLE		(0x0 << 14)
#define SPI_CTL_QPIEN_ENABLE		(0x1 << 14)

#define SPI_CTL_TOUT_CTRL_MASK			(0x0 << 12)
#define SPI_CTL_TOUT_CTRL_(x)			((x) << 12)

#define SPI_CTL_IO_MODE_MASK			(0x3 << 10)
#define SPI_CTL_IO_MODE_(x)				((x) << 10)


#define SPI_CTL_TX_IRQ_EN		(1 << 9)
#define SPI_CTL_RX_IRQ_EN		(1 << 8)
#define SPI_CTL_TX_DRQ_EN		(1 << 7)
#define SPI_CTL_RX_DRQ_EN		(1 << 6)
#define SPI_CTL_TX_FIFO_EN		(1 << 5)
#define SPI_CTL_RX_FIFO_EN		(1 << 4)
#define SPI_CTL_SS				(1 << 3)
#define SPI_CTL_LOOP			(1 << 2)
#define SPI_CTL_WR_MODE_MASK		(0x3 << 0)
#define SPI_CTL_WR_MODE_DISABLE		(0x0 << 0)
#define SPI_CTL_WR_MODE_READ		(0x1 << 0)
#define SPI_CTL_WR_MODE_WRITE		(0x2 << 0)
#define SPI_CTL_WR_MODE_READ_WRITE	(0x3 << 0)

#define SPI_STATUS_TX_FIFO_WERR		(1 << 11)
#define SPI_STATUS_RX_FIFO_WERR		(1 << 9)
#define SPI_STATUS_RX_FIFO_RERR		(1 << 8)
#define SPI_STATUS_RX_FULL		(1 << 7)
#define SPI_STATUS_RX_EMPTY		(1 << 6)
#define SPI_STATUS_TX_FULL		(1 << 5)
#define SPI_STATUS_TX_EMPTY		(1 << 4)
#define SPI_STATUS_TX_IRQ_PD		(1 << 3)
#define SPI_STATUS_RX_IRQ_PD		(1 << 2)
#define SPI_STATUS_BUSY			(1 << 0)
#define SPI_STATUS_ERR_MASK		(SPI_STATUS_RX_FIFO_RERR |	\
                     SPI_STATUS_RX_FIFO_WERR |	\
                     SPI_STATUS_TX_FIFO_WERR)

#define SPI_DMA_STRANFER_MIN_LEN 8
#define SPI_FIFO_LEN                   64
//#define CONFIG_STANDARD_SPI

struct acts_spi_controller
{
    volatile uint32_t ctrl;
    volatile uint32_t status;
    volatile uint32_t txdat;
    volatile uint32_t rxdat;
    volatile uint32_t bc;
} ;

struct acts_spi_config {
    struct acts_spi_controller *spi;
    uint32_t spiclk_reg;
    const char *dma_dev_name;
    uint8_t txdma_id;
    uint8_t txdma_chan;
    uint8_t rxdma_id;
    uint8_t rxdma_chan;
    uint8_t clock_id;
    uint8_t reset_id;
    uint8_t flag_use_dma:1;
#if defined(CONFIG_SOC_SERIES_LEOPARD)
    uint8_t spi_3wire:1;
    uint8_t delay_chain:6;
#else
    uint8_t delay_chain:7;
#endif
};

struct acts_spi_data {
    struct spi_context ctx;
    const struct device *dma_dev;
    struct k_sem dma_sync;
    uint8_t rxdma_chan;
    uint8_t txdma_chan;
};

#define DEV_CFG(dev) \
    ((const struct acts_spi_config *const)(dev)->config)
#define DEV_DATA(dev) \
    ((struct acts_spi_data *const)(dev)->data)

bool spi_acts_transfer_ongoing(struct acts_spi_data *data)
{
    return spi_context_tx_on(&data->ctx) || spi_context_rx_on(&data->ctx);
}

static void spi_acts_wait_tx_complete(struct acts_spi_controller *spi)
{
    while (!(spi->status & SPI_STATUS_TX_EMPTY))
        ;
    /* wait until tx fifo is empty for master mode*/
    while (((spi->ctrl & SPI_CTL_MS_SEL_MASK) == SPI_CTL_MS_SEL_MASTER) &&
        spi->status & SPI_STATUS_BUSY)
        ;
}

static void spi_acts_set_clk(const struct acts_spi_config *cfg, uint32_t freq_khz)
{
#if 1
    /* setup spi1 clock to 78MHz, coreclk/2 */
    //sys_write32((sys_read32(CMU_SPICLK) & ~0xff00) | 0x0100, CMU_SPICLK);

    /* FIXME: call soc_freq_set_spi_freq()
     * setup spi2 clock to 396/6 =66m bps, coreclk/6
     */
    // devclk=180M, spiclk=devclk/2=90M
    clk_set_rate(cfg->clock_id, freq_khz*1000);
    k_busy_wait(100);
#endif
}


static void dma_done_callback(const struct device *dev, void *callback_data, uint32_t ch , int type)
{
    struct acts_spi_data *data = (struct acts_spi_data *)callback_data;

    //if (type != DMA_IRQ_TC)
        //return;

    LOG_DBG("spi dma transfer is done");

    k_sem_give(&data->dma_sync);
}

static int spi_acts_start_dma(const struct acts_spi_config *cfg,
                  struct acts_spi_data *data,
                  uint32_t dma_chan,
                  uint8_t *buf,
                  int32_t len,
                  bool is_tx,
                  dma_callback_t callback)
{
    struct acts_spi_controller *spi = cfg->spi;
    struct dma_config dma_cfg = {0};
    struct dma_block_config dma_block_cfg = {0};

    if (callback) {
        dma_cfg.dma_callback = callback;
        dma_cfg.user_data = data;
        dma_cfg.complete_callback_en = 1;
    }

    dma_cfg.block_count = 1;
    dma_cfg.head_block = &dma_block_cfg;
    dma_block_cfg.block_size = len;
    if (is_tx) {
        dma_cfg.dma_slot = cfg->txdma_id;
        dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
        dma_block_cfg.source_address = (uint32_t)buf;
        dma_block_cfg.dest_address = (uint32_t)&spi->txdat;
        dma_cfg.dest_data_size = 1;
    } else {
        dma_cfg.dma_slot = cfg->rxdma_id;
        dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
        dma_block_cfg.source_address = (uint32_t)&spi->rxdat;
        dma_block_cfg.dest_address = (uint32_t)buf;
        dma_cfg.source_data_size = 1;
    }

    if(len < 8)//data length is too short
    dma_cfg.source_burst_length = 1;

    if (dma_config(data->dma_dev, dma_chan, &dma_cfg)) {
        LOG_ERR("dma%d config error", dma_chan);
        return -1;
    }

    if (dma_start(data->dma_dev, dma_chan)) {
        LOG_ERR("dma%d start error", dma_chan);
        return -1;
    }

    return 0;
}

static void spi_acts_stop_dma(const struct acts_spi_config *cfg,
                   struct acts_spi_data *data,
                   uint32_t dma_chan)
{
    dma_stop(data->dma_dev, dma_chan);
}

static int spi_acts_read_data_by_dma(const struct acts_spi_config *cfg,
                     struct acts_spi_data *data,
                     uint8_t *buf, int32_t len)
{
    struct acts_spi_controller *spi = cfg->spi;
    int ret;

    spi->bc = len;
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK)) |
            SPI_CTL_WR_MODE_READ | SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_RX_DRQ_EN;

    if(len < 8)
        spi->ctrl |=  SPI_CTL_DMS_SINGLE;

    ret = spi_acts_start_dma(cfg, data, data->rxdma_chan, buf, len,
                 false, dma_done_callback);
    if (ret) {
        LOG_ERR("faield to start dma chan 0x%x\n", data->rxdma_chan);
        goto out;
    }

    /* wait until dma transfer is done */
    k_sem_take(&data->dma_sync, K_FOREVER);

out:
    spi_acts_stop_dma(cfg, data, data->rxdma_chan);
    spi->ctrl = spi->ctrl & ~(SPI_CTL_CLK_SEL_DMA | SPI_CTL_RX_DRQ_EN);

    return ret;
}

static int spi_acts_write_data_by_dma(const struct acts_spi_config *cfg,
                      struct acts_spi_data *data,
                      const uint8_t *buf, int32_t len)
{
    struct acts_spi_controller *spi = cfg->spi;
    int ret;

    spi->bc = len;
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK)) |
            SPI_CTL_WR_MODE_WRITE | SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_TX_DRQ_EN;

    if(len < 8)
        spi->ctrl |=  SPI_CTL_DMS_SINGLE;

    ret = spi_acts_start_dma(cfg, data, data->txdma_chan, (uint8_t *)buf, len,
                 true, dma_done_callback);
    if (ret) {
        LOG_ERR("faield to start tx dma chan 0x%x\n", data->txdma_chan);
        goto out;
    }

    /* wait until dma transfer is done */

    k_sem_take(&data->dma_sync, K_FOREVER);

    /* wait until TX FIFO empty */

    while(!(spi->status & SPI_STATUS_TX_EMPTY));

out:
    spi_acts_stop_dma(cfg, data, data->txdma_chan);
    spi->ctrl = spi->ctrl & ~(SPI_CTL_CLK_SEL_DMA | SPI_CTL_TX_DRQ_EN);

    return ret;
}

int spi_acts_write_read_data_by_dma(const struct acts_spi_config *cfg,
        struct acts_spi_data *data,
        const uint8_t *tx_buf, uint8_t *rx_buf, int32_t len)
{
    struct acts_spi_controller *spi = cfg->spi;
    int ret;

    spi->bc = len;
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK)) |
            SPI_CTL_WR_MODE_READ_WRITE | SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN;

    if(len < 8)
        spi->ctrl |=  SPI_CTL_DMS_SINGLE;

    ret = spi_acts_start_dma(cfg, data, data->rxdma_chan, rx_buf, len,
                 false, dma_done_callback);
    if (ret) {
        LOG_ERR("faield to start dma rx chan 0x%x\n", data->rxdma_chan);
        goto out;
    }

    ret = spi_acts_start_dma(cfg, data, data->txdma_chan, (uint8_t *)tx_buf, len,
                 true, NULL);
    if (ret) {
        LOG_ERR("faield to start dma tx chan 0x%x\n", data->txdma_chan);
        goto out;
    }

    /* wait until dma transfer is done */

    k_sem_take(&data->dma_sync, K_FOREVER);

out:
    spi_acts_stop_dma(cfg, data, data->rxdma_chan);
    spi_acts_stop_dma(cfg, data, data->txdma_chan);

    spi->ctrl = spi->ctrl & ~(SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN);

    return 0;
}


static int spi_acts_write_data_by_cpu(struct acts_spi_controller *spi,
        const uint8_t *wbuf, int32_t len)
{
    int tx_len = 0;

    /* switch to write mode */
    spi->bc = len;
    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_WRITE;
    while (tx_len < len) {
        if(!(spi->status & SPI_STATUS_TX_FULL)) {
            spi->txdat = *wbuf++;
            tx_len++;
        }
    }
    return 0;
}

static int spi_acts_read_data_by_cpu(struct acts_spi_controller *spi,
        uint8_t *rbuf, int32_t len)
{
    int rx_len = 0;

    /* switch to write mode */
    spi->bc = len;
    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_READ;

    while (rx_len < len) {
        if(!(spi->status & SPI_STATUS_RX_EMPTY)) {
            *rbuf++ = spi->rxdat;
            rx_len++;
        }
    }

    return 0;
}

/**
  * @brief  使用 CPU FIFO 完成收发，从机在传输前预装 TX。
  * @param  spi SPI 控制器寄存器基址。
  * @param  wbuf 至少 len 字节发送数据。
  * @param  rbuf 至少 len 字节接收空间。
  * @param  len 本次双向传输字节数。
  * @retval 0：完成；-ETIMEDOUT：约 1 秒内未完成。
  * @note   从机依赖外部 CS 和时钟；预装量不超过一个 FIFO。
  */
static int spi_acts_write_read_data_by_cpu(struct acts_spi_controller *spi,
        const uint8_t *wbuf, uint8_t *rbuf, int32_t len)
{
    int rx_len = 0, tx_len = 0;
    uint32_t preload_before = 0, preload_after = 0;                                                                                      /* 保存预装 FIFO 前后状态，辅助确认发送数据确实入队。 */
    bool slave = (spi->ctrl & SPI_CTL_MS_SEL_SLAVE) != 0;                                                                                /* 根据控制寄存器识别主机/从机角色。 */
	uint32_t cycle;
    /* switch to write mode */
    spi->bc = len;
    /* Prime the slave TX FIFO before enabling the transfer. */
    if (spi->ctrl & SPI_CTL_MS_SEL_SLAVE) {                                                                                              /* 仅从机需要在响应外部时钟前预装发送 FIFO。 */
        spi->ctrl &= ~SPI_CTL_WR_MODE_MASK;                                                                                              /* 先关闭读写传输模式，避免预装过程中开始移位。 */
        preload_before = spi->status;                                                                                                    /* 记录装载前的 FIFO 状态。 */
        while (tx_len < len && tx_len < SPI_FIFO_LEN &&                                                                                  /* 预装不超过本次长度且不超过一个 FIFO 的数据。 */
               !(spi->status & SPI_STATUS_TX_FULL)) {                                                                                    /* 只有 TX FIFO 未满才继续写入。 */
            spi->txdat = *wbuf++;                                                                                                        /* 把当前发送字节写入 TX FIFO，并推进缓冲区指针。 */
            tx_len++;                                                                                                                    /* 增加实际装载到 TX FIFO 的字节数。 */
        }
        preload_after = spi->status;                                                                                                     /* 记录预装后的 FIFO 状态。 */
    }
    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_READ_WRITE;
	cycle = k_cycle_get_32();
    while (rx_len < len || tx_len < len) {

        while((tx_len < rx_len + SPI_FIFO_LEN - 2) && (tx_len < len) && !(spi->status & SPI_STATUS_TX_FULL)) {
            spi->txdat = *wbuf++;
            tx_len++;
        }

        while((rx_len < len) && !(spi->status & SPI_STATUS_RX_EMPTY)) {
            *rbuf++ = spi->rxdat;
            rx_len++;
        }

		if(k_cyc_to_ms_ceil32(k_cycle_get_32()-cycle) > 1000){
			LOG_ERR("spi err: txlen=%d, rxlen=%d, bc=0x%x, ctl=0x%x, status=0x%x\n",tx_len, rx_len, spi->bc, spi->ctrl, spi->status);
            return -ETIMEDOUT;                                                                                                           /* 超时必须返回错误，不能把未完成的传输当作成功。 */
		}
    }
    if (slave) {                                                                                                                         /* 从机测试额外输出预装和完成数量信息。 */
        LOG_INF("SPI slave diag-v2: pre_sta=0x%x loaded_sta=0x%x tx=%d rx=%d",                                                           /* 输出装载状态和实际收发字节数。 */
                preload_before, preload_after, tx_len, rx_len);                                                                          /* 打印预装前后状态及 TX/RX 完成数量。 */
    }
    return 0;
}

int spi_acts_write_data(const struct acts_spi_config *cfg, struct acts_spi_data *data,
    const uint8_t *tx_buf, int len)
{
    int ret;

    if (cfg->flag_use_dma) {
        ret = spi_acts_write_data_by_dma(cfg, data, tx_buf, len);
    } else {
        ret = spi_acts_write_data_by_cpu(cfg->spi, tx_buf, len);
    }

    return ret;
}

int spi_acts_read_data(const struct acts_spi_config *cfg, struct acts_spi_data *data,
    uint8_t *rx_buf, int len)
{
    int ret;

    if (cfg->flag_use_dma) {
        ret = spi_acts_read_data_by_dma(cfg, data,
            rx_buf, len);
    } else {
        ret = spi_acts_read_data_by_cpu(cfg->spi,
            rx_buf, len);
    }


    return ret;
}

int spi_acts_write_read_data(const struct acts_spi_config *cfg, struct acts_spi_data *data,
    uint8_t *tx_buf, uint8_t *rx_buf, int len)
{
    int ret;
    if (cfg->flag_use_dma) {
        ret = spi_acts_write_read_data_by_dma(cfg, data, tx_buf, rx_buf, len);
    } else {
		ret = spi_acts_write_read_data_by_cpu(cfg->spi, tx_buf, rx_buf, len);
    }

    return ret;
}

/**
  * @brief  传输上下文中的一个数据块并推进缓冲区。
  * @param  cfg 控制器静态配置，包含 DMA 开关和时钟信息。
  * @param  data 驱动运行数据及 SPI 上下文。
  * @retval 0：完成；负值：参数、FIFO、超时或底层传输错误。
  * @note   SPI2 CPU 从机要求等长非空的双向缓冲区，使用硬件片选。
  */
int spi_acts_transfer_data(const struct acts_spi_config *cfg, struct acts_spi_data *data)
{
    struct acts_spi_controller *spi = cfg->spi;
    struct spi_context *ctx = &data->ctx;
    int chunk_size;
    int ret = 0;

    chunk_size = spi_context_longest_current_buf(ctx);

    LOG_DBG("tx_len %d, rx_len %d, chunk_size %d",
        ctx->tx_len, ctx->rx_len, chunk_size);

    if (cfg->clock_id == CLOCK_ID_SPI2 && !cfg->flag_use_dma &&                                           /* 这段专用处理只作用于 SPI2 非 DMA 路径。 */
        SPI_OP_MODE_GET(ctx->config->operation) == SPI_OP_MODE_SLAVE) {                                   /* 进一步限定为从机模式，其他 SPI 用法走原逻辑。 */
        /* One equal-length full-duplex transaction using hardware SS. */
        if (!ctx->tx_buf || !ctx->rx_buf || ctx->tx_len != ctx->rx_len) {                                 /* 要求发送和接收缓冲区有效且长度一致。 */
            return -EINVAL;                                                                               /* 参数或缓冲区格式不满足要求，拒绝执行。 */
        }

        spi->status = SPI_STATUS_ERR_MASK;                                                                /* 按状态寄存器规则清除旧错误标志。 */
        spi->ctrl |= SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;                                             /* 开启接收和发送 FIFO。 */
        ret = spi_acts_write_read_data_by_cpu(spi, ctx->tx_buf, ctx->rx_buf,                              /* 调用带预装处理的 CPU 全双工收发实现。 */
                                              chunk_size);                                                /* 本次仅处理上下文给出的当前块长度。 */
        /* RX completion alone does not prove that TX FIFO was consumed.
         * BUSY and BC remaining count are master-only per the datasheet.
         */
        if (!ret) {                                                                                       /* 传输成功后再确认发送 FIFO 已经排空。 */
            uint32_t start = k_cycle_get_32();                                                            /* 记录等待起点，给 FIFO 排空设置上限。 */
            while (!(spi->status & SPI_STATUS_TX_EMPTY)) {                                                /* 从机不能以主机专属 BUSY/BC 语义判断完成，检查 TX 空标志。 */
                if (k_cyc_to_ms_ceil32(k_cycle_get_32() - start) > 10) {                                  /* 发送 FIFO 仍未排空超过 10 ms，按超时退出。 */
                    ret = -ETIMEDOUT;                                                                     /* 记录 FIFO 排空超时。 */
                    break;                                                                                /* 结束当前循环或消息分支，不继续处理后续项。 */
                }
            }
        }
        LOG_INF("SPI2 slave diag-v2: ret=%d ctl=0x%x sta=0x%x bc=0x%x",                                   /* 输出本次驱动结果及控制器状态快照。 */
                ret, spi->ctrl, spi->status, spi->bc);                                                    /* 打印返回码、控制、状态和字节计数寄存器。 */
        if (!ret && (spi->status & SPI_STATUS_ERR_MASK)) {                                                /* 即使收发数量达到目标，也不能忽略硬件错误标志。 */
            ret = -EIO;                                                                                   /* 把硬件错误转换为驱动 I/O 错误。 */
        }

#if defined(CONFIG_SOC_SERIES_LEOPARD)                                                                    /* 以下 GPIO 寄存器检查只适用于 LEOPARD SoC。 */
        LOG_INF("SPI2 pins after: 57=%x 58=%x 59=%x 60=%x",                                               /* 打印传输结束后的四个 SPI2 引脚复用状态。 */
                sys_read32(GPION_CTL(57)), sys_read32(GPION_CTL(58)),                                     /* 读取片选与时钟引脚的配置寄存器。 */
                sys_read32(GPION_CTL(59)), sys_read32(GPION_CTL(60)));                                    /* 读取 MISO 与 MOSI 引脚的配置寄存器。 */
#endif                                                                                                    /* 结束当前条件编译分支。 */
        spi->ctrl &= ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_RX_FIFO_EN |                                        /* 关闭读写模式和接收 FIFO。 */
                       SPI_CTL_TX_FIFO_EN);                                                               /* 同时关闭发送 FIFO，结束本次从机请求。 */
        if (ret) {                                                                                        /* 错误路径执行恢复或停止处理。 */
            /* Discard partial data and force fresh configuration on retry. */
            acts_reset_peripheral(cfg->reset_id);                                                         /* 复位外设，丢弃残留状态，准备后续重新配置。 */
            ctx->config = NULL;                                                                           /* 清除上下文配置缓存，确保下次请求重新初始化。 */
            return ret;                                                                                   /* 将底层错误返回调用者。 */
        }
        spi_context_update_tx(ctx, 1, chunk_size);                                                        /* 按字节推进已完成的发送缓冲区位置。 */
        spi_context_update_rx(ctx, 1, chunk_size);                                                        /* 按字节推进已完成的接收缓冲区位置。 */
        return 0;                                                                                         /* 当前函数或检查路径成功完成。 */
    }

    spi->ctrl |= SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;

    if (ctx->tx_len && ctx->rx_len) {
		#ifdef CONFIG_STANDARD_SPI
		if(ctx->tx_buf && ctx->rx_buf){
			ret = spi_acts_write_read_data(cfg, data, (uint8_t*)ctx->tx_buf, ctx->rx_buf, chunk_size);
		}else if(ctx->tx_buf) {
			ret = spi_acts_write_data(cfg, data, (uint8_t*)ctx->tx_buf, chunk_size);
		}else{
			ret = spi_acts_read_data(cfg, data, ctx->rx_buf, chunk_size);
		}
		spi_context_update_tx(ctx, 1, chunk_size);
		spi_context_update_rx(ctx, 1, chunk_size);
		#else
		if(ctx->tx_buf != NULL) {
			ret = spi_acts_write_data(cfg, data, ctx->tx_buf, ctx->tx_len);
		}
		if (ctx->rx_buf != NULL) {
			ret = spi_acts_read_data(cfg, data, ctx->rx_buf, ctx->rx_len);
        }
        spi_context_update_tx(ctx, 1, ctx->tx_len);
        spi_context_update_rx(ctx, 1, ctx->rx_len);
		#endif

    } else if (ctx->tx_len) {
        ret = spi_acts_write_data(cfg, data, (uint8_t*)ctx->tx_buf, chunk_size);
        spi_context_update_tx(ctx, 1, chunk_size);
    } else {
        ret = spi_acts_read_data(cfg, data, ctx->rx_buf, chunk_size);
        spi_context_update_rx(ctx, 1, chunk_size);
    }
    if (!ret) {
        spi_acts_wait_tx_complete(spi);
        if (spi->status & SPI_STATUS_ERR_MASK) {
            ret = -EIO;
        }
    }
    if (ret) {
        LOG_ERR("spi(%p) transfer error: ctrl: 0x%x, status: 0x%x",
                spi, spi->ctrl, spi->status);
    }

    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_DISABLE;
    spi->status |= SPI_STATUS_ERR_MASK;

    return ret;
}

int spi_acts_configure(const struct acts_spi_config *cfg,
                struct acts_spi_data *spi,
                const struct spi_config *config)
{
    uint32_t ctrl, word_size;
    uint32_t op = config->operation;

    LOG_DBG("%p (prev %p): op 0x%x", config, spi->ctx.config, op);

    ctrl = SPI_CTL_DELAYCHAIN(cfg->delay_chain);

    if (spi_context_configured(&spi->ctx, config)) {
        /* Nothing to do */
        return 0;
    }

    spi_acts_set_clk(cfg, config->frequency / 1000);
#if defined(CONFIG_SOC_SERIES_LEOPARD)
    if (cfg->clock_id == CLOCK_ID_SPI2)
        ctrl |= SPI_CTL_IO_MODE_(cfg->spi_3wire);
#endif
    if (cfg->clock_id == CLOCK_ID_SPI3){
      if (op & SPI_LINES_DUAL){
          ctrl &= ~SPI_CTL_IO_MODE_MASK;
          ctrl |= SPI_CTL_IO_MODE_(2);
          LOG_INF("SPI_2X MODE==");
      }
      if (op & SPI_LINES_QUAD){
          ctrl &= ~SPI_CTL_IO_MODE_MASK;
          ctrl |= SPI_CTL_IO_MODE_(3);
          LOG_INF("SPI_4X MODE==");
      }
    }
    if (SPI_OP_MODE_SLAVE == SPI_OP_MODE_GET(op))
        ctrl |= SPI_CTL_MS_SEL_SLAVE;

    word_size = SPI_WORD_SIZE_GET(op);
    if (word_size == 8)
        ctrl |= SPI_CTL_FIFO_WIDTH_8BIT;
    else if (word_size == 32)
        ctrl |= SPI_CTL_FIFO_WIDTH_32BIT;
    else
        ctrl |= SPI_CTL_FIFO_WIDTH_8BIT;

    if (op & SPI_MODE_CPOL)
        ctrl |= SPI_CTL_MODE_CPOL;

    if (op & SPI_MODE_CPHA)
        ctrl |= SPI_CTL_MODE_CPHA;

    if (op & SPI_MODE_LOOP)
        ctrl |= SPI_CTL_LOOP;

    if (op & SPI_TRANSFER_LSB)
        ctrl |= SPI_CTL_SB_SEL_LSB;

    if (cfg->clock_id == CLOCK_ID_SPI1)
        ctrl |= SPI_CTL_REQ_ENABLE;

    cfg->spi->ctrl = ctrl;

    /* At this point, it's mandatory to set this on the context! */
    spi->ctx.config = config;

    spi_context_cs_configure(&spi->ctx);

    return 0;
}



/**
  * @brief  配置 SPI 控制器并执行请求中的所有数据块。
  * @param  dev SPI 设备实例。
  * @param  config 本次模式、位宽、频率和片选配置。
  * @param  tx_bufs 发送缓冲区集合。
  * @param  rx_bufs 接收缓冲区集合。
  * @param  asynchronous 是否使用异步上下文通知。
  * @param  signal 异步请求的完成信号对象。
  * @retval SPI 上下文完成状态或负错误码。
  * @note   SPI2 CPU 从机每次请求重建外设状态，避免残留 FIFO。
  */
int transceive(const struct device *dev,
              const struct spi_config *config,
              const struct spi_buf_set *tx_bufs,
              const struct spi_buf_set *rx_bufs,
              bool asynchronous,
              struct k_poll_signal *signal)
{
    const struct acts_spi_config *cfg =DEV_CFG(dev);;
    struct acts_spi_data *data = DEV_DATA(dev);;
    struct acts_spi_controller *spi = cfg->spi;
    int ret;

	if ((tx_bufs && !tx_bufs->count) && (rx_bufs && !rx_bufs->count)){
        return 0;
    }

    spi_context_lock(&data->ctx, asynchronous, signal);

#if defined(CONFIG_SOC_SERIES_LEOPARD)                                                /* 以下 GPIO 寄存器检查只适用于 LEOPARD SoC。 */
    if (cfg->clock_id == CLOCK_ID_SPI2 && !cfg->flag_use_dma &&                       /* 这段专用处理只作用于 SPI2 非 DMA 路径。 */
        SPI_OP_MODE_GET(config->operation) == SPI_OP_MODE_SLAVE) {                    /* 进一步限定为从机模式，其他 SPI 用法走原逻辑。 */
        /* Diagnostic recovery: rebuild peripheral state on every request.
         * Observe pin mux without overwriting another owner's configuration.
         */
        LOG_INF("SPI2 diag-v2 before: ctl=0x%x sta=0x%x", spi->ctrl, spi->status);    /* 保存复位前的控制器快照，辅助排查残留状态。 */
        LOG_INF("SPI2 pins before: 57=%x 58=%x 59=%x 60=%x",                          /* 记录复位前四个 SPI2 引脚的复用设置。 */
                sys_read32(GPION_CTL(57)), sys_read32(GPION_CTL(58)),                 /* 读取片选与时钟引脚的配置寄存器。 */
                sys_read32(GPION_CTL(59)), sys_read32(GPION_CTL(60)));                /* 读取 MISO 与 MOSI 引脚的配置寄存器。 */
        acts_reset_peripheral(cfg->reset_id);                                         /* 复位外设，丢弃残留状态，准备后续重新配置。 */
        data->ctx.config = NULL;                                                      /* 取消配置缓存，强制本次请求按当前参数配置。 */
    }
#endif                                                                                /* 结束当前条件编译分支。 */
    /* Configure */
    ret = spi_acts_configure(cfg, data, config);
    if (ret) {
        goto out;
    }

    /* Set buffers info */

    spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

    /* assert chip select */
    if (SPI_OP_MODE_MASTER == SPI_OP_MODE_GET(config->operation)) {
        if (data->ctx.config->cs) {
            spi_context_cs_control(&data->ctx, true);
        } else {
            spi->ctrl &= ~SPI_CTL_SS;
        }
    }
    do {
        ret = spi_acts_transfer_data(cfg, data);
    } while (!ret && spi_acts_transfer_ongoing(data));
    /* deassert chip select */
    if (SPI_OP_MODE_MASTER == SPI_OP_MODE_GET(config->operation)) {
        if (data->ctx.config->cs) {
            spi_context_cs_control(&data->ctx, false);
        } else {
            spi->ctrl |= SPI_CTL_SS;
        }
    }

out:
    spi_context_release(&data->ctx, ret);

    return ret;
}


static int spi_acts_transceive(const struct device *dev,
                  const struct spi_config *config,
                  const struct spi_buf_set *tx_bufs,
                  const struct spi_buf_set *rx_bufs)
{
  return transceive(dev, config, tx_bufs,  rx_bufs, false, NULL);

}

#ifdef CONFIG_SPI_ASYNC
static int spi_acts_transceive_async(const struct device *dev,
                   const struct spi_config *config,
                   const struct spi_buf_set *tx_bufs,
                   const struct spi_buf_set *rx_bufs,
                   struct k_poll_signal *async)
{
    return transceive(dev, config, tx_bufs,  rx_bufs, true, async);

}
#endif /* CONFIG_SPI_ASYNC */


static int spi_acts_release(const struct device *dev,
               const struct spi_config *config)
{
    struct acts_spi_data *data = dev->data;
    spi_context_unlock_unconditionally(&data->ctx);
    return 0;
}

#ifdef SPI_TEST
static int spi_test(const struct device *dev)
{
    struct spi_config config = {
        .frequency = 2500000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
        .slave = 0,
		.cs = NULL,

    };

    struct spi_buf_set tx_bufs;
    struct spi_buf_set rx_bufs;
    struct spi_buf tx_buf[1];
    struct spi_buf rx_buf[1];
    u8_t buf_tx[16];
    u8_t buf_rx[48] = {0};
    int ret;

    memset(buf_tx, 0x86, 16);

    printk("spi test:%s\n", dev->name);

    tx_buf[0].buf = buf_tx;
    tx_buf[0].len = 1;
    rx_buf[0].buf = buf_rx;
    rx_buf[0].len = 1;
    rx_bufs.buffers = rx_buf;
    rx_bufs.count = 1;
    tx_bufs.buffers = tx_buf;
    tx_bufs.count = 1;
    ret = spi_transceive(dev, &config, &tx_bufs, &rx_bufs);
    if(ret)
        printk("spi test error\n");
    else
        printk("spi test pass\n");
    printk("buf_rx : 0x%x 0x%x 0x%x 0x%x\n", buf_rx[0], buf_rx[1], buf_rx[14], buf_rx[15]);

    return 0;
}
#endif

int spi_acts_init(const struct device *dev)
{
    const struct acts_spi_config *config = DEV_CFG(dev);
    struct acts_spi_data *data =  DEV_DATA(dev);
    int chan;

    k_sem_init(&data->dma_sync, 0, 1);
    if (config->flag_use_dma) {

        data->dma_dev = device_get_binding(config->dma_dev_name);
        if (!data->dma_dev){
            LOG_ERR("dma-dev binding err:%s\n", config->dma_dev_name);
            return -ENODEV;
        }
        chan = dma_request(data->dma_dev, config->txdma_chan);
        if(chan < 0){
            LOG_ERR("dma-dev txchan config err chan=%d\n", config->txdma_chan);
            return -ENODEV;
        }
        data->txdma_chan = chan;
        chan = dma_request(data->dma_dev, config->rxdma_chan);
        if(chan < 0){
            LOG_ERR("dma-dev rxchan config err chan=%d\n", config->txdma_chan);
            return -ENODEV;
        }
        data->rxdma_chan = chan;

    }
    printk("spi:clkreg=0x%x, dma=%d \n",config->spiclk_reg, config->flag_use_dma);

    /* enable spi controller clock */
    acts_clock_peripheral_enable(config->clock_id);
    /* reset spi controller */
    acts_reset_peripheral(config->reset_id);
    spi_context_unlock_unconditionally(&data->ctx);
#ifdef SPI_TEST
    spi_test(dev);
#endif

    return 0;
}

const struct spi_driver_api spi_acts_driver_api = {
    .transceive = spi_acts_transceive,
#ifdef CONFIG_SPI_ASYNC
    .transceive_async = spi_acts_transceive_async,
#endif
    .release = spi_acts_release,
};

#define  dma_use(n)  	(\
        .dma_dev_name =  CONFIG_DMA_0_NAME, \
        .txdma_id = CONFIG_SPI_##n##_DMA_ID,\
        .txdma_chan = CONFIG_SPI_##n##_TXDMA_CHAN,\
        .rxdma_id = CONFIG_SPI_##n##_DMA_ID,\
        .rxdma_chan = CONFIG_SPI_##n##_RXDMA_CHAN,\
        .flag_use_dma = 1,					\
        )

#define dma_not(n)	(\
        .flag_use_dma = 0, \
         )

#if defined(CONFIG_SOC_SERIES_LEOPARD)
#define SPI_ACTS_DEFINE_CONFIG(n)					\
    static const struct acts_spi_config spi_acts_config_##n = {    \
           .spi = (struct acts_spi_controller *)SPI##n##_REG_BASE,\
           .spiclk_reg = SPI##n##_REG_BASE,\
           .clock_id = CLOCK_ID_SPI##n,\
           .reset_id = RESET_ID_SPI##n,\
           .spi_3wire = 0,\
            COND_CODE_1(CONFIG_SPI_##n##_USE_DMA,dma_use(n), dma_not(n))\
    }
#else
#define SPI_ACTS_DEFINE_CONFIG(n)					\
    static const struct acts_spi_config spi_acts_config_##n = {    \
           .spi = (struct acts_spi_controller *)SPI##n##_REG_BASE,\
           .spiclk_reg = SPI##n##_REG_BASE,\
           .clock_id = CLOCK_ID_SPI##n,\
           .reset_id = RESET_ID_SPI##n,\
            COND_CODE_1(CONFIG_SPI_##n##_USE_DMA,dma_use(n), dma_not(n))\
    }
#endif

#define SPI_ACTS_DEVICE_INIT(n)						\
    static const struct device DEVICE_NAME_GET(spi_acts_##n);   \
    SPI_ACTS_DEFINE_CONFIG(n);					\
    static struct acts_spi_data spi_acts_dev_data_##n = {		\
        SPI_CONTEXT_INIT_LOCK(spi_acts_dev_data_##n, ctx),	\
        SPI_CONTEXT_INIT_SYNC(spi_acts_dev_data_##n, ctx),	\
    };								\
    DEVICE_DEFINE(spi_acts_##n,				\
                CONFIG_SPI_##n##_NAME,				\
                &spi_acts_init, NULL, &spi_acts_dev_data_##n,	\
                &spi_acts_config_##n, POST_KERNEL,		\
                CONFIG_SPI_INIT_PRIORITY, &spi_acts_driver_api);

#if IS_ENABLED(CONFIG_SPI_1)
                SPI_ACTS_DEVICE_INIT(1)
#endif
#if IS_ENABLED(CONFIG_SPI_2)
                SPI_ACTS_DEVICE_INIT(2)
#endif
#if IS_ENABLED(CONFIG_SPI_3)
                SPI_ACTS_DEVICE_INIT(3)
#endif


