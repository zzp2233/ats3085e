/* 将板端资源文件、片上随机源及 UMA503 的设备查询和 HTTP 服务绑定到 P4 状态机；仅诊断资源构建启用。 */
#ifndef SPI_LINK_P4_H
#define SPI_LINK_P4_H
/* 只有带外部验证资源和配置的 P4 固件才构建本适配层。 */
#ifdef P4_EMBEDDED_VALIDATION
#include "p4_upload.h"                               /* 业务状态机、录音描述及平台回调约定。 */
#include "p4_fixture.h"                              /* 配套构建提供录音元数据与设备签名所需配置。 */
#include "p4_fault.h"                                /* 每次命令独立的测试模式与触发位图。 */
#include <sdfs.h>                                    /* 通过 SDFS 读取 NOR 资源分区内的录音。 */

/* P4 验证包可选择时钟对照；默认 4 MHz，请求 2.3 MHz 时使用 32 MHz HOSC 的 /14（约 2.286 MHz）。 */
#ifndef P4_SPI_TEST_HZ
#define P4_SPI_TEST_HZ SFILE_HTTPS_HZ
#endif
/* 拒绝未经此 P4 时钟对照路径支持的其他请求频率。 */
#if P4_SPI_TEST_HZ != 2300000u && P4_SPI_TEST_HZ != 4000000u
#error Unsupported P4 SPI test clock
#endif

/* 此适配层同步运行在串口 Shell 线程，与 P3 诊断串行执行，避免另分配常驻任务栈。 */
struct p4_spi_source {
    const uint8_t *body;                             /* 内存型 POST 的正文；录音 PUT 从 record 回读。 */
    const struct p4_record *record;                  /* 指向可重读的源录音或选定分片。 */
    bool failed;                                     /* 一旦底层读取失败，退出时按源错误分类。 */
};
struct p4_spi_context {
    const struct shell *shell;                       /* 当前执行命令的串口 Shell。 */
    char *response;                                  /* 由业务层提供的响应收集空间。 */
    unsigned int capacity, used;                     /* 容量与已按连续偏移接收的字节数。 */
    bool response_error;                             /* 回调发现乱序、重复、越界时置位。 */
    struct p4_fault fault;                           /* 当前命令的故障配置及一次性触发记录。 */
    bool radio_sent;                                 /* 避免结果不明确时重复要求从机断开射频。 */
#ifdef P4_SPI_WITNESS_TEST
    bool witness_once;                               /* 仅在专用验证构建破坏一次设备查询配对的见证字。 */
#endif
    /* 与 P4 工作区同在堆上，避免把传输上下文压入 2 KiB Shell 栈。 */
    struct p4_spi_source source;                     /* 当前业务请求的数据源，供文件协议按块回读。 */
    struct sfile_http_files http;                    /* 当前 HTTP 规格、状态和响应回调。 */
    struct sfile_sender sender;                      /* 指针形式复用文件发送器，减少深调用栈中的副本。 */
    uint8_t device_info[SFILE_DEVICE_INFO_BYTES];    /* 设备查询的定长 20 字节临时响应。 */
};

/**
  * @brief  在开启栈初始化和栈信息时查询当前 Shell 线程剩余栈。
  * @param  c：[输入]包含 Shell 实例的任务上下文。
  * @param  stage：[输入]当前业务阶段名。
  * @retval 无。
  * @note   使用的是 SDK 的栈水位统计；未启用相应配置时无输出。
  */
static void p4_spi_stack(struct p4_spi_context *c, const char *stage)
{
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
    size_t unused;                                              /* 在当前任务栈中仍未使用的字节数。 */
    if (!k_thread_stack_space_get(k_current_get(), &unused))    /* 只有内核成功返回剩余栈字节数才打印。 */
        shell_print(c->shell, "P4 STACK %s unused=%u", stage, (unsigned int)unused);
#else
    (void)c; (void)stage;                                       /* 未配置水位检测时无需访问运行线程。 */
#endif
}

/**
 * @brief           将一页响应按连续偏移放入 P4 响应缓冲。
 * @param  context  [输入输出]本次请求的 p4_spi_context。
 * @param  offset   [输入]HTTP_READ 读回的响应正文偏移，必须等于已收字节数 used。
 * @param  data     [输入]至少 size 字节的分页数据。
 * @param  size     [输入]页内字节数。
 * @retval          0 成功，-EFBIG 为页偏移不连续或容量不足。
 * @note            503 向主机分页返回同一笔 HTTP 响应。调用者在 capacity 之外预留一字节补零，交给 JSON 解析器的字节数始终以 used 为准。
 */
static int p4_spi_receive(void *context, uint32_t offset, const uint8_t *data, unsigned int size)
{
    struct p4_spi_context *c = context;                                                /* 取回保存响应缓冲和游标的适配上下文。 */
    if (offset != c->used || offset > c->capacity || size > c->capacity - offset) {    /* 只接受从 used 开始的下一页，减法检查剩余容量。 */
        c->response_error = true;                                                      /* 锁存分页协议错误，避免被传输层状态覆盖。 */
        return -EFBIG;                                                                 /* 拒绝重复、乱序或超长页面。 */
    }
    memcpy(c->response + offset, data, size);                                          /* 复制当前页到已检查的连续区间。 */
    c->used += size;                                                                   /* 记录接收进度，供下一页核对起点。 */
    c->response[c->used] = 0;                                                          /* 每页收完都保留结尾零，实际数据长度仍看 used。 */
    return 0;                                                                          /* 此页已成功交给 P4 引擎的响应区。 */
}

/**
 * @brief           根据 SPI 协议块号回读当前 POST 正文或 PUT 分片。
 * @param  context  [输入输出]保存内存正文或录音描述的源上下文。
 * @param  index    [输入]本次 HTTP 正文内的零基 SPI 块号，每块至多 480 字节。
 * @param  data     [输出]至少 size 字节的缓冲。
 * @param  size     [输入]本块实际字节数。
 * @param  repair   [输入]补传标志，读取位置与首发相同。
 * @retval          0 成功，-EINVAL 为读取范围越界，其他失败来自录音回调。
 * @note            PUT 先用块号算出分片内偏移，再由 part_read 加上云端指定的分片起点。补传沿同一路径重读，源读取失败会置 source.failed。
 */
static int p4_spi_read(void *context, uint32_t index, uint8_t *data,
                        unsigned int size, bool repair)
{
    (void)repair;                                                                                    /* 补发回读同一位置，因此无需为 repair 单设数据源。 */
    struct p4_spi_source *source = context;                                                          /* 识别当前请求是内存 POST 还是录音分片。 */
    if (source->record) {
        uint32_t offset = index * SFILE_DATA_BYTES;                                                  /* SPI 文件队列给的是块号，先按每块 480 字节折算偏移。 */
        int ret;                                                                                     /* 记录源范围检查或读取回调的结果。 */
        if (offset > source->record->size || size > source->record->size - offset) ret = -EINVAL;    /* 使用剩余字节数判断本块是否超出所选分片。 */
        else ret = source->record->read(source->record->source, offset, data, size);                 /* 录音可由资源文件或分片窗口提供，通过其回调读取。 */
        if (ret) source->failed = true;                                                              /* 让传输层退出时能够上报源错误。 */
        return ret;                                                                                  /* 保留本次范围读取的具体结果。 */
    }
    memcpy(data, source->body + index * SFILE_DATA_BYTES, size);                                     /* 内存型 POST 在同步调用期间保持有效，直接复制相应块。 */
    return 0;                                                                                        /* 内存正文的指定片段已拷进发送缓冲。 */
}

/**
  * @brief  把一次 P4 请求映射为文件协议 HTTP 事务，并区分源、协议、链路和网络错误。
  * @param  context：[输入输出]调用期间有效的 p4_spi_context。
  * @param  spec：[输入]以零结束的方法、URL 和头字段规格。
  * @param  body：[输入]内存正文；采用录音回读时为 NULL。
  * @param  record：[输入]可重复回读的录音分片；内存正文时为 NULL。
  * @param  length：[输入]正文长度，单位字节。
  * @param  response：[输出]至少 capacity+1 字节的响应目标，额外一字节用于补零。
  * @param  capacity：[输入]可接收的响应字节数，不含结尾零。
  * @param  received：[输出]实际接收字节数。
  * @param  http_code：[输出]HTTP 状态码，未获得时为零。
  * @retval 0 表示完整 2xx 响应及正常收尾，负 P4_ERR_* 表示错误类别。
  * @note   不在 SPI 适配层重放请求。已恢复的 CRC/RESYNC 故障仍使本次业务失败。
  */
static int p4_spi_request(void *context, const char *spec, const uint8_t *body,
                           const struct p4_record *record, uint32_t length, char *response, unsigned int capacity,
                           unsigned int *received, unsigned int *http_code)
{
    struct p4_spi_context *c = context;                                                          /* 回调持有终端、响应收集区和文件传输适配对象。 */
    struct sfile_http_files *http = &c->http;                                                    /* 通用 HTTP 描述存放在任务工作区，避免在栈上复制。 */
    struct sfile_sender *x = &c->sender;                                                         /* 获取同一任务持有的发送器，以指针传至底层。 */
    int ret;
    if (!length || strlen(spec) > SFILE_HTTP_META_MAX) return P4_ERR_LOCAL;                      /* 请求体须非空，规格必须能放入从机约定的元数据区。 */

    /* 重置上一次 HTTP 传输的规格、发送器和源视图，回调在同一 Shell 线程同步执行。 */
    memset(http, 0, sizeof(*http));
    memset(x, 0, sizeof(*x));                                                                    /* 清除上一次控制交换与配对诊断计数。 */
    memset(&c->source, 0, sizeof(c->source));                                                    /* 使之前的录音回读故障不遗留到本次请求。 */
    c->source.body = body; c->source.record = record;                                            /* 保存正文来源，调用者保证本次同步传输期间数据有效。 */
    http->spec_data = (const uint8_t *)spec; http->spec_size = strlen(spec);                     /* 规格长度只包括有效文本字节。 */
#ifdef P4_RADIO_TEST
    if (!c->radio_sent &&
        ((c->fault.mode == 64u && !strncmp(spec, "POST\n" P4_ORIGIN P4_INIT_PATH "\n",
                         sizeof("POST\n" P4_ORIGIN P4_INIT_PATH "\n") - 1)) ||
         (c->fault.mode == 128u && !strncmp(spec, "PUT\n", 4)))) {
        http->radio_test = c->fault.mode == 64u ? 1u : 2u;                                       /* 仅为这笔匹配的业务请求附加射频场景字节。 */
        c->radio_sent = true;                                                                    /* 只下发一次射频诊断，I/O 结果未知时也不自动重复触发。 */
        shell_print(c->shell, "P4 RADIO request mode=%u; check COM6 for actual RF state", http->radio_test);
    }
#endif
    /* sfile_send_data 通过 read_block 取发送正文，sfile_read_http_result 通过 receive 把响应页交回同一个 P4 请求。 */
    http->receive = p4_spi_receive; http->receive_context = c;
    x->http = http; x->source = &c->source; x->read_block = p4_spi_read;                         /* 每个正文数据块通过 p4_spi_read 从源获得。 */
    x->size = length; x->bad_block = UINT32_MAX; x->delivery_command = SFILE_START_HTTP;         /* 正常 P4 请求采用通用 HTTP 命令并关闭合成坏块。 */
    c->response = response; c->capacity = capacity; c->used = 0;                                 /* 绑定业务提供的目标并从偏移零开始收本次响应。 */
    c->response_error = false;                                                                   /* 新请求尚未出现响应顺序或容量错误。 */
    response[0] = 0;                                                                             /* 响应区初始为空字符串，实际长度按 used 更新。 */
#ifdef P4_SPI_DIAG
    http->diag = k_heap_alloc(&sys_mem_pool, sizeof(*http->diag), K_NO_WAIT);                    /* 诊断快照从堆即时申请，内存不足时不开始 SPI 事务。 */
    if (!http->diag) return P4_ERR_LOCAL;                                                        /* 无法分配诊断区时不发起网络事务。 */
    memset(http->diag, 0, sizeof(*http->diag));                                                  /* 清零快照有效计数，区分未采集的槽位。 */
#endif
    ret = sfile_run_context(c->shell, x, NULL, true, P4_SPI_TEST_HZ);                            /* 设备交付必须走配对文件协议，时钟由验证构建参数给出。 */
#ifdef P4_SPI_DIAG
    {
        unsigned int i;
        const uint32_t *r = http->diag->initial;                                                 /* 先查看 SPI 开始时的时钟、状态与引脚快照。 */
        shell_print(c->shell, "P4 DIAG initial clk=%x ctrl=%x sta=%x pins=%x/%x/%x/%x",
                    r[0],r[1],r[2],r[3],r[4],r[5],r[6]);

        /* 事务已经结束，此时逐个打印保留下来的寄存器及帧头，不扰动现场的片选时序。 */
        for (i = 0; i < http->diag->count; ++i) {
            const struct sfile_diag_sample *s = &http->diag->samples[i];                         /* 每个样本对应长等待后的一次物理交换。 */
            r = s->regs;                                                                         /* 接着解释该样本处的寄存器状态。 */
            shell_print(c->shell, "P4 DIAG pair=%u wait_us=%u transfer_us=%u cmd=%u clk=%x ctrl=%x sta=%x pins=%x/%x/%x/%x",
                        i,http->diag->waited_us,s->transfer_us,s->command,r[0],r[1],r[2],r[3],r[4],r[5],r[6]);
            shell_print(c->shell, "P4 DIAG tx=%08x/%08x/%08x/%08x rx=%08x/%08x/%08x/%08x",
                        s->tx[0],s->tx[1],s->tx[2],s->tx[3],s->rx[0],s->rx[1],s->rx[2],s->rx[3]);
        }
        k_heap_free(&sys_mem_pool, http->diag);                                                  /* 打印完成后释放当前诊断包独享的快照。 */
        http->diag = NULL;                                                                       /* 释放后移除快照地址，避免下次请求使用失效对象。 */
    }
#endif
    /* 收齐响应且硬件正常关闭方可返回成功，源、分页与传输错误要优先于服务器状态处理。 */
    *received = c->used; *http_code = http->http_code;                                           /* 两项分别是主机已收的正文长度和 503 报告的 HTTP 状态，函数返回值另行反映本次传输结果。 */
    if (!ret && (!http->response_complete || c->used != http->response_size)) ret = -EBADMSG;    /* 即使传输返回正常，也必须收到所有声明的响应页。 */
    shell_print(c->shell, "P4 HTTP http=%u bytes=%u complete=%u link_failed=%u ret=%d",
                *http_code, *received, http->response_complete, http->link_failed, ret);
    p4_spi_stack(c, "HTTP");                                                                     /* 在 HTTP 会话退出后观察实际 Shell 栈余量。 */
    /* 只有链路正常且明确来自对端网络阶段的故障才可能重试。READY 超时、CRC 错及恢复过的链路均结束本次任务。 */
    if (c->source.failed) return P4_ERR_SOURCE;                                                  /* 先区分本地文件或分片读取失败。 */
    if (c->response_error) return P4_ERR_PROTOCOL;                                               /* 数据未按顺序完整写入时属于响应协议错误。 */
    if (http->transport_fault || http->link_failed) return P4_ERR_LINK;                          /* 链路收尾出错或曾重同步都会阻止这笔业务成功。 */
    if (http->wire_status == SFILE_SEND_FAILED || http->wire_status == SFILE_HTTP_FAILED)
        return http->http_code && (http->http_code < 200 || http->http_code >= 300) ?
               P4_ERR_HTTP : P4_ERR_NETWORK;                                                     /* 有明确的非 2xx 状态时保留 HTTP 类别，否则记为网络不可用。 */
    return ret ? P4_ERR_LINK : 0;                                                                /* 已知远端错误均处理后，其他本地失败保守归入链路错误。 */
}
/* 仅在命令明确指定业务 I/O 注入模式时绑定此包装回调。 */
/**
  * @brief  为显式故障测试包裹真实请求，分别处理发送前及响应后注入。
  * @param  context：[输入输出]调用期间有效的 p4_spi_context。
  * @param  spec：[输入]以零结束的方法、URL 和头字段规格。
  * @param  body：[输入]内存正文；采用录音回读时为 NULL。
  * @param  record：[输入]可重复回读的录音分片；内存正文时为 NULL。
  * @param  length：[输入]正文长度，单位字节。
  * @param  response：[输出]至少 capacity+1 字节的响应目标，额外一字节用于补零。
  * @param  capacity：[输入]可接收的响应字节数，不含结尾零。
  * @param  received：[输出]实际接收字节数。
  * @param  http：[输出]HTTP 状态码，未获得时为零。
  * @retval 0 表示真实成功，负值来自真实适配或一次性模拟的 P4 错误。
  * @note   若首次注入在发送前命中，跳过真实请求；仅记录本次新置位标记。
  */
static int p4_spi_test_request(void *context, const char *spec, const uint8_t *body,
                              const struct p4_record *record, uint32_t length,
                              char *response, unsigned int capacity,
                              unsigned int *received, unsigned int *http)
{
    struct p4_spi_context *c = context;                                                                   /* 读取本命令保存的模式和一次性注入计数。 */
    unsigned int previous = c->fault.fired;                                                               /* 保存原位图，以便只报告此调用新增的故障。 */
    int ret;
    *received = *http = 0; response[0] = 0;
    ret = p4_fault_apply(&c->fault, spec, false, 0, response, received, http);                            /* 命中发送前注入就直接返回模拟结果。 */
    if (!ret) {                                                                                           /* 发送前没有触发时才允许真实 SPI/HTTP 事务。 */
        ret = p4_spi_request(context, spec, body, record, length, response, capacity, received, http);    /* 真实传输结果作为发送后模拟判断的前提。 */
        ret = p4_fault_apply(&c->fault, spec, true, ret, response, received, http);                       /* 在真实传输之后应用一次性结果丢失规则。 */
    }
    if (c->fault.fired != previous)                                                                       /* 仅有新位触发时打印诊断事件。 */
        shell_print(c->shell, "P4 INJECT fired=%u simulated_ret=%d http=%u; next action must INIT",
                    c->fault.fired ^ previous, ret, *http);                                               /* 异或位图只指出本次新增的注入类型。 */
    return ret;
}

/**
  * @brief  通过文件协议设备信息命令读取 UMA503 的标识和当前 UTC。
  * @param  context：[输入输出]调用期间有效的 p4_spi_context。
  * @param  imei：[输出]可写 16 字节，接收 15 位 IMEI 和结尾零。
  * @param  utc：[输出]非空，UTC 整秒。
  * @retval 0 成功，P4_ERR_LINK 为事务失败或验证注入，P4_ERR_PROTOCOL 为版本不符。
  * @note   20 字节载荷含版本、4 字节小端 UTC 及 15 位 IMEI，日志仅输出后四位。
  */
static int p4_spi_device(void *context, char imei[16], uint32_t *utc)
{
    struct p4_spi_context *c = context;                                                           /* 使用本次上传独有的设备信息查询上下文。 */
    uint8_t *info = c->device_info;                                                               /* 借用堆对象中的 20 字节输出区。 */
    struct sfile_sender *x = &c->sender;                                                          /* 设备查询也使用实际主机发送器，不新建小栈上的副本。 */
    int ret;

    /* 用专门的设备查询事务取时钟和身份，复用同一配对及 READY 验证路径。 */
    memset(info, 0, sizeof(c->device_info));
    memset(x, 0, sizeof(*x));                                                                     /* 以前的 HTTP 与文件收发状态不能影响独立的设备查询。 */
    x->device_info = info; x->delivery_command = SFILE_DEVICE_INFO; x->bad_block = UINT32_MAX;    /* 设备查询不需要上传正文，以定长输出缓冲接回信息。 */
#ifdef P4_SPI_WITNESS_TEST
    x->witness_once = c->witness_once;                                                            /* 把此次诊断命令的注入要求带入设备查询传输。 */
#endif
    ret = sfile_run_context(c->shell, x, NULL, true, P4_SPI_TEST_HZ);                             /* 启动同步、READY 和双槽校验复用实际文件传输主路径。 */
#ifdef P4_SPI_WITNESS_TEST
    if (x->witness_once) {
        bool recovered = ret == -EBADMSG && x->witness_notice && !x->resync_needed &&
                         !x->link_failed && !s_ready_faulted;                                     /* 原注入故障须被识别，且重同步已完成、链路和 READY 锁均未留故障才计作恢复。 */
        shell_print(c->shell, "P4 WITNESS %s tid=%u frames=%u notice=%u link_ret=%d",
                    recovered ? "RECOVERED" : "UNVERIFIED", x->tid,
                    (unsigned int)x->witness_frames, (unsigned int)x->witness_notice, ret);
        return P4_ERR_LINK;                                                                       /* 在首次 HTTP 请求之前结束本次验证任务。 */
    }
#endif
    if (ret) return P4_ERR_LINK;                                                                  /* 文件链路查询未能完成，上层不可使用设备标识或 UTC。 */
    if (info[0] != 1) return P4_ERR_PROTOCOL;                                                     /* 回包格式版本不匹配，即使 CRC 正确也不能解读字段。 */
    *utc = sfile_u32(info + 1);                                                                   /* 承接协议小端 32 位 UTC 整秒。 */
    memcpy(imei, info + 5, 15); imei[15] = 0;                                                     /* 原始 IMEI 没有终止符，复制十五字节后在本地补零。 */
    shell_print(c->shell, "P4 DEVICE utc=%u imei_suffix=%.4s", *utc, imei + 11);
    p4_spi_stack(c, "DEVICE");                                                                    /* 在设备查询结束而接近签名路径时记录栈余量。 */
    return 0;                                                                                     /* IMEI 和 UTC 已取出，内容格式由签名方再校验。 */
}

/**
  * @brief  调用片上 TRNG 为签名提供随机字节。
  * @param  context：[输入]回调上下文，本函数忽略。
  * @param  out：[输出]可写 size 字节的缓冲。
  * @param  size：[输入]所需随机字节数。
  * @retval 0 成功，负值为长度、互斥、超时或采样错误。
  * @note   接口使用硬件随机源，长度必须为正的八字节倍数。
  */
static int p4_spi_random(void *context, uint8_t *out, unsigned int size)
{
    (void)context;                           /* 随机源为芯片全局 SE，当前上传上下文不参与取数。 */
    return sfile_entropy_fill(out, size);    /* 随机数来源与 TLS 种子共用 SE 的互斥和采样时限。 */
}

/**
  * @brief  将业务引擎的阶段事件送往当前 UART Shell。
  * @param  context：[输入输出]调用期间有效的 p4_spi_context。
  * @param  stage：[输入]事件或阶段名称。
  * @param  code：[输入]阶段错误码、状态或计数。
  * @param  value：[输入]随事件记录的字节数、时间或偏移等数值。
  * @retval 无。
  * @note   只记录传入的阶段、码和值，含义由触发点决定。
  */
static void p4_spi_event(void *context, const char *stage, int code, uint32_t value)
{
    struct p4_spi_context *c = context;    /* 由回调上下文定位当前命令的输出端。 */
    shell_print(c->shell, "P4 STAGE %s code=%d value=%u", stage, code, value);
}

/**
  * @brief  将业务重试等待映射为当前线程的毫秒休眠。
  * @param  context：[输入]回调上下文，此函数忽略。
  * @param  milliseconds：[输入]休眠毫秒数。
  * @retval 无。
  * @note   用于任务上下文，让出处理器等待后续同 ID 查询。
  */
static void p4_spi_sleep(void *context, uint32_t milliseconds)
{
    (void)context;             /* 休眠作用于当前运行线程，无需私有设备上下文。 */
    k_msleep(milliseconds);    /* 在 Shell 线程中让出 CPU 到指定时限。 */
}

/**
  * @brief  为网络恢复窗口读取系统单调运行时间。
  * @param  context：[输入]回调上下文，此函数忽略。
  * @retval 32 位毫秒计数，按无符号数回绕。
  * @note   与签名用 UTC 分开，计算等待时间使用两个计数的差。
  */
static uint32_t p4_spi_now(void *context)
{
    (void)context;               /* 单调时基由内核提供，与某笔上传任务无关。 */
    return k_uptime_get_32();    /* 返回单调毫秒计数，供网络期限和退避计算。 */
}

/**
  * @brief  按绝对偏移回读 NOR 资源分区中的录音。
  * @param  context：[输入输出]已打开的 sd_file。
  * @param  offset：[输入]从文件首字节起的偏移。
  * @param  data：[输出]至少 size 个字节。
  * @param  size：[输入]读取字节数。
  * @retval 0 足量读出，-EIO 为定位失败或读取字节数不符。
  * @note   要求句柄在整个上传期间有效，每次先定位，支持分片和补读。
  */
static int p4_resource_read(void *context, uint32_t offset, uint8_t *data, unsigned int size)
{
    struct sd_file *file = context;                                    /* 回读对象实际绑定为 NOR 上的 sd_file。 */
    if (sd_fseek(file, (int)offset, FS_SEEK_SET)) return -EIO;         /* 每块从绝对偏移定位，支持多个校验遍历和分片补读。 */
    return sd_fread(file, data, (int)size) == (int)size ? 0 : -EIO;    /* 短读即失败，不能将部分数据当完整源块。 */
}
struct p4_spi_job {
    struct p4_work work;                                               /* 所有请求共用的签名、响应及业务状态。 */
    struct p4_spi_context context;                                     /* 平台回调的运行上下文。 */
    struct p4_record record;                                           /* 当前命令覆盖 ID 和读取回调后的源记录副本。 */
    struct p4_io io;                                                   /* 引擎实际调用的函数表。 */
};

/**
  * @brief  打开 NOR 录音、绑定平台回调并同步执行 P4 上传和对账。
  * @param  sh：[输入]本次命令的有效 Shell 实例。
  * @param  argc：[输入]含命令名在内的参数项数。
  * @param  argv：[输入]可选十四位测试 ID、分片提示 128/4096 KiB、可选故障模式。
  * @retval 0 源文件和云端最终状态均通过，负数为参数、资源、链路或业务引擎错误。
  * @note   大工作区从堆一次分配，退出前擦除。相同录音重试需沿用同一 localFileId。
  */
static int shell_p4upload(const struct shell *sh, size_t argc, char *argv[])
{
    struct p4_spi_job *job;                                                                                    /* 指向一份堆上分配的完整上传任务。 */
    struct p4_record *record;                                                                                  /* 引用任务持有的录音元数据副本。 */
    const char *local_id = p4_fixture.local_id;                                                                /* 默认采用验证资源附带的 ID，命令可以显式覆盖。 */
    uint32_t part_size = p4_fixture.part_size;                                                                 /* 分片大小先取随内置录音提供的提示，命令可覆盖。 */
    struct sd_file *file;                                                                                      /* 打开后须保持到业务引擎退出才可关闭。 */
    int ret;
    int fault = 0;                                                                                             /* 缺省走正常传输，不施加故障事件。 */
 
    if (argc < 1 || argc > 4) {
        shell_error(sh, "Usage: app p4upload [14-digit test localFileId [128|4096 part KiB [fault]]]");
        return -EINVAL;                                                                                        /* 只接受命令本身及至多三个可选参数。 */
    }

    /* 允许通过命令显式选择本地 ID 和分片提示，所有格式检查均在打开资源及发送请求之前。 */
    if (argc >= 2) 
    {
        unsigned int i;
        if (strlen(argv[1]) != 14) {                                                                           /* 测试 ID 长度须与业务约定相符。 */
            shell_error(sh, "P4 localFileId must be 14 digits; no request sent");
            return -EINVAL;                                                                                    /* 长度不符时在打开文件和发请求之前退出。 */
        }
        for (i = 0; i < 14; ++i) {
            if (argv[1][i] < '0' || argv[1][i] > '9') {                                                        /* 每一位都检查为数字，拒绝符号、空格和路径字符。 */
                shell_error(sh, "P4 localFileId must be 14 digits; no request sent");
                return -EINVAL;                                                                                /* ID 的某一位不是十进制数，终止当前命令。 */
            }
        }
        /* Shell 命令同步执行，选定 ID 在 init、PUT 与 complete 间一直有效；再次运行须显式沿用同一 ID 才能对账。 */
        local_id = argv[1];                                                                                    /* 同步运行期间 argv 字符串一直由 Shell 保持有效。 */
    }

    if (argc >= 3) 
    {
        if (!strcmp(argv[2], "128")) part_size = 128u * 1024u;                                                 /* 小分片提示用于对照服务器下发的实际范围。 */
        else if (!strcmp(argv[2], "4096")) part_size = P4_PART_SIZE;                                           /* 4096 KiB 对应本地允许的 4 MiB 提示。 */
        else { 
                shell_error(sh, "P4 part KiB must be 128 or 4096; no request sent"); return -EINVAL; 
             }
    }
#ifdef P4_SPI_WITNESS_TEST
    bool witness_once = argc == 4 && !strcmp(argv[3], "witness-once");                                         /* 仅用于整条 SPI 恢复链路的见证字注入分支。 */
    if (witness_once)                                                                                          /* 识别见证字错误后不再按普通业务故障名解析。 */
        shell_print(sh, "P4 WITNESS armed device pair");
    else
#endif
    if (argc == 4 && (fault = p4_fault_mode(argv[3])) < 0) {
#ifdef P4_RADIO_TEST
        shell_error(sh, "P4 fault: put-before|put-lost|complete-before|complete-lost|both-lost|put-403|init-503|radio-before|radio-mid");
#else
        shell_error(sh, "P4 fault: put-before|put-lost|complete-before|complete-lost|both-lost|put-403|init-503");
#endif
        return -EINVAL;                                                                                        /* 当前构建不支持该故障名称。 */
    }

    /* 验证 NOR 资源与此次构建配套，再从系统堆取得完整任务工作区。 */
    file = sd_fopen("/NOR:A/p4.m4a");
    if (!file) { shell_error(sh, "P4 source missing; flash the matching resource image"); return -ENOENT; }    /* 打开失败通常需要核对是否烧录了对应资源镜像。 */
    if ((uint32_t)file->size != p4_fixture.size) { sd_fclose(file); return -EBADMSG; }                         /* 实物资源大小须与本次配套的元数据吻合。 */

    job = k_heap_alloc(&sys_mem_pool, sizeof(*job), K_NO_WAIT);                                                /* 一次从系统堆申请整个任务工作区，不占 Shell 的小栈。 */
    
    if (!job) {
        sd_fclose(file);                                                                                       /* 堆申请失败，立即关闭已经取得的资源。 */
        shell_error(sh, "P4 FAIL memory need=%u; no network request sent", (unsigned int)sizeof(*job));
        return -ENOMEM;                                                                                        /* 明确返回内存不足，设备端此时还没有发请求。 */
    }
    memset(job, 0, sizeof(*job));                                                                              /* 清除故障标志、回调及跨阶段临时区。 */
    job->context.shell = sh;                                                                                   /* 保存本条命令的 Shell 用于诊断事件输出。 */
#ifdef P4_SPI_WITNESS_TEST
    job->context.witness_once = witness_once;                                                                  /* 只给显式要求该场景的本次任务启用破坏见证字。 */
#endif

    /* 复制不可变录音描述并安装真实设备、随机源、传输、事件与重试计时回调。 */
    job->context.fault.mode = (unsigned int)fault;
    record = &job->record;                                                                                     /* 对堆中副本安装动态元数据，不改资源包中原始描述。 */
    *record = p4_fixture;                                                                                      /* 保留验证资源中的文件名、长度及预期摘要等属性。 */
    record->source = file; record->read = p4_resource_read;                                                    /* 将资源句柄绑定到支持绝对偏移回读的本机实现。 */
    record->local_id = local_id; record->part_size = part_size;                                                /* 本地 ID 与分片提示在整个业务循环期间固定。 */
    job->io.context = &job->context;                                                                           /* 所有引擎回调共享同一个任务私有上下文。 */
    job->io.device = p4_spi_device; job->io.random = p4_spi_random;                                            /* 每笔签名都从 UMA503 取设备信息，从 ATS 硬件取随机数。 */
    job->io.request = p4_spi_request; job->io.event = p4_spi_event;                                            /* 请求指向 SPI 实际交付；阶段事件输出到当前 Shell。 */
    job->io.sleep_ms = p4_spi_sleep;                                                                           /* 退避等待使用内核的毫秒休眠实现。 */
    job->io.now_ms = p4_spi_now;                                                                               /* 恢复窗口按单调运行时间计算，分开处理墙钟 UTC。 */

    if (fault) {
#ifdef P4_RADIO_TEST
        if (fault == 64 || fault == 128) {                                                                     /* 两种射频场景由真实从机无线控制处理，走正常 SPI 请求回调。 */
            shell_print(sh, "P4 RADIO armed=%s; real RF control, automatic restore on 503", argv[3]);
        } else
#endif
        {
        job->io.request = p4_spi_test_request;                                                                 /* 仅模拟业务 I/O 的故障以包装回调替换普通传输回调。 */
        shell_print(sh, "P4 INJECT armed=%u mode=%s; synthetic business IO fault, not physical network loss",
                    (unsigned int)fault, argv[3]);
        }
    }
    shell_print(sh, "P4 BEGIN id=%s file=%s bytes=%u duration_ms=%u sha256=%s",
                record->local_id, record->filename, record->size,
                record->duration_ms, record->sha256);
    shell_print(sh, "P4 SPI requested_hz=%u", (unsigned int)P4_SPI_TEST_HZ);
    shell_print(sh, "P4 MULTIPART part_size=%u requests_max=%u retries_without_progress=%u",
                part_size ? part_size : P4_PART_SIZE, P4_REQUEST_LIMIT, P4_RETRY_LIMIT);                       /* 输出本次实际分片提示和全局限额，便于核对构建参数。 */
    shell_print(sh, "P4 NETWORK window_ms=%u backoff_max_ms=%u; resume via INIT",
                P4_NETWORK_WINDOW_MS, P4_NETWORK_BACKOFF_MAX_MS);

    /* 业务调用同步结束后关闭源，检查注入情况，最终擦除并释放任务工作区。 */
    ret = p4_upload_run(&job->work, record, p4_fixture_key, p4_fixture_key_size, &job->io);
    sd_fclose(file);                                                                                           /* 业务引擎已退出，才能释放录音资源句柄。 */
    if (fault == 64 || fault == 128)
        shell_print(sh, "P4 RADIO RESULT %s; COM6 verifies trigger/restoration, upload PASS alone is insufficient",
                    job->context.radio_sent ? "REQUESTED" : "NOT_REQUESTED");                                  /* 是否下发过射频指令和对端是否实际完成断网是两个验收观察点。 */
    else if (fault)
        shell_print(sh, "P4 INJECT RESULT %s armed=%u fired=%u",
                    job->context.fault.fired == (unsigned int)fault ? "TRIGGERED" : "NOT_FULLY_TRIGGERED",
                    (unsigned int)fault, job->context.fault.fired);                                            /* 可用已触发位图核对全部预期模拟场景是否都发生过。 */
    {
        volatile uint8_t *p = (volatile uint8_t *)job;                                                         /* 对整块任务工作区使用逐字节可观察写入。 */
        unsigned int i;
        for (i = 0; i < sizeof(*job); ++i) p[i] = 0;                                                           /* 通过 volatile 写擦除签名、响应及规格临时数据，再归还堆。 */
    }
    k_heap_free(&sys_mem_pool, job);                                                                           /* 擦除后才把本次任务归还共享堆。 */
    shell_print(sh, "P4 RESULT %s bytes=%u id=%s",
                ret ? "FAIL" : "PASS", p4_fixture.size, local_id);
    return ret;
}
#endif
#endif
