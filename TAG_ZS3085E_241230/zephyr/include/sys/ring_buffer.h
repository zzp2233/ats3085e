/* 软件环形队列接口。控制对象持续保存读写位置，字节模式传递串口等原始字节，条目模式额外存储类型、附带值和长度。 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file */

/* 头文件保护覆盖 C 与 C++ 调用方。 */
#ifndef ZEPHYR_INCLUDE_SYS_RING_BUFFER_H_
#define ZEPHYR_INCLUDE_SYS_RING_BUFFER_H_

#include <kernel.h>      /* 内核类型及自旋锁定义。 */
#include <sys/util.h>    /* 提供容量位运算与构建断言等工具。 */
#include <errno.h>       /* 定义读取空队列、容量不足等错误码。 */
#include <string.h>      /* 内联初始化函数使用 memset 清除游标。 */

/* C++ 引用本接口时使用 C 的链接名。 */
#ifdef __cplusplus
extern "C" {
#endif

/* 将对象占用的字节数换算成完整的 32 位字数。 */
#define SIZE32_OF(x) (sizeof((x))/sizeof(uint32_t))

/* 容量必须小于该限值，使读写计数的距离和跨阈值修正保持可表示。 */
#define RING_BUFFER_MAX_SIZE 0x80000000

/* 静态声明和运行时初始化共用同一条断言提示。 */
#define RING_BUFFER_SIZE_ASSERT_MSG \
	"Size too big, if it is the ring buffer test check custom max size"
/** @brief 环的存储、容量与逻辑游标。head 由读方提交，tail 由写方提交；访问模式由调用者在初始化和使用时保持一致。 */
struct ring_buf {
	uint32_t head;    /* 读方已消费到的位置，指向下一份可读数据。 */
	uint32_t tail;    /* 写方已发布到的位置，指向下一处空位。 */
	union ring_buf_misc {
		struct ring_buf_misc_item_mode {
			uint32_t dropped_put_count;    /* 条目模式因空间不足而未能入队的累计次数。 */
		} item_mode;
		struct ring_buf_misc_byte_mode {
			uint32_t tmp_tail;    /* 写方临时预留到的位置；put_finish 后恢复到实际已提交的 tail。 */
			uint32_t tmp_head;    /* 读方临时查看到的位置；get_finish 后恢复到实际已消费的 head。 */
		} byte_mode;
	} misc;
	uint32_t size;    /* 环的容量，单位随模式为字节或 32 位字。 */

	union ring_buf_buffer {
		uint32_t *buf32;    /* 条目模式，以 32 位字为单位索引。 */
		uint8_t *buf8;      /* 字节模式，UART 接收队列通过这个视图访问。 */
	} buf;
	uint32_t mask;    /* 非零时以按位与得到数组下标，零表示按容量取余。 */

	struct k_spinlock lock;    /* 随实例保存的自旋锁对象；这些基础访问函数不自动为多个同向访问者加锁。 */
};

/**
 * @defgroup ring_buffer_apis 环形缓冲接口
 * @ingroup datastructure_apis
 * @{
 */

/**
  * @brief  定义条目队列，容量为 2 的 pow 次幂个 32 位字，物理索引用掩码回绕。
  * @param  name [输入] 定义的控制对象名；底层静态字数组由宏生成。可在其他模块声明 extern struct ring_buf name。
  * @param  pow [输入] 容量的二进制指数，生成的容量必须小于 RING_BUFFER_MAX_SIZE。
  */
#define RING_BUF_ITEM_DECLARE_POW2(name, pow) \
	BUILD_ASSERT((1 << pow) < RING_BUFFER_MAX_SIZE,\
		RING_BUFFER_SIZE_ASSERT_MSG); \
	static uint32_t __noinit _ring_buffer_data_##name[BIT(pow)]; \
	struct ring_buf name = { \
		.size = (BIT(pow)),	  \
		.mask = (BIT(pow)) - 1, \
		.buf = { .buf32 = _ring_buffer_data_##name } \
	}

/**
  * @brief  定义给定容量的条目队列和配套静态数组，控制对象的零值掩码使索引使用取余。
  * @param  name [输入] 定义的控制对象名，其他模块可用 extern struct ring_buf name 引用。
  * @param  size32 [输入] 数组容量，以 32 位字为单位，需大于零并小于 RING_BUFFER_MAX_SIZE。
  */
#define RING_BUF_ITEM_DECLARE_SIZE(name, size32) \
	BUILD_ASSERT(size32 < RING_BUFFER_MAX_SIZE,\
		RING_BUFFER_SIZE_ASSERT_MSG); \
	static uint32_t __noinit _ring_buffer_data_##name[size32]; \
	struct ring_buf name = { \
		.size = size32, \
		.buf = { .buf32 = _ring_buffer_data_##name} \
	}

/**
  * @brief  定义字节队列及其静态数组。存储内容无须预先置零，起始读写位置相等即为空。
  * @param  name [输入] 控制对象名，其他模块可用 extern struct ring_buf name 引用。
  * @param  size8 [输入] 容量，以字节为单位，需大于零并小于 RING_BUFFER_MAX_SIZE。
  */
#define RING_BUF_DECLARE(name, size8) \
	BUILD_ASSERT(size8 < RING_BUFFER_MAX_SIZE,\
		RING_BUFFER_SIZE_ASSERT_MSG); \
	static uint8_t __noinit _ring_buffer_data_##name[size8]; \
	struct ring_buf name = { \
		.size = size8, \
		.buf = { .buf8 = _ring_buffer_data_##name} \
	}


/**
  * @brief  将调用者提供的控制对象和数据区初始化为空环。
  * @param  buf [输出] 非空可写控制对象。
  * @param  size [输入] 字节模式填字节数，条目模式填 32 位字数；使用时须为正且小于上限。
  * @param  data [输入] 调用者维护的存储，按选定模式至少容纳 size 个元素，使用期间地址必须有效。
  * @retval 无。
  * @note   供未通过静态声明宏创建的实例使用。容量为 2 的幂时建立回绕掩码。
  */
static inline void ring_buf_init(struct ring_buf *buf,
				 uint32_t size,
				 void *data)
{
	__ASSERT(size < RING_BUFFER_MAX_SIZE, RING_BUFFER_SIZE_ASSERT_MSG);    /* 检查逻辑计数算法允许的最大容量。 */

	memset(buf, 0, sizeof(struct ring_buf));                               /* 将读写游标、临时位置及状态初始化为零。 */
	buf->size = size;                                                      /* 记录调用方按当前模式给出的容量。 */
	buf->buf.buf32 = (uint32_t *)data;                                     /* 将数据区基址存入联合体，之后按实际模式取对应成员。 */
	if (is_power_of_two(size)) {                                           /* 判断是否可将下标回绕化简为位运算。 */
		buf->mask = size - 1U;                                         /* 例如容量为 64 时，取低 6 位即可定位物理位置。 */
	} else {
		buf->mask = 0U;                                                /* 其他有效容量在索引时取余。 */
	}
}

/**
  * @brief  比较已提交的读、写位置，判断队列是否为空。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @retval 1 为空，0 为有数据。
  * @note   只观察已提交的位置，字节模式中的临时预留不单独计入。
  */
int ring_buf_is_empty(struct ring_buf *buf);

/**
  * @brief  清空队列游标及模式相关状态。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @retval 无。
  * @note   容量和数据区保持原绑定，数组内旧字节不再可读；确保没有其他上下文正在读写。
  */
static inline void ring_buf_reset(struct ring_buf *buf)
{
	buf->head = 0;                               /* 放弃已入队数据，将消费起点复位。 */
	buf->tail = 0;                               /* 使发布位置与消费位置重合，队列变为空。 */
	memset(&buf->misc, 0, sizeof(buf->misc));    /* 清除条目模式失败计数，或字节模式的两个临时游标。 */
}

/**
  * @brief  计算扣除已入队数据后的剩余容量。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @retval 字节模式为字节数，条目模式为剩余的 32 位字数。
  * @note   只使用已提交位置；进行中的临时写预留由 put_claim 单独扣除。
  */
uint32_t ring_buf_space_get(struct ring_buf *buf);

/**
  * @brief  取得实例初始化时给定的总容量。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @retval 字节模式返回字节数；条目模式返回 32 位字数。
  */
static inline uint32_t ring_buf_capacity_get(struct ring_buf *buf)
{
	return buf->size;    /* 返回初始化时设置的元素数。 */
}

/**
  * @brief  从已提交写位置和读位置计算队列占用量。
  * @param  buf [输入] 已初始化且非空的环形缓冲控制对象。
  * @retval 字节模式按字节，条目模式按 32 位字计数且包括各条目的头。
  */
uint32_t ring_buf_size_get(struct ring_buf *buf);

/**
  * @brief  将类型、附带值和一段 32 位数据作为一个完整条目入队。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  type [输入] 由应用解释的 16 位类型标识。
  * @param  value [输入] 随条目保存的 8 位数值。
  * @param  data [输入] 至少 size32 个可读的 32 位字，长度为零时可为空。
  * @param  size32 [输入] 正文字数，0～255；头部另占一个 32 位字。
  * @retval 0 表示条目已提交；-EMSGSIZE 表示空位不足以容纳完整条目。
  * @note   同一实例使用条目接口，多个写方须互斥或以其他方式避免同时写入。
  */
int ring_buf_item_put(struct ring_buf *buf, uint16_t type, uint8_t value,
		      uint32_t *data, uint8_t size32);

/**
  * @brief  取出队首条目的类型、附带值和可选正文。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  type [输出] 非空指针，成功时接收类型标识。
  * @param  value [输出] 非空指针，成功时接收条目附带的 8 位数值。
  * @param  data [输出] 可写数组容量由 *size32 给定；传空会消费条目并丢弃正文。
  * @param  size32 [输入输出] 非空；输入正文容量，成功或容量不足时返回实际所需的 32 位字数。
  * @retval 0 表示已消费；-EAGAIN 表示缓冲为空；-EMSGSIZE 表示输出容量不足，此时条目保留。
  * @note   同一实例使用条目接口，多个读方须串行化读取。
  */
int ring_buf_item_get(struct ring_buf *buf, uint16_t *type, uint8_t *value,
		      uint32_t *data, uint8_t *size32);

/**
  * @brief  在内部字节数组中预留一段连续可写空间。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 非空地址槽，接收内部写指针，仅可写返回长度覆盖的区域。
  * @param  size [输入] 希望预留的字节数。
  * @retval 本段可写字节数，受空位和数组尾端限制，可为零。
  * @note   同一实例使用字节模式；多个写方须串行化整个写操作，组合使用 claim/finish 时覆盖两步之间的区间。 缓冲跨尾时可再次申请，写完用 @ref ring_buf_put_finish 发布实际字节数。
  */
uint32_t ring_buf_put_claim(struct ring_buf *buf,
			    uint8_t **data,
			    uint32_t size);

/**
  * @brief  发布写方已写好的字节，并撤销多余的连续预留。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  size [输入] 从本轮预留起点实际写好的字节数，可传零放弃。
  * @retval 0 为已更新提交位置；-EINVAL 表示提交位置会越过总可写界限。
  * @note   同一实例使用字节模式；多个写方须串行化整个写操作，组合使用 claim/finish 时覆盖两步之间的区间。 调用者负责保证提交量在自己已经取得并写好的空间内。
  */
int ring_buf_put_finish(struct ring_buf *buf, uint32_t size);

/**
  * @brief  把调用者的数据复制进字节队列，在各段复制完成后一并发布。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输入] 指向至少 size 字节的可读数据。
  * @param  size [输入] 希望写入的字节数。
  * @retval 实际提交的字节数；空位不足时可以只写一部分或返回零。
  * @note   同一实例使用字节模式；多个写方须串行化整个写操作，组合使用 claim/finish 时覆盖两步之间的区间。
  */
uint32_t ring_buf_put(struct ring_buf *buf, const uint8_t *data, uint32_t size);

/**
  * @brief  取得队首尚未消费数据的一段连续内部地址。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 非空地址槽，接收内部指针，只能读取返回长度内的数据。
  * @param  size [输入] 请求读取的字节数。
  * @retval 当次授予的连续可读字节数，受已发布量及数组尾部限制。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 使用后调用 @ref ring_buf_get_finish，已释放区域可能立即被写方覆盖。
  */
uint32_t ring_buf_get_claim(struct ring_buf *buf,
			    uint8_t **data,
			    uint32_t size);

/**
  * @brief  确认已处理的字节数，将对应环内空间归还给写方。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  size [输入] 从本轮预取起点消费的字节数，零表示保留全部数据。
  * @retval 0 为成功；-EINVAL 表示拟消费范围越过已发布的数据。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 调用者负责保证消费量在已取得的数据范围内。
  */
int ring_buf_get_finish(struct ring_buf *buf, uint32_t size);

/**
  * @brief  从字节队列取走已发布的数据，必要时分两段越过数组末端。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 有效时为至少 size 字节的可写区域；传空会丢弃实际取到的字节。
  * @param  size [输入] 希望消费的字节数。
  * @retval 实际消费的字节数，空队列返回零。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 UART Shell 在读后端中用它从 rx_ringbuf 取字节，函数本身不解析命令或等待中断。
  */
uint32_t ring_buf_get(struct ring_buf *buf, uint8_t *data, uint32_t size);

/**
  * @brief  复制已发布的队首数据，保留原队首供后续读取。
  * @param  buf [输入输出] 已初始化且非空的环形缓冲控制对象。
  * @param  data [输出] 非空，指向至少 size 字节的可写缓冲。
  * @param  size [输入] 最多要查看的字节数。
  * @retval 实际复制的字节数，可小于请求值。
  * @note   同一实例使用字节模式；多个读方须串行化整个读操作，组合使用 claim/finish 时覆盖两步之间的区间。 多次查看可读到同一数据；要消费可用 @ref ring_buf_get 或非零长度的 @ref ring_buf_get_finish。
  */
uint32_t ring_buf_peek(struct ring_buf *buf, uint8_t *data, uint32_t size);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_RING_BUFFER_H_ */
