/* SPI2 有限帧收发实现，由 spi_acts.c 在配置函数之后引入。PDMA 搬运字节，调用线程轮询通道与 SPI 线端状态。 */
#ifndef SPI_ACTS_SPI2_DMA_H_    /* 防止同一编译单元重复定义静态缓冲和入口。 */
#define SPI_ACTS_SPI2_DMA_H_

#if defined(CONFIG_SOC_SERIES_LEOPARD) && defined(CONFIG_PDMA_ACTS)    /* 仅在 LEOPARD 使用物理 DMA 驱动时具备寄存器和通道实现。 */
#define SPI2_DMA_BYTES 512u                                            /* 最大帧 512 字节，同时接受旧诊断的 256 字节帧。 */
#define SPI2_DMA_TIMEOUT_US 40000u                                     /* 从发出 SPI 请求开始计时，收发轮询上限 40 ms。 */
/* 通道寄存器从 DMA 基址后第一个 0x100 字节窗口开始，各通道占一个窗口。 */
#define SPI2_DMA_REG(ch, off) (DMA_REG_BASE + ((uint32_t)(ch) + 1u) * 0x100u + (off))    /* 用物理通道号及寄存器偏移计算 MMIO 地址。 */
static uint8_t spi2_dma_tx[SPI2_DMA_BYTES] __aligned(4);                                 /* 四字节对齐的 512 字节 PDMA 发送源。上层先装好 s_swap_tx，再由驱动复制到这里；总线锁覆盖硬件读取期间。 */
static uint8_t spi2_dma_rx[2u * SPI2_DMA_BYTES] __aligned(4);                            /* 连续 1024 字节的 PDMA 接收目的区。有限模式取首部，循环模式轮用偏移 0、512 的两个槽，完成后复制到调用者的 rx。 */
/* 若无法确认 DMA 已停止，保留通道和缓冲区，避免仍在进行的写入破坏下一请求。 */
static bool spi2_dma_faulted;     /* 停通道失败后锁存，后续请求拒绝复用缓冲。 */
static bool spi2_dma_reported;    /* 仅第一次成功输出通道与剩余计数。 */

/**
  * @brief  判断整个地址区间能否由片内 SRAM 容纳。
  * @param  p：[输入]区间起始地址，仅比较数值。
  * @param  n：[输入]区间长度，单位字节。
  * @retval true 表示区间位于 SRAM 内且末地址计算无溢出，否则为 false。
  * @note   供静态中转缓冲的 DMA 可达性检查。
  */
static bool spi2_dma_internal_ram(const void *p, size_t n)
{
    uintptr_t a = (uintptr_t)p;                                                        /* 把指针转为地址值，整个判断无需访问缓冲内容。 */
    return a >= CONFIG_SRAM_BASE_ADDRESS &&
        a + n >= a && a + n <= CONFIG_SRAM_BASE_ADDRESS + CONFIG_SRAM_SIZE * 1024u;    /* 先检查加法未回绕，再比较 SRAM 的排他上界。 */
}

/**
  * @brief  配置一次性、单块、字节宽度的 SPI2 DMA 请求。
  * @param  dma：[输入]已绑定的非空 PDMA 设备。
  * @param  spi：[输入]SPI2 寄存器块，非空。
  * @param  channel：[输入]已申请的物理通道编号。
  * @param  tx：[输入]真为内存到 TXDAT，假为 RXDAT 到内存。
  * @param  len：[输入]搬运字节数，不能超过对应静态缓冲区。
  * @retval 返回 dma_config 的状态，0 表示描述符已提交。
  * @note   描述结构位于调用栈；当前 PDMA 驱动在配置调用中写入寄存器。
  */
static int spi2_dma_config_channel(const struct device *dma,
                                  struct acts_spi_controller *spi,
                                  int channel, bool tx, size_t len)
{
    struct dma_block_config block = {0};                                            /* 清零块描述，默认关闭重载和链表扩展。 */
    struct dma_config config = {0};                                                 /* 清零通道配置，避免误开回调或其他可选功能。 */
    block.block_size = len;                                                         /* 通道只搬运本帧指定的字节数。 */
    block.source_address = tx ? (uint32_t)spi2_dma_tx : (uint32_t)&spi->rxdat;      /* TX 从静态缓冲取数据，RX 从外设接收寄存器取数据。 */
    block.dest_address = tx ? (uint32_t)&spi->txdat : (uint32_t)spi2_dma_rx;        /* TX 写发送寄存器，RX 写静态接收缓冲。 */
    config.block_count = 1;                                                         /* 有限帧只使用一个描述块。 */
    config.head_block = &block;                                                     /* 驱动配置期间从该栈对象取地址和长度。 */
    config.dma_slot = CONFIG_SPI_2_DMA_ID;                                          /* 两个方向使用板级 SPI2 请求源。 */
    config.channel_direction = tx ? MEMORY_TO_PERIPHERAL : PERIPHERAL_TO_MEMORY;    /* 方向决定哪一侧为固定外设地址。 */
    config.source_data_size = config.dest_data_size = 1;                            /* 源端和目的端均按单字节计数。 */
    config.source_burst_length = config.dest_burst_length = 8;                      /* 配置两端的突发长度为 8。 */
    /* 不注册完成回调，调用线程同时检查 START、真实剩余计数和移位器空闲。 */
    return dma_config(dma, channel, &config);                                       /* 把配置结果交给上层决定能否同时启动两路。 */
}

/**
  * @brief  请求停止通道并回读 START 位。
  * @param  dma：[输入]非空的 PDMA 设备。
  * @param  channel：[输入]物理通道编号，负数代表尚未取得通道。
  * @retval 0 表示无需停止或已停止；返回 dma_stop 错误，或回读超时的 -ETIMEDOUT。
  * @note   2 ms 计时从 dma_stop 返回后开始，不约束该驱动调用自身的等待。
  */
static int spi2_dma_stop_channel(const struct device *dma, int channel)
{
    uint32_t start;                                                    /* 保存停止回读的起始周期。 */
    int ret;                                                           /* 保存驱动停止请求的结果。 */
    if (channel < 0) return 0;                                         /* 申请前的错误清理允许传入负号哨兵。 */
    ret = dma_stop(dma, channel);                                      /* 通过设备函数表请求停止物理通道。 */
    if (ret) return ret;                                               /* 停止请求失败时保留原始错误。 */
    start = k_cycle_get_32();                                          /* 从驱动返回点开始额外回读计时。 */
    while (sys_read32(SPI2_DMA_REG(channel, 0x04u)) & 1u) {            /* START 仍置位时通道可能继续访问此前提交的地址。 */
        if (k_cyc_to_us_floor32(k_cycle_get_32() - start) >= 2000u)    /* 将无符号周期差换算为微秒。 */
            return -ETIMEDOUT;                                         /* 未能确认停止，上层须保留通道和静态缓冲。 */
    }
    return 0;                                                          /* START 已清除，可以回收通道。 */
}
#endif

/**
  * @brief  用两个有限长度的 PDMA 通道完成一帧 SPI2 全双工交换。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @param  config：[输入]非空，主机、Mode 3、8 位、硬件片选 0，请求 100 kHz～8 MHz；实际频率还受 HOSC 分频档限制。
  * @param  tx：[输入]非空，可读至少 len 字节，同步调用期间有效。
  * @param  rx：[输出]非空，可写至少 len 字节，通过全部完成检查后才复制到此处。
  * @param  len：[输入]一帧的字节数，只接受 256 或 512。
  * @retval 0 表示已交付整帧。-EINVAL 为配置或长度无效，-ENOTSUP 为平台、实例或内部 SRAM 条件不符，-ENODEV 为 DMA 设备不可用，-EIO 为硬件状态异常，-ETIMEDOUT 为完成或停止回读超时。时钟和 DMA 驱动错误原样返回。
  * @note   线程持唤醒锁调用。先取得总线锁，再布置 RX 和 TX，轮询期间允许中断；任一路无法确认停机时，两通道与静态缓冲区保留至重启。
  */
int spi_acts_spi2_transceive_dma(const struct device *dev,
                               const struct spi_config *config,
                               const uint8_t *tx, uint8_t *rx, size_t len)
{
#if defined(CONFIG_SOC_SERIES_LEOPARD) && defined(CONFIG_PDMA_ACTS)
    const struct acts_spi_config *cfg;                                                       /* 板级实例提供控制器、三线模式和时钟 ID。 */
    struct acts_spi_data *data;                                                              /* 运行数据持有总线信号量和配置缓存。 */
    struct acts_spi_controller *spi;                                                         /* 映射本 SPI 实例的寄存器块。 */
    const struct device *dma;                                                                /* 保存由名称绑定的 PDMA 设备对象。 */
    struct dma_status rs = {0}, ts = {0};                                                    /* 分别接收 RX 和 TX 通道的 SDK 状态快照。 */
    int rxch = -1, txch = -1, ret = 0, rxstop, txstop;                                       /* 通道初值为尚未分配，分别保存本次传输和两个停止操作的结果。 */
    uint32_t start, status = 0u, rxrem = (uint32_t)len, txrem = (uint32_t)len;               /* 初始残量设成帧长，在轮询中用实际寄存器覆盖。 */
    bool selected = false, configured = false;                                               /* 标志区分已经改动外设和已经拉低片选两个阶段。 */
    const uint32_t operation = SPI_OP_MODE_MASTER | SPI_MODE_CPOL |
                               SPI_MODE_CPHA | SPI_WORD_SET(8);                              /* 严格匹配主机、Mode 3、八位字和缺省位序。 */

    /* 校验硬件片选和帧格式，必须同时具备收发缓冲。 */
    if (!dev || !config || !tx || !rx || (len != 256u && len != SPI2_DMA_BYTES) ||
        config->operation != operation || config->cs || config->slave ||
        config->frequency < 100000u || config->frequency > 8000000u)
        return -EINVAL;                                                                      /* 非法组合在取得设备锁和申请通道之前退出。 */
    cfg = DEV_CFG(dev);                                                                      /* 从设备对象读取板级静态配置。 */
    data = DEV_DATA(dev);                                                                    /* 取得绑定该设备的运行上下文。 */
    spi = cfg->spi;                                                                          /* 找到用于设置 FIFO、片选和请求位的寄存器。 */
    if (cfg->clock_id != CLOCK_ID_SPI2 || cfg->spi_3wire || cfg->flag_use_dma)
        return -ENOTSUP;                                                                     /* 不接管其他控制器、三线模式或常规 DMA 驱动已占用的资源。 */
    if (!spi2_dma_internal_ram(spi2_dma_tx, len) ||
        !spi2_dma_internal_ram(spi2_dma_rx, len)) return -ENOTSUP;                           /* 两个内部缓冲的整段地址都必须落在可达 SRAM。 */
    dma = device_get_binding(CONFIG_DMA_0_NAME);                                             /* 按板级名称绑定物理 DMA 控制器。 */
    if (!dma) return -ENODEV;                                                                /* 驱动未就绪时不申请通道。 */
    spi_context_lock(&data->ctx, false, NULL);                                               /* 同步获取 SPI 设备锁，防止其他入口改动硬件或静态缓冲。 */
    if (spi2_dma_faulted) { ret = -EIO; goto release; }                                      /* 先前停机异常已隔离资源，直接退出持锁区。 */

    /* 按接收、发送顺序申请两条独立通道，使部分申请也能回收。 */
    rxch = dma_request(dma, 0xffu);                                                          /* 动态取得 RX 物理通道，0xff 为任意空闲通道。 */
    if (rxch < 0) { ret = rxch; goto release; }                                              /* 未获得 RX，无 DMA 资源需要清理。 */
    txch = dma_request(dma, 0xffu);                                                          /* 再独立取得 TX 通道以支持同时收发。 */
    if (txch < 0) { ret = txch; goto cleanup; }                                              /* TX 申请失败时仍要归还已取得的 RX。 */
    /* 仅允许 DMA 访问静态片内缓冲，调用者的地址无需满足硬件存储区或缓存维护要求。 */
    memcpy(spi2_dma_tx, tx, len);                                                            /* 将调用者的帧复制到 PDMA 可达区域。 */
    memset(spi2_dma_rx, 0xa5, len);                                                          /* 预填哨兵值，未被搬运的字节保留可辨认的初始状态。 */
    data->ctx.config = NULL;                                                                 /* 使缓存失效，避免沿用上次调用的栈上配置地址。 */
    ret = spi_acts_configure(cfg, data, config);                                             /* 应用主机模式、八位字和硬件分频。 */
    if (ret) goto cleanup;                                                                   /* 时钟或 SPI 配置失败时停止申请流程。 */
    configured = true;                                                                       /* 标记后续清理需要撤销外设设置。 */

    /* 关闭所有搬运触发，再分别编程两个通道。 */
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_CLK_SEL_MASK |
                   SPI_CTL_DMS_MASK | SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN |
                   SPI_CTL_TX_FIFO_EN | SPI_CTL_RX_FIFO_EN)) | SPI_CTL_SS;                   /* 片选保持高，将两路 FIFO、DRQ 和读写模式置于配置前状态。 */
    spi->status = SPI_STATUS_ERR_MASK;                                                       /* 对错误状态写一清除，避免旧故障被算入本帧。 */
    ret = spi2_dma_config_channel(dma, spi, rxch, false, len);                               /* RX 描述从 RXDAT 搬至 SRAM。 */
    if (ret) goto cleanup;                                                                   /* 接收端未就绪不能产生发送时钟。 */
    ret = spi2_dma_config_channel(dma, spi, txch, true, len);                                /* TX 描述从 SRAM 搬至 TXDAT。 */
    if (ret) goto cleanup;                                                                   /* 发送端配置失败，同样从公共出口回收。 */
    spi->bc = len;                                                                           /* 记录帧长；全双工的实际搬运边界由两个有限计数器约束。 */
    spi->ctrl |= SPI_CTL_CLK_SEL_DMA | SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;              /* 选择 DMA 时钟路径并打开两个硬件队列。 */
    __asm__ volatile ("dmb" ::: "memory");                                                   /* 在启动通道之前对先前内存和设备访问排序。 */
    ret = dma_start(dma, rxch);                                                              /* 先使 RX 通道待命，以免丢失第一字节。 */
    if (ret) goto cleanup;                                                                   /* RX 启动失败时尚未选中从机。 */
    ret = dma_start(dma, txch);                                                              /* 使 TX 通道待命，此时 SPI 尚未开放请求。 */
    if (ret) goto cleanup;                                                                   /* 任一启动失败均停止已配置通道。 */
    /* 两个通道均已待命，再选中对端并打开搬运请求，防止首个接收字节无人取走。 */
    spi->ctrl &= ~SPI_CTL_SS;                                                                /* 拉低硬件片选，开始完整事务。 */
    selected = true;                                                                         /* 记录退出前需要释放片选。 */
    start = k_cycle_get_32();                                                                /* 记录本帧轮询的起点。 */
    spi->ctrl |= SPI_CTL_RX_DRQ_EN | SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_READ_WRITE;         /* 同时开放收发请求及全双工时钟。 */

    /* 搬运结束后继续等线端移位完成，以免最后一位尚未送出便抬高片选。 */
    for (;;) {                                                                               /* 持续采样两个计数器及 SPI 状态，直到完成、超时或错误。 */
        ret = dma_get_status(dma, rxch, &rs);                                                /* 读取接收通道的运行标志。 */
        if (ret) break;                                                                      /* 保留 RX 查询错误并进入统一清理。 */
        ret = dma_get_status(dma, txch, &ts);                                                /* 读取发送通道的运行标志。 */
        if (ret) break;                                                                      /* 保留 TX 查询错误并进入统一清理。 */
        status = spi->status;                                                                /* 同一轮检查使用这一份 SPI 状态。 */
        if (status & SPI_STATUS_ERR_MASK) { ret = -EIO; break; }                             /* 任一 SPI 错误位使整帧作废。 */
        if (!rs.busy && !ts.busy) {                                                          /* 两路已停止是进一步核对真实残量的前提。 */
            /* SDK 在通道停止时报告剩余长度为零，提前停止也会如此；独立读取硬件 RC。 */
            rxrem = sys_read32(SPI2_DMA_REG(rxch, 0x1cu)) & 0xfffffu;                        /* 读取 RX 计数寄存器的低 20 位。 */
            txrem = sys_read32(SPI2_DMA_REG(txch, 0x1cu)) & 0xfffffu;                        /* 读取 TX 计数寄存器的低 20 位。 */
            if (rxrem || txrem) { ret = -EIO; break; }                                       /* 即使通道已停止，计数非零也表示帧不完整。 */
            if ((status & SPI_STATUS_TX_EMPTY) && !(status & SPI_STATUS_BUSY)) {             /* TX FIFO 空且移位器不忙才允许释放片选。 */
                if (!(status & SPI_STATUS_RX_EMPTY)) ret = -EIO;                             /* 此时 RX FIFO 残留表示硬件与预期字节边界不一致。 */
                break;                                                                       /* 完成条件或末端 FIFO 错误已确定。 */
            }
        }
        if (k_cyc_to_us_floor32(k_cycle_get_32() - start) >= SPI2_DMA_TIMEOUT_US) {          /* 以周期差换算微秒，限制整帧轮询时间。 */
            ret = -ETIMEDOUT;                                                                /* 将期限耗尽与外设报告错误区别返回。 */
            break;                                                                           /* 退出轮询，先清理再交付结果。 */
        }
    }
cleanup:
    /* 先关外设请求和片选，再停通道。尚未启用的资源也能通过同一出口清理。 */
    if (configured) spi->ctrl &= ~(SPI_CTL_RX_DRQ_EN | SPI_CTL_TX_DRQ_EN);                   /* 若外设已配置，先阻止两个方向产生新请求。 */
    if (selected) spi->ctrl |= SPI_CTL_SS;                                                   /* 只有选中过对端才需要拉高硬件片选。 */
    rxstop = spi2_dma_stop_channel(dma, rxch);                                               /* 检查 RX 是否已经停止写入内存。 */
    txstop = spi2_dma_stop_channel(dma, txch);                                               /* 检查 TX 是否已经停止读取内存。 */
    __asm__ volatile ("dmb" ::: "memory");                                                   /* 对停止操作和后续缓冲访问排序。 */
    if (rxstop || txstop) {                                                                  /* 任一方向停止失败，都无法安全复用这对资源。 */
        spi2_dma_faulted = true;                                                             /* 锁存隔离状态，两个通道保持预留。 */
        if (!ret) ret = rxstop ? rxstop : txstop;                                            /* 已有传输错误优先；否则优先报告 RX 停止错误。 */
    } else {
        if (rxch >= 0) dma_free(dma, rxch);                                                  /* 只归还已成功申请的 RX 通道。 */
        if (txch >= 0) dma_free(dma, txch);                                                  /* 只归还已成功申请的 TX 通道。 */
    }
    if (configured) {                                                                        /* 只有配置过该控制器才恢复其空闲状态。 */
        spi->ctrl &= ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_CLK_SEL_MASK |
                       SPI_CTL_TX_FIFO_EN | SPI_CTL_RX_FIFO_EN);                             /* 关闭读写、DMA 时钟路径及 FIFO。 */
        if (ret) {                                                                           /* 失败帧可能留下外设内部状态，需复位。 */
            acts_reset_peripheral(cfg->reset_id);                                            /* 通过该设备的板级编号复位 SPI。 */
            spi->ctrl = SPI_CTL_SS | SPI_CTL_MODE_CPOL | SPI_CTL_MODE_CPHA;                  /* 片选释放，恢复 Mode 3 空闲电平。 */
        }
    }

    /* 成功后才交付接收缓冲，失败帧只输出诊断。 */
    if (!ret) {                                                                              /* 所有传输和停止检查均通过才向外复制数据。 */
        memcpy(rx, spi2_dma_rx, len);                                                        /* 将完整帧复制到调用者接收区。 */
        if (!spi2_dma_reported) {                                                            /* 首次成功时记录验证快照。 */
            printk("SPI2 DMA FIRST_OK bytes=%u rx_ch=%d tx_ch=%d rx_rem=%u tx_rem=%u sta=%08x\n",
                   (unsigned int)len, rxch, txch, rxrem, txrem, status);                     /* 输出完成时的状态与两个实际剩余计数。 */
            spi2_dma_reported = true;                                                        /* 后续成功帧省去串口输出。 */
        }
    }
    if (ret) printk("SPI2 DMA fail ret=%d rxch=%d txch=%d sta=%08x rx_rem=%u tx_rem=%u stop=%d/%d\n",
                    ret, rxch, txch, status, rxrem, txrem, rxstop, txstop);                  /* 故障日志同时保留传输错误、残量与停通道结果。 */
release:
    data->ctx.config = NULL;                                                                 /* 清掉调用者配置地址，避免后续引用已过期的对象。 */
    /* 此入口拒绝持锁与异步标志。清掉配置指针后直接归还信号量，避免通用从机释放逻辑访问空配置。 */
    k_sem_give(&data->ctx.lock);                                                             /* 结束本次对 SPI 和静态缓冲的独占。 */
    return ret;                                                                              /* 同步返回本帧与清理的最终状态。 */
#else
    ARG_UNUSED(dev); ARG_UNUSED(config); ARG_UNUSED(tx); ARG_UNUSED(rx); ARG_UNUSED(len);    /* 未编译物理 DMA 时，接口仍保留完整签名。 */
    return -ENOTSUP;                                                                         /* 此构建无法执行专用 DMA 交换。 */
#endif
}
#endif
