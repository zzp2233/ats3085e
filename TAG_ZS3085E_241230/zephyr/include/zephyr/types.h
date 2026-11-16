/* Zephyr基础整数与对齐类型。stdint.h提供uint8_t等固定宽度整数，stddef.h提供size_t等标准类型；它们属于标准库，不在本次修改范围。旧u*_t/s*_t别名仅在兼容配置下可见。 */
/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_TYPES_H_
#define ZEPHYR_INCLUDE_ZEPHYR_TYPES_H_    /* 保护typedef不重复展开。 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_DEPRECATED_ZEPHYR_INT_TYPES

typedef signed char         s8_t;     /* 兼容的8位有符号整数别名。 */
typedef signed short        s16_t;    /* 兼容的16位有符号整数别名。 */
typedef signed int          s32_t;    /* 兼容的32位有符号整数别名。 */
typedef signed long long    s64_t;    /* 兼容的64位有符号整数别名。 */

typedef unsigned char       u8_t;     /* 兼容的8位无符号整数别名。 */
typedef unsigned short      u16_t;    /* 兼容的16位无符号整数别名。 */
typedef unsigned int        u32_t;    /* 兼容的32位无符号整数别名。 */
typedef unsigned long long  u64_t;    /* 兼容的64位无符号整数别名。 */

#endif

/* ulong_t保持unsigned long宽度：ILP32为32位，LP64为64位，不保证所有平台固定32位。 */
typedef unsigned long       ulong_t;

/* 对齐需求较强的联合体，类似C11 max_align_t；各成员用于汇集整数、浮点、
 * 数据指针和函数指针的对齐要求，可用于栈/堆结构的通用对齐。它不是一组同时有效的数据字段。 */
typedef union {
	long long       thelonglong;          /* 引入long long所需对齐。 */
	long double     thelongdouble;        /* 引入long double所需对齐。 */
	uintmax_t       theuintmax_t;         /* 引入最大宽度无符号整型对齐。 */
	size_t          thesize_t;            /* 引入对象大小类型对齐。 */
	uintptr_t       theuintptr_t;         /* 引入可容纳指针整数类型的对齐。 */
	void            *thepvoid;            /* 引入数据指针对齐，不表示拥有目标内存。 */
	void            (*thepfunc)(void);    /* 引入函数指针对齐，无参无返回，仅作类型布局用途。 */
} z_max_align_t;

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_ZEPHYR_TYPES_H_ */
