/* 线程切换的锁交接层：调用方在对象锁/调度锁内建立等待状态，然后把锁钥匙交给z_swap完成释放与切换。当前ATS未启用CONFIG_USE_SWITCH，实际使用arch_swap及PendSV路径；USE_SWITCH/SMP分支为其他构建保留。 */
/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_KERNEL_INCLUDE_KSWAP_H_
#define ZEPHYR_KERNEL_INCLUDE_KSWAP_H_    /* 保护内联切换实现与声明。 */

#include <ksched.h>
#include <spinlock.h>
#include <kernel_arch_func.h>

#ifdef CONFIG_STACK_SENTINEL

/**
  * @brief  检查当前线程栈底的可选哨兵是否被覆盖。
  * @param  无。
  * @retval 无；检测到破坏时按内核致命错误策略处理。
  * @note   仅CONFIG_STACK_SENTINEL声明；关闭时宏为空操作，不等于已检查栈安全。
  */
extern void z_check_stack_sentinel(void);
#else
#define z_check_stack_sentinel()    /**/    /* 未启用哨兵时为空宏，不执行任何栈访问。 */
#endif

extern struct k_spinlock sched_spinlock;    /* 只声明sched.c定义的全局调度锁，不在此创建另一把锁。 */

/* SMP的irq_lock隐含全局自旋锁，线程切换时隐式释放/重取；无论从z_swap还是
 * 中断/异常返回线程，都须根据目标线程的嵌套计数恢复锁状态，不能只恢复硬件屏蔽位。 */

/**
  * @brief  按目标线程嵌套状态交接SMP隐式全局中断锁。
  * @param  thread：[输入输出] 即将恢复执行的有效线程对象，不可为空。
  * @retval 无。
  * @note   仅SMP切换路径使用；具体实现负责把全局锁状态与线程记录对齐。
  */
void z_smp_release_global_lock(struct k_thread *thread);

/* 根据架构切换接口选择新式arch_switch或旧式arch_swap路径。 */
#ifdef CONFIG_USE_SWITCH

/* SMP线程重新可见于队列时，arch_switch可能尚未完成旧上下文保存，因此必须等到
 * switch_handle非空才可切入。原实现未在此提供通用内存屏障/缓存失效机制；
 * 不能把volatile轮询自动解释为适用于所有未来架构的完整跨CPU发布协议。 */

/**
  * @brief  等待SMP目标线程的上下文句柄被保存完成。
  * @param  thread：[输入] 非空有效目标线程对象。
  * @retval 无。
  * @note   SMP循环每次忙等1微秒，没有总超时；单核为空逻辑。volatile读取不是通用CPU屏障。
  */
static inline void wait_for_switch(struct k_thread *thread)
{
#ifdef CONFIG_SMP
	volatile void **shp = (void *)&thread->switch_handle;    /* 取得待发布上下文句柄的volatile读取视图。 */

	while (*shp == NULL) {                                   /* 对方尚未完成上下文保存则继续等待，原逻辑无超时。 */
		k_busy_wait(1);                                  /* 每轮占用CPU忙等1微秒，减少空转读取频率。 */
	}
#endif
}

/* 新式切换把调度与锁管理放在C层，arch_switch只负责保存/恢复上下文，不理解
 * 调度队列或API返回值。is_spinlock由调用处常量传入，内联后可裁掉无关分支。 */

/**
  * @brief  在C层交接对象锁与调度锁，并执行新式上下文切换。
  * @param  key：[输入] 原本CPU中断恢复钥匙。
  * @param  lock：[输入输出] is_spinlock非0时为当前持有的锁，可为NULL表示仅中断已屏蔽。
  * @param  is_spinlock：[输入] 非0表示架构本地中断锁/自旋锁路径，0表示irq_lock隐含全局锁路径。
  * @retval 当前线程恢复后swap_retval按unsigned int返回；默认写入-EAGAIN的位模式，外层int包装解释为状态。
  * @note   仅CONFIG_USE_SWITCH编译；切换前释放对象锁，保持中断屏蔽直到调度锁交接及上下文恢复完成。
  */
static ALWAYS_INLINE unsigned int do_swap(unsigned int key,
					  struct k_spinlock *lock,
					  int is_spinlock)
{
	ARG_UNUSED(lock);                                                /* 在无需访问锁对象的配置中消除未使用告警。 */
	struct k_thread *new_thread, *old_thread;                        /* 分别保存将切入和将切出的线程对象。 */

#ifdef CONFIG_SPIN_VALIDATE
	/* 正常换出要求钥匙能恢复为未屏蔽，不能破坏调用栈更外层尚持有的锁。
	 * 启动dummy线程与刚被终止的当前线程为例外；ARM64系统调用的屏蔽可能
	 * 不代表锁嵌套，故该架构跳过这项验证，原问题编号#35307。 */
# ifndef CONFIG_ARM64
	__ASSERT(arch_irq_unlocked(key) ||
		 _current->base.thread_state & (_THREAD_DUMMY | _THREAD_DEAD),
		 "Context switching while holding lock!");
# endif
#endif

	old_thread = _current;                                           /* 保存切换前的当前线程。 */

	z_check_stack_sentinel();                                        /* 启用时检查当前线程栈底哨兵，尽早发现越界。 */

	/* 保持本CPU中断屏蔽：先释放外层对象锁的所有权，再取得调度锁，
	 * 队列和上下文切换受调度锁串行保护；直到出口才恢复中断。 */
	if (is_spinlock && lock != NULL && lock != &sched_spinlock) {    /* 传入普通对象锁时交出其所有权，但仍保持本CPU中断屏蔽。 */
		k_spin_release(lock);                                    /* 释放对象锁的所有权/验证记录，不恢复中断。 */
	}
	if (!is_spinlock || lock != &sched_spinlock) {                   /* 调用方尚未持调度锁时在此补取，保护下一线程选择。 */
		(void) k_spin_lock(&sched_spinlock);                     /* 保持现有中断屏蔽的前提下取得调度锁，恢复仍使用原key。 */
	}

	new_thread = z_swap_next_thread();                               /* 由调度器选取下一条允许运行的线程。 */

	if (new_thread != old_thread) {                                  /* 确实选择不同线程时才进行上下文保存/恢复。 */
#ifdef CONFIG_TIMESLICING
		z_reset_time_slice();                                    /* 新线程开始运行时建立其时间片预算。 */
#endif

		old_thread->swap_retval = -EAGAIN;                       /* 默认等待返回为超时/未显式成功，唤醒者可覆盖成0等状态。 */

#ifdef CONFIG_SMP
		_current_cpu->swap_ok = 0;                               /* 消费本次主动切换许可。 */
		new_thread->base.cpu = arch_curr_cpu()->id;              /* 记录新线程即将运行的CPU编号。 */

		if (!is_spinlock) {                                      /* irq_lock路径还须交接SMP隐含的全局锁嵌套。 */
			z_smp_release_global_lock(new_thread);           /* 按新线程需要恢复隐式全局中断锁。 */
		}
#endif
		z_thread_mark_switched_out();                            /* 记录旧线程结束本轮执行的跟踪/统计状态。 */
		wait_for_switch(new_thread);                             /* 等待新线程可安全恢复，SMP避免切入尚未保存完的上下文。 */
		_current_cpu->current = new_thread;                      /* 把本CPU当前线程指针切换到新对象。 */

#ifdef CONFIG_SPIN_VALIDATE
		z_spin_lock_set_owner(&sched_spinlock);                  /* 当前对象已变化，同步锁验证所有者避免误报。 */
#endif

		arch_cohere_stacks(old_thread, NULL, new_thread);        /* 按架构要求使旧/新栈的内存状态满足一致性。 */

#ifdef CONFIG_SMP
		/* 等候目标句柄完成后，当前CPU将有限时间内进入真正切换；
		 * 到此才重新放回旧线程，避免多CPU互相选择仍活动线程形成死锁。 */
		z_requeue_current(old_thread);                           /* 确定即将换出后才允许其他CPU选择旧线程。 */
#endif
		void *newsh = new_thread->switch_handle;                 /* 保存下一线程完整的可恢复上下文句柄。 */

		if (IS_ENABLED(CONFIG_SMP)) {                            /* SMP以空句柄标识线程正在CPU上活动。 */
			/* 活动线程的句柄必须为NULL，其他CPU不可切入尚未保存的上下文。 */
			new_thread->switch_handle = NULL;                /* 撤销新线程公开句柄，避免被其他CPU重复恢复。 */
		}
		k_spin_release(&sched_spinlock);                         /* 交出调度锁所有权，但暂不恢复本CPU中断。 */
		arch_switch(newsh, &old_thread->switch_handle);          /* 保存旧上下文句柄并恢复新上下文；旧线程未来恢复后才继续此调用。 */
	} else {
		k_spin_release(&sched_spinlock);                         /* 交出调度锁所有权，但暂不恢复本CPU中断。 */
	}

	if (is_spinlock) {                                               /* 本地自旋锁路径直接恢复架构中断屏蔽状态。 */
		arch_irq_unlock(key);                                    /* 恢复原本CPU中断状态，不处理SMP隐式全局锁。 */
	} else {
		irq_unlock(key);                                         /* irq_lock路径通过公共接口恢复全局/本地锁状态。 */
	}

	return _current->swap_retval;                                    /* 恢复后读取属于当前线程的唤醒状态。 */
}

/**
  * @brief  用已保存中断钥匙交接到当前架构的线程切换路径。
  * @param  key：[输入] 调用者已经屏蔽中断时保存的原状态。
  * @retval 恢复后由唤醒路径设定的状态，通常0成功、-EAGAIN超时。
  * @note   USE_SWITCH分支调用do_swap；当前ATS旧分支先检查栈哨兵，再调用arch_swap并由PendSV完成切换。
  */
static inline int z_swap_irqlock(unsigned int key)
{
	return do_swap(key, NULL, 0);    /* 指定irq_lock语义，交给新式切换处理隐式全局锁。 */
}

/**
  * @brief  交接调用方自旋锁并在合适线程上下文恢复后返回。
  * @param  lock：[输入输出] 当前持有的有效锁对象，不可为空。
  * @param  key：[输入] 取得该锁时返回的原始中断恢复钥匙。
  * @retval 线程恢复后的唤醒状态，通常0成功、-EAGAIN超时。
  * @note   返回时不重新持有原lock；当前ATS非SMP旧式分支先释放验证所有权，再把key.key传给arch_swap恢复中断状态。
  */
static inline int z_swap(struct k_spinlock *lock, k_spinlock_key_t key)
{
	return do_swap(key.key, lock, 1);    /* 指定显式自旋锁语义并传入原始中断钥匙。 */
}

/**
  * @brief  从未持自旋锁的路径屏蔽本CPU中断后进入切换。
  * @param  无。
  * @retval 无；本包装忽略底层切换状态。
  * @note   返回时恢复进入本包装之前的中断状态；仍需由调用者确保线程调度状态已正确建立。
  */
static inline void z_swap_unlocked(void)
{
	(void) do_swap(arch_irq_lock(), NULL, 1);    /* 本函数临时屏蔽中断后切换，忽略唤醒返回值。 */
}

#else    /* !CONFIG_USE_SWITCH */

/**
  * @brief  调用旧式架构上下文切换入口。
  * @param  key：[输入] 进入本轮临界区前的原中断状态钥匙。
  * @retval 恢复执行后由线程唤醒路径设置的整数状态，默认超时/未显式成功为-EAGAIN。
  * @note   当前ATS在arch/arm/core/aarch32/swap.c实现：保存BASEPRI，触发PendSV并开放普通中断以执行切换。
  */
extern int arch_swap(unsigned int key);

/**
  * @brief  用已保存中断钥匙交接到当前架构的线程切换路径。
  * @param  key：[输入] 调用者已经屏蔽中断时保存的原状态。
  * @retval 恢复后由唤醒路径设定的状态，通常0成功、-EAGAIN超时。
  * @note   USE_SWITCH分支调用do_swap；当前ATS旧分支先检查栈哨兵，再调用arch_swap并由PendSV完成切换。
  */
static inline int z_swap_irqlock(unsigned int key)
{
	int ret;                     /* 保存架构切换恢复后返回的线程状态。 */
	z_check_stack_sentinel();    /* 启用时检查当前线程栈底哨兵，尽早发现越界。 */
	ret = arch_swap(key);        /* 当前ATS通过PendSV保存/恢复上下文，再接收唤醒状态。 */
	return ret;                  /* 传播旧式架构切换的返回状态。 */
}

/* 旧式!USE_SWITCH不支持SMP，自旋锁退化为本CPU中断屏蔽；
 * k_spin_release主要维护可选锁验证，真正恢复中断由arch_swap完成。 */

/**
  * @brief  交接调用方自旋锁并在合适线程上下文恢复后返回。
  * @param  lock：[输入输出] 当前持有的有效锁对象，不可为空。
  * @param  key：[输入] 取得该锁时返回的原始中断恢复钥匙。
  * @retval 线程恢复后的唤醒状态，通常0成功、-EAGAIN超时。
  * @note   返回时不重新持有原lock；当前ATS非SMP旧式分支先释放验证所有权，再把key.key传给arch_swap恢复中断状态。
  */
static ALWAYS_INLINE int z_swap(struct k_spinlock *lock, k_spinlock_key_t key)
{
	k_spin_release(lock);              /* 释放对象锁的所有权/验证记录，不恢复中断。 */
	return z_swap_irqlock(key.key);    /* 把自旋锁钥匙的中断状态交给旧式切换入口。 */
}

/**
  * @brief  从未持自旋锁的路径屏蔽本CPU中断后进入切换。
  * @param  无。
  * @retval 无；本包装忽略底层切换状态。
  * @note   返回时恢复进入本包装之前的中断状态；仍需由调用者确保线程调度状态已正确建立。
  */
static inline void z_swap_unlocked(void)
{
	(void) z_swap_irqlock(arch_irq_lock());    /* 取得当前中断钥匙并切换，返回后丢弃底层状态。 */
}

#endif    /* !CONFIG_USE_SWITCH */

/**
  * @brief  为CPU启动首个线程准备可被切换代码保存的dummy线程状态。
  * @param  dummy_thread：[输出] 非空可写k_thread存储，允许事先未初始化，须覆盖启动交接阶段生命周期。
  * @retval 无。
  * @note   只初始化首次切换必需字段并设为当前线程，其他字段仍可能未初始化；旧上下文保存后即丢弃，不能当普通完整线程使用。
  */
static inline void z_dummy_thread_init(struct k_thread *dummy_thread)
{
	dummy_thread->base.thread_state = _THREAD_DUMMY;                     /* 标记为启动占位对象，允许首次切换的特例处理。 */
#ifdef CONFIG_SCHED_CPU_MASK
	dummy_thread->base.cpu_mask = -1;                                    /* CPU亲和启用时允许在掩码可表示的所有CPU运行。 */
#endif
	dummy_thread->base.user_options = K_ESSENTIAL;                       /* 占位启动上下文视为关键系统线程。 */
#ifdef CONFIG_THREAD_STACK_INFO
	dummy_thread->stack_info.start = 0U;                                 /* dummy没有普通可检查线程栈，起始地址置0。 */
	dummy_thread->stack_info.size = 0U;                                  /* 栈信息大小置0，避免把未初始化内存当栈范围。 */
#endif
#ifdef CONFIG_USERSPACE
	dummy_thread->mem_domain_info.mem_domain = &k_mem_domain_default;    /* 用户态配置下关联默认内存域。 */
#endif

	_current_cpu->current = dummy_thread;                                /* 首次调度前先建立架构切换可引用的当前线程对象。 */
}
#endif    /* ZEPHYR_KERNEL_INCLUDE_KSWAP_H_ */
