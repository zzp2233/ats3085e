/*
 * Copyright (c) 2018 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Actions SPI 控制器驱动；公共 SPI API 通过设备函数表进入本实现。
 */

/* 日志模块、板级实例配置与 SoC 寄存器共同决定驱动行为。当前链路为 SPI2、8 位 Mode 3 从机、CPU FIFO；DMA 和 SPI_TEST 为其他配置保留。 */
#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(spi_acts);    /* 注册SPI驱动日志模块，级别由CONFIG_SPI_LOG_LEVEL决定。 */

#include "spi_context.h"
#include <errno.h>
#include <device.h>
#include <drivers/spi.h>
#include <drivers/dma.h>
#include <soc.h>
#include <board_cfg.h>

/* 诊断打印预算：只输出前若干条，之后静默；
 * 打印带宽在 5 ms 级帧周期下会压过收发本身，导致从机来不及预装下一帧。 */
static volatile uint32_t s_spi2_diag_budget = 40;
/* 控制/状态位编码；字段位置来自本驱动寄存器定义，不可套用 STM32 的 SPI_SR/SPI_CR1。 */
#define SPI_CTL_CLK_SEL_MASK		(0x1 << 31)            /* CPU/DMA 访问时钟选择：字段掩码，数值按原实现保留。 */
#define SPI_CTL_CLK_SEL_CPU			(0x0 << 31)    /* CPU/DMA 访问时钟选择：选择 CPU 路径。 */
#define SPI_CTL_CLK_SEL_DMA			(0x1 << 31)    /* CPU/DMA 访问时钟选择：选择 DMA 路径。 */

#define SPI_CTL_FIFO_WIDTH_MASK		(0x1 << 30)    /* FIFO 数据访问宽度：字段掩码，数值按原实现保留。 */
#define SPI_CTL_FIFO_WIDTH_8BIT		(0x0 << 30)    /* FIFO 数据访问宽度：选择 8 位访问。 */
#define SPI_CTL_FIFO_WIDTH_32BIT	(0x1 << 30)    /* FIFO 数据访问宽度：选择 32 位访问。 */

#define SPI_CTL_MODE_MASK		(3 << 28)      /* CPOL/CPHA 时钟模式：字段掩码，数值按原实现保留。 */
#define SPI_CTL_MODE(x)			((x) << 28)    /* CPOL/CPHA 时钟模式：字段编码。 */
#define SPI_CTL_MODE_CPHA		(1 << 28)      /* CPOL/CPHA 时钟模式：CPHA。 */
#define SPI_CTL_MODE_CPOL		(1 << 29)      /* CPOL/CPHA 时钟模式：CPOL。 */

//#define SPI_CTL_DUAL_QUAD_SEL_MASK			(0x0 << 27)
//#define SPI_CTL_DUAL_QUAD_SEL_2x_4x			(0x0 << 27)
//#define SPI_CTL_DUAL_QUAD_SEL_DUAL_QUAL		(0x1 << 27)

#define SPI_CTL_RXW_DELAY_MASK		(0x1 << 26)    /* 接收写入延时选择：字段掩码，数值按原实现保留。 */
#define SPI_CTL_RXW_DELAY_2CYCLE	(0x0 << 26)    /* 接收写入延时选择：2周期编码。 */
#define SPI_CTL_RXW_DELAY_3CYCLE	(0x1 << 26)    /* 接收写入延时选择：3周期编码。 */

#define SPI_CTL_DMS_MASK		(0x1 << 25)    /* DMA 请求突发方式：字段掩码，数值按原实现保留。 */
#define SPI_CTL_DMS_BURST8		(0x0 << 25)    /* DMA 请求突发方式：8单位突发。 */
#define SPI_CTL_DMS_SINGLE		(0x1 << 25)    /* DMA 请求突发方式：单次请求。 */

#define SPI_CTL_TXCEB_MASK				(0x0 << 24)    /* 发送字节转换：字段掩码，数值按原实现保留。 */
#define SPI_CTL_TXCEB_NOT_CONVERT		(0x0 << 24)            /* 发送字节转换：不转换。 */
#define SPI_CTL_TXCEB_CONVERT			(0x1 << 24)            /* 发送字节转换：执行转换。 */

#define SPI_CTL_RXCEB_MASK				(0x0 << 23)    /* 接收字节转换：字段掩码，数值按原实现保留。 */
#define SPI_CTL_RXCEB_NOT_CONVERT		(0x0 << 23)            /* 接收字节转换：不转换。 */
#define SPI_CTL_RXCEB_CONVERT			(0x1 << 23)            /* 接收字节转换：执行转换。 */

#define SPI_CTL_MS_SEL_MASK			(0x1 << 22)    /* 主从角色：字段掩码，数值按原实现保留。 */
#define SPI_CTL_MS_SEL_MASTER		(0x0 << 22)            /* 主从角色：主机编码。 */
#define SPI_CTL_MS_SEL_SLAVE		(0x1 << 22)            /* 主从角色：从机编码。 */

#define SPI_CTL_SB_SEL_MASK		(0x1 << 21)    /* 串行位序：字段掩码，数值按原实现保留。 */
#define SPI_CTL_SB_SEL_MSB		(0x0 << 21)    /* 串行位序：最高位先传。 */
#define SPI_CTL_SB_SEL_LSB		(0x1 << 21)    /* 串行位序：最低位先传。 */

#if defined(CONFIG_SOC_SERIES_LEOPARD)
#define SPI_CTL_DELAYCHAIN_MASK		(0x1f << 15)    /* 接收采样延迟链：字段掩码，数值按原实现保留。 */
#define SPI_CTL_DELAYCHAIN_SHIFT	(15)            /* 接收采样延迟链：字段起始位号。 */
#define SPI_CTL_DELAYCHAIN(x)		((x) << 15)     /* 接收采样延迟链：字段编码。 */

#define SPI_STATUS_RX_HALF_FULL		(1 << 13)      /* 接收 FIFO 半满状态，仅 LEOPARD 分支定义的状态位。 */
#define SPI_STATUS_TX_HALF_FULL		(1 << 12)      /* 发送 FIFO 半满状态，仅 LEOPARD 分支定义的状态位。 */
#else
#define SPI_CTL_DELAYCHAIN_MASK		(0xf << 16)    /* 接收采样延迟链：字段掩码，数值按原实现保留。 */
#define SPI_CTL_DELAYCHAIN_SHIFT	(16)           /* 接收采样延迟链：字段起始位号。 */
#define SPI_CTL_DELAYCHAIN(x)		((x) << 16)    /* 接收采样延迟链：字段编码。 */
#endif

#define SPI_CTL_REQ_MASK		(0x0 << 15)    /* 请求控制：字段掩码，数值按原实现保留。 */
#define SPI_CTL_REQ_DISABLE		(0x0 << 15)    /* 请求控制：关闭编码。 */
#define SPI_CTL_REQ_ENABLE		(0x1 << 15)    /* 请求控制：开启编码。 */

#define SPI_CTL_QPIEN_MASK			(0x0 << 14)    /* QPI 使能：字段掩码，数值按原实现保留。 */
#define SPI_CTL_QPIEN_DISABLE		(0x0 << 14)            /* QPI 使能：关闭编码。 */
#define SPI_CTL_QPIEN_ENABLE		(0x1 << 14)            /* QPI 使能：开启编码。 */

#define SPI_CTL_TOUT_CTRL_MASK			(0x0 << 12)    /* 超时控制编码：字段掩码，数值按原实现保留。 */
#define SPI_CTL_TOUT_CTRL_(x)			((x) << 12)    /* 超时控制编码：字段编码。 */

#define SPI_CTL_IO_MODE_MASK			(0x3 << 10)            /* I/O 线模式：字段掩码，数值按原实现保留。 */
#define SPI_CTL_IO_MODE_(x)				((x) << 10)    /* I/O 线模式：字段编码。 */


#define SPI_CTL_TX_IRQ_EN		(1 << 9)            /* 发送FIFO中断使能位。 */
#define SPI_CTL_RX_IRQ_EN		(1 << 8)            /* 接收FIFO中断使能位。 */
#define SPI_CTL_TX_DRQ_EN		(1 << 7)            /* 发送DMA硬件请求使能位。 */
#define SPI_CTL_RX_DRQ_EN		(1 << 6)            /* 接收DMA硬件请求使能位。 */
#define SPI_CTL_TX_FIFO_EN		(1 << 5)            /* 发送FIFO使能位。 */
#define SPI_CTL_RX_FIFO_EN		(1 << 4)            /* 接收FIFO使能位。 */
#define SPI_CTL_SS				(1 << 3)    /* 主机硬件片选控制位，驱动清零选中、置位释放。 */
#define SPI_CTL_LOOP			(1 << 2)            /* 内部回环测试使能位。 */
#define SPI_CTL_WR_MODE_MASK		(0x3 << 0)          /* 读写工作模式：字段掩码，数值按原实现保留。 */
#define SPI_CTL_WR_MODE_DISABLE		(0x0 << 0)          /* 读写工作模式：关闭编码。 */
#define SPI_CTL_WR_MODE_READ		(0x1 << 0)          /* 读写工作模式：只读编码。 */
#define SPI_CTL_WR_MODE_WRITE		(0x2 << 0)          /* 读写工作模式：只写编码。 */
#define SPI_CTL_WR_MODE_READ_WRITE	(0x3 << 0)          /* 读写工作模式：全双工编码。 */

#define SPI_STATUS_TX_FIFO_WERR		(1 << 11)    /* 发送 FIFO 写错误的状态位。 */
#define SPI_STATUS_RX_FIFO_WERR		(1 << 9)     /* 接收 FIFO 写错误的状态位。 */
#define SPI_STATUS_RX_FIFO_RERR		(1 << 8)     /* 接收 FIFO 读错误的状态位。 */
#define SPI_STATUS_RX_FULL		(1 << 7)     /* 接收 FIFO 满的状态位。 */
#define SPI_STATUS_RX_EMPTY		(1 << 6)     /* 接收 FIFO 空的状态位。 */
#define SPI_STATUS_TX_FULL		(1 << 5)     /* 发送 FIFO 满的状态位。 */
#define SPI_STATUS_TX_EMPTY		(1 << 4)     /* 发送 FIFO 空的状态位。 */
#define SPI_STATUS_TX_IRQ_PD		(1 << 3)     /* 发送中断挂起的状态位。 */
#define SPI_STATUS_RX_IRQ_PD		(1 << 2)     /* 接收中断挂起的状态位。 */
#define SPI_STATUS_BUSY			(1 << 0)     /* 硬件忙状态，完成判断需区分主从模式的状态位。 */
/* 汇总接收读错、接收写错和发送写错，用于清除及检测本轮硬件错误。 */
#define SPI_STATUS_ERR_MASK		(SPI_STATUS_RX_FIFO_RERR |	\
                     SPI_STATUS_RX_FIFO_WERR |	\
                     SPI_STATUS_TX_FIFO_WERR)

#define SPI_DMA_STRANFER_MIN_LEN 8           /* 保留的 DMA 最小传输长度常量，单位字节；本文件选择 DMA 仍按板级开关。 */
#define SPI_FIFO_LEN                   64    /* CPU 路径采用的 FIFO 容量界限为 64 字节；当前 16 字节帧可整帧预装。 */
//#define CONFIG_STANDARD_SPI

/* 寄存器映射按 32 位顺序排列；volatile 保留每次硬件访问，不提供互斥、缓存维护或处理器内存屏障。 */
struct acts_spi_controller
{
    volatile uint32_t ctrl;      /* 偏移 0x00，控制寄存器，配置角色、模式和 FIFO。 */
    volatile uint32_t status;    /* 偏移 0x04，状态寄存器，含 FIFO 空满及错误标志。 */
    volatile uint32_t txdat;     /* 偏移 0x08，写入数据送入发送 FIFO。 */
    volatile uint32_t rxdat;     /* 偏移 0x0C，读取时取走接收 FIFO 数据。 */
    volatile uint32_t bc;        /* 偏移 0x10，字节计数；完成判据需区分主从语义。 */
} ;

/* 每个实例的只读硬件资源配置，来自 SPI_ACTS_DEFINE_CONFIG；不是应用传入的 spi_config。 */
struct acts_spi_config {
    struct acts_spi_controller *spi;    /* 本实例的硬件寄存器基址。 */
    uint32_t spiclk_reg;                /* 设备配置保留的时钟诊断字段，当前宏填入 SPI 基址。 */
    const char *dma_dev_name;           /* DMA 控制器设备名，只有启用 DMA 才用于绑定。 */
    uint8_t txdma_id;                   /* 发送 DMA 外设请求源编号。 */
    uint8_t txdma_chan;                 /* 发送 DMA 期望通道或已申请的通道号，依所属结构而定。 */
    uint8_t rxdma_id;                   /* 接收 DMA 外设请求源编号。 */
    uint8_t rxdma_chan;                 /* 接收 DMA 期望通道或已申请的通道号，依所属结构而定。 */
    uint8_t clock_id;                   /* SoC 时钟门控和频率选择使用的 CLOCK_ID_*。 */
    uint8_t reset_id;                   /* SoC 复位控制使用的 RESET_ID_*。 */
    uint8_t flag_use_dma:1;             /* 板级路径选择位，0 为 CPU FIFO，1 为 DMA。 */
#if defined(CONFIG_SOC_SERIES_LEOPARD)
    uint8_t spi_3wire:1;                /* LEOPARD 的三线模式配置，当前实例初始化为 0。 */
    uint8_t delay_chain:6;              /* LEOPARD 配置保存的延迟链编码。 */
#else
    uint8_t delay_chain:7;              /* 其他 SoC 分支保存的延迟链编码。 */
#endif
};

/* 每个实例的可变运行状态，设备对象 data 指向这里；与只读 config 分离。 */
struct acts_spi_data {
    struct spi_context ctx;          /* 缓存配置、独占锁及双向缓冲游标。 */
    const struct device *dma_dev;    /* 初始化时绑定的 DMA 设备。 */
    struct k_sem dma_sync;           /* DMA 回调给出、传输函数等待的二值完成信号。 */
    uint8_t rxdma_chan;              /* 接收 DMA 期望通道或已申请的通道号，依所属结构而定。 */
    uint8_t txdma_chan;              /* 发送 DMA 期望通道或已申请的通道号，依所属结构而定。 */
};

/* 从设备对象取得本驱动的静态资源配置；宏参数必须是本驱动设备。 */
#define DEV_CFG(dev) \
    ((const struct acts_spi_config *const)(dev)->config)
/* 从设备对象取得本驱动的可变上下文。 */
#define DEV_DATA(dev) \
    ((struct acts_spi_data *const)(dev)->data)

/**
  * @brief  检查当前上下文是否仍有发送或接收数据。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @retval true：任一当前块剩余非零；false：均无剩余。
  * @note   只读取游标，不读取硬件忙标志。
  */
bool spi_acts_transfer_ongoing(struct acts_spi_data *data)
{
    return spi_context_tx_on(&data->ctx) || spi_context_rx_on(&data->ctx);    /* 任一方向尚有数据字时继续块传输。 */
}

/**
  * @brief  等待 TX FIFO 排空，并在主机模式下等待硬件不忙。
  * @param  spi：[输入/输出]已开时钟的控制器寄存器映射，不可为空。
  * @retval 无。
  * @note   轮询无超时；从机不使用主机 BUSY 完成判据。SPI2 CPU 从机专用路径另有 10 ms 等待。
  */
static void spi_acts_wait_tx_complete(struct acts_spi_controller *spi)
{
    /* 先等待发送FIFO（硬件排队缓冲）为空，确认已无排队数据；此旧路径没有超时。 */
    while (!(spi->status & SPI_STATUS_TX_EMPTY))    /* 轮询发送队列直到排空；此旧等待没有超时。 */
        ;

    /* 主机还需等待移位逻辑不忙；FIFO 空只说明排队数据已取走，不等于最后一位已发送。 */
    while (((spi->ctrl & SPI_CTL_MS_SEL_MASK) == SPI_CTL_MS_SEL_MASTER) &&
        spi->status & SPI_STATUS_BUSY)
        ;
}

/**
  * @brief  把 kHz 请求换算为 Hz 并调用 SoC 时钟分频配置。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  freq_khz：[输入]目标控制器时钟频率，单位 kHz。
  * @retval 无；未传播 clk_set_rate 的状态。
  * @note   配置后忙等 100 微秒；实际速率由时钟源和分频精度决定，从机线上 SCK 仍由外部提供。
  */
static void spi_acts_set_clk(const struct acts_spi_config *cfg, uint32_t freq_khz)
{
#if 1
    /* 以下注释掉的历史时钟写法不执行；当前频率由 clk_set_rate 请求。 */
    //sys_write32((sys_read32(CMU_SPICLK) & ~0xff00) | 0x0100, CMU_SPICLK);

    /* FIXME：原注释曾计划改用 soc_freq_set_spi_freq，并给出历史 396/6 分频示例；
     * 该示例不是当前实际频率，运行值取决于 clk_set_rate 的 SoC 分频实现。 */
    // 历史 180M/2=90M 示例不作为当前时钟结论。
    clk_set_rate(cfg->clock_id, freq_khz*1000);    /* 将 kHz 乘 1000 转成 Hz，提交 SoC 时钟配置。 */
    k_busy_wait(100);                              /* 配置后忙等 100 微秒，让时钟调整稳定。 */
#endif
}


/**
  * @brief  在 DMA 回调中唤醒等待搬运完成的 SPI 调用。
  * @param  dev：[输入]DMA 控制器设备，当前未使用。
  * @param  callback_data：[输入/输出]提交时保存的 acts_spi_data 地址，不可为空。
  * @param  ch：[输入]DMA 通道号，当前未使用。
  * @param  type：[输入]DMA 回调类型，当前实现未筛选。
  * @retval 无。
  * @note   原过滤判断已注释掉，因此本实现对收到的任何回调类型都给出完成许可，不能将其描述为只处理成功完成。
  */
static void dma_done_callback(const struct device *dev, void *callback_data, uint32_t ch , int type)
{
    struct acts_spi_data *data = (struct acts_spi_data *)callback_data;    /* 取回提交 DMA 时保存的 SPI 运行数据。 */

    //if (type != DMA_IRQ_TC)
        //return;

    LOG_DBG("spi dma transfer is done");                                   /* 记录收到 DMA 回调，当前代码不筛选回调类型。 */

    k_sem_give(&data->dma_sync);                                           /* 释放完成许可，唤醒当前 DMA 等待者。 */
}

/**
  * @brief  构造单块 DMA 搬运描述并配置、启动指定方向通道。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  dma_chan：[输入]已申请的 DMA 通道号。
  * @param  buf：[输入/输出]至少 len 字节区；发送可读、接收可写，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @param  is_tx：[输入]true 从内存搬到 TXDAT，false 从 RXDAT 搬到内存。
  * @param  callback：[输入]可选完成回调；非空时保存 data 并启用完成通知。
  * @retval 0：配置及启动请求均成功；-1：配置或启动失败。
  * @note   DMA 分支使用，当前 SPI2 CPU 从机不经过此处；局部配置描述在 dma_config 中按驱动约定消费。
  */
static int spi_acts_start_dma(const struct acts_spi_config *cfg,
                  struct acts_spi_data *data,
                  uint32_t dma_chan,
                  uint8_t *buf,
                  int32_t len,
                  bool is_tx,
                  dma_callback_t callback)
{
    struct acts_spi_controller *spi = cfg->spi;                  /* 取得本实例寄存器映射，后续读写直接作用于外设。 */
    struct dma_config dma_cfg = {0};                             /* 清零 DMA 通道配置，避免未填写选项残留。 */
    struct dma_block_config dma_block_cfg = {0};                 /* 清零单块搬运描述，本次只提交一块。 */

    if (callback) {                                              /* 提供回调时才启用通知并保存用户数据。 */
        dma_cfg.dma_callback = callback;                         /* 绑定 DMA 完成回调入口。 */
        dma_cfg.user_data = data;                                /* 将 SPI 运行数据传回 DMA 回调。 */
        dma_cfg.complete_callback_en = 1;                        /* 请求 DMA 驱动发出完成通知。 */
    }

    dma_cfg.block_count = 1;                                     /* 单次请求只包含一个数据块。 */
    dma_cfg.head_block = &dma_block_cfg;                         /* 指向本次局部单块描述，供 dma_config 消费。 */
    dma_block_cfg.block_size = len;                              /* 搬运字节数与本轮 SPI 长度一致。 */
    if (is_tx) {                                                 /* 发送方向从内存搬到外设数据寄存器。 */
        dma_cfg.dma_slot = cfg->txdma_id;                        /* 选择本控制器的 TX DMA 请求源。 */
        dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;        /* 设置内存到外设方向。 */
        dma_block_cfg.source_address = (uint32_t)buf;            /* 发送数据源为调用者内存区。 */
        dma_block_cfg.dest_address = (uint32_t)&spi->txdat;      /* 发送目的地址为 TXDAT，不是普通内存。 */
        dma_cfg.dest_data_size = 1;                              /* 外设端以 1 字节访问发送寄存器。 */
    } else {
        dma_cfg.dma_slot = cfg->rxdma_id;                        /* 选择本控制器的 RX DMA 请求源。 */
        dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;        /* 设置外设到内存方向。 */
        dma_block_cfg.source_address = (uint32_t)&spi->rxdat;    /* 接收源地址为 RXDAT，读取会消费 FIFO。 */
        dma_block_cfg.dest_address = (uint32_t)buf;              /* 接收目的区由调用者提供。 */
        dma_cfg.source_data_size = 1;                            /* 外设端以 1 字节读取接收寄存器。 */
    }

    if(len < 8)// 短于 8 字节时使用单次突发，避免短块使用默认突发长度。
    dma_cfg.source_burst_length = 1;                             /* 短块请求使用单次突发长度。 */

    if (dma_config(data->dma_dev, dma_chan, &dma_cfg)) {         /* 先把方向、地址和长度提交给 DMA 驱动，失败则不启动。 */
        LOG_ERR("dma%d config error", dma_chan);                 /* 报告DMA描述配置失败的通道号。 */
        return -1;                                               /* 向上报告 DMA 配置或启动失败。 */
    }

    if (dma_start(data->dma_dev, dma_chan)) {                    /* 启动已配置通道；非零状态按启动失败处理。 */
        LOG_ERR("dma%d start error", dma_chan);                  /* 报告DMA启动失败的通道号。 */
        return -1;                                               /* 向上报告 DMA 配置或启动失败。 */
    }

    return 0;                                                    /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}

/**
  * @brief  请求 DMA 驱动停止指定搬运通道。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  dma_chan：[输入]要停止的已申请通道号。
  * @retval 无；忽略 dma_stop 的返回状态。
  * @note   不释放通道所有权，也不自行复位 SPI。
  */
static void spi_acts_stop_dma(const struct acts_spi_config *cfg,
                   struct acts_spi_data *data,
                   uint32_t dma_chan)
{
    dma_stop(data->dma_dev, dma_chan);    /* 停止指定 DMA 通道，本函数不释放通道。 */
}

/**
  * @brief  启动 RX DMA 并等待完成，退出时关闭接收 DMA 请求。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  buf：[输出]至少 len 字节的可写接收区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 0：DMA 启动路径成功；-1：DMA 配置或启动失败。
  * @note   K_FOREVER 无超时；停止通道后返回启动状态，未独立传播回调错误。
  */
static int spi_acts_read_data_by_dma(const struct acts_spi_config *cfg,
                     struct acts_spi_data *data,
                     uint8_t *buf, int32_t len)
{
    struct acts_spi_controller *spi = cfg->spi;                            /* 取得本实例寄存器映射，后续读写直接作用于外设。 */
    int ret;                                                               /* 保存底层配置或收发状态，供当前路径返回。 */

    spi->bc = len;                                                         /* 写入本轮字节计数，读写工作模式随后设置。 */
    /* 保留其他控制字段，清读写模式及DMA突发方式后，按下面组合选择方向、DMA访问时钟和请求使能。 */
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK)) |
            SPI_CTL_WR_MODE_READ | SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_RX_DRQ_EN;

    if(len < 8)                                                            /* 短于 8 字节时选择单次 DMA 请求。 */
        spi->ctrl |=  SPI_CTL_DMS_SINGLE;                                  /* 置位单次 DMA 请求方式。 */

    /* 配置RX DMA从RXDAT搬到buf并启用完成回调。 */
    ret = spi_acts_start_dma(cfg, data, data->rxdma_chan, buf, len,
                 false, dma_done_callback);
    if (ret) {                                                             /* 存在前序错误时记录诊断或退出恢复。 */
        LOG_ERR("faield to start dma chan 0x%x\n", data->rxdma_chan);
        goto out;                                                          /* 跳转到公共清理段，停止已提交的通道或归还总线锁。 */
    }

    /* 等待 DMA 回调释放完成信号量；K_FOREVER 没有超时，不能在中断内阻塞。 */
    k_sem_take(&data->dma_sync, K_FOREVER);                                /* 无限等待回调给出完成许可；此旧路径没有超时。 */

out:
    spi_acts_stop_dma(cfg, data, data->rxdma_chan);                        /* 退出时停止接收 DMA 通道，防止后续继续写缓冲。 */
    /* 双通道停止后关闭DMA访问时钟选择和收发DMA请求，保留其他控制位。 */
    spi->ctrl = spi->ctrl & ~(SPI_CTL_CLK_SEL_DMA | SPI_CTL_RX_DRQ_EN);    /* 恢复 CPU 时钟选择并关闭 RX DMA 请求。 */

    return ret;                                                            /* 向调用者传播本路径保存的状态。 */
}

/**
  * @brief  启动 TX DMA，等待搬运及 TX FIFO 排空后关闭发送 DMA 请求。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  buf：[输入]至少 len 字节的可读发送区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 0：DMA 启动路径成功；-1：DMA 配置或启动失败。
  * @note   信号量等待和 FIFO 排空均没有超时。
  */
static int spi_acts_write_data_by_dma(const struct acts_spi_config *cfg,
                      struct acts_spi_data *data,
                      const uint8_t *buf, int32_t len)
{
    struct acts_spi_controller *spi = cfg->spi;                            /* 取得本实例寄存器映射，后续读写直接作用于外设。 */
    int ret;                                                               /* 保存底层配置或收发状态，供当前路径返回。 */

    spi->bc = len;                                                         /* 写入本轮字节计数，读写工作模式随后设置。 */
    /* 保留其他控制字段，清读写模式及DMA突发方式后，按下面组合选择方向、DMA访问时钟和请求使能。 */
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK)) |
            SPI_CTL_WR_MODE_WRITE | SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_TX_DRQ_EN;

    if(len < 8)                                                            /* 短于 8 字节时选择单次 DMA 请求。 */
        spi->ctrl |=  SPI_CTL_DMS_SINGLE;                                  /* 置位单次 DMA 请求方式。 */

    /* 配置TX DMA从buf搬到TXDAT并启用完成回调。 */
    ret = spi_acts_start_dma(cfg, data, data->txdma_chan, (uint8_t *)buf, len,
                 true, dma_done_callback);
    if (ret) {                                                             /* 存在前序错误时记录诊断或退出恢复。 */
        LOG_ERR("faield to start tx dma chan 0x%x\n", data->txdma_chan);
        goto out;                                                          /* 跳转到公共清理段，停止已提交的通道或归还总线锁。 */
    }

    /* 等待 DMA 回调释放完成信号量；K_FOREVER 没有超时，不能在中断内阻塞。 */

    k_sem_take(&data->dma_sync, K_FOREVER);                                /* 无限等待回调给出完成许可；此旧路径没有超时。 */

    /* DMA 搬运完成后另等 TX FIFO 排空；搬运到 FIFO 与线上发送完成是不同阶段。 */

    while(!(spi->status & SPI_STATUS_TX_EMPTY));                           /* 无限轮询直到TX FIFO排空；此旧DMA路径没有超时。 */

out:
    spi_acts_stop_dma(cfg, data, data->txdma_chan);                        /* 退出时停止发送 DMA 通道。 */
    /* 双通道停止后关闭DMA访问时钟选择和收发DMA请求，保留其他控制位。 */
    spi->ctrl = spi->ctrl & ~(SPI_CTL_CLK_SEL_DMA | SPI_CTL_TX_DRQ_EN);    /* 恢复 CPU 时钟选择并关闭 TX DMA 请求。 */

    return ret;                                                            /* 向调用者传播本路径保存的状态。 */
}

/**
  * @brief  先提交 RX DMA，再提交 TX DMA，等待接收回调后停止双通道。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  tx_buf：[输入]至少 len 字节的可读发送区，不可为空，保持到函数结束。
  * @param  rx_buf：[输出]至少 len 字节的可写接收区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 当前实现固定返回 0，包括启动失败跳到 out 的路径。
  * @note   该原有错误传播限制不能解释为传输总能成功；当前 SPI2 CPU 从机绕过此函数。
  */
int spi_acts_write_read_data_by_dma(const struct acts_spi_config *cfg,
        struct acts_spi_data *data,
        const uint8_t *tx_buf, uint8_t *rx_buf, int32_t len)
{
    struct acts_spi_controller *spi = cfg->spi;        /* 取得本实例寄存器映射，后续读写直接作用于外设。 */
    int ret;                                           /* 保存底层配置或收发状态，供当前路径返回。 */

    spi->bc = len;                                     /* 写入本轮字节计数，读写工作模式随后设置。 */
    /* 保留其他控制字段，清读写模式及DMA突发方式后，按下面组合选择方向、DMA访问时钟和请求使能。 */
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK)) |
            SPI_CTL_WR_MODE_READ_WRITE | SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN;

    if(len < 8)                                        /* 短于 8 字节时选择单次 DMA 请求。 */
        spi->ctrl |=  SPI_CTL_DMS_SINGLE;              /* 置位单次 DMA 请求方式。 */

    /* 双向DMA先准备接收通道，接收完成回调负责释放等待信号量。 */
    ret = spi_acts_start_dma(cfg, data, data->rxdma_chan, rx_buf, len,
                 false, dma_done_callback);
    if (ret) {                                         /* 存在前序错误时记录诊断或退出恢复。 */
        LOG_ERR("faield to start dma rx chan 0x%x\n", data->rxdma_chan);
        goto out;                                      /* 跳转到公共清理段，停止已提交的通道或归还总线锁。 */
    }

    /* 接收已准备好后启动发送DMA；发送本身不注册完成回调。 */
    ret = spi_acts_start_dma(cfg, data, data->txdma_chan, (uint8_t *)tx_buf, len,
                 true, NULL);
    if (ret) {                                         /* 存在前序错误时记录诊断或退出恢复。 */
        LOG_ERR("faield to start dma tx chan 0x%x\n", data->txdma_chan);
        goto out;                                      /* 跳转到公共清理段，停止已提交的通道或归还总线锁。 */
    }

    /* 等待 DMA 回调释放完成信号量；K_FOREVER 没有超时，不能在中断内阻塞。 */

    k_sem_take(&data->dma_sync, K_FOREVER);            /* 无限等待回调给出完成许可；此旧路径没有超时。 */

out:
    spi_acts_stop_dma(cfg, data, data->rxdma_chan);    /* 退出时停止接收 DMA 通道，防止后续继续写缓冲。 */
    spi_acts_stop_dma(cfg, data, data->txdma_chan);    /* 退出时停止发送 DMA 通道。 */

    /* 双通道停止后关闭DMA访问时钟选择和收发DMA请求，保留其他控制位。 */
    spi->ctrl = spi->ctrl & ~(SPI_CTL_CLK_SEL_DMA |
            SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN);

    return 0;                                          /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}


/**
  * @brief  在只写模式下逐字节填充发送 FIFO。
  * @param  spi：[输入/输出]已开时钟的控制器寄存器映射，不可为空。
  * @param  wbuf：[输入]至少 len 字节的可读发送区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 固定返回 0；只表示本函数完成装载。
  * @note   内部等待 FIFO 空间没有超时；装载完不等价于线上发送完。
  */
static int spi_acts_write_data_by_cpu(struct acts_spi_controller *spi,
        const uint8_t *wbuf, int32_t len)
{
    int tx_len = 0;                                                             /* 已装载的发送字节数从零开始。 */

    /* 设置本轮字节计数，随后按当前函数配置只读、只写或全双工模式。 */
    spi->bc = len;                                                              /* 写入本轮字节计数，读写工作模式随后设置。 */
    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_WRITE;    /* 保留其他字段并选择只写模式。 */
    while (tx_len < len) {                                                      /* 逐字节装载，直到指定长度全部进入 TX FIFO。 */
        if(!(spi->status & SPI_STATUS_TX_FULL)) {                               /* TX FIFO 未满时才允许写数据，避免发送写错误。 */
            spi->txdat = *wbuf++;                                               /* 写入当前字节并推进发送源地址。 */
            tx_len++;                                                           /* 累计真正装入 TX FIFO 的字节数。 */
        }
    }
    return 0;                                                                   /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}

/**
  * @brief  在只读模式下轮询接收 FIFO 并写入缓冲。
  * @param  spi：[输入/输出]已开时钟的控制器寄存器映射，不可为空。
  * @param  rbuf：[输出]至少 len 字节的可写接收区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 固定返回 0。
  * @note   内部轮询无超时；依赖硬件最终产生足够接收数据。
  */
static int spi_acts_read_data_by_cpu(struct acts_spi_controller *spi,
        uint8_t *rbuf, int32_t len)
{
    int rx_len = 0;                                                            /* 已取出的接收字节数从零开始。 */

    /* 设置本轮字节计数，随后按当前函数配置只读、只写或全双工模式。 */
    spi->bc = len;                                                             /* 写入本轮字节计数，读写工作模式随后设置。 */
    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_READ;    /* 保留其他字段并选择只读模式。 */

    while (rx_len < len) {                                                     /* 持续读取，直到取得指定接收字节数；此单向旧路径无超时。 */
        if(!(spi->status & SPI_STATUS_RX_EMPTY)) {                             /* 接收 FIFO 非空时才读取数据。 */
            *rbuf++ = spi->rxdat;    /* 取走一个接收字节并推进目的地址。 */
            rx_len++;                                                          /* 累计已写入调用者缓冲的接收字节数。 */
        }
    }

    return 0;                                                                  /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}

/**
  * @brief  交替装载 TX 和取走 RX，从机先预装发送 FIFO 再开放收发。
  * @param  spi：[输入/输出]已开时钟的控制器寄存器映射，不可为空。
  * @param  wbuf：[输入]至少 len 字节的可读发送区，不可为空。
  * @param  rbuf：[输出]至少 len 字节的可写接收区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 0：收发字节数均达到 len；-ETIMEDOUT：循环超过约 1000 ms。
  * @note   使用 k_cycle_get_32 差值及 k_cyc_to_ms_ceil32 换算；依赖硬件计数器；从机模式需要外部主机提供SCK。函数本身不完成 FIFO 关闭或失败复位。
  */
static int spi_acts_write_read_data_by_cpu(struct acts_spi_controller *spi,
        const uint8_t *wbuf, uint8_t *rbuf, int32_t len)
{
    int rx_len = 0, tx_len = 0;                                                                                   /* 双向进度分别从零开始，写入 FIFO 不等于接收完成。 */
    uint32_t preload_before = 0, preload_after = 0;                                                               /* 保存预装 FIFO 前后状态，辅助确认发送数据确实入队。 */
    bool slave = (spi->ctrl & SPI_CTL_MS_SEL_SLAVE) != 0;                                                         /* 根据控制寄存器识别主机/从机角色。 */
	uint32_t cycle;                                                                                           /* 记录硬件周期计数起点，用于毫秒超时换算。 */
    /* 设置本轮字节计数，随后按当前函数配置只读、只写或全双工模式。 */
    spi->bc = len;                                                                                                /* 写入本轮字节计数，读写工作模式随后设置。 */
    /* 从机无法决定外部时钟何时到来，先关闭读写模式并预装 TX，再开启全双工响应。 */
    if (spi->ctrl & SPI_CTL_MS_SEL_SLAVE) {                                                                       /* 仅从机需要在响应外部时钟前预装发送 FIFO。 */
        spi->ctrl &= ~SPI_CTL_WR_MODE_MASK;                                                                       /* 先关闭读写传输模式，避免预装过程中开始移位。 */
        preload_before = spi->status;                                                                             /* 记录装载前的 FIFO 状态。 */
        while (tx_len < len && tx_len < SPI_FIFO_LEN &&                                                           /* 预装不超过本次长度且不超过一个 FIFO 的数据。 */
               !(spi->status & SPI_STATUS_TX_FULL)) {                                                             /* 只有 TX FIFO 未满才继续写入。 */
            spi->txdat = *wbuf++;                                                                                 /* 把当前发送字节写入 TX FIFO，并推进缓冲区指针。 */
            tx_len++;                                                                                             /* 增加实际装载到 TX FIFO 的字节数。 */
        }
        preload_after = spi->status;                                                                              /* 记录预装后的 FIFO 状态。 */
    }
    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_READ_WRITE;                                 /* 完成预装后开启全双工收发。 */
	cycle = k_cycle_get_32();                                                                                 /* 保存轮询起点，不使用 OS tick 直接当作毫秒。 */

    /* 交替填充发送 FIFO 和取走接收 FIFO；两者共享约 1 秒截止预算。 */
    while (rx_len < len || tx_len < len) {                                                                        /* 任一方向计数未达到目标时继续交替服务 FIFO。 */

        while((tx_len < rx_len + SPI_FIFO_LEN - 2) && (tx_len < len) && !(spi->status & SPI_STATUS_TX_FULL)) {    /* 限制 TX 相对 RX 的领先量，并在 FIFO 有空间及未超过长度时继续装载。 */
            spi->txdat = *wbuf++;                                                                                 /* 写入当前字节并推进发送源地址。 */
            tx_len++;                                                                                             /* 累计真正装入 TX FIFO 的字节数。 */
        }


        /* 尽快排空已接收数据，避免持续外部时钟导致接收溢出。 */
        while((rx_len < len) && !(spi->status & SPI_STATUS_RX_EMPTY)) {                                           /* 取走当前所有可读 RX 字节，但不超过目标接收长度。 */
            *rbuf++ = spi->rxdat;    /* 取走一个接收字节并推进目的地址。 */
            rx_len++;                                                                                             /* 累计已写入调用者缓冲的接收字节数。 */
        }

		if(k_cyc_to_ms_ceil32(k_cycle_get_32()-cycle) > 1000){                                            /* 将无符号周期差换算为毫秒，超过 1000 ms 则报告超时。 */
			/* 超时日志保留TX/RX进度、字节计数及控制状态，便于区分未给时钟与FIFO异常。 */
			LOG_ERR("spi err: txlen=%d, rxlen=%d, bc=0x%x, ctl=0x%x, status=0x%x\n",tx_len, rx_len, spi->bc, spi->ctrl, spi->status);
            return -ETIMEDOUT;                                                                                    /* 超时必须返回错误，不能把未完成的传输当作成功。 */
		}
    }
    if (slave && s_spi2_diag_budget) {                                                                                                  /* 从机测试额外输出预装和完成数量信息。 */
        LOG_INF("SPI slave diag-v2: pre_sta=0x%x loaded_sta=0x%x tx=%d rx=%d",                                    /* 输出装载状态和实际收发字节数。 */
                preload_before, preload_after, tx_len, rx_len);                                                   /* 打印预装前后状态及 TX/RX 完成数量。 */
            s_spi2_diag_budget--;                                                                                  /* 消耗一条打印预算，预算耗尽后本处静默。 */
    }
    return 0;                                                                                                     /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}

/**
  * @brief  按板级 DMA 开关选择只写搬运路径。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  tx_buf：[输入]至少 len 字节的可读发送区，不可为空，保持到函数结束。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 原样返回 CPU 或 DMA 分支状态。
  * @note   CPU 分支等待无超时；最终发送完成由上层判断。
  */
int spi_acts_write_data(const struct acts_spi_config *cfg, struct acts_spi_data *data,
    const uint8_t *tx_buf, int len)
{
    int ret;                                                         /* 保存底层配置或收发状态，供当前路径返回。 */

    if (cfg->flag_use_dma) {                                         /* 按实例板级开关分派 CPU 或 DMA；当前 SPI2 为 CPU。 */
        ret = spi_acts_write_data_by_dma(cfg, data, tx_buf, len);    /* 使用 TX DMA 搬运本轮发送区。 */
    } else {
        ret = spi_acts_write_data_by_cpu(cfg->spi, tx_buf, len);     /* 由 CPU 逐字节装载发送 FIFO。 */
    }

    return ret;                                                      /* 向调用者传播本路径保存的状态。 */
}

/**
  * @brief  按板级 DMA 开关选择只读搬运路径。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  rx_buf：[输出]至少 len 字节的可写接收区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 原样返回 CPU 或 DMA 分支状态。
  * @note   当前 SPI2 CPU 从机使用双向专用路径。
  */
int spi_acts_read_data(const struct acts_spi_config *cfg, struct acts_spi_data *data,
    uint8_t *rx_buf, int len)
{
    int ret;                    /* 保存底层配置或收发状态，供当前路径返回。 */

    if (cfg->flag_use_dma) {    /* 按实例板级开关分派 CPU 或 DMA；当前 SPI2 为 CPU。 */
        /* 板级DMA路径从外设接收至rx_buf。 */
        ret = spi_acts_read_data_by_dma(cfg, data,
            rx_buf, len);
    } else {
        /* CPU路径轮询接收FIFO，直到取得len字节。 */
        ret = spi_acts_read_data_by_cpu(cfg->spi,
            rx_buf, len);
    }


    return ret;                 /* 向调用者传播本路径保存的状态。 */
}

/**
  * @brief  按板级 DMA 开关选择全双工收发路径。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @param  tx_buf：[输入]至少 len 字节的可读发送区，不可为空，保持到函数结束。
  * @param  rx_buf：[输出]至少 len 字节的可写接收区，不可为空。
  * @param  len：[输入]本轮传输字节数，应为非负且不超过缓冲容量。
  * @retval 原样返回所选分支状态；DMA 分支原实现固定返回 0。
  * @note   双向区域至少各有 len 字节，不能假定各分支错误传播完全相同。
  */
int spi_acts_write_read_data(const struct acts_spi_config *cfg, struct acts_spi_data *data,
    uint8_t *tx_buf, uint8_t *rx_buf, int len)
{
    int ret;                                                                             /* 保存底层配置或收发状态，供当前路径返回。 */
    if (cfg->flag_use_dma) {                                                             /* 按实例板级开关分派 CPU 或 DMA；当前 SPI2 为 CPU。 */
        ret = spi_acts_write_read_data_by_dma(cfg, data, tx_buf, rx_buf, len);           /* DMA 双向收发；此旧实现的返回状态传播有限。 */
    } else {
		ret = spi_acts_write_read_data_by_cpu(cfg->spi, tx_buf, rx_buf, len);    /* CPU 同时服务双向 FIFO，从机先预装 TX。 */
    }

    return ret;                                                                          /* 向调用者传播本路径保存的状态。 */
}

/**
  * @brief  执行当前数据块并处理完成判据、硬件错误与缓冲推进。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  data：[输入/输出]本设备运行数据及上下文，不可为空；调用方需串行化。
  * @retval 0：完成；-EINVAL：SPI2 从机双向缓冲无效或不等长；-ETIMEDOUT：CPU 收发或 TX 排空超时；-EIO：硬件错误；其他值来自底层路径。
  * @note   SPI2 CPU 从机成功才推进游标，失败关闭 FIFO、复位并清缓存；其他旧路径存在先推进后检查错误的不同顺序。
  */
int spi_acts_transfer_data(const struct acts_spi_config *cfg, struct acts_spi_data *data)
{
    struct acts_spi_controller *spi = cfg->spi;                                                                       /* 取得本实例寄存器映射，后续读写直接作用于外设。 */
    struct spi_context *ctx = &data->ctx;                                                                             /* 取得本设备独占上下文及当前块游标。 */
    int chunk_size;                                                                                                   /* 本轮共同处理长度，单位字节，当前 dfs 为 1。 */
    int ret = 0;                                                                                                      /* 在本轮检查前以成功初始化状态。 */

    chunk_size = spi_context_longest_current_buf(ctx);                                                                /* 单向取该方向长度，双向取较短当前块，避免越过块边界。 */

    /* 记录双向剩余长度和本轮共同块长，便于检查块切换。 */
    LOG_DBG("tx_len %d, rx_len %d, chunk_size %d",
        ctx->tx_len, ctx->rx_len, chunk_size);

    if (cfg->clock_id == CLOCK_ID_SPI2 && !cfg->flag_use_dma &&                                                       /* 这段专用处理只作用于 SPI2 非 DMA 路径。 */
        SPI_OP_MODE_GET(ctx->config->operation) == SPI_OP_MODE_SLAVE) {                                               /* 进一步限定为从机模式，其他 SPI 用法走原逻辑。 */
        /* SPI2 CPU 从机专用：以硬件 SS 完成一组双向等长事务，拒绝空数据指针。 */
        if (!ctx->tx_buf || !ctx->rx_buf || ctx->tx_len != ctx->rx_len) {                                             /* 要求发送和接收缓冲区有效且长度一致。 */
            return -EINVAL;                                                                                           /* 参数或缓冲区格式不满足要求，拒绝执行。 */
        }

        spi->status = SPI_STATUS_ERR_MASK;                                                                            /* 按状态寄存器规则清除旧错误标志。 */
        spi->ctrl |= SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;                                                         /* 开启接收和发送 FIFO。 */
        ret = spi_acts_write_read_data_by_cpu(spi, ctx->tx_buf, ctx->rx_buf,                                          /* 调用带预装处理的 CPU 全双工收发实现。 */
                                              chunk_size);                                                            /* 本次仅处理上下文给出的当前块长度。 */
        /* RX 数量满足不能证明 TX FIFO 已耗尽，因此另查 TX_EMPTY。
         * 当前从机完成判据不套用主机模式的 BUSY 和 BC 剩余计数语义。 */
        if (!ret) {                                                                                                   /* 传输成功后再确认发送 FIFO 已经排空。 */
            uint32_t start = k_cycle_get_32();                                                                        /* 记录等待起点，给 FIFO 排空设置上限。 */
            while (!(spi->status & SPI_STATUS_TX_EMPTY)) {                                                            /* 从机不能以主机专属 BUSY/BC 语义判断完成，检查 TX 空标志。 */
                if (k_cyc_to_ms_ceil32(k_cycle_get_32() - start) > 10) {                                              /* 发送 FIFO 仍未排空超过 10 ms，按超时退出。 */
                    ret = -ETIMEDOUT;                                                                                 /* 记录 FIFO 排空超时。 */
                    break;                                                                                            /* 结束当前循环或消息分支，不继续处理后续项。 */
                }
            }
        }
        if (s_spi2_diag_budget) {
        LOG_INF("SPI2 slave diag-v2: ret=%d ctl=0x%x sta=0x%x bc=0x%x",                                               /* 输出本次驱动结果及控制器状态快照。 */
                ret, spi->ctrl, spi->status, spi->bc);                                                                /* 打印返回码、控制、状态和字节计数寄存器。 */
            s_spi2_diag_budget--;
        }
        if (!ret && (spi->status & SPI_STATUS_ERR_MASK)) {                                                            /* 即使收发数量达到目标，也不能忽略硬件错误标志。 */
            ret = -EIO;                                                                                               /* 把硬件错误转换为驱动 I/O 错误。 */
        }

#if defined(CONFIG_SOC_SERIES_LEOPARD)                                                                                /* 以下 GPIO 寄存器检查只适用于 LEOPARD SoC。 */
        if (s_spi2_diag_budget) {
        LOG_INF("SPI2 pins after: 57=%x 58=%x 59=%x 60=%x",                                                           /* 打印传输结束后的四个 SPI2 引脚复用状态。 */
                sys_read32(GPION_CTL(57)), sys_read32(GPION_CTL(58)),                                                 /* 读取片选与时钟引脚的配置寄存器。 */
                sys_read32(GPION_CTL(59)), sys_read32(GPION_CTL(60)));                                                /* 读取 MISO 与 MOSI 引脚的配置寄存器。 */
            s_spi2_diag_budget--;
        }
#endif                                                                                                                /* 结束当前条件编译分支。 */
        spi->ctrl &= ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_RX_FIFO_EN |                                                    /* 关闭读写模式和接收 FIFO。 */
                       SPI_CTL_TX_FIFO_EN);                                                                           /* 同时关闭发送 FIFO，结束本次从机请求。 */
        if (ret) {                                                                                                    /* 错误路径执行恢复或停止处理。 */
            /* 失败不推进缓冲游标；复位丢弃部分硬件状态，并强制下次重新配置。 */
            acts_reset_peripheral(cfg->reset_id);                                                                     /* 复位外设，丢弃残留状态，准备后续重新配置。 */
            ctx->config = NULL;                                                                                       /* 清除上下文配置缓存，确保下次请求重新初始化。 */
            return ret;                                                                                               /* 将底层错误返回调用者。 */
        }
        spi_context_update_tx(ctx, 1, chunk_size);                                                                    /* 按字节推进已完成的发送缓冲区位置。 */
        spi_context_update_rx(ctx, 1, chunk_size);                                                                    /* 按字节推进已完成的接收缓冲区位置。 */
        return 0;                                                                                                     /* 当前函数或检查路径成功完成。 */
    }

    spi->ctrl |= SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;                                                             /* 同时启用双向 FIFO，准备服务本轮事务。 */

    if (ctx->tx_len && ctx->rx_len) {                                                                                 /* 双向当前块均有剩余数据时进入双向或旧顺序收发分支。 */
		#ifdef CONFIG_STANDARD_SPI
		if(ctx->tx_buf && ctx->rx_buf){                                                                       /* 标准 SPI 分支中双向地址均存在，可执行全双工。 */
			ret = spi_acts_write_read_data(cfg, data, (uint8_t*)ctx->tx_buf, ctx->rx_buf, chunk_size);    /* 在标准分支按共同块长执行全双工。 */
		}else if(ctx->tx_buf) {                                                                               /* 标准分支仅发送地址有效时只写。 */
			ret = spi_acts_write_data(cfg, data, (uint8_t*)ctx->tx_buf, chunk_size);                      /* 按共同块长执行发送。 */
		}else{
			ret = spi_acts_read_data(cfg, data, ctx->rx_buf, chunk_size);                                 /* 按共同块长执行接收。 */
		}
		spi_context_update_tx(ctx, 1, chunk_size);                                                            /* 以每数据字1字节推进当前发送块，原旧分支不以成功作为推进前提。 */
		spi_context_update_rx(ctx, 1, chunk_size);                                                            /* 以每数据字1字节推进当前接收块，原旧分支不以成功作为推进前提。 */
		#else
		if(ctx->tx_buf != NULL) {                                                                             /* 未启用 CONFIG_STANDARD_SPI 的旧路径先处理发送区。 */
			ret = spi_acts_write_data(cfg, data, ctx->tx_buf, ctx->tx_len);                               /* 先写完整当前发送块，该路径不是同步全双工。 */
		}
		if (ctx->rx_buf != NULL) {                                                                            /* 旧顺序路径在发送后单独处理接收区。 */
			ret = spi_acts_read_data(cfg, data, ctx->rx_buf, ctx->rx_len);                                /* 再读完整当前接收块，覆盖先前保存的返回状态。 */
        }
        spi_context_update_tx(ctx, 1, ctx->tx_len);                                                                   /* 旧路径推进完整发送块；即使前面失败也执行。 */
        spi_context_update_rx(ctx, 1, ctx->rx_len);                                                                   /* 旧路径推进完整接收块；与专用从机成功后推进不同。 */
		#endif

    } else if (ctx->tx_len) {                                                                                         /* 只有发送方向有剩余长度时执行只写。 */
        ret = spi_acts_write_data(cfg, data, (uint8_t*)ctx->tx_buf, chunk_size);                                      /* 按共同块长执行发送。 */
        spi_context_update_tx(ctx, 1, chunk_size);                                                                    /* 以每数据字1字节推进当前发送块，原旧分支不以成功作为推进前提。 */
    } else {
        ret = spi_acts_read_data(cfg, data, ctx->rx_buf, chunk_size);                                                 /* 按共同块长执行接收。 */
        spi_context_update_rx(ctx, 1, chunk_size);                                                                    /* 以每数据字1字节推进当前接收块，原旧分支不以成功作为推进前提。 */
    }
    if (!ret) {                                                                                                       /* 前序传输未报错时才继续检查硬件完成状态。 */
        spi_acts_wait_tx_complete(spi);                                                                               /* 旧路径等待发送 FIFO 及主机忙状态完成，无超时。 */
        if (spi->status & SPI_STATUS_ERR_MASK) {                                                                      /* 任何定义的 FIFO 错误位均使本轮失败。 */
            ret = -EIO;                                                                                               /* 把硬件 FIFO 错误映射为 I/O 错误。 */
        }
    }
    if (ret) {                                                                                                        /* 存在前序错误时记录诊断或退出恢复。 */
        /* 报告错误时控制器地址、控制字及FIFO状态快照。 */
        LOG_ERR("spi(%p) transfer error: ctrl: 0x%x, status: 0x%x",
                spi, spi->ctrl, spi->status);
    }

    spi->ctrl = (spi->ctrl & ~SPI_CTL_WR_MODE_MASK) | SPI_CTL_WR_MODE_DISABLE;                                        /* 停止当前读写模式，保留其他控制字段。 */
    spi->status |= SPI_STATUS_ERR_MASK;                                                                               /* 按原实现读改写错误状态；与专用路径直接写掩码区分。 */

    return ret;                                                                                                       /* 向调用者传播本路径保存的状态。 */
}

/**
  * @brief  将公共 SPI 配置转换为控制器字段，并缓存配置地址。
  * @param  cfg：[输入]本设备的有效只读静态配置，不可为空。
  * @param  spi：[输入/输出]驱动运行数据，不是寄存器结构，不可为空。
  * @param  config：[输入]有效 SPI 配置，不可为空，传输期间保持地址和内容有效。
  * @retval 当前实现固定返回 0。
  * @note   只按地址判断缓存；位宽非 8/32 时回退到 8 位，不报告不支持。frequency 经 Hz→kHz 截断后交给 SoC 时钟接口。
  */
int spi_acts_configure(const struct acts_spi_config *cfg,
                struct acts_spi_data *spi,
                const struct spi_config *config)
{
    uint32_t ctrl, word_size;                                         /* 组装控制字并保存从 operation 提取的位宽。 */
    uint32_t op = config->operation;                                  /* 读取本次公共操作位域。 */

    LOG_DBG("%p (prev %p): op 0x%x", config, spi->ctx.config, op);    /* 记录新旧配置地址及操作位图，定位指针缓存是否命中。 */

    ctrl = SPI_CTL_DELAYCHAIN(cfg->delay_chain);                      /* 以板级延迟链配置作为控制字初值。 */

    if (spi_context_configured(&spi->ctx, config)) {                  /* 配置地址未变时沿用缓存，跳过硬件配置。 */
        /* 配置地址相同则复用旧设置；只修改同一对象的字段不会在此被检测。 */
        return 0;                                                     /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
    }

    spi_acts_set_clk(cfg, config->frequency / 1000);                  /* 将应用 Hz 截断成 kHz，再请求 SoC 分频。 */
#if defined(CONFIG_SOC_SERIES_LEOPARD)
    if (cfg->clock_id == CLOCK_ID_SPI2)                               /* LEOPARD SPI2 需要设置实例中的三线模式编码。 */
        ctrl |= SPI_CTL_IO_MODE_(cfg->spi_3wire);                     /* 合入 SPI2 三线模式设置。 */
#endif
    if (cfg->clock_id == CLOCK_ID_SPI3){                              /* SPI3 的多线选项在此转换为硬件 I/O 模式。 */
      if (op & SPI_LINES_DUAL){                                       /* 公共配置请求双线时设置相应模式。 */
          ctrl &= ~SPI_CTL_IO_MODE_MASK;                              /* 先清除旧 I/O 模式位，避免多种编码叠加。 */
          ctrl |= SPI_CTL_IO_MODE_(2);                                /* 使用本控制器双线模式编码 2。 */
          LOG_INF("SPI_2X MODE==");                                   /* 记录SPI3切换到双线模式。 */
      }
      if (op & SPI_LINES_QUAD){                                       /* 公共配置请求四线时设置相应模式。 */
          ctrl &= ~SPI_CTL_IO_MODE_MASK;                              /* 先清除旧 I/O 模式位，避免多种编码叠加。 */
          ctrl |= SPI_CTL_IO_MODE_(3);                                /* 使用本控制器四线模式编码 3。 */
          LOG_INF("SPI_4X MODE==");                                   /* 记录SPI3切换到四线模式。 */
      }
    }
    if (SPI_OP_MODE_SLAVE == SPI_OP_MODE_GET(op))                     /* 从公共角色字段提取从机请求。 */
        ctrl |= SPI_CTL_MS_SEL_SLAVE;                                 /* 设置硬件从机角色，由外部产生 SCK/SS。 */

    word_size = SPI_WORD_SIZE_GET(op);                                /* 提取应用配置的每个数据字位数。 */
    if (word_size == 8)                                               /* 8 位请求采用 8 位 FIFO 访问。 */
        ctrl |= SPI_CTL_FIFO_WIDTH_8BIT;                              /* 选择 8 位 FIFO 宽度；其他未支持位宽也回退到此值。 */
    else if (word_size == 32)                                         /* 32 位请求采用 32 位 FIFO 访问。 */
        ctrl |= SPI_CTL_FIFO_WIDTH_32BIT;                             /* 选择 32 位 FIFO 宽度。 */
    else
        ctrl |= SPI_CTL_FIFO_WIDTH_8BIT;                              /* 选择 8 位 FIFO 宽度；其他未支持位宽也回退到此值。 */

    if (op & SPI_MODE_CPOL)                                           /* 配置高空闲时钟极性时设置 CPOL。 */
        ctrl |= SPI_CTL_MODE_CPOL;                                    /* 将公共 CPOL 请求映射到硬件极性位。 */

    if (op & SPI_MODE_CPHA)                                           /* 配置第二边沿采样时设置 CPHA。 */
        ctrl |= SPI_CTL_MODE_CPHA;                                    /* 将公共 CPHA 请求映射到硬件相位位。 */

    if (op & SPI_MODE_LOOP)                                           /* 仅在应用请求内部回环时合入测试位。 */
        ctrl |= SPI_CTL_LOOP;                                         /* 启用控制器内部回环。 */

    if (op & SPI_TRANSFER_LSB)                                        /* 请求最低有效位先传时改变硬件位序。 */
        ctrl |= SPI_CTL_SB_SEL_LSB;                                   /* 选择 LSB 在先。 */

    if (cfg->clock_id == CLOCK_ID_SPI1)                               /* SPI1 另需合入该控制器请求使能位。 */
        ctrl |= SPI_CTL_REQ_ENABLE;                                   /* 设置 SPI1 请求使能。 */

    cfg->spi->ctrl = ctrl;                                            /* 一次写入组装后的硬件控制配置。 */

    /* 软件片选配置要读取 ctx.config，必须先绑定该配置地址。 */
    spi->ctx.config = config;                                         /* 缓存配置地址供软件片选及后续缓存比较使用。 */

    spi_context_cs_configure(&spi->ctx);                              /* 配置可选 GPIO 片选；硬件片选不通过此处设置引脚复用。 */

    return 0;                                                         /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}



/**
  * @brief  串行化一次请求，配置控制器并处理全部缓冲块及片选收尾。
  * @param  dev：[输入]由设备注册宏创建的 SPI 设备，不可为空。
  * @param  config：[输入]有效 SPI 配置，不可为空，传输期间保持地址和内容有效。
  * @param  tx_bufs：[输入]发送集合，可为空；非空时 count 和首项须有效。
  * @param  rx_bufs：[输入/输出]接收集合，可为空；非空时描述可读、数据区可写。
  * @param  asynchronous：[输入]是否保存异步通知方式。
  * @param  signal：[输入/输出]可选异步完成信号，生命周期覆盖请求。
  * @retval 0：成功或双向空集合；-EINVAL、-ETIMEDOUT、-EIO 等来自配置和传输路径。
  * @note   获取上下文锁可能永久等待；当前实现实际收发在本调用内轮询执行。SPI2 CPU 从机每次请求复位且清缓存；从机不主动驱动硬件 SS。
  */
int transceive(const struct device *dev,
              const struct spi_config *config,
              const struct spi_buf_set *tx_bufs,
              const struct spi_buf_set *rx_bufs,
              bool asynchronous,
              struct k_poll_signal *signal)
{
    const struct acts_spi_config *cfg =DEV_CFG(dev);;                                 /* 读取静态实例配置；保留原语句中多余分号。 */
    struct acts_spi_data *data = DEV_DATA(dev);;                                      /* 取得可变运行状态；上下文由本设备独占锁保护。 */
    struct acts_spi_controller *spi = cfg->spi;                                       /* 取得本实例寄存器映射，后续读写直接作用于外设。 */
    int ret;                                                                          /* 保存底层配置或收发状态，供当前路径返回。 */

	if ((tx_bufs && !tx_bufs->count) && (rx_bufs && !rx_bufs->count)){            /* 仅当两个集合都存在且均空时直接成功，NULL集合不满足此条件。 */
        return 0;                                                                     /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
    }

    spi_context_lock(&data->ctx, asynchronous, signal);                               /* 取得总线信号量锁并保存可选异步通知参数，可能永久等待。 */

#if defined(CONFIG_SOC_SERIES_LEOPARD)                                                /* 以下 GPIO 寄存器检查只适用于 LEOPARD SoC。 */
    if (cfg->clock_id == CLOCK_ID_SPI2 && !cfg->flag_use_dma &&                       /* 这段专用处理只作用于 SPI2 非 DMA 路径。 */
        SPI_OP_MODE_GET(config->operation) == SPI_OP_MODE_SLAVE) {                    /* 进一步限定为从机模式，其他 SPI 用法走原逻辑。 */
        /* 每次 SPI2 CPU 从机请求都重建硬件状态并清除配置缓存。
         * 引脚只读诊断，不在此覆盖可能属于其他模块的复用配置。 */
        if (s_spi2_diag_budget) {
        LOG_INF("SPI2 diag-v2 before: ctl=0x%x sta=0x%x", spi->ctrl, spi->status);    /* 保存复位前的控制器快照，辅助排查残留状态。 */
            s_spi2_diag_budget--;
        }
        if (s_spi2_diag_budget) {
        LOG_INF("SPI2 pins before: 57=%x 58=%x 59=%x 60=%x",                          /* 记录复位前四个 SPI2 引脚的复用设置。 */
                sys_read32(GPION_CTL(57)), sys_read32(GPION_CTL(58)),                 /* 读取片选与时钟引脚的配置寄存器。 */
                sys_read32(GPION_CTL(59)), sys_read32(GPION_CTL(60)));                /* 读取 MISO 与 MOSI 引脚的配置寄存器。 */
            s_spi2_diag_budget--;
        }
        acts_reset_peripheral(cfg->reset_id);                                         /* 复位外设，丢弃残留状态，准备后续重新配置。 */
        data->ctx.config = NULL;                                                      /* 取消配置缓存，强制本次请求按当前参数配置。 */
    }
#endif                                                                                /* 结束当前条件编译分支。 */
    /* 先配置控制器；失败时归还上下文锁，不继续访问数据缓冲。 */
    ret = spi_acts_configure(cfg, data, config);                                      /* 将公共配置映射到控制器寄存器并缓存配置地址。 */
    if (ret) {                                                                        /* 存在前序错误时记录诊断或退出恢复。 */
        goto out;                                                                     /* 跳转到公共清理段，停止已提交的通道或归还总线锁。 */
    }

    /* 将缓冲集合转换为当前块游标；当前驱动按每字 1 字节推进。 */

    spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);                       /* 初始化双向块游标，当前驱动按1字节数据字推进。 */

    /* 只有主机主动选中对端；从机硬件 SS 由外部主机驱动。 */
    if (SPI_OP_MODE_MASTER == SPI_OP_MODE_GET(config->operation)) {                   /* 主机才主动控制片选，从机响应外部SS。 */
        if (data->ctx.config->cs) {                                                   /* 存在软件片选描述时经GPIO驱动控制。 */
            spi_context_cs_control(&data->ctx, true);                                 /* 按配置极性拉到软件片选有效状态。 */
        } else {
            spi->ctrl &= ~SPI_CTL_SS;                                                 /* 硬件片选路径清SS控制位以选中从机。 */
        }
    }
    do {                                                                              /* 先处理至少一轮当前数据块，再检查双方剩余长度。 */
        ret = spi_acts_transfer_data(cfg, data);                                      /* 执行当前块的搬运、完成检查及必要恢复。 */
    } while (!ret && spi_acts_transfer_ongoing(data));                                /* 无错误且仍有剩余数据时处理下一块。 */
    /* 收发循环退出后，主机按软件 GPIO 或硬件 SS 路径释放片选。 */
    if (SPI_OP_MODE_MASTER == SPI_OP_MODE_GET(config->operation)) {                   /* 主机才主动控制片选，从机响应外部SS。 */
        if (data->ctx.config->cs) {                                                   /* 存在软件片选描述时经GPIO驱动控制。 */
            spi_context_cs_control(&data->ctx, false);                                /* 软件片选退出有效状态；HOLD选项可让上下文保留。 */
        } else {
            spi->ctrl |= SPI_CTL_SS;                                                  /* 硬件片选路径置SS控制位以取消选中。 */
        }
    }

out:
    spi_context_release(&data->ctx, ret);                                             /* 根据状态、角色及持锁选项决定是否归还访问许可。 */

    return ret;                                                                       /* 向调用者传播本路径保存的状态。 */
}


/**
  * @brief  作为设备函数表的同步入口，转交通用请求处理。
  * @param  dev：[输入]由设备注册宏创建的 SPI 设备，不可为空。
  * @param  config：[输入]有效 SPI 配置，不可为空，传输期间保持地址和内容有效。
  * @param  tx_bufs：[输入]发送集合，可为空；非空时 count 和首项须有效。
  * @param  rx_bufs：[输入/输出]接收集合，可为空；非空时描述可读、数据区可写。
  * @retval 与 transceive 相同。
  * @note   明确选择同步上下文，完成前不得释放配置和缓冲。
  */
static int spi_acts_transceive(const struct device *dev,
                  const struct spi_config *config,
                  const struct spi_buf_set *tx_bufs,
                  const struct spi_buf_set *rx_bufs)
{
  return transceive(dev, config, tx_bufs,  rx_bufs, false, NULL);    /* 同步包装入口直接执行全部传输，不提交异步信号。 */

}

#ifdef CONFIG_SPI_ASYNC

/**
  * @brief  作为可选异步函数表入口，保存通知参数后转交通用处理。
  * @param  dev：[输入]由设备注册宏创建的 SPI 设备，不可为空。
  * @param  config：[输入]有效 SPI 配置，不可为空，传输期间保持地址和内容有效。
  * @param  tx_bufs：[输入]发送集合，可为空；非空时 count 和首项须有效。
  * @param  rx_bufs：[输入/输出]接收集合，可为空；非空时描述可读、数据区可写。
  * @param  async：[输入/输出]可选完成信号。
  * @retval 与 transceive 相同。
  * @note   仅 CONFIG_SPI_ASYNC 构建存在；当前实现内部仍轮询，且此路径未调用 spi_context_complete，不能保证标准异步完成通知。
  */
static int spi_acts_transceive_async(const struct device *dev,
                   const struct spi_config *config,
                   const struct spi_buf_set *tx_bufs,
                   const struct spi_buf_set *rx_bufs,
                   struct k_poll_signal *async)
{
    return transceive(dev, config, tx_bufs,  rx_bufs, true, async);    /* 异步包装保存信号参数，但当前硬件收发仍在本调用内执行。 */

}
#endif    /* CONFIG_SPI_ASYNC */


/**
  * @brief  强制释放上下文锁和可选 GPIO 片选。
  * @param  dev：[输入]由设备注册宏创建的 SPI 设备，不可为空。
  * @param  config：[输入]有效 SPI 配置，不可为空，传输期间保持地址和内容有效。
  * @retval 固定返回 0。
  * @note   本实现不检查 config 是否匹配锁所有者，调用者需遵守独占约定。
  */
static int spi_acts_release(const struct device *dev,
               const struct spi_config *config)
{
    struct acts_spi_data *data = dev->data;            /* 从设备对象取得上下文以执行显式释放。 */
    spi_context_unlock_unconditionally(&data->ctx);    /* 强制取消片选并释放锁；初始化时用它首次释放静态0初值锁。 */
    return 0;                                          /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}

#ifdef SPI_TEST

/**
  * @brief  执行保留的主机自测并打印接收结果。
  * @param  dev：[输入]由设备注册宏创建的 SPI 设备，不可为空。
  * @retval 固定返回 0；传输错误通过日志报告。
  * @note   仅定义 SPI_TEST 时构建调用；当前默认未启用。缓冲虽较大，实际描述长度仅各 1 字节。
  */
static int spi_test(const struct device *dev)
{
    struct spi_config config = {                                                               /* 保留主机自测配置，详细字段以初始化器为准。 */
        .frequency = 2500000,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
        .slave = 0,
		.cs = NULL,

    };

    struct spi_buf_set tx_bufs;                                                                /* 保存自测发送块集合。 */
    struct spi_buf_set rx_bufs;                                                                /* 保存自测接收块集合。 */
    struct spi_buf tx_buf[1];                                                                  /* 自测仅提交一个发送块。 */
    struct spi_buf rx_buf[1];                                                                  /* 自测仅提交一个接收块。 */
    u8_t buf_tx[16];                                                                           /* 16字节发送空间，描述器实际仅使用首字节。 */
    u8_t buf_rx[48] = {0};                                                                     /* 48字节接收空间清零，实际仅接收首字节。 */
    int ret;                                                                                   /* 保存底层配置或收发状态，供当前路径返回。 */

    memset(buf_tx, 0x86, 16);                                                                  /* 填充旧自测图案0x86。 */

    printk("spi test:%s\n", dev->name);                                                        /* 输出正在自测的设备名。 */

    tx_buf[0].buf = buf_tx;                                                                    /* 发送描述指向本地测试数据。 */
    tx_buf[0].len = 1;                                                                         /* 实际发送1字节，并非整个16字节数组。 */
    rx_buf[0].buf = buf_rx;                                                                    /* 接收描述指向本地结果区。 */
    rx_buf[0].len = 1;                                                                         /* 实际接收1字节。 */
    rx_bufs.buffers = rx_buf;                                                                  /* 接收集合绑定单块描述数组。 */
    rx_bufs.count = 1;                                                                         /* 接收集合项数为1。 */
    tx_bufs.buffers = tx_buf;                                                                  /* 发送集合绑定单块描述数组。 */
    tx_bufs.count = 1;                                                                         /* 发送集合项数为1。 */
    ret = spi_transceive(dev, &config, &tx_bufs, &rx_bufs);                                    /* 经公共API表回到本驱动同步传输。 */
    if(ret)                                                                                    /* 旧自测把任意非0状态当失败。 */
        printk("spi test error\n");                                                            /* 输出旧自测失败提示。 */
    else
        printk("spi test pass\n");                                                             /* 输出传输状态为0的提示，未逐字节比较内容。 */
    printk("buf_rx : 0x%x 0x%x 0x%x 0x%x\n", buf_rx[0], buf_rx[1], buf_rx[14], buf_rx[15]);    /* 打印首字节及未参与本次1字节接收的保留数组位置。 */

    return 0;                                                                                  /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}
#endif

/**
  * @brief  初始化完成信号量、可选 DMA 通道，并启用时钟、复位及首次解锁。
  * @param  dev：[输入]由设备注册宏创建的 SPI 设备，不可为空。
  * @retval 0：初始化结束；-ENODEV：DMA 设备绑定或通道申请失败。
  * @note   由 DEVICE_DEFINE 在 POST_KERNEL、CONFIG_SPI_INIT_PRIORITY 阶段调用；SPI2 DMA=0 时不申请 DMA 通道。
  */
int spi_acts_init(const struct device *dev)
{
    const struct acts_spi_config *config = DEV_CFG(dev);
    struct acts_spi_data *data =  DEV_DATA(dev);
    int chan;                                                                         /* 保存DMA申请返回的通道号或负错误。 */

    k_sem_init(&data->dma_sync, 0, 1);                                                /* 建立容量1、初值0的DMA完成信号量。 */
    if (config->flag_use_dma) {                                                       /* 仅板级开启DMA的实例申请DMA资源，当前SPI2跳过。 */

        data->dma_dev = device_get_binding(config->dma_dev_name);                     /* 按静态配置绑定DMA控制器。 */
        if (!data->dma_dev){                                                          /* 没有DMA控制器则不能初始化DMA实例。 */
            LOG_ERR("dma-dev binding err:%s\n", config->dma_dev_name);                /* 打印无法绑定的DMA设备名。 */
            return -ENODEV;                                                           /* 必需DMA设备或通道不可用，拒绝设备初始化。 */
        }
        chan = dma_request(data->dma_dev, config->txdma_chan);                        /* 申请TX通道，0xff配置按DMA驱动约定动态分配。 */
        if(chan < 0){                                                                 /* 通道申请返回负值表示资源不可用。 */
            LOG_ERR("dma-dev txchan config err chan=%d\n", config->txdma_chan);       /* 报告发送DMA通道申请失败。 */
            return -ENODEV;                                                           /* 必需DMA设备或通道不可用，拒绝设备初始化。 */
        }
        data->txdma_chan = chan;                                                      /* 保存实际分配的发送通道号。 */
        chan = dma_request(data->dma_dev, config->rxdma_chan);                        /* 申请独立RX通道；失败路径原代码未释放此前TX通道。 */
        if(chan < 0){                                                                 /* 通道申请返回负值表示资源不可用。 */
            LOG_ERR("dma-dev rxchan config err chan=%d\n", config->txdma_chan);       /* 输出原接收申请失败日志，原参数仍使用txdma_chan。 */
            return -ENODEV;                                                           /* 必需DMA设备或通道不可用，拒绝设备初始化。 */
        }
        data->rxdma_chan = chan;                                                      /* 保存实际分配的接收通道号。 */

    }
    printk("spi:clkreg=0x%x, dma=%d \n",config->spiclk_reg, config->flag_use_dma);    /* 输出实例保留诊断字段与DMA开关，spiclk_reg宏实际填SPI基址。 */

    /* 设备初始化阶段先打开时钟门控，之后才复位并配置外设。 */
    acts_clock_peripheral_enable(config->clock_id);                                   /* 先打开控制器时钟门控，复位与寄存器访问才能有效。 */
    /* 复位控制器，清理上电后的外设状态。 */
    acts_reset_peripheral(config->reset_id);                                          /* 执行该SPI实例的外设复位序列。 */
    spi_context_unlock_unconditionally(&data->ctx);                                   /* 强制取消片选并释放锁；初始化时用它首次释放静态0初值锁。 */
#ifdef SPI_TEST
    spi_test(dev);                                                                    /* 仅SPI_TEST编译时在初始化内执行旧自测。 */
#endif

    return 0;                                                                         /* 本路径按接口约定返回0；DMA双向旧实现即使出错也固定返回此值，详见函数头。 */
}

/* 公共 spi_transceive 通过 dev->api->transceive 落到同步包装；异步入口仅在相应构建启用。 */
const struct spi_driver_api spi_acts_driver_api = {
    .transceive = spi_acts_transceive,
#ifdef CONFIG_SPI_ASYNC
    .transceive_async = spi_acts_transceive_async,
#endif
    .release = spi_acts_release,
};

/* DMA 实例初始化片段：绑定 DMA 控制器、请求源和收发通道，并设置 DMA 标志；多行宏保持原续行语义。 */
#define  dma_use(n)  	(\
        .dma_dev_name =  CONFIG_DMA_0_NAME, \
        .txdma_id = CONFIG_SPI_##n##_DMA_ID,\
        .txdma_chan = CONFIG_SPI_##n##_TXDMA_CHAN,\
        .rxdma_id = CONFIG_SPI_##n##_DMA_ID,\
        .rxdma_chan = CONFIG_SPI_##n##_RXDMA_CHAN,\
        .flag_use_dma = 1,					\
        )

/* CPU 实例初始化片段：明确关闭 DMA，SPI2 当前选择此片段。 */
#define dma_not(n)	(\
        .flag_use_dma = 0, \
         )

#if defined(CONFIG_SOC_SERIES_LEOPARD)
/* 为编号 n 构造寄存器基址、时钟及复位资源；LEOPARD 分支另含三线配置。 */
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
/* 为编号 n 构造寄存器基址、时钟及复位资源；LEOPARD 分支另含三线配置。 */
#define SPI_ACTS_DEFINE_CONFIG(n)					\
    static const struct acts_spi_config spi_acts_config_##n = {    \
           .spi = (struct acts_spi_controller *)SPI##n##_REG_BASE,\
           .spiclk_reg = SPI##n##_REG_BASE,\
           .clock_id = CLOCK_ID_SPI##n,\
           .reset_id = RESET_ID_SPI##n,\
            COND_CODE_1(CONFIG_SPI_##n##_USE_DMA,dma_use(n), dma_not(n))\
    }
#endif

/* 生成设备静态配置、运行数据与上下文信号量，并以 POST_KERNEL 阶段注册设备及函数表；不是逐帧调用代码。 */
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


