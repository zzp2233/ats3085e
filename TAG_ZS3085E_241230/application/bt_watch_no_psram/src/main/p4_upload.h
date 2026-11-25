/* P4 业务引擎与平台适配的共同接口。一个工作区对应一次同步调用，源录音在完整校验和重试期间均须可回读。 */
#ifndef P4_UPLOAD_H
#define P4_UPLOAD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tinycrypt/hmac.h>    /* 提供 SHA-256 与 HMAC 状态定义，二者在工作区内分阶段复用。 */
#include "p4_json.h"           /* 引入服务端响应的只读 JSON 视图。 */

#define P4_RESPONSE_MAX 6144u               /* P4 业务响应最多接收的正文数据字节数。 */
#define P4_SPEC_MAX 3072u                   /* 请求规格（方法、URL 和头）的最大字节数。 */
#define P4_PART_SIZE 4194304u               /* 默认分片提示 4 MiB，实际区间以服务端为准。 */
#define P4_REQUEST_LIMIT 128u               /* init、PUT 和 complete 共用的单任务请求上限。 */
#define P4_RETRY_LIMIT 3u                   /* 服务端可重试状态或没有进展时允许的重试次数。 */
/* 模组尚在重新注册时可能立即报网络失败。单独给无 HTTP 结果的失联情况设置总时长和退避上限。 */
#define P4_NETWORK_WINDOW_MS 180000u        /* 无 HTTP 状态的一轮网络恢复最长 180 秒。 */
#define P4_NETWORK_BACKOFF_MAX_MS 10000u    /* 网络等待指数回退封顶 10 秒，最后一次还受剩余窗口限制。 */
/* 只有适配层能确认的网络错误才进入恢复分支，未知错误按本地故障结束。 */
enum p4_error {
    P4_ERR_NETWORK = -1001, P4_ERR_LINK = -1002, P4_ERR_SOURCE = -1003,    /* 网络不可用、SPI 链路异常和源文件错误分别向上返回。 */
    P4_ERR_PROTOCOL = -1004, P4_ERR_AUTH = -1005, P4_ERR_LIMIT = -1006,    /* 区分数据或会话协议错误、鉴权失败和超出请求或等待限额。 */
    P4_ERR_LOCAL = -1007, P4_ERR_HTTP = -1008                              /* 分别表示本地配置或构造失败，以及其余 HTTP 错误。 */
};
#define P4_INIT_PATH "/voice-card/device/v1/oss/audio-sync/multipart/init"            /* init 同时承担新建会话和查询同一录音服务端进度。 */
#define P4_COMPLETE_PATH "/voice-card/device/v1/oss/audio-sync/multipart/complete"    /* 只有分片均已确认并再次验源才提交 complete。 */
#define P4_ORIGIN "https://lifeiot.cmaiot.cn:8081"                                    /* 设备签名 POST 的业务服务源站。 */
struct p4_record {
    void *source;                                                                    /* 由读取回调解释的源对象，上传调用结束前必须有效。 */
    int (*read)(void *source, uint32_t offset, uint8_t *data, unsigned int size);    /* 业务引擎按绝对字节偏移读 size 字节到 data，足量返回 0，失败返回非零。 */
    uint32_t size, duration_ms;                                                      /* 分别为完整文件长度（字节）和录音持续时间（毫秒）。 */
    const char *local_id, *filename, *recorded_at, *sample_md5, *sha256;             /* 本地唯一标识、文件名、录音时间、样本 MD5 和用于全文件校验的 SHA-256 文本。 */
    uint32_t part_size;                                                              /* 字节数；零采用 4 MiB。同一录音 ID 的提示应保持稳定。 */
};
struct p4_io {
    void *context;                                                                /* 下列所有平台回调的首参数；由调用方管理生命周期。 */
    int (*device)(void *context, char imei[16], uint32_t *utc);                   /* 签名前填入 15 位 IMEI 加结尾零，并返回 UTC 整秒；0 成功。 */
    int (*random)(void *context, uint8_t *out, unsigned int size);                /* 调用平台随机源，成功时恰好填写 size 字节并返回 0。 */
    /* request 同步发送规格和正文，在容量内写入响应并回填长度及 HTTP 状态。完整 2xx 且 SPI 正常收尾才返回 0，结果未知时交由业务层重新对账。 */
    int (*request)(void *context, const char *spec, const uint8_t *body,
                   const struct p4_record *record, uint32_t length, char *response, unsigned int capacity,
                   unsigned int *received, unsigned int *http);
    void (*event)(void *context, const char *stage, int code, uint32_t value);    /* 引擎用阶段名、状态与数值向平台报告进度，调用过程不保存指针。 */
    void (*sleep_ms)(void *context, uint32_t milliseconds);                       /* 由平台让当前线程休眠指定毫秒数后继续对账。 */
    uint32_t (*now_ms)(void *context);                                            /* 单调运行毫秒数，按 32 位无符号整数回绕。 */
};
struct p4_work {
    char response[P4_RESPONSE_MAX + 1], spec[P4_SPEC_MAX + 1], body[768];                     /* 存放业务响应、要下发的 HTTP 规格及待签名的 JSON 正文。 */
    char session[192], upload[192], imei[16];                                                 /* 会话及上传号跨循环核对，IMEI 用于确认重试期间设备身份一致。 */
    /* 签名临时区跟随工作区分配，避免占用 2 KiB Shell 线程栈。 */
    struct {
        uint8_t digest[32], random[16];                                                       /* SHA/HMAC 输出共享 32 字节暂存，随机数是 16 字节 nonce 的原料。 */
        char hash[65], signature[65], nonce[33], timestamp[24], imei[16];                     /* 将签名相关的二进制值编码为可提交的文本字段。 */
    } sign;
    union { struct tc_sha256_state_struct sha; struct tc_hmac_state_struct hmac; } crypto;    /* SHA 计算结束后才复用同一片内存作 HMAC 上下文。 */
    unsigned int received, http;                                                              /* 最近一次请求填入的响应字节数和 HTTP 状态码。 */
    uint32_t network_started;                                                                 /* 首次网络不可用时记录的单调毫秒计数。 */
    unsigned int network_retries;                                                             /* 自上次出现可见上传进展以来的网络等待次数。 */
    bool network_waiting;                                                                     /* 标识网络恢复窗口是否已经启动。 */
    uint32_t utc;                                                                             /* 由设备信息回调更新，用于 POST 的 UTC 整秒时间。 */
    const struct p4_record *original;                                                         /* 当前分片所借用的不可变原始录音描述。 */
    struct p4_record part;                                                                    /* 带区间读取回调和分片长度的临时源描述。 */
    uint32_t part_offset, part_size;                                                          /* 当前分片的绝对字节偏移，以及该 ID 的分片大小提示。 */
    unsigned int requests;                                                                    /* 已经向 I/O 回调发起的业务请求总数。 */
};
/* 源文件在整个调用期间保持可回读；服务端确认完成后再校验源摘要，调用者继续持有录音。 */
/**
  * @brief  核对不可变录音，按服务端进度执行 init、分片 PUT 和 complete。
  * @param  w：[输入输出]本次录音上传独占的工作区，不可为空。
  * @param  record：[输入]整个调用期间可重复按偏移读取的录音描述。
  * @param  key：[输入]至少 key_size 字节的设备签名密钥。
  * @param  key_size：[输入]密钥长度，单位字节。
  * @param  io：[输入]平台设备、随机数、传输、事件及定时回调表。
  * @retval 0 为服务端完成且源摘要再次校验通过，负 P4_ERR_* 表示失败类别。
  * @note   每次恢复从同一 localFileId 的 init 开始。请求次数、网络时限和无进展重试各有边界。
  */
int p4_upload_run(struct p4_work *w, const struct p4_record *record,
                  const uint8_t *key, size_t key_size, const struct p4_io *io);
#endif
