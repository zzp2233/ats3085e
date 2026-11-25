/* P4 响应解析接口；视图只描述已借用的原始响应区，不拥有内存。 */
#ifndef P4_JSON_H
#define P4_JSON_H
#include <stdint.h>                         /* 公开无符号 32 位字段访问器。 */
#include <stddef.h>                         /* 使用 size_t 指定输入长度和输出缓冲容量。 */
/* 无堆分配的字节视图，[p, end) 借用原始缓冲。先验证完整文档，再访问其中的字段或数组。 */
struct p4_json { const char *p, *end; };    /* 两个指针分别为首字节与末字节之后的地址。 */
/**
  * @brief  验证按字节长度给出的完整 JSON 文档，返回根值视图。
  * @param  data：[输入]至少 size 字节的原始文本，无需以零结束。
  * @param  size：[输入]文档字节数。
  * @param  out：[输出]非空根值视图，成功时有效。
  * @retval 0 成功，-1 为语法错误、截断、过深或尾随内容。
  * @note   后续访问器接收这里验证过的视图，原始缓冲在使用期间须保持有效。
  */
int p4_json_document(const char *data, size_t size, struct p4_json *out);

/**
  * @brief  在已验证的对象中查找唯一指定字段。
  * @param  object：[输入]已验证的对象视图。
  * @param  key：[输入]以零结束的非空指针，用于区分大小写比较。
  * @param  out：[输出]非空字段视图，返回 1 时有效。
  * @retval 1 找到，0 缺失，-1 为错误或目标键重复。
  * @note   继续扫描到对象结束以发现同名键；扫描经过的键须能解入 80 字节临时区。
  */
int p4_json_get(struct p4_json object, const char *key, struct p4_json *out);

/**
  * @brief  将一个完整字符串视图解入缓冲并添加结尾零。
  * @param  value：[输入]已验证的单值视图。
  * @param  out：[输出]可写 capacity 字节的目标，可为 NULL。
  * @param  capacity：[输入]目标总字节数，含结尾零。
  * @retval 非负为解码长度，-1 为类型、编码约束、容量或尾随内容错误。
  * @note   输出限非零 ASCII 字符，传 NULL 时只按解析器的扫描规则计数。
  */
int p4_json_string(struct p4_json value, char *out, size_t capacity);

/**
  * @brief  将纯十进制数字视图解为无符号 32 位数。
  * @param  value：[输入]已验证的值视图。
  * @param  out：[输出]非空整数接收地址。
  * @retval 0 成功，-1 为空、含非数字或溢出。
  * @note   在乘十和加当前位之前检查 UINT32_MAX 边界。
  */
int p4_json_u32(struct p4_json value, uint32_t *out);

/**
  * @brief  统计已验证数组的元素个数，可同时借用首项视图。
  * @param  value：[输入]已验证的数组视图。
  * @param  first：[输出]可空，数组非空时写第一项视图。
  * @retval 非负为元素数，-1 为类型或解析错误。
  * @note   空数组返回零，此时不写首项。
  */
int p4_json_array(struct p4_json value, struct p4_json *first);

/**
  * @brief  按从零开始的下标返回已验证数组的一项。
  * @param  value：[输入]已验证的数组视图。
  * @param  index：[输入]元素下标，从 0 起。
  * @param  out：[输出]非空视图，成功时可读。
  * @retval 0 找到，-1 为无效数组、解析错误或下标越界。
  * @note   先跳过前面的完整元素，不分配数组索引。
  */
int p4_json_at(struct p4_json value, unsigned int index, struct p4_json *out);

/**
  * @brief  将 JSON 对象转换为逐行以 CRLF 终止的 HTTP 头部。
  * @param  value：[输入]已验证的头字段对象视图。
  * @param  out：[输出]至少 capacity 字节的可写缓冲，不可为空。
  * @param  capacity：[输入]包含结尾零的容量，单位字节。
  * @retval 非负为结果字节数（不含结尾零），-1 为解析、字段或容量错误。
  * @note   按 HTTP 规则忽略头名大小写判重，值限可打印 ASCII。
  */
int p4_json_headers(struct p4_json value, char *out, size_t capacity);
#endif
