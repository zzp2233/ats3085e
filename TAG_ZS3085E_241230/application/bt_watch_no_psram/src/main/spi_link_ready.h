/* UMA GPIO50 接 ATS GPIO61（1.8 V），以保持的电平传递授权，双边沿只负责唤醒任务。普通帧每次消费后等待相反电平；文件配对模式按每对消费一次。期望值跨命令保留，启用启动同步的固件可在新信标及严格握手确认后恢复。 */
#ifndef SPI_LINK_READY_H_
#define SPI_LINK_READY_H_

#define SWAP_READY_PIN 61u            /* GPIO61 属 GPIOB，驱动接口使用它在 32 位端口内的编号。 */
#define SWAP_READY_TIMEOUT_MS 200u    /* 普通压测帧的缺省 READY 等待时限，单位毫秒。 */
/* 板级可选外设也可占用这个焊盘，出现配置冲突时在编译阶段阻止共享。 */
#if IS_ENABLED(CONFIG_I2C_2) || IS_ENABLED(CONFIG_SPIMT_0) || IS_ENABLED(CONFIG_MMC_0)
#error GPIO61 must be reserved for SPI READY; conflicting board peripheral enabled
#endif

static const struct device *s_ready_gpio;                                              /* GPIOB 的已注册设备实例，用于操作引脚和回调。 */
static uint32_t s_ready_saved_ctl;                                                     /* 申请 READY 前保存的引脚控制寄存器。 */
static uint8_t s_ready_expected = 1u;                                                  /* 首次普通授权等高电平，此值随后按每次消费翻转。 */
static bool s_ready_used, s_ready_legacy_used, s_ready_faulted, s_ready_configured;    /* 分别记录 READY 使用史、旧路径使用史、锁存故障和本次已配置状态。 */
static bool s_ready_cleanup_failed;                                                    /* 清理失败单独保留，不能靠发现新启动信标解除。 */
static struct gpio_callback s_ready_callback;                                          /* 持久回调对象，登记期间须保持地址有效。 */
static struct k_sem s_ready_sem;                                                       /* 最大计数为一的信号量，通知边沿而非累计发送许可。 */
static bool s_ready_callback_added;                                                    /* 记录回调是否已挂入驱动，供错误分支回收。 */
static volatile uint32_t s_ready_irq_count;                                            /* 中断递增的序号，线程用它识别合并或竞争的边沿。 */
static uint32_t s_ready_block_count;                                                   /* 本次命令等待线程实际调用阻塞操作的次数。 */
static uint64_t s_ready_block_us;                                                      /* 累计阻塞微秒数，不含立即满足的电平。 */
static volatile uint32_t s_ready_irq_cycles;                                           /* 最近一次有效引脚中断的硬件周期计数。 */
static struct {
    uint64_t pre_us, post_us;                         /* 从准备阻塞到中断、从中断到线程恢复的可配对累计微秒数。 */
    uint32_t n, pre_max, post_max, held, unpaired;    /* 样本数、两段最大值、无需阻塞次数及不能匹配单边沿次数。 */
} s_ready_diag;

/**
  * @brief  在 GPIO 双边沿中断记录时刻并唤醒等待者。
  * @param  port：[输入]GPIO 驱动传入的设备，可能是共享中断实例，本回调忽略。
  * @param  cb：[输入]已登记的回调，本函数不解引用。
  * @param  pins：[输入]本次中断的端口位掩码。
  * @retval 无。
  * @note   中断不读取 SPI/DMA；任务恢复后须重新采样引脚电平确认授权。
  */
static void swap_ready_irq(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    /* SDK 可能传入共享中断设备，因此按已注册的 GPIOB 位掩码过滤。中断中只记时并通知线程。 */
    (void)port; (void)cb;                           /* 回调身份与共享端口设备由外层提供，本函数只用 pins 位图。 */
    if (pins & (1u << (SWAP_READY_PIN % 32u))) {    /* 共享端口可能同时上报其他引脚，仅处理目标位。 */
        s_ready_irq_cycles = k_cycle_get_32();      /* 先保存边沿时刻，再发布序号。 */
        __asm__ volatile ("dmb" ::: "memory");      /* DMB 对内存访问排序；volatile 变量本身不提供这种顺序保证。 */
        ++s_ready_irq_count;                        /* 任务前后两次读取可判断是否只有一次新中断。 */
        k_sem_give(&s_ready_sem);                   /* 唤醒等待线程，真实发送权限仍由 GPIO 电平决定。 */
    }
}

/**
  * @brief  关闭 READY 中断、注销回调并恢复此前保存的引脚控制值。
  * @param  无。
  * @retval 0 已清理，负数为底层首次清理错误。
  * @note   清理失败同时锁存链路故障和清理失败标志，后续启动探测也受限。
  */
static int swap_ready_restore(void)
{
    int ret = 0, r;                                                                                  /* 返回首个故障，r 临时接收注销操作的结果。 */

    /* 停止产生新的边沿通知，注销已安装的回调，然后恢复调用前保存的引脚控制字。 */
    if (s_ready_configured)
        ret = gpio_pin_interrupt_configure(s_ready_gpio, SWAP_READY_PIN % 32u, GPIO_INT_DISABLE);    /* 先屏蔽边沿，减少卸载回调时的新通知。 */
    if (s_ready_callback_added) {                                                                    /* 只有实际挂上回调才需要将它移除。 */
        r = gpio_remove_callback(s_ready_gpio, &s_ready_callback);                                   /* 只注销确实已登记的对象。 */
        if (!ret) ret = r;                                                                           /* 保留最早失败的步骤用于诊断。 */
        if (!r) s_ready_callback_added = false;                                                      /* 注销确认成功才清除回调挂接标志。 */
    }
    if (s_ready_configured) sys_write32(s_ready_saved_ctl, GPION_CTL(SWAP_READY_PIN));               /* 将引脚复用、输入等控制位恢复为进入前的值。 */
    s_ready_configured = false;                                                                      /* 本次输入配置生命周期已结束。 */
    if (ret) s_ready_faulted = s_ready_cleanup_failed = true;                                        /* 出错后禁止后续流程在不确定的硬件状态下发时钟。 */
    return ret;                                                                                      /* 向调用端报告禁用中断或注销时的首个故障。 */
}

/**
  * @brief  申请 GPIO61 并建立电平采样及双边沿通知。
  * @param  boot_probe：[输入]true 允许在普通链路故障后被动探测启动信标。
  * @retval 0 成功，-EPERM 为状态禁止，-ENODEV 无设备，-EBUSY 已占用，-EIO 回读不符，其余为 GPIO 驱动错误。
  * @note   使用 GPIOB 的端口内编号。采样硬件配置之前保存原控制字，失败或结束时恢复。
  */
static int swap_ready_begin_internal(bool boot_probe)
{
    int ret;                                                                                                /* 接收设备配置、挂接回调或开启中断的错误。 */

    /* 先排除上一会话留下的故障和复用冲突，再申请实际的 GPIOB 设备及输入通路。 */
    if ((!boot_probe && s_ready_faulted) || s_ready_legacy_used || s_ready_cleanup_failed) return -EPERM;
    s_ready_gpio = device_get_binding(CONFIG_GPIO_B_NAME);                                                  /* GPIO61 需要通过 GPIOB 实例和端口内位号访问。 */
    if (!s_ready_gpio) return -ENODEV;                                                                      /* 找不到已登记设备时不能继续采样。 */
    s_ready_saved_ctl = sys_read32(GPION_CTL(SWAP_READY_PIN));                                              /* 在改动引脚前保存其完整控制字。 */
    if (s_ready_saved_ctl & (GPIO_CTL_MFP_MASK | GPIO_CTL_GPIO_OUTEN | GPIO_CTL_INTC_EN)) return -EBUSY;    /* 已复用、正在输出或已有中断的引脚不能用于 READY。 */
    ret = gpio_pin_configure(s_ready_gpio, SWAP_READY_PIN % 32u, GPIO_INPUT | GPIO_PULL_DOWN);              /* 使用下拉输入，对端未驱动时保持低电平。 */
    if (ret) {                                                                                              /* 驱动拒绝引脚配置时先处理已取得的原寄存器。 */
        sys_write32(s_ready_saved_ctl, GPION_CTL(SWAP_READY_PIN));                                          /* 回写操作前保存的原始引脚选择和电气设置。 */
        return ret;                                                                                         /* 返回驱动原错误以区别于后面的回读不符。 */
    }
    s_ready_configured = true;                                                                              /* 标记引脚控制已被本次会话接管。 */
    if ((sys_read32(GPION_CTL(SWAP_READY_PIN)) &
         (GPIO_CTL_MFP_MASK | GPIO_CTL_GPIO_OUTEN | GPIO_CTL_GPIO_INEN)) != GPIO_CTL_GPIO_INEN) {           /* 确认复用为 GPIO、输出关闭且输入通路已打开。 */
        sys_write32(s_ready_saved_ctl, GPION_CTL(SWAP_READY_PIN));                                          /* 回读与要求不符时还原原控制字。 */
        s_ready_configured = false;                                                                         /* 完成恢复后不再把此引脚视为已配置。 */
        return -EIO;                                                                                        /* 即使配置函数成功，寄存器不符也视为硬件 I/O 异常。 */
    }

    /* 将通知计数与统计窗口清零；随后先挂接回调再开启双边沿，使首个中断有接收者。 */
    k_sem_init(&s_ready_sem, 0, 1);
    s_ready_irq_count = s_ready_block_count = 0;                                                            /* 新命令重新统计中断次数和线程阻塞次数。 */
    s_ready_block_us = 0;                                                                                   /* 重置本次命令的累计阻塞微秒数。 */
    s_ready_irq_cycles = 0;                                                                                 /* 在任何新边沿发生之前清零历史周期时间戳。 */
    s_ready_diag.pre_us = s_ready_diag.post_us = 0;                                                         /* 两段调度时间从本次命令开始累计。 */
    s_ready_diag.n = s_ready_diag.pre_max = s_ready_diag.post_max = 0;                                      /* 配对样本数和两段最慢时延都按命令重新统计。 */
    s_ready_diag.held = s_ready_diag.unpaired = 0;                                                          /* 分开记录无需阻塞和无法匹配中断的样本。 */
    gpio_init_callback(&s_ready_callback, swap_ready_irq, 1u << (SWAP_READY_PIN % 32u));                    /* 用端口内掩码登记本静态回调对象。 */
    ret = gpio_add_callback(s_ready_gpio, &s_ready_callback);                                               /* 将静态回调对象挂到 GPIOB 驱动。 */
    if (!ret) {
        s_ready_callback_added = true;                                                                      /* 回调已挂接，后续若中断初始化失败必须注销。 */
        ret = gpio_pin_interrupt_configure(s_ready_gpio, SWAP_READY_PIN % 32u, GPIO_INT_EDGE_BOTH);         /* 上升和下降沿都唤醒线程，以支持交替电平授权。 */
    }
    if (ret) { (void)swap_ready_restore(); return ret; }                                                    /* 回滚部分成功的 GPIO 步骤，返回最初的配置错误。 */
    s_ready_used = true;                                                                                    /* 保留使用史，阻止后来直接混入未跟踪授权的旧模式。 */
    return 0;                                                                                               /* 引脚输入、回调和中断配置均成功。 */
}

/**
  * @brief  为普通传输开启 READY 输入及双边沿中断。
  * @param  无。
  * @retval 沿用内部初始化结果，0 成功，负数为状态、设备或硬件配置错误。
  * @note   普通事务须通过链路故障锁存检查。
  */
static int swap_ready_begin(void)
{
    return swap_ready_begin_internal(false);    /* 普通业务不得绕过已锁存的 READY 故障。 */
}

/* 仅与实现启动信标及握手的对端固件配套时启用。 */
#ifdef SPI_LINK_BOOT_SYNC
/* 在无 SPI 时钟时静默会话最多留下一个最后授权，重复的约 40 ms 半周期用于发现启动信标。只有后续 CRC 握手成功才能确认新会话。 */
/**
  * @brief  无 SPI 时钟地观察 GPIO61，识别从机启动信标。
  * @param  无。
  * @retval 1 允许尝试握手，0 未看到足够边沿，负值为 GPIO 错误或异常波形。
  * @note   320 ms 内观察至少四个边沿、后续间距 25～65 ms 且末电平高才放行，身份由后续 CRC 握手验证。
  */
static int swap_ready_boot_probe(void)
{
    uint32_t start = k_uptime_get_32(), edge = start;                       /* 记录总体观察起点及上一边沿时刻，单位毫秒。 */
    unsigned int edges = 0;                                                 /* 开始观测时还没有看到电平翻转。 */
    int previous = gpio_pin_get_raw(s_ready_gpio, SWAP_READY_PIN % 32u);    /* 先读当前电平，第一次观测的宽度可能不完整。 */
    if (previous < 0) return previous;                                      /* GPIO 初次采集失败即中止被动观察。 */

    /* 在观察窗口内每毫秒读取电平，按相邻真实边沿的宽度判断启动信标。 */
    while ((uint32_t)(k_uptime_get_32() - start) < 320u) {
        uint32_t now;                                                       /* 当前采样的单调毫秒计数。 */
        int level;                                                          /* 当前引脚返回的原始 0/1 电平或驱动错误。 */
        k_msleep(1);                                                        /* 每毫秒采样，给其他任务留出调度机会。 */
        now = k_uptime_get_32();                                            /* 用真正的唤醒时刻测量，不假定 k_msleep 恰好 1 ms。 */
        level = gpio_pin_get_raw(s_ready_gpio, SWAP_READY_PIN % 32u);       /* 再次读取 GPIO 实际电平。 */
        if (level < 0) return level;                                        /* 驱动读取故障需要与无启动信标区分。 */
        if (level != previous) {                                            /* 仅电平相对上一样本变化才形成边沿。 */
            uint32_t width = now - edge;                                    /* 计算两次识别到边沿之间的毫秒数。 */
            if (edges && (width < 25u || width > 65u)) return -EIO;         /* 首段起点不确定，从第二个边沿起检查半周期范围。 */
            ++edges; edge = now; previous = level;                          /* 同时更新边沿数、上一边沿时刻与上一电平。 */
            if (edges >= 4u && level == 1) return 1;                        /* 最终高电平留给下一次启动握手尝试。 */
        }
    }
    return edges > 1u ? -EIO : 0;                                           /* 多个边沿但不成合法信标视为坏波形，至多一个边沿可属静默业务。 */
}
#endif

/* 最后一块发送完也可调用：观察从机重新装载后的电平，但保留本次授权供下一条命令使用。 */
/**
  * @brief  在给定总时限内等待期望 READY 电平，记录阻塞及调度耗时。
  * @param  wait_us：[输出]非空地址，返回本次等待微秒数。
  * @param  timeout_ms：[输入]从本次等待开始计算的总超时，单位毫秒。
  * @retval 0 取得当前授权，-EPERM 未配置或链路已锁，-ETIMEDOUT 超时，其余负值来自 GPIO。
  * @note   信号量只提示电平可能变化。函数不消耗授权，帧传输前由调用方另行翻转期望值。
  */
static int swap_ready_wait_timeout(uint32_t *wait_us, uint32_t timeout_ms)
{
    uint32_t start_ms = k_uptime_get_32(), start_cycles = k_cycle_get_32();                         /* 毫秒计数判断时限，硬件周期测量更短的等待间隔。 */
    uint32_t paired = 0, pair_gen = 0, pre = 0, post = 0, blocks = 0;                               /* 为一次等待保存调度样本标志、序号、前后时延和阻塞次数。 */
    int ret;                                                                                        /* 记录 GPIO 结果或本次等待故障码。 */
    *wait_us = 0;                                                                                   /* 即使状态检查立即失败，也将输出等待时间置零。 */
    if (!s_ready_configured || s_ready_faulted) return -EPERM;                                      /* 没有可用 READY 会话时不得允许发送。 */
    for (;;) {                                                                                      /* 直到电平匹配、GPIO 错误或期限耗尽。 */
        uint32_t elapsed = k_uptime_get_32() - start_ms, block_cycles, block_gen;                   /* 保存已耗毫秒、阻塞前周期数和中断序号。 */
        uint32_t resume_cycles, resume_gen, irq_cycles;                                             /* 唤醒后记录周期和中断序号，也保存 ISR 发布的时刻。 */
        int take_ret;                                                                               /* 内核等待通知的结果，用于筛选有效时延样本。 */

        /* 先以当前实际电平判定授权；只有尚未到时且电平仍不匹配才阻塞。 */
        if (elapsed > timeout_ms) { ret = -ETIMEDOUT; break; }
        ret = gpio_pin_get_raw(s_ready_gpio, SWAP_READY_PIN % 32u);                                 /* 以实际电平为准，防止将过期中断当成发送许可。 */
        if (ret < 0) break;                                                                         /* GPIO 读取错误原样向上返回。 */
        if ((unsigned int)ret == s_ready_expected) {                                                /* 期望电平成立才取得本次传输许可。 */
            /* 阻塞期间恰好出现一次新中断才拆分调度时延。电平原已满足、边沿合并或旧通知仍按相同电平规则判定授权。 */
            if (paired && pair_gen == s_ready_irq_count) {                                          /* 样本需与当前最新中断序号一致。 */
                ++s_ready_diag.n;                                                                   /* 新增可信的单边沿调度样本数。 */
                s_ready_diag.pre_us += pre;                                                         /* 累加开始阻塞至中断发生的时间。 */
                s_ready_diag.post_us += post;                                                       /* 累加中断发生至线程重新运行的时间。 */
                if (pre > s_ready_diag.pre_max) s_ready_diag.pre_max = pre;                         /* 更新前半段最大硬件等待时延。 */
                if (post > s_ready_diag.post_max) s_ready_diag.post_max = post;                     /* 更新后半段最大线程恢复时延。 */
            } else if (!blocks) ++s_ready_diag.held;                                                /* 电平首次读取即满足，无需调度样本。 */
            else ++s_ready_diag.unpaired;                                                           /* 阻塞过却无法唯一对应中断，单列计数。 */
            ret = 0;                                                                                /* 电平已经匹配，给调用方成功状态。 */
            break;                                                                                  /* 离开等待环，统一输出实际微秒数。 */
        }
        paired = 0;                                                                                 /* 电平不满足时丢弃上一次循环暂存的配对结果。 */
        elapsed = k_uptime_get_32() - start_ms;                                                     /* 阻塞前重新计时，传给内核的是总时限的剩余部分。 */
        if (elapsed >= timeout_ms) {                                                                /* 实际电平未达时，到期限边界就停止阻塞。 */
            ret = -ETIMEDOUT;                                                                       /* 标记没有在规定时间内看到目标电平。 */
            break;                                                                                  /* 交给统一收尾记录耗时和故障状态。 */
        }
        /* 读电平与取信号量之间不重置信号量，保留窗口中新来的中断。虚假或过期通知只触发复读，不延长总时限。 */
        block_cycles = k_cycle_get_32();                                                            /* 在可能睡眠前快照处理器周期。 */
        block_gen = s_ready_irq_count;                                                              /* 记录起始中断序号，供醒来后判断是否跨过单个边沿。 */
        ++s_ready_block_count;                                                                      /* 所有调用累计一次实际阻塞尝试。 */
        ++blocks;                                                                                   /* 本轮等待另外记录是否曾阻塞。 */
        take_ret = k_sem_take(&s_ready_sem, K_MSEC(timeout_ms - elapsed));                          /* 中断恰在读电平和本次等待之间到达时，已保存的通知仍可领取。 */
        resume_cycles = k_cycle_get_32();                                                           /* 从内核等待返回即刻记录周期数。 */
        resume_gen = s_ready_irq_count;                                                             /* 同时取当前中断序号，供接下来的两次稳定性检查。 */
        __asm__ volatile ("dmb" ::: "memory");                                                      /* 约束中断序号与对应时间戳的读取次序。 */
        irq_cycles = s_ready_irq_cycles;                                                            /* 取得中断回调记录的最近边沿时刻。 */
        __asm__ volatile ("dmb" ::: "memory");                                                      /* 读取时间戳后再检查序号，剔除采样中的新中断。 */

        /* 检查阻塞前后恰好发生一个中断，且中断周期位于这段时间内，再区分硬件等待和调度延迟。 */
        if (!take_ret && resume_gen == s_ready_irq_count &&
            resume_gen - block_gen == 1u &&
            (uint32_t)(irq_cycles - block_cycles) <= (uint32_t)(resume_cycles - block_cycles)) {    /* 只有中断时刻落在阻塞区间内，才计算两段调度时间。 */
            paired = 1;                                                                             /* 这个样本暂时可用于配对，还须下轮读电平时再次验证。 */
            pair_gen = resume_gen;                                                                  /* 保存配对样本对应的中断代数。 */
            pre = k_cyc_to_us_floor32(irq_cycles - block_cycles);                                   /* 将阻塞开始到中断出现的硬件周期折成微秒。 */
            post = k_cyc_to_us_floor32(resume_cycles - irq_cycles);                                 /* 将中断到任务重新运行的硬件周期折成微秒。 */
        }
        s_ready_block_us += k_cyc_to_us_floor32(resume_cycles - block_cycles);                      /* 即使无法配对单次中断，整个阻塞时间仍可累加。 */
    }
    *wait_us = k_cyc_to_us_floor32(k_cycle_get_32() - start_cycles);                                /* 返回覆盖全部重检和阻塞的实际等待时间。 */
    if (ret) s_ready_faulted = true;                                                                /* GPIO 或超时错误使普通流量停止，防止猜测令牌极性。 */
    return ret;                                                                                     /* 返回获得电平许可、超时或底层错误。 */
}

/**
  * @brief  按普通帧的缺省时限等待期望电平。
  * @param  wait_us：[输出]非空地址，接收等待微秒数。
  * @retval 0 就绪，负值为状态、GPIO 或超时错误。
  * @note   常规压测默认 200 ms，文件控制可另指定操作时限。
  */
static int swap_ready_wait(uint32_t *wait_us)
{
    return swap_ready_wait_timeout(wait_us, SWAP_READY_TIMEOUT_MS);    /* 使用普通压测的缺省 READY 时限。 */
}

/**
  * @brief  消费本次电平授权并期待下一次相反电平。
  * @param  无。
  * @retval 无。
  * @note   在发时钟前调用，期望电平跨 Shell 命令保留。
  */
static void swap_ready_consume(void)
{
    s_ready_expected ^= 1u;    /* 在发时钟前切换下一次期望电平。 */
}

/**
  * @brief  锁存事务失败并释放 GPIO 及中断资源。
  * @param  result：[输入]调用方要交给 READY 层的事务状态，0 表示正常结束。
  * @retval 优先返回非零事务结果，否则返回清理结果。
  * @note   运行期和启动期恢复还需文件协议握手验证。
  */
static int swap_ready_end(int result)
{
    int ret;                               /* 保存 GPIO 清理操作返回值。 */
    if (result) s_ready_faulted = true;    /* 调用方报告失败时阻止普通传输继续。 */
    ret = swap_ready_restore();            /* 统一禁用中断、移除回调并恢复引脚。 */
    return result ? result : ret;          /* 保留原事务故障；事务正常时仍须上报资源清理失败。 */
}
#endif
