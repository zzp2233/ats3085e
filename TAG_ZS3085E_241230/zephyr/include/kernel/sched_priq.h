/* 调度优先级队列接口及存储布局；包含保护防止重复声明，具体操作实现在 kernel/sched.c，sys/dlist 和 sys/rb 提供两种节点容器。 */
/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_SCHED_PRIQ_H_
#define ZEPHYR_INCLUDE_SCHED_PRIQ_H_

#include <sys/util.h>
#include <sys/dlist.h>
#include <sys/rb.h>

/* 线程优先级队列提供三个后端，调度器和等待队列在构建时分别选择：
 * DUMB 用有序双向链表，插入 O(N)，适合线程较少并关注代码体积的设备。
 * 红黑树后端插入/删除 O(log N)，较多线程时扩展性好，但结构和常数开销较大。
 * MULTIQ 为有限个固定优先级分别维护队列，以位图查找最优优先级。
 * 当前 SCHED_DUMB 和 WAITQ_DUMB 均启用，实际走链表后端；其他后端仅保留接口。
 */

struct k_thread;    /* 仅前向声明线程控制块；完整定义由内核线程头提供。 */

/**
  * @brief  从有序双向链表读取最高优先级线程。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @retval 队首宿主线程地址；空表返回NULL。
  * @note   不移除节点，调用者保证队列同步。
  */
struct k_thread *z_priq_dumb_best(sys_dlist_t *pq);

/**
  * @brief  从双向链表优先级队列移除线程节点。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁且线程已在队列；pq参数不被本实现访问，idle不能移除。
  */
void z_priq_dumb_remove(sys_dlist_t *pq, struct k_thread *thread);

/**
  * @brief  将线程按有效优先级插入双向链表队列。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   同优先级排在已有项之后，形成先进先出顺序；调用者持调度锁，idle不入队。
  */
void z_priq_dumb_add(sys_dlist_t *pq, struct k_thread *thread);

struct _priq_rb {
	struct rbtree tree;    /* 红黑树实体，节点嵌在各线程控制块中。 */
	int next_order_key;    /* 同优先级线程的插入顺序键，用来保持先来先服务。 */
};

/**
  * @brief  赋予入队顺序键后将线程加入优先级红黑树。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁，顺序键回绕会遍历整树重新编号，可能增加延迟。
  */
void z_priq_rb_add(struct _priq_rb *pq, struct k_thread *thread);

/**
  * @brief  从优先级红黑树摘除线程并在树空时复位顺序键。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁且线程在树内，idle不作为普通节点。
  */
void z_priq_rb_remove(struct _priq_rb *pq, struct k_thread *thread);

/**
  * @brief  读取优先级红黑树的最小排序节点对应线程。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @retval 最优线程地址；空树返回NULL。
  * @note   只查询不移除，调用者持队列保护锁。
  */
struct k_thread *z_priq_rb_best(struct _priq_rb *pq);

/* 多队列后端使用最多 32 条优先级链表，以 RAM 换取快速查找。
 * 不支持截止期等需要更大排序空间的策略；此结构不代表当前 DUMB 构建的运行队列。
 */
struct _priq_mq {
	sys_dlist_t queues[32];    /* 每个索引对应一个优先级槽，槽内按插入顺序排列。 */
	unsigned int bitmask;      /* 位 i 为 1 表示 queues[i] 非空，用于定位最优可运行优先级。 */
};

/**
  * @brief  把线程加入对应优先级子队列并置位非空位图。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁，优先级索引必须合法；同级通过尾插保持FIFO。
  */
void z_priq_mq_add(struct _priq_mq *pq, struct k_thread *thread);

/**
  * @brief  从优先级子队列移除线程，空队列清对应位图。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁，线程须已在对应子队列。
  */
void z_priq_mq_remove(struct _priq_mq *pq, struct k_thread *thread);

/**
  * @brief  由最低置位优先级位图取得最优子队列队首。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @retval 最高优先级线程地址；所有队列为空则NULL。
  * @note   先排除零位图再用ctz，避免零参数未定义行为。
  */
struct k_thread *z_priq_mq_best(struct _priq_mq *pq);

#endif    /* ZEPHYR_INCLUDE_SCHED_PRIQ_H_ */
