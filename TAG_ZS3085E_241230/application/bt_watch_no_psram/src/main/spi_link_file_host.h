/* ATS 文件协议主机。Shell 线程驱动 SPI2 发时钟，根据 READY 授权推进事务；CRC、事务号和请求号用于把流水应答对应到本次操作。 */
#ifndef ATS_SPI_LINK_FILE_H
#define ATS_SPI_LINK_FILE_H

#include <fs/fs.h>                      /* Zephyr 文件句柄及定位、读写和关闭接口。 */
#include "spi_link_file_protocol.h"     /* 两端一致的文件帧布局、控制码与校验函数。 */
#include "spi_link_entropy.h"           /* 为 HTTPS 启动控制帧取得硬件随机种子。 */

#define SFILE_READY_TIMEOUT_MS 2000u    /* 普通文件控制等待 READY 的缺省毫秒数，网络阶段可覆盖。 */
#define SFILE_DEFAULT_HZ 4000000u       /* 普通文件和 RAM 测试使用的 SPI2 请求频率，单位 Hz。 */
/* 蜂窝/TLS 交付命令使用 4 MHz，避免沿用发生过从机 RX FIFO 溢出的 8 MHz 设置。SPI2 的 32 MHz HOSC 使用 /8 得到 4 MHz，最大 /14 分频无法得到 1 MHz。 */
#define SFILE_HTTPS_HZ 4000000u

extern struct k_heap sys_mem_pool;      /* 应用共享堆的外部声明，控制参数和调试快照使用即时分配。 */

/* P4_SPI_DIAG 保存长静默后首对块的寄存器和帧头，采样后再由 Shell 输出。 */
#ifdef P4_SPI_DIAG
struct sfile_diag_sample {
    uint32_t regs[7], tx[4], rx[4], transfer_us, command;    /* 七个外设寄存器、收发帧头各四字、传输微秒数和命令。 */
};
struct sfile_diag {
    struct sfile_diag_sample samples[2];                     /* 两个物理块对应的诊断样本，容量限于一对。 */
    uint32_t initial[7], waited_us;                          /* 初始寄存器快照及首次超过诊断阈值的等待时长。 */
    unsigned int count;                                      /* 已采集的样本数，受固定两个槽限制。 */
};

/**
  * @brief  保存 SPI2 时钟、控制、状态和 GPIO57～60 的复用快照。
  * @param  regs：[输出]至少七个 32 位字的可写数组。
  * @retval 无。
  * @note   仅在诊断构建读取七个寄存器，用于还原慢等待附近的硬件状态。
  */
static void sfile_diag_regs(uint32_t regs[7])
{
    unsigned int i;
    regs[0] = sys_read32(CMU_SPI2CLK);                                                   /* 时钟源与分频选择的现场值。 */
    regs[1] = sys_read32(SPI2_REG_BASE);                                                 /* SPI2 控制寄存器。 */
    regs[2] = sys_read32(SPI2_REG_BASE + 4);                                             /* SPI2 状态寄存器。 */
    for (i = 0; i < 4; ++i) regs[3 + i] = sys_read32(GPIO_REG_BASE + (57u + i) * 4u);    /* 保存 GPIO57、58、59、60 各自的复用寄存器。 */
}
#endif

struct sfile_http_files {
    uint8_t radio_test;                                                                       /* 专用射频诊断构建使用：1 表示 init 前，2 表示 PUT 期间。 */
    struct fs_file_t spec, result;                                                            /* 文件型命令已打开的规格输入和响应输出句柄。 */
    const uint8_t *spec_data;                                                                 /* 指向内存中的请求规格；为空时从已打开的规格文件读取。 */
    uint16_t spec_size, http_code, response_size;                                             /* 规格字节数、从机报告的 HTTP 状态码和完整响应长度。 */
    bool console_result;                                                                      /* 为 true 时将响应分页转为串口十六进制。 */
    int (*receive)(void *context, uint32_t offset, const uint8_t *data, unsigned int len);    /* 每收完整一页交给上层，offset 为响应偏移，len 为有效字节数；非零返回中止。 */
    void *receive_context;                                                                    /* receive 的调用者私有上下文，回调期间持续有效。 */
    bool response_complete, link_failed;                                                      /* 分别表示全部响应页已收齐、SPI 或清理阶段失败。 */
    bool transport_fault;                                                                     /* 保留实际传输失败，包括后续 RESYNC 成功的 CRC 故障。 */
    unsigned int wire_status;                                                                 /* 保存从机文件协议状态，使上层区分 HTTP、交付与链路错误。 */
#ifdef P4_SPI_DIAG
    struct sfile_diag *diag;                                                                  /* 可选的长等待诊断快照，普通镜像不保留这部分状态。 */
#endif
};

#ifdef P4_SPI_SOAK
/* spi2soak 从堆上持有首错现场，避开 2 KiB 串口 Shell 栈。 */
struct sfile_probe {
    uint32_t regs[4][7], fault_regs[7], regs_mask;       /* 打开前后和关闭前后的四组快照、首错快照及有效阶段位图。 */
    uint8_t tx[SWAP_BLOCK_SIZE], rx[SWAP_BLOCK_SIZE];    /* 保留首错现场的一整个发送块和接收块。 */
    uint32_t frame, invalid, recoveries, recovered;      /* 首错帧号、无效应答数、重同步尝试数及验证成功数。 */
    int error;                                           /* 首次失败的错误码。 */
    bool captured, frame_valid, quiet;                   /* 是否已捕获、帧缓冲是否有现场以及是否在做 QUIET 验证。 */
};
#endif
struct sfile_sender {
#ifdef P4_SPI_SOAK
    struct sfile_probe *probe;                                        /* 可选的离线诊断探针，由 spi2soak 管理内存。 */
#endif
    struct sfile_http_files *http;                                    /* 通用 HTTP 请求规格和响应输出；其他模式可空。 */
    uint8_t *device_info;                                             /* 设备查询模式的定长输出区，为空时按文件正文传输。 */
    const struct shell *shell;                                        /* 输出本次诊断信息所用的 Shell。 */
    const char *url;                                                  /* 旧 HTTPS 命令直接传给从机的目标 URL。 */
    const struct device *dev;                                         /* 当前绑定的 SPI2 主机设备实例。 */
    int (*read_block)(void *context, uint32_t index, uint8_t *data,
                      unsigned int len, bool repair);                 /* 按块号读 len 字节到 data，repair 表示补传，0 成功，非零中止。 */
    void *source;                                                     /* read_block 的源上下文，生成数据的模式可为空。 */
    uint32_t tid, size, crc, transfers, resent, groups, bad_block;    /* 业务事务号、正文长度、流式 CRC、交换数、补传数、组数及诊断坏块号。 */
    uint32_t last_index, peak_index, peak_ready_us;                   /* 最近块索引、最长等待对应的索引和该等待的微秒数。 */
    uint32_t ready_timeout_ms, peak_timeout_ms;                       /* 本次控制操作指定的 READY 超时及最大等待那次使用的超时，单位毫秒。 */
    uint64_t ready_us, data_ready_us;                                 /* 所有 READY 等待及归属数据块的等待累计，单位微秒。 */
    uint16_t request;                                                 /* 递增的控制请求号，用于把延迟 ACK 与当前命令配对。 */
    uint8_t last_type, peak_type;                                     /* 最后一个非 IDLE 命令及最长等待所跟随的命令。 */
    unsigned int pairs;                                               /* 已经发完两个物理块的配对数。 */
    uint32_t hold_ms, fail_at;                                        /* 交付保持毫秒数，以及流式诊断失败的字节位置。 */
    uint32_t group_busy, group_wait_ms, group_peak_ms;                /* 组结束收到 BUSY 的次数、组等待总毫秒和峰值毫秒。 */
    unsigned int invalid_replies;                                     /* 配对模式下既非有效文件块也非有效历史测试块的回复数。 */
    bool resync_needed, resync_active;                                /* 等待重同步与正在执行重同步分别记录，避免递归触发。 */
    uint8_t delivery_command;                                         /* 决定设备查询、内存交付或 HTTP 等开始操作。 */
    bool link_failed, repaired_bad_block, paired, half_pair;          /* 分别为链路故障、坏块已补、双块模式、当前只发了半对。 */
#ifdef P4_SPI_WITNESS_TEST
    bool witness_once, witness_notice;                                /* 单次见证字注入使能及是否观察到符合约定的重建通知。 */
    uint8_t witness_frames;                                           /* 记录实际完成的注入帧数，区分准备和线上发送。 */
#endif
};

#ifdef P4_SPI_SOAK

/**
  * @brief  首次故障时保留计数、寄存器和可用的双向帧。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @param  error：[输入]本次失败的错误码。
  * @param  frame_valid：[输入]当前收发缓存是否应随快照一同保存。
  * @retval 无。
  * @note   仅在离线诊断且探针已挂接时记录，后续错误不覆盖首错。
  */
static void sfile_probe_fault(struct sfile_sender *x, int error, bool frame_valid)
{
    struct sfile_probe *p = x->probe;                                 /* 从当前发送器取得由 SOAK 任务分配的现场对象。 */
    if (!p || p->captured) return;                                    /* 未挂探针或已有首错时返回，避免覆盖最早证据。 */
    p->captured = true; p->error = error; p->frame = x->transfers;    /* 锁存失败的返回码和物理交换序号。 */
    p->frame_valid = frame_valid;                                     /* 告知显示端是否应解析本次保存的收发缓冲。 */
    sfile_diag_regs(p->fault_regs);                                   /* 当场读取当前硬件状态，避免随后关闭驱动改变寄存器。 */
    if (frame_valid) {                                                /* 仅有对应事务时才保存收发缓存作帧诊断。 */
        memcpy(p->tx, s_swap_tx, SWAP_BLOCK_SIZE);                    /* 保存触发故障的主机发送块。 */
        memcpy(p->rx, s_swap_rx, SWAP_BLOCK_SIZE);                    /* 与同一物理事务的从机返回块成对保留。 */
    }
}

/**
  * @brief  记录指定开关驱动阶段的寄存器并标记有效。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @param  phase：[输入]现场类别的数组下标，0～3。
  * @retval 无。
  * @note   调用现场的 phase 为 0～3，对应打开和关闭操作的前后。
  */
static void sfile_probe_regs(struct sfile_sender *x, unsigned int phase)
{
    if (!x->probe) return;                      /* 业务请求没有挂接 SOAK 探针时跳过寄存器采样。 */
    sfile_diag_regs(x->probe->regs[phase]);     /* 将当前硬件值直接写进指定阶段的槽位。 */
    x->probe->regs_mask |= 1u << phase;         /* 有效阶段置位，show 时据此排除从未采集的阶段。 */
}
#else
#define sfile_probe_regs(x, phase) ((void)0)    /* 未开启离线诊断时，阶段采样展开为空操作。 */
#endif
static bool sfile_pair_mode;                    /* 跨命令保存双方已完成的配对模式切换。 */

/**
  * @brief  按当前命令的剩余预算等待从机授权，并累计延迟。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @retval 0 电平匹配，负值为 GPIO、状态或超时错误。
  * @note   文件模式默认 2000 ms；网络组确认等操作可通过发送上下文指定时限。
  */
static int sfile_wait_ready(struct sfile_sender *x)
{
    uint32_t waited;                                                                           /* 实际等待的微秒数由 READY 层回填。 */
    uint32_t timeout = x->ready_timeout_ms ? x->ready_timeout_ms : SFILE_READY_TIMEOUT_MS;     /* 特定控制阶段可给更长等待，其他情况沿用 2000 ms。 */
    int ret = swap_ready_wait_timeout(&waited, timeout);                                       /* 同步等待持有的授权电平，失败锁存由 READY 层处理。 */
#ifdef P4_SPI_DIAG
    if (!ret && x->http && x->http->diag && waited >= 100000u && !x->http->diag->waited_us)    /* 只保留首次超过 100 ms 的成功等待，供后续配对采样。 */
        x->http->diag->waited_us = waited;                                                     /* 将慢等待值留给后续两个槽的诊断采样。 */
#endif
    x->ready_us += waited;                                                                     /* 不论是否成功都计入整个会话的等待开销。 */
    if (x->last_type == SFILE_DATA) x->data_ready_us += waited;                                /* 将等待归到最后的非空闲命令，便于区分数据与控制背压。 */
    if (waited > x->peak_ready_us) {
        x->peak_ready_us = waited;                                                             /* 出现新的最大值时同步记下命令、索引和本次预算。 */
        x->peak_type = x->last_type;                                                           /* 记录峰值前完成的非空闲命令。 */
        x->peak_index = x->last_index;                                                         /* 记录该命令所处的数据块或控制索引。 */
        x->peak_timeout_ms = timeout;                                                          /* 保存峰值样本真正使用的时限，便于区分网络静默和硬故障。 */
    }
    return ret;                                                                                /* 保留电平读取、超时或成功的实际状态。 */
}

/**
  * @brief  按当前单双块授权推进一次 512 字节全双工交换。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @retval 0 交换完成，-EBADMSG 为需重同步的完整配对，其余为 READY 或驱动错误。
  * @note   配对的第二块复用首块授权；接到从机重建通知仍需填完第二槽，才退出当前业务。
  */
static int sfile_exchange(struct sfile_sender *x)
{
    /* 根据当前半对标志取得发送授权，必要时为配对尾部填入约定见证字。 */
    int ret = x->half_pair ? 0 : sfile_wait_ready(x);
    if (!ret) {
#ifdef P4_SPI_WITNESS_TEST
        bool inject = false;                                                                                    /* 此物理块默认不破坏配对尾标。 */
#endif
        if (!x->half_pair) swap_ready_consume();                                                                /* 只在每对起点消费一次令牌，第二槽不能再次翻转期望电平。 */
        if (s_swap_tx[2] != SFILE_IDLE) {
            x->last_type = s_swap_tx[2];                                                                        /* 记录正在发送的业务类型，以归属随后的 READY 等待。 */
            x->last_index = sfile_u32(s_swap_tx + 8);                                                           /* IDLE 只填流水空位，不覆盖诊断需要的最后业务索引。 */
        }
        if (x->paired) {
            uint32_t witness = SFILE_PAIR_WITNESS;                                                              /* 正常配对块放置两端约定的四字节常量。 */
#ifdef P4_SPI_WITNESS_TEST
            /* 只破坏一对 DEVICE_INFO/QUERY 的见证字，实际成功发出的块数在驱动返回后累计。 */

            /* 按命令、半槽状态及已完成数精确选择本次设备查询的两个槽。 */
            inject = x->witness_once &&
                ((!x->half_pair && !x->witness_frames && s_swap_tx[2] == SFILE_DEVICE_INFO) ||
                 (x->half_pair && x->witness_frames == 1 && s_swap_tx[2] == SFILE_QUERY));
            if (inject) witness ^= 1u;                                                                          /* 只改独立见证常量的最低位，头及有效载荷的 CRC 保持原规则。 */
#endif
            sfile_put32(s_swap_tx + SFILE_PAIR_WITNESS_OFFSET, witness);                                        /* 把见证字写到 CRC 保护范围之外的块末预留区。 */
        }
        ++x->transfers;                                                                                         /* 此计数包括已经进入驱动的失败尝试。 */
#ifdef P4_SPI_DIAG
        /* 长静默后只取首个配对的两个样本，先保留寄存器和小段帧头，退出时统一打印。 */
        struct sfile_diag *d = x->http ? x->http->diag : NULL;                                                  /* 诊断入口才有采样记录，普通 HTTP 会话可以没有。 */
        struct sfile_diag_sample *sample = d && d->waited_us && d->count < 2 ? &d->samples[d->count] : NULL;    /* 只有先记录了慢等待、且两个样本槽尚未用尽才采集。 */
        uint32_t transfer_start = 0;                                                                            /* 只有诊断采样选中这一槽时才读取额外的周期计数。 */
        if (sample) {
            sfile_diag_regs(sample->regs);                                                                      /* 在实际发时钟前读取主机寄存器现场。 */
            memcpy(sample->tx, s_swap_tx, sizeof(sample->tx));                                                  /* 保存传输前的 TX 帧头和命令，后续统一打印。 */
            sample->command = s_swap_tx[2];                                                                     /* 记录这份快照对应的线上命令。 */
            transfer_start = k_cycle_get_32();                                                                  /* 稍后用周期差计算传输微秒数。 */
        }
#endif
        /* 同步完成物理交换后，再从新收到的块判定诊断、恢复提示和下一次应使用的槽位。 */
        ret = spi_acts_spi2_circular_transfer(x->dev, s_swap_tx, s_swap_rx);
#ifdef P4_SPI_WITNESS_TEST
        if (!ret && inject) ++x->witness_frames;                                                                /* 只有驱动确实完成的注入传输才记入验证数量。 */
#endif
#ifdef P4_SPI_SOAK
        if (x->probe) {
            bool invalid = !sfile_valid(s_swap_rx, SFILE_SLAVE) && swap_block_check(s_swap_rx, 0x3cu);          /* 文件格式与上电历史图案块均失败才计作无效。 */
            bool recovery = sfile_valid(s_swap_rx, SFILE_SLAVE) && s_swap_rx[2] == SFILE_LINK_RECOVERY;         /* 诊断将格式有效且命令为恢复通知的回包另行标记。 */
            if (invalid) {
                ++x->probe->invalid;                                                                            /* 独立累计既不是有效文件块也不是预装图案块的回复。 */
                /* 离线诊断见到首个坏回复即记录。配对模式先发完另一槽，才进入有界恢复。 */
                if (x->paired) x->resync_needed = true;                                                         /* 等到信用对的完整边界再开始有界重同步。 */
            }
            if (ret || invalid || recovery)
                sfile_probe_fault(x, ret ? ret : -EBADMSG, true);                                               /* 先记录首错帧，后续控制交换不能覆盖这一现场。 */
        }
#endif
        /* 从机本地重建后无法确认先前的请求身份。CRC 和 cookie 有效的通知要求中止当前业务，补完配对后发起新的 RESYNC 握手。 */
        if (!ret && x->paired && sfile_valid(s_swap_rx, SFILE_SLAVE) &&
            s_swap_rx[2] == SFILE_LINK_RECOVERY &&
            !sfile_u32(s_swap_rx + 4) && !sfile_u32(s_swap_rx + 8) &&
            !swap_u16(s_swap_rx + 12) && swap_u16(s_swap_rx + 14) == 4 &&
            sfile_u32(s_swap_rx + SFILE_HEADER) == SFILE_RECOVERY_COOKIE) {                                     /* 仅信任方向、CRC、命令、保留身份、长度和 cookie 全部匹配的恢复通知。 */
#ifdef P4_SPI_WITNESS_TEST
            if (!inject && x->witness_frames == 2 && !x->resync_active)
                x->witness_notice = true;                                                                       /* 两个注入块已发送，且后续确实收到有效恢复通知。 */
#endif
            if (!x->resync_needed)
                shell_error(x->shell, "SPI RESYNC required tid=%u peer RX boundary reset", x->tid);
            x->resync_needed = true;                                                                            /* 重建通知没有原业务请求的可信确认，不能作为文件成功结果。 */
        }
        if (!ret && x->paired && !sfile_valid(s_swap_rx, SFILE_SLAVE) &&
            swap_block_check(s_swap_rx, 0x3cu))
            ++x->invalid_replies;                                                                               /* 多次无效的配对应答累计到控制层的重同步阈值。 */
#ifdef P4_SPI_DIAG
        if (sample) {
            sample->transfer_us = k_cyc_to_us_floor32(k_cycle_get_32() - transfer_start);                       /* 结果回到任务时由周期差得到本块实际耗时。 */
            memcpy(sample->rx, s_swap_rx, sizeof(sample->rx));                                                  /* 保存实际收回的配对帧头，同之前的发送现场一起核对。 */
            ++d->count;                                                                                         /* 完成收发快照才占用一个样本名额。 */
        }
#endif
        /* 擦除线上种子的暂存副本。成功交换才推进半对标志，底层故障另行锁存。 */
        if (s_swap_tx[2] == SFILE_START_HTTPS || s_swap_tx[2] == SFILE_START_HTTP) {
            volatile uint8_t *seed = s_swap_tx + SFILE_HEADER + 4;                                              /* TLS 种子在开始载荷中紧随四字节长度字段。 */
            unsigned int i;
            for (i = 0; i < SFILE_HTTPS_SEED_BYTES; ++i) seed[i] = 0;                                           /* 启动参数包含 TLS 种子，发送结束后逐字节擦除 TX 副本。 */
        }
        if (!ret && x->paired) {
            x->half_pair = !x->half_pair;                                                                       /* 只有驱动成功返回才推进本地半槽状态。 */
            if (!x->half_pair) ++x->pairs;                                                                      /* 第二槽结束后增加完整配对计数。 */
        }
    }
#ifdef P4_SPI_SOAK
    if (ret) sfile_probe_fault(x, ret, false);                                                                  /* 等待或驱动返回错误，没有已记录的当前有效帧时仅留寄存器。 */
#endif
    if (ret) x->link_failed = true;                                                                             /* 底层等待或驱动出错时锁住本次链路。 */
    if (!ret && !x->half_pair && x->resync_needed && !x->resync_active)                                         /* 只有完整配对且未在重同步中，才把本次业务转为坏帧错误。 */
        return -EBADMSG;                                                                                        /* 向控制层报告需重同步，业务请求不能被确认。 */
    return ret;
}

/**
  * @brief  检查当前接收块是否完整回应指定的控制请求。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @param  command：[输入]期待回显的命令。
  * @param  index：[输入]期待的块号或页偏移。
  * @param  request：[输入]等待确认的请求号。
  * @retval true 全部匹配，false 表示无效、陈旧或与命令不符。
  * @note   QUERY 取回先前操作的回执。CRC16 正确的旧 ACK 仍须丢弃，只有 tid、index、request、回显命令和载荷格式均对应本次请求才接受。
  */
static bool sfile_ack_matches(const struct sfile_sender *x, uint8_t command,
                              uint32_t index, uint16_t request)
{
    unsigned int len = swap_u16(s_swap_rx + 14);                                                     /* 解出本次回包宣告的实际载荷字节数。 */

    /* 普通 ACK 只有状态及位图；FINISH、分页和设备信息可按各自命令附加数据。 */
    bool http_result = (x->url || x->http) && command == SFILE_FINISH &&
        len >= SFILE_HTTP_RESULT_HEAD && len <= SFILE_HTTP_RESULT_HEAD + SFILE_HTTP_RESPONSE_MAX;    /* 旧 FINISH 结果可附不超过 192 字节的响应内容。 */
    bool http_page = x->http && command == SFILE_HTTP_READ &&
        len > SFILE_ACK_BYTES && len <= SFILE_ACK_BYTES + SFILE_HTTP_PAGE_BYTES;                     /* 分页 ACK 必须含非空正文，且至多 400 字节。 */
    bool info = x->device_info && command == SFILE_DEVICE_INFO &&
        len == SFILE_ACK_BYTES + SFILE_DEVICE_INFO_BYTES;                                            /* 设备信息命令的扩展必须恰为 20 字节。 */

    /* 匹配业务身份并限制扩展载荷形状，最后检查回显命令与整帧校验。 */
    return s_swap_rx[2] == SFILE_ACK && sfile_u32(s_swap_rx + 4) == x->tid &&
        sfile_u32(s_swap_rx + 8) == index && swap_u16(s_swap_rx + 12) == request &&
        (len == SFILE_ACK_BYTES || http_result || http_page || info) &&
        s_swap_rx[SFILE_HEADER] == command && sfile_valid(s_swap_rx, SFILE_SLAVE);                   /* 最后验证 ACK 回显的命令及帧头、长度与 CRC。 */
}

/* 从机须收到严格三次单块事务后才切换配对。最后的 ACK 身份不符即锁住链路。 */
/**
  * @brief  用三次单块事务把双方切入每次授权两块的模式。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @param  command：[输入]触发切换的开始或设备信息命令。
  * @param  body：[输入]至少 len 字节的有效开始参数。
  * @param  len：[输入]参数字节数，不超过单块载荷上限。
  * @retval 0 切换成功，正数为从机状态，负数为传输或确认错误。
  * @note   首块是开始命令，后两块为 QUERY。第三次返回的 ACK 必须与本次请求一致。
  */
static int sfile_pair_begin(struct sfile_sender *x, uint8_t command,
                            const uint8_t *body, unsigned int len)
{
    unsigned int i;
    uint16_t request = ++x->request;                                                                    /* 三次单块共享同一控制请求号。 */
    int ret;

    /* 开始命令后紧跟两次 QUERY，三笔均按单块授权等待对端就绪。 */
    memcpy(s_swap_tx + SFILE_HEADER, body, len);
    for (i = 0; i < 3; ++i) {
        sfile_frame(s_swap_tx, SFILE_MASTER, i ? SFILE_QUERY : command,                                 /* 首次发开始命令，随后两次发空载荷 QUERY 取回延迟 ACK。 */
                    x->tid, 0, request, i ? 0 : len);
        ret = sfile_exchange(x);                                                                        /* 每笔单块各自消耗一次 READY。 */
        if (ret) return ret;                                                                            /* 驱动或电平异常时不继续切换配对。 */
    }
    if (!sfile_ack_matches(x, command, 0, request)) {                                                   /* 对端是否进入双块模式只以明确 ACK 为据，含糊结果不能继续。 */
        x->link_failed = true;                                                                          /* 无法判定从机是否已切换，阻止当前链路继续。 */
        return -EBADMSG;
    }
    ret = s_swap_rx[SFILE_HEADER + 1];                                                                  /* 取对端在 ACK 中给出的文件协议状态。 */
    if (!ret && x->device_info) {
        if (swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + SFILE_DEVICE_INFO_BYTES)                      /* 设备信息成功时必须携带完整定长数据。 */
            return -EBADMSG;
        memcpy(x->device_info, s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES, SFILE_DEVICE_INFO_BYTES);    /* 跳过回显命令、状态和位图，把设备信息复制出去。 */
    }
    if (!ret) sfile_pair_mode = x->paired = true;                                                       /* 对端确认后才同时提交跨命令模式和本次发送器状态。 */
    return ret;                                                                                         /* 成功为零，正值保留从机拒绝或错误的状态。 */
}

/**
  * @brief  发送一个控制命令，在同一操作期限内轮询匹配 ACK。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @param  command：[输入]需要完成的控制命令。
  * @param  index：[输入]该命令的组首块号、页偏移或零。
  * @param  body：[输入]有效控制参数，len 为零时可为 NULL。
  * @param  len：[输入]参数字节数，不超过 480。
  * @retval 0 成功，正数为从机状态，负值为超时、帧错误或底层失败。
  * @note   进入验证后只发 QUERY，配对边界完整才接受 ACK；FINISH 最长 120 s，组交付采用相应时限。
  */
static int sfile_control(struct sfile_sender *x, uint8_t command, uint32_t index,
                         const uint8_t *body, unsigned int len)
{
    uint32_t start = k_uptime_get_32();                                                                             /* 后续所有 QUERY 共用此控制操作的毫秒起点。 */
    bool delivery_group = x->delivery_command && command == SFILE_GROUP_END;                                        /* 交付命令的 GROUP_END 可能等待网络或消费者处理。 */
    uint32_t timeout = command == SFILE_FINISH ? 120000u :
                       (delivery_group ? ((x->url || x->http) ? 35000u : 20000u) : 2000u);                          /* 普通控制 2 s，普通交付组 20 s，网络组 35 s，结束 120 s。 */
    uint16_t request;                                                                                               /* 保存本次请求号，轮询期间保持不变。 */
    unsigned int i = 0;                                                                                             /* 自本次请求起累计尝试次数，用于安排命令与 QUERY。 */
    bool verifying = false;                                                                                         /* 收到 BUSY 之前可有限重发命令，之后转为只读查询。 */

    /* 分配新的非零请求号，在这次控制的统一截止时间内发送原命令或查询。 */
    if (++x->request == 0) ++x->request;
    request = x->request;                                                                                           /* 针对同一轮控制保持同一请求号。 */
    do {
        bool send_command = !verifying && (i++ % 4u == 0);                                                          /* 未确认时每四次尝试发送一次原命令，其余用 QUERY 推进流水。 */
        int ret;
        if (send_command && len) memcpy(s_swap_tx + SFILE_HEADER, body, len);                                       /* 只有发原命令且存在参数时才拷入有效载荷。 */
        sfile_frame(s_swap_tx, SFILE_MASTER, send_command ? command : SFILE_QUERY,                                  /* 控制数据只随原命令发送；QUERY 的有效载荷为零。 */
                    x->tid, index, request, send_command ? len : 0);
        /* 从机处理网络业务期间可保持 READY。后续轮询复用该控制操作的总期限。 */
        if ((x->url || x->http
#ifdef P4_SPI_SOAK
             || (x->probe && x->probe->quiet)
#endif
            ) && (delivery_group || command == SFILE_FINISH) && i > 1u) {
            uint32_t elapsed = (uint32_t)(k_uptime_get_32() - start);                                               /* 计算从本次控制开始已经消耗的总毫秒。 */
            x->ready_timeout_ms = elapsed < timeout ? timeout - elapsed : 1u;                                       /* 整个控制操作共用截止时间，本轮分配剩余 READY 毫秒数。 */
        }
        ret = sfile_exchange(x);                                                                                    /* 一次收发同时送出控制或查询、读取此前流水回复。 */
        /* 完成两个槽后再判断是否停机。CRC 损坏或位移得到的表面匹配字段无效。 */
        if (!ret && x->paired && !x->half_pair && x->invalid_replies >= 4u) {                                       /* 在完整配对边界处理累计四个无效回包，避免仅发送半对就停止。 */
            x->resync_needed = true;                                                                                /* 四次无效应答触发有界恢复，当前操作先失败。 */
            shell_error(x->shell, "SPI RESYNC required tid=%u invalid=%u rx=%08x/%08x",
                        x->tid, x->invalid_replies, sfile_u32(s_swap_rx), sfile_u32(s_swap_rx + 4));
            return -EBADMSG;                                                                                        /* 报告应答无法作为有效业务确认。 */
        }
        x->ready_timeout_ms = 0;                                                                                    /* 清除临时预算，以免影响下一种控制操作。 */
        if (ret) return ret;                                                                                        /* 未获得有效事务时直接上抛，不能解析 RX 残留。 */

        /* 只有在完整槽对边界才接受匹配的 ACK；根据命令验证其额外的设备信息或 HTTP 结果。 */
        if (!x->half_pair && sfile_ack_matches(x, command, index, request)) {
            ret = s_swap_rx[SFILE_HEADER + 1];                                                                      /* ACK 载荷前两个字节为回显命令和结果，从第二字节取状态。 */
            if (ret != SFILE_BUSY || (command != SFILE_FINISH && !delivery_group)) {                                /* 对 FINISH 或交付组的 BUSY 继续查询，其他状态直接处理。 */
                unsigned int len = swap_u16(s_swap_rx + 14);                                                        /* 再按具体控制请求检查精确长度。 */
                if (!ret && x->device_info && command == SFILE_DEVICE_INFO) {
                    if (len != SFILE_ACK_BYTES + SFILE_DEVICE_INFO_BYTES) return -EBADMSG;                          /* 成功的设备信息回包必须完整，普通 ACK 长度不够。 */
                    memcpy(x->device_info, s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES, SFILE_DEVICE_INFO_BYTES);    /* 把设备查询附加区复制到上层持有的输出区。 */
                }
                if (x->http && command == SFILE_FINISH &&
                    (ret == SFILE_OK || ret == SFILE_HTTP_FAILED)) {
                    if (len != SFILE_HTTP_RESULT_HEAD + 2u) return -EBADMSG;                                        /* 通用 HTTP 的 FINISH 结果携带状态码和响应总字节数。 */
                    x->http->http_code = swap_u16(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES);                      /* 解析从机报告的 16 位 HTTP 返回码。 */
                    x->http->response_size = swap_u16(s_swap_rx + SFILE_HEADER + SFILE_HTTP_RESULT_HEAD);           /* 提取响应总长以安排后面的 HTTP_READ 分页。 */
                    if (x->http->response_size > SFILE_HTTP_BODY_MAX ||                                             /* 同时限制可分页的总长度，并要求协议成功时 HTTP 也为 2xx。 */
                        (ret == SFILE_OK && (x->http->http_code < 200 || x->http->http_code >= 300)))
                        return -EBADMSG;                                                                            /* 声称成功的 HTTP 状态或响应长度不可信，终止任务。 */
                }
                if (x->url && command == SFILE_FINISH) {
                    unsigned int code = 0;                                                                          /* 在内联内容不足以提取 HTTP 状态时保留零。 */
                    if (len >= SFILE_HTTP_RESULT_HEAD) {
                        uint8_t *body = s_swap_rx + SFILE_HEADER + SFILE_HTTP_RESULT_HEAD;                          /* 旧版内联响应紧跟 ACK 基本字段和 HTTP 状态。 */
                        unsigned int n = len - SFILE_HTTP_RESULT_HEAD, j;                                           /* 有效显示字节数不含结果头，j 为清理显示字符的索引。 */
                        code = swap_u16(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES);                                /* 从内联响应头读取 HTTP 状态。 */
                        for (j = 0; j < n; ++j)                                                                     /* 旧 URL 入口只为串口展示作可打印转换，通用分页另走无损路径。 */
                            if (body[j] < 32 || body[j] > 126) body[j] = ' ';                                       /* 将不可打印字节变成空格供这条旧日志显示。 */
                        body[n] = 0;                                                                                /* 在合法载荷边界内为 Shell 字符串补终止符。 */
                        shell_print(x->shell, "SPI HTTPS tid=%u http=%u response=%s",
                                    x->tid, code, (const char *)body);
                    }
                    if (ret == SFILE_OK && (code < 200 || code >= 300)) return -EBADMSG;                            /* 协议报成功却没有 2xx 时，不接受它作为交付成功。 */
                }
                return ret;                                                                                         /* 除需要继续查询的 BUSY 外，将明确的从机结果交还上层。 */
            }
            if (delivery_group) ++x->group_busy;                                                                    /* 组消费者仍在处理，记一次忙响应。 */
            verifying = true;                                                                                       /* 此后仅发 QUERY 读取进度，避免重复执行原命令。 */
        }
        if ((delivery_group || ((x->url || x->http) && command == SFILE_FINISH)) &&
            verifying && !x->half_pair) k_msleep(20);                                                               /* 在配对边界让线程短暂休眠，避免持续空轮询。 */
    } while (x->half_pair || (uint32_t)(k_uptime_get_32() - start) < timeout);                                      /* 总时限到期时若仅发了一半，仍要按协议填完该对。 */
    return -ETIMEDOUT;                                                                                              /* 控制操作到期且配对已完整，报告等待超时。 */
}

/* 仅在完整配对边界重建一次。发送重置对时可遇到旧应答；下一对两个槽都须返回本次匹配的 CRC 有效 ACK。恢复后当前操作仍失败。 */
/**
  * @brief  在已排空的配对边界执行一次协议级重同步。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @retval 0 验证双槽成功，-EPERM 为状态不允许，-EBADMSG 为复查错，其余为底层错误。
  * @note   先发 RESYNC/QUERY，再发两次 QUERY 核对两个新鲜 ACK 和 cookie；当前业务仍按失败结算。
  */
static int sfile_resync(struct sfile_sender *x)
{
    unsigned int i;
    uint16_t request;
    int ret, invalid = 0;                                                            /* invalid 独立锁存两个确认槽中任何一份不合格的情形。 */

    /* 确认没有半对或硬错误，再分配全新请求号和有界的恢复等待预算。 */
    if (!x->paired || x->half_pair || x->link_failed) return -EPERM;
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recoveries;                                            /* 仅有离线现场时累加实际进入恢复的次数。 */
#endif
    x->resync_active = true;                                                         /* 标记正在恢复，使内部交换不会再次进入普通失败流程。 */
    if (++x->request == 0) ++x->request;                                             /* 为新恢复握手选择非零请求号，排除之前的确认回复。 */
    request = x->request;                                                            /* 两次重置发送和两次复核使用同一请求。 */
    shell_print(x->shell, "SPI RESYNC begin tid=%u request=%u", x->tid, request);
    x->ready_timeout_ms = 35000u;                                                    /* 为对端停机重装及重发授权预留有限时限。 */
    for (i = 0; i < 2; ++i) {                                                        /* 按同一授权填满两个原槽，暂不以这两份迟到回复判定恢复。 */
        sfile_put32(s_swap_tx + SFILE_HEADER, SFILE_RESYNC_COOKIE);                  /* 命令块的有效载荷携带明确重同步标记。 */
        sfile_frame(s_swap_tx, SFILE_MASTER, i ? SFILE_QUERY : SFILE_LINK_RESYNC,    /* 先 RESYNC 后 QUERY 填满原授权对，此对可能仍读到旧响应。 */
                    x->tid, 0, request, i ? 0 : 4);
        ret = sfile_exchange(x);                                                     /* 交换一块，保留本次物理等待和 DMA 的错误。 */
        if (ret) return ret;                                                         /* 实际传输失败时没有试探第二种恢复流程。 */
    }
    /* 从机确认停机、重建并重新装载后才给下一次 READY。 */
    for (i = 0; i < 2; ++i) {                                                        /* 接下来两个槽均应携带从机重建后的新确认。 */
        sfile_frame(s_swap_tx, SFILE_MASTER, SFILE_QUERY, x->tid, 0, request, 0);    /* 用同一请求号发空载荷查询，取回每个已预装 ACK。 */
        ret = sfile_exchange(x);                                                     /* 读取本槽并推进配对边界。 */
        if (ret) return ret;                                                         /* 单槽传输失败就不能宣布两槽均已恢复。 */

        /* 下一授权上的两个槽都必须是匹配重置请求的新 ACK，逐项核对身份及 cookie。 */
        if (!sfile_valid(s_swap_rx, SFILE_SLAVE) || s_swap_rx[2] != SFILE_ACK ||
            sfile_u32(s_swap_rx + 4) != x->tid || sfile_u32(s_swap_rx + 8) ||
            swap_u16(s_swap_rx + 12) != request ||
            swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + 4u ||
            s_swap_rx[SFILE_HEADER] != SFILE_LINK_RESYNC || s_swap_rx[SFILE_HEADER + 1] ||
            sfile_u32(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES) != SFILE_RESYNC_COOKIE)
            invalid = 1;                                                             /* 记下本槽不合格，但把当前授权对的另一槽也完成。 */
    }
    x->ready_timeout_ms = 0;                                                         /* 恢复结束后恢复普通文件 READY 缺省时限。 */
    if (invalid) return -EBADMSG;                                                    /* 任何一份新 ACK 不符都不放行。 */
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recovered;                                             /* 两个新确认都通过才增加成功恢复数。 */
#endif
    x->invalid_replies = 0; x->resync_needed = x->resync_active = false;             /* 两槽均确认后才撤销重同步标记，原业务结果由外层保留。 */
    shell_print(x->shell, "SPI RESYNC PASS tid=%u; retry same file ID", x->tid);
    return 0;                                                                        /* 这表示链路可供下个任务使用。 */
}

#ifdef SPI_LINK_BOOT_SYNC
static bool sfile_boot_hard_fault;                                                   /* 驱动关闭失败锁存为硬故障，之后启动探测也不再发时钟。 */
/* 启动阶段先用三个单块：前两次读出启动通知，第三次读出携带此次挑战的 ACK。全部验证再进入配对，期间不触发业务。 */
/**
  * @brief  用三次单块传输验证从机的启动通知和本次挑战应答。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @param  failed_frame：[输出]当前正在尝试的帧号，1～3，供失败分类。
  * @retval 0 握手及最终 READY 成功，-EBADMSG 为通知或应答错误，其余为底层错误。
  * @note   前两次返回须为有效启动通知，第三次须回显本次事务、请求、挑战及 cookie。
  */
static int sfile_boot_attempt(struct sfile_sender *x, unsigned int *failed_frame)
{
    uint32_t challenge = k_cycle_get_32() | 1u;                                               /* 每次握手生成与当下周期相关的非零挑战，区分陈旧 ACK。 */
    unsigned int i;
    uint16_t request = ++x->request;                                                          /* 三笔启动操作共享一次请求号，防止应答串到上次尝试。 */
    int ret;
    x->paired = x->half_pair = false;                                                         /* 启动阶段先回到单块授权，暂不宣布双方配对。 */
    s_ready_expected = 1u;                                                                    /* 按启动信标的高电平开始观察 READY。 */
    s_ready_faulted = false;                                                                  /* 暂时允许本次探测，后续失败由收尾重新锁定。 */
    shell_print(x->shell, "SPI BOOT SYNC begin tid=%u", x->tid);

    /* 依次读两个预装启动通知和一个带新挑战的 ACK。每个响应都先验自己的 CRC。 */
    for (i = 0; i < 3u; ++i) {
        *failed_frame = i + 1u;                                                               /* 向上报告当前帧号，只有前两帧有特定恢复机会。 */
        sfile_put32(s_swap_tx + SFILE_HEADER, SFILE_BOOT_COOKIE);                             /* 启动标记位于第一个命令块的有效负载。 */
        sfile_frame(s_swap_tx, SFILE_MASTER, i ? SFILE_QUERY : SFILE_LINK_BOOT,               /* 第一块带启动 cookie，后两块以同一身份轮询。 */
                    x->tid, challenge, request, i ? 0 : 4);
        ret = sfile_exchange(x);                                                              /* 单块模式下每次交换都需要一份独立 READY 许可。 */
        if (ret) return ret;                                                                  /* 未能可靠完成实际收发时，调用方按硬件状态决定停机。 */
        if (!sfile_valid(s_swap_rx, SFILE_SLAVE)) return -EBADMSG;                            /* 三个返回块的文件头和 CRC 均须先通过。 */
        if (i < 2u) {

            /* 前两块是从机预装的启动通知，身份字段应为零，且带约定的版本标记。 */
            if (s_swap_rx[2] != SFILE_LINK_BOOT || sfile_u32(s_swap_rx + 4) ||
                sfile_u32(s_swap_rx + 8) || swap_u16(s_swap_rx + 12) ||
                swap_u16(s_swap_rx + 14) != 4u ||
                sfile_u32(s_swap_rx + SFILE_HEADER) != SFILE_BOOT_COOKIE) return -EBADMSG;    /* 旧数据或错误 cookie 都不能当作上电预填通知。 */

        /* 第三块必须是针对当前传输号、挑战及请求号的 ACK。 */
        } else if (s_swap_rx[2] != SFILE_ACK || sfile_u32(s_swap_rx + 4) != x->tid ||
                   sfile_u32(s_swap_rx + 8) != challenge || swap_u16(s_swap_rx + 12) != request ||
                   swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + 4u ||
                   s_swap_rx[SFILE_HEADER] != SFILE_LINK_BOOT || s_swap_rx[SFILE_HEADER + 1] ||
                   sfile_u32(s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES) != SFILE_BOOT_COOKIE)
            return -EBADMSG;                                                                  /* 第三块不匹配时从机可能已经配对，外层不会盲目再试。 */
    }
    ret = sfile_wait_ready(x);                                                                /* 等待从机完成第三块并重新准备接收。 */
    if (ret) return ret;                                                                      /* 若从机没有完成重装，即使前三个返回正确也不配对。 */
    sfile_pair_mode = x->paired = true;                                                       /* 最终重新就绪也得到确认后，提交双方配对模式。 */
    shell_print(x->shell, "SPI BOOT SYNC PASS tid=%u", x->tid);
    return 0;                                                                                 /* 首次或再次完整握手的结果由调用者判别。 */
}

/* 对端可在启动前两阶段超时后于稳定空闲且 DMA 已停止时重新装载。等待新的启动信标后才允许再试；第三块出错时双方可能已经切换配对，不做猜测性重试。 */
/**
  * @brief  执行启动握手，在可判定的早期帧错误后有限恢复。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @retval 0 首次成功，1 重握手成功但当前业务仍失败，负数为不可恢复错误。
  * @note   只接受前两帧的校验错误进入重试；静默 2200 ms 并重新观察启动信标后才允许再次发时钟。
  */
static int sfile_boot_sync(struct sfile_sender *x)
{
    unsigned int failed_frame = 0;                                             /* 记录出错的是三个启动阶段中的哪一帧。 */
    int ret = sfile_boot_attempt(x, &failed_frame);                            /* 首先取得一次完整尝试的实际返回和帧号。 */
    if (ret != -EBADMSG || x->link_failed || failed_frame > 2u) return ret;    /* 硬件错误或第三块失败时不重试，排除对端已切换模式的歧义。 */
    shell_error(x->shell, "SPI BOOT RECOVER wait frame=%u ret=%d; no clocks for 2200 ms",
                failed_frame, ret);
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recoveries;                                      /* 仅在允许重握手时累计恢复尝试。 */
#endif
    /* 中止早期握手后先等待对端回到启动状态；观察到新的信标才从第一个槽重新验证。 */
    k_msleep(2200);
    ret = swap_ready_boot_probe();                                             /* 重新看到完整启动信标才有下一次发时钟的依据。 */
    if (ret != 1) {
        shell_error(x->shell, "SPI BOOT RECOVER FAIL no fresh beacon ret=%d", ret);
        return ret < 0 ? ret : -EBADMSG;                                       /* 未看到新信标时保留 GPIO 错误或返回坏帧。 */
    }
    /* 第二次尝试更换挑战及请求号，仍逐帧核对两个通知和最终 ACK。 */
    ret = sfile_boot_attempt(x, &failed_frame);                                /* 再次完整执行三块握手，而非移位或就地修正坏帧。 */
    if (ret) {
        shell_error(x->shell, "SPI BOOT RECOVER FAIL retry frame=%u ret=%d", failed_frame, ret);
        return ret;                                                            /* 第二次失败直接结束，禁止无界反复试时钟。 */
    }
#ifdef P4_SPI_SOAK
    if (x->probe) ++x->probe->recovered;                                       /* 已看到新信标且严格握手成功才记录恢复成功。 */
#endif
    shell_print(x->shell, "SPI BOOT RECOVER PASS; current task FAIL, retry same file ID");
    return 1;                                                                  /* 用特殊成功值告知上层链路已恢复，但原任务必须结束。 */
}
#endif

/**
  * @brief  若只发了一半配对，则补一块 IDLE 结束这一对。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @retval 0 已处于完整边界或补齐成功，负值表示交换失败。
  * @note   在组确认或补传后保持两端 DMA 槽位一致。
  */
static int sfile_end_pair(struct sfile_sender *x)
{
    if (!x->half_pair) return 0;                                          /* 当前恰好在配对边界时无需补块。 */
    sfile_frame(s_swap_tx, SFILE_MASTER, SFILE_IDLE, x->tid, 0, 0, 0);    /* 用 IDLE 占用第二槽，保持双方对下一授权的认识相同。 */
    return sfile_exchange(x);                                             /* 使用正常交换路径补第二槽，包括见证字、CRC 和故障检查。 */
}

/**
  * @brief  按块号和块内偏移生成可重现的测试正文。
  * @param  context：[输入]此生成器不使用数据源，可为 NULL。
  * @param  index：[输入]零基数据块号。
  * @param  data：[输出]至少 len 字节的缓存。
  * @param  len：[输入]该块的实际字节数。
  * @param  repair：[输入]补发标志，此生成器忽略。
  * @retval 0 生成完毕。
  * @note   同一块首发和补发生成相同字节，无需保存完整文件。
  */
static int sfile_read_generated(void *context, uint32_t index, uint8_t *data,
                                unsigned int len, bool repair)
{
    unsigned int i;
    (void)context;                                                /* 生成数据完全由索引决定，不需要源句柄。 */
    (void)repair;                                                 /* 补传时按相同规则重建图案。 */
    for (i = 0; i < len; ++i)
        data[i] = (uint8_t)(index ^ (index >> 8) ^ (i * 29u));    /* 图案仅依赖块号与块内偏移，重发可精确重建。 */
    return 0;                                                     /* 已按请求填满本块。 */
}

struct sfile_file_source {
    struct fs_file_t file;                                        /* 只读文件系统句柄。 */
    uint32_t offset, position;                                    /* 源窗口基址与当前文件游标的绝对字节位置。 */
};

/**
  * @brief  按相对块号读出源文件范围的一块数据。
  * @param  context：[输入输出]含已打开文件、起始偏移和游标的 sfile_file_source。
  * @param  index：[输入]范围内的零基块号。
  * @param  data：[输出]至少 len 个字节。
  * @param  len：[输入]请求字节数。
  * @param  repair：[输入]补发标志，定位由实际偏移决定。
  * @retval 0 足量读出，-EIO 为定位失败或短读。
  * @note   顺序发送沿用文件位置，补发或跳块时按源起点重新定位。
  */
static int sfile_read_file(void *context, uint32_t index, uint8_t *data,
                           unsigned int len, bool repair)
{
    struct sfile_file_source *source = context;                                     /* 取得源文件句柄以及迄今记录的游标。 */
    uint32_t start = source->offset + index * SFILE_DATA_BYTES;                     /* 将协议块索引映射为本地文件绝对位置。 */
    (void)repair;                                                                   /* 是否需要重定位取决于当前位置，不依赖 repair 布尔值。 */
    if (source->position != start && fs_seek(&source->file, start, FS_SEEK_SET))    /* 顺序读取无需反复定位，回补早先的块才需要回退。 */
        return -EIO;                                                                /* 文件系统无法定位到块的绝对偏移。 */
    if (fs_read(&source->file, data, len) != (ssize_t)len) return -EIO;             /* 短读即失败，不能用残留数据补足正文。 */
    source->position = start + len;                                                 /* 用实际完整读出的字节数推进游标。 */
    return 0;                                                                       /* 缓冲中的字节可交给组帧逻辑。 */
}

/**
 * @brief          从指定数据源读取一块并封装发送，可注入首次 CRC 错误。
 * @param  x       [输入输出]当前会话的发送器、计数及链路状态。
 * @param  index   [输入]零基块号，位于当前正文范围内。
 * @param  repair  [输入]true 表示位图要求重发的块。
 * @retval         0 完成，其他值为取数或块交换错误。
 * @note           x->read_block 在 P4 请求中绑定 p4_spi_read，len 是本块有效字节数。首发计入 CRC32，位图要求补发时重读同一块并单独计数。
 */
static int sfile_send_data(struct sfile_sender *x, uint32_t index, bool repair)
{
    uint32_t offset = index * SFILE_DATA_BYTES;                                         /* 数据块序号转换成相对于本次正文起点的偏移。 */
    unsigned int len = x->size - offset;                                                /* 先取剩余文件长度，再限到单块载荷上限。 */
    uint8_t *data = s_swap_tx + SFILE_HEADER;                                           /* 直接把文件读入发送块的载荷区，减少一次复制。 */
    int ret;
    if (len > SFILE_DATA_BYTES) len = SFILE_DATA_BYTES;                                 /* 末块只发送剩余有效字节。 */
    ret = x->read_block(x->source, index, data, len, repair);                           /* 按全局块号从源文件回读或现场生成，首次与补发共用这一帧空间；主机无需缓存整组 48,000 字节。 */
    if (ret) return ret;                                                                /* 读取失败时不发送含残留字节的块。 */
    if (!x->source && repair && index == x->bad_block) x->repaired_bad_block = true;    /* 命中指定的生成数据坏块时记下已执行补发。 */
    if (!repair) x->crc = sfile_crc32(x->crc, data, len);                               /* 按原文件顺序累计 CRC32；同一块补发时跳过，整文件校验不会把重发的字节算第二遍。 */
    else ++x->resent;                                                                   /* 重发次数单列，避免重复把同一数据计入校验。 */
    sfile_frame(s_swap_tx, SFILE_MASTER, SFILE_DATA, x->tid, index, 0, len);            /* 封装本次数据块，数据 ACK 靠组结束控制取得。 */
    if (!x->source && !repair && index == x->bad_block)                                 /* 仅为生成数据的指定首发块破坏帧尾 CRC，测试缺块修复。 */
        s_swap_tx[SFILE_CRC_OFFSET] ^= 1u;                                              /* 翻转块尾 CRC 的一位，使接收端按缺块处理。 */
    return sfile_exchange(x);                                                           /* 发送完成后由组确认位图决定是否还要重发。 */
}

/**
  * @brief  以最多 100 个块为一组发送正文，按接收位图补缺。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @retval 0 所有组通过，-EBADMSG 为补传三轮仍缺块，其余来自数据或控制交换。
  * @note   复制位图后再发后续块，避免接收缓存被覆盖。
  */
static int sfile_send_body(struct sfile_sender *x)
{
    uint32_t blocks = (x->size + SFILE_DATA_BYTES - 1u) / SFILE_DATA_BYTES;    /* 对文件字节数向上取整，末块长度另由发送函数限制。 */
    uint32_t first = 0;                                                        /* 第一组从文件的零号块开始。 */

    /* 按组先发送全部数据，并把最后一对补齐；随后通过组尾控制应答确认从机实际收到的块。 */
    while (first < blocks) {
        uint8_t bitmap[SFILE_BITMAP];                                          /* 暂存本组 ACK 位图，后续 RX 缓冲会被复用。 */
        unsigned int count = sfile_group_count(blocks, first);                 /* 末组可能小于通常的一百块。 */
        unsigned int i, round;                                                 /* 区分组内块号 i 和已经进行的位图修复轮数。 */
        int ret;
        for (i = 0; i < count; ++i) {
            ret = sfile_send_data(x, first + i, false);                        /* 首轮发每一块，按原文件顺序累加 CRC。 */
            if (ret) return ret;
        }
        ret = sfile_end_pair(x);                                               /* 组尾不足一对时先填完流水，再切换到控制命令。 */
        if (ret) return ret;                                                   /* 补齐末槽失败时不能发下一条组确认。 */

        /* 每轮重新取得完整接收位图，只补仍缺的块，达到上限则中止本次文件传输。 */
        for (round = 0; ; ++round) {
            uint32_t started = k_uptime_get_32(), waited;                      /* 单独计量本轮等待组确认的毫秒数。 */
            ret = sfile_control(x, SFILE_GROUP_END, first, NULL, 0);           /* GROUP_END 的应答决定是否需要按位图补发。 */
            waited = k_uptime_get_32() - started;                              /* 取同一毫秒时基下的无符号差值。 */
            x->group_wait_ms += waited;                                        /* 累加各组及其补传确认的总耗时。 */
            if (waited > x->group_peak_ms) x->group_peak_ms = waited;          /* 记录单次组确认最慢值。 */
            if (ret == SFILE_OK) break;                                        /* 对端已完整接收本组，跳到下一组。 */
            if (ret != SFILE_MISSING) return ret;                              /* 只有明确缺块状态可用位图补发。 */
            if (round == 3u) return -EBADMSG;                                  /* 超过三轮补发仍缺块时停止。 */
            memcpy(bitmap, s_swap_rx + SFILE_HEADER + 2, sizeof(bitmap));      /* 保留本次 ACK 的 13 字节位图。每补一帧都会刷新 s_swap_rx，这份局部副本仍能指出后面缺哪些块。 */
            for (i = 0; i < count; ++i) {
                if (sfile_bit(bitmap, i)) continue;                            /* i 从本组零位起算，例如 i=17 对应 bitmap[2] 的 bit 1；只补零位对应的全局块 first+i。 */
                ret = sfile_send_data(x, first + i, true);                     /* 以相同全局块号重新读源并装 DATA。repair=true 使这个块不会在文件 CRC32 中多算一次。 */
                if (ret) return ret;
            }
            ret = sfile_end_pair(x);                                           /* 缺块数量可能为奇数，重查之前仍需补齐配对。 */
            if (ret) return ret;
        }
        ++x->groups;                                                           /* 本组已经得到明确成功。 */
        first += count;                                                        /* 收到本组 OK 才前移全局块号；48,001 字节的文件在首组 100 块完成后，第二组只剩块 100 的 1 字节。 */
    }
    return 0;                                                                  /* 所有物理数据块已分组确认。 */
}

/**
  * @brief  将 HTTP 方法、URL 和头字段描述按偏移分块交给从机。
  * @param  x：[输入输出]当前会话的发送器、计数及链路状态。
  * @param  buffer：[输出]至少 SFILE_DATA_BYTES 字节的暂存区。
  * @retval 0 全部确认，-EIO 为文件短读，其他值来自控制请求。
  * @note   规格来源可为内存或已定位到开头的文件，每段最多 480 字节。
  */
static int sfile_send_http_spec(struct sfile_sender *x, uint8_t *buffer)
{
    uint32_t offset = 0;                                                                     /* 规格偏移从方法行首个字节开始。 */
    while (offset < x->http->spec_size) {                                                    /* 只发送规格真实有效长度。 */
        unsigned int length = x->http->spec_size - offset;                                   /* 末块按剩余元信息字节数计算。 */
        int ret;
        if (length > SFILE_DATA_BYTES) length = SFILE_DATA_BYTES;                            /* 规格每段不超过一份有效载荷的容量。 */
        if (x->http->spec_data) memcpy(buffer, x->http->spec_data + offset, length);         /* 通用 P4 直接使用 RAM 中已签好的规格，文件命令从规格文件读取。 */
        else if (fs_read(&x->http->spec, buffer, length) != (ssize_t)length) return -EIO;    /* 文件型规格发生短读，不能继续发不完整的元数据。 */
        ret = sfile_control(x, SFILE_HTTP_META, offset, buffer, length);                     /* META 的 index 是规格内的字节偏移。 */
        if (ret) return ret;                                                                 /* 当前段未获确认，停止推进元信息偏移。 */
        offset += length;                                                                    /* 只在对端确认后转到下一段。 */
    }
    return 0;                                                                                /* 所有规格数据都已交给对端。 */
}

/**
 * @brief     按页读尽从机保留的 HTTP 响应。
 * @param  x  [输入输出]当前会话的发送器、计数及链路状态。
 * @retval    0 全部已收，-EBADMSG 为页长不匹配，其他值为控制、回调或写文件错误。
 * @note      每次 HTTP_READ 最多取 400 字节，offset 从响应正文开头算起。P4 用 receive 回调拼回业务缓冲，诊断命令可转为串口十六进制或写入文件。
 */
static int sfile_read_http_result(struct sfile_sender *x)
{
    uint32_t offset = 0;                                                               /* 响应从零偏移开始按页回取。 */

    /* 按已声明的总响应长度逐页发 HTTP_READ，逐页严格校验长度后交给对应输出。 */
    while (offset < x->http->response_size) {
        unsigned int length = x->http->response_size - offset;                         /* 末页可能小于协议规定的页长。 */
        int ret;
        if (length > SFILE_HTTP_PAGE_BYTES) length = SFILE_HTTP_PAGE_BYTES;            /* 限制单页正文长度，尾页可不足 400 字节。 */
        ret = sfile_control(x, SFILE_HTTP_READ, offset, NULL, 0);                      /* HTTP_READ 以响应内字节位置标识请求，不携正文。 */
        if (ret) return ret;                                                           /* 控制应答未通过时不能解析当前缓存。 */
        if (swap_u16(s_swap_rx + 14) != SFILE_ACK_BYTES + length) return -EBADMSG;     /* 匹配 ACK 后还须核对具体页长，以免接受截短响应。 */
        if (x->http->receive) {

            /* 优先交应用回调，P4 用该回调按顺序拼成有界的 JSON 响应。 */
            ret = x->http->receive(x->http->receive_context, offset,
                                  s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES, length);
            if (ret) return ret;                                                       /* 业务层拒绝当前页时停止，不宣告完整响应。 */
        } else if (x->http->console_result) {
            /* 每行十六进制表示至多 32 字节，保留二进制及非 ASCII 的响应原貌。 */
            static const char digits[] = "0123456789abcdef";
            unsigned int at;
            for (at = 0; at < length; at += 32u) {
                char hex[65];                                                          /* 每行不超过 32 个原始字节，需 64 个十六进制字符和结尾零。 */
                unsigned int j, n = length - at;
                if (n > 32u) n = 32u;                                                  /* 末行按真实剩余字节截取。 */
                for (j = 0; j < n; ++j) {                                              /* 将每个字节拆成两个十六进制字符，保留非文本响应内容。 */
                    uint8_t b = s_swap_rx[SFILE_HEADER + SFILE_ACK_BYTES + at + j];    /* 跳过协议头和 ACK 字段后取本页数据。 */
                    hex[j * 2u] = digits[b >> 4];                                      /* 高半字节放在该字节表示的第一位。 */
                    hex[j * 2u + 1u] = digits[b & 15u];                                /* 低半字节放在第二位。 */
                }
                hex[n * 2u] = 0;                                                       /* 只给用于打印的文本补零，保持原始响应长度。 */
                shell_print(x->shell, "SPI HTTP DATA tid=%u offset=%u hex=%s",
                            x->tid, offset + at, hex);
            }

        /* 没有回调也没有串口输出要求时，将本页原始字节写入结果文件。 */
        } else if (fs_write(&x->http->result, s_swap_rx + SFILE_HEADER + SFILE_ACK_BYTES,
                            length) != (ssize_t)length) return -EIO;                   /* 写文件时短写与直接失败都不能当作分页完成。 */
        offset += length;                                                              /* 回调、串口或文件输出成功后才推进分页偏移。 */
    }
    shell_print(x->shell, "SPI HTTP tid=%u http=%u bytes=%u", x->tid,
                (unsigned int)x->http->http_code, (unsigned int)x->http->response_size);
    x->http->response_complete = true;                                                 /* 所有页均成功后才将响应标记为完整。 */
    return 0;                                                                          /* 全部声明长度均已处理。 */
}

/**
 * @brief        在一次唤醒锁内完成主机文件会话、结果回读、链路收尾。
 * @param  sh    [输入]本次命令的有效 Shell 实例。
 * @param  x     [输入输出]当前会话的发送器、计数及链路状态。
 * @param  name  [输入]从机落盘名，可为 NULL 以选择 RAM 或交付命令。
 * @param  pair  [输入]是否要求配对传输。
 * @param  hz    [输入]SPI 请求时钟，单位 Hz。
 * @retval       0 请求完成且收尾通过，负值为参数、模式、存储、远端状态、握手或驱动失败。
 * @note         P4 一次请求经 START_HTTP、HTTP_META、DATA、GROUP_END、FINISH 传给 503，有响应正文再发 HTTP_READ。FINISH 回执丢失时这笔远端请求结果仍未知。
 */
static int sfile_run_context(const struct shell *sh, struct sfile_sender *x, const char *name, bool pair,
                     uint32_t hz)
{
    static uint32_t next_id;                                                                              /* 跨命令分配不同传输标识，防止匹配旧回复。 */
    struct spi_config config = {
        .frequency = hz,
        .operation = SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8),                /* 主机模式、模式 3、八位字宽配合对端从机。 */
    };
    uint8_t *params;                                                                                      /* 堆上的控制载荷及元数据临时区。 */
    uint32_t start, elapsed;                                                                              /* 会话起点与持续时间都以毫秒计量。 */
    size_t name_len = name ? strlen(name) : 0;                                                            /* 普通落盘名不包含 C 字符串的终止符。 */
    size_t url_len = x->url ? strlen(x->url) : 0;                                                         /* 只在旧式 URL 交付命令中携带网址本身。 */
    unsigned int param_len = 4u + name_len;                                                               /* 开始载荷默认含四字节正文长度，后接可选的目标文件名。 */
    uint8_t start_command = x->delivery_command ? x->delivery_command :
                            (pair ? SFILE_START_PAIR : (name ? SFILE_START : SFILE_START_RAM));           /* 根据设备查询、指定交付命令、RAM 或落盘选择开始码。 */
    int ret, wire_status = SFILE_OK, end_ret = 0;                                                         /* 分别保存本地结果、远端协议状态和清理结果。 */
    bool opened = false, finish_sent = false;                                                             /* 跟踪驱动是否成功打开以及 FINISH 是否已发，用来选择安全收尾路径。 */
#ifdef SPI_LINK_BOOT_SYNC
    int boot = 0;
    bool previous_session = s_swap_file_used;                                                             /* 记录探测前有无已使用的文件会话，用于区分首次启动和从机重启。 */
    if (sfile_boot_hard_fault) return -EPERM;                                                             /* 驱动上次关闭不确定时禁止本轮硬件操作。 */
#endif

    /* 先校验模式和参数，绑定 SPI2 并分配事务号。此阶段不向对端发时钟。 */
    if (x->url && (url_len < 10 || url_len > SFILE_HTTPS_URL_MAX)) return -EINVAL;
    x->shell = sh;
    if (name && !sfile_name_valid((const uint8_t *)name, name_len)) return -EINVAL;                       /* 拒绝会导致从机异常文件名或路径的输入。 */
    if (sfile_pair_mode && !pair) return -EPERM;                                                          /* 双方一旦进入配对就不能直接执行单块文件协议。 */
    x->paired = sfile_pair_mode;                                                                          /* 将跨命令的配对状态带入本次会话。 */
    if (s_ready_legacy_used || (s_ready_used && !s_swap_file_used)) return -EPERM;                        /* READY 曾被不兼容测试占用时拒绝混用。 */
    x->dev = device_get_binding(CONFIG_SPI_2_NAME);                                                       /* 绑定板级已注册的 SPI2 设备。 */
    if (!x->dev) return -ENODEV;
    x->crc = UINT32_MAX;                                                                                  /* 文件原始 CRC32 从全一开始，最终发送前再取反。 */
    if (!next_id) next_id = k_cycle_get_32();                                                             /* 首次用硬件周期数作为传输号的初始种子。 */
    if (++next_id == 0) ++next_id;                                                                        /* 分配非零传输号，零保留给无身份通知。 */
    x->tid = next_id;                                                                                     /* 本次所有控制和数据块共用此传输标识。 */

    /* 将控制载荷移到堆中，持唤醒锁并为网络命令准备硬件随机种子。 */
    params = k_heap_alloc(&sys_mem_pool, SFILE_DATA_BYTES, K_NO_WAIT);
    if (!params) {
        shell_error(sh, "SPI FILE request allocation failed");
        return -ENOMEM;                                                                                   /* 尚未持有唤醒锁或打开外设，内存不足直接返回。 */
    }
    sys_wake_lock(FULL_WAKE_LOCK);                                                                        /* 覆盖整个硬件会话，防止低功耗切换占用总线的引脚。 */
    start = k_uptime_get_32();                                                                            /* 从持锁后开始统计本次会话耗时。 */
    if (x->url || x->http) {
        ret = sfile_entropy_fill(params + 4, SFILE_HTTPS_SEED_BYTES);                                     /* 在首次发给从机前取得 48 字节硬件 TLS 种子。 */
        shell_print(sh, "SPI HTTPS SEED source=SE_TRNG bytes=%u ret=%d",
                    SFILE_HTTPS_SEED_BYTES, ret);
        if (ret) goto unlock;                                                                             /* 随机源失败，不能打开 SPI 发送不可靠的 TLS 种子。 */
    }
#ifdef SPI_LINK_BOOT_SYNC
    ret = swap_ready_begin_internal(true);                                                                /* 允许在普通故障锁定状态下只观察新启动信标。 */
    if (ret) goto unlock;
    boot = swap_ready_boot_probe();                                                                       /* 初次使用和重启探测均先被动读取 READY。 */
    if (boot < 0 || (!boot && (s_ready_faulted || !sfile_pair_mode))) {                                   /* 没有新信标时，只有已建立且未锁定的旧配对才可继续。 */
        ret = boot < 0 ? boot : -EPERM;                                                                   /* 保留探测的硬件错误，无信标且没有合法旧会话时返回禁止。 */
        shell_error(sh, "SPI BOOT SYNC unavailable ret=%d; no clocks sent", ret);
        if (!previous_session && !s_ready_faulted) {
            /* 初次探测只采样 GPIO，尚未打开 SPI、发时钟或消耗令牌。清理成功才允许重新探测。 */
            end_ret = swap_ready_end(0);                                                                  /* 初次被动探测没有消费令牌，只回收临时的 GPIO 配置。 */
            if (end_ret) {
                ret = end_ret;                                                                            /* 即使尚未发时钟，GPIO 清理失败也必须报告。 */
                shell_error(sh, "SPI BOOT probe GPIO cleanup failed ret=%d", ret);
            } else {
                s_ready_used = false;                                                                     /* 首次只探测未发时钟且清理成功，可撤销 READY 占用痕迹。 */
                shell_print(sh, "SPI BOOT probe released; retry when peer ready");
            }
            goto unlock;                                                                                  /* 此时已处理 READY，只释放堆和唤醒锁。 */
        }
        goto end_ready;                                                                                   /* 既有会话发生故障时保留锁存并走正常 READY 收尾。 */
    }
#else
    ret = swap_ready_begin();                                                                             /* 未启用启动同步时按普通 READY 状态规则开始。 */
#endif
    if (ret) goto unlock;
    s_swap_circular_used = s_swap_file_used = true;                                                       /* 该链路自此已使用文件协议，其他压测不能混入。 */
    sfile_probe_regs(x, 0);                                                                               /* 记录打开 SPI2 前的硬件状态。 */

    /* READY 已就绪后打开 SPI；如探测到启动信标，必须先核实两端实际的配对状态。 */
    ret = spi_acts_spi2_circular_open(x->dev, &config);
    sfile_probe_regs(x, 1);                                                                               /* 尝试打开后立即采样，便于与前一快照比较。 */
    if (ret) goto end_ready;                                                                              /* 打开失败不能关闭未取得的驱动会话，转而处理 READY。 */
    opened = true;                                                                                        /* 以此标记要求后续路径关闭已打开的驱动。 */
#ifdef SPI_LINK_BOOT_SYNC
    if (boot) {
        ret = sfile_boot_sync(x);                                                                         /* 检出新启动信标时一定先验证线上启动握手。 */
        if (ret < 0) {
            x->link_failed = true;                                                                        /* 不能确认启动握手时隔离链路。 */
            shell_error(sh, "SPI BOOT SYNC FAIL ret=%d; link quarantined", ret);
            goto close_driver;
        }
        if (previous_session || ret == 1) {                                                               /* 如果此前已有业务会话或本次为重握手，旧业务身份已失效。 */
            /* 对端重启会使旧任务身份失效，后续用同一文件 ID 重新 init 对账。 */
            ret = -EIO;                                                                                   /* 即使重建了双方同步，旧任务也不能继续标为成功。 */
            shell_error(sh, "SPI boot session recovered; current task FAIL, retry same file ID");
            goto close_driver;
        }
    }
#endif
#ifdef P4_SPI_DIAG
    if (x->http && x->http->diag) sfile_diag_regs(x->http->diag->initial);                                /* 可选 P4 诊断保存业务开始前的寄存器初值。 */
#endif
    /* 按照开始命令类型组装长度、目标名、种子、规格长度或诊断参数。 */
    memset(s_swap_tx, 0, SWAP_BLOCK_SIZE);
    sfile_put32(params, x->size);                                                                         /* 所有开始参数前四字节都编码本次正文长度。 */
    if (name_len) memcpy(params + 4, name, name_len);                                                     /* 只有落盘命令才把文件名字节附在长度之后。 */
    if (x->url || x->http) {
        if (x->http) {
            sfile_put16(params + SFILE_HTTPS_START_FIXED, x->http->spec_size);                            /* 通用 HTTP 随种子另带规格总长，后续经 META 分块下发。 */
            param_len = SFILE_HTTPS_START_FIXED + 2u;                                                     /* 通用 HTTP 的开始载荷比固定部分多一个 16 位元数据长度。 */
#ifdef P4_RADIO_TEST
            if (x->http->radio_test) {
                params[param_len++] = x->http->radio_test;                                                /* 仅射频验证包附加受控断网场景字节。 */
            }
#endif
        } else {
            memcpy(params + SFILE_HTTPS_START_FIXED, x->url, url_len);                                    /* 旧式 HTTPS 直接在种子之后携带目标 URL。 */
            param_len = SFILE_HTTPS_START_FIXED + url_len;                                                /* 旧 HTTPS 的实际载荷长度还包括 URL 字节数。 */
        }
    } else if (x->delivery_command && !x->device_info) {
        sfile_put32(params + 4, x->hold_ms);                                                              /* 交付路径附带接收端消费暂停的毫秒数。 */
        param_len = 8;                                                                                    /* 普通交付控制携带长度和暂停时长两项。 */
#ifdef P4_SPI_SOAK
        if (x->probe && x->probe->quiet && x->delivery_command == SFILE_START_DELIVERY) {
            sfile_put32(params + 8, SFILE_PROBE_COOKIE);                                                  /* 显式 cookie 让对端区分离线静默诊断和普通交付。 */
            param_len = 12;                                                                               /* 显式 QUIET 标记使本次开始载荷增加四字节。 */
        }
#endif
        if (x->delivery_command == SFILE_START_STREAM) {
            sfile_put32(params + 8, x->fail_at);                                                          /* 流式压力测试在约定字节偏移处注入失败。 */
            param_len = 12;                                                                               /* 流式注入的失败位置同样占用四字节扩展参数。 */
        }
    }

    /* 完成开始握手后，依次传送 HTTP 规格、分组正文及最终 CRC；设备查询单独结束。 */
    if (pair && !x->paired) ret = sfile_pair_begin(x, start_command, params, param_len);
    else ret = sfile_control(x, start_command, 0, params, param_len);                                     /* 已同步的配对或普通单块模式直接发送开始控制。 */
    if (x->url || x->http) {
        volatile uint8_t *seed = params + 4;
        unsigned int i;
        for (i = 0; i < SFILE_HTTPS_SEED_BYTES; ++i) seed[i] = 0;                                         /* 开始命令执行后擦除暂存参数里的种子。 */
    }
    if (!ret && x->http) ret = sfile_send_http_spec(x, params);                                           /* 通用 HTTP 先传完规格，再开始传正文。 */
    if (!ret && !x->device_info) ret = sfile_send_body(x);                                                /* 设备查询只交换控制信息，其他任务继续按组发送正文。 */
    if (!ret && !x->device_info && !x->source && x->bad_block != UINT32_MAX && !x->repaired_bad_block)    /* 合成坏块测试必须实际看到相应块被补传。 */
        ret = -EBADMSG;                                                                                   /* 声称测试成功却没实际修复指定坏块时拒绝通过。 */
    if (!ret && !x->device_info) {
        finish_sent = true;                                                                               /* 先记住 FINISH 已经发起，之后的超时不能随意取消。 */
        sfile_put32(params, x->crc ^ UINT32_MAX);                                                         /* 全部组确认后将累加状态取最终异或，作为整文件 CRC32。 */
        ret = sfile_control(x, SFILE_FINISH, 0, params, 4);                                               /* FINISH 使用四字节全文件 CRC 请求对端确认提交。 */
        if (ret > 0) wire_status = ret;                                                                   /* 暂存正的协议状态，后续结果回读失败也能诊断。 */
        if (x->http && (ret == SFILE_OK || ret == SFILE_HTTP_FAILED)) {
            int result_ret = sfile_read_http_result(x);                                                   /* 从机明确给出 HTTP 错误时也可以回读完整错误响应。 */
            if (result_ret) { x->http->transport_fault = true; ret = result_ret; }                        /* 响应回读或验证本身失败时标为传输故障。 */
        }
    }
    if (!ret && x->device_info) ret = sfile_control(x, SFILE_ABORT, 0, NULL, 0);                          /* 设备查询没有正文和 FINISH，显式 ABORT 回收本次上下文。 */
    if (ret > 0) {
        wire_status = ret;                                                                                /* 保存正的线上状态，再转为主机接口的负错误码。 */
        ret = -EIO;                                                                                       /* 文件 API 统一返回负的本地错误，同时另存线上状态。 */
    }
    /* FINISH 已发且确认丢失时，远端可能已经提交。只有尚未提交或对端明确返回失败时才发 ABORT。 */
    if (x->http && (x->resync_needed || x->link_failed || (ret && !wire_status)))
        x->http->transport_fault = true;                                                                  /* 留下包装层分类所需的异常标记，避免按普通网络错误重试。 */
    if (ret && !x->link_failed && !x->resync_needed && (!finish_sent || wire_status)) {                   /* 只有链路可信且业务尚可安全取消时才发 ABORT。 */
        if (sfile_control(x, SFILE_ABORT, 0, NULL, 0)) x->link_failed = true;                             /* 无法确认取消结果时，链路状态已不可信。 */
    }

    /* 链路允许时先恢复配对，再观察最后 READY。成功重同步仅恢复后续通信资格。 */
    if (x->resync_needed && !x->link_failed) {
        int recovery = sfile_resync(x);                                                                   /* 尝试有限重同步只决定后续任务是否可用，保留原任务失败。 */
        if (recovery) {
            x->link_failed = true;                                                                        /* 重同步失败将阻止后续普通事务。 */
            shell_error(sh, "SPI RESYNC FAIL ret=%d; link quarantined", recovery);
        }
    }
    if (!x->link_failed) {
        end_ret = sfile_wait_ready(x);                                                                    /* 清理前观察从机最终重新就绪，不消耗下一命令的令牌。 */
        if (end_ret) x->link_failed = true;                                                               /* 最终无法重新就绪是链路故障。 */
        if (!ret) ret = end_ret;                                                                          /* 没有早先错误时返回末次 READY 的失败。 */
    }
#ifdef SPI_LINK_BOOT_SYNC
close_driver:
#endif
    /* 按驱动、READY 输入、参数暂存、唤醒锁的归属逆向清理，保留最早业务或硬件错误。 */
    sfile_probe_regs(x, 2);
    end_ret = spi_acts_spi2_circular_close(x->dev);                                                       /* 关闭 SPI2 环形会话并记录两侧寄存器快照。 */
    sfile_probe_regs(x, 3);                                                                               /* 采集关闭后的寄存器供独立对照。 */
#ifdef SPI_LINK_BOOT_SYNC
    if (end_ret) sfile_boot_hard_fault = true;                                                            /* 驱动关闭失败是硬错误，不能依靠新信标解锁。 */
#endif
    if (end_ret) x->link_failed = true;                                                                   /* 即使业务数据确认成功，关闭驱动失败仍属于链路异常。 */
    if (!ret) ret = end_ret;                                                                              /* 清理故障仅在没有更早错误时替代返回码。 */
end_ready:
    end_ret = swap_ready_end(!opened || x->link_failed ? (ret ? ret : -EIO) : 0);                         /* 区分业务错误和不确定的硬件状态，再清理 READY 资源。 */
    if (!ret) ret = end_ret;                                                                              /* 保留原业务故障，正常业务则仍须确认 READY 清理成功。 */
unlock:
    {
        volatile uint8_t *p = params;                                                                     /* 通过逐字节写擦除本次控制参数。 */
        unsigned int i;
        for (i = 0; i < SFILE_DATA_BYTES; ++i) p[i] = 0;                                                  /* 擦除整个暂存区后返回应用堆。 */
        k_heap_free(&sys_mem_pool, params);                                                               /* 释放与开启会话前配对取得的常规堆块。 */
    }
    sys_wake_unlock(FULL_WAKE_LOCK);                                                                      /* 资源和线路操作结束，允许系统恢复正常休眠。 */
    if (x->http) {
        x->http->link_failed = x->link_failed;                                                            /* HTTP 适配层通过此字段区分链路故障和网络错误。 */
        x->http->wire_status = wire_status;                                                               /* 原始线上状态留给上层区分交付和 HTTP 失败。 */
        if (x->link_failed || x->resync_needed || end_ret) x->http->transport_fault = true;               /* 若曾重建或清理失败，显式传回上层，不能误判网络可重试。 */
    }

    /* 退出硬件会话后汇总吞吐、补发和等待峰值；若 FINISH 结果未知则单独提示。 */
    elapsed = k_uptime_get_32() - start;
    shell_print(sh, "SPI FILE tid=%u bytes=%u transfers=%u resent=%u ms=%u Bps=%u ready_us=%llu status=%u ret=%d",
                x->tid, x->size, x->transfers, x->resent, elapsed,
                !ret && elapsed ? (uint32_t)((uint64_t)x->size * 1000u / elapsed) : 0u,                   /* 使用实际有效文件字节数和持续毫秒计算吞吐。 */
                (unsigned long long)x->ready_us, wire_status, ret);
    if (!x->source) {
        shell_print(sh, "SPI FILE TEST groups=%u crc32=%08x %s",
                    x->groups, x->crc ^ UINT32_MAX, ret ? "FAIL" : "PASS");
        if (x->bad_block != UINT32_MAX)
            shell_print(sh, "SPI FILE TEST bad_block=%u repaired=%u",
                        x->bad_block, (unsigned int)x->repaired_bad_block);
    }

    /* 输出最长 READY 等待对应的命令和索引，帮助区分卡在数据、组确认还是远端提交。 */
    shell_print(sh, "SPI FILE READY max_us=%u after_type=%u after_index=%u timeout_ms=%u",
                x->peak_ready_us, x->peak_type, x->peak_index,
                x->peak_timeout_ms ? x->peak_timeout_ms : SFILE_READY_TIMEOUT_MS);
    if (x->url || x->http)
        shell_print(sh, "SPI HTTP FLOW tid=%u group_busy=%u group_wait_ms=%u group_peak_ms=%u",
                    x->tid, x->group_busy, x->group_wait_ms, x->group_peak_ms);
    if (!name)
        shell_print(sh, "SPI RAM READY data_us=%llu control_us=%llu",
                    (unsigned long long)x->data_ready_us,
                    (unsigned long long)(x->ready_us - x->data_ready_us));                                /* 用全部 READY 等待减去数据等待，得控制等待开销。 */
    if (pair) shell_print(sh, "SPI PAIR HOST pairs=%u blocks=%u", x->pairs, x->transfers);
    if (s_ready_faulted && ret == -ETIMEDOUT)
        shell_warn(sh, "SPI FILE READY timeout after_type=%u after_index=%u",
                   x->last_type, x->last_index);
    if (finish_sent && ret && !wire_status) {                                                             /* FINISH 已发送而没有收到明确状态时，远端提交结果未知。 */
        if (name) shell_warn(sh, "FINISH outcome unknown. Check spi_rx_%s on UMA503.", name);
        else shell_warn(sh, "%s FINISH result was not confirmed.", (x->url || x->http) ? "HTTPS" : "RAM");
    }
    return ret;                                                                                           /* 返回首个业务、传输或必要清理错误。 */
}

/* 常规诊断入口按值接收发送器。P4 直接传堆中发送器地址，节省串口 Shell 的栈空间。 */
/**
  * @brief  为现有诊断命令按值传入发送状态并执行文件会话。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  x：[输入]按值复制的发送器及其源和回调。
  * @param  name：[输入]可空的目标落盘名。
  * @param  pair：[输入]是否要求配对。
  * @param  hz：[输入]SPI 请求频率，Hz。
  * @retval 0 成功，负值沿用 sfile_run_context 的错误。
  * @note   上下文本身是副本，内部所指的文件、回调和响应对象由调用者持有。
  */
static int sfile_run(const struct shell *sh, struct sfile_sender x, const char *name,
                     bool pair, uint32_t hz)
{
    return sfile_run_context(sh, &x, name, pair, hz);    /* 将副本地址交由实际发送逻辑，旧诊断调用方式保持一致。 */
}

/**
  * @brief  只读打开本地文件，验证范围后将整个文件或选定字节交付从机。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  path：[输入]本地文件路径。
  * @param  name：[输入]普通复制模式的远端名，其他模式可空。
  * @param  url：[输入]旧 HTTPS 命令的 URL，其余模式可空。
  * @param  offset：[输入]本地文件起始字节偏移。
  * @param  length：[输入]网络请求体长度，单位字节。
  * @param  http：[输入输出]可空，通用 HTTP 规格与结果上下文。
  * @retval 0 成功，-EFBIG 或 -EINVAL 表示大小、范围无效，其他值为文件或传输错误。
  * @note   普通文件发送使用整文件；HTTP/HTTPS 使用 offset 和 length 的指定窗口。
  */
static int sfile_send_path(const struct shell *sh, const char *path, const char *name,
                           const char *url, uint32_t offset, uint32_t length,
                           struct sfile_http_files *http)
{
    struct sfile_file_source source = { .offset = offset, .position = offset };                                        /* 文件绝对基址和已知游标都从请求偏移起算。 */
    struct sfile_sender x = { .read_block = sfile_read_file, .source = &source, .url = url,
                              .http = http, .delivery_command = http ? SFILE_START_HTTP :
                                                               (url ? SFILE_START_HTTPS : 0) };
    off_t size;                                                                                                        /* 使用文件系统的偏移类型读取源文件字节数。 */
    int ret;

    /* 打开源文件并量出大小，检查相对范围后复位到所需位置，再把句柄作为块源。 */
    fs_file_t_init(&source.file);
    ret = fs_open(&source.file, path, FS_O_READ);                                                                      /* 只读打开用户指定的本地源。 */
    if (ret) return ret;
    ret = fs_seek(&source.file, 0, FS_SEEK_END);                                                                       /* 通过定位到文件末尾让 fs_tell 返回其长度。 */
    if (!ret) {
        size = fs_tell(&source.file);                                                                                  /* 通过文件尾位置获得完整大小，随后验证请求范围。 */
        if (size < 0 || (uint64_t)size > SFILE_MAX_SIZE) ret = -EFBIG;                                                 /* 负的当前位置或超过 31 位的文件大小无法编码。 */
        else if (offset > (uint32_t)size || length > (uint32_t)size - offset)                                          /* 用剩余字节数检查长度，避免计算 offset+length 的回绕。 */
            ret = -EINVAL;                                                                                             /* 指定窗口超出文件实际范围。 */
        else {
            x.size = (url || http) ? length : (uint32_t)size;                                                          /* 网络模式只发选定范围，普通落盘发送整份文件。 */
            ret = fs_seek(&source.file, offset, FS_SEEK_SET);                                                          /* 读取正文前必须从选定的绝对偏移开始。 */
            if (!ret) ret = sfile_run(sh, x, name, url || http, (url || http) ? SFILE_HTTPS_HZ : SFILE_DEFAULT_HZ);    /* 网络交付选择配对和专用时钟，普通落盘选默认时钟。 */
        }
    }
    fs_close(&source.file);                                                                                            /* 任一进入文件传输的分支结束后关闭源句柄。 */
    return ret;
}

/**
  * @brief  从本地文件系统读取整文件并在从机按指定名称落盘。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]本地路径、远端文件名。
  * @retval 0 完成，-EINVAL 为参数错误，其他负数为文件或传输错误。
  * @note   使用普通单块文件模式和缺省请求频率。
  */
static int shell_spi2file(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc != 3) return -EINVAL;                                     /* 文件复制要求本地源路径和远端名两项。 */
    return sfile_send_path(sh, argv[1], argv[2], NULL, 0, 0, NULL);    /* 普通落盘命令不设置 URL 和 HTTP 结果上下文。 */
}

/**
  * @brief  把本地文件指定字节范围作为 HTTPS PUT 正文。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]本地路径、偏移、长度、目的 URL。
  * @retval 0 完成，负值为参数、文件、传输或远端错误。
  * @note   调用前检查十进制偏移、非零长度及文件大小上限。
  */
static int shell_spi2putfile(const struct shell *sh, size_t argc, char *argv[])
{
    unsigned long offset, length;
    char *end;

    if (argc != 5 || argv[2][0] < '0' || argv[2][0] > '9' ||
        argv[3][0] < '0' || argv[3][0] > '9') return -EINVAL;
    offset = strtoul(argv[2], &end, 10);                                                             /* 起点和长度分别完整解析，调用层不接受符号或尾随字符。 */
    if (*end || offset > SFILE_MAX_SIZE) return -EINVAL;                                             /* 拒绝多余字符及不可表示的绝对偏移。 */
    length = strtoul(argv[3], &end, 10);                                                             /* 将要求交付的正文长度作为十进制字节数读取。 */
    if (*end || !length || length > SFILE_MAX_SIZE) return -EINVAL;                                  /* 不接受空正文、尾随字符或超大长度。 */
    return sfile_send_path(sh, argv[1], NULL, argv[4], (uint32_t)offset, (uint32_t)length, NULL);    /* 将已核对的范围以及命令传入的目标 URL 交给公共文件源路径。 */
}

/* 请求规格文件包含方法、URL 和原始 HTTP 头，Shell 这里只需要输入本地路径和文件范围。 */
/**
  * @brief  从规格文件和源文件范围构建 HTTP 请求，并保存分页响应。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]规格路径、源路径、字节偏移、长度及新结果路径。
  * @retval 0 完成，-EEXIST 为结果已存在，其他负值为参数、文件或传输错误。
  * @note   规格和源文件只读；新结果文件可能已包含故障前写入的响应页。
  */
static int shell_spi2http(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_http_files http = {0};                                                           /* 起始没有结果、回调和诊断标记，只安装后面打开的文件。 */
    struct fs_dirent existing;                                                                    /* 用于检查结果路径是否已经存在的文件系统元数据缓存。 */
    unsigned long offset, length;
    off_t spec_size;                                                                              /* 规格的文件系统长度先保留原生类型，再核对线上容量。 */
    char *end;
    int ret, close_ret;

    if (argc != 6 || argv[3][0] < '0' || argv[3][0] > '9' ||
        argv[4][0] < '0' || argv[4][0] > '9') return -EINVAL;
    offset = strtoul(argv[3], &end, 10);
    if (*end || offset > SFILE_MAX_SIZE) return -EINVAL;                                          /* 拒绝不完整或超限的源偏移。 */
    length = strtoul(argv[4], &end, 10);
    if (*end || !length || length > SFILE_MAX_SIZE) return -EINVAL;                               /* 正文长度必须是本协议支持的非零值。 */

    /* 现有结果文件须保留，先检查输出路径，再打开规格并确认长度。 */
    ret = fs_stat(argv[5], &existing);
    if (ret != -ENOENT) return ret ? ret : -EEXIST;                                               /* 只有确实不存在时才允许创建新结果文件。 */
    fs_file_t_init(&http.spec);                                                                   /* 初始化只读规格句柄。 */
    fs_file_t_init(&http.result);                                                                 /* 初始化新结果文件句柄。 */
    ret = fs_open(&http.spec, argv[1], FS_O_READ);                                                /* 以只读方式打开用户指定的规格文件。 */
    if (ret) return ret;
    ret = fs_seek(&http.spec, 0, FS_SEEK_END);                                                    /* 定位文件尾以取得元数据总长度。 */
    if (ret) goto close_spec;
    spec_size = fs_tell(&http.spec);                                                              /* 规格文件不能为空或超过传输层元数据总上限。 */
    if (spec_size <= 0 || spec_size > (off_t)SFILE_HTTP_META_MAX) {
        ret = -EINVAL;
        goto close_spec;
    }
    http.spec_size = (uint16_t)spec_size;                                                         /* 通过上限检查后规格长度可安全装入线上 16 位字段。 */
    ret = fs_seek(&http.spec, 0, FS_SEEK_SET);                                                    /* 从文件开头顺序传规格，而非从刚测得的文件末尾读。 */
    if (ret) goto close_spec;

    /* 开始范围传输后优先保留业务错误；成功时仍核对结果文件关闭状态。 */
    ret = fs_open(&http.result, argv[5], FS_O_CREATE | FS_O_WRITE);
    if (ret) goto close_spec;
    ret = sfile_send_path(sh, argv[2], NULL, NULL, (uint32_t)offset, (uint32_t)length, &http);    /* 交付指定源文件范围，结果页交给刚创建的结果文件。 */
    close_ret = fs_close(&http.result);                                                           /* 优先保留传输错误，传输成功时仍检查结果文件收尾。 */
    if (!ret) ret = close_ret;                                                                    /* 正常传输还必须成功关闭结果句柄。 */
close_spec:
    fs_close(&http.spec);                                                                         /* 收尾时也释放只读规格文件。 */
    return ret;
}

/**
  * @brief  校验块数和可选坏块编号，执行生成数据的诊断传输。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  blocks_arg：[输入]十进制块数 1～10000。
  * @param  bad_arg：[输入]可空的十进制坏块编号，从零开始。
  * @param  name：[输入]可空的从机落盘名。
  * @param  pair：[输入]是否使用配对。
  * @param  hz：[输入]请求时钟，Hz。
  * @retval 0 成功，-EINVAL 为范围或格式错误，其他负数来自文件会话。
  * @note   坏块只在首次发送时破坏 CRC，再检查位图是否驱动对应补发。
  */
static int sfile_run_generated(const struct shell *sh, const char *blocks_arg,
                               const char *bad_arg, const char *name, bool pair, uint32_t hz)
{
    struct sfile_sender x = { .read_block = sfile_read_generated, .bad_block = UINT32_MAX };    /* 生成数据取代真实文件，默认没有指定坏块。 */
    unsigned long blocks, bad;
    char *end;
    if (blocks_arg[0] < '0' || blocks_arg[0] > '9') return -EINVAL;
    blocks = strtoul(blocks_arg, &end, 10);                                                     /* 块数为十进制，限制到一万块以控制诊断规模。 */
    if (*end || !blocks || blocks > 10000u) return -EINVAL;                                     /* 块数必须在 1 至 10000 以内且不含尾随字符。 */
    if (bad_arg) {
        if (bad_arg[0] < '0' || bad_arg[0] > '9') return -EINVAL;                               /* 坏块位置使用非负十进制且必须从数字开头。 */
        bad = strtoul(bad_arg, &end, 10);                                                       /* 解析用户指定的零基坏块号。 */
        if (*end || bad >= blocks) return -EINVAL;                                              /* 注入索引从零起，必须落在本次生成的有效块内。 */
        x.bad_block = (uint32_t)bad;                                                            /* 本次发送器只在这个块首次发送时破坏 CRC。 */
    }
    x.size = (uint32_t)blocks * SFILE_DATA_BYTES;                                               /* 生成模式每个数据块都是完整的 480 字节。 */
    return sfile_run(sh, x, name, pair, hz);                                                    /* 使用此前选择的 RAM/落盘目标及单块/配对模式运行。 */
}

/**
  * @brief  生成按块可复算的文件用于验证从机写入和缺块补传。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]块数、目标文件名、可选坏块号。
  * @retval 0 成功，负值来自参数或文件会话。
  * @note   远端有文件名，命令可选择一次破坏指定块 CRC。
  */
static int shell_spi2filetest(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc != 3 && argc != 4) return -EINVAL;
    return sfile_run_generated(sh, argv[1], argc == 4 ? argv[3] : NULL, argv[2], false,    /* 有目标名时让从机执行落盘诊断。 */
                               SFILE_DEFAULT_HZ);
}

/**
  * @brief  用生成数据验证不落盘的单块授权路径。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]argv[0] 为子命令名；argv[1] 是十进制块数字符串；可选的 argv[2] 是从零起算的注错块号。
  * @retval 0 成功；-EINVAL 表示参数项数、数字格式或范围错误；其他负值来自文件会话。
  * @note   可指定一块首次发送时破坏 CRC，验证接收位图和补传。
  */
static int shell_spi2ram(const struct shell *sh, size_t argc, char *argv[])
{
    /* 输入 app spi2ram 8 时，根命令已被 Shell 消耗，本函数收到两项：spi2ram 和 8。 */
    if (argc != 2 && argc != 3) return -EINVAL;

    /* 下层解析并校验十进制参数；省略坏块号时传空，目标文件名为空时对端在 RAM 中接收。 */
    return sfile_run_generated(sh, argv[1], argc == 3 ? argv[2] : NULL, NULL, false,    /* 只用从机 RAM 收块，按单块授权。 */
                               SFILE_DEFAULT_HZ);
}

/**
  * @brief  用 RAM 数据运行每次授权两块的文件传输。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]块数、可选坏块号及可选 --hz 和频率。
  * @retval 0 成功，负值为参数、链路或远端错误。
  * @note   可选 --hz 设置请求频率，驱动再检查实际时钟能否满足。
  */
static int shell_spi2rampair(const struct shell *sh, size_t argc, char *argv[])
{
    unsigned long hz = SFILE_DEFAULT_HZ;                                               /* 无频率选项时沿用常规文件请求时钟。 */
    if (argc >= 4 && strcmp(argv[argc - 2], "--hz") == 0) {
        const char *arg = argv[argc - 1];                                              /* 可选 --hz 后的最后一项是实际频率文本。 */
        char *end;
        if (arg[0] < '0' || arg[0] > '9') return -EINVAL;                              /* 第一位须为数字，排除负号或空值。 */
        hz = strtoul(arg, &end, 10);
        if (*end || hz < 100000u || hz > 8000000u) return -EINVAL;                     /* 限制命令允许的频率范围且要求完整解析。 */
        argc -= 2;                                                                     /* 去掉已解析的两个频率选项，再按普通块数及坏块参数处理。 */
    }
    if (argc != 2 && argc != 3) return -EINVAL;
    return sfile_run_generated(sh, argv[1], argc == 3 ? argv[2] : NULL, NULL, true,    /* 指定配对模式，每次 READY 提供两个 512 字节槽。 */
                               (uint32_t)hz);
}

/**
  * @brief  构造流式或整组交付诊断，在指定延迟和故障位置运行。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]总字节数、保持毫秒数、可选注入位置。
  * @param  command：[输入]SFILE_START_STREAM 或 SFILE_START_DELIVERY。
  * @retval 0 成功，负值为参数、传输或远端交付错误。
  * @note   两种模式的注入单位不同，流式按字节偏移，普通交付按坏块号。
  */
static int sfile_run_delivery(const struct shell *sh, size_t argc, char *argv[], uint8_t command)
{
    struct sfile_sender x = {
        .read_block = sfile_read_generated, .bad_block = UINT32_MAX,
        .fail_at = UINT32_MAX, .delivery_command = command,                                     /* 用哨值关闭两个可能的故障注入位置。 */
    };
    unsigned long size, hold_ms, inject;
    char *end;

    /* 交付诊断的大小和保持时长有界；可选故障位置按流式字节或普通块号解释。 */
    if (argc != 3 && argc != 4) return -EINVAL;
    if (argv[1][0] < '0' || argv[1][0] > '9' || argv[2][0] < '0' || argv[2][0] > '9')
        return -EINVAL;
    size = strtoul(argv[1], &end, 10);
    if (*end || !size || size > 10000u * SFILE_DATA_BYTES) return -EINVAL;                      /* 以字节数限制生成正文，末块可以小于协议载荷。 */
    hold_ms = strtoul(argv[2], &end, 10);
    if (*end || hold_ms > SFILE_DELIVERY_MAX_HOLD_MS) return -EINVAL;                           /* 远端保持时间不能超过线协议上限。 */
    if (argc == 4) {
        if (argv[3][0] < '0' || argv[3][0] > '9') return -EINVAL;
        inject = strtoul(argv[3], &end, 10);                                                    /* 可选注入位置按十进制数读取。 */
        if (*end) return -EINVAL;                                                               /* 尾随字符意味着位置参数有歧义。 */
        if (command == SFILE_START_STREAM) {
            if (inject >= size) return -EINVAL;                                                 /* 流式失败位置必须早于正文末尾。 */
            x.fail_at = (uint32_t)inject;                                                       /* 流式模式的失败位置按正文内的字节偏移计算。 */
        } else {
            if (inject >= (size + SFILE_DATA_BYTES - 1u) / SFILE_DATA_BYTES) return -EINVAL;    /* 坏块编号必须小于向上取整后的数据块数。 */
            x.bad_block = (uint32_t)inject;                                                     /* 普通交付的故障位置按块编号计算。 */
        }
    }
    x.size = (uint32_t)size;                                                                    /* 保存本次要交付的正文总字节数。 */
    x.hold_ms = (uint32_t)hold_ms;                                                              /* 保存请求对端暂停消费的毫秒数。 */
    return sfile_run(sh, x, NULL, true, 8000000u);                                              /* 配对传输请求 8 MHz，接收端按给定保持时间施加背压。 */
}

/**
  * @brief  执行整组消费和背压的 RAM 交付诊断。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]总字节数、保持毫秒数、可选坏块号。
  * @retval 0 成功，负值为参数、链路或交付错误。
  * @note   申请 8 MHz，允许指定组交付保持时间和坏块。
  */
static int shell_spi2deliver(const struct shell *sh, size_t argc, char *argv[])
{
    return sfile_run_delivery(sh, argc, argv, SFILE_START_DELIVERY);    /* 普通交付路径可通过坏块检验位图补发。 */
}

/**
  * @brief  验证按流消费及指定字节位置失败时的错误回传。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]总字节数、保持毫秒数、可选失败字节偏移。
  * @retval 0 成功，负值为参数、链路或远端交付错误。
  * @note   申请 8 MHz，保持时限受线上协议约束。
  */
static int shell_spi2stream(const struct shell *sh, size_t argc, char *argv[])
{
    return sfile_run_delivery(sh, argc, argv, SFILE_START_STREAM);    /* 流式交付路径允许模拟消费者在某字节位置失败。 */
}

/**
  * @brief  生成指定长度正文，通过从机向命令给定的 URL 执行 HTTPS PUT。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]正的正文长度（字节）和 HTTPS URL。
  * @retval 0 成功，负值为大小、链路、设备或远端错误。
  * @note   通过 SFILE_START_HTTPS 将种子和 URL 一起传给从机。
  */
static int shell_spi2put(const struct shell *sh, size_t argc, char *argv[])
{
    struct sfile_sender x = {
        .read_block = sfile_read_generated, .bad_block = UINT32_MAX,
        .delivery_command = SFILE_START_HTTPS,                                /* 旧式 PUT 通过开始帧直接传 URL，正文由确定性生成器提供。 */
    };
    unsigned long size;
    char *end;

    if (argc != 3 || argv[1][0] < '0' || argv[1][0] > '9') return -EINVAL;
    size = strtoul(argv[1], &end, 10);
    if (*end || !size || size > 10000u * SFILE_DATA_BYTES) return -EINVAL;    /* 限制合成 PUT 正文的总数据量并排除空请求。 */
    x.size = (uint32_t)size;                                                  /* 本次生成器恰好发送此字节数。 */
    x.url = argv[2];                                                          /* 在本次同步 Shell 命令期间借用目标 URL 参数。 */
    return sfile_run(sh, x, NULL, true, SFILE_HTTPS_HZ);
}

#include "spi_link_put_probe.h"                                               /* 规格分段探针复用本文件已定义的发送器与 HTTP 元数据接口。 */
#ifdef P4_SPI_SOAK
#include "spi_link_soak.h"                                                    /* 仅诊断构建启用三阶段离线 SPI 压测。 */
#endif

#endif
