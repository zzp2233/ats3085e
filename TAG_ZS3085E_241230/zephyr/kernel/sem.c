/*
 * Copyright (c) 2010-2016 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 内核计数信号量：有等待线程时give直接交付许可，否则计数加1直到limit。
 * init建立初始许可数，take消费一个许可；计数为0时可立即失败或挂起等待，避免应用自己忙轮询。
 * SPI上下文使用limit=1实现总线串行化；DMA完成另用初值0的信号量通知。
 */

#include <kernel.h>
#include <kernel_structs.h>

#include <toolchain.h>
#include <wait_q.h>
#include <sys/dlist.h>
#include <ksched.h>
#include <init.h>
#include <syscall_handler.h>
#include <tracing/tracing.h>
#include <sys/check.h>

/* 全系统共用一把自旋锁保护信号量计数和等待队列，节省每对象锁的RAM，代价是不同信号量之间也会争用。
 * 自旋锁只覆盖短临界区，真正等待许可由调度器挂起线程；不能持普通中断锁无限等待外设。
 * 原实现保留了改用计数原子操作优化争用的设想，此处并未实现该优化。 */
static struct k_spinlock lock;    /* 所有信号量共用的短临界区锁，静态零初始化。 */

/**
  * @brief  初始化计数信号量及等待队列。
  * @param  sem：[输出] 非空可写信号量对象，不能在仍被其他线程使用时重初始化。
  * @param  initial_count：[输入] 初始许可数0～limit。
  * @param  limit：[输入] 计数上限1～K_SEM_MAX_LIMIT。
  * @retval 0：初始化完成；-EINVAL：启用参数检查时检测到无效范围。
  * @note   仅初始化状态，不分配堆内存；初值0的SPI总线锁须在驱动初始化结束时首次give。
  */
int z_impl_k_sem_init(struct k_sem *sem, unsigned int initial_count,
		      unsigned int limit)
{
	/* 容量须为1～K_SEM_MAX_LIMIT，初始计数不可超过容量；CHECKIF行为还受检查配置影响。 */
	CHECKIF(limit == 0U || limit > K_SEM_MAX_LIMIT || initial_count > limit) {    /* 检查容量及初值，禁止无意义容量和初始溢出。 */
		SYS_PORT_TRACING_OBJ_FUNC(k_sem, init, sem, -EINVAL);                 /* 向可选跟踪后端报告初始化参数错误。 */

		return -EINVAL;                                                       /* 参数不符合计数范围，不继续修改对象。 */
	}

	sem->count = initial_count;                                                   /* 建立可立即消费的初始许可数。 */
	sem->limit = limit;                                                           /* 保存give时使用的饱和上限。 */

	SYS_PORT_TRACING_OBJ_FUNC(k_sem, init, sem, 0);                               /* 报告初始化成功的跟踪事件。 */

	z_waitq_init(&sem->wait_q);                                                   /* 建立空线程等待队列。 */
#if defined(CONFIG_POLL)
	sys_dlist_init(&sem->poll_events);                                            /* 启用poll时初始化挂接的事件链表。 */
#endif
	z_object_init(sem);                                                           /* 登记对象已初始化状态，支持用户态对象检查。 */

	return 0;                                                                     /* 成功完成本路径。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  验证用户信号量对象后执行初始化。
  * @param  sem：[输出] 用户有权限的信号量对象，不可为空。
  * @param  initial_count：[输入] 初始许可数0～limit。
  * @param  limit：[输入] 正的许可上限。
  * @retval 与z_impl_k_sem_init相同；对象验证失败触发系统调用异常。
  * @note   仅CONFIG_USERSPACE编译。
  */
int z_vrfy_k_sem_init(struct k_sem *sem, unsigned int initial_count,
		      unsigned int limit)
{
	Z_OOPS(Z_SYSCALL_OBJ_INIT(sem, K_OBJ_SEM));             /* 验证用户对象类型及初始化操作权限，失败进入系统调用异常。 */
	return z_impl_k_sem_init(sem, initial_count, limit);    /* 通过验证后执行真正的初始化。 */
}
#include <syscalls/k_sem_init_mrsh.c>
#endif

/**
  * @brief  通知挂在此信号量上的poll等待者。
  * @param  sem：[输入输出] 非空有效信号量，调用者持全局信号量锁。
  * @retval 无。
  * @note   CONFIG_POLL关闭时仅消除参数未使用告警；不会建立许可。
  */
static inline void handle_poll_events(struct k_sem *sem)
{
#ifdef CONFIG_POLL
	z_handle_obj_poll_events(&sem->poll_events, K_POLL_STATE_SEM_AVAILABLE);    /* 按信号量可用事件唤醒相应poll观察者。 */
#else
	ARG_UNUSED(sem);                                                            /* 未编译poll时不访问事件链表。 */
#endif
}

/**
  * @brief  交付一个信号量许可并在需要时重新调度。
  * @param  sem：[输入输出] 已初始化且仍有效的信号量，不可为空。
  * @retval 无。
  * @note   可由线程或中断调用；有等待线程时直接令其成功就绪，否则计数饱和增加。释放自旋锁由z_reschedule负责。
  */
void z_impl_k_sem_give(struct k_sem *sem)
{
	k_spinlock_key_t key = k_spin_lock(&lock);                     /* 锁住计数/等待队列并保存恢复中断所需状态。 */
	struct k_thread *thread;                                       /* 保存从信号量等待队列取出的线程。 */

	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_sem, give, sem);             /* 记录give开始事件。 */

	thread = z_unpend_first_thread(&sem->wait_q);                  /* 取优先等待线程并从等待队列移除。 */

	if (thread != NULL) {                                          /* 已有等待者时直接交付许可，不增加公共计数。 */
		arch_thread_return_value_set(thread, 0);               /* 让被唤醒线程的take返回成功0。 */
		z_ready_thread(thread);                                /* 将等待线程移入可运行状态，实际切换在调度阶段。 */
	} else {
		sem->count += (sem->count != sem->limit) ? 1U : 0U;    /* 无等待者时只增加到limit，不因多余give溢出。 */
		handle_poll_events(sem);                               /* 检查并通知关联poll等待者。 */
	}

	z_reschedule(&lock, key);                                      /* 释放全局锁并按优先级判断是否切换到就绪线程。 */

	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_sem, give, sem);              /* 记录give结束事件。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  验证用户对象后释放一个许可。
  * @param  sem：[输入输出] 可访问的已初始化信号量。
  * @retval 无；验证失败触发系统调用异常。
  * @note   仅CONFIG_USERSPACE编译。
  */
static inline void z_vrfy_k_sem_give(struct k_sem *sem)
{
	Z_OOPS(Z_SYSCALL_OBJ(sem, K_OBJ_SEM));    /* 验证用户可访问的已初始化信号量对象。 */
	z_impl_k_sem_give(sem);                   /* 执行内核许可交付。 */
}
#include <syscalls/k_sem_give_mrsh.c>
#endif

/**
  * @brief  取得一个许可，必要时按超时参数挂起当前线程。
  * @param  sem：[输入输出] 已初始化信号量，不可为空。
  * @param  timeout：[输入] K_NO_WAIT立即尝试，K_FOREVER无限等，其他为k_timeout_t编码的tick预算。
  * @retval 0：取得许可；-EBUSY：无许可且不等待；-EAGAIN：等待超时或被reset中断。
  * @note   中断上下文只能K_NO_WAIT；挂起由z_pend_curr原子释放锁并切换线程，不在自旋锁内忙等。
  */
int z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	int ret = 0;                                                       /* 默认成功状态，失败或挂起后由具体路径更新。 */

	/* 校验中断调用约束：ISR不能进入可阻塞的等待路径，只允许K_NO_WAIT。 */
	__ASSERT(((arch_is_in_isr() == false) ||
		  K_TIMEOUT_EQ(timeout, K_NO_WAIT)), "");

	k_spinlock_key_t key = k_spin_lock(&lock);                         /* 锁住计数/等待队列并保存恢复中断所需状态。 */

	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_sem, take, sem, timeout);        /* 记录take及其等待预算。 */

	/* 先处理可立即成功的计数路径，减少调度器与等待队列开销。 */
	if (likely(sem->count > 0U)) {                                     /* 常见快速路径：已有许可，无需进入等待队列。 */
		sem->count--;                                              /* 消费一个许可。 */
		k_spin_unlock(&lock, key);                                 /* 快速返回前恢复锁和中断状态。 */
		ret = 0;                                                   /* 本次已经得到许可，返回成功。 */
		goto out;                                                  /* 跳到公共跟踪与返回出口，避免重复释放锁。 */
	}

	/* 计数为零时区分立即失败与阻塞等待；两条路径都正确交接锁。 */
	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {                            /* 没有许可且请求不等待，立即报告资源忙。 */
		k_spin_unlock(&lock, key);                                 /* 快速返回前恢复锁和中断状态。 */
		ret = -EBUSY;                                              /* 无可用许可，调用者选择了立即返回。 */
		goto out;                                                  /* 跳到公共跟踪与返回出口，避免重复释放锁。 */
	}

	SYS_PORT_TRACING_OBJ_FUNC_BLOCKING(k_sem, take, sem, timeout);     /* 记录即将挂起等待许可。 */

	/* 进入等待队列的排队与释放锁必须原子交接，避免give恰好发生时丢失唤醒。 */
	ret = z_pend_curr(&lock, key, &sem->wait_q, timeout);              /* 原子排队、释放锁并阻塞；由give、超时或reset恢复。 */

out:
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_sem, take, sem, timeout, ret);    /* 跟踪本次take的最终返回状态。 */

	return ret;                                                        /* 传播获得许可或等待失败的结果。 */
}

/**
  * @brief  清零许可并使所有等待线程以-EAGAIN结束。
  * @param  sem：[输入输出] 已初始化且有效的信号量，不可为空。
  * @retval 无。
  * @note   在锁内清空等待队列并唤醒线程，之后处理poll及重新调度；不销毁对象。
  */
void z_impl_k_sem_reset(struct k_sem *sem)
{
	struct k_thread *thread;                                  /* 保存从信号量等待队列取出的线程。 */
	k_spinlock_key_t key = k_spin_lock(&lock);                /* 锁住计数/等待队列并保存恢复中断所需状态。 */

	while (true) {                                            /* 逐个摘取全部等待者，直到队列为空。 */
		thread = z_unpend_first_thread(&sem->wait_q);     /* 取优先等待线程并从等待队列移除。 */
		if (thread == NULL) {                             /* 没有更多等待线程时完成清理。 */
			break;                                    /* 结束等待队列清空循环。 */
		}
		arch_thread_return_value_set(thread, -EAGAIN);    /* 让因reset唤醒的take调用返回-EAGAIN。 */
		z_ready_thread(thread);                           /* 将等待线程移入可运行状态，实际切换在调度阶段。 */
	}
	sem->count = 0;                                           /* 丢弃此前未消费的许可。 */

	SYS_PORT_TRACING_OBJ_FUNC(k_sem, reset, sem);             /* 记录reset事件。 */

	handle_poll_events(sem);                                  /* 检查并通知关联poll等待者。 */

	z_reschedule(&lock, key);                                 /* 释放全局锁并按优先级判断是否切换到就绪线程。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  验证用户对象后取得信号量许可。
  * @param  sem：[输入输出] 当前用户可访问的信号量。
  * @param  timeout：[输入] 同实现层k_timeout_t等待约定。
  * @retval 与z_impl_k_sem_take相同；验证失败触发系统调用异常。
  * @note   仅CONFIG_USERSPACE编译。
  */
static inline int z_vrfy_k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	Z_OOPS(Z_SYSCALL_OBJ(sem, K_OBJ_SEM));                     /* 验证用户可访问的已初始化信号量对象。 */
	return z_impl_k_sem_take((struct k_sem *)sem, timeout);    /* 转入实际take逻辑并传播状态。 */
}
#include <syscalls/k_sem_take_mrsh.c>

/**
  * @brief  验证用户对象后复位信号量。
  * @param  sem：[输入输出] 当前用户可访问的已初始化信号量。
  * @retval 无；验证失败触发系统调用异常。
  * @note   仅CONFIG_USERSPACE编译。
  */
static inline void z_vrfy_k_sem_reset(struct k_sem *sem)
{
	Z_OOPS(Z_SYSCALL_OBJ(sem, K_OBJ_SEM));    /* 验证用户可访问的已初始化信号量对象。 */
	z_impl_k_sem_reset(sem);                  /* 执行计数清零和等待者唤醒。 */
}
#include <syscalls/k_sem_reset_mrsh.c>

/**
  * @brief  验证用户对象后读取当前许可计数。
  * @param  sem：[输入] 当前用户可访问的已初始化信号量。
  * @retval 当前许可数0～limit，是瞬时快照。
  * @note   仅CONFIG_USERSPACE编译；读取后其他线程可立即改变计数。
  */
static inline unsigned int z_vrfy_k_sem_count_get(struct k_sem *sem)
{
	Z_OOPS(Z_SYSCALL_OBJ(sem, K_OBJ_SEM));    /* 验证用户可访问的已初始化信号量对象。 */
	return z_impl_k_sem_count_get(sem);       /* 读取当前许可数，不阻塞也不消费许可。 */
}
#include <syscalls/k_sem_count_get_mrsh.c>

#endif
