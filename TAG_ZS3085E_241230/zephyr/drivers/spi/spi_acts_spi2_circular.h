/* SPI2 硬件循环接收。RX 跨帧重载，TX 每块只发送 512 字节，约束全双工产生的时钟数。会话从打开到关闭一直占有设备锁。 */
#ifndef SPI_ACTS_SPI2_CIRCULAR_H_    /* 保证静态会话和外部入口只定义一次。 */
#define SPI_ACTS_SPI2_CIRCULAR_H_
#if defined(CONFIG_SOC_SERIES_LEOPARD) && defined(CONFIG_PDMA_ACTS)    /* 当前实现直接使用 LEOPARD 的 PDMA 通道寄存器。 */
#define SPI2_CIRC_BYTES 512u                                           /* 一次硬件片选下交换 512 个字节，MOSI 和 MISO 共用时钟。文件 DATA 的有效正文最多为其中的 480 字节。 */
#define SPI2_CIRC_RING_BYTES (2u * SPI2_CIRC_BYTES)                    /* 接收区含两个等长槽，第二个槽末尾触发硬件重载。 */
#define SPI2_CIRC_TIMEOUT_US 100000u                                   /* 单块完成轮询的时间预算为 100 ms。 */
#define SPI2_CIRC_RELOAD_GRACE_US 20u                                  /* 仅允许环尾重载过程中短暂采到 20 µs 以内的停机状态。 */
#define SPI2_CIRC_RELOAD (1u << 18)                                    /* 通道控制字第 18 位表示重载使能。 */
static struct {
    const struct device *dev, *dma;    /* 分别保存总线所有者的 SPI 设备和实际 PDMA 设备。 */
    int rxch, txch;                    /* 会话期间独占的接收和发送物理通道号。 */
    uint32_t blocks, rx_starts;        /* blocks 每成功交付一帧才加一，奇偶决定 A/B；rx_starts 记录本会话接收通道的启动次数。 */
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
 * @brief          取得 SPI2 总线并启动跨帧持续工作的接收环。
 * @param  dev：   [输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
 * @param  config：[输入]非空，主机、Mode 3、8 位、硬件片选 0，请求 100 kHz～8 MHz；实际频率还受 HOSC 分频档限制。
 * @retval         0 表示可收发。-EINVAL 为配置无效，-EBUSY 为会话已开启，-ENOTSUP 为平台、实例或存储区不符，-ENODEV 为 DMA 设备不可用，-EIO 为隔离或重载位异常。申请、时钟、配置与启动错误原样返回。
 * @note           持唤醒锁的同一线程配对调用 open、transfer 和 close。成功后跨帧持有设备锁；申请过程失败时自行回收已取得的资源。
 * @note           全函数分四段：校验（阶段 1-3）、拿资源（阶段 4-9）、搭接收环（阶段 10-13）、启动（阶段 14）。失败统一在 fail 标签回收。
 */
int spi_acts_spi2_circular_open(const struct device *dev, const struct spi_config *config)
{
    const struct acts_spi_config *cfg;      /* 板级静态配置：实例号、时钟号、复位号、DMA 归属。 */
    struct acts_spi_data *data;             /* 驱动运行数据：总线锁 ctx、配置缓存、实测频率 spi2_hz。 */
    struct acts_spi_controller *spi;        /* SPI 寄存器窗口：片选、FIFO、读写请求都在这里。 */
    struct dma_config dc = {0};             /* PDMA 通道配置，全零起步：不注册回调、不启用中断。 */
    struct dma_block_config block = {0};    /* 单块描述：源、目的、块长、是否重载。 */
    int ret;                                /* 各阶段返回码，失败时原样上抛给调用者。 */

    /* 阶段 1｜参数与平台校验：确认调用者要的正是"主机 + Mode 3 + 8 位 + 硬件片选"这一种组合。 */
    /* 校验全部在碰寄存器之前完成，任一项不符直接返回，不会留下配了一半的外设。 */
    /* operation 是整位相等比较而非掩码比较：多带一个位（例如双线模式）也会被拒。 */
    if (!dev || !config || config->operation !=
        (SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8)) ||
        config->cs || config->slave || config->frequency < 100000u ||
        config->frequency > 8000000u)
    {
        return -EINVAL;
    }

    /* 阶段 2｜会话互斥：上一次 open 没有 close，硬件和通道都还挂在旧会话上，不能重入。 */
    if (spi2_circ.active)
    {
        return -EBUSY;
    }

    /* 阶段 3｜平台自检：确认这个构建和这块板确实支持"四线 SPI2 + 片内 DMA 缓冲"。 */
    /* clock_id 必须是 SPI2，防止实例串位；spi_3wire 为 0 表示四线；flag_use_dma 为 0 表示普通 SPI 通路没占 DMA。 */
    /* 两个缓冲区必须落在片内 SRAM：PDMA 只能访问 SRAM，落到外部 NOR/PSRAM 会静默失败，不报错也不搬数据。 */
    cfg  = DEV_CFG(dev);
    data = DEV_DATA(dev);
    spi  = cfg->spi;
    if (cfg->clock_id != CLOCK_ID_SPI2 || cfg->spi_3wire || cfg->flag_use_dma ||
        !spi2_dma_internal_ram(spi2_dma_tx, SPI2_CIRC_BYTES) ||
        !spi2_dma_internal_ram(spi2_dma_rx, SPI2_CIRC_RING_BYTES))
    {
        return -ENOTSUP;
    }

    /* 阶段 4｜按名字取到 PDMA 控制器。此时还没持锁，失败可以直接返回，不需要清理。 */
    spi2_circ.dma = device_get_binding(CONFIG_DMA_0_NAME);
    if (!spi2_circ.dma)
    {
        return -ENODEV;
    }

    /* 阶段 5｜拿总线锁。等价于 STM32 的 __HAL_LOCK，区别是这把锁要一直持有到 close。 */
    /* 期间别的线程既不能走 CPU FIFO 通路，也不能走普通 DMA 通路，否则两条路径抢同一个 FIFO。 */
    spi_context_lock(&data->ctx, false, NULL);

    /* spi2_dma_faulted 是上一次 close 没停干净留下的隔离标记：缓冲内容已不可信，只能等重启。 */
    if (spi2_dma_faulted)
    {
        k_sem_give(&data->ctx.lock);
        return -EIO;
    }

    /* 阶段 6｜把会话状态写成"可被 close 回收"的初值。 */
    /* active 要先置 true：这样后面任何一步失败都能走统一 close；close 按 -1 判断"通道还没申请到"，跳过停止动作。 */
    spi2_circ.dev       = dev;
    spi2_circ.active    = true;
    spi2_circ.failed    = false;
    spi2_circ.blocks    = 0;
    spi2_circ.rx_starts = 0;
    spi2_circ.rxch      = spi2_circ.txch = -1;      /* -1 表示尚未申请，close 据此不去停止一个不存在的通道。 */

    /* 阶段 7｜总线静音：把"我要数据"的请求全部掐掉，并让硬件片选保持无效。 */
    /* 只要 RX_DRQ_EN/TX_DRQ_EN 还开着，FIFO 一空就会向 PDMA 发请求、进而产生 SPI 时钟； */
    /* 后面申请通道、写 DMA 描述的这段时间里从机不能被打扰，所以必须先关请求再动机器。 */
    spi->ctrl &= ~(SPI_CTL_RX_DRQ_EN | SPI_CTL_TX_DRQ_EN | SPI_CTL_WR_MODE_MASK);
    spi->ctrl |= SPI_CTL_SS;                        /* SS 置 1：硬件片选保持无效，不选中从机。 */

    /* 阶段 8｜从 PDMA 公共池里领两个物理通道：RX 一个、TX 一个。 */
    /* 0xff 表示"随便给一个空闲的"，返回的是通道号，不是请求号；请求号 9 稍后写进通道控制字。 */
    /* 池子被占满时 dma_request 返回负值，这里不做手工归还，交给 fail 标签统一回收。 */
    spi2_circ.rxch = dma_request(spi2_circ.dma, 0xffu);
    if (spi2_circ.rxch < 0)
    {
        ret = spi2_circ.rxch;
        goto fail;
    }

    spi2_circ.txch = dma_request(spi2_circ.dma, 0xffu);
    if (spi2_circ.txch < 0)
    {
        ret = spi2_circ.txch;
        goto fail;
    }

    /* 阶段 9｜配置时钟与传输格式，并回读校验实际频率。 */
    /* 先把 ctx.config 置空是必须的：spi_acts_configure 以"配置指针是否相同"判断要不要重配， */
    /* 指针相同就直接返回，分频压根不会重算，所以这里要先作废缓存强制它走一遍。 */
    data->ctx.config = NULL;

    ret = spi_acts_configure(cfg, data, config);
    if (ret)
    {
        goto fail;
    }

    /* 阶段 10｜切到 DMA 通路，并把接收区先"弄脏"。 */
    /* 0xa5 哨兵：某段若没被硬件覆盖，读回来是 0xa5 而不是旧数据，一眼能看出漏收。 */
    /* ctrl 里清 FIFO 使能和读写模式位，再选 CLK_SEL_DMA，表示读写由 DMA 请求驱动，而不是 CPU 逐字节访问。 */
    memset(spi2_dma_rx, 0xa5, SPI2_CIRC_RING_BYTES);

    spi->ctrl = (spi->ctrl & ~(SPI_CTL_WR_MODE_MASK | SPI_CTL_DMS_MASK |
        SPI_CTL_TX_DRQ_EN | SPI_CTL_RX_DRQ_EN | SPI_CTL_RX_FIFO_EN |
        SPI_CTL_TX_FIFO_EN)) | SPI_CTL_SS | SPI_CTL_CLK_SEL_DMA;

    spi->status = SPI_STATUS_ERR_MASK;              /* 状态位写 1 清除，先清掉上次遗留的溢出/错误标志。 */

    /* 阶段 11｜描述接收环：一整块 1024 字节片内 SRAM，由硬件自己循环使用。 */
    /* 源地址固定为接收数据寄存器，等价 STM32 的 &SPI2->DR，所以控制字里要设"源地址恒定"。 */
    /* 块长 1024 = A 槽 512 + B 槽 512；写完 B 槽末尾后硬件重装地址与计数、回到 A 槽开头。 */
    /* 一个重载块就描述完整个环，不需要把两块串成链表。 */
    block.source_address   = (uint32_t)&spi->rxdat;
    block.dest_address     = (uint32_t)spi2_dma_rx;
    block.block_size       = SPI2_CIRC_RING_BYTES;
    block.dest_reload_en   = 1;                     /* 置硬件重载位，等价 STM32 的 DMA 循环模式（CIRC=1）。 */

    /* 通道侧配置。dma_slot = 9 是 SPI2 的请求号，不是通道号：本芯片没有固定映射表， */
    /* 靠控制字低 6 位告诉硬件"这条通道去服务谁"。 */
    dc.dma_slot            = CONFIG_SPI_2_DMA_ID;
    dc.channel_direction   = PERIPHERAL_TO_MEMORY;
    dc.source_data_size    = dc.dest_data_size = 1; /* 两端按字节计，RC 寄存器可直接当字节数读。 */
    dc.source_burst_length = dc.dest_burst_length = 8;
    dc.block_count         = 1;                     /* 一个块够用：靠重载位循环，而不是多块链。 */
    dc.head_block          = &block;

    /* 阶段 12｜把描述写进硬件：saddr0 / daddr0 / bc / ctl，重载请求在这里转成 CTL 的 bit18。 */
    /* 不注册回调，半传输与整块完成只锁存在中断待决寄存器里，由 transfer 主动查。 */
    ret = dma_config(spi2_circ.dma, spi2_circ.rxch, &dc);
    if (ret)
    {
        goto fail;
    }

    /* 阶段 13｜回读校验：写进去不等于生效。 */
    /* 读 RX 通道 CTL 的 bit18（SPI2_CIRC_RELOAD）。为 0 说明循环模式没建立，只有一次 1024 字节搬运， */
    /* A/B 双槽的假设整体失效，表现为跑几十块后突然超时、数据错位。宁可在开门时拒，也不带"假环"跑业务。 */
    if (!(sys_read32(SPI2_DMA_REG(spi2_circ.rxch, 0)) & SPI2_CIRC_RELOAD))
    {
        ret = -EIO;
        goto fail;
    }

    /* 阶段 14｜先开 FIFO，再启动 RX，最后才放开接收请求，三步顺序不能换。 */
    /* 通道没跑起来就开请求，第一批数据会被丢；而 RX_DRQ_EN 一开，FIFO 空就拉请求、出时钟。 */
    /* TX 请求此处仍保持关闭，等每次 transfer 时才临时开有限的一批。 */
    spi->ctrl |= SPI_CTL_RX_FIFO_EN | SPI_CTL_TX_FIFO_EN;

    ret = dma_start(spi2_circ.dma, spi2_circ.rxch);
    if (ret)
    {
        goto fail;
    }

    ++spi2_circ.rx_starts;                          /* 累计启动次数，transfer 用它确认"这一轮环确实重开过"。 */

    spi->ctrl |= SPI_CTL_RX_DRQ_EN;                 /* 只放开接收请求；从此刻起从机一有数据就会出时钟。 */

    printk("SPI11 DMA open rx_reload=1 ring=1024 tx_finite=512 rxch=%d txch=%d rev=11a-wrap\n",
           spi2_circ.rxch, spi2_circ.txch);         /* 诊断行：通道号与缓冲布局，用来和实机日志对账。 */

    return 0;                                       /* 成功返回时仍持有 SPI 设备锁，由 close 释放。 */

fail:
    /* 阶段 15｜统一回收。active 在阶段 6 已置 true，close 能处理"通道只领了一半、FIFO 只开了一半"的半成品。 */
    (void)spi_acts_spi2_circular_close(dev);
    return ret;
}

/**
  * @brief  按当前环槽交换一个固定块并核对硬件完成边界。
  * @param  dev：[输入]已绑定的 SPI2 设备对象，非空，由设备模型持有。
  * @param  tx：[输入]非空且可读的 512 字节，函数先复制到内部 SRAM。
  * @param  rx：[输出]非空且可写的 512 字节，仅在整块校验通过后交付。
  * @retval 0 表示交付 512 字节，-EPERM 为会话无效，-EINVAL 为空缓冲，-EIO 为硬件状态、事件或计数不符，-ETIMEDOUT 为超过 100 ms，其他负值来自 DMA。无平台支持时返回 -ENOTSUP。
  * @note   调用者按当前单块或配对规则取得 READY。函数同步等待有限 TX、持续重载的 RX 和 SPI 线路均达到本槽的结束条件，将 512 字节复制到 rx 后才返回成功。硬件故障后须 close。
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

        /* A 半区交付时需有半传输事件且 RX 剩 512 字节；B 半区需有整圈事件且 RX 已重载为 1024。刚打开时也可能读到 1024，所以还要核对事件、通道状态和两个 FIFO。 */
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
    ++spi2_circ.blocks;                                                                          /* 确认已把本槽的 512 字节复制到调用者后再推进槽序号；下一笔可以使用另一半接收区。 */
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
