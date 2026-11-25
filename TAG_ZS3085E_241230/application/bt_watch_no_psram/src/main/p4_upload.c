/* 板端 P4 录音上传状态机。业务层用回调查设备时间与身份、取硬件随机数、发送 HTTPS 并回读源文件；用同一录音 ID 的 init 查询决定下一步。 */
#include "p4_upload.h"
#include <stdio.h>
#include <string.h>

/**
  * @brief  把二进制摘要或随机值编码为小写十六进制串。
  * @param  data：[输入]至少 n 字节的二进制数据。
  * @param  n：[输入]数据字节数。
  * @param  out：[输出]至少 2*n+1 字节的可写区域。
  * @retval 无。
  * @note   每个字节先高半字节、后低半字节，末尾追加零。
  */
static void tohex(const uint8_t *data, size_t n, char *out)
{
    static const char digits[] = "0123456789abcdef";    /* 编码表固定为小写，与服务器期望的摘要和签名文本一致。 */
    size_t i;                                           /* i 是当前二进制源字节的位置。 */
    for (i = 0; i < n; ++i) {                           /* 一个输入字节恰好产生两个输出字符。 */
        out[i * 2] = digits[data[i] >> 4];              /* 先取高半字节作为该字节的第一个字符。 */
        out[i * 2 + 1] = digits[data[i] & 15];          /* 低四位索引同一张表，完成一字节的编码。 */
    }
    out[n * 2] = 0;                                     /* 两倍长度之后写零供后续字符串接口使用。 */
}

/**
  * @brief  取出对象内唯一且非空的 ASCII 字符串字段。
  * @param  o：[输入]已验证的 JSON 对象。
  * @param  key：[输入]字段名。
  * @param  out：[输出]接收字符串及结尾零的缓冲。
  * @param  cap：[输入]缓冲容量，单位字节。
  * @retval 0 成功，-1 为字段缺失、重复、空串或解码失败。
  * @note   用于读取服务端状态与会话标识。
  */
static int field_string(struct p4_json o, const char *key, char *out, size_t cap)
{
    struct p4_json v;                                                                   /* 字段视图借用对象内的响应文本。 */
    return p4_json_get(o, key, &v) == 1 && p4_json_string(v, out, cap) > 0 ? 0 : -1;    /* 必须恰有一个字段，且解出至少一个字符。 */
}

/**
  * @brief  读取唯一的无符号整数字段。
  * @param  o：[输入]已验证的 JSON 对象。
  * @param  key：[输入]字段名。
  * @param  out：[输出]非空结果地址。
  * @retval 0 成功，-1 为缺失、重复、类型或数值错误。
  * @note   先查字段，再校验纯数字格式及 32 位边界。
  */
static int field_u32(struct p4_json o, const char *key, uint32_t *out)
{
    struct p4_json v;                                                  /* 用局部视图暂存按键定位到的数值字段。 */
    return p4_json_get(o, key, &v) == 1 ? p4_json_u32(v, out) : -1;    /* 缺失或重复的整数字段都不能用默认零替代。 */
}

/**
  * @brief  检查可直接插入 JSON 字符串的非空文本。
  * @param  s：[输入]以零结束的候选字符串，可为 NULL。
  * @retval 1 允许，0 为空或包含不可直接嵌入的字符。
  * @note   构造请求体时直接使用这些文本，因此排除双引号、反斜杠及不可打印字符。
  */
static int safe_text(const char *s)
{
    if (!s || !*s) return 0;                                  /* 构造身份或文件名字段时拒绝空指针和空文本。 */
    for (; *s; ++s) if ((unsigned char)*s < 32 || (unsigned char)*s > 126 ||
                        *s == '"' || *s == '\\') return 0;    /* 对双引号和反斜杠没有转义步骤，因此直接拒绝。 */
    return 1;                                                 /* 全部字符都可原样插入请求体的双引号内。 */
}
/* 签名阶段单独调用，让其局部数组在进入更深的 SPI 驱动和日志调用前退栈。 */
/**
  * @brief  读取当前设备身份和时间，生成签名 POST 的元数据。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  key：[输入]至少 key_size 字节的设备签名密钥。
  * @param  key_size：[输入]密钥长度，单位字节。
  * @param  io：[输入]平台设备、随机数、传输、事件及定时回调表。
  * @param  path：[输入]服务端 init 或 complete 的请求路径。
  * @retval 0 成功，设备失败返回网络或链路错误，其余构造或密码运算失败返回 -1。
  * @note   六个签名字段按顺序用 LF 分隔，不在最后追加 LF。工作区保存摘要和 HMAC 状态。
  */
static __attribute__((noinline)) int prepare_post(struct p4_work *w, const uint8_t *key, size_t key_size,
                       const struct p4_io *io, const char *path)
{
    const char *parts[6];                                                                                            /* 六个指针依次引用方法、路径、IMEI、时间戳、nonce 和正文摘要。 */
    int n, ret;                                                                                                      /* n 检查格式化长度，ret 保留设备查询错误。 */
    size_t i;                                                                                                        /* 同一索引先检查设备标识，后遍历待签名字段。 */
    /* 每一笔签名 POST 都重新向模组读 UTC 和设备身份。 */
    w->received = w->http = 0;                                                                                       /* 设备查询失败时不要留用前一次请求的状态。 */

    /* 先更新身份、UTC 与随机 nonce，拒绝时间未同步、标识异常或任务期间换设备。 */
    ret = io->device(io->context, w->sign.imei, &w->utc);
    if (ret) return ret == P4_ERR_NETWORK ? ret : P4_ERR_LINK;                                                       /* 只有平台明确返回的网络错误可以进入网络重试。 */
    if (w->utc < 1735689600u ||
        strlen(w->sign.imei) != 15 || io->random(io->context, w->sign.random, sizeof(w->sign.random))) return -1;    /* 设备时间须至少为 2025 年起点，同时获取本次独立的随机数。 */
    for (i = 0; i < 15; ++i) if (w->sign.imei[i] < '0' || w->sign.imei[i] > '9') return -1;                          /* IMEI 必须由整整十五个十进制数字组成。 */
    if (w->imei[0] && strcmp(w->imei, w->sign.imei)) return -1;                                                      /* 同一录音会话中禁止切换签名设备身份。 */
    memcpy(w->imei, w->sign.imei, sizeof(w->imei));                                                                  /* 保存已确认身份，供下一次 POST 比对及组装头字段。 */
    tohex(w->sign.random, sizeof(w->sign.random), w->sign.nonce);                                                    /* 十六个随机字节编码为三十二位十六进制文本。 */
    /* 板上 newlib-nano 格式化不支持 %llu。模组时间精度为整秒，十进制后附三个零生成协议要求的毫秒文本。 */
    n = snprintf(w->sign.timestamp, sizeof(w->sign.timestamp), "%u000", (unsigned int)w->utc);                       /* 秒值按十进制输出后补三个零，与服务器要求的毫秒单位一致。 */
    if (n <= 0 || (size_t)n >= sizeof(w->sign.timestamp)) return -1;                                                 /* 对格式化返回值检查截断，确保时间文本可用于签名。 */

    /* 先对实际发送的 JSON 正文字节做 SHA-256，不把字符串终止符算入正文。 */
    if (!tc_sha256_init(&w->crypto.sha) ||
        !tc_sha256_update(&w->crypto.sha, (const uint8_t *)w->body, strlen(w->body)) ||
        !tc_sha256_final(w->sign.digest, &w->crypto.sha)) return -1;                                                 /* 将固定大小的正文摘要写入工作区，密码运算失败就放弃此次构造。 */
    tohex(w->sign.digest, 32, w->sign.hash);                                                                         /* 将二进制摘要编码成 HMAC 输入的最后一项。 */
    parts[0] = "POST"; parts[1] = path; parts[2] = w->imei;                                                          /* 规范原文头三项固定为方法、请求路径和设备标识。 */
    parts[3] = w->sign.timestamp; parts[4] = w->sign.nonce; parts[5] = w->sign.hash;                                 /* 后三项依次为设备时间、随机 nonce、正文摘要。 */

    /* 用设备密钥初始化 HMAC，然后按协议顺序输入六段原始文本。 */
    if (!tc_hmac_set_key(&w->crypto.hmac, key, key_size) || !tc_hmac_init(&w->crypto.hmac))
        return -1;                                                                                                   /* 准备密钥或初始化 HMAC 失败时不能生成有效请求。 */

    /* 签名原文依约定逐段输入，不创建拼接六个字段所需的大临时字符串。 */
    for (i = 0; i < 6; ++i) {
        if (i && !tc_hmac_update(&w->crypto.hmac, (const uint8_t *)"\n", 1)) return -1;                              /* 从第二个字段开始在前面插入一个换行分隔符。 */
        if (!tc_hmac_update(&w->crypto.hmac, (const uint8_t *)parts[i], strlen(parts[i]))) return -1;                /* 输入到 HMAC 的字段长度不包含 C 字符串结尾零。 */
    }
    if (!tc_hmac_final(w->sign.digest, sizeof(w->sign.digest), &w->crypto.hmac)) return -1;                          /* HMAC 结果写入复用的 32 字节摘要暂存。 */
    tohex(w->sign.digest, 32, w->sign.signature);                                                                    /* 将 HMAC 结果转换为请求头使用的小写十六进制文本。 */

    /* 把已签名的实际方法、URL 和头写入从机能识别的规格。外层规格以 LF 分列，HTTP 头使用 CRLF。 */
    n = snprintf(w->spec, sizeof(w->spec),
        "POST\n%s%s\nContent-Type: application/json\r\n"
        "X-Device-Imei: %s\r\nX-Device-Timestamp: %s\r\n"
        "X-Device-Nonce: %s\r\nX-Device-Signature: %s\r\n",
        P4_ORIGIN, path, w->imei, w->sign.timestamp, w->sign.nonce, w->sign.signature);
    memset(&w->crypto, 0, sizeof(w->crypto));                                                                        /* 成功完成签名计算后清理密码上下文。 */
    memset(&w->sign, 0, sizeof(w->sign));                                                                            /* 清除临时 nonce、摘要与签名文本。 */
    if (n <= 0 || (size_t)n >= sizeof(w->spec)) return -1;                                                           /* 请求规格截断时，即使已有部分签名头也不能发送。 */
    return 0;                                                                                                        /* 元数据及签名准备完成，可进入传输层。 */
}

/**
  * @brief  判断已启动的网络恢复窗口是否耗尽。
  * @param  w：[输入]保存本次重试窗口起点和是否正在等待的工作区。
  * @param  io：[输入]平台设备、随机数、传输、事件及定时回调表。
  * @retval true 为已到期，false 为尚未开始或仍有余量。
  * @note   单调毫秒计数用无符号差值处理一次回绕。
  */
static bool network_expired(const struct p4_work *w, const struct p4_io *io)
{
    return w->network_waiting &&
        (uint32_t)(io->now_ms(io->context) - w->network_started) >= P4_NETWORK_WINDOW_MS;    /* 单调计时的无符号差值可跨过 32 位回绕点。 */
}

/**
  * @brief  签名并经适配层同步提交一笔 JSON POST。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  key：[输入]至少 key_size 字节的设备签名密钥。
  * @param  key_size：[输入]密钥长度，单位字节。
  * @param  io：[输入]平台设备、随机数、传输、事件及定时回调表。
  * @param  path：[输入]要签名的服务端路径。
  * @retval 成功返回 0，构造与 I/O 错误透传，超出预算返回 P4_ERR_LIMIT。
  * @note   通过检查后才增加请求计数，正文使用工作区内存。
  */
static int signed_post(struct p4_work *w, const uint8_t *key, size_t key_size,
                       const struct p4_io *io, const char *path)
{
    int ret = prepare_post(w, key, key_size, io, path);                                    /* 签名之前先获得新设备时间及 nonce，重试不会复用旧签名。 */
    if (ret) return ret;                                                                   /* 签名或读取设备信息失败时不发 POST。 */
    w->received = w->http = 0;                                                             /* 不沿用上笔请求的响应长度和状态。 */
    if (w->requests >= P4_REQUEST_LIMIT || network_expired(w, io)) return P4_ERR_LIMIT;    /* 预算检查在交给传输层之前，压住网络恢复和总请求数两个上限。 */
    ++w->requests;                                                                         /* 包括失败的实际发送尝试在内共同消耗请求额度。 */
    return io->request(io->context, w->spec, (const uint8_t *)w->body, NULL, strlen(w->body),
                       w->response, P4_RESPONSE_MAX, &w->received, &w->http);              /* POST 以已准备好的内存正文发送，录音分片参数为空。 */
}

/**
  * @brief  按固定录音身份、尺寸与分片提示构造 init 请求体。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  r：[输入]已验证的不可变录音描述。
  * @retval 0 成功，-1 为编码失败或目标空间不足。
  * @note   重试沿用相同源记录和本地 ID，以查询服务器进度。
  */
static int init_body(struct p4_work *w, const struct p4_record *r)
{
    /* init 的 JSON 同时描述源身份和媒体属性。partSize 是客户端提示，后续仍以服务器回报的区间为准。 */
    int n = snprintf(w->body, sizeof(w->body),
        "{\"localFileId\":\"%s\",\"fileMd5\":\"%s\",\"fileSize\":%u,"
        "\"durationMs\":%u,\"audioFormat\":\"m4a\",\"contentType\":\"audio/mp4\","
        "\"originalFileName\":\"%s\",\"recordedAt\":\"%s\","
        "\"partSize\":%u,\"uploadSignVersion\":\"V2\"}",
        r->local_id, r->sample_md5, (unsigned int)r->size,
        (unsigned int)r->duration_ms, r->filename, r->recorded_at, (unsigned int)w->part_size);
    return n <= 0 || (size_t)n >= sizeof(w->body) ? -1 : 0;    /* snprintf 报告所需长度而非已写字节数，达到容量即为截断。 */
}

/**
  * @brief  验证 HTTP 及业务状态，取出服务端结果对象。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  out：[输出]借用工作区响应文本的 result 对象。
  * @param  done：[输出]是否为 SYNC_SUCCESS。
  * @retval 0 为可识别的同步状态，-1 为 HTTP、JSON 或业务字段异常。
  * @note   只接收 SYNCING 和 SYNC_SUCCESS；通过 HTTP 2xx 后还须校验业务状态。
  */
static int result(struct p4_work *w, struct p4_json *out, bool *done)
{
    struct p4_json root;                                                        /* 根视图借用 w->response，直到下一次请求覆盖它。 */
    char status[24];                                                            /* 限制业务结果及同步状态的临时文本长度。 */

    /* 先按实际收包长度验证完整文档，再校验根状态、唯一结果对象和同步状态。 */
    if (w->http < 200 || w->http >= 300 || w->received > P4_RESPONSE_MAX ||
        p4_json_document(w->response, w->received, &root) ||
        field_string(root, "status", status, sizeof(status)) || strcmp(status, "00000") ||
        p4_json_get(root, "result", out) != 1 ||
        field_string(*out, "syncStatus", status, sizeof(status))) return -1;    /* 顶级结果或嵌套状态缺一项都不接受。 */
    *done = !strcmp(status, "SYNC_SUCCESS");                                    /* 完成标志来自业务结果中的状态，而非 HTTP 状态码。 */
    return *done || !strcmp(status, "SYNCING") ? 0 : -1;                        /* 完成以外只接受明确的同步进行中状态。 */
}

/**
  * @brief  记录并交叉检查本次录音的服务端会话和分片上传标识。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  r：[输入]服务端 result 对象。
  * @param  completed：[输入]是否已报告完成。
  * @retval 0 身份一致，-1 为缺失、非法或与已记录值不一致。
  * @note   服务端已完成时允许省略 multipartUploadId，出现时仍须验证。
  */
static int identity(struct p4_work *w, struct p4_json r, bool completed)
{
    char value[192];                                                                                          /* 标识先解码到临时数组，再校验是否与工作区一致。 */
    struct p4_json upload;                                                                                    /* 仅在响应包含上传标识时借用对应值。 */
    int present;                                                                                              /* 区分字段缺失、找到以及对象读取错误三种结果。 */
    if (field_string(r, "syncSessionId", value, sizeof(value)) || !safe_text(value)) return -1;               /* 会话号随后要直接写入 JSON，须同时满足非空、长度和安全字符要求。 */
    if (w->session[0] && strcmp(w->session, value)) return -1;                                                /* 已缓存的会话号必须与当前服务器结果相同。 */
    strcpy(w->session, value);                                                                                /* 首次取得的会话号供随后重新 init 或提交时核实。 */
    present = p4_json_get(r, "multipartUploadId", &upload);                                                   /* 单独查询上传 ID，以兼容已完成且不再给出该字段的响应。 */
    /* 实际 complete 响应会给出会话号，允许省略分片上传标识。 */
    if (!present && completed) return 0;
    if (present != 1 || p4_json_string(upload, value, sizeof(value)) <= 0 || !safe_text(value)) return -1;    /* 字段重复、空串、过长或含不安全字符均不能用于后续提交。 */
    if (w->upload[0] && strcmp(w->upload, value)) return -1;                                                  /* 已取得的分片上传号也须保持一致。 */
    strcpy(w->upload, value);                                                                                 /* 会话与上传号相互配套，复制到同一任务工作区。 */
    return 0;                                                                                                 /* 服务端回报的身份已满足当前阶段要求。 */
}

/**
  * @brief  检查服务端报告的已上传字节和连续区间边界。
  * @param  r：[输入]服务端 result 对象。
  * @param  size：[输入]源文件总字节数。
  * @param  complete：[输入]true 表示执行完成状态的全长检查。
  * @retval 0 合法，-1 为字段错误、边界越界或完成时长度不符。
  * @note   最终确认同时要求累计数和从头连续数等于源长度。
  */
static int progress(struct p4_json r, uint32_t size, bool complete)
{
    uint32_t uploaded, contiguous;                                           /* 累计已上传量与从首字节起的连续已上传量分别比较。 */
    if (field_u32(r, "uploadedBytes", &uploaded) ||
        field_u32(r, "contiguousUploadedBytes", &contiguous) ||
        uploaded > size || contiguous > uploaded) return -1;                 /* 连续已传字节不能超过已传总量，总量不能超过源大小。 */
    return complete && (uploaded != size || contiguous != size) ? -1 : 0;    /* 完成后还要覆盖从首字节到末字节的整个连续区间。 */
}

/**
  * @brief  把分片局部偏移映射到同一份原始文件。
  * @param  context：[输入]持有原始记录和分片基址的 p4_work。
  * @param  offset：[输入]分片内字节偏移。
  * @param  data：[输出]可写 size 字节。
  * @param  size：[输入]请求读取字节数。
  * @retval 0 成功，越界返回 P4_ERR_SOURCE，其余错误来自原始读取回调。
  * @note   以减法检查 offset+size，避免无符号加法先溢出。
  */
static int part_read(void *context, uint32_t offset, uint8_t *data, unsigned int size)
{
    struct p4_work *w = context;                                                           /* 读取回调的上下文指向产生该分片的工作区。 */
    if (offset > w->part.size || size > w->part.size - offset) return P4_ERR_SOURCE;       /* 先比较分片内偏移和剩余空间，避免偏移与长度相加时回绕。 */
    return w->original->read(w->original->source, w->part_offset + offset, data, size);    /* 以服务端下发的分片起点映射到原文件的绝对偏移。 */
}
/* 区间与头部验证完毕即释放局部栈帧，再进入传输层。 */
/**
  * @brief  验证全部待传区间，从第一项生成 PUT 元数据及回读窗口。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  r：[输入]已验证的服务端结果视图。
  * @param  record：[输入]完整源记录。
  * @retval 0 可以开始传输，-1 为字段、范围、URL、头部或容量错误。
  * @note   服务端分片号与偏移顺序须一致且区间互不覆盖。一次只传一项，随后重新 init。
  */
static __attribute__((noinline)) int put_spec(struct p4_work *w, struct p4_json r, const struct p4_record *record)
{
    struct p4_json pending, part, value;                                                           /* 分别借用待传数组、本次分片以及某个字段或前项的视图。 */
    uint32_t number, offset, length, uploaded, contiguous, missing = 0;                            /* missing 累计服务器本轮列出的全部待传字节。 */
    int n, found, count, j, k;                                                                     /* 保存解析结果、字段存在状态、项数以及双层检查的下标。 */
    size_t used, i;                                                                                /* 已生成的请求规格长度及 URL 字符索引。 */

    /* 取出非空的待传数组和当前服务端进度，两者一起用于判断长度与覆盖关系。 */
    if (p4_json_get(r, "pendingParts", &pending) != 1 ||
        (count = p4_json_array(pending, NULL)) <= 0 ||
        field_u32(r, "uploadedBytes", &uploaded) ||
        field_u32(r, "contiguousUploadedBytes", &contiguous)) return -1;                           /* 未取得完整的待传列表和两种进度数据时禁止猜测范围。 */
    /* 服务端决定每一片的偏移和长度，本地 part_size 仅为提示。在发送任何源字节前先检查所有公告区间。 */
    for (j = 0; j < count; ++j) {                                                                  /* 检查每个公告区间，即使本轮只实际发送第一项。 */
        if (p4_json_at(pending, j, &part) ||
            field_u32(part, "partNumber", &number) || !number || number > 10000u ||
            field_u32(part, "offset", &offset) || offset >= record->size ||
            ((number == 1u) != (offset == 0u)) ||                                                  /* 第一片当且仅当起始偏移为零。 */
            offset < contiguous || field_u32(part, "length", &length) || !length ||
            length > record->size - offset || length > record->size - missing) return -1;          /* 本区间和全部累计缺失量都要能放进原始文件。 */
        missing += length;                                                                         /* 经过减法上限检查后才累计缺失字节。 */

        /* 将当前片与此前所有片逐对比较；所需 RAM 恒定，耗时随片数增长。 */
        for (k = 0; k < j; ++k) {
            uint32_t previous, previous_offset, previous_length;                                   /* 暂存前一待传片的编号、起点和长度。 */
            if (p4_json_at(pending, k, &value) || field_u32(value, "partNumber", &previous) ||
                field_u32(value, "offset", &previous_offset) ||
                field_u32(value, "length", &previous_length) || previous == number ||
                (previous < number && previous_offset + previous_length > offset) ||
                (previous > number && offset + length > previous_offset)) return -1;               /* 编号相同或顺序与区间不一致，以及重叠的范围都不接受。 */
        }
    }

    /* 确认缺失量不超过尚未上传的总量后，只取本轮数组首项执行 PUT。 */
    if (uploaded > record->size || missing > record->size - uploaded ||
        p4_json_at(pending, 0, &part) ||
        field_u32(part, "offset", &w->part_offset) ||
        field_u32(part, "length", &length) || p4_json_get(part, "url", &value) != 1) return -1;    /* 当前第一片要同时提供长度和可用的 URL 字段。 */
    w->original = record;                                                                          /* 保存真实源描述供 part_read 映射读取。 */
    w->part = *record;                                                                             /* 复制元数据，再仅改变属于分片的数据源、读回调和范围。 */
    w->part.source = w; w->part.read = part_read; w->part.size = length;                           /* 把分片读操作改为带起点映射的局部窗口，元数据仍来自原记录。 */
    memcpy(w->spec, "PUT\n", 4);                                                                   /* 元数据以方法行开始，URL 将紧随换行写入。 */
    n = p4_json_string(value, w->spec + 4, sizeof(w->spec) - 4);                                   /* URL 直接解到方法行之后，保留服务端签发的内容。 */
    if (n < 10 || strncmp(w->spec + 4, "https://", 8)) return -1;                                  /* 只接受有最小长度的 HTTPS 目标。 */
    used = 4u + (size_t)n;                                                                         /* 已用字节为方法行四字节与已解码 URL 长度之和。 */
    for (i = 4; i < used; ++i)
        if ((unsigned char)w->spec[i] <= 32 || (unsigned char)w->spec[i] >= 127) return -1;        /* 排除空格、控制字节及非 ASCII，防止规格中插入额外行。 */
    if (used + 2 >= sizeof(w->spec)) return -1;                                                    /* 必须还能写入 URL 行分隔符及后续头部。 */
    w->spec[used++] = '\n';                                                                        /* 方法、URL 与后面的 HTTP 头以换行分隔。 */

    /* 根据服务端响应版本逐级选择附加请求头，再将其编码为从机能直接发送的 HTTP 头。 */
    found = p4_json_get(part, "headers", &value);
    if (!found) found = p4_json_get(part, "uploadHeaders", &value);                                /* 分片未提供 headers 时读取该片的 uploadHeaders。 */
    if (!found) found = p4_json_get(r, "uploadHeaders", &value);                                   /* 分片没有指定时才查结果对象上的公共 uploadHeaders。 */
    if (found < 0) return -1;                                                                      /* 格式错或目标键重复不能按“字段缺失”处理。 */
    if (found) n = p4_json_headers(value, w->spec + used, sizeof(w->spec) - used);                 /* 找到头字段时逐个检查后下发，不存在则使用二进制默认类型。 */
    else n = snprintf(w->spec + used, sizeof(w->spec) - used,
                      "Content-Type: application/octet-stream\r\n");                               /* 无服务器附加头时提供二进制正文类型。 */
    return n < 0 || (size_t)n >= sizeof(w->spec) - used ? -1 : 0;                                  /* 校验转换或格式化返回值，避免把部分头传给从机。 */
}

/**
  * @brief  分块回读整份录音并核对预期 SHA-256。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  r：[输入]提供大小、预期摘要和随机位置回读的记录。
  * @retval 0 摘要一致，P4_ERR_SOURCE 表示读源、摘要运算或比对失败。
  * @note   复用请求体缓冲作读块，调用后须重建待发的 JSON。
  */
static int verify_source(struct p4_work *w, const struct p4_record *r)
{
    uint32_t offset;                                                                                      /* 以源文件绝对字节偏移推进 SHA-256 读取。 */
    if (!tc_sha256_init(&w->crypto.sha)) return P4_ERR_SOURCE;                                            /* 每次验源都从新的 SHA-256 上下文开始。 */

    /* 按照可重复定位的源接口从零读至文件末尾，整个遍历只复用一块固定长度缓存。 */
    for (offset = 0; offset < r->size;) {
        uint32_t length = r->size - offset;                                                               /* 先计算距文件末尾尚余多少字节。 */
        if (length > sizeof(w->body)) length = sizeof(w->body);                                           /* 每次只读取工作区正文缓存可以容纳的一段。 */
        if (r->read(r->source, offset, (uint8_t *)w->body, length) ||
            !tc_sha256_update(&w->crypto.sha, (const uint8_t *)w->body, length)) return P4_ERR_SOURCE;    /* 读失败与哈希计算失败都中止整份源的验证。 */
        offset += length;                                                                                 /* 完整读出并参与哈希后再推进。 */
    }
    if (!tc_sha256_final(w->sign.digest, &w->crypto.sha)) return P4_ERR_SOURCE;                           /* 结束哈希并取得完整 32 字节摘要。 */
    tohex(w->sign.digest, sizeof(w->sign.digest), w->sign.hash);                                          /* 将摘要转为与元数据形式一致的小写十六进制。 */
    return strcmp(w->sign.hash, r->sha256) ? P4_ERR_SOURCE : 0;                                           /* 全文件哈希必须与录音描述中的预期值一致。 */
}

/**
  * @brief  将适配层错误和 HTTP 状态映射到业务重试策略。
  * @param  ret：[输入]适配层返回值。
  * @param  http：[输入]HTTP 状态，0 表示未获得状态。
  * @param  put：[输入]是否为预签名 PUT。
  * @retval 0 为 2xx 成功，其余为 p4_error 分类。
  * @note   PUT 403 可通过重新 init 换取 URL；签名 POST 的 401/403 按鉴权失败处理。
  */
static int request_error(int ret, unsigned int http, bool put)
{
    /* 先处理源读取、CRC、DMA 和清理故障，HTTP 2xx 不能覆盖这些错误。 */
    if (ret && ret != P4_ERR_NETWORK && ret != P4_ERR_HTTP)
        return ret <= P4_ERR_NETWORK && ret >= P4_ERR_HTTP ? ret : P4_ERR_LOCAL;    /* 已知 P4 错误按类别透传，其他接口返回归为本地错误。 */
    if (ret == P4_ERR_NETWORK) return ret;                                          /* 平台已明确报告网络不可用时原样交给重试分支。 */
    if (http == 408 || http == 429 || (http >= 500 && http <= 599) || (put && http == 403))
        return P4_ERR_NETWORK;                                                      /* 这些服务状态可通过重新 init 查询下一步，PUT 403 也可据此取得新授权。 */
    if (http == 401 || http == 403) return P4_ERR_AUTH;                             /* 未命中 PUT 可以刷新授权的特例时，401 和 403 按鉴权故障处理。 */
    return ret || http < 200 || http >= 300 ? P4_ERR_HTTP : 0;                      /* 其他非 2xx 或传输返回非零都报告 HTTP 错误。 */
}

/**
  * @brief  核对不可变录音，按服务端进度执行 init、分片 PUT 和 complete。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  r：[输入]整个调用期间可重复按偏移读取的录音描述。
  * @param  key：[输入]至少 key_size 字节的设备签名密钥。
  * @param  key_size：[输入]密钥长度，单位字节。
  * @param  io：[输入]平台设备、随机数、传输、事件及定时回调表。
  * @retval 0 为服务端完成且源摘要再次校验通过，负 P4_ERR_* 表示失败类别。
  * @note   每次恢复从同一 localFileId 的 init 开始。请求次数、网络时限和无进展重试各有边界。
  */
int p4_upload_run(struct p4_work *w, const struct p4_record *r,
                  const uint8_t *key, size_t key_size, const struct p4_io *io)
{
    struct p4_json res, pending;                                                                                     /* 本次 init/complete 的结果对象及其中待传片数组。 */
    bool done, await_progress = false;                                                                               /* done 表示云端完成，await_progress 表示前次 PUT 需要用 init 核验进展。 */
    uint32_t uploaded = 0, contiguous = 0, high = 0, high_contiguous = 0;                                            /* high 与 high_contiguous 保存历次对账的最高进度，用于拒绝回退。 */
    unsigned int retries = 0;                                                                                        /* 计数无进展与明确可重试 HTTP 状态的恢复次数。 */
    int ret = P4_ERR_SOURCE, count, n;                                                                               /* 在完成输入校验前默认归为源错误，count/n 用于响应查询。 */
    const char *stage = "SOURCE";                                                                                    /* 在发任何请求前，失败事件归于源验证阶段。 */

    /* 在清空工作区之前验证所有必需回调，后续分支均会依赖它们。 */
    if (!w || !io || !io->device || !io->random || !io->request || !io->event || !io->sleep_ms || !io->now_ms)
        return P4_ERR_LOCAL;                                                                                         /* 核心回调不全时连事件上报也不一定可用。 */
    memset(w, 0, sizeof(*w));                                                                                        /* 开始一个新业务任务，清除上一任务缓存的身份及重试计数。 */

    /* 源需非空且可重读，长度在有符号文件定位范围内；同时检查签名密钥、字符串与分片提示。 */
    if (!r || !r->read || !r->size || r->size > 0x7fffffffu || !key || !key_size ||
        !safe_text(r->local_id) || !safe_text(r->filename) || !safe_text(r->recorded_at) ||
        !safe_text(r->sample_md5) || strlen(r->sample_md5) != 32 || !r->sha256 ||
        strlen(r->sha256) != 64 || r->part_size > P4_PART_SIZE) goto fail;                                           /* 本地元数据不能满足上传约定时，报告当前失败阶段。 */

    /* 选定该录音的分片提示并校验完整源文件，成功后才进入网络阶段。 */
    w->part_size = r->part_size ? r->part_size : P4_PART_SIZE;
    if ((ret = verify_source(w, r))) goto fail;                                                                      /* 发起任何网络请求之前先校验源全文。 */
    io->event(io->context, stage, 0, r->size);                                                                       /* 源摘要已核实，通知上层录音实际长度。 */
    for (;;) {                                                                                                       /* 每轮 init 决定当前是查询完成、继续 PUT，还是提交。 */
        /* 已经进入传输的请求由其自身超时和清理处理；窗口到期后禁止启动新请求。 */
        if (network_expired(w, io)) {
            stage = "NETWORK_LIMIT"; ret = P4_ERR_LIMIT; goto fail;                                                  /* 若此前启动的网络恢复窗口耗尽，直接以限额结束。 */
        }
        stage = "INIT";                                                                                              /* 每一轮从同 ID 的 init 开始，含首次上传和故障后对账。 */
        if (init_body(w, r)) { ret = P4_ERR_LOCAL; goto fail; }                                                      /* init 请求体无法完整放入缓存时属于本地构造故障。 */
        ret = signed_post(w, key, key_size, io, P4_INIT_PATH);                                                       /* 使用新的设备时间、随机数及签名查询服务端进度。 */
        ret = request_error(ret, w->http, false);                                                                    /* 签名 POST 的鉴权处理和预签名 PUT 不同。 */
        if (ret) goto retry;                                                                                         /* 只让下面的公共恢复入口处理已分类的错误。 */
        ret = P4_ERR_PROTOCOL;                                                                                       /* 下面的解析和一致性失败都报告协议错误。 */

        /* 先验证服务端会话、长度和进度，再读取其是否还需要上传分片。 */
        if (result(w, &res, &done) || identity(w, res, done) || progress(res, r->size, done) ||
            field_u32(res, "uploadedBytes", &uploaded) ||
            field_u32(res, "contiguousUploadedBytes", &contiguous) ||
            uploaded < high || contiguous < high_contiguous) goto fail;                                              /* 拒绝同会话累计或连续已传字节倒退。 */
        io->event(io->context, stage, (int)w->http, w->received);                                                    /* 记录已经通过业务字段和单调进度检查的 init 响应。 */
        n = p4_json_get(res, "pendingParts", &pending);                                                              /* 是否仍有待传片决定下一阶段。 */
        if (n < 0 || (!done && n != 1)) goto fail;                                                                   /* 仍在上传时必须存在待传数组，已完成时允许省略。 */
        count = n ? p4_json_array(pending, NULL) : 0;                                                                /* 已完成时允许不提供数组，否则按实际项数判断。 */
        if (count < 0 || (done && count)) goto fail;                                                                 /* 声称已完成的服务器不应再列出待传片。 */
        if (done) goto success;                                                                                      /* 服务器已确认完成时直接作最终验源，避免重复 PUT 或 complete。 */

        /* 与上一轮最高进度相比，前进可恢复重试预算；PUT 后没有进展则按有界策略重新查询。 */
        if (uploaded > high || contiguous > high_contiguous) {
            retries = 0;                                                                                             /* 真实进展重新给予有限次数的重试预算。 */
            w->network_waiting = false;                                                                              /* 结束此前由于无网络响应而启动的等待窗口。 */
            w->network_retries = 0;                                                                                  /* 下次暂不可达时从最短退避间隔重新开始。 */
        }
        else if (await_progress) {                                                                                   /* 上次成功 PUT 后服务器仍无进展，消耗独立的有界重试次数。 */
            await_progress = false;                                                                                  /* 同一个已成功 PUT 只记一次无进展事件。 */
            ret = P4_ERR_NETWORK;                                                                                    /* 使本次无进展进入允许重试类别。 */
            stage = "NO_PROGRESS";                                                                                   /* 通过独立阶段名进入有界次数策略，避免启动无响应时限。 */
            goto retry;                                                                                              /* 重试前仍按统一的请求总数及退避预算检查。 */
        }
        high = uploaded; high_contiguous = contiguous;                                                               /* 保存本次高水位供下次 init 对比。 */
        await_progress = false;                                                                                      /* 下次 PUT 成功后才重新要求进度必须增长。 */

        /* 还有待传片时取第一片并发 PUT；随后回到循环顶部，从同一 ID 查询服务端真实进度。 */
        if (count) {
            stage = "PUT_PREPARE";                                                                                   /* 将范围或 URL 的错误归入分片准备阶段。 */
            if (put_spec(w, res, r)) goto fail;                                                                      /* 先验证服务器返回的每个区间，再准备本次第一片。 */
            io->event(io->context, "PART_RANGE", (int)w->part_offset, w->part.size);                                 /* 报告即将回读的真实文件偏移和分片长度。 */
            stage = "PUT";                                                                                           /* 后面 I/O 失败将明确属于正文上传步骤。 */
            if (network_expired(w, io)) { stage = "NETWORK_LIMIT"; ret = P4_ERR_LIMIT; goto fail; }                  /* 长时间准备分片后，实际发送前须再次检查网络期限。 */
            if (w->requests >= P4_REQUEST_LIMIT) { ret = P4_ERR_LIMIT; goto fail; }                                  /* 达到请求总数限制时不再尝试一笔 PUT。 */
            ++w->requests;                                                                                           /* 此次 PUT 与其他业务请求共用总上限。 */
            w->received = w->http = 0;                                                                               /* 清除前一次 init 的响应元信息，以便判断本次是否真正收到 HTTP。 */
            ret = io->request(io->context, w->spec, NULL, &w->part, w->part.size,
                              w->response, P4_RESPONSE_MAX, &w->received, &w->http);                                 /* 传输层按需回读所选分片，不将整片载入 RAM。 */
            io->event(io->context, stage, ret ? ret : (int)w->http, w->part_offset);                                 /* 记录本片结果和起点，便于对应服务端进度。 */
            ret = request_error(ret, w->http, true);                                                                 /* 只有 PUT 的 403 可作为获取新的预签名 URL 的理由。 */
            if (ret) goto retry;                                                                                     /* 通过公共恢复入口处理 PUT 处的网络或 HTTP 结果。 */
            await_progress = true;                                                                                   /* 需要下一次 init 的进度确认才能接受这个 PUT 的业务结果。 */
            continue;                                                                                                /* 返回 init；是否写到服务器只以新的对账为准。 */
        }

        /* 没有新片时先核实全文件连续完成及源摘要，再携本次会话标识提交。 */
        stage = "COMPLETE_PREFLIGHT";
        if (progress(res, r->size, true)) goto fail;                                                                 /* 即使没有待传项，提交前也要确认服务端连续收到整份文件。 */
        if ((ret = verify_source(w, r))) goto fail;                                                                  /* 再次计算全文摘要，防止校验后源内容发生变化。 */
        n = snprintf(w->body, sizeof(w->body),
            "{\"syncSessionId\":\"%s\",\"multipartUploadId\":\"%s\"}", w->session, w->upload);                       /* 提交 body 使用已验证后固定下来的会话和上传标识。 */
        if (n <= 0 || (size_t)n >= sizeof(w->body)) { ret = P4_ERR_LOCAL; goto fail; }                               /* 标识组合后的 JSON 若截断，不能向云端提交。 */
        stage = "COMPLETE";                                                                                          /* 使提交失败和之前的本地校验失败可区分。 */
        ret = signed_post(w, key, key_size, io, P4_COMPLETE_PATH);                                                   /* complete 同样取新时间及 nonce，并对提交正文签名。 */
        ret = request_error(ret, w->http, false);                                                                    /* complete 的 401/403 按设备鉴权失败分类。 */
        if (ret) goto retry;                                                                                         /* 请求结果不明确时仍先回到 init 对账。 */
        ret = P4_ERR_PROTOCOL;                                                                                       /* 对端发回的完成声明接下来要按实际字段再次验证。 */
        if (result(w, &res, &done) || !done || identity(w, res, true) || progress(res, r->size, true)) goto fail;    /* 完成回复必须确认成功，并与记忆的身份及全长相符。 */
        n = p4_json_get(res, "pendingParts", &pending);                                                              /* 完成后可选的待传列表仍须排除重复或非法值。 */
        if (n < 0 || (n && p4_json_array(pending, NULL) != 0)) goto fail;                                            /* 如果回复包含待传列表，列表必须为空。 */
        io->event(io->context, stage, (int)w->http, w->received);                                                    /* 记录已核对的服务端完成结果，随后还要验源。 */
        goto success;                                                                                                /* 所有成功出口统一再计算源的最终摘要。 */
retry:

        /* 只有归类为可恢复网络状态的错误能到达退避逻辑，其余错误立即报告。 */
        if (ret != P4_ERR_NETWORK) goto fail;
        io->event(io->context, stage, ret, w->http);                                                                 /* 记录失败发生的阶段和最近 HTTP 状态。 */
        if (w->requests >= P4_REQUEST_LIMIT) {
            ret = P4_ERR_LIMIT; goto fail;                                                                           /* 已耗尽总请求数时，不能以任何恢复政策继续。 */
        }
        /* 未得到 HTTP 状态时按首次失联起点计时。没有字节进展的 init 成功不能重置窗口，以免 PUT 或 complete 持续失败却一直续期。 */
        if (!w->http && strcmp(stage, "NO_PROGRESS")) {
            uint32_t now = io->now_ms(io->context), elapsed, delay;                                                  /* 单调毫秒时钟用于计算窗口内余量和本轮等待时间。 */
            if (!w->network_waiting) {
                w->network_waiting = true;                                                                           /* 开始一段无 HTTP 响应的恢复等待。 */
                w->network_started = now;                                                                            /* 只在首个无 HTTP 结果的故障处记起点，无进展的 init 不续期。 */
            }
            elapsed = (uint32_t)(now - w->network_started);                                                          /* 无符号差值计算本轮持续失联的时间，包括计数回绕。 */
            if (elapsed >= P4_NETWORK_WINDOW_MS) {
                stage = "NETWORK_LIMIT"; ret = P4_ERR_LIMIT; goto fail;                                              /* 全部窗口已用完，返回单独的网络限额阶段。 */
            }
            delay = w->network_retries < 4u ? 1000u << w->network_retries : P4_NETWORK_BACKOFF_MAX_MS;               /* 等待间隔依次为 1、2、4、8 秒，随后封顶 10 秒。 */
            if (delay > P4_NETWORK_WINDOW_MS - elapsed) delay = P4_NETWORK_WINDOW_MS - elapsed;                      /* 临近窗口结束时缩短最后一次睡眠，避免超出剩余预算。 */
            ++w->network_retries;                                                                                    /* 无响应重试单独计次，不消耗 HTTP 类的次数额度。 */
            io->event(io->context, "NETWORK_WAIT", w->network_retries, P4_NETWORK_WINDOW_MS - elapsed);              /* 事件中同时报告失联重试次数和窗口剩余毫秒。 */
            io->event(io->context, "RETRY_INIT", w->network_retries, delay);                                         /* 报告下次 init 前实际需要休眠多久。 */
            io->sleep_ms(io->context, delay);                                                                        /* 平台休眠回调在任务线程让出 CPU。 */
            continue;                                                                                                /* 醒来先查询服务器状态，避免重复提交。 */
        }
        /* HTTP 408、429、5xx、PUT 403 及服务端进度未增长按有界次数回退；认证、协议、源或链路错误已在此前退出。 */
        if (retries >= P4_RETRY_LIMIT) { ret = P4_ERR_LIMIT; goto fail; }                                            /* 限制可重试 HTTP 状态及无进展重试的次数。 */
        ++retries;                                                                                                   /* 把本次按 HTTP 结果或进度不足的重试计入额度。 */
        io->event(io->context, "RETRY_INIT", retries, 1000u << (retries - 1));                                       /* 向 Shell 报告重试次数及计算出的毫秒间隔。 */
        io->sleep_ms(io->context, 1000u << (retries - 1));                                                           /* 按 1、2、4 秒退避后返回循环顶端重新查询。 */
        /* 下一次循环先用 init 查询剩余工作，再决定是否发 PUT 或 complete。 */
    }
success:

    /* 云端完成后最后一次回读本地数据，确保成功标志和当前源文件相符。 */
    stage = "SOURCE_FINAL";
    if ((ret = verify_source(w, r))) goto fail;                                                                      /* 即使曾在此前查到已完成，此处仍需证实本地源可回读且摘要一致。 */
    io->event(io->context, "SYNC_SUCCESS", 0, r->size);                                                              /* 只有源最终摘要也正确才发布 SYNC_SUCCESS。 */
    return 0;                                                                                                        /* 业务和源校验都完成。 */
fail:
    if (ret == P4_ERR_LIMIT && network_expired(w, io)) stage = "NETWORK_LIMIT";                                      /* 若总时限确已耗尽，将错误阶段归到网络恢复窗口。 */
    io->event(io->context, stage, ret, w->http);                                                                     /* 用最后确定的阶段、错误类和 HTTP 状态上报。 */
    return ret;                                                                                                      /* 返回本次任务最终的业务错误类别。 */
}
