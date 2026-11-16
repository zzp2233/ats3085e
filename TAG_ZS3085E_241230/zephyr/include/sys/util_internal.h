/*
 * Copyright (c) 2011-2014, Wind River Systems, Inc.
 * Copyright (c) 2020, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 通用宏的底层展开机制。
 * sys/util.h 使用的配置检测、条件选码、列表过滤和宏映射辅助定义。
 */

#ifndef ZEPHYR_INCLUDE_SYS_UTIL_INTERNAL_H_
#define ZEPHYR_INCLUDE_SYS_UTIL_INTERNAL_H_

#include "util_loops.h"

/* 包含保护及列表重复展开引擎；SPI 实际使用配置判断路径，不因其他列表宏而扩展所有辅助模块。 */

/* 将配置值拼接到 _XXXX 后面：值为 1 得到 _XXXX1，未定义的名字则保留为普通预处理记号。 */
#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)

/* 仅 _XXXX1 被替换成末尾带逗号的 _YYYY,，把“配置值为 1”转化为额外的宏参数分隔符。 */
#define _XXXX1 _YYYY,

/* 追加 1 和 0：存在上一步逗号时第二参数为 1，否则第二参数为 0；额外参数兼容可变参数展开。 */
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)

/* 取第二参数，从而得到 IS_ENABLED 所需的整数常量 1 或 0。 */
#define Z_IS_ENABLED3(ignore_this, val, ...) val

/* 条件选码的内部实现：_XXXX1 和 _ZZZZ0 各自在标志匹配时生成逗号，选择对应的括号代码块。 */
#define Z_COND_CODE_1(_flag, _if_1_code, _else_code) \
	__COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
#define Z_COND_CODE_0(_flag, _if_0_code, _else_code) \
	__COND_CODE(_ZZZZ##_flag, _if_0_code, _else_code)
/* 值为 0 的条件检测专用哨兵，与检测 1 的 _XXXX1 采用相同逗号机制。 */
#define _ZZZZ0 _YYYY,
#define __COND_CODE(one_or_two_args, _if_code, _else_code) \
	__GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)

/* 取第二参数并剥去外层括号；调用方必须用括号包围候选代码，以保护其中的逗号。 */
#define __GET_ARG2_DEBRACKET(ignore_this, val, ...) __DEBRACKET val

/* 去掉选中代码最外层的一对括号，返回其中的可变参数记号。 */
#define __DEBRACKET(...) __VA_ARGS__

/* IS_EMPTY 的分层展开：拼接空项探测记号，再借 GET_ARG_N 提取空/非空结果。 */
#define Z_IS_EMPTY_(...) Z_IS_EMPTY__(__VA_ARGS__)
#define Z_IS_EMPTY__(a, ...) Z_IS_EMPTY___(_ZZ##a##ZZ0, __VA_ARGS__)
#define Z_IS_EMPTY___(...) GET_ARG_N(3, __VA_ARGS__)

/* LIST_DROP_EMPTY 的内部宏。 */
/* 给保留项前置逗号而非后置逗号，避免产生难以删除的末尾空项；统一生成的首个空项由 DROP_FIRST 去掉。 */
#define Z_LIST_ADD_ELEM(e) EMPTY, e
#define Z_LIST_DROP_FIRST(...) GET_ARGS_LESS_N(1, __VA_ARGS__)
#define Z_LIST_NO_EMPTIES(e) \
	COND_CODE_1(IS_EMPTY(e), (), (Z_LIST_ADD_ELEM(e)))

/* 先展开实参再拼接；PRIMITIVE_CAT 直接使用 ##，其相邻实参不会先展开。 */
#define UTIL_CAT(a, ...) UTIL_PRIMITIVE_CAT(a, __VA_ARGS__)
#define UTIL_PRIMITIVE_CAT(a, ...) a##__VA_ARGS__
/* 取第二参数；没有探测逗号时由 UTIL_CHECK 追加的 0 作为缺省结果。 */
#define UTIL_CHECK_N(x, n, ...) n
#define UTIL_CHECK(...) UTIL_CHECK_N(__VA_ARGS__, 0,)
/* 预处理逻辑非：仅记号 0 命中 UTIL_NOT_0 哨兵得到 1，其他未命中的记号得到 0。 */
#define UTIL_NOT(x) UTIL_CHECK(UTIL_PRIMITIVE_CAT(UTIL_NOT_, x))
#define UTIL_NOT_0 ~, 1,
/* 对已归一化的 0/1 取反，其他输入不受支持。 */
#define UTIL_COMPL(b) UTIL_PRIMITIVE_CAT(UTIL_COMPL_, b)
#define UTIL_COMPL_0 1
#define UTIL_COMPL_1 0
/* 将 0 与其他可探测单记号分别归一为 0 和 1，不执行 C 表达式求值。 */
#define UTIL_BOOL(x) UTIL_COMPL(UTIL_NOT(x))

/* 透传并促使参数展开；REPEAT 转发给列表重复展开宏。 */
#define UTIL_EVAL(...) __VA_ARGS__
#define UTIL_EXPAND(...) __VA_ARGS__
#define UTIL_REPEAT(...) UTIL_LISTIFY(__VA_ARGS__)

/* 可变参数数量减一的底层选择器：按占位参数位置取 N，调用入口负责追加计数序列。 */
#define NUM_VA_ARGS_LESS_1_IMPL(				\
	_ignored,						\
	_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,		\
	_11, _12, _13, _14, _15, _16, _17, _18, _19, _20,	\
	_21, _22, _23, _24, _25, _26, _27, _28, _29, _30,	\
	_31, _32, _33, _34, _35, _36, _37, _38, _39, _40,	\
	_41, _42, _43, _44, _45, _46, _47, _48, _49, _50,	\
	_51, _52, _53, _54, _55, _56, _57, _58, _59, _60,	\
	_61, _62, N, ...) N

/* MACRO_MAP_CAT 将映射结果逐次拼接为一个记号。 */
#define MACRO_MAP_CAT_(...)						\
	/* 保持只有映射宏和一个数据参数的情况也能正确展开。 */	\
	MACRO_MAP_CAT_N(NUM_VA_ARGS_LESS_1(__VA_ARGS__), __VA_ARGS__)
/* 按数量选择 MACRO_MC_0 至 MACRO_MC_15；递归层逐项映射并拼接，0 项不输出内容。 */
#define MACRO_MAP_CAT_N_(N, ...) UTIL_CAT(MACRO_MC_, N)(__VA_ARGS__,)
#define MACRO_MC_0(...)
#define MACRO_MC_1(m, a, ...)  m(a)
#define MACRO_MC_2(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_1(m, __VA_ARGS__,))
#define MACRO_MC_3(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_2(m, __VA_ARGS__,))
#define MACRO_MC_4(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_3(m, __VA_ARGS__,))
#define MACRO_MC_5(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_4(m, __VA_ARGS__,))
#define MACRO_MC_6(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_5(m, __VA_ARGS__,))
#define MACRO_MC_7(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_6(m, __VA_ARGS__,))
#define MACRO_MC_8(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_7(m, __VA_ARGS__,))
#define MACRO_MC_9(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_8(m, __VA_ARGS__,))
#define MACRO_MC_10(m, a, ...) UTIL_CAT(m(a), MACRO_MC_9(m, __VA_ARGS__,))
#define MACRO_MC_11(m, a, ...) UTIL_CAT(m(a), MACRO_MC_10(m, __VA_ARGS__,))
#define MACRO_MC_12(m, a, ...) UTIL_CAT(m(a), MACRO_MC_11(m, __VA_ARGS__,))
#define MACRO_MC_13(m, a, ...) UTIL_CAT(m(a), MACRO_MC_12(m, __VA_ARGS__,))
#define MACRO_MC_14(m, a, ...) UTIL_CAT(m(a), MACRO_MC_13(m, __VA_ARGS__,))
#define MACRO_MC_15(m, a, ...) UTIL_CAT(m(a), MACRO_MC_14(m, __VA_ARGS__,))

#endif    /* ZEPHYR_INCLUDE_SYS_UTIL_INTERNAL_H_ 包含保护结束。 */
