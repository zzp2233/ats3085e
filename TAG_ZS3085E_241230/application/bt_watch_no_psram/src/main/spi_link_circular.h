/* V11 环形 DMA 主机压测，每次 READY 授权交换一个 512 字节块。从机双槽轮换使 ACK 延后两块，开头两次读出的是流水历史。 */
#ifndef ATS_SPI_LINK_CIRCULAR_H_
#define ATS_SPI_LINK_CIRCULAR_H_
#define CIRC_MASTER 0xc3u        /* V11 主机测试块的方向标识，避免接收方误用文件协议。 */
#define CIRC_SLAVE 0x3cu         /* 期待从机在两个逻辑槽中返回此方向。 */
#define CIRC_ACK_DELAY 2u        /* 两个环形槽提前预装的回复形成两块流水延迟。 */
static bool s_swap_file_used;    /* 一旦使用文件会话便锁定模式，防止压测改变已同步的槽位。 */

/**
  * @brief  执行 V11 双槽环形 DMA 主机回环压测。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]可选目标块数 1～10000、间隔 0～1000 ms 和请求频率 100000～8000000 Hz。
  * @retval 0 为全部目标块通过，负值表示参数、模式、READY、驱动或确认失败。
  * @note   默认请求 4 MHz，一次 READY 授予一个 512 字节块；补发两块取回流水尾部确认。
  */
static int shell_spi2circ(const struct shell *sh, size_t argc, char *argv[])
{
    static uint16_t sid;                                                                                 /* 跨 Shell 命令递增，检测旧会话残留。 */
    struct spi_config config = {
        .frequency = 4000000u,
        .operation = SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8),               /* 主机产生时钟，CPOL 与 CPHA 同时置位对应模式 3，每字八位。 */
    };
    const struct device *dev;                                                                            /* 保存已注册的 SPI2 主机设备。 */
    unsigned long count = 1, gap = 0, hz = 4000000u;                                                     /* 默认单块、无人工间隔、4 MHz。 */
    unsigned int i, good = 0, attempts = 0;                                                              /* good 只统计已确认的数据块，attempts 包含填充和排空。 */
    uint32_t start, elapsed, waited;                                                                     /* 记录本次开始与经过的毫秒数，以及每次 READY 等待的微秒数。 */
    uint64_t wait_total = 0;                                                                             /* 所有块的 READY 等待使用 64 位总数避免累计溢出。 */
    char *end;                                                                                           /* 用转换终止位置识别参数中非十进制的尾随内容。 */
    int ret, close_ret;                                                                                  /* 分开保存传输结果和驱动关闭结果。 */

    /* 先校验循环次数、块间间隔与请求时钟，参数合法后才访问设备。 */
    if (argc > 4) return -EINVAL;
    if (argc > 1) {
        count = strtoul(argv[1], &end, 10);                                                              /* 把首个可选参数按十进制解释为目标块数。 */
        if (!argv[1][0] || *end || count < 1 || count > 10000) return -EINVAL;                           /* 限制诊断时长，拒绝空串和未完整解析的数字。 */
    }
    if (argc > 2) {
        gap = strtoul(argv[2], &end, 10);                                                                /* 第二个可选参数设置相邻物理块之间的毫秒停顿。 */
        if (!argv[2][0] || *end || gap > 1000) return -EINVAL;                                           /* 上限为一秒，不接受空值或尾随字符。 */
    }
    if (argc > 3) {
        hz = strtoul(argv[3], &end, 10);                                                                 /* 最后一项可覆盖默认 SPI 时钟。 */
        if (!argv[3][0] || *end || hz < 100000 || hz > 8000000) return -EINVAL;                          /* 命令只接收请求上限范围内的十进制 Hz。 */
    }

    /* 确认本次使用 V11 单块授权流水，取得设备和唤醒锁后申请 READY。 */
    if (s_swap_file_used || s_ready_legacy_used || (s_ready_used && !s_swap_circular_used)) return -EPERM;
    dev = device_get_binding(CONFIG_SPI_2_NAME);                                                         /* 按板级配置名查找 SPI2 驱动对象。 */
    if (!dev) return -ENODEV;                                                                            /* 未注册目标设备时无法产生时钟。 */
    config.frequency = hz;                                                                               /* 将已验证频率写入本次调用的配置。 */
    sys_wake_lock(FULL_WAKE_LOCK);                                                                       /* 引脚切换和 DMA 期间保持系统唤醒。 */
    ret = swap_ready_begin();                                                                            /* 建立 GPIO 电平输入、双边沿回调和等待机制。 */
    if (ret) {
        if (ret == -EPERM)                                                                               /* -EPERM 表示链路已经禁止普通传输，需与初始化硬件故障区分。 */
            shell_error(sh, "SPI11 BLOCKED ret=%d; reboot both peers after a link fault", ret);
        else
            shell_error(sh, "SPI11 READY begin failed ret=%d", ret);
        sys_wake_unlock(FULL_WAKE_LOCK);                                                                 /* READY 初始化未通过时尚未打开 SPI，直接释放唤醒锁。 */
        return ret;                                                                                      /* 在未发时钟时将 READY 初始化故障返回。 */
    }
    s_swap_circular_used = true;                                                                         /* 成功取得 READY 后记录环形模式的使用历史。 */
    if (++sid == 0) ++sid;                                                                               /* 跳过保留给空闲回复的零会话号。 */
    shell_print(sh, "SPI11 START sid=%u target=%u bytes=512 payload=480 ack_delay=2 hz=%u gap_ms=%u",
                sid, (unsigned int)count, (unsigned int)hz, (unsigned int)gap);
    start = k_uptime_get_32();                                                                           /* 让驱动开关和末块处理也进入本次耗时统计。 */

    /* 打开主机后反复等待授权、交换整块并核对两块前的应答，额外两次传输用于取回末尾确认。 */
    ret = spi_acts_spi2_circular_open(dev, &config);
    if (ret) goto end;                                                                                   /* 驱动没有打开，无需调用 close，直接处理 READY 清理。 */
    for (i = 0; i < count + CIRC_ACK_DELAY; ++i) {                                                       /* 额外发送两个块以读出最后两笔请求的延迟 ACK。 */
        swap_block_frame(s_swap_tx, CIRC_MASTER, sid, (uint16_t)i);                                      /* 填充包含两个 V3 逻辑帧的本轮物理块。 */
        ret = swap_ready_wait(&waited);                                                                  /* 检查保持电平，只有匹配才允许启动本块时钟。 */
        wait_total += waited;                                                                            /* 每笔等待都计入总量，单凭一笔成功不能推断整轮通过。 */
        if (ret) break;                                                                                  /* 本次无法取得授权，不调用物理收发。 */
        swap_ready_consume();                                                                            /* 先标记当前授权已消费，下一块必须等相反电平。 */
        ++attempts;                                                                                      /* 实际驱动调用计数包含随后失败的传输。 */
        ret = spi_acts_spi2_circular_transfer(dev, s_swap_tx, s_swap_rx);                                /* 交换一个完整 512 字节物理块。 */
        if (ret) break;                                                                                  /* 驱动失败就停止，不检查本次无效的接收缓存。 */
        ret = swap_block_check(s_swap_rx, CIRC_SLAVE);                                                   /* 开头两个历史块也须通过双槽 CRC、图案和配对检查。 */
        if (ret || (i >= CIRC_ACK_DELAY && (swap_u16(s_swap_rx + 2) != sid ||
            swap_block_sequence(s_swap_rx) != i - CIRC_ACK_DELAY))) {                                    /* 流水填满之后再检查会话和落后两块的确认序号。 */
            shell_error(sh, "SPI11 ACK fail at=%u chk=%d sid=%u seq=%u", i, ret,
                        swap_u16(s_swap_rx + 2), swap_block_sequence(s_swap_rx));                        /* 输出本次接收的会话号和块号，定位是否读到陈旧 ACK。 */
            shell_print(sh, "SPI11 RX headers frame0/frame1 (16 bytes each):");
            shell_hexdump(sh, s_swap_rx, 16);                                                            /* 输出首槽头部，便于独立检查方向、会话、序号。 */
            shell_hexdump(sh, s_swap_rx + SWAP_SIZE, 16);                                                /* 同时输出第二槽开头以核实两帧的边界。 */
            ret = -EIO; break;                                                                           /* 不能容忍半槽或历史错位，本次按链路错误退出。 */
        }
        if (i >= CIRC_ACK_DELAY) ++good;                                                                 /* 历史块不计入有效吞吐量。 */
        if (gap && i + 1u < count + CIRC_ACK_DELAY) k_msleep(gap);                                       /* 留下用户要求的块间停顿，末块后不再额外睡眠。 */
    }

    /* 对端最终重新装载也计入测试时长，然后关闭驱动和 READY 并汇报有效吞吐。 */
    if (!ret) {
        ret = swap_ready_wait(&waited);                                                                  /* 等到对端处理完最后一块，耗时计入结果。 */
        wait_total += waited;                                                                            /* 保留已观察的电平授权供下一条命令消费。 */
    }
    close_ret = spi_acts_spi2_circular_close(dev);                                                       /* 完成时按驱动约定关闭本次环形主机会话。 */
    if (!ret) ret = close_ret;                                                                           /* 仅在没有更早的错误时使用驱动收尾错误。 */
end:
    ret = swap_ready_end(ret);                                                                           /* 注销 READY 中断并恢复引脚配置，必要时锁存故障。 */
    elapsed = k_uptime_get_32() - start;                                                                 /* 用同一时间基准求整个压测实际耗时。 */
    shell_print(sh, "SPI11 RESULT sid=%u good=%u target=%u tx_blocks=%u ms=%u payload_Bps=%u ready_us=%llu ret=%d %s",
                sid, good, (unsigned int)count, attempts, elapsed,
                elapsed ? (unsigned int)((uint64_t)good * SWAP_BLOCK_PAYLOAD * 1000u / elapsed) : 0u,    /* 只以确认成功的有效载荷计算吞吐，排除协议头尾。 */
                (unsigned long long)wait_total, ret, ret ? "FAIL" : "PASS");
    sys_wake_unlock(FULL_WAKE_LOCK);                                                                     /* 退出后允许系统恢复正常低功耗策略。 */
    return ret;                                                                                          /* 传回驱动、授权、校验或收尾的实际结果。 */
}
#endif
