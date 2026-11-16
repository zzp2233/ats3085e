/* 内核对象的多线程等待队列；信号量take无许可时由调度器把线程加入此队列。
 * CONFIG_WAITQ_SCALABLE采用红黑树，否则采用双向链表；本头初始化/查询不自行加锁。 */

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_
#define ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_    /* 保护等待队列宏与内联接口。 */

#include <kernel_structs.h>
#include <sys/dlist.h>
#include <sys/rb.h>
#include <kernel/sched_priq.h>
#include <timeout_q.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_WAITQ_SCALABLE

/* 遍历等待线程，wq为有效队列，thread_ptr为线程指针变量；不允许无同步并发修改。 */
#define _WAIT_Q_FOR_EACH(wq, thread_ptr) \
	RB_FOR_EACH_CONTAINER(&(wq)->waitq.tree, thread_ptr, base.qnode_rb)

/**
  * @brief  按所选等待队列后端建立空队列。
  * @param  w：[输出] 非空可写等待队列；不能重置仍含等待线程的对象。
  * @retval 无。
  * @note   SCALABLE分支设置红黑树比较器，其他分支初始化自指空链表；线程排序/入队由调度器处理。
  */
static inline void z_waitq_init(_wait_q_t *w)
{
	/* 用零初始化建立空树，并指定线程优先级/排队顺序的比较函数；回调由红黑树操作触发。 */
	w->waitq = (struct _priq_rb) {
		.tree = {
			.lessthan_fn = z_priq_rb_lessthan    /* 红黑树排序回调，节点所属线程的优先关系由调度接口定义。 */
		}
	};
}

/**
  * @brief  读取队列中优先唤醒的线程，不移除它。
  * @param  w：[输入] 非空且已初始化的等待队列。
  * @retval 首个线程指针；空队列返回NULL。
  * @note   红黑树取最小排序节点，链表取队首；需由调用者保持与入队/出队相同的锁保护。
  */
static inline struct k_thread *z_waitq_head(_wait_q_t *w)
{
	return (struct k_thread *)rb_get_min(&w->waitq.tree);    /* 红黑树返回排序最小节点，按线程布局转换为线程对象。 */
}

#else    /* !CONFIG_WAITQ_SCALABLE: */

/* 遍历等待线程，wq为有效队列，thread_ptr为线程指针变量；不允许无同步并发修改。 */
#define _WAIT_Q_FOR_EACH(wq, thread_ptr) \
	SYS_DLIST_FOR_EACH_CONTAINER(&((wq)->waitq), thread_ptr, \
				     base.qnode_dlist)

/**
  * @brief  按所选等待队列后端建立空队列。
  * @param  w：[输出] 非空可写等待队列；不能重置仍含等待线程的对象。
  * @retval 无。
  * @note   SCALABLE分支设置红黑树比较器，其他分支初始化自指空链表；线程排序/入队由调度器处理。
  */
static inline void z_waitq_init(_wait_q_t *w)
{
	sys_dlist_init(&w->waitq);    /* 建立空双向链表，头尾指回队列哨兵。 */
}

/**
  * @brief  读取队列中优先唤醒的线程，不移除它。
  * @param  w：[输入] 非空且已初始化的等待队列。
  * @retval 首个线程指针；空队列返回NULL。
  * @note   红黑树取最小排序节点，链表取队首；需由调用者保持与入队/出队相同的锁保护。
  */
static inline struct k_thread *z_waitq_head(_wait_q_t *w)
{
	return (struct k_thread *)sys_dlist_peek_head(&w->waitq);    /* 链表返回队首或NULL，依赖线程内嵌节点布局。 */
}

#endif    /* !CONFIG_WAITQ_SCALABLE */

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_ */
