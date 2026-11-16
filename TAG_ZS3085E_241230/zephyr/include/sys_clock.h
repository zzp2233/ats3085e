/*
 * Copyright (c) 2014-2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 系统时钟共享的tick类型、超时编码及单位常量。
 *
 *
 * 由硬件定时器驱动和内核共同使用；tick是内核时间粒度，cycle是硬件计数粒度。
 */

#ifndef ZEPHYR_INCLUDE_SYS_CLOCK_H_
#define ZEPHYR_INCLUDE_SYS_CLOCK_H_    /* 头文件保护，避免类型和宏重复定义。 */

#include <sys/util.h>
#include <sys/dlist.h>

#include <toolchain.h>
#include <zephyr/types.h>

#include <sys/time_units.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup clock_apis
 * @{
 */

/**
 * @brief 超时接口采用的tick位宽。
 *
 * CONFIG_TIMEOUT_64BIT选择64位有符号编码，否则用32位无符号编码，决定可表示的超时范围。
 * 这不改变系统uptime累计量，后者始终为64位tick。
 */
#ifdef CONFIG_TIMEOUT_64BIT
typedef int64_t k_ticks_t;     /* 64位模式支持更大范围与负数绝对时间编码。 */
#else
typedef uint32_t k_ticks_t;    /* 32位模式的tick预算类型，-1转换后保留作无限等待。 */
#endif

#define K_TICKS_FOREVER ((k_ticks_t) -1)    /* 无限等待的内部tick哨兵，不是普通负延时。 */

/**
 * @brief 内核不透明超时对象。
 *
 * 应用用K_MSEC、K_TIMEOUT_ABS_TICKS等宏构造，或使用K_NO_WAIT、K_FOREVER常量；
 * 不直接读写内部ticks编码。比较两个超时对象是否相同应使用K_TIMEOUT_EQ。
 */
typedef struct {
	k_ticks_t ticks;    /* 内部编码字段，不要求应用直接解释。 */
} k_timeout_t;

/**
 * @brief 比较两个超时对象是否具有相同编码。
 *
 * a、b为k_timeout_t值，可与K_NO_WAIT/K_FOREVER比较；不换算成实际截止时间，
 * 因而相对时间与绝对时间即使恰巧同刻到期，也不一定相等。
 *
 * @return 编码相同为true，否则false。
 */
#define K_TIMEOUT_EQ(a, b) ((a).ticks == (b).ticks)

#define Z_TIMEOUT_NO_WAIT ((k_timeout_t) {})                   /* 构造全零编码，代表不等待。 */
#if defined(__cplusplus) && ((__cplusplus - 0) < 202002L)
#define Z_TIMEOUT_TICKS(t) ((k_timeout_t) { (t) })             /* 较早C++标准采用顺序初始化，避免指定成员初始化。 */
#else
#define Z_TIMEOUT_TICKS(t) ((k_timeout_t) { .ticks = (t) })    /* C及支持的C++使用指定成员初始化。 */
#endif
#define Z_FOREVER Z_TIMEOUT_TICKS(K_TICKS_FOREVER)             /* 把无限等待哨兵包装为超时对象。 */

#ifdef CONFIG_TIMEOUT_64BIT
/* 64位超时分支：负时长夹到0，再把毫秒/微秒/纳秒/cycle向上取整为tick，避免正等待被截短。 */
# define Z_TIMEOUT_MS(t) Z_TIMEOUT_TICKS((k_ticks_t)k_ms_to_ticks_ceil64(MAX(t, 0)))
# define Z_TIMEOUT_US(t) Z_TIMEOUT_TICKS((k_ticks_t)k_us_to_ticks_ceil64(MAX(t, 0)))
# define Z_TIMEOUT_NS(t) Z_TIMEOUT_TICKS((k_ticks_t)k_ns_to_ticks_ceil64(MAX(t, 0)))
# define Z_TIMEOUT_CYC(t) Z_TIMEOUT_TICKS((k_ticks_t)k_cyc_to_ticks_ceil64(MAX(t, 0)))
#else
/* 32位超时分支：换算规则相同，结果范围受32位tick编码约束。 */
# define Z_TIMEOUT_MS(t) Z_TIMEOUT_TICKS((k_ticks_t)k_ms_to_ticks_ceil32(MAX(t, 0)))
# define Z_TIMEOUT_US(t) Z_TIMEOUT_TICKS((k_ticks_t)k_us_to_ticks_ceil32(MAX(t, 0)))
# define Z_TIMEOUT_NS(t) Z_TIMEOUT_TICKS((k_ticks_t)k_ns_to_ticks_ceil32(MAX(t, 0)))
# define Z_TIMEOUT_CYC(t) Z_TIMEOUT_TICKS((k_ticks_t)k_cyc_to_ticks_ceil32(MAX(t, 0)))
#endif

/* 64位超时把绝对截止tick编码到K_TICKS_FOREVER以下的负值区间；
 * Z_TICK_ABS(t)>=0时可解码出绝对截止值。此变换可逆，执行两次还原输入，
 * K_TICKS_FOREVER自身保持不变。32位模式不据此启用绝对时间支持。 */
#define Z_TICK_ABS(t) (K_TICKS_FOREVER - 1 - (t))

/** @} */

#ifdef CONFIG_TICKLESS_KERNEL

/**
  * @brief  声明tickless系统重新启用时钟的入口。
  * @param  无。
  * @retval 无。
  * @note   仅CONFIG_TICKLESS_KERNEL声明，具体恢复动作取决于平台实现。
  */
extern void z_enable_sys_clock(void);
#endif

#if defined(CONFIG_SYS_CLOCK_EXISTS) && \
	(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC == 0)
#error "SYS_CLOCK_HW_CYCLES_PER_SEC must be non-zero!"
#endif

/* 每微秒包含的纳秒数。 */
#define NSEC_PER_USEC 1000U

/* 每毫秒包含的微秒数。 */
#define USEC_PER_MSEC 1000U

/* 每秒包含的毫秒数。 */
#define MSEC_PER_SEC 1000U

/* 每秒包含的微秒数。 */
#define USEC_PER_SEC ((USEC_PER_MSEC) * (MSEC_PER_SEC))

/* 每秒包含的纳秒数。 */
#define NSEC_PER_SEC ((NSEC_PER_USEC) * (USEC_PER_MSEC) * (MSEC_PER_SEC))


/* 内核时钟换算配置。 */

/* 单位换算通常使用64位中间量；若周期频率、tick频率和毫秒频率可整除，
 * 可使用更便宜的32位计算，否则标记需要精确换算。 */

#ifdef CONFIG_SYS_CLOCK_EXISTS

#if defined(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME) || \
	(MSEC_PER_SEC % CONFIG_SYS_CLOCK_TICKS_PER_SEC) || \
	(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC % CONFIG_SYS_CLOCK_TICKS_PER_SEC)
#define _NEED_PRECISE_TICK_MS_CONVERSION    /* 标记运行期频率或非整除频率需要精确时间换算。 */
#endif

#endif

/* 相对等待增加一个tick以覆盖当前尚未完整经过的tick。 */
#define _TICK_ALIGN 1

/* X为硬件cycle总数、NCYCLES为样本/轮次数且须非零；先向下换算纳秒再取整数平均。
 * 计数频率来自系统时钟配置，不能无条件当成CPU主频。 */
#define SYS_CLOCK_HW_CYCLES_TO_NS_AVG(X, NCYCLES) \
	(uint32_t)(k_cyc_to_ns_floor64(X) / NCYCLES)

/**
  * @brief  读取当前系统tick累计值的低32位。
  * @param  无。
  * @retval 32位tick快照；tickless包含驱动未报告的完整tick，无系统时钟时宏返回0。
  * @note   会回绕，不能作为长期绝对时间；当前1000 tick/s约49.7天回绕。
  */
uint32_t sys_clock_tick_get_32(void);

/**
  * @brief  读取当前系统64位tick累计值。
  * @param  无。
  * @retval 自启动累计的tick快照，包含驱动尚未报告的完整tick；无系统时钟时宏返回0。
  * @note   实现以自旋锁保护64位读取及驱动快照；单位不是毫秒，换算须依据tick频率。
  */
int64_t sys_clock_tick_get(void);

#ifndef CONFIG_SYS_CLOCK_EXISTS
#define sys_clock_tick_get() (0)       /* 未提供系统时钟时64位查询宏退化为0。 */
#define sys_clock_tick_get_32() (0)    /* 未提供系统时钟时低32位查询宏退化为0。 */
#endif

/**
  * @brief  把一次超时预算换算为uptime绝对截止tick。
  * @param  timeout：[输入] K_FOREVER、K_NO_WAIT、相对或支持的绝对超时编码。
  * @retval K_FOREVER返回UINT64_MAX；K_NO_WAIT返回当前tick；绝对编码解码返回截止值；其他返回当前tick加至少1。
  * @note   以未锁住的当前时刻为基准，应在收到新预算时调用一次，不应反复用它延长同一次等待。
  */
uint64_t sys_clock_timeout_end_calc(k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_SYS_CLOCK_H_ */
