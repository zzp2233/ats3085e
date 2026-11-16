/* 包含保护防止宏被重复定义，C++ 分支保留 C 链接形式。所有续行反斜杠和记号拼接规则保持原状；这些宏多数在预处理期展开，不是运行期函数。 */
/*
 * Copyright (c) 2011-2014, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 纯预处理工具的公共接口，供 C/C++ 和设备树实现使用。一般 C/C++ 包含 util.h，设备树代码使用本头而不直接依赖内部展开实现。 */

#ifndef ZEPHYR_INCLUDE_SYS_UTIL_MACROS_H_
#define ZEPHYR_INCLUDE_SYS_UTIL_MACROS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup sys-util
 * @{
 */

/* 条件展开、变参映射等实现细节放在 util_internal.h；本文件提供稳定的公共宏入口。 */
#include <sys/util_internal.h>

#ifndef BIT
#if defined(_ASMLANGUAGE)
/* 汇编无法识别 UL 类型后缀，本分支使用普通整数移位；仍需保证位索引合法。 */
#define BIT(n)  (1 << (n))
#else
/* 构造仅位 n 为 1 的 unsigned long 值；汇编分支使用无 C 类型后缀的整数。n 必须小于操作数位宽且非负。 */
#define BIT(n)  (1UL << (n))
#endif
#endif

/* 构造仅位 _n 为 1 的 64 位无符号数；_n 必须在 0 到 63。 */
#define BIT64(_n) (1ULL << (_n))

/* 按 set 的真假改写变量 var 的第 bit 位：0 清位，非零置位。var 必须是可写左值，bit 是合法位索引；宏不是原子操作且参数可能重复求值，不能替代锁保护。 */
#define WRITE_BIT(var, bit, set) \
	((var) = (set) ? ((var) | BIT(bit)) : ((var) & ~BIT(bit)))

/* 构造低 n 位全为 1 的 unsigned long 掩码，n 为 0 返回 0；本实现先计算 BIT(n)，n 不能等于或超过操作数位宽。 */
#define BIT_MASK(n) (BIT(n) - 1UL)

/* 构造低 n 位全为 1 的 64 位掩码，n 为 0 返回 0；由于先计算 BIT64(n)，n 必须小于 64。 */
#define BIT64_MASK(n) (BIT64(n) - 1ULL)

/* 把配置宏转换为 C 可见常量：config_macro 恰好展开成字面量 1 时为 1，未定义或其他值时为 0。与 #if 不同，if(IS_ENABLED(...)) 两支仍须能被 C 编译器检查；思路沿用 Linux config_enabled。 */
#define IS_ENABLED(config_macro) Z_IS_ENABLED1(config_macro)
/* 第一层先展开已有宏，下一层必须看见字面量 1，而不是 (1) 或求值后为 1 的算式；不能只依赖普通递归展开。 */

/* 根据 _flag 是否恰好展开成字面量 1 选择 _if_1_code 或 _else_code。两段代码必须用括号包住以保护其中逗号，展开后去掉外层括号。例如 COND_CODE_1(CONFIG_FLAG, (uint32_t x;), (there_is_no_flag();)) 就地选择一段；预处理算式即使数学值为 1 也不能代替字面量。 */
#define COND_CODE_1(_flag, _if_1_code, _else_code) \
	Z_COND_CODE_1(_flag, _if_1_code, _else_code)

/* 与 COND_CODE_1 相同，但检查 _flag 是否恰好展开成字面量 0；成立展开 _if_0_code，否则展开 _else_code，两段均须括号包裹。 */
#define COND_CODE_0(_flag, _if_0_code, _else_code) \
	Z_COND_CODE_0(_flag, _if_0_code, _else_code)

/* _flag 定义且恰好展开成字面量 1 时插入括号内的 _code，其他情况展开为空；相当于 COND_CODE_1 的空否则分支。 */
#define IF_ENABLED(_flag, _code) \
	COND_CODE_1(_flag, _code, ())

/* 检查宏参数替换序列是否为空：为空得到 1，非空得到 0。参数应以可参与记号拼接的字母或数字开始，不能传 .arg、(arg)、字符串或花括号；主要用于已定义宏，需要时另用 #ifdef。原注释的 true/false 叙述前后相反，应以 IS_EMPTY 名称及底层展开为准。 */
#define IS_EMPTY(a) Z_IS_EMPTY_(a, 1, 0,)

/* 从变参列表删除空元素；例如 LIST_DROP_EMPTY(,a,b,,d,) 展开为 a,b,d。避免 FOR_EACH 把空项也传给映射宏；实现先过滤，再丢弃辅助首项。 */
#define LIST_DROP_EMPTY(...) \
	Z_LIST_DROP_FIRST(FOR_EACH(Z_LIST_NO_EMPTIES, (), __VA_ARGS__))

/* 显式的空展开宏，用于表达有意留空的记号位置，也可避免 checkpatch 对行首逗号等写法发出格式告警。 */
#define EMPTY

/* 原样返回参数 V 的记号序列；常用于 FOR_EACH 无需变换元素时。 */
#define IDENTITY(V) V

/* 从变参列表取得第 N 项，N 从 1 开始且须有对应内部选择宏，剩余参数为待选择列表。 */
#define GET_ARG_N(N, ...) Z_GET_ARG_##N(__VA_ARGS__)

/* 从变参列表丢弃前 N 项，返回剩余记号列表；N 须受内部宏支持。 */
#define GET_ARGS_LESS_N(N, ...) Z_GET_ARGS_LESS_##N(__VA_ARGS__)

/* 在预处理期执行类似逻辑或的短路选择：a 应为可转换为布尔的受支持整数记号，推荐字面量 0/1；为真返回 a，否则展开 b。不是运行期 ||，未选中的 b 不必形成有效 C 表达式。 */
#define UTIL_OR(a, b) COND_CODE_1(UTIL_BOOL(a), (a), (b))

/* 在预处理期执行类似逻辑与的短路选择：a 推荐使用字面量 0/1；为真展开 b，否则产生 0。不是运行期 &&，可保护不应展开的 b。 */
#define UTIL_AND(a, b) COND_CODE_1(UTIL_BOOL(a), (b), (0))

/* 产生 LEN 次宏调用形成代码序列。LEN 必须是小于 255 的受支持非负整数字面量；F 至少接收索引 i 和后续变参。例 F(i, _) 展开 MY_PWM##i, 时可生成 MY_PWM0, MY_PWM1, ...。未定义或不支持的参数行为未保证。 */
#define UTIL_LISTIFY(LEN, F, ...) UTIL_CAT(Z_UTIL_LISTIFY_, LEN)(F, __VA_ARGS__)

/* 依次对每个变参调用 F(element)，调用结果间插入 sep。sep 必须用括号包裹以允许逗号，例如 FOR_EACH(F, (;), 4,5,6) 得到 F(4);F(5);F(6)。末项后没有分隔符，内部先反转参数配合展开引擎还原原顺序。 */
#define FOR_EACH(F, sep, ...) \
	Z_FOR_EACH(F, sep, REVERSE_ARGS(__VA_ARGS__))

/* 先删除空变参，再对每个非空元素调用 F，且每次结果后都附加 term。term 必须用括号包裹；与普通 FOR_EACH 的中间分隔符不同，最后也有终止符，空列表则完全不产生内容，便于组合多个数组初始化片段。 */
#define FOR_EACH_NONEMPTY_TERM(F, term, ...)				\
	COND_CODE_0(							\
		/* 先判断过滤后是否已没有非空参数。 */		\
		NUM_VA_ARGS_LESS_1(LIST_DROP_EMPTY(__VA_ARGS__, _)),	\
		/* 没有非空参数则展开为空。 */				\
		(),							\
		/* 仍有参数时展开下面的映射和终止符。 */				\
		(/* 对非空参数逐项展开，项间使用指定的终止符。 */		\
			FOR_EACH(F, term, LIST_DROP_EMPTY(__VA_ARGS__))	\
			/* 最后再补一个去掉外括号的终止符。 */			\
			__DEBRACKET term				\
		))

/* 依次调用 F(index, element)，索引从 0 开始；sep 是括号包裹的分隔符，剩余变参为元素列表。例如输入 4,5,6，可生成以 0,1,2 命名并分别赋值的声明。 */
#define FOR_EACH_IDX(F, sep, ...) \
	Z_FOR_EACH_IDX(F, sep, REVERSE_ARGS(__VA_ARGS__))

/* 依次调用 F(element, fixed_arg)；F 为映射宏，sep 为括号包裹的分隔符，fixed_arg 作为每次调用的第二参数，后续变参为元素列表。比如可生成对同一设备指针的多个函数调用。 */
#define FOR_EACH_FIXED_ARG(F, sep, fixed_arg, ...) \
	Z_FOR_EACH_FIXED_ARG(F, sep, fixed_arg, REVERSE_ARGS(__VA_ARGS__))

/* 依次调用 F(index, element, fixed_arg)，把从 0 起的索引、当前元素和公共第三参数一并传给 F；sep 必须括号包裹。 */
#define FOR_EACH_IDX_FIXED_ARG(F, sep, fixed_arg, ...) \
	Z_FOR_EACH_IDX_FIXED_ARG(F, sep, fixed_arg, REVERSE_ARGS(__VA_ARGS__))

/* 逆序输出全部变参；内部利用遍历引擎与逗号分隔，不计算运行期值。 */
#define REVERSE_ARGS(...) \
	Z_FOR_EACH_ENGINE(Z_FOR_EACH_EXEC, (,), Z_BYPASS, _, __VA_ARGS__)

/* 计算变参个数减一；本参数展开表支持最多 64 项，空实参的预处理计数行为不能当成普通容器长度。 */
#define NUM_VA_ARGS_LESS_1(...) \
	NUM_VA_ARGS_LESS_1_IMPL(__VA_ARGS__, 63, 62, 61, \
	60, 59, 58, 57, 56, 55, 54, 53, 52, 51,		 \
	50, 49, 48, 47, 46, 45, 44, 43, 42, 41,		 \
	40, 39, 38, 37, 36, 35, 34, 33, 32, 31,		 \
	30, 29, 28, 27, 26, 25, 24, 23, 22, 21,		 \
	20, 19, 18, 17, 16, 15, 14, 13, 12, 11,		 \
	10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, ~)

/* 首个变参为接收单参数的映射宏，其余为元素；逐项展开后把结果拼接成一个记号。例如 FOO(x)=item_##x##_ 时，映射 a,b,c 产生 item_a_item_b_item_c_。 */
#define MACRO_MAP_CAT(...) MACRO_MAP_CAT_(__VA_ARGS__)

/* 与 MACRO_MAP_CAT 类似，但仅处理前 N 项，多余项忽略；N 须有对应内部实现。第一个后续参数是映射宏，其他为待映射项，输出是拼接后的单个记号。 */
#define MACRO_MAP_CAT_N(N, ...) MACRO_MAP_CAT_N_(N, __VA_ARGS__)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_SYS_UTIL_MACROS_H_ */
