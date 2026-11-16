/* 内核调度内部接口：线程等待队列、运行队列与超时队列通过本头的声明和状态内联函数协作。SPI信号量的take/give调用这些接口完成阻塞和唤醒。数值较小的优先级更高；状态位操作本身不加锁、不执行实际上下文切换，调用方须按sched.c的锁约定使用。 */
/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_KSCHED_H_
#define ZEPHYR_KERNEL_INCLUDE_KSCHED_H_    /* 防止调度接口和内联函数重复定义。 */

#include <kernel_structs.h>
#include <kernel_internal.h>
#include <timeout_q.h>
#include <tracing/tracing.h>
#include <stdbool.h>

/* 编译期检查应用优先级上下界顺序正确，避免构造空的可用区间。 */
BUILD_ASSERT(K_LOWEST_APPLICATION_THREAD_PRIO
	     >= K_HIGHEST_APPLICATION_THREAD_PRIO);

#ifdef CONFIG_MULTITHREADING
/* 多线程优先级校验：idle入口允许专用idle级，其他入口必须落在应用区间；宏参数会重复求值。 */
#define Z_VALID_PRIO(prio, entry_point)				     \
	(((prio) == K_IDLE_PRIO && z_is_idle_thread_entry(entry_point)) || \
	 ((K_LOWEST_APPLICATION_THREAD_PRIO			     \
	   >= K_HIGHEST_APPLICATION_THREAD_PRIO)		     \
	  && (prio) >= K_HIGHEST_APPLICATION_THREAD_PRIO	     \
	  && (prio) <= K_LOWEST_APPLICATION_THREAD_PRIO))

/* 按同一范围检查触发可选断言，日志字符串保持原文；宏是完整do-while语句。 */
#define Z_ASSERT_VALID_PRIO(prio, entry_point) do { \
	__ASSERT(Z_VALID_PRIO((prio), (entry_point)), \
		 "invalid priority (%d); allowed range: %d to %d", \
		 (prio), \
		 K_LOWEST_APPLICATION_THREAD_PRIO, \
		 K_HIGHEST_APPLICATION_THREAD_PRIO); \
	} while (false)
#else
#define Z_VALID_PRIO(prio, entry_point) ((prio) == -1)                       /* 无多线程配置只接受优先级-1。 */
#define Z_ASSERT_VALID_PRIO(prio, entry_point) __ASSERT((prio) == -1, "")    /* 无多线程配置按相同-1约束断言。 */
#endif

/**
  * @brief  按构建配置初始化运行队列及默认时间片参数。
  * @param  无。
  * @retval 无。
  * @note   系统启动阶段调用；链表、红黑树和多队列后端互为配置分支。
  */
void z_sched_init(void);

/**
  * @brief  加锁后将线程移动到同优先级末尾。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   更新队列与候选，但实际线程切换由后续重调度完成。
  */
void z_move_thread_to_end_of_prio_q(struct k_thread *thread);

/**
  * @brief  声明旧式线程时间片状态查询接口。
  * @param  thread：[输入输出] 原接口目标线程指针；实现约束未能由本树定义确认。
  * @retval 本源码树未找到定义，不能确认状态编码或副作用。
  * @note   本次SPI调用链不经这两个声明；保留声明不代表存在可链接实现，不继续据此扩展无关模块。
  */
int z_is_thread_time_slicing(struct k_thread *thread);

/**
  * @brief  加锁摘除线程的对象等待状态，保留超时事件。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   通常需由上层随后取消超时并决定是否就绪。
  */
void z_unpend_thread_no_timeout(struct k_thread *thread);

/**
  * @brief  摘下等待队列中最优线程而保留它的超时事件。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 被摘线程地址；空队列返回NULL。
  * @note   内部持调度锁，不直接就绪；调用方负责超时和状态后续处理。
  */
struct k_thread *z_unpend1_no_timeout(_wait_q_t *wait_q);

/**
  * @brief  原子交接调用方自旋锁，把当前线程排队等待并换出。
  * @param  lock：[输入输出] 调用者当前持有的有效自旋锁，由换出/重调度路径释放或交接。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @param  wait_q：[输入输出] 有效对象等待队列，z_pend_curr系列允许NULL不关联对象；z_sched_wait应传有效对象队列。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 唤醒路径设置的返回值；信号量正常give为0、超时通常为-EAGAIN。
  * @note   调用者已持lock；z_swap负责释放/交接锁和恢复中断，返回时不重新持有原lock。SPI上下文靠此避免持锁忙等。
  */
int z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key,
	       _wait_q_t *wait_q, k_timeout_t timeout);

/**
  * @brief  在调用方已屏蔽中断的前提下挂起当前线程并换出。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @param  wait_q：[输入输出] 有效对象等待队列，z_pend_curr系列允许NULL不关联对象；z_sched_wait应传有效对象队列。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 由唤醒路径设置的切换返回值，有限等待超时通常为-EAGAIN。
  * @note   接收原中断钥匙并交给z_swap_irqlock恢复，不能在ISR执行可阻塞调用。
  */
int z_pend_curr_irqlock(uint32_t key, _wait_q_t *wait_q, k_timeout_t timeout);

/**
  * @brief  为当前线程或占位线程建立对象等待和超时。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 无。
  * @note   断言限制目标为_current或dummy，不直接切换上下文；上层决定何时换出。
  */
void z_pend_thread(struct k_thread *thread, _wait_q_t *wait_q,
		   k_timeout_t timeout);

/**
  * @brief  交接自旋锁后按上下文与候选决定是否换线程。
  * @param  lock：[输入输出] 调用者当前持有的有效自旋锁，由换出/重调度路径释放或交接。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @retval 无。
  * @note   必定在本路径释放或交接传入lock；ISR或外层中断仍屏蔽时不在此同步切换。
  */
void z_reschedule(struct k_spinlock *lock, k_spinlock_key_t key);

/**
  * @brief  以中断钥匙为交接条件进行重调度或恢复中断。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @retval 无。
  * @note   无需传入自旋锁；允许调度时调用z_swap_irqlock，否则irq_unlock恢复原状态。
  */
void z_reschedule_irqlock(uint32_t key);

/**
  * @brief  摘下等待队首线程并取消其活动超时。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 被摘线程地址；空队列返回NULL。
  * @note   内部持调度锁，不直接设置就绪；信号量give随后写返回值并调用z_ready_thread。
  */
struct k_thread *z_unpend_first_thread(_wait_q_t *wait_q);

/**
  * @brief  摘除指定线程等待状态并尝试取消线程超时。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   线程必须在对象等待队列中；本函数不直接加入运行队列。
  */
void z_unpend_thread(struct k_thread *thread);

/**
  * @brief  取消队列中所有线程的等待和超时并使其就绪。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 1：至少处理一个等待线程；0：原队列为空。
  * @note   队列访问的外层对象同步由调用者提供；不直接设置每个线程的切换返回值。
  */
int z_unpend_all(_wait_q_t *wait_q);

/**
  * @brief  改变优先级并在需要且当前未锁调度时重选线程。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval 无。
  * @note   合法范围由上层验证；SMP可发送IPI通知其他CPU重新选择。
  */
void z_thread_priority_set(struct k_thread *thread, int prio);

/**
  * @brief  改变线程基础优先级并按需重新排列运行队列。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval true：线程当时满足就绪条件，之后可能需要调度；false：无需因该线程就绪重选。
  * @note   不实际上下文切换；调用者确保优先级合法，等待队列重新排序行为不由此保证。
  */
bool z_set_prio(struct k_thread *thread, int prio);

/**
  * @brief  保存被中断上下文句柄并取得下一线程可恢复的上下文。
  * @param  interrupted：[输入] 架构保存的被中断上下文句柄，空值约定由底层切换实现决定。
  * @retval 下一线程的switch_handle，供架构切换代码使用。
  * @note   仅CONFIG_USE_SWITCH编译；SMP锁内同步队列、栈一致性和句柄所有权，活动线程句柄保持NULL。
  */
void *z_get_next_switch_handle(void *interrupted);

/**
  * @brief  声明系统空闲线程入口。
  * @param  unused1：[输入] 未使用入口参数。
  * @param  unused2：[输入] 未使用入口参数。
  * @param  unused3：[输入] 未使用入口参数。
  * @retval 正常运行不返回。
  * @note   由内核建立，每CPU专用，不能作为普通应用线程随意改变其优先级。
  */
void idle(void *unused1, void *unused2, void *unused3);

/**
  * @brief  消费新报告的tick，时间片耗尽时把当前线程移到同级末尾。
  * @param  ticks：[输入] 本次tick数量；睡眠接口中为k_ticks_t预算，单位由系统tick频率决定。
  * @retval 无。
  * @note   通常从时钟上报路径调用，内部持sched_spinlock；处理非原子换出窗口避免重复挂起。
  */
void z_time_slice(int ticks);

/**
  * @brief  重建本CPU当前时间片的tick预算与硬件事件。
  * @param  无。
  * @retval 无。
  * @note   仅CONFIG_TIMESLICING编译；slice_time为0时不安排时间片事件，含驱动未报告elapsed。
  */
void z_reset_time_slice(void);

/**
  * @brief  声明旧式调度器线程终止入口。
  * @param  thread：[输入输出] 原接口目标线程指针；实现约束未能由本树定义确认。
  * @retval 本源码树未找到定义，不能确认状态编码或副作用。
  * @note   本次SPI调用链不经这两个声明；保留声明不代表存在可链接实现，不继续据此扩展无关模块。
  */
void z_sched_abort(struct k_thread *thread);

/**
  * @brief  处理跨CPU调度通知的可选跟踪入口。
  * @param  无。
  * @retval 无。
  * @note   仅SMP编译，当前主体只在启用跟踪时调用z_trace_sched_ipi。
  */
void z_sched_ipi(void);

/**
  * @brief  启动尚处于PRESTART状态的线程并评估调度。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   已经启动则不重复操作；持锁清PRESTART、就绪后由z_reschedule释放锁。
  */
void z_sched_start(struct k_thread *thread);

/**
  * @brief  在锁保护下尝试使线程就绪，避免重复调度其他CPU上的线程。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   内部持调度锁；只入就绪队列和更新候选，不保证立即执行该线程。
  */
void z_ready_thread(struct k_thread *thread);

/**
  * @brief  在SMP上下文切换阶段将原当前线程重新放回运行队列。
  * @param  curr：[输入输出] 正在被换出的有效线程对象。
  * @retval 无。
  * @note   仅CONFIG_SMP编译，须在确定不会再直接返回原上下文时调用。
  */
void z_requeue_current(struct k_thread *curr);

/**
  * @brief  返回上下文切换路径已经选择或即将选择的下一线程。
  * @param  无。
  * @retval SMP由next_up选择；单核返回ready_q.cache。
  * @note   由切换路径在合适同步条件下调用。
  */
struct k_thread *z_swap_next_thread(void);

/**
  * @brief  协调本CPU或远端CPU停止目标并完成终止清理。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无；若终止当前线程且非ISR则正常不应返回该线程。
  * @note   SMP远端线程需IPI协作，中断上下文可能无超时忙等；不负责释放用户持有资源。
  */
void z_thread_abort(struct k_thread *thread);

/**
  * @brief  从未持调度自旋锁的路径进入当前线程等待。
  * @param  wait_q：[输入输出] 有效对象队列，NULL表示不关联对象队列。
  * @param  timeout：[输入] k_timeout_t预算，K_FOREVER无限等待。
  * @retval 无；本包装丢弃底层等待状态。
  * @note   先保存并屏蔽中断，再由z_pend_curr_irqlock排队/换出/恢复；不得从ISR阻塞。
  */
static inline void z_pend_curr_unlocked(_wait_q_t *wait_q, k_timeout_t timeout)
{
	(void) z_pend_curr_irqlock(arch_irq_lock(), wait_q, timeout);    /* 先锁普通中断，再排队并交接钥匙换出，返回时丢弃底层状态。 */
}

/**
  * @brief  在未持对象自旋锁的路径重新评估调度。
  * @param  无。
  * @retval 无。
  * @note   先屏蔽中断取得钥匙，交给z_reschedule_irqlock恢复；不会保持本函数新增的中断屏蔽。
  */
static inline void z_reschedule_unlocked(void)
{
	(void) z_reschedule_irqlock(arch_irq_lock());    /* 把新取得的中断钥匙交给重调度出口恢复。 */
}

/**
  * @brief  检查给定入口地址是否为内核idle入口。
  * @param  entry_point：[输入] 用于比较的函数地址或NULL，不解引用。
  * @retval true：等于idle地址；false：否则。
  * @note   只比较地址，不启动线程。
  */
static inline bool z_is_idle_thread_entry(void *entry_point)
{
	return entry_point == idle;    /* 仅比较入口地址，不解引用它。 */
}

/**
  * @brief  按构建配置识别线程对象是否为idle。
  * @param  thread：[输入] 有效线程对象；SMP会读取is_idle字段，不可为空。
  * @retval SMP读取is_idle，单核比较z_idle_threads[0]；无多线程时false。
  * @note   配置分支不同，不可用对象名或优先级猜测idle身份。
  */
static inline bool z_is_idle_thread_object(struct k_thread *thread)
{
#ifdef CONFIG_MULTITHREADING
#ifdef CONFIG_SMP
	return thread->base.is_idle;            /* SMP读取线程自身的idle身份标记。 */
#else
	return thread == &z_idle_threads[0];    /* 单核idle对象固定为数组第0项。 */
#endif
#else
	return false;                           /* 无多线程分支不存在可识别的idle对象。 */
#endif                                          /* CONFIG_MULTITHREADING */
}

/**
  * @brief  检查线程是否处于挂起状态。
  * @param  thread：[输入] 非空有效线程对象。
  * @retval true：含_THREAD_SUSPENDED；false：未置该位。
  * @note   只读该状态位，不判断其余就绪条件；并发一致性由调用者保证。
  */
static inline bool z_is_thread_suspended(struct k_thread *thread)
{
	return (thread->base.thread_state & _THREAD_SUSPENDED) != 0U;    /* 测试挂起标志，不检查对象等待和超时。 */
}

/**
  * @brief  检查线程是否处于等待对象条件状态。
  * @param  thread：[输入] 非空有效线程对象。
  * @retval true：含_THREAD_PENDING；false：未置该位。
  * @note   只读该状态位，不判断其余就绪条件；并发一致性由调用者保证。
  */
static inline bool z_is_thread_pending(struct k_thread *thread)
{
	return (thread->base.thread_state & _THREAD_PENDING) != 0U;    /* 测试对象等待标志，不修改队列。 */
}

/**
  * @brief  检查线程是否带有阻止运行的基本状态。
  * @param  thread：[输入] 非空有效线程对象。
  * @retval true：含等待、未启动、死亡、占位或挂起任一位；否则false。
  * @note   只检查指定状态掩码，不包含所有其他调度条件，是否有活动超时另行检查。
  */
static inline bool z_is_thread_prevented_from_running(struct k_thread *thread)
{
	uint8_t state = thread->base.thread_state;    /* 采样基本线程状态位供组合检查。 */

	/* 任一基本运行禁止位存在即可阻止运行；活动超时另由z_is_thread_ready检查。 */
	return (state & (_THREAD_PENDING | _THREAD_PRESTART | _THREAD_DEAD |
			 _THREAD_DUMMY | _THREAD_SUSPENDED)) != 0U;

}

/**
  * @brief  检查线程内嵌超时是否仍链接在队列中。
  * @param  thread：[输入] 非空有效线程，超时节点已初始化。
  * @retval true：超时未被标记inactive；false：没有活动超时。
  * @note   只检查节点状态，受无系统时钟占位宏语义影响。
  */
static inline bool z_is_thread_timeout_active(struct k_thread *thread)
{
	return !z_is_inactive_timeout(&thread->base.timeout);    /* 链表节点仍链接则视为活动超时。 */
}

/**
  * @brief  检查线程基本状态和活动超时均允许就绪。
  * @param  thread：[输入] 非空有效线程对象。
  * @retval true：无阻止运行状态且无活动超时；否则false。
  * @note   不等于已经在运行队列，也不保证立刻获得CPU，调用方仍需同步入队。
  */
static inline bool z_is_thread_ready(struct k_thread *thread)
{
	/* 就绪须同时没有基本运行禁止标志和活动超时，不能仅凭SUSPENDED已清判断。 */
	return !((z_is_thread_prevented_from_running(thread)) != 0U ||
		 z_is_thread_timeout_active(thread));
}

/**
  * @brief  检查线程是否已经解除未启动状态。
  * @param  thread：[输入] 非空有效线程对象。
  * @retval true：PRESTART已清；false：仍未启动。
  * @note   不保证当前未死亡、未等待或已经实际执行过指令。
  */
static inline bool z_has_thread_started(struct k_thread *thread)
{
	return (thread->base.thread_state & _THREAD_PRESTART) == 0U;    /* 未启动标志已清表示启动过程已经放行。 */
}

/**
  * @brief  检查线程状态是否包含给定掩码中的任一位。
  * @param  thread：[输入] 非空有效线程对象。
  * @param  state：[输入] _THREAD_*位掩码，可组合多个标志。
  * @retval true：按位与非0；false：所有指定标志均未置位。
  * @note   组合掩码判断是任一位，不是要求全部位同时存在。
  */
static inline bool z_is_thread_state_set(struct k_thread *thread, uint32_t state)
{
	return (thread->base.thread_state & state) != 0U;    /* 掩码内有任一状态位置1即返回true。 */
}

/**
  * @brief  检查线程是否带就绪队列资格标志。
  * @param  thread：[输入] 非空有效线程对象。
  * @retval true：带_THREAD_QUEUED；false：否则。
  * @note   SMP当前线程可带标志但暂不在实体队列中，不能把此结果直接等同于链表已链接。
  */
static inline bool z_is_thread_queued(struct k_thread *thread)
{
	return z_is_thread_state_set(thread, _THREAD_QUEUED);    /* 读取就绪排队资格，SMP下不一定代表当前实体已链接。 */
}

/**
  * @brief  设置挂起标志并输出可选跟踪。
  * @param  thread：[输入输出] 非空可写线程对象，调用者持调度状态保护锁。
  * @retval 无。
  * @note   只修改_THREAD_SUSPENDED及可选跟踪，不自动移动等待/运行队列，不注册超时或实际切换。
  */
static inline void z_mark_thread_as_suspended(struct k_thread *thread)
{
	thread->base.thread_state |= _THREAD_SUSPENDED;            /* 置挂起位，阻止后续就绪检查通过。 */

	SYS_PORT_TRACING_FUNC(k_thread, sched_suspend, thread);    /* 通知可选跟踪后端线程被挂起。 */
}

/**
  * @brief  清除挂起标志并输出可选跟踪。
  * @param  thread：[输入输出] 非空可写线程对象，调用者持调度状态保护锁。
  * @retval 无。
  * @note   只修改_THREAD_SUSPENDED及可选跟踪，不自动移动等待/运行队列，不注册超时或实际切换。
  */
static inline void z_mark_thread_as_not_suspended(struct k_thread *thread)
{
	thread->base.thread_state &= ~_THREAD_SUSPENDED;          /* 清挂起位，其他运行限制仍由调用方处理。 */

	SYS_PORT_TRACING_FUNC(k_thread, sched_resume, thread);    /* 通知可选跟踪后端挂起标志已解除。 */
}

/**
  * @brief  清除未启动标志，允许线程参与后续就绪判断。
  * @param  thread：[输入输出] 非空可写线程对象，调用者持调度状态保护锁。
  * @retval 无。
  * @note   只修改_THREAD_PRESTART及可选跟踪，不自动移动等待/运行队列，不注册超时或实际切换。
  */
static inline void z_mark_thread_as_started(struct k_thread *thread)
{
	thread->base.thread_state &= ~_THREAD_PRESTART;    /* 清除尚未启动限制。 */
}

/**
  * @brief  设置对象等待标志。
  * @param  thread：[输入输出] 非空可写线程对象，调用者持调度状态保护锁。
  * @retval 无。
  * @note   只修改_THREAD_PENDING及可选跟踪，不自动移动等待/运行队列，不注册超时或实际切换。
  */
static inline void z_mark_thread_as_pending(struct k_thread *thread)
{
	thread->base.thread_state |= _THREAD_PENDING;    /* 设置正在等待内核对象的标志。 */
}

/**
  * @brief  清除对象等待标志。
  * @param  thread：[输入输出] 非空可写线程对象，调用者持调度状态保护锁。
  * @retval 无。
  * @note   只修改_THREAD_PENDING及可选跟踪，不自动移动等待/运行队列，不注册超时或实际切换。
  */
static inline void z_mark_thread_as_not_pending(struct k_thread *thread)
{
	thread->base.thread_state &= ~_THREAD_PENDING;    /* 解除对象等待限制。 */
}

/**
  * @brief  置位指定线程状态掩码。
  * @param  thread：[输入输出] 非空可写线程对象。
  * @param  states：[输入] 要置位/清除的_THREAD_*位掩码。
  * @retval 无。
  * @note   本函数不加锁；调用者同步并同时维护相关队列/超时关系。
  */
static inline void z_set_thread_states(struct k_thread *thread, uint32_t states)
{
	thread->base.thread_state |= states;    /* 将掩码中所有位并入线程状态。 */
}

/**
  * @brief  清除指定线程状态掩码。
  * @param  thread：[输入输出] 非空可写线程对象。
  * @param  states：[输入] 要置位/清除的_THREAD_*位掩码。
  * @retval 无。
  * @note   本函数不加锁；调用者同步并同时维护相关队列/超时关系。
  */
static inline void z_reset_thread_states(struct k_thread *thread,
					uint32_t states)
{
	thread->base.thread_state &= ~states;    /* 清除掩码指定的状态位，保留其他标志。 */
}

/**
  * @brief  检查优先级数值是否受配置天花板允许。
  * @param  prio：[输入] 待比较的基础优先级数值，越小越优先。
  * @retval true：prio>=CONFIG_PRIORITY_CEILING；false：请求比天花板更高的优先级。
  * @note   这里只比较数值，不改变线程。
  */
static inline bool z_is_under_prio_ceiling(int prio)
{
	return prio >= CONFIG_PRIORITY_CEILING;    /* 数值不小于天花板时无需限制更高优先级。 */
}

/**
  * @brief  把请求优先级限制为不高于配置天花板。
  * @param  prio：[输入] 请求的基础优先级数值。
  * @retval 合法低优先值原样返回；比天花板更高则返回CONFIG_PRIORITY_CEILING。
  * @note   不检查应用上下界，不执行优先级更新。
  */
static inline int z_get_new_prio_with_ceiling(int prio)
{
	return z_is_under_prio_ceiling(prio) ? prio : CONFIG_PRIORITY_CEILING;    /* 比天花板更高的请求被夹到配置天花板。 */
}

/**
  * @brief  判断第一个优先级是否高于或等于第二个。
  * @param  prio1：[输入] 第一个优先级数值。
  * @param  prio2：[输入] 第二个优先级数值。
  * @retval true：prio1 <= prio2；false：否则。
  * @note   数值越小越优先，不比较线程截止排序键。
  */
static inline bool z_is_prio1_higher_than_or_equal_to_prio2(int prio1, int prio2)
{
	return prio1 <= prio2;    /* 数值小或相同意味着优先级高或相同。 */
}

/**
  * @brief  判断第一个优先级是否高于或等于第二个。
  * @param  prio1：[输入] 第一个优先级数值。
  * @param  prio2：[输入] 第二个优先级数值。
  * @retval true：prio1 <= prio2；false：否则。
  * @note   数值越小越优先，不比较线程截止排序键。
  */
static inline bool z_is_prio_higher_or_equal(int prio1, int prio2)
{
	return z_is_prio1_higher_than_or_equal_to_prio2(prio1, prio2);    /* 复用同一数值比较规则。 */
}

/**
  * @brief  判断第一个优先级是否低于或等于第二个。
  * @param  prio1：[输入] 第一个优先级数值。
  * @param  prio2：[输入] 第二个优先级数值。
  * @retval true：prio1 >= prio2；false：否则。
  * @note   数值越小越优先，不比较线程截止排序键。
  */
static inline bool z_is_prio1_lower_than_or_equal_to_prio2(int prio1, int prio2)
{
	return prio1 >= prio2;    /* 数值大或相同意味着优先级低或相同。 */
}

/**
  * @brief  判断第一个优先级是否严格高于第二个。
  * @param  prio1：[输入] 第一个优先级数值。
  * @param  prio2：[输入] 第二个优先级数值。
  * @retval true：prio1 < prio2；false：否则。
  * @note   数值越小越优先，不比较线程截止排序键。
  */
static inline bool z_is_prio1_higher_than_prio2(int prio1, int prio2)
{
	return prio1 < prio2;    /* 严格数值较小者优先级更高。 */
}

/**
  * @brief  判断请求优先级是否严格高于参照优先级。
  * @param  prio：[输入] 待比较数值。
  * @param  test_prio：[输入] 参照数值。
  * @retval true：prio<test_prio；false：否则。
  * @note   只比较基础优先级，数值越小越优先。
  */
static inline bool z_is_prio_higher(int prio, int test_prio)
{
	return z_is_prio1_higher_than_prio2(prio, test_prio);    /* 复用严格优先级比较规则。 */
}

/**
  * @brief  判断第一个优先级是否低于或等于第二个。
  * @param  prio1：[输入] 第一个优先级数值。
  * @param  prio2：[输入] 第二个优先级数值。
  * @retval true：prio1 >= prio2；false：否则。
  * @note   数值越小越优先，不比较线程截止排序键。
  */
static inline bool z_is_prio_lower_or_equal(int prio1, int prio2)
{
	return z_is_prio1_lower_than_or_equal_to_prio2(prio1, prio2);    /* 复用低于或等于的数值比较规则。 */
}

/**
  * @brief  比较基础优先级及可选截止时间，决定线程调度先后。
  * @param  thread_1：[输入] 第一个非空有效线程对象。
  * @param  thread_2：[输入] 第二个非空有效线程对象。
  * @retval 正：thread_1优先；负：thread_2优先；0：二者等价。
  * @note   仅依赖结果符号。截止时间比较要求所有差值处于32位模空间同一半区间。
  */
int32_t z_sched_prio_cmp(struct k_thread *thread_1, struct k_thread *thread_2);

/**
  * @brief  按入口与应用上下界检查优先级。
  * @param  prio：[输入] 请求优先级数值。
  * @param  entry_point：[输入] 线程入口地址或NULL，仅与idle比较。
  * @retval true：专用idle入口使用idle级，或位于应用优先级范围；否则false。
  * @note   不访问线程控制块，只检查数值及入口地址。
  */
static inline bool _is_valid_prio(int prio, void *entry_point)
{
	if (prio == K_IDLE_PRIO && z_is_idle_thread_entry(entry_point)) {    /* 仅真正idle入口允许专用idle优先级例外。 */
		return true;                                                 /* 通过idle入口例外或全部应用上下界检查。 */
	}

	/* 普通应用优先级不能低于应用最低级，也不能侵入idle专用级。 */
	if (!z_is_prio_higher_or_equal(prio,
				       K_LOWEST_APPLICATION_THREAD_PRIO)) {
		return false;                                                /* 请求超出相应应用优先级边界。 */
	}

	/* 普通应用优先级也不能高于配置允许的最高级。 */
	if (!z_is_prio_lower_or_equal(prio,
				      K_HIGHEST_APPLICATION_THREAD_PRIO)) {
		return false;                                                /* 请求超出相应应用优先级边界。 */
	}

	return true;                                                         /* 通过idle入口例外或全部应用上下界检查。 */
}

/**
  * @brief  从等待队列取出一个线程并取消超时，使其可运行。
  * @param  wq：[输入输出] 已初始化的非空等待队列，外层对象同步由调用者负责。
  * @retval 无。
  * @note   内部辅助负责调度锁，不主动实际切换，也不写入特殊唤醒返回值。
  */
static inline void _ready_one_thread(_wait_q_t *wq)
{
	struct k_thread *thread = z_unpend_first_thread(wq);    /* 摘下最高优先等待者并取消其活动超时。 */

	if (thread != NULL) {                                   /* 队列非空才有线程可恢复就绪。 */
		z_ready_thread(thread);                         /* 检查其他运行条件并加入就绪队列，不直接执行切换。 */
	}
}

/**
  * @brief  增加当前线程调度锁嵌套。
  * @param  无。
  * @retval 无。
  * @note   只能在线程上下文使用，调用者保证调度状态同步；不屏蔽中断，不阻止线程主动阻塞；计数用递减反向编码并有溢出断言。
  */
static inline void z_sched_lock(void)
{
	__ASSERT(!arch_is_in_isr(), "");                    /* 调度锁计数属于任务上下文，不能从ISR修改。 */
	__ASSERT(_current->base.sched_locked != 1U, "");    /* 递减前检查防止调度锁计数反向编码回绕成0。 */

	--_current->base.sched_locked;                      /* 用无符号递减增加一层调度锁嵌套。 */

	compiler_barrier();                                 /* 约束编译器不要跨越调度锁边界移动访问，不生成CPU硬件屏障。 */
}

/**
  * @brief  减少当前线程调度锁嵌套而不立即重新调度。
  * @param  无。
  * @retval 无。
  * @note   只能在线程上下文与此前锁定配对；调用方稍后在安全出口评估调度。compiler_barrier仅约束编译器，不是CPU内存屏障。
  */
static ALWAYS_INLINE void z_sched_unlock_no_reschedule(void)
{
	__ASSERT(!arch_is_in_isr(), "");                    /* 调度锁计数属于任务上下文，不能从ISR修改。 */
	__ASSERT(_current->base.sched_locked != 0U, "");    /* 只有当前已锁定才允许减少嵌套层数。 */

	compiler_barrier();                                 /* 约束编译器不要跨越调度锁边界移动访问，不生成CPU硬件屏障。 */

	++_current->base.sched_locked;                      /* 递增退回一层锁嵌套，0为完全解锁。 */
}

/**
  * @brief  检查线程超时差分字段是否等于过期哨兵。
  * @param  thread：[输入] 非空有效线程对象。
  * @retval 有系统时钟时返回dticks==_EXPIRED，无时钟时false。
  * @note   仅检查保留的字段编码，不保证线程已经运行或回调已经结束。
  */
static ALWAYS_INLINE bool z_is_thread_timeout_expired(struct k_thread *thread)
{
#ifdef CONFIG_SYS_CLOCK_EXISTS
	return thread->base.timeout.dticks == _EXPIRED;    /* 检查保留的过期哨兵编码，不移除队列节点。 */
#else
	return 0;                                          /* 无系统时钟分支固定报告未过期。 */
#endif
}

/* 以下等待/唤醒接口供内核IPC对象和操作系统兼容层使用，以调用方的对象自旋锁
 * 同步条件检查和排队/唤醒，避免条件满足恰好发生在换出窗口时丢失通知。
 * 原接口作为公开契约使用，即使底层调度实现变化也须保持其行为。 */

/**
  * @brief  摘下等待队首，设置返回值和数据后使线程就绪。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @param  swap_retval：[输入] 交给被唤醒线程的整数返回状态。
  * @param  swap_data：[输入] 交给被唤醒线程的数据指针，可为空；所有权/生命周期由通信双方约定。
  * @retval true：处理一个等待者；false：队列为空。
  * @note   调用者须持外层对象同步锁；内部持sched_spinlock完成状态交接，但不主动执行调度。
  */
bool z_sched_wake(_wait_q_t *wait_q, int swap_retval, void *swap_data);

/**
  * @brief  依次唤醒同一等待队列中的全部线程。
  * @param  wait_q：[输入输出] 有效队列，调用者持外层对象同步锁。
  * @param  swap_retval：[输入] 交给各等待者的返回状态。
  * @param  swap_data：[输入] 交给各等待者的附带指针，可为空；双方约定内容生命周期，不转移所有权。
  * @retval true：至少处理一个等待者；false：原队列为空。
  * @note   反复调用z_sched_wake直到false，不主动切换线程；并发条件仍需由外层同步。
  */
static inline bool z_sched_wake_all(_wait_q_t *wait_q, int swap_retval,
				    void *swap_data)
{
	bool woken = false;                                       /* 默认没有等待者被处理。 */

	while (z_sched_wake(wait_q, swap_retval, swap_data)) {    /* 逐个唤醒直到队列空，内层每次独立持调度锁。 */
		woken = true;                                     /* 记录本次至少处理过一个等待线程。 */
	}

	/* 只要至少唤醒一个等待者便返回true，不表示已经运行该线程。 */
	return woken;                                             /* 返回是否曾经唤醒等待者，不保证其已实际运行。 */
}

/**
  * @brief  在自旋锁交接下阻塞当前线程，并可接收唤醒附带数据。
  * @param  lock：[输入输出] 调用者当前持有的有效自旋锁，由换出/重调度路径释放或交接。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @param  wait_q：[输入输出] 有效对象等待队列，z_pend_curr系列允许NULL不关联对象；z_sched_wait应传有效对象队列。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @param  data：[输出] 可为空；非空则为可写指针对象，接收唤醒附带数据，不分配内存。
  * @retval 唤醒者设置的返回值；超时通常为-EAGAIN。
  * @note   调用者已持lock；z_pend_curr负责排队和释放/交接锁，返回时原锁不再持有。data非空时保存swap_data快照。
  */
int z_sched_wait(struct k_spinlock *lock, k_spinlock_key_t key,
		 _wait_q_t *wait_q, k_timeout_t timeout, void **data);

#endif    /* ZEPHYR_KERNEL_INCLUDE_KSCHED_H_ */
