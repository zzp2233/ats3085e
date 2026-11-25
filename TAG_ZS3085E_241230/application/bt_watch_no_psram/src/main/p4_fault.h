/* P4 诊断请求的故障选择与一次性触发记录，仅影响当前命令的业务 I/O。 */
#ifndef P4_FAULT_H
#define P4_FAULT_H
#include "p4_upload.h"                                             /* 使用业务层错误分类和两个签名 POST 路径来识别请求。 */
#include <string.h>

/* 诊断用一次性故障注入。每次 Shell 调用独立保存允许位和已触发位。BEFORE 模拟请求未发出；LOST 在真实事务成功后屏蔽响应，供业务引擎验证同 ID 对账路径。 */
enum p4_fault_mode {
    P4_FAULT_PUT_BEFORE = 1u, P4_FAULT_PUT_LOST = 2u,              /* 同一 PUT 阶段分别模拟请求未发送和真实成功后丢响应。 */
    P4_FAULT_COMPLETE_BEFORE = 4u, P4_FAULT_COMPLETE_LOST = 8u,    /* complete 阶段同样支持发送前及成功后的两种场景。 */
    P4_FAULT_PUT_403 = 16u, P4_FAULT_INIT_503 = 32u                /* 指定 PUT 授权错误和 init 服务暂不可用的测试位。 */
};
struct p4_fault { unsigned int mode, fired; };                     /* mode 记录本次允许的测试，fired 记录已消耗的单次事件。 */
/**
  * @brief  解析一次验证命令选择的故障类型。
  * @param  name：[输入]以零结束的测试模式名称。
  * @retval 正数为模式标记，-1 为未识别的名称。
  * @note   组合丢包模式可分别触发 PUT 和 complete；射频选项仅在对应测试构建开启。
  */
static int p4_fault_mode(const char *name)
{
    if (!strcmp(name, "put-before")) return P4_FAULT_PUT_BEFORE;                          /* PUT 即将发出时模拟本次完全未发送。 */
    if (!strcmp(name, "put-lost")) return P4_FAULT_PUT_LOST;                              /* PUT 真实成功后向业务层隐藏结果。 */
    if (!strcmp(name, "complete-before")) return P4_FAULT_COMPLETE_BEFORE;                /* complete 在真实调用之前模拟未到服务器。 */
    if (!strcmp(name, "complete-lost")) return P4_FAULT_COMPLETE_LOST;                    /* complete 已生效后隐藏确认。 */
    if (!strcmp(name, "both-lost")) return P4_FAULT_PUT_LOST | P4_FAULT_COMPLETE_LOST;    /* 同时测试两个结果丢失时，两位分别在各自请求后触发。 */
    if (!strcmp(name, "put-403")) return P4_FAULT_PUT_403;                                /* 只把第一次匹配的 PUT 改成 HTTP 403。 */
    if (!strcmp(name, "init-503")) return P4_FAULT_INIT_503;                              /* 只把第一次匹配的 init 改成 HTTP 503。 */

/* 射频构建的两个特殊选择交由平台适配层实际下发，从机负责恢复。 */
#ifdef P4_RADIO_TEST
    if (!strcmp(name, "radio-before")) return 64;                                         /* 返回射频诊断专用值，由适配器识别 init 前场景。 */
    if (!strcmp(name, "radio-mid")) return 128;                                           /* 返回 PUT 期间触发射频诊断的模式值。 */
#endif
    return -1;                                                                            /* 未识别的选择在发请求前即由命令行拒绝。 */
}

/**
  * @brief  在请求前后各自允许的位置触发一次指定故障。
  * @param  f：[输入输出]当前命令的配置和已触发位图。
  * @param  spec：[输入]以零结束的方法、URL 和头字段规格。
  * @param  after：[输入]true 表示真实调用完成后，false 表示调用前。
  * @param  ret：[输入]本次真实调用结果或发送前的初值。
  * @param  response：[输入输出]可写的响应区，至少可容纳已接收数据及首字节。
  * @param  received：[输入输出]响应字节数，注入后清零。
  * @param  http：[输入输出]HTTP 状态，注入时重置或设为指定错误码。
  * @retval 未命中沿用 ret，命中返回 P4_ERR_HTTP 或 P4_ERR_NETWORK。
  * @note   LOST 只丢弃真实成功的 2xx 结果；BEFORE 路径不调用底层请求。
  */
static int p4_fault_apply(struct p4_fault *f, const char *spec, bool after,
                          int ret, char *response, unsigned int *received, unsigned int *http)
{
    unsigned int bit = 0;                                                                                /* 保持零表示本次请求与任何注入阶段都不匹配。 */
    bool put = !strncmp(spec, "PUT\n", 4);                                                               /* 从规格首行区分 PUT 与 POST。 */
    bool complete = !strncmp(spec, "POST\n" P4_ORIGIN P4_COMPLETE_PATH "\n",
                              sizeof("POST\n" P4_ORIGIN P4_COMPLETE_PATH "\n") - 1);                     /* 同为 POST 时还需核对完整的 complete 路径。 */
    if (after) {                                                                                         /* 真实 I/O 已返回，此时只能模拟已成功请求的结果丢失。 */
        /* 已发生的真实失败必须透传；只有收到完整 2xx 的请求才可能模拟丢失结果。 */
        if (ret || *http < 200 || *http >= 300) return ret;
        if (put) bit = P4_FAULT_PUT_LOST;                                                                /* 将成功 PUT 映射到丢响应配置位。 */
        else if (complete) bit = P4_FAULT_COMPLETE_LOST;                                                 /* 独立选择成功 complete 的配置位。 */
    } else {
        if (put) bit = (f->mode & P4_FAULT_PUT_403) ? P4_FAULT_PUT_403 : P4_FAULT_PUT_BEFORE;            /* PUT 发送前可选择模拟 HTTP 403 或模拟请求根本未发出。 */
        else if (complete) bit = P4_FAULT_COMPLETE_BEFORE;                                               /* complete 请求只接受相应的发送前注入。 */
        else if (!strncmp(spec, "POST\n" P4_ORIGIN P4_INIT_PATH "\n",
                          sizeof("POST\n" P4_ORIGIN P4_INIT_PATH "\n") - 1)) bit = P4_FAULT_INIT_503;    /* init 只有完整路径匹配才允许模拟服务不可用。 */
    }
    if (!(f->mode & bit) || (f->fired & bit)) return ret;                                                /* 未选中的位或已经触发的位保留真实返回。 */
    f->fired |= bit;                                                                                     /* 给当前任务的位图打标记，避免对账循环反复注入。 */
    /* 清除业务可见的响应及长度，并按配置生成 HTTP 状态或网络错误。 */
    memset(response, 0, *received);                                                                      /* 擦除真实响应曾经包含的全部有效字节。 */
    response[0] = 0; *received = 0;                                                                      /* 首字节和有效长度同时清零，避免业务读取前一响应。 */
    *http = bit == P4_FAULT_PUT_403 ? 403u : bit == P4_FAULT_INIT_503 ? 503u : 0u;                       /* 只有指定 HTTP 场景提供状态码，丢包或未发送场景返回零状态。 */
    return *http ? P4_ERR_HTTP : P4_ERR_NETWORK;                                                         /* 有明确模拟 HTTP 码时交给 HTTP 分类器，否则报告网络不可用。 */
}
#endif
