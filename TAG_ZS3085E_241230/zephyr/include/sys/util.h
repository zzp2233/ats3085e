/* 包含保护防止重复定义；util_macro.h 提供 BIT、条件展开等纯预处理接口，C 分支另依赖固定宽度整数、size_t 和 offsetof；C++ 使用 C 链接声明外部函数。 */
/*
 * Copyright (c) 2011-2014, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 内核和应用共用的类型转换、数组、对齐、数值及字符转换工具；SPI 使用这里的数组大小和取最小值等宏。 */

#ifndef ZEPHYR_INCLUDE_SYS_UTIL_H_
#define ZEPHYR_INCLUDE_SYS_UTIL_H_

#include <sys/util_macro.h>

/* stdbool 必须在汇编分支之外包含，让汇编或链接脚本也能把 true/false 转成 1/0。 */
#include <stdbool.h>

#ifndef _ASMLANGUAGE

#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sys-util 通用工具接口
 * @{
 */

/* 把指针 x 转换为可容纳指针的 uintptr_t 无符号整数，不解引用地址。 */
#define POINTER_TO_UINT(x) ((uintptr_t) (x))
/* 经 uintptr_t 把无符号整数 x 转为 void 指针，不创建指向的对象。 */
#define UINT_TO_POINTER(x) ((void *) (uintptr_t) (x))
/* 把指针 x 转换为可容纳指针的 intptr_t 有符号整数。 */
#define POINTER_TO_INT(x)  ((intptr_t) (x))
/* 经 intptr_t 把有符号整数 x 转为 void 指针，不校验地址有效性。 */
#define INT_TO_POINTER(x)  ((void *) (intptr_t) (x))

#if !(defined(__CHAR_BIT__) && defined(__SIZEOF_LONG__))
#	error Missing required predefined macros for BITS_PER_LONG calculation
#endif

/* long 类型的位数，由每字节位数和 long 字节数相乘得到；缺少编译器预定义值时上方直接报错。 */
#define BITS_PER_LONG	(__CHAR_BIT__ * __SIZEOF_LONG__)

/* 构造从位 l 到位 h（两端包含）的连续掩码；要求 0 <= l <= h < BITS_PER_LONG，不能越宽移位。 */
#define GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

/* cond 非零时产生常量 0；cond 为零时利用非法数组长度使编译失败。 */
#define ZERO_OR_COMPILE_ERROR(cond) ((int) sizeof(char[1 - 2 * !(cond)]) - 1)

#if defined(__cplusplus)

/* GNU C++ 不支持下面 C 分支用于识别数组的类型兼容内建函数，因此 C++ 仅以 sizeof 计算数量，不拒绝指针或自定义下标对象。 */
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#else    /* __cplusplus */

/* 仅 C 分支检查 array 是否真为数组；数组得到 0，传指针则编译失败，不能在 C++ 使用。 */
#define IS_ARRAY(array) \
	ZERO_OR_COMPILE_ERROR( \
		!__builtin_types_compatible_p(__typeof__(array), \
					      __typeof__(&(array)[0])))

/* 计算真正数组的元素数；C 分支用 IS_ARRAY 拒绝指针。C++ 分支接受可下标对象但结果未必有意义，不能用指针代替数组。 */
#define ARRAY_SIZE(array) \
	((long) (IS_ARRAY(array) + (sizeof(array) / sizeof((array)[0]))))

#endif    /* __cplusplus */

/* 判断非空 ptr 是否落在 array 的首元素至末端边界之间；要求参与比较的地址具有适当的同一数组关系。C 分支同时检查 array 不能为指针，宏参数可能重复求值。 */
#define PART_OF_ARRAY(array, ptr) \
	((ptr) && ((ptr) >= &array[0] && (ptr) < &array[ARRAY_SIZE(array)]))

/* 由结构成员地址 ptr 减去 offsetof(type, field) 得到所属结构地址；ptr 必须确实指向该类型的 field。例：ptr = &my_foo.bar 时，CONTAINER_OF(ptr, struct foo, bar) 返回 &my_foo；不分配、不复制对象。 */
#define CONTAINER_OF(ptr, type, field) \
	((type *)(((char *)(ptr)) - offsetof(type, field)))

/* 把数值或地址 x 向上对齐到 align 的倍数；align 必须为非零的 2 的幂，加法不能溢出。 */
#define ROUND_UP(x, align)                                   \
	(((unsigned long)(x) + ((unsigned long)(align) - 1)) & \
	 ~((unsigned long)(align) - 1))

/* 把 x 向下对齐到 align 的倍数；align 必须为非零的 2 的幂。 */
#define ROUND_DOWN(x, align)                                 \
	((unsigned long)(x) & ~((unsigned long)(align) - 1))

/* 把 x 向上对齐到 sizeof(void *) 的字边界。 */
#define WB_UP(x) ROUND_UP(x, sizeof(void *))

/* 把 x 向下对齐到 sizeof(void *) 的字边界。 */
#define WB_DN(x) ROUND_DOWN(x, sizeof(void *))

/* 计算非负整数 numerator/divider 的向上取整商；divider 必须大于零，并避免加法溢出。 */
#define ceiling_fraction(numerator, divider) \
	(((numerator) + ((divider) - 1)) / (divider))

/* 返回 a 和 b 中较大者；比较及结果选择会重复求值参数，不能传带副作用的表达式。 */
#ifndef MAX
/* 仅 GCC 可改用单次求值的 Z_MAX；本宏保持通用重复求值行为。 */
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/* 返回 a 和 b 中较小者；比较及结果选择会重复求值参数，不能传带副作用的表达式。 */
#ifndef MIN
/* 仅 GCC 可改用单次求值的 Z_MIN；本宏仍保持原展开。 */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* 把 val 裁到闭区间 [low, high]；调用方保证 low <= high。参数可多次求值，禁止依赖副作用。 */
#ifndef CLAMP
/* 仅 GCC 可选用单次求值的 Z_CLAMP；本定义保留原重复求值方式。 */
#define CLAMP(val, low, high) (((val) <= (low)) ? (low) : MIN(val, high))
#endif

/**
  * @brief  判断非零无符号数是否只有一个置位。
  * @param  x：[输入]待检查的无符号整数。
  * @retval 非零且为 2 的幂返回 true，0 或其他数返回 false。
  * @note   不访问硬件或共享状态。
  */
static inline bool is_power_of_two(unsigned int x)
{
	return (x != 0U) && ((x & (x - 1U)) == 0U);    /* 先排除 0，再利用减一会清除最低置位并翻转低位的性质。 */
}

/**
  * @brief  按原有实现对 64 位整数作符号扩展右移。
  * @param  value：[输入]待移位的有符号 64 位值。
  * @param  shift：[输入]右移位数，合法范围 0 到 63。
  * @retval 右移并补符号位的计算结果。
  * @note   shift 为 0 直接返回。原实现包含负有符号数左移，按标准 C 存在未定义行为风险；此处记录限制，不修改运算或声称完全可移植。
  */
static inline int64_t arithmetic_shift_right(int64_t value, uint8_t shift)
{
	int64_t sign_ext;                                        /* 暂存由原符号位扩展出的全零或全一量。 */

	/* 先处理不移位情形，再执行符号扩展计算。 */
	if (shift == 0U) {                                       /* 避免后续表达式出现按 64 位移位。 */
		return value;                                    /* 零移位保持原值。 */
	}

	/* 提取原值的符号位。 */
	sign_ext = (value >> 63) & 1;                            /* 保留最高位的符号信息为 0 或 1。 */

	/* 把符号位扩展成全零或全一的 64 位量。 */
	sign_ext = -sign_ext;                                    /* 原符号为 1 时得到 -1，为后续高位补一准备。 */

	/* 将原值右移，再尝试用符号位补齐高位。 */
	return (value >> shift) | (sign_ext << (64 - shift));    /* 合并移位值与高位符号扩展；保留原有符号移位风险。 */
}

/**
  * @brief  以逐字节索引循环复制指定长度内存。
  * @param  dst：[输出]可写目标区，至少 size 字节；size 非零时不可为空。
  * @param  src：[输入]可读源区，至少 size 字节；size 非零时不可为空。
  * @param  size：[输入]复制长度，字节。
  * @retval 无。
  * @note   调用方保证区域适于此前向复制；不提供 memmove 重叠保证。普通非 volatile C 访问不能代替寄存器访问约束或内存屏障。
  */
static inline void bytecpy(void *dst, const void *src, size_t size)
{
	size_t i;                                             /* 遍历从 0 到 size-1 的源和目标字节索引。 */

	/* 按字节前向遍历内存区；此实现没有重叠方向判断。 */
	for (i = 0; i < size; ++i) {                          /* 按地址递增复制，恰好处理 size 个字节。 */
		((uint8_t *)dst)[i] = ((uint8_t *)src)[i];    /* 读出一个源字节并写到同偏移目标位置。 */
	}
}

/**
  * @brief  将一个十六进制字符转换为半字节值。
  * @param  c：[输入]字符 0-9、a-f 或 A-F。
  * @param  x：[输出]非空可写字节，成功时接收 0 到 15。
  * @retval 0 成功；-EINVAL 字符不在合法范围。
  * @note   失败不写输出值，大小写字母均接受。
  */
int char2hex(char c, uint8_t *x);

/**
  * @brief  将半字节数值转换为小写十六进制字符。
  * @param  x：[输入]数值 0 到 15。
  * @param  c：[输出]非空可写字符对象。
  * @retval 0 成功；-EINVAL 数值大于 15。
  * @note   失败不写目标字符。
  */
int hex2char(uint8_t x, char *c);

/**
  * @brief  将二进制数据转换为以零结束的小写十六进制串。
  * @param  buf：[输入]至少 buflen 字节的可读源区，非空数据时不可为空。
  * @param  buflen：[输入]源字节数。
  * @param  hex：[输出]非空可写目标区，必须实际容纳 2 * buflen + 1 字节。
  * @param  hexlen：[输入]调用方提供的目标容量，字节。
  * @retval 成功返回 2 * buflen，不含结束零；容量判断失败或转换错误返回 0。
  * @note   当前 hex.c 的容量检查为 (hexlen + 1) < buflen * 2，未严格保证最后的结束零写入安全；调用方仍必须满足实际容量要求并避免长度乘法溢出。
  */
size_t bin2hex(const uint8_t *buf, size_t buflen, char *hex, size_t hexlen);

/**
  * @brief  将十六进制字符区转换为二进制字节。
  * @param  hex：[输入]至少 hexlen 个可读十六进制字符，无需结束零。
  * @param  hexlen：[输入]源字符数。
  * @param  buf：[输出]可写目标区，至少向上取整 hexlen/2 字节。
  * @param  buflen：[输入]目标容量，字节。
  * @retval 成功返回 (hexlen + 1)/2 字节；容量不足或字符非法返回 0。
  * @note   奇数字符数时首字符按高半字节补零处理；失败前可能已经写入部分输出。
  */
size_t hex2bin(const char *hex, size_t hexlen, uint8_t *buf, size_t buflen);

/**
  * @brief  将两位 8421 BCD 编码换算为二进制整数。
  * @param  bcd：[输入]高低半字节分别为十位和个位，均应为 0 到 9。
  * @retval 对应的 0 到 99 数值。
  * @note   不检查非法 BCD 数字；不是字符编码转换。
  */
static inline uint8_t bcd2bin(uint8_t bcd)
{
	return ((10 * (bcd >> 4)) + (bcd & 0x0F));    /* 十位乘十加个位，不校验两个半字节是否合法。 */
}

/**
  * @brief  将两位十进制数编码为 8421 BCD。
  * @param  bin：[输入]有效数值 0 到 99。
  * @retval 高半字节为十位、低半字节为个位的 BCD 字节。
  * @note   不检查范围；超范围输入不能解释为有效的两位 BCD。
  */
static inline uint8_t bin2bcd(uint8_t bin)
{
	return (((bin / 10) << 4) | (bin % 10));    /* 把商作为高半字节、余数作为低半字节组合。 */
}

/**
  * @brief  将无符号字节值写成十进制字符。
  * @param  buf：[输出]可写目标区，至少 buflen 字节；容量非零时不可为空。
  * @param  buflen：[输入]目标容量，字节，最多三位数字加一个结束零。
  * @param  value：[输入]待转换的 0 到 255 数值。
  * @retval 实际写入的数字字符数，不含结束零；容量为 0 时返回 0，容量不足时可能只写前部数字。
  * @note   仅剩余容量足够时追加结束零，不保证小缓冲区总能形成完整 C 字符串。
  */
uint8_t u8_to_dec(char *buf, uint8_t buflen, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif    /* !_ASMLANGUAGE */

/* 将 x KiB 换算成字节，比例为 1024；调用方须避免类型宽度溢出。 */
#ifdef _LINKER
/* 链接脚本不能使用 C 类型转换，因此 _LINKER 分支只作左移。 */
#define KB(x) ((x) << 10)
#else
#define KB(x) (((size_t)x) << 10)
#endif
/* 将 x MiB 换算成字节，比例为 1024 * 1024。 */
#define MB(x) (KB(x) << 10)
/* 将 x GiB 换算成字节，按 MB 宏继续左移 10 位。 */
#define GB(x) (MB(x) << 10)

/* 将 x kHz 换算为 Hz，比例为 1000。 */
#define KHZ(x) ((x) * 1000)
/* 将 x MHz 换算为 Hz，比例为 1000000。 */
#define MHZ(x) (KHZ(x) * 1000)

/**
 * @}
 */

#endif    /* ZEPHYR_INCLUDE_SYS_UTIL_H_ */
