/* SPI2 硬件循环接收。RX 跨帧重载，TX 每块只发送 512 字节，约束全双工产生的时钟数。会话从打开到关闭一直占有设备锁。 */
#ifndef SPI_ACTS_SPI2_CIRCULAR_H_    /* 保证静态会话和外部入口只定义一次。 */
#define SPI_ACTS_SPI2_CIRCULAR_H_
#if defined(CONFIG_SOC_SERIES_LEOPARD) && defined(CONFIG_PDMA_ACTS)    /* 当前实现直接使用 LEOPARD 的 PDMA 通道寄存器。 */
#define SPI2_CIRC_BYTES 512u                                           /* 每次片选传输的有效负载为 512 字节。 */
#define SPI2_CIRC_RING_BYTES (2u * SPI2_CIRC_BYTES)                    /* 接收区含两个等长槽，第二个槽末尾触发硬件重载。 */
#define SPI2_CIRC_TIMEOUT_US 100000u                                   /* 单块完成轮询的时间预算为 100 ms。 */
#define SPI2_CIRC_RELOAD_GRACE_US 20u                                  /* 仅允许环尾重载过程中短暂采到 20 µs 以内的停机状态。 */
#define SPI2_CIRC_RELOAD (1u << 18)                                    /* 通道控制字第 18 位表示重载使能。 */
static struct {
    const struct device *dev, *dma;    /* 分别保存总线所有者的 SPI 设备和实际 PDMA 设备。 */
    int rxch, txch;                    /* 会话期间独占的接收和发送物理通道号。 */
    uint32_t blocks, rx_starts;        /* 成功块数决定下个槽，接收启动次数用于检查持续搬运。 */
    bool active, failed;               /* active 表示持锁，failed 表示须关闭会话再继续。 */
} spi2_circ;

/**
  * @brief  读取物理 PDMA 通道的剩余字节计数。
  * @param  ch：[输入]已经申请的物理通道号。
  * @retval RC 寄存器低 20 位的当前计数，单位字节。
  * @note   停止标记与剩余计数独立采样，不能用 SDK 停止状态替代此回读。
  */
static uint32_t spi2_circ_rc(int ch)
{
    return sys_read32(SPI2_DMA_REG(ch, 0x1cu)) & 0xfffffu;    /* 直接读取通道 RC，独立于 SDK 对已停止通道的状态换算。 */
}

/**
  * @brief  关断 SPI 请求并检查两路 PDMA 是否已停止，然后释放会话锁。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @retval 0 表示通道均已停止，-EINVAL 为会话或设备不匹配，-ENOTSUP 为平台不支持；停止失败优先返回 RX 错误，否则返回 TX 错误。
  * @note   释放前先拉高片选。任一停止失败会保留两个通道及静态缓冲区供隔离，后续 DMA 使用须待重启。
  */
int spi_acts_spi2_circular_close(const struct device *dev)
{
    struct acts_spi_data *data;                                                        /* 用于清除运行配置与归还锁。 */
    struct acts_spi_controller *spi;                                                   /* 映射需关闭的 SPI 请求和片选控制寄存器。 */
    int r, t;                                                                          /* 两个停止结果分别保存，均尝试停机。 */
    if (!spi2_circ.active || dev != spi2_circ.dev) return -EINVAL;                     /* 仅当前持有会话的设备可关闭。 */
    data = DEV_DATA(dev);                                                              /* 找到该设备的锁和配置缓存。 */
    spi = DEV_CFG(dev)->spi;                                                           /* 定位本控制器的寄存器。 */

    /* 阻止外设产生新请求后分别停通道；不能确认停机时禁止资源复用。 */
    spi->ctrl &= ~(SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN | SPI_CTL_WR_MODE_MASK);      /* 关闭两路请求及读写模式，阻止继续产生时钟。 */
    spi->ctrl |= SPI_CTL_SS;                                                           /* 抬高硬件片选，使对端结束本帧。 */
    r = spi2_dma_stop_channel(spi2_circ.dma, spi2_circ.rxch);                          /* 确认 RX 不再向环缓冲写入。 */
    t = spi2_dma_stop_channel(spi2_circ.dma, spi2_circ.txch);                          /* 即使 RX 停止失败也尝试停 TX。 */
    if (r || t) spi2_dma_faulted = true;                                               /* 无法确认任一通道停止时保留两套资源直至重启。 */
    else {
        if (spi2_circ.rxch >= 0) dma_free(spi2_circ.dma, spi2_circ.rxch);              /* 只释放确实申请过的 RX 通道。 */
        if (spi2_circ.txch >= 0) dma_free(spi2_circ.dma, spi2_circ.txch);              /* 成功停机后把 TX 通道归还给其他外设。 */
    }

    /* 退出循环收发并归还从 open 起一直持有的锁。 */
    spi->ctrl &= ~(SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN | SPI_CTL_CLK_SEL_MASK);    /* 在通道停机处理之后关闭 FIFO 和 DMA 时钟选择。 */
    printk("SPI11 DMA close blocks=%u stop=%d/%d rx_starts=%u\n",
           spi2_circ.blocks, r, t, spi2_circ.rx_starts);                               /* 记录成功块数和 RX 启动次数，便于检查是否跨帧持续工作。 */
    data->ctx.config = NULL;                                                           /* 清除持锁期间缓存的配置地址。 */
    spi2_circ.active = false;                                                          /* 关闭标记使后续 transfer 立即拒绝该会话。 */
    k_sem_give(&data->ctx.lock);                                                       /* 归还 open 时取得的 SPI 信号量。 */
    return r ? r : t;                                                                  /* 优先向调用者报告 RX 停止错误。 */
}

/**
  * @brief  取得 SPI2 总线并启动跨帧持续工作的接收环。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @param  config：[输入]非空，主机、Mode 3、8 位、硬件片选 0，请求 100 kHz～8 MHz；实际频率还受 HOSC 分频档限制。
  * @retval 0 表示可收发。-EINVAL 为配置无效，-EBUSY 为会话已开启，-ENOTSUP 为平台、实例或存储区不符，-ENODEV 为 DMA 设备不可用，-EIO 为隔离或重载位异常。申请、时钟、配置与启动错误原样返回。
  * @note   持唤醒锁的同一线程配对调用 open、transfer 和 close。成功后跨帧持有设备锁；申请过程失败时自行回收已取得的资源。
  */
int spi_acts_spi2_circular_open(const struct device *dev, const struct spi_config *config)
{
    const struct acts_spi_config *cfg;                                                 /* 用板级配置确认实例和 DMA 所有权。 */
    struct acts_spi_data *data;                                                        /* 获取包含设备锁与配置缓存的运行数据。 */
    struct acts_spi_controller *spi;                                                   /* 使用 SPI 寄存器控制片选、FIFO 和外设请求。 */
    struct dma_config dc = {0};                                                        /* 从全零配置建立 RX 通道，默认无回调。 */
    struct dma_block_config block = {0};                                               /* 单个 DMA 块描述整块连续的 1024 字节接收区。 */
    int ret;                                                                           /* 保存各阶段返回状态，失败时原样报告。 */

    /* 先校验固定格式、板级实例与 SRAM 可达性。 */
    if (!dev || !config || config->operation !=
        (SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8)) ||
        config->cs || config->slave || config->frequency < 100000u ||
        config->frequency > 8000000u) return -EINVAL;                                  /* 仅接受指定频率范围、硬件片选和精确的八位 Mode 3 主机配置。 */
    if (spi2_circ.active) return -EBUSY;                                               /* 同一时间只能有一个循环会话。 */
    cfg = DEV_CFG(dev);                                                                /* 取得实例配置。 */
    data = DEV_DATA(dev);                                                              /* 取得设备运行上下文。 */
    spi = cfg->spi;                                                                    /* 将操作绑定到当前实例的寄存器地址。 */
    if (cfg->clock_id != CLOCK_ID_SPI2 || cfg->spi_3wire || cfg->flag_use_dma ||
        !spi2_dma_internal_ram(spi2_dma_tx, SPI2_CIRC_BYTES) ||
        !spi2_dma_internal_ram(spi2_dma_rx, SPI2_CIRC_RING_BYTES)) return -ENOTSUP;    /* 保证是四线 SPI2、无常规 DMA 占用，且整个 TX 与双槽 RX 均位于 SRAM。 */
    spi2_circ.dma = device_get_binding(CONFIG_DMA_0_NAME);                             /* 以名称取得实际物理 DMA 控制器。 */
    if (!spi2_circ.dma) return -ENODEV;                                                /* 无法绑定时尚未持锁，可直接返回。 */
    spi_context_lock(&data->ctx, false, NULL);                                         /* 取得总线所有权直至 close；避免并行走 FIFO 或有限 DMA。 */
    if (spi2_dma_faulted) { k_sem_give(&data->ctx.lock); return -EIO; }                /* 上次停机失败已隔离缓冲，此次应归还锁并报告错误。 */

    /* 建立可被 close 回收的会话状态，再进行可能失败的分配。 */
    spi2_circ.dev = dev;                                                               /* 记录所有者设备，收发和关闭时均校验。 */
    spi2_circ.active = true;                                                           /* 标记会话已建立，使后续失败也能走统一关闭逻辑。 */
    spi2_circ.failed = false;                                                          /* 新会话尚未发生传输错误。 */
    spi2_circ.blocks = 0;                                                              /* 第一帧对应接收区前半部。 */
    spi2_circ.rx_starts = 0;                                                           /* 从零统计 RX 的实际启动次数。 */
    spi2_circ.rxch = spi2_circ.txch = -1;                                              /* 两个负数表示通道都还未取得。 */
    /* 即使下一步分配失败，也须保持片选无效且无 SPI 时钟请求。 */
    spi->ctrl &= ~(SPI_CTL_RX_DRQ_EN | SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_MASK);      /* 同时屏蔽收发请求和读写，防止申请期间产生时钟。 */
    spi->ctrl |= SPI_CTL_SS;                                                           /* 保持硬件片选无效。 */
    spi2_circ.rxch = dma_request(spi2_circ.dma, 0xffu);                                /* 从公共池动态申请 RX 通道。 */
    if (spi2_circ.rxch < 0) { ret = spi2_circ.rxch; goto fail; }                       /* 保留分配失败原因并走清理。 */
    spi2_circ.txch = dma_request(spi2_circ.dma, 0xffu);                                /* 独立申请用于有限发送的 TX 通道。 */
    if (spi2_circ.txch < 0) { ret = spi2_circ.txch; goto fail; }                       /* 分配失败时回收此前取得的 RX。 */
    data->ctx.config = NULL;                                                           /* 使总线配置缓存失效，强制重新设置时钟和模式。 */
    ret = spi_acts_configure(cfg, data, config);                                       /* 应用调用者配置并核对时钟。 */
    if (ret) goto fail;                                                                /* 配置失败时关闭会话并归还锁。 */
    memset(spi2_dma_rx, 0xa5, SPI2_CIRC_RING_BYTES);                                   /* 原始缓冲填入哨兵，整个接收环准备由硬件覆盖。 */
    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK |
        SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN | SPI_CTL_RX_FIFO_EN |
        SPI_CTL_TX_FIFO_EN)) | SPI_CTL_SS | SPI_CTL_CLK_SEL_DMA;                       /* 在片选未使能时切入 DMA 路径，清空旧读写使能状态。 */
    spi->status = SPI_STATUS_ERR_MASK;                                                 /* 写一清除上次遗留的 SPI 错误。 */

    /* 在一块连续 SRAM 上配置两槽 RX 环。 */
    block.source_address = (uint32_t)&spi->rxdat;                                      /* DMA 源为固定接收数据寄存器。 */
    block.dest_address = (uint32_t)spi2_dma_rx;                                        /* DMA 目的为两个相邻的片内槽。 */
    block.block_size = SPI2_CIRC_RING_BYTES;                                           /* 一个 DMA 块同时覆盖前、后两个槽。 */
    block.dest_reload_en = 1;                                                          /* 到达环末端时让硬件重新装载目的地址和计数。 */
    dc.dma_slot = CONFIG_SPI_2_DMA_ID;                                                 /* 选用板级 SPI2 请求号。 */
    dc.channel_direction = PERIPHERAL_TO_MEMORY;                                       /* RX 的方向是外设到内存。 */
    dc.source_data_size = dc.dest_data_size = 1;                                       /* 两端以字节为元素，RC 可直接按字节解释。 */
    dc.source_burst_length = dc.dest_burst_length = 8;                                 /* 请求突发配置为 8。 */
    dc.block_count = 1;                                                                /* 用单个重载块描述环，无需两条软件链表描述符。 */
    dc.head_block = &block;                                                            /* 将栈上的块配置交给驱动同步编程。 */
    /* 不使用通道回调，中断待决寄存器仍锁存半传输和整块完成事件。 */
    ret = dma_config(spi2_circ.dma, spi2_circ.rxch, &dc);                              /* 配置 RX 通道，不传入中断回调。 */
    if (ret) goto fail;                                                                /* 配置失败时无需启动。 */
    if (!(sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 0)) & SPI2_CIRC_RELOAD)) {           /* 从硬件回读 RELOAD 位，确认能够跨块持续接收。 */
        ret = -EIO; goto fail;                                                         /* 重载未生效时禁止以循环模式运行。 */
    }

    /* 启动持续接收，但等每次 transfer 才开放有限 TX。 */
    spi->ctrl |= SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;                              /* 打开收发 FIFO，以便后续 SPI 请求得到服务。 */
    ret = dma_start(spi2_circ.dma, spi2_circ.rxch);                                    /* 整个会话只在此启动一次 RX。 */
    if (ret) goto fail;                                                                /* 启动失败时关闭已配置资源。 */
    ++spi2_circ.rx_starts;                                                             /* 记录已成功发出的 RX 启动动作。 */
    spi->ctrl |= SPI_CTL_RX_DRQ_EN;                                                    /* 放开接收请求，发送侧仍关闭，因此此刻无新 SPI 时钟。 */
    printk("SPI11 DMA open rx_reload=1 ring=1024 tx_finite=512 rxch=%d txch=%d rev=11a-wrap\n",
           spi2_circ.rxch, spi2_circ.txch);                                            /* 报告会话初始通道和缓冲布局。 */
    return 0;                                                                          /* 成功返回时仍持有 SPI 设备锁。 */
fail:
    (void)spi_acts_spi2_circular_close(dev);                                           /* 启动阶段失败时使用同一关闭逻辑回收部分建立的状态。 */
    return ret;                                                                        /* 报告导致打开失败的原始错误。 */
}

/**
  * @brief  按当前环槽交换一个固定块并核对硬件完成边界。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @param  tx：[输入]非空且可读的 512 字节，函数先复制到内部 SRAM。
  * @param  rx：[输出]非空且可写的 512 字节，仅在整块校验通过后交付。
  * @retval 0 表示交付 512 字节，-EPERM 为会话无效，-EINVAL 为空缓冲，-EIO 为硬件状态、事件或计数不符，-ETIMEDOUT 为超过 100 ms，其他负值来自 DMA。无平台支持时返回 -ENOTSUP。
  * @note   调用者先消费对端的一次 READY。每次只启动有限 TX，RX 保持重载；进入硬件故障分支后必须 close。
  */
int spi_acts_spi2_circular_transfer(const struct device *dev, const uint8_t *tx, uint8_t *rx)
{
    struct acts_spi_controller *spi;                                                             /* 保存当前会话对应的 SPI 寄存器地址。 */
    struct dma_status ts = {0};                                                                  /* 用 SDK 状态确认有限 TX 已停止。 */
    uint32_t start, status, pending, expected, mask, remaining, txrem, rxrun;                    /* 保存计时起点、事件位和两个真实剩余计数以核对同一块。 */
    uint32_t reload_start = 0;                                                                   /* 记录首次观察到环尾短暂停机的周期。 */
    bool checking_reload = false;                                                                /* 避免反复刷新宽限起点掩盖永久停机。 */
    unsigned int slot;                                                                           /* 由成功块数奇偶确定本次 RX 槽。 */
    const char *stage = "PRECHECK";                                                              /* 若前置条件未满足，诊断定位到事务开始之前。 */
    int ret = 0;                                                                                 /* 依次保存配置、启动与完成检查的结果。 */
    if (!spi2_circ.active || spi2_circ.failed || dev != spi2_circ.dev) return -EPERM;            /* 拒绝未打开、已失败或不属于此设备的会话。 */
    if (!tx || !rx) return -EINVAL;                                                              /* 两个缓冲均是必需的，长度由固定块接口约定。 */
    spi = DEV_CFG(dev)->spi;                                                                     /* 取得持锁期间使用的 SPI 实例。 */

    /* 在片选有效前记录初态，拒绝把旧事件算作新帧完成。 */
    slot = spi2_circ.blocks & 1u;                                                                /* 偶数块写前槽，奇数块写后槽。 */
    mask = (1u << spi2_circ.rxch) | (1u << (16u + spi2_circ.rxch));                              /* 从 DMAIP 中只筛选本 RX 通道的 TC 和 HF 位。 */
    expected = 1u << (spi2_circ.rxch + (slot ? 0u : 16u));                                       /* 前槽应出现半传输事件，后槽应出现完成事件。 */
    pending = sys_read32(DMA_REG_BASE) & mask;                                                   /* 开始前不应有上一槽尚未确认的事件。 */
    remaining = spi2_circ_rc(spi2_circ.rxch);                                                    /* 采样 RX 计数供前置诊断。 */
    txrem = spi2_circ_rc(spi2_circ.txch);                                                        /* 采样 TX 计数供前置诊断。 */
    status = spi->status;                                                                        /* 读片选前的 SPI 空闲和错误状态。 */
    rxrun = sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 4)) & 1u;                                    /* 读取 RX START，持续接收通道应在运行。 */
    if (pending || !(spi->ctrl & SPI_CTL_SS) ||
        !(sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 0)) & SPI2_CIRC_RELOAD) ||
        !rxrun ||
        (status & (SPI_STATUS_BUSY | SPI_STATUS_ERR_MASK)) ||
        !(status & SPI_STATUS_RX_EMPTY) || !(status & SPI_STATUS_TX_EMPTY)) {                    /* 选中前须满足无残留事件、片选高、重载有效、RX 运行且两侧 FIFO 均空闲。 */
        ret = -EIO; goto fail;                                                                   /* 前置检查失败，禁止产生下一组时钟。 */
    }

    /* 每块重设 TX 并回读无重载；全双工时钟数由 TX 计数封顶。 */
    memcpy(spi2_dma_tx, tx, SPI2_CIRC_BYTES);                                                    /* 将整块复制到静态 TX SRAM，保证有限搬运的长度与存储区。 */
    stage = "TX_CONFIG";                                                                         /* 接下来若出错，日志定位到配置有限 TX。 */
    ret = spi2_dma_config_channel(spi2_circ.dma, spi, spi2_circ.txch, true, SPI2_CIRC_BYTES);    /* 为当前块重配一次 512 字节的无重载 TX。 */
    if (ret) goto fail;                                                                          /* 配置失败时隔离本次会话。 */
    stage = "TX_MODE";                                                                           /* 单独记录发送模式回读阶段。 */
    if (sys_read32(SPI2_DMA_REG(spi2_circ.txch, 0)) & SPI2_CIRC_RELOAD) {                        /* 若误开 TX 重载，将可能产生超出块长的时钟。 */
        ret = -EIO; goto fail;                                                                   /* 在选中从机前拒绝这个配置。 */
    }
    __asm__ volatile ("dmb" ::: "memory");                                                       /* 排序内存写入和通道启动，确保 DMA 能看见发送数据。 */
    stage = "TX_START";                                                                          /* 更新诊断阶段为启动发送通道。 */
    ret = dma_start(spi2_circ.dma, spi2_circ.txch);                                              /* TX 进入待命，尚未向其开放 SPI 请求。 */
    if (ret) goto fail;                                                                          /* 启动失败时停止继续交换。 */
    spi->ctrl &= ~SPI_CTL_SS;                                                                    /* 拉低片选，整个块只选中一次。 */
    start = k_cycle_get_32();                                                                    /* 记录本块完成轮询的起始时刻。 */
    spi->ctrl |= SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_READ_WRITE;                                 /* 打开 TX 请求及双向移位，主机由此产生时钟。 */

    /* 轮询半传输或完成事件，并独立校验两个方向的进度。 */
    for (;;) {                                                                                   /* 在块期限内重复采样通道事件与线端状态。 */
        ret = dma_get_status(spi2_circ.dma, spi2_circ.txch, &ts);                                /* 读取 TX 通道运行状态。 */
        if (ret) { stage = "TX_STATUS"; break; }                                                 /* SDK 状态查询失败时保留其错误并退出轮询。 */
        pending = sys_read32(DMA_REG_BASE) & mask;                                               /* 只观察本通道两种锁存的接收事件。 */
        remaining = spi2_circ_rc(spi2_circ.rxch);                                                /* 读取 RX 剩余字节数以区分半环和重载。 */
        txrem = spi2_circ_rc(spi2_circ.txch);                                                    /* 读取真实 TX 残量以识别提前停机。 */
        /* 在读取接收事件与计数之后再采样 SPI 状态，避免把最后一次 DMA 突发前的 FIFO 非空误配给完成事件。 */
        status = spi->status;                                                                    /* 等待获取 DMA 状态后再读取 FIFO 和忙位。 */
        rxrun = sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 4)) & 1u;                                /* 采样 START，确认环是否仍在运行。 */
        if ((status & SPI_STATUS_ERR_MASK) || (pending & ~expected)) {                           /* 任一 SPI 错误或来自另一个槽的事件都意味着边界失配。 */
            stage = "RX_STATE"; ret = -EIO; break;                                               /* 将错误定位为 RX 状态异常。 */
        }
        if (!rxrun) {                                                                            /* 观察到 RX 短暂停止时判断是否正好在重载边界。 */
            uint32_t now = k_cycle_get_32();                                                     /* 同一轮用此周期值计算短暂停机的宽限。 */
            /* 仅在 TX 已耗尽且 RX 到达环尾附近时，容许短暂观察到重载前的 START 低电平。 */
            if (!checking_reload && slot && !txrem &&
                (remaining <= SPI_DMA_STRANFER_MIN_LEN ||
                 (pending == expected && remaining == SPI2_CIRC_RING_BYTES))) {                  /* 仅后槽、TX 耗尽且计数接近零或已重载时开始宽限。 */
                checking_reload = true;                                                          /* 记录已经开始宽限，后续轮询不重新计时。 */
                reload_start = now;                                                              /* 保存首次允许短暂停机的时刻。 */
            }
            if (!checking_reload ||
                k_cyc_to_us_floor32(now - reload_start) >= SPI2_CIRC_RELOAD_GRACE_US) {          /* 不符合重载条件或已超过宽限均作故障处理。 */
                stage = "RX_STATE"; ret = -EIO; break;                                           /* 禁止把永久停机误认成已交付下一块。 */
            }
        }
        if (!ts.busy && txrem) { stage = "TX_COUNT"; ret = -EIO; break; }                        /* TX 报停但真实计数非零说明少发字节。 */
        if (rxrun && !ts.busy && pending == expected &&
            remaining == (slot ? SPI2_CIRC_RING_BYTES : SPI2_CIRC_BYTES) &&
            !(status & SPI_STATUS_BUSY) && (status & SPI_STATUS_TX_EMPTY)) {                     /* 同时检查 RX 运行、TX 停、槽事件、字节数、线端空闲与 TX FIFO 空。 */
            if (!(status & SPI_STATUS_RX_EMPTY)) { stage = "RX_FIFO"; ret = -EIO; }              /* 完成点 RX FIFO 必须为空，否则存在未入环的字节。 */
            break;                                                                               /* 本轮已确定最终状态。 */
        }
        if (k_cyc_to_us_floor32(k_cycle_get_32() - start) >= SPI2_CIRC_TIMEOUT_US) {             /* 以周期差限制本块完成等待。 */
            stage = "TIMEOUT"; ret = -ETIMEDOUT; break;                                          /* 轮询期限耗尽后让调用方关闭本会话。 */
        }
    }

    /* 只有线端空闲且计数与槽相符时才确认事件并交付数据。 */
    if (ret) goto fail;                                                                          /* 硬件检查失败时保持外部接收区未提交。 */
    spi->ctrl &= ~(SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_MASK);                                    /* 只屏蔽 TX 请求和读写模式，RX 通道留给下一块。 */
    spi->ctrl |= SPI_CTL_SS;                                                                     /* 释放硬件片选，结束本块线路事务。 */
    /* 待决位写一清除，只确认本通道本次半环或整环事件，其他通道继续保留各自状态。 */
    sys_write32(expected, DMA_REG_BASE);                                                         /* 仅清除成功消费的那个锁存事件。 */
    __asm__ volatile ("dmb" ::: "memory");                                                       /* 在读取已完成槽前对内存与设备访问排序。 */
    memcpy(rx, spi2_dma_rx + slot * SPI2_CIRC_BYTES, SPI2_CIRC_BYTES);                           /* 根据首尾槽偏移，将整块复制到调用者缓冲。 */
    ++spi2_circ.blocks;                                                                          /* 仅交付成功后才改变下次预期的槽与事件。 */
    return 0;                                                                                    /* 返回后仍持锁，调用者等待下一次 READY 或关闭。 */
fail:
    spi2_circ.failed = true;                                                                     /* 标记故障使后续 transfer 不会使用这个环。 */
    spi->ctrl &= ~(SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN | SPI_CTL_WR_MODE_MASK);                /* 关闭收发请求和读写模式，等待调用者关闭通道。 */
    spi->ctrl |= SPI_CTL_SS;                                                                     /* 保持片选无效，防止异常时钟进入对端。 */
    /* 第一份快照来自故障判定，最后一份在关闭请求后重新采集。串口输出仅在失败路径执行。 */
    printk("SPI11 DMA fail rev=11a-wrap stage=%s ret=%d block=%u slot=%u\n",
           stage, ret, spi2_circ.blocks, slot);                                                  /* 记录故障阶段与已提交的块号。 */
    printk("SPI11 DMA sample sta=%08x pending=%08x expect=%08x rxrem=%u txrem=%u rxrun=%u\n",
           status, pending, expected, remaining, txrem, rxrun);                                  /* 输出构成失败判据的那一组样本。 */
    printk("SPI11 DMA halted ctrl=%08x sta=%08x rxrun=%u txrun=%u rxrc=%u txrc=%u\n",
           spi->ctrl, spi->status,
           sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 4)) & 1u,
           sys_read32(SPI2_DMA_REG(spi2_circ.txch, 4)) & 1u,
           spi2_circ_rc(spi2_circ.rxch), spi2_circ_rc(spi2_circ.txch));                          /* 再读关断请求后的硬件状态，区分判定时刻和停止后的状态。 */
    return ret;                                                                                  /* 资源仍属于会话，交由 close 完成停机。 */
}
#else    /* 未编译硬件路径时保留同名公共入口。 */
/**
  * @brief  提供未启用物理 DMA 的会话打开入口。
  * @param  d：[输入]保留的设备参数。
  * @param  c：[输入]保留的 SPI 配置参数。
  * @retval 固定返回 -ENOTSUP。
  * @note   仅在未配置 LEOPARD 或物理 PDMA 时编译，不访问外设或缓冲区。
  */
int spi_acts_spi2_circular_open(const struct device *d, const struct spi_config *c)
{ ARG_UNUSED(d); ARG_UNUSED(c); return -ENOTSUP; }
/**
  * @brief  提供未启用物理 DMA 的会话收发入口。
  * @param  d：[输入]设备。
  * @param  t：[输入]发送缓冲。
  * @param  r：[输出]接收缓冲，此分支不写入。
  * @retval 固定返回 -ENOTSUP。
  * @note   仅在未配置 LEOPARD 或物理 PDMA 时编译，不访问外设或缓冲区。
  */
int spi_acts_spi2_circular_transfer(const struct device *d, const uint8_t *t, uint8_t *r)
{ ARG_UNUSED(d); ARG_UNUSED(t); ARG_UNUSED(r); return -ENOTSUP; }
/**
  * @brief  提供未启用物理 DMA 的会话关闭入口。
  * @param  d：[输入]保留的设备参数。
  * @retval 固定返回 -ENOTSUP。
  * @note   仅在未配置 LEOPARD 或物理 PDMA 时编译，不访问外设或缓冲区。
  */
int spi_acts_spi2_circular_close(const struct device *d)
{ ARG_UNUSED(d); return -ENOTSUP; }
#endif
#endif
