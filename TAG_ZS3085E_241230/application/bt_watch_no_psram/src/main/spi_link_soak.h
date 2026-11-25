/* 有界的离线 SPI 诊断，以 INFO、RAM 和 QUIET 隔离设备查询、数据传送及静默恢复阶段。 */
#ifndef ATS_SPI_LINK_SOAK_H
#define ATS_SPI_LINK_SOAK_H

struct sfile_soak_job {
    struct sfile_sender sender;                                               /* 每阶段在同一堆对象内复用的发送器。 */
    struct sfile_probe probe;                                                 /* 首错寄存器、发送和接收快照及恢复统计。 */
    uint8_t info[SFILE_DEVICE_INFO_BYTES];                                    /* INFO 阶段捕获的固定长度设备信息。 */
    uint32_t round, phase, clean_rounds, clean_phases, elapsed;               /* 当前轮次和阶段、已完整通过的轮次和阶段、总耗时毫秒。 */
    int result;                                                               /* 首次失败的原因，供 show 复查。 */
};
static struct sfile_soak_job *sfile_soak_retained;                            /* 指向需要显式 clear 的失败现场，成功运行即释放。 */
static const char *const sfile_soak_phases[] = { "INFO", "RAM", "QUIET" };    /* 各轮依次验证短查询、内存块和静默交付。 */

/**
  * @brief  解析离线诊断的有界十进制参数。
  * @param  arg：[输入]以零结束的十进制文本。
  * @param  max：[输入]允许的最大数，含端点。
  * @param  out：[输出]成功时写入数值的非空地址。
  * @retval 0 成功，-EINVAL 为格式错误或超限。
  * @note   按十倍累加之前比较商和余数，防止溢出。
  */
static int sfile_soak_number(const char *arg, uint32_t max, uint32_t *out)
{
    uint32_t n = 0;                                                                          /* 从零开始构造当前诊断参数。 */
    if (!*arg) return -EINVAL;                                                               /* 缺少数字时返回 Shell 参数错误。 */
    while (*arg) {                                                                           /* 逐个消费直到参数的终止零。 */
        unsigned int d = (unsigned char)*arg++ - '0';                                        /* 取当前十进制位，用无符号比较同时拒绝小于零字符的情况。 */
        if (d > 9u || n > max / 10u || (n == max / 10u && d > max % 10u)) return -EINVAL;    /* 在乘十前比较上限的商及余数，拒绝越界的尾位。 */
        n = n * 10u + d;                                                                     /* 在已经验证的上限内把新数字合入结果。 */
    }
    *out = n;
    return 0;                                                                                /* 已确认所有字符均可表示指定上限内的数。 */
}

/**
  * @brief  打印一个阶段保存的 SPI2 时钟、控制、状态和复用寄存器。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  label：[输入]采样阶段标识。
  * @param  r：[输入]七个已保存的 32 位寄存器值。
  * @retval 无。
  * @note   读取的是在指定阶段保存的快照，不在打印时重新访问外设。
  */
static void sfile_soak_regs(const struct shell *sh, const char *label, const uint32_t r[7])
{
    shell_print(sh, "SPI SOAK REG %s clk=%08x ctrl=%08x status=%08x", label, r[0], r[1], r[2]);      /* 先显示时钟与 SPI 控制、状态寄存器。 */
    shell_print(sh, "SPI SOAK PIN %s 57..60=%08x/%08x/%08x/%08x", label, r[3], r[4], r[5], r[6]);    /* 再显示四个总线引脚的控制寄存器，便于检查复用状态。 */
}

/**
  * @brief  展示首错阶段、统计以及实际捕获的收发数据。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  j：[输入]仍由诊断任务持有的完整现场。
  * @retval 无。
  * @note   只有标为 frame_valid 的快照才打印帧，READY 超时可能没有新帧。
  */
static void sfile_soak_show(const struct shell *sh, const struct sfile_soak_job *j)
{
    static const char *const labels[] = { "before_open", "after_open", "before_close", "after_close" };              /* 与探针的四个开关驱动阶段逐一对应。 */
    const struct sfile_probe *p = &j->probe;                                                                         /* 始终查看这一轮保存的首错探针。 */
    unsigned int i, at;                                                                                              /* i 遍历寄存器阶段或 TX/RX 方向，at 为块内偏移。 */
    shell_print(sh, "SPI SOAK FIRST round=%u phase=%s tid=%u ret=%d frame=%u captured=%u bytes_valid=%u",
        j->round, sfile_soak_phases[j->phase], j->sender.tid, j->result, p->frame,
        (unsigned int)p->captured, (unsigned int)p->frame_valid);                                                    /* 标明寄存器及帧快照是否有效，避免当作正常新事务。 */
    shell_print(sh, "SPI SOAK COUNTERS clean_rounds=%u clean_phases=%u invalid=%u resync=%u recovered=%u resent=%u ms=%u",
        j->clean_rounds, j->clean_phases, p->invalid, p->recoveries, p->recovered, j->sender.resent, j->elapsed);    /* 输出干净阶段、坏帧、恢复尝试、成功及补传计数。 */
    for (i = 0; i < 4; ++i) if (p->regs_mask & (1u << i)) sfile_soak_regs(sh, labels[i], p->regs[i]);                /* 只打印阶段掩码中确认采集过的数据。 */
    if (p->captured) sfile_soak_regs(sh, "first_fault", p->fault_regs);                                              /* 仅首错已捕获时才打印出错瞬间的寄存器。 */
    if (!p->frame_valid) return;                                                                                     /* READY 超时可能没有本次接收数据，避免将旧缓冲解释为新帧。 */
    for (i = 0; i < 2; ++i) for (at = 0; at < SWAP_BLOCK_SIZE; at += 16u) {                                          /* 如果确有新帧，按发送和接收两个方向各打印 512 字节。 */
        const uint8_t *b = (i ? p->rx : p->tx) + at;                                                                 /* 在本次保存的发送与接收块中按相同偏移取数据。 */
        shell_print(sh, "SPI SOAK %s off=%03u le32=%08x/%08x/%08x/%08x", i ? "RX" : "TX", at,
            sfile_u32(b), sfile_u32(b + 4), sfile_u32(b + 8), sfile_u32(b + 12));                                    /* 每行按小端解出四个 32 位字，与协议字段和波形定位对应。 */
    }
}

/**
  * @brief  依次执行设备信息、RAM 及 QUIET 三阶段离线检查。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]show、clear，或轮数 1～10、阶段间隔 0～60000 ms、静默 0～15000 ms。
  * @retval 0 全部通过或查看清除成功，负值为参数、内存、保留现场或传输验证错误。
  * @note   失败立即停止并保留首错供 show。clear 仅释放快照；阶段间允许正常休眠。
  */
static int shell_spi2soak(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_soak_job *j;                                                                        /* 堆上保存完整测试轮、阶段和错误快照。 */
    uint32_t rounds, idle_ms, quiet_ms, start;                                                       /* 轮数、阶段间空闲、QUIET 时间与整轮起点使用毫秒时基。 */
    unsigned int n;                                                                                  /* INFO 回复中检查 IMEI 数字的字节索引。 */
    int ret = 0;                                                                                     /* 默认无错误，后续阶段失败或校验不符时覆盖。 */

    /* show 和 clear 操作上次保留的现场，均不启动传输。 */
    if (argc == 2 && !strcmp(argv[1], "show")) {
        if (sfile_soak_retained) sfile_soak_show(sh, sfile_soak_retained);                           /* show 只读保留现场，不发 SPI 时钟。 */
        else shell_print(sh, "SPI SOAK no retained fault");                                          /* 此次没有历史现场时只报告状态。 */
        return 0;                                                                                    /* 读取快照不会重新执行失败的事务。 */
    }
    if (argc == 2 && !strcmp(argv[1], "clear")) {                                                    /* clear 接受单个子命令，释放上次首错对象。 */
        if (sfile_soak_retained) k_heap_free(&sys_mem_pool, sfile_soak_retained);                    /* 仅当保留的对象确实存在时归还应用堆。 */
        sfile_soak_retained = NULL;                                                                  /* clear 释放历史堆快照；硬件故障锁仍由链路路径管理。 */
        shell_print(sh, "SPI SOAK snapshot cleared; hardware quarantine unchanged");
        return 0;                                                                                    /* 清理快照命令完成，硬件会话状态由传输层持有。 */
    }

    /* 启动新诊断前限制轮数与空闲时长，先分配足够空间再允许发 SPI 时钟。 */
    if (argc != 4 || sfile_soak_number(argv[1], 10u, &rounds) || !rounds ||
        sfile_soak_number(argv[2], 60000u, &idle_ms) || sfile_soak_number(argv[3], 15000u, &quiet_ms))
        return -EINVAL;                                                                              /* 非法轮次、时限或参数格式不触发任何操作。 */
    if (sfile_soak_retained) {                                                                       /* 上次失败尚未归档时拒绝启动另一轮覆盖快照。 */
        shell_error(sh, "SPI SOAK STOP retained fault; save logs, use show");
        return -EBUSY;                                                                               /* 请先处理已有首错现场，再开启新测试。 */
    }
    j = k_heap_alloc(&sys_mem_pool, sizeof(*j), K_NO_WAIT);                                          /* 为整份快照预留空间，分配失败时尚未发时钟。 */
    if (!j) { shell_error(sh, "SPI SOAK FAIL allocation; no clocks sent"); return -ENOMEM; }         /* 堆不够保存首错时测试不能开始。 */
    memset(j, 0, sizeof(*j));                                                                        /* 初始所有计数、快照标志及回调指针为空。 */
    start = k_uptime_get_32();                                                                       /* 从任务开始时计量包括空闲在内的总时间。 */
    shell_print(sh, "SPI SOAK BEGIN rounds=%u idle_ms=%u quiet_ms=%u hz=4000000 offline=1",
        rounds, idle_ms, quiet_ms);

    /* 三个阶段各自重新建立发送器和探针。阶段间的空闲可触发正常系统休眠。 */
    for (j->round = 1; j->round <= rounds; ++j->round) {
        for (j->phase = 0; j->phase < 3; ++j->phase) {                                               /* 每轮完整检查三个阶段，任何阶段失败都保留当时状态。 */
            struct sfile_sender *x = &j->sender;                                                     /* 每个小阶段复用任务内的发送状态。 */
            if (j->clean_phases && idle_ms) {                                                        /* 首阶段前无历史事务；仅在之后且用户要求时安排间隔。 */
                shell_print(sh, "SPI SOAK IDLE ms=%u next_round=%u next_phase=%s",
                    idle_ms, j->round, sfile_soak_phases[j->phase]);
                /* 阶段间直接休眠，使电源管理按正常事务间空闲策略运行。 */
                k_msleep(idle_ms);                                                                   /* 自然睡眠指定的阶段间毫秒数。 */
            }
            memset(x, 0, sizeof(*x));                                                                /* 清除上一阶段的传输计数，探针和设备信息也分别复位。 */
            memset(&j->probe, 0, sizeof(j->probe));                                                  /* 重新打开本阶段的首错采集窗口。 */
            memset(j->info, 0, sizeof(j->info));                                                     /* 清空旧设备信息，排除误读前一轮内容。 */
            x->probe = &j->probe; x->bad_block = x->fail_at = UINT32_MAX;                            /* 禁用合成坏块及流式失败注入，只记录自然发生的故障。 */
            if (!j->phase) { x->device_info = j->info; x->delivery_command = SFILE_DEVICE_INFO; }    /* 第一阶段读取设备信息，不触发网络服务。 */
            else {
                x->read_block = sfile_read_generated; x->size = SFILE_DATA_BYTES;                    /* RAM 和 QUIET 都发送恰好一块 480 字节生成数据。 */
                if (j->phase == 2) {                                                                 /* 第三个阶段在完整文件交付中制造 QUIET。 */
                    x->delivery_command = SFILE_START_DELIVERY; x->hold_ms = quiet_ms;               /* QUIET 额外请求在组交付期间维持指定静默时间。 */
                    j->probe.quiet = true;                                                           /* 使发送端附上明确的离线 QUIET 诊断标记。 */
                }
            }
            shell_print(sh, "SPI SOAK PHASE BEGIN round=%u phase=%s", j->round, sfile_soak_phases[j->phase]);
            ret = sfile_run_context(sh, x, NULL, true, 4000000u);                                    /* 三个阶段均经实际配对传输路径以 4 MHz 运行。 */

            /* INFO 还要校验设备信息字段；三个阶段均要求没有任何传输恢复或补发计数。 */
            if (!ret && !j->phase) {
                if (j->info[0] != 1 || sfile_u32(j->info + 1) < 1735689600u ||
                    sfile_u32(j->info + 1) >= 4102444800u) ret = -EBADMSG;                           /* 时间必须落在 2025 年起至 2100 年初之前。 */
                for (n = 5; n < SFILE_DEVICE_INFO_BYTES; ++n)                                        /* 设备信息的版本和时间占前五字节，余下为 IMEI。 */
                    if (j->info[n] < '0' || j->info[n] > '9') ret = -EBADMSG;                        /* 校验设备信息尾部的十五个数字。 */
                /* 本阶段配合对端验证 2025 至 2099 年的 UTC 范围，不建立 TLS 会话。 */
            }
            if (!ret && (j->probe.captured || j->probe.invalid || j->probe.recoveries ||
                         x->resent || x->resync_needed || x->link_failed)) ret = -EBADMSG;           /* 即使数据最终到达，发生过坏帧、补传或恢复也不算洁净样本。 */
            if (ret) goto failed;                                                                    /* 任一阶段失败立即进入封存现场的路径。 */
            ++j->clean_phases;                                                                       /* 只有整个阶段及其附加校验通过才增加计数。 */
            shell_print(sh, "SPI SOAK PHASE PASS round=%u phase=%s tid=%u frames=%u",
                j->round, sfile_soak_phases[j->phase], x->tid, x->transfers);
        }
        ++j->clean_rounds;                                                                           /* 三个阶段全部通过才增加完整轮次。 */
    }
    shell_print(sh, "SPI SOAK RESULT PASS clean_rounds=%u clean_phases=%u resync=0 invalid=0 ms=%u",
        j->clean_rounds, j->clean_phases, (uint32_t)(k_uptime_get_32() - start));
    k_heap_free(&sys_mem_pool, j);                                                                   /* 全部轮次成功时不需要保留首错快照。 */
    return 0;                                                                                        /* 所有计划轮数均已执行，返回洁净结果。 */
failed:

    /* 将堆对象所有权从当前调用交给保留指针，直到操作者读出并显式清除。 */
    j->result = ret; j->elapsed = k_uptime_get_32() - start;
    sfile_soak_retained = j;                                                                         /* 留下失败瞬间的完整上下文，以供下一次 show 或 clear。 */
    shell_error(sh, "SPI SOAK RESULT FAIL STOP round=%u phase=%s ret=%d; first fault retained",
        j->round, sfile_soak_phases[j->phase], ret);
    sfile_soak_show(sh, j);                                                                          /* 在命令退出前先展示现场，以后仍可用 show 重读。 */
    return ret;                                                                                      /* 向命令框架报告首次停止的实际原因。 */
}
#endif
