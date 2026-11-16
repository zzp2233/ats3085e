/* Zephyr调度器：SPI总线信号量的等待/释放经z_pend_curr与z_reschedule在这里交接到线程状态及运行队列；应用k_sleep也在这里挂起线程。优先级数值越小越优先，等待队列与超时队列承担不同职责。条件分支保留单核/SMP、不同队列后端、用户态与时间片等实现；当前构建选项以.config为准。 */
/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel.h>
#include <ksched.h>
#include <spinlock.h>
#include <kernel/sched_priq.h>
#include <wait_q.h>
#include <kswap.h>
#include <kernel_arch_func.h>
#include <syscall_handler.h>
#include <drivers/timer/system_timer.h>
#include <stdbool.h>
#include <kernel_internal.h>
#include <logging/log.h>
#include <sys/atomic.h>
LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);    /* 复用内核os日志模块，输出级别由内核配置决定。 */

/* 运行队列后端在构建时选定：DUMB为有序链表，SCALABLE为红黑树，MULTIQ为优先级子队列加位图。以下别名把统一操作绑定到实际实现。 */
#if defined(CONFIG_SCHED_DUMB)
#define _priq_run_add		z_priq_dumb_add
#define _priq_run_remove	z_priq_dumb_remove
# if defined(CONFIG_SCHED_CPU_MASK)
#  define _priq_run_best	_priq_dumb_mask_best
# else
#  define _priq_run_best	z_priq_dumb_best
# endif
#elif defined(CONFIG_SCHED_SCALABLE)
#define _priq_run_add		z_priq_rb_add
#define _priq_run_remove	z_priq_rb_remove
#define _priq_run_best		z_priq_rb_best
#elif defined(CONFIG_SCHED_MULTIQ)
#define _priq_run_add		z_priq_mq_add
#define _priq_run_remove	z_priq_mq_remove
#define _priq_run_best		z_priq_mq_best
#endif

/* 对象等待队列独立选择红黑树或有序链表；信号量等待者由相应best接口按优先关系取出。 */
#if defined(CONFIG_WAITQ_SCALABLE)
#define z_priq_wait_add		z_priq_rb_add
#define _priq_wait_remove	z_priq_rb_remove
#define _priq_wait_best		z_priq_rb_best
#elif defined(CONFIG_WAITQ_DUMB)
#define z_priq_wait_add		z_priq_dumb_add
#define _priq_wait_remove	z_priq_dumb_remove
#define _priq_wait_best		z_priq_dumb_best
#endif

struct k_spinlock sched_spinlock;    /* 全局调度锁，保护运行队列、等待状态和CPU调度决策，静态零初始化。 */

/**
  * @brief  更新单核下一线程缓存或SMP允许切换标记。
  * @param  preempt_ok：[输入] 非0表示本次软件状态明确允许切换；0按普通抢占规则判断。
  * @retval 无。
  * @note   内部辅助，调用者持sched_spinlock；单核切换候选改变时可重置时间片。
  */
static void update_cache(int preempt_ok);

/**
  * @brief  在锁内完成线程终止、解除队列及可选权限清理。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者保证目标未在任何CPU执行；不自行free线程控制块或栈，清理join等待者并标记DEAD。
  */
static void end_thread(struct k_thread *thread);

/**
  * @brief  判断线程基础状态是否允许被抢占。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 非0：preempt编码不大于_PREEMPT_THRESHOLD；0：不允许。
  * @note   只读状态，调用者保证线程有效及并发一致。
  */
static inline int is_preempt(struct k_thread *thread)
{
	/* 可抢占门限编码由线程基础状态定义，包含优先级与调度锁状态。 */
	return thread->base.preempt <= _PREEMPT_THRESHOLD;    /* 按合并优先级/调度锁状态的编码判断可抢占性。 */
}

/**
  * @brief  判断线程是否位于特殊meta-IRQ优先级区间。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 非0：属于配置的meta-IRQ区间；未配置该区间时为0。
  * @note   meta-IRQ可打断普通协作线程，其恢复规则由next_up处理。
  */
static inline int is_metairq(struct k_thread *thread)
{
#if CONFIG_NUM_METAIRQ_PRIORITIES > 0
	/* 把线程优先级换成距最高优先级的偏移，再与meta-IRQ区间长度比较。 */
	return (thread->base.prio - K_HIGHEST_THREAD_PRIO)
		< CONFIG_NUM_METAIRQ_PRIORITIES;
#else
	return 0;    /* 当前配置未分配meta-IRQ优先级。 */
#endif
}

#if CONFIG_ASSERT

/**
  * @brief  检查用于早期启动等用途的占位线程标志。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval true：含_THREAD_DUMMY标志；false：其他。
  * @note   仅CONFIG_ASSERT编译，供内部断言使用。
  */
static inline bool is_thread_dummy(struct k_thread *thread)
{
	return (thread->base.thread_state & _THREAD_DUMMY) != 0U;    /* 仅检查占位标志，不修改线程。 */
}
#endif

/* 比较结果只看符号：正数为thread_1优先，0为同优先，负数为thread_2优先；不能依赖差值的具体大小。 */

/**
  * @brief  比较基础优先级及可选截止时间，决定线程调度先后。
  * @param  thread_1：[输入] 第一个非空有效线程对象。
  * @param  thread_2：[输入] 第二个非空有效线程对象。
  * @retval 正：thread_1优先；负：thread_2优先；0：二者等价。
  * @note   仅依赖结果符号。截止时间比较要求所有差值处于32位模空间同一半区间。
  */
int32_t z_sched_prio_cmp(struct k_thread *thread_1,
	struct k_thread *thread_2)
{
	/* prio字段位宽小于32位，两个优先级转int32_t后相减不会溢出。 */
	int32_t b1 = thread_1->base.prio;              /* 提取第一个线程基础优先级，数值小者更优先。 */
	int32_t b2 = thread_2->base.prio;              /* 提取第二个线程基础优先级用于比较。 */

	if (b1 != b2) {                                /* 基础优先级不同则不必继续比较截止时间。 */
		return b2 - b1;                        /* 反向相减，使thread_1数值较小时得到正值。 */
	}

#ifdef CONFIG_SCHED_DEADLINE
	/* 截止时间须处于同一32位模空间的半区间，队列内最大跨度不超过2^31个cycle；因此可由差值符号排序而不必读取当前时刻。 */
	uint32_t d1 = thread_1->base.prio_deadline;    /* 第一个线程的32位绝对cycle截止排序键。 */
	uint32_t d2 = thread_2->base.prio_deadline;    /* 第二个线程的截止排序键。 */

	if (d1 != d2) {                                /* 只有同基础优先级且截止键不同时才用截止差决定先后。 */
		/* 同基础优先级时较早截止者优先；先用无符号减法避免有符号溢出，再转有符号，其超范围转换仍受实现定义。 */
		return (int32_t) (d2 - d1);            /* 较早截止返回正值；正确性依赖半模空间跨度约束。 */
	}
#endif
	return 0;                                      /* 基础优先级及可选截止排序键均相同。 */
}

/**
  * @brief  结合主动让出、当前状态和目标优先级类别决定能否切换。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  preempt_ok：[输入] 非0表示本次软件状态明确允许切换；0按普通抢占规则判断。
  * @retval true：允许切换；false：保持当前协作线程。
  * @note   内部策略判定，不执行上下文切换；调用者保证调度状态稳定。
  */
static ALWAYS_INLINE bool should_preempt(struct k_thread *thread,
					 int preempt_ok)
{
	/* 显式让出等软件状态允许切换时，不再受当前协作线程不可抢占约束。 */
	if (preempt_ok != 0) {                                 /* 主动yield等路径已明确允许本次线程切换。 */
		return true;                                   /* 当前分支满足允许切换条件。 */
	}

	__ASSERT(_current != NULL, "");                        /* 抢占策略必须已经具有有效当前线程对象。 */

	/* 当前线程已挂起、等待或为占位对象时不能继续运行，必须允许选择其他线程。 */
	if (z_is_thread_prevented_from_running(_current)) {    /* 当前线程已不能运行时必须选择其他线程。 */
		return true;                                   /* 当前分支满足允许切换条件。 */
	}

	/* ARM非原子切换可能在同步换出前被中断再次改变挂起状态；这种边界情况需放行切换，原子切换平台不会遇到。 */

	/* 处理非原子切换的ARM窗口：目标超时仍活动时允许完成此前已开始的切换。 */
	if (IS_ENABLED(CONFIG_SWAP_NONATOMIC)
	    && z_is_thread_timeout_active(thread)) {
		return true;                                   /* 当前分支满足允许切换条件。 */
	}

	/* 否则仅允许抢占可抢占线程，或由特殊meta-IRQ优先级线程介入。 */
	if (is_preempt(_current) || is_metairq(thread)) {      /* 允许抢占普通可抢占线程，或让meta-IRQ介入协作线程。 */
		return true;                                   /* 当前分支满足允许切换条件。 */
	}

	return false;                                          /* 当前协作线程仍可运行且无例外，保持它继续执行。 */
}

#ifdef CONFIG_SCHED_CPU_MASK

/**
  * @brief  扫描链表选出允许在本CPU运行的最高优先级线程。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @retval 匹配线程地址；没有匹配返回NULL。
  * @note   仅CONFIG_SCHED_CPU_MASK编译，队列已按优先级排序。
  */
static ALWAYS_INLINE struct k_thread *_priq_dumb_mask_best(sys_dlist_t *pq)
{
	/* 启用CPU亲和掩码时，需扫描优先级队列寻找允许在当前CPU运行的首个线程。 */
	struct k_thread *thread;                                               /* 暂存当前遍历、选择或唤醒的线程对象。 */

	SYS_DLIST_FOR_EACH_CONTAINER(pq, thread, base.qnode_dlist) {           /* 依优先级顺序扫描链表线程，寻找满足CPU亲和条件的首项。 */
		if ((thread->base.cpu_mask & BIT(_current_cpu->id)) != 0) {    /* 目标允许在本CPU运行时即可作为最优候选。 */
			return thread;                                         /* 返回第一个允许在本CPU执行的最高优先级项。 */
		}
	}
	return NULL;                                                           /* 队列中没有符合本CPU亲和条件的线程。 */
}
#endif

/**
  * @brief  将线程按有效优先级插入双向链表队列。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   同优先级排在已有项之后，形成先进先出顺序；调用者持调度锁，idle不入队。
  */
ALWAYS_INLINE void z_priq_dumb_add(sys_dlist_t *pq, struct k_thread *thread)
{
	struct k_thread *t;                                        /* 遍历已有优先级队列的线程对象。 */

	__ASSERT_NO_MSG(!z_is_idle_thread_object(thread));         /* idle由每CPU单独保存，不允许作为普通队列节点操作。 */

	SYS_DLIST_FOR_EACH_CONTAINER(pq, t, base.qnode_dlist) {    /* 从最高优先级开始找第一个低于新线程的节点。 */
		if (z_sched_prio_cmp(thread, t) > 0) {             /* 新线程更优先，应插在当前节点前面。 */

			/* 新线程更优先时，在当前扫描节点之前插入其内嵌链表节点。 */
			sys_dlist_insert(&t->base.qnode_dlist,
					 &thread->base.qnode_dlist);
			return;                                    /* 线程已经插入合适位置，不再追加队尾。 */
		}
	}

	sys_dlist_append(pq, &thread->base.qnode_dlist);           /* 未找到更低优先项，追加队尾，也保持同级FIFO顺序。 */
}

/* SMP当前线程在真正进入不可逆的上下文切换前不放回运行队列，见z_requeue_current。 */

/**
  * @brief  判断当前配置下线程是否需要实际加入运行队列。
  * @param  th：[输入] 非空有效线程对象。
  * @retval 单核恒true；SMP中目标为当前线程时false。
  * @note   SMP当前线程直到不可逆切换阶段才重新入队。
  */
static inline bool should_queue_thread(struct k_thread *th)
{
	return !IS_ENABLED(CONFIG_SMP) || th != _current;    /* 单核总是实际排队；SMP当前线程推迟到换出阶段排队。 */
}

/**
  * @brief  设置QUEUED标记并按配置把线程加入运行队列。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持sched_spinlock；SMP当前线程不立即加入实体队列，而设置swap_ok。
  */
static ALWAYS_INLINE void queue_thread(void *pq,
				       struct k_thread *thread)
{
	thread->base.thread_state |= _THREAD_QUEUED;    /* 标记线程具有就绪队列资格，SMP实体入队时机可能稍后。 */
	if (should_queue_thread(thread)) {              /* 只有非SMP当前线程才立即操作实体运行队列。 */
		_priq_run_add(pq, thread);              /* 通过所选队列后端按优先级入队。 */
	}
#ifdef CONFIG_SMP
	if (thread == _current) {                       /* SMP当前线程重新排到末尾视为主动让出请求。 */
		/* 当前线程排到同优先级末尾表示主动让出，允许下次选择其他线程。 */
		_current_cpu->swap_ok = true;           /* 允许下一次选择同优先级的其他线程。 */
	}
#endif
}

/**
  * @brief  清除QUEUED标记并按配置从运行队列移除线程。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持sched_spinlock，并保证目标具有有效排队状态。
  */
static ALWAYS_INLINE void dequeue_thread(void *pq,
					 struct k_thread *thread)
{
	thread->base.thread_state &= ~_THREAD_QUEUED;    /* 撤销该线程就绪排队标志。 */
	if (should_queue_thread(thread)) {               /* 只有非SMP当前线程才立即操作实体运行队列。 */
		_priq_run_remove(pq, thread);            /* 经配置选择的后端摘除线程节点。 */
	}
}

#ifdef CONFIG_SMP
/* SMP从z_swap进入本函数；只有确定不再返回当前上下文后才能重新入队，否则多CPU可能循环选中彼此仍在运行的线程并死锁。 */

/**
  * @brief  在SMP上下文切换阶段将原当前线程重新放回运行队列。
  * @param  curr：[输入输出] 正在被换出的有效线程对象。
  * @retval 无。
  * @note   仅CONFIG_SMP编译，须在确定不会再直接返回原上下文时调用。
  */
void z_requeue_current(struct k_thread *curr)
{
	if (z_is_thread_queued(curr)) {                        /* 原当前线程仍有可运行排队资格时才重新放回队列。 */
		_priq_run_add(&_kernel.ready_q.runq, curr);    /* 在不可逆换出阶段重新加入原当前线程。 */
	}
}
#endif

/**
  * @brief  检查线程是否等待完成跨CPU终止。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval true：含_THREAD_ABORTING；false：其他。
  * @note   用于SMP终止同步，调用者按终止路径维护对象生命周期。
  */
__unused static inline bool is_aborting(struct k_thread *thread)
{
	return (thread->base.thread_state & _THREAD_ABORTING) != 0U;    /* 查询跨CPU终止是否仍在进行。 */
}

/**
  * @brief  选择下一条应运行的线程并维护SMP排队状态。
  * @param  无。
  * @retval 下一线程地址，普通就绪队列为空时选择当前CPU的idle线程。
  * @note   调用者持调度锁；可能完成当前线程终止、恢复被meta-IRQ打断的协作线程并更新SMP队列。
  */
static ALWAYS_INLINE struct k_thread *next_up(void)
{
	struct k_thread *thread;                                              /* 暂存当前遍历、选择或唤醒的线程对象。 */

	thread = _priq_run_best(&_kernel.ready_q.runq);                       /* 向当前后端查询最高有效优先级的就绪线程。 */

#if (CONFIG_NUM_METAIRQ_PRIORITIES > 0) && (CONFIG_NUM_COOP_PRIORITIES > 0)
	/* meta-IRQ退出应优先回到被它抢占的协作线程，不能随意改选普通高优先级线程，以维持协作线程的不可抢占约定。 */
	struct k_thread *mirqp = _current_cpu->metairq_preempted;             /* 读取meta-IRQ先前打断的协作线程，可能为空。 */

	if (mirqp != NULL && (thread == NULL || !is_metairq(thread))) {       /* meta-IRQ已无更高候选时尝试回到原协作线程。 */
		if (!z_is_thread_prevented_from_running(mirqp)) {             /* 被打断线程仍能运行则优先恢复它。 */
			thread = mirqp;                                       /* 恢复协作线程的连续执行承诺。 */
		} else {
			_current_cpu->metairq_preempted = NULL;               /* 清除不可恢复或已经返回的meta-IRQ抢占记录。 */
		}
	}
#endif

#ifndef CONFIG_SMP
	/* 单核当前线程可留在就绪队列中；否则每种架构的同步切换与中断返回汇编都需负责重新入队。 */
	return (thread != NULL) ? thread : _current_cpu->idle_thread;         /* 普通队列为空时返回本CPU专用idle线程。 */
#else
	/* SMP不使用单核的下一线程缓存；先取队列中最优的非当前线程，再与当前线程比较。此处QUEUED标记不等于正在队列中，而表示当前线程需要/已经被纳入重新排队语义，防止重复入队。 */
	if (is_aborting(_current)) {                                          /* 当前线程已被其他CPU要求终止，先完成清理。 */
		end_thread(_current);                                         /* 当前CPU确认终止标志后执行队列与权限清理。 */
	}

	int queued = z_is_thread_queued(_current);                            /* 保存当前线程是否已经带排队资格，防止重复加入。 */
	int active = !z_is_thread_prevented_from_running(_current);           /* 当前线程未等待/挂起/死亡时仍可参与候选比较。 */

	if (thread == NULL) {                                                 /* 普通就绪队列无候选时改用idle。 */
		thread = _current_cpu->idle_thread;                           /* 选择本CPU空闲线程作为保底运行者。 */
	}

	if (active) {                                                         /* 当前线程仍可运行时与队列最优项比较，不必无条件换走。 */
		int32_t cmp = z_sched_prio_cmp(_current, thread);             /* 比较当前线程与最佳非当前候选的有效优先级。 */

		/* 同优先级只在swap_ok表示主动让出时换到其他线程。 */
		if ((cmp > 0) || ((cmp == 0) && !_current_cpu->swap_ok)) {    /* 当前更优，或同级且未主动让出时继续运行当前线程。 */
			thread = _current;                                    /* 保留当前线程作为最终选择。 */
		}

		if (!should_preempt(thread, _current_cpu->swap_ok)) {         /* 协作/调度锁状态不允许抢占时放弃队列候选。 */
			thread = _current;                                    /* 保留当前线程作为最终选择。 */
		}
	}

	/* 当前线程仍可运行且将被换出时，重新排入运行队列。 */

	/* 选中不同线程后，仅把仍可运行、非idle且未重复排队的原当前线程重新入队。 */
	if (thread != _current && active &&
		!z_is_idle_thread_object(_current) && !queued) {
		queue_thread(&_kernel.ready_q.runq, _current);                /* 把当前线程尾插到同优先级队列。 */
	}

	/* 将即将成为当前线程的对象从运行队列取走，避免其他CPU同时选择它。 */
	if (z_is_thread_queued(thread)) {                                     /* 目标标记为已排队时才摘除其就绪节点。 */
		dequeue_thread(&_kernel.ready_q.runq, thread);                /* 从运行队列摘除目标线程，或同步其SMP排队标记。 */
	}

	_current_cpu->swap_ok = false;                                        /* 本次主动切换许可已消费，下次须重新决定。 */
	return thread;                                                        /* 返回已经维护好SMP排队关系的最终下一线程。 */
#endif
}

/**
  * @brief  把线程移到同优先级队列末尾并更新调度候选。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持sched_spinlock，同级轮转使用，不改变基础优先级。
  */
static void move_thread_to_end_of_prio_q(struct k_thread *thread)
{
	if (z_is_thread_queued(thread)) {                         /* 目标标记为已排队时才摘除其就绪节点。 */
		dequeue_thread(&_kernel.ready_q.runq, thread);    /* 从运行队列摘除目标线程，或同步其SMP排队标记。 */
	}
	queue_thread(&_kernel.ready_q.runq, thread);              /* 按当前优先级把线程加入运行队列末尾。 */
	update_cache(thread == _current);                         /* 更新候选，若改变的是当前线程则允许本次主动切换。 */
}

#ifdef CONFIG_TIMESLICING

static int slice_time;        /* 全局时间片长度，单位tick，0表示关闭时间片。 */
static int slice_max_prio;    /* 参与时间片轮转的最高优先级门限，数值更小的线程不受此轮转限制。 */

#ifdef CONFIG_SWAP_NONATOMIC
/* 非原子z_swap允许线程已挂起但尚未实际换出时被定时器中断；pending_current使时间片处理跳过这个窗口，避免把已挂起线程再次换出。 */
static struct k_thread *pending_current;    /* 记录已进入挂起但尚未完成非原子换出的当前线程。 */
#endif

/**
  * @brief  重建本CPU当前时间片的tick预算与硬件事件。
  * @param  无。
  * @retval 无。
  * @note   仅CONFIG_TIMESLICING编译；slice_time为0时不安排时间片事件，含驱动未报告elapsed。
  */
void z_reset_time_slice(void)
{
	/* 时间片预算加上距上次报告后的elapsed，因为这些经过量会在未来z_time_slice调用中再次作为上报tick出现。 */
	if (slice_time != 0) {                                                   /* 只在时间片启用时建立新的预算。 */
		_current_cpu->slice_ticks = slice_time + sys_clock_elapsed();    /* 把驱动尚未报告量补入时间片，避免未来上报重复缩短新时间片。 */
		z_set_timeout_expiry(slice_time, false);                         /* 要求硬件事件及时到来以检查时间片结束。 */
	}
}

/**
  * @brief  配置时间片长度及参与轮转的优先级门限。
  * @param  slice：[输入] 时间片长度，单位毫秒；0关闭时间片，负值不由本实现显式检查。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval 无。
  * @note   仅CONFIG_TIMESLICING编译；tickless正时间片至少2 tick，内部锁住调度状态。
  */
void k_sched_time_slice_set(int32_t slice, int prio)
{
	LOCKED(&sched_spinlock) {                                         /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		_current_cpu->slice_ticks = 0;                            /* 清空旧的当前CPU时间片预算。 */
		slice_time = k_ms_to_ticks_ceil32(slice);                 /* 把毫秒时间片向上换算成tick，避免正时长被向下截短。 */
		if (IS_ENABLED(CONFIG_TICKLESS_KERNEL) && slice > 0) {    /* tickless正时间片需避免难以可靠安排的单tick事件。 */
			/* tickless模式无法可靠安排只有1 tick的事件，因此正时间片至少设为2 tick。 */
			slice_time = MAX(2, slice_time);                  /* 保证tickless正时间片至少2 tick。 */
		}
		slice_max_prio = prio;                                    /* 记录允许时间片轮转的优先级门限。 */
		z_reset_time_slice();                                     /* 按最新时间片配置或新线程重新开始本CPU预算。 */
	}
}

/**
  * @brief  判断线程是否可参与当前配置的时间片轮转。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 非0：可抢占、可运行、优先级符合门限且非idle；否则0。
  * @note   仅时间片配置使用，不修改线程状态。
  */
static inline int sliceable(struct k_thread *thread)
{
	/* 可参与轮转须同时满足可抢占、可运行、门限及非idle四项条件。 */
	return is_preempt(thread)
		&& !z_is_thread_prevented_from_running(thread)
		&& !z_is_prio_higher(thread->base.prio, slice_max_prio)
		&& !z_is_idle_thread_object(thread);
}

/* 由系统时钟报告路径调用，ticks为本次新经过的内核tick。 */

/**
  * @brief  消费新报告的tick，时间片耗尽时把当前线程移到同级末尾。
  * @param  ticks：[输入] 本次tick数量；睡眠接口中为k_ticks_t预算，单位由系统tick频率决定。
  * @retval 无。
  * @note   通常从时钟上报路径调用，内部持sched_spinlock；处理非原子换出窗口避免重复挂起。
  */
void z_time_slice(int ticks)
{
	/* 锁住调度状态以防其他CPU终止线程或嵌套普通中断改变判断依据；此处本来可能处于中断已开放的时钟处理路径。 */
	k_spinlock_key_t key = k_spin_lock(&sched_spinlock);       /* 锁住调度状态并保存进入前的本CPU中断状态。 */

#ifdef CONFIG_SWAP_NONATOMIC
	if (pending_current == _current) {                         /* 当前线程正处于非原子换出窗口，不能再由时间片换出一次。 */
		z_reset_time_slice();                              /* 按最新时间片配置或新线程重新开始本CPU预算。 */
		k_spin_unlock(&sched_spinlock, key);               /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
		return;                                            /* 当前线程正挂起换出，跳过本次时间片处理。 */
	}
	pending_current = NULL;                                    /* 结束非原子挂起窗口的保护记录。 */
#endif

	if (slice_time && sliceable(_current)) {                   /* 时间片启用且当前线程满足轮转条件才扣预算。 */
		if (ticks >= _current_cpu->slice_ticks) {          /* 本次经过量已经耗尽剩余时间片。 */
			move_thread_to_end_of_prio_q(_current);    /* 让同级其他线程获得下一轮运行机会。 */
			z_reset_time_slice();                      /* 按最新时间片配置或新线程重新开始本CPU预算。 */
		} else {
			_current_cpu->slice_ticks -= ticks;        /* 预算未耗尽，仅扣除本次报告的tick。 */
		}
	} else {
		_current_cpu->slice_ticks = 0;                     /* 清空旧的当前CPU时间片预算。 */
	}
	k_spin_unlock(&sched_spinlock, key);                       /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
}
#endif

/* 选出新线程时记录被meta-IRQ介入的协作线程，便于之后恢复到该线程。 */

/**
  * @brief  记录或清除meta-IRQ介入的协作线程恢复目标。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者已决定下一线程并持调度锁；未配置meta-IRQ和协作优先级组合时为空逻辑。
  */
static void update_metairq_preempt(struct k_thread *thread)
{
#if (CONFIG_NUM_METAIRQ_PRIORITIES > 0) && (CONFIG_NUM_COOP_PRIORITIES > 0)

	/* 从协作线程切入meta-IRQ时保存原线程；返回普通非idle线程时清除恢复记录。 */
	if (is_metairq(thread) && !is_metairq(_current) &&
	    !is_preempt(_current)) {
		/* 记录本次meta-IRQ抢占的协作线程。 */
		_current_cpu->metairq_preempted = _current;    /* 保存被meta-IRQ打断的协作线程以供退出时恢复。 */
	} else if (!is_metairq(thread) && !z_is_idle_thread_object(thread)) {
		/* 正在返回普通线程，清除先前抢占记录。 */
		_current_cpu->metairq_preempted = NULL;        /* 清除不可恢复或已经返回的meta-IRQ抢占记录。 */
	}
#endif
}

/**
  * @brief  更新单核下一线程缓存或SMP允许切换标记。
  * @param  preempt_ok：[输入] 非0表示本次软件状态明确允许切换；0按普通抢占规则判断。
  * @retval 无。
  * @note   内部辅助，调用者持sched_spinlock；单核切换候选改变时可重置时间片。
  */
static void update_cache(int preempt_ok)
{
#ifndef CONFIG_SMP
	struct k_thread *thread = next_up();         /* 按运行队列、协作规则及meta-IRQ恢复关系选取候选。 */

	if (should_preempt(thread, preempt_ok)) {    /* 当前上下文允许切换时才接受新候选。 */
#ifdef CONFIG_TIMESLICING
		if (thread != _current) {            /* 只有候选改变才需要重设时间片/执行切换相关处理。 */
			z_reset_time_slice();        /* 按最新时间片配置或新线程重新开始本CPU预算。 */
		}
#endif
		update_metairq_preempt(thread);      /* 更新协作线程被特殊优先级介入的恢复记录。 */
		_kernel.ready_q.cache = thread;      /* 单核缓存下一条应运行的线程。 */
	} else {
		_kernel.ready_q.cache = _current;    /* 当前不可被抢占时缓存仍指向_current。 */
	}

#else
	/* SMP把允许协作切换的状态存于CPU，保持到下一次重调度或切换；无需每线程保存，因为被抢占后重新比较仍会得到相同决定。 */
	_current_cpu->swap_ok = preempt_ok;          /* SMP保存这次允许主动切换的状态，直到实际决策。 */
#endif
}

/**
  * @brief  检查目标线程是否正在其他CPU执行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval true：其他CPU的current指向目标；单核或未找到为false。
  * @note   调用者持调度锁，避免根据已过时执行位置重复加入运行队列。
  */
static bool thread_active_elsewhere(struct k_thread *thread)
{
	/* SMP扫描其他CPU的current字段，判断目标是否正在别处运行；复杂度随CPU数增加，单核固定false。 */
#ifdef CONFIG_SMP
	int currcpu = _current_cpu->id;                   /* 取得当前CPU编号以排除自身。 */

	for (int i = 0; i < CONFIG_MP_NUM_CPUS; i++) {    /* 逐个检查其他CPU的当前线程指针。 */

		/* 排除当前CPU，匹配其他CPU的current字段以识别远端活动线程。 */
		if ((i != currcpu) &&
		    (_kernel.cpus[i].current == thread)) {
			return true;                      /* 已在其他CPU的current字段找到目标。 */
		}
	}
#endif
	return false;                                     /* 单核或所有其他CPU均未运行此线程。 */
}

/**
  * @brief  将满足就绪条件且尚未排队的线程加入运行队列。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持sched_spinlock；有等待/挂起/活动超时时不会入队，SMP可发送调度IPI。
  */
static void ready_thread(struct k_thread *thread)
{
#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(arch_mem_coherent(thread));                          /* 一致性配置下要求线程状态内存可被相关CPU一致观察。 */
#endif

	/* 已经带QUEUED标记时不能重复加入运行队列；同时仍须满足所有就绪条件。 */
	if (!z_is_thread_queued(thread) && z_is_thread_ready(thread)) {      /* 避免重复入队，且等待/挂起/未启动/死亡等状态均已解除。 */
		SYS_PORT_TRACING_OBJ_FUNC(k_thread, sched_ready, thread);    /* 向可选跟踪后端记录sched_ready事件及本次操作的参数/状态。 */

		queue_thread(&_kernel.ready_q.runq, thread);                 /* 按当前优先级把线程加入运行队列末尾。 */
		update_cache(0);                                             /* 按普通抢占规则更新下一候选，不强制协作线程让出。 */
#if defined(CONFIG_SMP) &&  defined(CONFIG_SCHED_IPI_SUPPORTED)
		arch_sched_ipi();                                            /* 通知其他CPU重新评估可运行线程，实际切换由各CPU完成。 */
#endif
	}
}

/**
  * @brief  在锁保护下尝试使线程就绪，避免重复调度其他CPU上的线程。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   内部持调度锁；只入就绪队列和更新候选，不保证立即执行该线程。
  */
void z_ready_thread(struct k_thread *thread)
{
	LOCKED(&sched_spinlock) {                          /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		if (!thread_active_elsewhere(thread)) {    /* 目标未在其他CPU运行时才允许本路径排为就绪。 */
			ready_thread(thread);              /* 检查全部就绪条件后入运行队列，不在此直接切换。 */
		}
	}
}

/**
  * @brief  加锁后将线程移动到同优先级末尾。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   更新队列与候选，但实际线程切换由后续重调度完成。
  */
void z_move_thread_to_end_of_prio_q(struct k_thread *thread)
{
	LOCKED(&sched_spinlock) {                        /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		move_thread_to_end_of_prio_q(thread);    /* 在调度锁内完成同优先级轮转。 */
	}
}

/**
  * @brief  启动尚处于PRESTART状态的线程并评估调度。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   已经启动则不重复操作；持锁清PRESTART、就绪后由z_reschedule释放锁。
  */
void z_sched_start(struct k_thread *thread)
{
	k_spinlock_key_t key = k_spin_lock(&sched_spinlock);    /* 锁住调度状态并保存进入前的本CPU中断状态。 */

	if (z_has_thread_started(thread)) {                     /* 已经启动的线程无需重复清PRESTART或入队。 */
		k_spin_unlock(&sched_spinlock, key);            /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
		return;                                         /* 目标已经启动，释放锁后保持原状态。 */
	}

	z_mark_thread_as_started(thread);                       /* 清PRESTART标记，解除尚未启动的运行限制。 */
	ready_thread(thread);                                   /* 检查全部就绪条件后入运行队列，不在此直接切换。 */
	z_reschedule(&sched_spinlock, key);                     /* 把锁钥匙交给调度出口，由其换出或显式解锁。 */
}

/**
  * @brief  取消线程定时等待并设置挂起状态。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   挂起当前线程会主动重调度；不释放线程对象/栈。调用者维护生命周期及接口上下文约束。
  */
void z_impl_k_thread_suspend(struct k_thread *thread)
{
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_thread, suspend, thread);       /* 向可选跟踪后端记录suspend事件及本次操作的参数/状态。 */

	(void)z_abort_thread_timeout(thread);                             /* 尝试取消活动超时，未激活状态不作为本路径错误。 */

	LOCKED(&sched_spinlock) {                                         /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		if (z_is_thread_queued(thread)) {                         /* 目标标记为已排队时才摘除其就绪节点。 */
			dequeue_thread(&_kernel.ready_q.runq, thread);    /* 从运行队列摘除目标线程，或同步其SMP排队标记。 */
		}
		z_mark_thread_as_suspended(thread);                       /* 设置挂起标志，使线程暂时不满足就绪条件。 */
		update_cache(thread == _current);                         /* 更新候选，若改变的是当前线程则允许本次主动切换。 */
	}

	if (thread == _current) {                                         /* SMP当前线程重新排到末尾视为主动让出请求。 */
		z_reschedule_unlocked();                                  /* 当前未持自旋锁，重新屏蔽中断后评估线程切换。 */
	}

	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_thread, suspend, thread);        /* 向可选跟踪后端记录suspend事件及本次操作的参数/状态。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：取消线程定时等待并设置挂起状态。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline void z_vrfy_k_thread_suspend(struct k_thread *thread)
{
	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));    /* 验证用户线程对象的类型、初始化及访问权限，失败触发系统调用异常。 */
	z_impl_k_thread_suspend(thread);                /* 执行已验证对象的挂起流程。 */
}
#include <syscalls/k_thread_suspend_mrsh.c>
#endif

/**
  * @brief  清除线程挂起标志并重新评估其可运行条件。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   非挂起线程立即返回；其他等待条件仍可能阻止其就绪，内部锁最终交给z_reschedule释放。
  */
void z_impl_k_thread_resume(struct k_thread *thread)
{
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_thread, resume, thread);    /* 向可选跟踪后端记录resume事件及本次操作的参数/状态。 */

	k_spinlock_key_t key = k_spin_lock(&sched_spinlock);          /* 锁住调度状态并保存进入前的本CPU中断状态。 */

	/* 没有挂起标志的线程无需恢复，直接保持原状态。 */
	if (!z_is_thread_suspended(thread)) {                         /* 无挂起标志时保持原状态，不能重复恢复。 */
		k_spin_unlock(&sched_spinlock, key);                  /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
		return;                                               /* 线程没有挂起标志，不执行重复恢复。 */
	}

	z_mark_thread_as_not_suspended(thread);                       /* 清SUSPENDED，其他等待或超时条件仍需满足才就绪。 */
	ready_thread(thread);                                         /* 检查全部就绪条件后入运行队列，不在此直接切换。 */

	z_reschedule(&sched_spinlock, key);                           /* 把锁钥匙交给调度出口，由其换出或显式解锁。 */

	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_thread, resume, thread);     /* 向可选跟踪后端记录resume事件及本次操作的参数/状态。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：清除线程挂起标志并重新评估其可运行条件。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline void z_vrfy_k_thread_resume(struct k_thread *thread)
{
	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));    /* 验证用户线程对象的类型、初始化及访问权限，失败触发系统调用异常。 */
	z_impl_k_thread_resume(thread);                 /* 执行已验证线程的恢复逻辑。 */
}
#include <syscalls/k_thread_resume_mrsh.c>
#endif

/**
  * @brief  取得线程当前所属等待队列。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 非空等待队列指针，前提由断言检查。
  * @note   只适用于已在等待队列中的线程，调用者持调度锁。
  */
static _wait_q_t *pended_on_thread(struct k_thread *thread)
{
	__ASSERT_NO_MSG(thread->base.pended_on);    /* 队列摘除要求线程确实关联到等待对象。 */

	return thread->base.pended_on;              /* 返回该线程当前等待对象的队列。 */
}

/**
  * @brief  从运行队列移除线程并更新下一候选。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持sched_spinlock；此辅助不设置PENDING/SUSPENDED，状态由上层路径决定。
  */
static void unready_thread(struct k_thread *thread)
{
	if (z_is_thread_queued(thread)) {                         /* 目标标记为已排队时才摘除其就绪节点。 */
		dequeue_thread(&_kernel.ready_q.runq, thread);    /* 从运行队列摘除目标线程，或同步其SMP排队标记。 */
	}
	update_cache(thread == _current);                         /* 更新候选，若改变的是当前线程则允许本次主动切换。 */
}

/* 调用者须持sched_spinlock，使移出运行队列、标记等待与加入等待队列形成同一临界区。 */

/**
  * @brief  把线程从运行状态转入指定对象等待队列。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 无。
  * @note   调用者须持sched_spinlock；wait_q为NULL时只置PENDING，不关联对象队列。
  */
static void add_to_waitq_locked(struct k_thread *thread, _wait_q_t *wait_q)
{
	unready_thread(thread);                                 /* 先移出运行队列，避免线程同时处于可运行和等待状态。 */
	z_mark_thread_as_pending(thread);                       /* 置PENDING表示线程正在等待对象条件。 */

	SYS_PORT_TRACING_FUNC(k_thread, sched_pend, thread);    /* 向可选跟踪后端记录sched_pend事件及本次操作的参数/状态。 */

	if (wait_q != NULL) {                                   /* 有具体内核对象时才关联它的等待队列。 */
		thread->base.pended_on = wait_q;                /* 记录所属等待队列，供解除等待时定位。 */
		z_priq_wait_add(&wait_q->waitq, thread);        /* 按等待队列后端的优先级规则加入线程。 */
	}
}

/**
  * @brief  为有限等待建立线程到期事件。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 无。
  * @note   K_FOREVER不入超时队列；具体tick对齐和差分排序由timeout模块完成。
  */
static void add_thread_timeout(struct k_thread *thread, k_timeout_t timeout)
{
	if (!K_TIMEOUT_EQ(timeout, K_FOREVER)) {          /* 只有有限预算需要注册到内核超时队列。 */
		z_add_thread_timeout(thread, timeout);    /* 关联线程到期回调，按预算安排超时唤醒。 */
	}
}

/**
  * @brief  完成线程等待队列登记及可选超时登记。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 无。
  * @note   内部仅在改等待状态时持sched_spinlock，随后加入超时；调用者须按外层对象锁约定防止丢失唤醒。
  */
static void pend(struct k_thread *thread, _wait_q_t *wait_q,
		 k_timeout_t timeout)
{
#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(wait_q == NULL || arch_mem_coherent(wait_q));    /* 等待队列若存在，要求其内存满足架构一致性。 */
#endif

	LOCKED(&sched_spinlock) {                                        /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		add_to_waitq_locked(thread, wait_q);                     /* 在锁内完成移出运行队列、标记等待及对象队列登记。 */
	}

	add_thread_timeout(thread, timeout);                             /* 在等待状态建立后登记有限超时。 */
}

/**
  * @brief  为当前线程或占位线程建立对象等待和超时。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 无。
  * @note   断言限制目标为_current或dummy，不直接切换上下文；上层决定何时换出。
  */
void z_pend_thread(struct k_thread *thread, _wait_q_t *wait_q,
		   k_timeout_t timeout)
{
	__ASSERT_NO_MSG(thread == _current || is_thread_dummy(thread));    /* 只能直接挂起当前线程或特殊dummy线程。 */
	pend(thread, wait_q, timeout);                                     /* 建立对象等待与超时，本调用本身不执行上下文切换。 */
}

/**
  * @brief  从对象等待队列摘下线程并清除PENDING。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持sched_spinlock且目标确实在队列中；不取消超时，也不直接置为就绪。
  */
static inline void unpend_thread_no_timeout(struct k_thread *thread)
{
	_priq_wait_remove(&pended_on_thread(thread)->waitq, thread);    /* 从记录的对象等待队列摘除线程节点。 */
	z_mark_thread_as_not_pending(thread);                           /* 清PENDING，解除对象等待限制。 */
	thread->base.pended_on = NULL;                                  /* 摘除后不再保留所属等待队列地址。 */
}

/**
  * @brief  加锁摘除线程的对象等待状态，保留超时事件。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   通常需由上层随后取消超时并决定是否就绪。
  */
ALWAYS_INLINE void z_unpend_thread_no_timeout(struct k_thread *thread)
{
	LOCKED(&sched_spinlock) {                    /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		unpend_thread_no_timeout(thread);    /* 只摘除对象等待，不在此取消或重建超时事件。 */
	}
}

#ifdef CONFIG_SYS_CLOCK_EXISTS
/* 内核线程超时回调，超时模块摘除节点并释放timeout_lock后才进入此处。 */

/**
  * @brief  在线程等待到期后清等待/挂起标志并尝试就绪。
  * @param  timeout：[输入输出] 嵌入线程base.timeout的已到期_timeout对象，不可为空；超时模块已将其摘除。
  * @retval 无。
  * @note   超时模块已摘除超时节点并释放其锁；内部持调度锁，已死亡/终止中的线程不再唤醒。
  */
void z_thread_timeout(struct _timeout *timeout)
{

	/* 由已到期_timeout成员反推所属线程，随后在调度锁内解除其等待状态。 */
	struct k_thread *thread = CONTAINER_OF(timeout,
					       struct k_thread, base.timeout);

	LOCKED(&sched_spinlock) {                                    /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */

		/* 已死亡或正在跨CPU终止的线程不可被定时事件重新投入运行。 */
		bool killed = ((thread->base.thread_state & _THREAD_DEAD) ||
			       (thread->base.thread_state & _THREAD_ABORTING));

		if (!killed) {                                       /* 死亡或跨CPU终止中的线程不能因超时重新运行。 */
			if (thread->base.pended_on != NULL) {        /* 仍挂在对象队列上时先完成摘除。 */
				unpend_thread_no_timeout(thread);    /* 只摘除对象等待，不在此取消或重建超时事件。 */
			}
			z_mark_thread_as_started(thread);            /* 清PRESTART标记，解除尚未启动的运行限制。 */
			z_mark_thread_as_not_suspended(thread);      /* 清SUSPENDED，其他等待或超时条件仍需满足才就绪。 */
			ready_thread(thread);                        /* 检查全部就绪条件后入运行队列，不在此直接切换。 */
		}
	}
}
#endif

/**
  * @brief  在调用方已屏蔽中断的前提下挂起当前线程并换出。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @param  wait_q：[输入输出] 有效对象等待队列，z_pend_curr系列允许NULL不关联对象；z_sched_wait应传有效对象队列。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 由唤醒路径设置的切换返回值，有限等待超时通常为-EAGAIN。
  * @note   接收原中断钥匙并交给z_swap_irqlock恢复，不能在ISR执行可阻塞调用。
  */
int z_pend_curr_irqlock(uint32_t key, _wait_q_t *wait_q, k_timeout_t timeout)
{
	pend(_current, wait_q, timeout);              /* 当前线程登记对象等待及可选到期事件。 */

#if defined(CONFIG_TIMESLICING) && defined(CONFIG_SWAP_NONATOMIC)
	pending_current = _current;                   /* 记录即将换出的线程，避免非原子切换窗口被时间片重复处理。 */

	int ret = z_swap_irqlock(key);                /* 交接中断钥匙并切走，恢复后接收唤醒路径的返回值。 */
	LOCKED(&sched_spinlock) {                     /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		if (pending_current == _current) {    /* 当前线程正处于非原子换出窗口，不能再由时间片换出一次。 */
			pending_current = NULL;       /* 结束非原子挂起窗口的保护记录。 */
		}
	}
	return ret;                                   /* 返回线程恢复时由唤醒路径设置的状态。 */
#else
	return z_swap_irqlock(key);                   /* 切出当前线程并在唤醒后传播切换返回值，恢复原中断状态。 */
#endif
}

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
	       _wait_q_t *wait_q, k_timeout_t timeout)
{
#if defined(CONFIG_TIMESLICING) && defined(CONFIG_SWAP_NONATOMIC)
	pending_current = _current;         /* 记录即将换出的线程，避免非原子切换窗口被时间片重复处理。 */
#endif
	pend(_current, wait_q, timeout);    /* 当前线程登记对象等待及可选到期事件。 */
	return z_swap(lock, key);           /* 把自旋锁交给底层切换路径，唤醒后返回而不重新持有该锁。 */
}

/**
  * @brief  摘下等待队列中最优线程而保留它的超时事件。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 被摘线程地址；空队列返回NULL。
  * @note   内部持调度锁，不直接就绪；调用方负责超时和状态后续处理。
  */
struct k_thread *z_unpend1_no_timeout(_wait_q_t *wait_q)
{
	struct k_thread *thread = NULL;                      /* 默认无候选/无等待者，只有找到节点时赋有效线程。 */

	LOCKED(&sched_spinlock) {                            /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		thread = _priq_wait_best(&wait_q->waitq);    /* 按等待队列排序取最优等待者，不在这里直接修改其返回值。 */

		if (thread != NULL) {                        /* 存在可处理线程时才摘除等待、赋状态或就绪。 */
			unpend_thread_no_timeout(thread);    /* 只摘除对象等待，不在此取消或重建超时事件。 */
		}
	}

	return thread;                                       /* 返回摘下的等待者或NULL，超时仍由上层管理。 */
}

/**
  * @brief  摘下等待队首线程并取消其活动超时。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 被摘线程地址；空队列返回NULL。
  * @note   内部持调度锁，不直接设置就绪；信号量give随后写返回值并调用z_ready_thread。
  */
struct k_thread *z_unpend_first_thread(_wait_q_t *wait_q)
{
	struct k_thread *thread = NULL;                          /* 默认无候选/无等待者，只有找到节点时赋有效线程。 */

	LOCKED(&sched_spinlock) {                                /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		thread = _priq_wait_best(&wait_q->waitq);        /* 按等待队列排序取最优等待者，不在这里直接修改其返回值。 */

		if (thread != NULL) {                            /* 存在可处理线程时才摘除等待、赋状态或就绪。 */
			unpend_thread_no_timeout(thread);        /* 只摘除对象等待，不在此取消或重建超时事件。 */
			(void)z_abort_thread_timeout(thread);    /* 尝试取消活动超时，未激活状态不作为本路径错误。 */
		}
	}

	return thread;                                           /* 返回已摘等待并尝试取消超时的线程，空队列为NULL。 */
}

/**
  * @brief  摘除指定线程等待状态并尝试取消线程超时。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   线程必须在对象等待队列中；本函数不直接加入运行队列。
  */
void z_unpend_thread(struct k_thread *thread)
{
	z_unpend_thread_no_timeout(thread);      /* 在内部调度锁保护下取消对象等待。 */
	(void)z_abort_thread_timeout(thread);    /* 尝试取消活动超时，未激活状态不作为本路径错误。 */
}

/* 本辅助只更新优先级与运行队列，不实际执行调度；返回值提示上层之后是否需要重新选择线程。 */

/**
  * @brief  改变线程基础优先级并按需重新排列运行队列。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval true：线程当时满足就绪条件，之后可能需要调度；false：无需因该线程就绪重选。
  * @note   不实际上下文切换；调用者确保优先级合法，等待队列重新排序行为不由此保证。
  */
bool z_set_prio(struct k_thread *thread, int prio)
{
	bool need_sched = 0;                                                            /* 默认无需因优先级更新重新调度。 */

	LOCKED(&sched_spinlock) {                                                       /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		need_sched = z_is_thread_ready(thread);                                 /* 先确认目标是否具有参与运行队列排序的资格。 */

		if (need_sched) {                                                       /* 就绪线程改变优先级需要同步重新排队。 */
			/* SMP中正在运行且不在队列的线程只修改优先级，不做无效出队/重入队。 */
			if (!IS_ENABLED(CONFIG_SMP) || z_is_thread_queued(thread)) {    /* 单核或确实带排队标志的SMP线程才做实体重排。 */
				dequeue_thread(&_kernel.ready_q.runq, thread);          /* 从运行队列摘除目标线程，或同步其SMP排队标记。 */
				thread->base.prio = prio;                               /* 保存新的基础优先级，越小越优先。 */
				queue_thread(&_kernel.ready_q.runq, thread);            /* 按当前优先级把线程加入运行队列末尾。 */
			} else {
				thread->base.prio = prio;                               /* 保存新的基础优先级，越小越优先。 */
			}
			update_cache(1);                                                /* 优先级变化允许重新评估当前协作执行选择。 */
		} else {
			thread->base.prio = prio;                                       /* 保存新的基础优先级，越小越优先。 */
		}
	}

	SYS_PORT_TRACING_OBJ_FUNC(k_thread, sched_priority_set, thread, prio);          /* 向可选跟踪后端记录sched_priority_set事件及本次操作的参数/状态。 */

	return need_sched;                                                              /* 向上层提示是否应在合适出口重新调度。 */
}

/**
  * @brief  改变优先级并在需要且当前未锁调度时重选线程。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval 无。
  * @note   合法范围由上层验证；SMP可发送IPI通知其他CPU重新选择。
  */
void z_thread_priority_set(struct k_thread *thread, int prio)
{
	bool need_sched = z_set_prio(thread, prio);               /* 先完成队列重排，取得是否需要重调度的提示。 */

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_IPI_SUPPORTED)
	arch_sched_ipi();                                         /* 通知其他CPU重新评估可运行线程，实际切换由各CPU完成。 */
#endif

	if (need_sched && _current->base.sched_locked == 0U) {    /* 当前未锁调度且变化影响就绪线程时才同步重选。 */
		z_reschedule_unlocked();                          /* 当前未持自旋锁，重新屏蔽中断后评估线程切换。 */
	}
}

/**
  * @brief  判断当前上下文是否可在释放临界区时立即调度。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @retval true：原中断状态允许且不在ISR；false：只能恢复中断/锁状态。
  * @note   SMP同时清本CPU swap_ok，ISR的实际切换留给中断退出。
  */
static inline bool resched(uint32_t key)
{
#ifdef CONFIG_SMP
	_current_cpu->swap_ok = 0;                             /* 清除本次之前残留的主动切换许可。 */
#endif

	return arch_irq_unlocked(key) && !arch_is_in_isr();    /* 只有进入前允许普通中断且在任务上下文才可同步调度。 */
}

/* 下一就绪线程仍是当前线程时省去上下文切换。 */

/**
  * @brief  判断单核缓存候选是否与当前线程不同。
  * @param  无。
  * @retval 单核不同则true；SMP恒true，交给底层再判断。
  * @note   仅决策，不修改队列或上下文。
  */
static inline bool need_swap(void)
{
	/* SMP是否真的换线程留给C实现的z_swap处理。 */
#ifdef CONFIG_SMP
	return true;                           /* SMP将实际是否换线程留给底层z_swap决定。 */
#else
	struct k_thread *new_thread;           /* 接收单核缓存的下一就绪候选地址。 */

	/* 单核读取已更新的下一就绪线程缓存，与_current比较。 */
	new_thread = _kernel.ready_q.cache;    /* 单核取得先前更新好的最高合适候选。 */
	return new_thread != _current;         /* 候选等于当前线程则省去上下文保存/恢复。 */
#endif
}

/**
  * @brief  交接自旋锁后按上下文与候选决定是否换线程。
  * @param  lock：[输入输出] 调用者当前持有的有效自旋锁，由换出/重调度路径释放或交接。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @retval 无。
  * @note   必定在本路径释放或交接传入lock；ISR或外层中断仍屏蔽时不在此同步切换。
  */
void z_reschedule(struct k_spinlock *lock, k_spinlock_key_t key)
{
	if (resched(key.key) && need_swap()) {    /* 原中断状态及当前上下文允许，且存在切换需求才换线程。 */
		z_swap(lock, key);                /* 由上下文切换路径释放传入锁并恢复原中断状态。 */
	} else {
		k_spin_unlock(lock, key);         /* 无需切换也必须成对释放原锁。 */
	}
}

/**
  * @brief  以中断钥匙为交接条件进行重调度或恢复中断。
  * @param  key：[输入] 与当前临界区匹配的原始中断/自旋锁钥匙。
  * @retval 无。
  * @note   无需传入自旋锁；允许调度时调用z_swap_irqlock，否则irq_unlock恢复原状态。
  */
void z_reschedule_irqlock(uint32_t key)
{
	if (resched(key)) {             /* 无外层屏蔽且非ISR时才同步调度。 */
		z_swap_irqlock(key);    /* 以中断钥匙进入上下文切换。 */
	} else {
		irq_unlock(key);        /* 不能同步切换时直接恢复原中断状态。 */
	}
}

/**
  * @brief  增加当前线程的调度锁嵌套，暂禁普通线程抢占。
  * @param  无。
  * @retval 无。
  * @note   线程上下文调用；调度锁不同于中断屏蔽，不能据此保证硬件寄存器互斥。
  */
void k_sched_lock(void)
{
	LOCKED(&sched_spinlock) {                               /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		SYS_PORT_TRACING_FUNC(k_thread, sched_lock);    /* 向可选跟踪后端记录sched_lock事件及本次操作的参数/状态。 */

		z_sched_lock();                                 /* 增加当前线程调度锁嵌套，保持普通中断仍可按原规则响应。 */
	}
}

/**
  * @brief  减少当前线程调度锁嵌套并重新评估调度。
  * @param  无。
  * @retval 无。
  * @note   必须与k_sched_lock匹配且不在ISR，最后一层解锁后可被就绪高优先级线程切走。
  */
void k_sched_unlock(void)
{
	LOCKED(&sched_spinlock) {                         /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		__ASSERT(_current->base.sched_locked != 0U, "");
		__ASSERT(!arch_is_in_isr(), "");          /* 睡眠、yield或调度锁接口必须在任务上下文执行。 */

		++_current->base.sched_locked;            /* 调度锁嵌套按无符号反向编码回退一层，0表示完全解锁。 */
		update_cache(0);                          /* 按普通抢占规则更新下一候选，不强制协作线程让出。 */
	}

	LOG_DBG("scheduler unlocked (%p:%d)",
		_current, _current->base.sched_locked);

	SYS_PORT_TRACING_FUNC(k_thread, sched_unlock);    /* 向可选跟踪后端记录sched_unlock事件及本次操作的参数/状态。 */

	z_reschedule_unlocked();                          /* 当前未持自旋锁，重新屏蔽中断后评估线程切换。 */
}

/**
  * @brief  返回上下文切换路径已经选择或即将选择的下一线程。
  * @param  无。
  * @retval SMP由next_up选择；单核返回ready_q.cache。
  * @note   由切换路径在合适同步条件下调用。
  */
struct k_thread *z_swap_next_thread(void)
{
#ifdef CONFIG_SMP
	return next_up();                /* SMP在切换阶段即时选择下一线程。 */
#else
	return _kernel.ready_q.cache;    /* 单核使用已维护的下一线程缓存。 */
#endif
}

/* 更新当前线程指针前先记录换出跟踪信息。 */

/**
  * @brief  跟踪换出事件后更新本CPU的当前线程指针。
  * @param  new_thread：[输入输出] 即将成为当前线程的非空对象。
  * @retval 无。
  * @note   调用者保证上下文已进入切换流程，不能作为普通线程切换API单独调用。
  */
__unused static inline void set_current(struct k_thread *new_thread)
{
	z_thread_mark_switched_out();          /* 记录当前线程结束本轮运行的跟踪/统计状态。 */
	_current_cpu->current = new_thread;    /* 更新当前CPU的线程对象指针。 */
}

#ifdef CONFIG_USE_SWITCH

/**
  * @brief  保存被中断上下文句柄并取得下一线程可恢复的上下文。
  * @param  interrupted：[输入] 架构保存的被中断上下文句柄，空值约定由底层切换实现决定。
  * @retval 下一线程的switch_handle，供架构切换代码使用。
  * @note   仅CONFIG_USE_SWITCH编译；SMP锁内同步队列、栈一致性和句柄所有权，活动线程句柄保持NULL。
  */
void *z_get_next_switch_handle(void *interrupted)
{
	z_check_stack_sentinel();                                                   /* 上下文切换前检查可选栈哨兵，及时发现越界破坏。 */

#ifdef CONFIG_SMP
	void *ret = NULL;                                                           /* 保存将交给架构恢复的下一上下文句柄。 */

	LOCKED(&sched_spinlock) {                                                   /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		struct k_thread *old_thread = _current, *new_thread;                /* 保留旧线程并为下一候选预留指针。 */

		if (IS_ENABLED(CONFIG_SMP)) {                                       /* SMP需要用句柄NULL表示目标正在CPU上活动。 */
			old_thread->switch_handle = NULL;                           /* 旧线程尚在运行，暂不允许其他CPU切入其上下文。 */
		}
		new_thread = next_up();                                             /* 在锁内选择可由本CPU运行的下一线程。 */

		if (old_thread != new_thread) {                                     /* 仅在真正换线程时处理栈一致性、时间片及队列交接。 */
			update_metairq_preempt(new_thread);                         /* 更新特殊优先级介入协作线程的恢复关系。 */
			wait_for_switch(new_thread);                                /* 等待新线程的上下文可安全被切入，细节由切换层保证。 */
			arch_cohere_stacks(old_thread, interrupted, new_thread);    /* 按架构要求处理旧/新线程栈的内存一致性。 */

#ifdef CONFIG_TIMESLICING
			z_reset_time_slice();                                       /* 按最新时间片配置或新线程重新开始本CPU预算。 */
#endif
			_current_cpu->swap_ok = 0;                                  /* 清除本次之前残留的主动切换许可。 */
			set_current(new_thread);                                    /* 记录换出后把当前CPU线程指针切到新对象。 */

#ifdef CONFIG_SPIN_VALIDATE
			/* _current已经改变，需同步锁验证所有者记录，避免新线程释放调度锁被误报为错误所有者。 */
			z_spin_lock_set_owner(&sched_spinlock);                     /* 当前线程已变更，更新锁验证所有者以匹配后续解锁。 */
#endif

			/* 旧线程仍可运行时在此重新入队，并与下方保存switch_handle保持原子；此时已确定不会直接返回旧上下文。 */
			if (z_is_thread_queued(old_thread)) {                       /* 旧线程仍具就绪资格时才重新入运行队列。 */

				/* SMP切换已确定换出旧线程，在锁内重新登记其可运行队列节点。 */
				_priq_run_add(&_kernel.ready_q.runq,
					      old_thread);
			}
		}
		old_thread->switch_handle = interrupted;                            /* 保存旧线程完整上下文句柄，供未来恢复。 */
		ret = new_thread->switch_handle;                                    /* 取得下一线程已经保存的恢复句柄。 */
		if (IS_ENABLED(CONFIG_SMP)) {                                       /* SMP需要用句柄NULL表示目标正在CPU上活动。 */
			/* 正在运行的线程必须把switch_handle清空，其他CPU不能把它视为可切入上下文。 */
			new_thread->switch_handle = NULL;                           /* 新线程即将活动，撤销可供其他CPU切入的句柄。 */
		}
	}
	return ret;                                                                 /* 返回下一线程保存好的上下文句柄。 */
#else
	_current->switch_handle = interrupted;                                      /* 单核保存当前线程的被中断上下文。 */
	set_current(_kernel.ready_q.cache);                                         /* 单核直接切换当前指针到已缓存候选。 */
	return _current->switch_handle;                                             /* 把新当前线程的保存句柄交给架构恢复。 */
#endif
}
#endif

/**
  * @brief  从双向链表优先级队列移除线程节点。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁且线程已在队列；pq参数不被本实现访问，idle不能移除。
  */
void z_priq_dumb_remove(sys_dlist_t *pq, struct k_thread *thread)
{
	__ASSERT_NO_MSG(!z_is_idle_thread_object(thread));    /* idle由每CPU单独保存，不允许作为普通队列节点操作。 */

	sys_dlist_remove(&thread->base.qnode_dlist);          /* 摘除线程的链表队列节点，不释放线程内存。 */
}

/**
  * @brief  从有序双向链表读取最高优先级线程。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @retval 队首宿主线程地址；空表返回NULL。
  * @note   不移除节点，调用者保证队列同步。
  */
struct k_thread *z_priq_dumb_best(sys_dlist_t *pq)
{
	struct k_thread *thread = NULL;                                         /* 默认无候选/无等待者，只有找到节点时赋有效线程。 */
	sys_dnode_t *n = sys_dlist_peek_head(pq);                               /* 有序链表的首节点代表最高有效优先级。 */

	if (n != NULL) {                                                        /* 队列非空时才由节点还原宿主线程。 */
		thread = CONTAINER_OF(n, struct k_thread, base.qnode_dlist);    /* 从内嵌链表节点反推k_thread对象。 */
	}
	return thread;                                                          /* 返回有序链表队首线程，空队列为NULL。 */
}

/**
  * @brief  按有效优先级与入队序号比较两个线程红黑树节点。
  * @param  a：[输入] 嵌入线程对象的有效红黑树节点，不可为空。
  * @param  b：[输入] 嵌入另一个线程对象的有效红黑树节点，不可为空。
  * @retval true：a应排在b前；false：否则。
  * @note   树操作回调，节点必须嵌入有效k_thread；同优先级按order_key保持先入先出。
  */
bool z_priq_rb_lessthan(struct rbnode *a, struct rbnode *b)
{
	struct k_thread *thread_a, *thread_b;                          /* 保存两个树节点所属线程，供优先级比较。 */
	int32_t cmp;                                                   /* 保存只需检查符号的有效优先级比较结果。 */

	thread_a = CONTAINER_OF(a, struct k_thread, base.qnode_rb);    /* 还原第一个红黑树节点所属线程。 */
	thread_b = CONTAINER_OF(b, struct k_thread, base.qnode_rb);    /* 还原第二个红黑树节点所属线程。 */

	cmp = z_sched_prio_cmp(thread_a, thread_b);                    /* 先比较基础优先级和可选截止时间。 */

	if (cmp > 0) {                                                 /* a所属线程更优先，应排在b前。 */
		return true;                                           /* a线程有效优先级更高，红黑树把a排在b前。 */
	} else if (cmp < 0) {                                          /* a所属线程优先级较低，应排在b后。 */
		return false;                                          /* a线程有效优先级较低，不应排在b前。 */
	} else {
		/* 有效优先级相同时按入队序号升序维持FIFO。 */
		return thread_a->base.order_key < thread_b->base.order_key
			? 1 : 0;
	}
}

/**
  * @brief  赋予入队顺序键后将线程加入优先级红黑树。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁，顺序键回绕会遍历整树重新编号，可能增加延迟。
  */
void z_priq_rb_add(struct _priq_rb *pq, struct k_thread *thread)
{
	struct k_thread *t;                                             /* 遍历已有优先级队列的线程对象。 */

	__ASSERT_NO_MSG(!z_is_idle_thread_object(thread));              /* idle由每CPU单独保存，不允许作为普通队列节点操作。 */

	thread->base.order_key = pq->next_order_key++;                  /* 为本次入队分配顺序键，再递增下次键。 */

	/* 入队顺序键回绕时遍历整树重新编号。通常很少触发，但长期不清空且线程很多的队列可能出现明显延迟。 */
	if (!pq->next_order_key) {                                      /* 键递增回绕到0时重编号，避免新键破坏FIFO次序。 */
		RB_FOR_EACH_CONTAINER(&pq->tree, t, base.qnode_rb) {    /* 按当前树序依次遍历已有线程重建顺序键。 */
			t->base.order_key = pq->next_order_key++;       /* 保留现有排序，依次重新分配从0开始的入队键。 */
		}
	}

	rb_insert(&pq->tree, &thread->base.qnode_rb);                   /* 按比较回调把线程树节点插入优先级红黑树。 */
}

/**
  * @brief  从优先级红黑树摘除线程并在树空时复位顺序键。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁且线程在树内，idle不作为普通节点。
  */
void z_priq_rb_remove(struct _priq_rb *pq, struct k_thread *thread)
{
	__ASSERT_NO_MSG(!z_is_idle_thread_object(thread));    /* idle由每CPU单独保存，不允许作为普通队列节点操作。 */

	rb_remove(&pq->tree, &thread->base.qnode_rb);         /* 从红黑树摘除线程节点，不释放对象。 */

	if (!pq->tree.root) {                                 /* 队列已完全为空时可从头使用入队键空间。 */
		pq->next_order_key = 0;                       /* 空树复位下次入队序号。 */
	}
}

/**
  * @brief  读取优先级红黑树的最小排序节点对应线程。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @retval 最优线程地址；空树返回NULL。
  * @note   只查询不移除，调用者持队列保护锁。
  */
struct k_thread *z_priq_rb_best(struct _priq_rb *pq)
{
	struct k_thread *thread = NULL;                                      /* 默认无候选/无等待者，只有找到节点时赋有效线程。 */
	struct rbnode *n = rb_get_min(&pq->tree);                            /* 按树比较规则取最优节点，空树为NULL。 */

	if (n != NULL) {                                                     /* 队列非空时才由节点还原宿主线程。 */
		thread = CONTAINER_OF(n, struct k_thread, base.qnode_rb);    /* 由内嵌树节点还原目标线程。 */
	}
	return thread;                                                       /* 返回树的最优线程，空树为NULL。 */
}

#ifdef CONFIG_SCHED_MULTIQ
# if (K_LOWEST_THREAD_PRIO - K_HIGHEST_THREAD_PRIO) > 31
# error Too many priorities for multiqueue scheduler (max 32)
# endif
#endif

/**
  * @brief  把线程加入对应优先级子队列并置位非空位图。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁，优先级索引必须合法；同级通过尾插保持FIFO。
  */
ALWAYS_INLINE void z_priq_mq_add(struct _priq_mq *pq, struct k_thread *thread)
{
	int priority_bit = thread->base.prio - K_HIGHEST_THREAD_PRIO;              /* 把线程优先级平移成0起始子队列/位图索引。 */

	sys_dlist_append(&pq->queues[priority_bit], &thread->base.qnode_dlist);    /* 线程尾插同优先级子队列，实现同级FIFO。 */
	pq->bitmask |= BIT(priority_bit);                                          /* 标记此优先级至少有一个就绪线程。 */
}

/**
  * @brief  从优先级子队列移除线程，空队列清对应位图。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者持调度锁，线程须已在对应子队列。
  */
ALWAYS_INLINE void z_priq_mq_remove(struct _priq_mq *pq, struct k_thread *thread)
{
	int priority_bit = thread->base.prio - K_HIGHEST_THREAD_PRIO;    /* 把线程优先级平移成0起始子队列/位图索引。 */

	sys_dlist_remove(&thread->base.qnode_dlist);                     /* 摘除线程的链表队列节点，不释放线程内存。 */
	if (sys_dlist_is_empty(&pq->queues[priority_bit])) {             /* 摘除后该优先级无其他线程时撤销位图标记。 */
		pq->bitmask &= ~BIT(priority_bit);                       /* 清除空子队列对应位，避免下次错误选择。 */
	}
}

/**
  * @brief  由最低置位优先级位图取得最优子队列队首。
  * @param  pq：[输入输出] 已初始化的对应后端优先级队列，不可为空。
  * @retval 最高优先级线程地址；所有队列为空则NULL。
  * @note   先排除零位图再用ctz，避免零参数未定义行为。
  */
struct k_thread *z_priq_mq_best(struct _priq_mq *pq)
{
	if (!pq->bitmask) {                                                     /* 没有非空子队列，且不能对0调用ctz。 */
		return NULL;                                                    /* 位图没有任何就绪项。 */
	}

	struct k_thread *thread = NULL;                                         /* 默认无候选/无等待者，只有找到节点时赋有效线程。 */
	sys_dlist_t *l = &pq->queues[__builtin_ctz(pq->bitmask)];               /* 最低置位对应数值最小、最优先的非空子队列。 */
	sys_dnode_t *n = sys_dlist_peek_head(l);                                /* 取同级最先排队的线程节点。 */

	if (n != NULL) {                                                        /* 队列非空时才由节点还原宿主线程。 */
		thread = CONTAINER_OF(n, struct k_thread, base.qnode_dlist);    /* 从内嵌链表节点反推k_thread对象。 */
	}
	return thread;                                                          /* 返回最高优先级子队列的首线程，异常空节点时仍为NULL。 */
}

/**
  * @brief  取消队列中所有线程的等待和超时并使其就绪。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 1：至少处理一个等待线程；0：原队列为空。
  * @note   队列访问的外层对象同步由调用者提供；不直接设置每个线程的切换返回值。
  */
int z_unpend_all(_wait_q_t *wait_q)
{
	int need_sched = 0;                                  /* 默认队列为空，不需因本次唤醒重新调度。 */
	struct k_thread *thread;                             /* 暂存当前遍历、选择或唤醒的线程对象。 */

	while ((thread = z_waitq_head(wait_q)) != NULL) {    /* 逐个取等待队首直到空，整个循环的对象同步由调用者保证。 */
		z_unpend_thread(thread);                     /* 解除对象等待并取消该线程的超时。 */
		z_ready_thread(thread);                      /* 在调度锁保护下检查并建立就绪资格。 */
		need_sched = 1;                              /* 至少有线程被处理，通知调用者考虑调度。 */
	}

	return need_sched;                                   /* 非0表示至少处理过一个等待者，需由上层考虑调度。 */
}

/**
  * @brief  按构建配置初始化运行队列及默认时间片参数。
  * @param  无。
  * @retval 无。
  * @note   系统启动阶段调用；链表、红黑树和多队列后端互为配置分支。
  */
void z_sched_init(void)
{
#ifdef CONFIG_SCHED_DUMB
	sys_dlist_init(&_kernel.ready_q.runq);                                 /* 链表后端建立空运行队列。 */
#endif

#ifdef CONFIG_SCHED_SCALABLE

	/* 红黑树后端建立空树并绑定有效优先级/入队序号比较器。 */
	_kernel.ready_q.runq = (struct _priq_rb) {
		.tree = {
			.lessthan_fn = z_priq_rb_lessthan,
		}
	};
#endif

#ifdef CONFIG_SCHED_MULTIQ
	for (int i = 0; i < ARRAY_SIZE(_kernel.ready_q.runq.queues); i++) {    /* 多队列后端逐个初始化所有优先级子队列。 */
		sys_dlist_init(&_kernel.ready_q.runq.queues[i]);               /* 建立本优先级的空链表。 */
	}
#endif

#ifdef CONFIG_TIMESLICING

	/* 把构建配置的毫秒时间片和门限装入运行期调度器状态。 */
	k_sched_time_slice_set(CONFIG_TIMESLICE_SIZE,
		CONFIG_TIMESLICE_PRIORITY);
#endif
}

/**
  * @brief  读取线程的基础优先级数值。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 当前base.prio，数值越小优先级越高。
  * @note   瞬时读取，不保证返回后优先级不变化。
  */
int z_impl_k_thread_priority_get(k_tid_t thread)
{
	return thread->base.prio;    /* 读取瞬时基础优先级，不加对象锁。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：读取线程的基础优先级数值。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 当前base.prio，数值越小优先级越高。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline int z_vrfy_k_thread_priority_get(k_tid_t thread)
{
	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));    /* 验证用户线程对象的类型、初始化及访问权限，失败触发系统调用异常。 */
	return z_impl_k_thread_priority_get(thread);    /* 用户态对象验证后沿用基础优先级读取实现。 */
}
#include <syscalls/k_thread_priority_get_mrsh.c>
#endif

/**
  * @brief  检查调用上下文和合法优先级后更新目标线程。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval 无。
  * @note   不能从ISR调用；不允许修改idle为普通应用优先级。
  */
void z_impl_k_thread_priority_set(k_tid_t thread, int prio)
{
	/* 未保存线程入口地址，故用NULL验证普通应用优先级；idle入口不能据此改变为其他优先级。 */
	Z_ASSERT_VALID_PRIO(prio, NULL);                    /* 检查应用可用优先级范围，不允许按idle入口例外。 */
	__ASSERT(!arch_is_in_isr(), "");                    /* 睡眠、yield或调度锁接口必须在任务上下文执行。 */

	struct k_thread *th = (struct k_thread *)thread;    /* 把线程句柄视为真实线程对象地址。 */

	z_thread_priority_set(th, prio);                    /* 更新优先级、重新排队并按需调度。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：检查调用上下文和合法优先级后更新目标线程。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval 无。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。 另外只允许把优先级数值增大或不变，即不能由用户态提高优先级。
  */
static inline void z_vrfy_k_thread_priority_set(k_tid_t thread, int prio)
{
	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));    /* 验证用户线程对象的类型、初始化及访问权限，失败触发系统调用异常。 */

	/* 用户态先验证普通应用优先级范围，不能利用idle入口例外。 */
	Z_OOPS(Z_SYSCALL_VERIFY_MSG(_is_valid_prio(prio, NULL),
				    "invalid thread priority %d", prio));

	/* 用户态只允许降低或保持优先级，数值变小意味着提高优先级，必须拒绝。 */
	Z_OOPS(Z_SYSCALL_VERIFY_MSG((int8_t)prio >= thread->base.prio,
				    "thread priority may only be downgraded (%d < %d)",
				    prio, thread->base.prio));

	z_impl_k_thread_priority_set(thread, prio);     /* 验证合法范围和只能降级后更新线程优先级。 */
}
#include <syscalls/k_thread_priority_set_mrsh.c>
#endif

#ifdef CONFIG_SCHED_DEADLINE

/**
  * @brief  按当前cycle加相对预算设置线程截止排序键。
  * @param  tid：[输入输出] 非空有效目标线程句柄。
  * @param  deadline：[输入] 相对截止距离，单位硬件cycle；须为正且队列截止跨度小于2^31周期。
  * @retval 无。
  * @note   仅CONFIG_SCHED_DEADLINE编译；同基础优先级的辅助排序，不是硬件到期通知，也不保证实时截止。
  */
void z_impl_k_thread_deadline_set(k_tid_t tid, int deadline)
{
	struct k_thread *thread = tid;                                       /* 线程句柄本身就是控制块指针。 */

	LOCKED(&sched_spinlock) {                                            /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		thread->base.prio_deadline = k_cycle_get_32() + deadline;    /* 把相对cycle预算转换为32位绝对截止排序键。 */
		if (z_is_thread_queued(thread)) {                            /* 目标标记为已排队时才摘除其就绪节点。 */
			dequeue_thread(&_kernel.ready_q.runq, thread);       /* 从运行队列摘除目标线程，或同步其SMP排队标记。 */
			queue_thread(&_kernel.ready_q.runq, thread);         /* 按当前优先级把线程加入运行队列末尾。 */
		}
	}
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：按当前cycle加相对预算设置线程截止排序键。
  * @param  tid：[输入输出] 非空有效目标线程句柄。
  * @param  deadline：[输入] 相对截止距离，单位硬件cycle；须为正且队列截止跨度小于2^31周期。
  * @retval 无。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。 明确要求deadline>0。
  */
static inline void z_vrfy_k_thread_deadline_set(k_tid_t tid, int deadline)
{
	struct k_thread *thread = tid;                              /* 线程句柄本身就是控制块指针。 */

	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));                /* 验证用户线程对象的类型、初始化及访问权限，失败触发系统调用异常。 */

	/* 用户态截止预算必须为正，不能用零/负距离构造非法排序键。 */
	Z_OOPS(Z_SYSCALL_VERIFY_MSG(deadline > 0,
				    "invalid thread deadline %d",
				    (int)deadline));

	z_impl_k_thread_deadline_set((k_tid_t)thread, deadline);    /* 用户对象与正预算验证后写截止键。 */
}
#include <syscalls/k_thread_deadline_set_mrsh.c>
#endif
#endif

/**
  * @brief  将当前线程排到同优先级末尾并主动重新选择运行者。
  * @param  无。
  * @retval 无。
  * @note   线程上下文调用；没有更合适候选时可继续执行当前线程，不保证其他线程必运行。
  */
void z_impl_k_yield(void)
{
	__ASSERT(!arch_is_in_isr(), "");                        /* 睡眠、yield或调度锁接口必须在任务上下文执行。 */

	SYS_PORT_TRACING_FUNC(k_thread, yield);                 /* 向可选跟踪后端记录yield事件及本次操作的参数/状态。 */

	k_spinlock_key_t key = k_spin_lock(&sched_spinlock);    /* 锁住调度状态并保存进入前的本CPU中断状态。 */

	/* yield时单核当前线程在队列中，SMP仅在带QUEUED标记时执行出队，随后统一尾插。 */
	if (!IS_ENABLED(CONFIG_SMP) ||
	    z_is_thread_queued(_current)) {
		/* 将当前线程从原位置摘下，后续尾插形成同级轮转。 */
		dequeue_thread(&_kernel.ready_q.runq,
			       _current);
	}
	queue_thread(&_kernel.ready_q.runq, _current);          /* 把当前线程尾插到同优先级队列。 */
	update_cache(1);                                        /* 优先级变化允许重新评估当前协作执行选择。 */
	z_swap(&sched_spinlock, key);                           /* 交接调度锁，保存当前上下文并切到已选线程。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：将当前线程排到同优先级末尾并主动重新选择运行者。
  * @param  无。
  * @retval 无。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline void z_vrfy_k_yield(void)
{
	z_impl_k_yield();    /* 用户态转接主动让出实现。 */
}
#include <syscalls/k_yield_mrsh.c>
#endif

/**
  * @brief  以tick预算将当前线程暂停，并返回被提前唤醒时剩余量。
  * @param  ticks：[输入] tick预算或支持的绝对编码；0表示yield。
  * @retval 0：预算经过或零预算yield；正值：计算的剩余tick。
  * @note   不在ISR；预期唤醒时刻使用低32位tick，长等待/回绕受原实现计算范围限制。无多线程分支直接0。
  */
static int32_t z_tick_sleep(k_ticks_t ticks)
{
#ifdef CONFIG_MULTITHREADING
	uint32_t expected_wakeup_ticks;                                        /* 保存预计唤醒时刻的低32位tick，用于计算提前唤醒余量。 */

	__ASSERT(!arch_is_in_isr(), "");                                       /* 睡眠、yield或调度锁接口必须在任务上下文执行。 */

#ifndef CONFIG_TIMEOUT_64BIT
	/* 旧日志子系统不能处理64位值，此日志只在32位超时配置编译。
	 * 原问题记录：https://github.com/zephyrproject-rtos/zephyr/issues/26246 */
	LOG_DBG("thread %p for %u ticks", _current, ticks);
#endif

	/* 零tick睡眠等价主动yield，不进入超时队列。 */
	if (ticks == 0) {                                                      /* 零预算不登记超时，直接主动yield。 */
		k_yield();                                                     /* 让同优先级其他就绪线程有机会运行。 */
		return 0;                                                      /* 零预算yield、等待预算已过或未启用多线程，均无剩余tick。 */
	}

	k_timeout_t timeout = Z_TIMEOUT_TICKS(ticks);                          /* 将tick预算编码包装为内核超时对象。 */
	if (Z_TICK_ABS(ticks) <= 0) {                                          /* 沿用原编码判定，把非绝对正截止请求按相对预算处理。 */
		expected_wakeup_ticks = ticks + sys_clock_tick_get_32();       /* 预计低32位截止时间为当前tick加相对预算。 */
	} else {
		expected_wakeup_ticks = Z_TICK_ABS(ticks);                     /* 绝对请求直接解码为预计截止tick，保留低32位。 */
	}

	k_spinlock_key_t key = k_spin_lock(&sched_spinlock);                   /* 锁住调度状态并保存进入前的本CPU中断状态。 */

#if defined(CONFIG_TIMESLICING) && defined(CONFIG_SWAP_NONATOMIC)
	pending_current = _current;                                            /* 记录即将换出的线程，避免非原子切换窗口被时间片重复处理。 */
#endif
	unready_thread(_current);                                              /* 先从运行队列撤下当前线程，避免睡眠期间仍被选择。 */
	z_add_thread_timeout(_current, timeout);                               /* 为当前线程注册到期恢复事件。 */
	z_mark_thread_as_suspended(_current);                                  /* 以挂起标志阻止其在睡眠期间运行。 */

	(void)z_swap(&sched_spinlock, key);                                    /* 切出并交接锁，直到超时或显式唤醒恢复执行。 */

	__ASSERT(!z_is_thread_state_set(_current, _THREAD_SUSPENDED), "");     /* 睡眠返回运行后必须已经解除挂起标志。 */

	ticks = (k_ticks_t)expected_wakeup_ticks - sys_clock_tick_get_32();    /* 按原低32位截止公式计算尚未睡满的tick。 */
	if (ticks > 0) {                                                       /* 若在预计截止前恢复，向上层返回剩余预算。 */
		return ticks;                                                  /* 提前唤醒时返回预计截止前剩余的tick数。 */
	}
#endif

	return 0;                                                              /* 零预算yield、等待预算已过或未启用多线程，均无剩余tick。 */
}

/**
  * @brief  让当前线程按超时对象睡眠，并把剩余量转换为毫秒。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 常规预算到期为0，提前唤醒为剩余毫秒向下取整；K_FOREVER恢复后返回K_TICKS_FOREVER的int32_t值。
  * @note   线程上下文；K_FOREVER通过suspend实现，保留本实现与公共文档可能不同的无限等待返回值。
  */
int32_t z_impl_k_sleep(k_timeout_t timeout)
{
	k_ticks_t ticks;                                                                            /* 保存解码的等待预算或提前唤醒后的剩余tick。 */

	__ASSERT(!arch_is_in_isr(), "");                                                            /* 睡眠、yield或调度锁接口必须在任务上下文执行。 */

	SYS_PORT_TRACING_FUNC_ENTER(k_thread, sleep, timeout);                                      /* 向可选跟踪后端记录sleep事件及本次操作的参数/状态。 */

	/* K_FOREVER不建立有限超时，改用线程挂起。 */
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {                                                     /* 无限睡眠不创建定时事件，交给挂起/恢复机制。 */
		k_thread_suspend(_current);                                                         /* 挂起当前线程直到其他路径恢复它。 */

		SYS_PORT_TRACING_FUNC_EXIT(k_thread, sleep, timeout, (int32_t) K_TICKS_FOREVER);    /* 向可选跟踪后端记录sleep事件及本次操作的参数/状态。 */

		return (int32_t) K_TICKS_FOREVER;                                                   /* 保留无限睡眠路径的特殊返回值，非普通剩余毫秒数。 */
	}

	ticks = timeout.ticks;                                                                      /* 提取内部tick预算编码。 */

	ticks = z_tick_sleep(ticks);                                                                /* 完成实际挂起并接收提前唤醒剩余tick。 */

	int32_t ret = k_ticks_to_ms_floor64(ticks);                                                 /* 把剩余tick向下换算成毫秒，返回值范围仍为int32_t。 */

	SYS_PORT_TRACING_FUNC_EXIT(k_thread, sleep, timeout, ret);                                  /* 向可选跟踪后端记录sleep事件及本次操作的参数/状态。 */

	return ret;                                                                                 /* 返回本实现换算出的剩余毫秒数。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：让当前线程按超时对象睡眠，并把剩余量转换为毫秒。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 常规预算到期为0，提前唤醒为剩余毫秒向下取整；K_FOREVER恢复后返回K_TICKS_FOREVER的int32_t值。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline int32_t z_vrfy_k_sleep(k_timeout_t timeout)
{
	return z_impl_k_sleep(timeout);    /* 用户态请求沿用同一睡眠和剩余毫秒语义。 */
}
#include <syscalls/k_sleep_mrsh.c>
#endif

/**
  * @brief  把微秒请求向上换算tick后挂起当前线程。
  * @param  us：[输入] 希望睡眠的时长，单位微秒；非负，实际精度受tick约束。
  * @retval 到期0，提前唤醒时为剩余微秒向下换算的int32_t值。
  * @note   线程上下文，精度受tick限制，不能把微秒参数当成微秒级硬件唤醒保证。
  */
int32_t z_impl_k_usleep(int us)
{
	int32_t ticks;                                                                     /* 保存微秒预算换算的tick和提前唤醒后的剩余tick。 */

	SYS_PORT_TRACING_FUNC_ENTER(k_thread, usleep, us);                                 /* 向可选跟踪后端记录usleep事件及本次操作的参数/状态。 */

	ticks = k_us_to_ticks_ceil64(us);                                                  /* 微秒向上换算tick，再按原实现存入32位变量。 */
	ticks = z_tick_sleep(ticks);                                                       /* 完成实际挂起并接收提前唤醒剩余tick。 */

	SYS_PORT_TRACING_FUNC_EXIT(k_thread, usleep, us, k_ticks_to_us_floor64(ticks));    /* 向可选跟踪后端记录usleep事件及本次操作的参数/状态。 */

	return k_ticks_to_us_floor64(ticks);                                               /* 将剩余tick向下换算微秒并按返回类型截为int32_t。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：把微秒请求向上换算tick后挂起当前线程。
  * @param  us：[输入] 希望睡眠的时长，单位微秒；非负，实际精度受tick约束。
  * @retval 到期0，提前唤醒时为剩余微秒向下换算的int32_t值。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline int32_t z_vrfy_k_usleep(int us)
{
	return z_impl_k_usleep(us);    /* 用户态转接微秒参数睡眠，仍受tick粒度限制。 */
}
#include <syscalls/k_usleep_mrsh.c>
#endif

/**
  * @brief  取消普通睡眠的超时并清挂起状态，使线程重新可运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   对象等待中的PENDING线程不在此唤醒；ISR只更新状态，线程上下文随后重调度。
  */
void z_impl_k_wakeup(k_tid_t thread)
{
	SYS_PORT_TRACING_OBJ_FUNC(k_thread, wakeup, thread);             /* 向可选跟踪后端记录wakeup事件及本次操作的参数/状态。 */

	if (z_is_thread_pending(thread)) {                               /* 对象等待不能用普通sleep唤醒接口打断，此路径直接忽略。 */
		return;                                                  /* 线程不属于本接口可恢复的普通睡眠，保持其状态。 */
	}

	if (z_abort_thread_timeout(thread) < 0) {                        /* 没有活动超时时再检查是否属于无限睡眠。 */
		/* 取消超时失败也可能是无限睡眠，需检查是否只带SUSPENDED标志。 */
		if (thread->base.thread_state != _THREAD_SUSPENDED) {    /* 只有纯挂起状态才按无限睡眠恢复，其他状态直接保持。 */
			return;                                          /* 线程不属于本接口可恢复的普通睡眠，保持其状态。 */
		}
	}

	z_mark_thread_as_not_suspended(thread);                          /* 清SUSPENDED，其他等待或超时条件仍需满足才就绪。 */
	z_ready_thread(thread);                                          /* 在调度锁保护下检查并建立就绪资格。 */

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_IPI_SUPPORTED)
	arch_sched_ipi();                                                /* 通知其他CPU重新评估可运行线程，实际切换由各CPU完成。 */
#endif

	if (!arch_is_in_isr()) {                                         /* ISR中的切换留待中断退出，线程上下文可立即重调度。 */
		z_reschedule_unlocked();                                 /* 当前未持自旋锁，重新屏蔽中断后评估线程切换。 */
	}
}

#ifdef CONFIG_TRACE_SCHED_IPI

/**
  * @brief  记录本CPU收到的调度IPI跟踪事件。
  * @param  无。
  * @retval 无。
  * @note   仅CONFIG_TRACE_SCHED_IPI声明，供跟踪后端实现。
  */
extern void z_trace_sched_ipi(void);
#endif

#ifdef CONFIG_SMP

/**
  * @brief  处理跨CPU调度通知的可选跟踪入口。
  * @param  无。
  * @retval 无。
  * @note   仅SMP编译，当前主体只在启用跟踪时调用z_trace_sched_ipi。
  */
void z_sched_ipi(void)
{
	/* 若在此增加逻辑，也须保证不支持调度IPI的平台在适当位置调用对应逻辑。 */
#ifdef CONFIG_TRACE_SCHED_IPI
	z_trace_sched_ipi();    /* 把本CPU收到的调度通知交给可选跟踪后端。 */
#endif
}
#endif

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：取消普通睡眠的超时并清挂起状态，使线程重新可运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline void z_vrfy_k_wakeup(k_tid_t thread)
{
	Z_OOPS(Z_SYSCALL_OBJ(thread, K_OBJ_THREAD));    /* 验证用户线程对象的类型、初始化及访问权限，失败触发系统调用异常。 */
	z_impl_k_wakeup(thread);                        /* 验证用户对象后执行普通睡眠唤醒。 */
}
#include <syscalls/k_wakeup_mrsh.c>
#endif

/**
  * @brief  读取本CPU当前线程对象地址。
  * @param  无。
  * @retval 当前k_tid_t线程句柄，不转移所有权。
  * @note   SMP读取期间屏蔽本CPU普通中断以避免抢占迁移带来的不一致。
  */
k_tid_t z_impl_z_current_get(void)
{
#ifdef CONFIG_SMP
	/* SMP读取_current_cpu及current期间可能被抢占迁移，因此暂时锁住本CPU普通中断以取得一致的当前线程地址。 */
	unsigned int k = arch_irq_lock();       /* SMP暂屏蔽本CPU普通中断，避免读取current期间迁移/抢占。 */
#endif

	k_tid_t ret = _current_cpu->current;    /* 采样本CPU当前线程对象指针。 */

#ifdef CONFIG_SMP
	arch_irq_unlock(k);                     /* 恢复读取当前线程前的本CPU中断状态。 */
#endif
	return ret;                             /* 返回锁内取得的本CPU当前线程地址。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：读取本CPU当前线程对象地址。
  * @param  无。
  * @retval 当前k_tid_t线程句柄，不转移所有权。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline k_tid_t z_vrfy_z_current_get(void)
{
	return z_impl_z_current_get();    /* 用户态转接当前线程查询，无对象参数需要验证。 */
}
#include <syscalls/z_current_get_mrsh.c>
#endif

/**
  * @brief  判断当前执行上下文是否为可抢占线程。
  * @param  无。
  * @retval 非0：不在ISR且线程preempt编码允许；否则0。
  * @note   不会改变调度锁状态。
  */
int z_impl_k_is_preempt_thread(void)
{
	return !arch_is_in_isr() && is_preempt(_current);    /* 必须在任务上下文且preempt编码允许才报告可抢占线程。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  用户态验证/转接：判断当前执行上下文是否为可抢占线程。
  * @param  无。
  * @retval 非0：不在ISR且线程preempt编码允许；否则0。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。
  */
static inline int z_vrfy_k_is_preempt_thread(void)
{
	return z_impl_k_is_preempt_thread();    /* 用户态复用相同上下文判定。 */
}
#include <syscalls/k_is_preempt_thread_mrsh.c>
#endif

#ifdef CONFIG_SCHED_CPU_MASK
# ifdef CONFIG_SMP
/* CPU亲和掩码目前只有一个字节，最多表示8个CPU。 */
BUILD_ASSERT(CONFIG_MP_NUM_CPUS <= 8, "Too many CPUs for mask word");
# endif


/**
  * @brief  在目标不能运行时修改其CPU亲和掩码。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  enable_mask：[输入] 要允许的CPU位掩码，第n位对应CPU n。
  * @param  disable_mask：[输入] 要禁止的CPU位掩码，最后清位，优先于enable_mask。
  * @retval 0：更新完成；-EINVAL：线程仍可运行，拒绝修改。
  * @note   仅CONFIG_SCHED_CPU_MASK编译；不验证传入CPU位范围，enable与disable顺序为先置位后清位。
  */
static int cpu_mask_mod(k_tid_t thread, uint32_t enable_mask, uint32_t disable_mask)
{
	int ret = 0;                                                 /* 初始化成功状态，具体拒绝/死锁/等待路径再更新。 */

	LOCKED(&sched_spinlock) {                                    /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		if (z_is_thread_prevented_from_running(thread)) {    /* 只允许目标不能运行时修改CPU亲和，以免与执行位置竞态。 */
			thread->base.cpu_mask |= enable_mask;        /* 先允许enable_mask指定的CPU。 */
			thread->base.cpu_mask  &= ~disable_mask;     /* 再禁用disable_mask指定CPU，清位优先于同位使能。 */
		} else {
			ret = -EINVAL;                               /* 目标仍可运行，拒绝修改其CPU亲和掩码。 */
		}
	}
	return ret;                                                  /* 返回更新成功0或目标可运行导致的-EINVAL。 */
}

/**
  * @brief  清除线程允许运行的全部CPU位。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   需先使目标处于不能运行状态，否则无法更改亲和设置。
  */
int k_thread_cpu_mask_clear(k_tid_t thread)
{
	return cpu_mask_mod(thread, 0, 0xffffffff);    /* 清除所有可表示的CPU许可位。 */
}

/**
  * @brief  允许目标线程在掩码可表示的所有CPU运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   只修改亲和掩码，不自动启动/恢复线程。
  */
int k_thread_cpu_mask_enable_all(k_tid_t thread)
{
	return cpu_mask_mod(thread, 0xffffffff, 0);    /* 置所有可表示的CPU许可位。 */
}

/**
  * @brief  允许目标线程在指定CPU运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  cpu：[输入] 合法CPU编号，须处于当前配置及掩码表示范围。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   调用方保证cpu为合法CPU编号和移位范围，本包装不检查。
  */
int k_thread_cpu_mask_enable(k_tid_t thread, int cpu)
{
	return cpu_mask_mod(thread, BIT(cpu), 0);    /* 只设置指定CPU对应许可位。 */
}

/**
  * @brief  禁止目标线程在指定CPU运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  cpu：[输入] 合法CPU编号，须处于当前配置及掩码表示范围。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   调用方保证cpu合法；掩码清空后线程将无可运行CPU。
  */
int k_thread_cpu_mask_disable(k_tid_t thread, int cpu)
{
	return cpu_mask_mod(thread, 0, BIT(cpu));    /* 只清除指定CPU对应许可位。 */
}

#endif    /* CONFIG_SCHED_CPU_MASK */

/**
  * @brief  让join等队列的全部等待者以成功状态就绪。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @retval 无。
  * @note   调用者持sched_spinlock；清等待/超时、写返回0后只排队，不在循环内切换线程。
  */
static inline void unpend_all(_wait_q_t *wait_q)
{
	struct k_thread *thread;                             /* 暂存当前遍历、选择或唤醒的线程对象。 */

	while ((thread = z_waitq_head(wait_q)) != NULL) {    /* 逐个取等待队首直到空，整个循环的对象同步由调用者保证。 */
		unpend_thread_no_timeout(thread);            /* 只摘除对象等待，不在此取消或重建超时事件。 */
		(void)z_abort_thread_timeout(thread);        /* 尝试取消活动超时，未激活状态不作为本路径错误。 */
		arch_thread_return_value_set(thread, 0);     /* join等成功结束等待者恢复后返回0。 */
		ready_thread(thread);                        /* 检查全部就绪条件后入运行队列，不在此直接切换。 */
	}
}

#ifdef CONFIG_CMSIS_RTOS_V1

/**
  * @brief  清理被终止线程的CMSIS v1状态掩码。
  * @param  thread：[输入输出] 已终止的有效线程对象，不可为空。
  * @retval 无。
  * @note   仅CONFIG_CMSIS_RTOS_V1声明，供兼容层实现；不释放线程栈。
  */
extern void z_thread_cmsis_status_mask_clear(struct k_thread *thread);
#endif

/**
  * @brief  在锁内完成线程终止、解除队列及可选权限清理。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   调用者保证目标未在任何CPU执行；不自行free线程控制块或栈，清理join等待者并标记DEAD。
  */
static void end_thread(struct k_thread *thread)
{
	/* 调用方已经持锁，且保证目标线程未在任何CPU继续运行后才可完成终止清理。 */
	if ((thread->base.thread_state & _THREAD_DEAD) == 0U) {           /* 只对尚未完成死亡清理的线程执行一次终止收尾。 */
		thread->base.thread_state |= _THREAD_DEAD;                /* 标记线程永久结束，禁止后续超时重新就绪。 */
		thread->base.thread_state &= ~_THREAD_ABORTING;           /* 跨CPU终止已经完成，清进行中标志供其他CPU结束等待。 */
		if (z_is_thread_queued(thread)) {                         /* 目标标记为已排队时才摘除其就绪节点。 */
			dequeue_thread(&_kernel.ready_q.runq, thread);    /* 从运行队列摘除目标线程，或同步其SMP排队标记。 */
		}
		if (thread->base.pended_on != NULL) {                     /* 仍挂在对象队列上时先完成摘除。 */
			unpend_thread_no_timeout(thread);                 /* 只摘除对象等待，不在此取消或重建超时事件。 */
		}
		(void)z_abort_thread_timeout(thread);                     /* 尝试取消活动超时，未激活状态不作为本路径错误。 */
		unpend_all(&thread->join_queue);                          /* 让等待此线程退出的join调用以成功状态就绪。 */
		update_cache(1);                                          /* 优先级变化允许重新评估当前协作执行选择。 */

		SYS_PORT_TRACING_FUNC(k_thread, sched_abort, thread);     /* 向可选跟踪后端记录sched_abort事件及本次操作的参数/状态。 */

		z_thread_monitor_exit(thread);                            /* 从可选线程监控列表注销目标。 */

#ifdef CONFIG_CMSIS_RTOS_V1
		z_thread_cmsis_status_mask_clear(thread);                 /* 清理CMSIS v1兼容状态记录。 */
#endif

#ifdef CONFIG_USERSPACE
		z_mem_domain_exit_thread(thread);                         /* 用户态配置下解除线程的内存域成员关系。 */
		z_thread_perms_all_clear(thread);                         /* 清除该线程拥有的内核对象权限。 */
		z_object_uninit(thread->stack_obj);                       /* 把栈对象标记为未初始化，不在此释放其存储。 */
		z_object_uninit(thread);                                  /* 把线程对象标记为未初始化，允许受控的后续复用。 */
#endif
	}
}

/**
  * @brief  协调本CPU或远端CPU停止目标并完成终止清理。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无；若终止当前线程且非ISR则正常不应返回该线程。
  * @note   SMP远端线程需IPI协作，中断上下文可能无超时忙等；不负责释放用户持有资源。
  */
void z_thread_abort(struct k_thread *thread)
{
	k_spinlock_key_t key = k_spin_lock(&sched_spinlock);                    /* 锁住调度状态并保存进入前的本CPU中断状态。 */

	if ((thread->base.thread_state & _THREAD_DEAD) != 0U) {                 /* 已经死亡的目标无需再次等待或清理。 */
		k_spin_unlock(&sched_spinlock, key);                            /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
		return;                                                         /* 目标已死亡或远端终止等待分支已交接锁，无需重复清理。 */
	}

#ifdef CONFIG_SMP
	if (is_aborting(thread) && thread == _current && arch_is_in_isr()) {    /* 本CPU中断中发现远端正等自己终止时立即完成清理，避免相互死等。 */
		/* 其他CPU正在等待当前CPU终止，直接完成以避免互相等待死锁。 */
		end_thread(thread);                                             /* 完成目标死亡标记、队列解除、join唤醒及可选权限清理。 */
	}

	bool active = thread_active_elsewhere(thread);                          /* 记录目标是否仍在其他CPU执行。 */

	if (active) {                                                           /* 当前线程仍可运行时与队列最优项比较，不必无条件换走。 */
		/* 目标正在其他CPU运行时先标记ABORTING，再发调度IPI要求对方完成换出。 */
		thread->base.thread_state |= _THREAD_ABORTING;                  /* 请求远端CPU在安全切换点停止目标。 */

#ifdef CONFIG_SCHED_IPI_SUPPORTED
		arch_sched_ipi();                                               /* 通知其他CPU重新评估可运行线程，实际切换由各CPU完成。 */
#endif
	}

	if (is_aborting(thread) && thread != _current) {                        /* 目标终止尚未完成且不是自身时等待其退出。 */
		if (arch_is_in_isr()) {                                         /* 中断不能阻塞到等待队列，采用释放锁后的轮询路径。 */
			/* 中断上下文不能挂起等待，只能先释放锁再忙等其他CPU完成终止；原循环无超时。 */
			k_spin_unlock(&sched_spinlock, key);                    /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
			while (is_aborting(thread)) {                           /* 持续等待远端清除ABORTING，原实现无超时。 */
			}
		} else if (active) {                                            /* 线程上下文且目标确实在远端运行时可睡眠等待join。 */
			/* 线程上下文可进入目标join队列后换出，不需持续占用CPU。 */
			add_to_waitq_locked(_current, &thread->join_queue);     /* 把当前线程挂到目标退出通知队列。 */
			z_swap(&sched_spinlock, key);                           /* 交接调度锁，保存当前上下文并切到已选线程。 */
		}
		return;                                                         /* 前面的分支已经释放或交接锁，不能在公共出口重复解锁。 */
	}
#endif
	end_thread(thread);                                                     /* 完成目标死亡标记、队列解除、join唤醒及可选权限清理。 */
	if (thread == _current && !arch_is_in_isr()) {                          /* 终止任务上下文中的自身时必须换走且不能再返回。 */
		z_swap(&sched_spinlock, key);                                   /* 交接调度锁，保存当前上下文并切到已选线程。 */
		__ASSERT(false, "aborted _current back from dead");
	}
	k_spin_unlock(&sched_spinlock, key);                                    /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
}

#if !defined(CONFIG_ARCH_HAS_THREAD_ABORT)

/**
  * @brief  为通用线程终止入口添加跟踪并调用调度终止。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无；终止当前线程后不应继续执行。
  * @note   仅未提供CONFIG_ARCH_HAS_THREAD_ABORT的架构编译此实现。
  */
void z_impl_k_thread_abort(struct k_thread *thread)
{
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_thread, abort, thread);    /* 向可选跟踪后端记录abort事件及本次操作的参数/状态。 */

	z_thread_abort(thread);                                      /* 执行通用单核/跨CPU协调终止流程。 */

	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_thread, abort, thread);     /* 向可选跟踪后端记录abort事件及本次操作的参数/状态。 */
}
#endif

/**
  * @brief  等待目标线程退出，并处理立即返回或死锁情况。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 0：目标已死亡或等待完成；-EBUSY：不等待；-EDEADLK：自等待或直接互等；超时通常-EAGAIN。
  * @note   ISR仅可走不阻塞路径；在join队列排队后由z_swap交接调度锁。
  */
int z_impl_k_thread_join(struct k_thread *thread, k_timeout_t timeout)
{
	k_spinlock_key_t key = k_spin_lock(&sched_spinlock);                             /* 锁住调度状态并保存进入前的本CPU中断状态。 */
	int ret = 0;                                                                     /* 初始化成功状态，具体拒绝/死锁/等待路径再更新。 */

	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_thread, join, thread, timeout);                /* 向可选跟踪后端记录join事件及本次操作的参数/状态。 */

	if ((thread->base.thread_state & _THREAD_DEAD) != 0U) {                          /* 已经死亡的目标无需再次等待或清理。 */
		ret = 0;                                                                 /* 目标已经退出，本次join立即成功。 */
	} else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {                                   /* 目标尚未退出且调用方不等待，则立即失败。 */
		ret = -EBUSY;                                                            /* 不阻塞等待活动目标。 */

	/* 拒绝当前线程等待自己，以及目标正在等待当前线程退出的直接互等。 */
	} else if ((thread == _current) ||
		   (thread->base.pended_on == &_current->join_queue)) {
		ret = -EDEADLK;                                                          /* 检测到自join或直接互等，拒绝建立死锁等待。 */
	} else {
		__ASSERT(!arch_is_in_isr(), "cannot join in ISR");                       /* 实际等待join不能在中断上下文阻塞。 */
		add_to_waitq_locked(_current, &thread->join_queue);                      /* 把当前线程挂到目标退出通知队列。 */
		add_thread_timeout(_current, timeout);                                   /* 为等待目标退出安排可选超时。 */

		SYS_PORT_TRACING_OBJ_FUNC_BLOCKING(k_thread, join, thread, timeout);     /* 向可选跟踪后端记录join事件及本次操作的参数/状态。 */
		ret = z_swap(&sched_spinlock, key);                                      /* 交接调度锁并阻塞，唤醒后取得成功/超时状态。 */
		SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_thread, join, thread, timeout, ret);    /* 向可选跟踪后端记录join事件及本次操作的参数/状态。 */

		return ret;                                                              /* 返回join成功、立即忙、死锁或实际等待的唤醒/超时状态。 */
	}

	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_thread, join, thread, timeout, ret);            /* 向可选跟踪后端记录join事件及本次操作的参数/状态。 */

	k_spin_unlock(&sched_spinlock, key);                                             /* 本路径不交接到切换函数，显式释放锁并恢复中断状态。 */
	return ret;                                                                      /* 返回join成功、立即忙、死锁或实际等待的唤醒/超时状态。 */
}

#ifdef CONFIG_USERSPACE
/* 线程对象初始化位为false既可能是从未初始化，也可能已运行后退出；对join/abort按已退出处理，返回true使验证包装直接成功。其他对象类型/权限错误仍走系统调用异常。 */

/**
  * @brief  验证用户线程对象，并把未初始化/已退出状态当作无需操作。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval true：验证返回-EINVAL，可直接成功；false：活动有效对象；其他错误进入系统调用异常。
  * @note   仅CONFIG_USERSPACE编译，保留LCOV_EXCL_LINE工具标记。
  */
static bool thread_obj_validate(struct k_thread *thread)
{
	struct z_object *ko = z_object_find(thread);                      /* 在对象元数据表查询用户传入的线程地址。 */
	int ret = z_object_validate(ko, K_OBJ_THREAD, _OBJ_INIT_TRUE);    /* 验证对象类型、权限及活动初始化状态。 */

	switch (ret) {                                                    /* 区分有效活动对象、未初始化/退出对象与其他权限错误。 */
	case 0:                                                           /* 对象有效且仍活动，后续继续真实操作。 */
		return false;                                             /* 用户线程对象有效且活动，需要继续真实操作。 */
	case -EINVAL:                                                     /* 按此兼容接口约定，把未初始化/已退出线程视为无需操作。 */
		return true;                                              /* 对象未初始化或已经退出，包装层可直接成功结束。 */
	default:
#ifdef CONFIG_LOG
		z_dump_object_error(ret, thread, ko, K_OBJ_THREAD);       /* 启用日志时记录对象访问失败原因。 */
#endif
		Z_OOPS(Z_SYSCALL_VERIFY_MSG(ret, "access denied"));       /* 无权访问或类型错误通过系统调用异常路径处理。 */
	}
	CODE_UNREACHABLE;                                                 /* LCOV_EXCL_LINE */
}

/**
  * @brief  用户态验证/转接：等待目标线程退出，并处理立即返回或死锁情况。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 0：目标已死亡或等待完成；-EBUSY：不等待；-EDEADLK：自等待或直接互等；超时通常-EAGAIN。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。 未初始化/已退出对象直接返回0。
  */
static inline int z_vrfy_k_thread_join(struct k_thread *thread,
				       k_timeout_t timeout)
{
	if (thread_obj_validate(thread)) {               /* 未初始化/已退出对象直接结束，不触发普通活动线程操作。 */
		return 0;                                /* 对象未初始化或已退出，按约定join直接成功。 */
	}

	return z_impl_k_thread_join(thread, timeout);    /* 活动对象验证通过后执行有限/无限join等待。 */
}
#include <syscalls/k_thread_join_mrsh.c>

/**
  * @brief  用户态验证/转接：为通用线程终止入口添加跟踪并调用调度终止。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无；终止当前线程后不应继续执行。
  * @note   仅CONFIG_USERSPACE编译；有线程指针时验证对象类型和访问权限，失败进入系统调用异常。 未初始化/退出对象直接返回，拒绝终止K_ESSENTIAL线程。
  */
static inline void z_vrfy_k_thread_abort(k_tid_t thread)
{
	if (thread_obj_validate(thread)) {                   /* 未初始化/已退出对象直接结束，不触发普通活动线程操作。 */
		return;                                      /* 对象未初始化或已退出，无需再次终止。 */
	}

	/* 不能从用户态终止标为K_ESSENTIAL的关键系统线程。 */
	Z_OOPS(Z_SYSCALL_VERIFY_MSG(!(thread->base.user_options & K_ESSENTIAL),
				    "aborting essential thread %p", thread));

	z_impl_k_thread_abort((struct k_thread *)thread);    /* 确认对象权限及非essential后执行终止。 */
}
#include <syscalls/k_thread_abort_mrsh.c>
#endif    /* CONFIG_USERSPACE */

/* 面向IPC适配层的调度等待/唤醒接口，内部仍使用相同等待队列与自旋锁交接。 */

/**
  * @brief  摘下等待队首，设置返回值和数据后使线程就绪。
  * @param  wait_q：[输入输出] 已初始化等待队列；pend系列允许NULL以表示不关联对象队列，其他队列操作须非空。
  * @param  swap_retval：[输入] 交给被唤醒线程的整数返回状态。
  * @param  swap_data：[输入] 交给被唤醒线程的数据指针，可为空；所有权/生命周期由通信双方约定。
  * @retval true：处理一个等待者；false：队列为空。
  * @note   调用者须持外层对象同步锁；内部持sched_spinlock完成状态交接，但不主动执行调度。
  */
bool z_sched_wake(_wait_q_t *wait_q, int swap_retval, void *swap_data)
{
	struct k_thread *thread;                                 /* 暂存当前遍历、选择或唤醒的线程对象。 */
	bool ret = false;                                        /* 默认未找到等待者，成功唤醒后置true。 */

	LOCKED(&sched_spinlock) {                                /* 本作用域内串行修改调度状态，退出由宏恢复锁和中断状态。 */
		thread = _priq_wait_best(&wait_q->waitq);        /* 按等待队列排序取最优等待者，不在这里直接修改其返回值。 */

		if (thread != NULL) {                            /* 存在可处理线程时才摘除等待、赋状态或就绪。 */

			/* 在同一调度锁内先写唤醒状态和附带数据，再摘等待及取消超时，最后排为就绪。 */
			z_thread_return_value_set_with_data(thread,
							    swap_retval,
							    swap_data);
			unpend_thread_no_timeout(thread);        /* 只摘除对象等待，不在此取消或重建超时事件。 */
			(void)z_abort_thread_timeout(thread);    /* 尝试取消活动超时，未激活状态不作为本路径错误。 */
			ready_thread(thread);                    /* 检查全部就绪条件后入运行队列，不在此直接切换。 */
			ret = true;                              /* 已经设置返回数据并使一个等待者就绪。 */
		}
	}

	return ret;                                              /* true表示已处理一个等待者，false表示原队列为空。 */
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
		 _wait_q_t *wait_q, k_timeout_t timeout, void **data)
{
	int ret = z_pend_curr(lock, key, wait_q, timeout);    /* 登记等待并把调用方锁交给换出路径，恢复后取得唤醒状态。 */

	if (data != NULL) {                                   /* 调用者需要附带数据时才写入输出指针。 */
		*data = _current->base.swap_data;    /* 拷贝唤醒者附带的指针，不复制其指向内容也不取得所有权。 */
	}
	return ret;                                           /* 向IPC调用方传播唤醒状态或等待超时状态。 */
}
