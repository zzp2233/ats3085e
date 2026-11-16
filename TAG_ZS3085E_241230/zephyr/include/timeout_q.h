/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_TIMEOUT_Q_H_
#define ZEPHYR_KERNEL_INCLUDE_TIMEOUT_Q_H_    /* 防止超时内联包装重复定义。 */

/**
 * @file
 * @brief 线程在内核对象上等待时使用的超时队列接口。
 */

#include <kernel.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_SYS_CLOCK_EXISTS

/**
  * @brief  将超时对象节点初始化为未激活状态。
  * @param  to：[输出] 非空可写超时对象，不能在已入队时直接重初始化。
  * @retval 无。
  * @note   这里只清节点链接，不写回调/预算；对象存储由上层持有。
  */
static inline void z_init_timeout(struct _timeout *to)
{
	sys_dnode_init(&to->node);    /* 清除超时节点的前后链接，建立未激活状态。 */
}

/**
  * @brief  将超时对象按到期时刻插入差分队列并必要时提前硬件事件。
  * @param  to：[输入输出] 非空、未链接的超时对象，存储须保持有效直到取消或回调完成。
  * @param  fn：[输入] 到期回调，不可为空；执行时收到to指针，队列锁已释放，通常处于时钟中断上下文。
  * @param  timeout：[输入] 相对tick预算、支持的绝对编码或K_FOREVER。
  * @retval 无；K_FOREVER不入队也不触发回调。
  * @note   相对预算额外加1及elapsed以对齐当前tick；到期回调可重入超时接口。仅复制回调指针，不分配对象。
  */
void z_add_timeout(struct _timeout *to, _timeout_func_t fn,
		   k_timeout_t timeout);

/**
  * @brief  尝试取消仍在队列中的超时对象。
  * @param  to：[输入输出] 非空有效超时对象，可处于未激活状态。
  * @retval 0：从主队列或睡眠暂存队列移除；-EINVAL：未找到已链接对象。
  * @note   不等待已经开始的回调结束，取消成功与对象生命周期管理须由上层同步。
  */
int z_abort_timeout(struct _timeout *to);

/**
  * @brief  检查超时对象是否尚未链接到队列。
  * @param  to：[输入] 已初始化的非空超时对象。
  * @retval 节点未链接返回true，否则false。
  * @note   只读瞬时链接状态，不判断回调是否已开始执行。
  */
static inline bool z_is_inactive_timeout(const struct _timeout *to)
{
	return !sys_dnode_is_linked(&to->node);    /* 节点后继为空说明当前未处于超时链表。 */
}

/**
  * @brief  初始化线程基础状态中的超时节点。
  * @param  thread_base：[输出] 非空线程基础对象，其超时尚未在队列中。
  * @retval 无。
  * @note   供线程建立阶段使用，不分配超时对象。
  */
static inline void z_init_thread_timeout(struct _thread_base *thread_base)
{
	z_init_timeout(&thread_base->timeout);    /* 初始化线程内嵌的_timeout对象。 */
}

/**
  * @brief  声明线程等待预算耗尽时的调度回调。
  * @param  timeout：[输入输出] 已到期的线程内嵌超时对象，不可为空。
  * @retval 无。
  * @note   由超时模块摘除节点并释放timeout_lock后调用；实现在调度器中，负责结束等待/恢复就绪。
  */
extern void z_thread_timeout(struct _timeout *timeout);

/**
  * @brief  为线程的内嵌超时对象安排等待预算。
  * @param  thread：[输入输出] 非空有效线程对象。
  * @param  ticks：[输入] k_timeout_t预算对象，单位/无限等待编码见sys_clock.h。
  * @retval 无。
  * @note   有系统时钟时注册z_thread_timeout；无系统时钟分支为空操作，不建立唤醒事件。
  */
static inline void z_add_thread_timeout(struct k_thread *thread, k_timeout_t ticks)
{
	z_add_timeout(&thread->base.timeout, z_thread_timeout, ticks);    /* 关联线程超时回调并按预算加入内核队列。 */
}

/**
  * @brief  取消线程尚未触发的超时事件。
  * @param  thread：[输入输出] 非空有效线程对象。
  * @retval 0：成功取消；-EINVAL：对象没有活动超时。
  * @note   包装z_abort_timeout，不保证已经开始的回调结束。
  */
static inline int z_abort_thread_timeout(struct k_thread *thread)
{
	return z_abort_timeout(&thread->base.timeout);    /* 将取消状态直接返回给调用者。 */
}

/**
  * @brief  查询内核希望下一次时钟事件发生的相对tick。
  * @param  无。
  * @retval 最近超时/时间片预算；无定时事件返回MAX_WAIT配置值。
  * @note   内部加锁；MAX_WAIT可能是K_TICKS_FOREVER或INT_MAX。
  */
int32_t z_get_next_timeout_expiry(void);

/**
  * @brief  根据更早预算和空闲提示请求更新硬件时钟事件。
  * @param  ticks：[输入] 新请求的相对tick预算。
  * @param  is_idle：[输入] 是否即将进入空闲，原样传驱动。
  * @retval 无。
  * @note   不改变软件超时链表；临近到期时避免反复重设，SMP另考虑切换/时间片条件。
  */
void z_set_timeout_expiry(int32_t ticks, bool is_idle);

/**
  * @brief  查询指定超时项的剩余tick。
  * @param  timeout：[输入] 非空有效超时对象。
  * @retval 未激活返回0，否则返回锁内计算的剩余tick快照。
  * @note   查询不改变队列；离开锁后时间与状态可立即变化。
  */
k_ticks_t z_timeout_remaining(const struct _timeout *timeout);

#else

/* 无系统时钟配置的占位：不实际建立有限时间唤醒事件；不要据返回0误认硬件超时可用。 */
#define z_init_thread_timeout(thread_base) do {} while (false)      /* 无时钟时不建立线程超时节点。 */
#define z_abort_thread_timeout(to) (0)                              /* 无时钟占位固定报告0，并未取消实际定时事件。 */
#define z_is_inactive_timeout(to) 0                                 /* 保留原无时钟分支固定值0，不能解释为真实链表状态。 */
#define z_get_next_timeout_expiry() ((int32_t) K_TICKS_FOREVER)     /* 无时钟时无有限下一事件。 */
#define z_set_timeout_expiry(ticks, is_idle) do {} while (false)    /* 无时钟时忽略事件预算与空闲提示。 */

/**
  * @brief  为线程的内嵌超时对象安排等待预算。
  * @param  thread：[输入输出] 非空有效线程对象。
  * @param  ticks：[输入] k_timeout_t预算对象，单位/无限等待编码见sys_clock.h。
  * @retval 无。
  * @note   有系统时钟时注册z_thread_timeout；无系统时钟分支为空操作，不建立唤醒事件。
  */
static inline void z_add_thread_timeout(struct k_thread *thread, k_timeout_t ticks)
{
	ARG_UNUSED(thread);    /* 无时钟分支不访问线程超时对象。 */
	ARG_UNUSED(ticks);     /* 无时钟分支不处理有限超时预算。 */
}

#endif

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_KERNEL_INCLUDE_TIMEOUT_Q_H_ */
