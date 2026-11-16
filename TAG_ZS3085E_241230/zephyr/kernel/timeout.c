/* 内核超时队列：双向链表按到期先后排列，每节点dticks存距前一节点的tick差。硬件报告经过时间后逐个摘除到期节点、释放锁执行回调，再重设下一硬件事件；SPI信号量限时等待及应用sleep依赖此机制。 */
/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <spinlock.h>
#include <ksched.h>
#include <timeout_q.h>
#include <syscall_handler.h>
#include <drivers/timer/system_timer.h>
#include <sys_clock.h>

static uint64_t curr_tick;    /* 已消费的系统tick累计量，静态初值0。 */

static sys_dlist_t timeout_list = SYS_DLIST_STATIC_INIT(&timeout_list);    /* 按到期时刻排列的双向差分链表，初始为空。 */

#ifdef CONFIG_TIME_SLEEP_COMP
static sys_dlist_t timeout_list_sl = SYS_DLIST_STATIC_INIT(&timeout_list_sl);    /* 睡眠补偿期间新建超时的暂存队列，节点保存相对预算。 */
static bool b_time_exit_sleep = false;                                           /* 是否暂存新超时，初始走正常主队列。 */

/**
  * @brief  标记进入睡眠时间补偿后的超时重建阶段。
  * @param  无。
  * @retval 无。
  * @note   仅CONFIG_TIME_SLEEP_COMP启用；标记使新超时先进入暂存队列，下一次时钟上报时转回主队列。
  */
void acts_exit_sleep(void)
{
	b_time_exit_sleep = true;    /* 后续新超时暂时不混入正在补偿的主队列。 */
}

#endif


static struct k_spinlock timeout_lock;    /* 保护tick累计与两条超时队列的全局自旋锁。 */

/* 没有已安排超时时，宽松空闲模式允许无限停钟；其他配置用INT_MAX安排远期事件以保持uptime。 */
#define MAX_WAIT (IS_ENABLED(CONFIG_SYSTEM_CLOCK_SLOPPY_IDLE) \
		  ? K_TICKS_FOREVER : INT_MAX)

/* 当前一次sys_clock_announce尚未分配处理的tick数；原英文误称cycle，实际单位为tick。 */
static int announce_remaining;

#if defined(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME)
int z_clock_hw_cycles_per_sec = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;    /* 运行期频率变量以配置值Hz初始化，供时间单位换算使用。 */

#ifdef CONFIG_USERSPACE

/**
  * @brief  为用户态返回运行期硬件计数频率。
  * @param  无。
  * @retval 当前硬件cycle频率，单位Hz。
  * @note   仅同时启用运行期频率与CONFIG_USERSPACE时编译。
  */
static inline int z_vrfy_sys_clock_hw_cycles_per_sec_runtime_get(void)
{
	return z_impl_sys_clock_hw_cycles_per_sec_runtime_get();    /* 读取当前配置的运行期硬件频率。 */
}
#include <syscalls/sys_clock_hw_cycles_per_sec_runtime_get_mrsh.c>
#endif    /* CONFIG_USERSPACE */
#endif    /* CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME */

/**
  * @brief  取得主超时队列最早到期节点。
  * @param  无。
  * @retval 队首_timeout指针；队列为空返回NULL。
  * @note   内部辅助，调用方持timeout_lock；链表节点通过CONTAINER_OF还原宿主对象。
  */
static struct _timeout *first(void)
{
	sys_dnode_t *t = sys_dlist_peek_head(&timeout_list);                 /* 取得主队列头节点，不移除节点。 */

	return t == NULL ? NULL : CONTAINER_OF(t, struct _timeout, node);    /* 空队列返回NULL，否则从node成员反推_timeout对象地址。 */
}

/**
  * @brief  取得主队列中指定超时项的后继。
  * @param  t：[输入] 位于主队列中的非空超时对象。
  * @retval 后继_timeout指针；到达链尾返回NULL。
  * @note   调用方持timeout_lock，不能传未链接对象。
  */
static struct _timeout *next(struct _timeout *t)
{
	sys_dnode_t *n = sys_dlist_peek_next(&timeout_list, &t->node);       /* 读取下一个链表节点，尾部由链表API返回NULL。 */

	return n == NULL ? NULL : CONTAINER_OF(n, struct _timeout, node);    /* 将后继链表节点还原为超时对象。 */
}

/**
  * @brief  从主队列摘除节点并保持后继绝对到期时刻不变。
  * @param  t：[输入输出] 主队列中的非空有效超时对象。
  * @retval 无。
  * @note   调用者持timeout_lock；把被移除节点的dticks加给后继，再断开链表。
  */
static void remove_timeout(struct _timeout *t)
{
	if (next(t) != NULL) {                   /* 存在后继时需保留后继原来的绝对截止时间。 */
		next(t)->dticks += t->dticks;    /* 把移除项的差分时间合并给后继。 */
	}

	sys_dlist_remove(&t->node);              /* 从睡眠暂存队列摘除目标，无需修正其他节点。 */
}

/**
  * @brief  取得可加入当前队列时间基准的未报告tick数。
  * @param  无。
  * @retval 未处理上报期间返回驱动sys_clock_elapsed，否则返回0。
  * @note   announce期间curr_tick逐步推进，必须屏蔽硬件elapsed以避免重复累计；调用者持timeout_lock。
  */
static int32_t elapsed(void)
{
	return announce_remaining == 0 ? sys_clock_elapsed() : 0U;    /* 上报处理期间不重复计入驱动elapsed，平时读取未上报tick。 */
}

/**
  * @brief  计算下一次硬件事件所需的相对tick预算。
  * @param  无。
  * @retval 队首剩余tick与时间片限制的较小值；无超时时返回MAX_WAIT。
  * @note   调用者持timeout_lock；已逾期预算夹为0，允许驱动尽快上报。
  */
static int32_t next_timeout(void)
{
	struct _timeout *to = first();                                         /* 取得当前最早到期对象，可为空。 */
	int32_t ticks_elapsed = elapsed();                                     /* 采样当前队列基准之后已经过的完整tick。 */
	/* 空队列采用远期预算；非空队列减已流逝量并夹到可安排范围。 */
	int32_t ret = to == NULL ? MAX_WAIT
		: CLAMP(to->dticks - ticks_elapsed, 0, MAX_WAIT);

#ifdef CONFIG_TIMESLICING
	if (_current_cpu->slice_ticks && _current_cpu->slice_ticks < ret) {    /* 时间片更早结束时不能只等待队列超时。 */
		ret = _current_cpu->slice_ticks;                               /* 缩短预算，使下一事件及时处理时间片。 */
	}
#endif
	return ret;                                                            /* 返回本路径计算的预算或取消状态。 */
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
		   k_timeout_t timeout)
{
	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {                                     /* 无限等待不需要在硬件定时队列安排事件。 */
		return;                                                             /* 此路径不再安排任何超时或继续忙等。 */
	}

#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(arch_mem_coherent(to));                                     /* 一致性配置下要求超时对象的内存满足架构要求。 */
#endif

	__ASSERT(!sys_dnode_is_linked(&to->node), "");                              /* 同一节点不能重复入队，避免破坏链表。 */
	to->fn = fn;                                                                /* 保存到期回调供时钟上报阶段调用。 */
	#ifdef CONFIG_TIME_SLEEP_COMP
	if(b_time_exit_sleep){                                                      /* 睡眠补偿期间暂存新超时，避免与正在推进的主队列混用时间基准。 */
		to->dticks = timeout.ticks;                                         /* 睡眠暂存队列直接保留原预算编码，暂不做主队列差分。 */
		//printk("sl:add time, %p, fun=%p, tick=%d\n", to, fn, (uint32_t)to->dticks);
		LOCKED(&timeout_lock) {                                             /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
			sys_dlist_append(&timeout_list_sl, &to->node);              /* 挂入睡眠暂存队列，等待主时钟推进结束后重插。 */
		}
		return;                                                             /* 此路径不再安排任何超时或继续忙等。 */
	}
	#endif
	LOCKED(&timeout_lock) {                                                     /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
		struct _timeout *t;                                                 /* 遍历主队列使用的当前节点。 */

		if (IS_ENABLED(CONFIG_TIMEOUT_64BIT) &&
		    /* 分别处理绝对与相对超时编码，统一变成距当前软件时间基准的正tick距离。 */
		    Z_TICK_ABS(timeout.ticks) >= 0) {
			k_ticks_t ticks = Z_TICK_ABS(timeout.ticks) - curr_tick;    /* 把绝对截止时间转换为相对当前软件tick的差。 */

			to->dticks = MAX(1, ticks);                                 /* 已经到期的绝对请求也至少延后一个tick。 */
		} else {
			to->dticks = timeout.ticks + 1 + elapsed();                 /* 相对等待对齐到下个tick，并补上当前未报告的经过量。 */
		}

		for (t = first(); t != NULL; t = next(t)) {                         /* 按到期顺序扫描，将目标预算逐项换成距前驱的差分。 */
			if (t->dticks > to->dticks) {                               /* 当前节点在新超时之后到期，应把新节点插在其前。 */
				t->dticks -= to->dticks;                            /* 缩短原节点相对新前驱的距离，保留原到期时刻。 */
				sys_dlist_insert(&t->node, &to->node);              /* 在当前节点之前插入新超时。 */
				break;                                              /* 已经找到插入/目标位置，或已满足忙等预算，结束当前循环。 */
			}
			to->dticks -= t->dticks;                                    /* 越过该节点，余量改为相对下一节点的差。 */
		}

		if (t == NULL) {                                                    /* 所有已有节点都更早或同刻到期时放到队尾。 */
			sys_dlist_append(&timeout_list, &to->node);                 /* 追加末尾，保存相对前一项的剩余tick差。 */
		}

		if (to == first()) {                                                /* 新超时最早到期，需要重新评估硬件事件。 */
#if CONFIG_TIMESLICING
			/* 时间片基于上一次上报边界，而这里还未完全对齐该基准，
			 * 因此下一次上报的实际剩余时间可能短于slice_ticks。 */
			int32_t next_time = next_timeout();                         /* 把新队首及时间片限制一起换成相对预算。 */

			if (next_time == 0 ||
			    _current_cpu->slice_ticks != next_time) {
				sys_clock_set_timeout(next_time, false);            /* 更新硬件事件以满足新的最早截止时间。 */
			}
#else
			sys_clock_set_timeout(next_timeout(), false);               /* 让定时器驱动安排新的最近事件，空队列则使用远期预算。 */
#endif                                                                              /* CONFIG_TIMESLICING */
		}
	}
}

/**
  * @brief  尝试取消仍在队列中的超时对象。
  * @param  to：[输入输出] 非空有效超时对象，可处于未激活状态。
  * @retval 0：从主队列或睡眠暂存队列移除；-EINVAL：未找到已链接对象。
  * @note   不等待已经开始的回调结束，取消成功与对象生命周期管理须由上层同步。
  */
int z_abort_timeout(struct _timeout *to)
{
	int ret = -EINVAL;                                                                      /* 默认未找到可取消的活动超时。 */
#ifdef CONFIG_TIME_SLEEP_COMP
	if(b_time_exit_sleep){                                                                  /* 睡眠补偿阶段先查找暂存队列中的新请求。 */
		struct _timeout *t, *next;                                                      /* 安全遍历时分别保存当前项和下一项，允许删除当前项。 */
		bool b_rm = false;                                                              /* 记录是否从睡眠暂存队列成功移除。 */
		if (!sys_dnode_is_linked(&to->node))                                            /* 节点未链接时没有可取消的活动事件。 */
			return ret;                                                             /* 返回本路径计算的预算或取消状态。 */
		LOCKED(&timeout_lock) {                                                         /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
			SYS_DLIST_FOR_EACH_CONTAINER_SAFE(&timeout_list_sl, t, next, node) {    /* 遍历暂存对象，同时保留后继以支持摘除。 */
				if(t == to){                                                    /* 目标在睡眠暂存队列中时直接摘除，无需修正主队列差分。 */
					sys_dlist_remove(&t->node);                             /* 从睡眠暂存队列摘除目标，无需修正其他节点。 */
					//printk("sl:remove time, %p, fun=%p, tick=%d\n", to, to->fn, (uint32_t)to->dticks);
					b_rm = true;                                            /* 记下成功取消，避免继续按主队列处理。 */
					break;                                                  /* 已经找到插入/目标位置，或已满足忙等预算，结束当前循环。 */
				}
			}
		}
		if(b_rm)                                                                        /* 暂存队列已取消成功，可直接返回。 */
			return 0;                                                               /* 成功取消暂存超时或报告未激活对象无剩余时间。 */
	}
#endif

	LOCKED(&timeout_lock) {                                                                 /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
		if (sys_dnode_is_linked(&to->node)) {                                           /* 仅移除仍活动的节点，避免重复取消。 */
			remove_timeout(to);                                                     /* 摘除主队列对象并修复后继的差分时间。 */
			ret = 0;                                                                /* 记录成功取消。 */
		}
	}

	return ret;                                                                             /* 返回本路径计算的预算或取消状态。 */
}

/* 调用者须持timeout_lock，保证遍历及累计使用同一队列快照。 */

/**
  * @brief  在锁内累计指定超时项距当前时间的剩余tick。
  * @param  timeout：[输入] 非空有效超时对象，活动对象须属于本超时队列。
  * @retval 未激活返回0；否则差分累加再减当前elapsed。
  * @note   调用者持timeout_lock；不执行回调。
  */
static k_ticks_t timeout_rem(const struct _timeout *timeout)
{
	k_ticks_t ticks = 0;                     /* 从零开始累计差分或接收锁内查询快照。 */

	if (z_is_inactive_timeout(timeout)) {    /* 未激活对象没有剩余等待。 */
		return 0;                        /* 成功取消暂存超时或报告未激活对象无剩余时间。 */
	}

	for (struct _timeout *t = first(); t != NULL; t = next(t)) {
		ticks += t->dticks;              /* 把沿途节点差分相加，得到相对队列时间基准的距离。 */
		if (timeout == t) {              /* 累计到目标节点即可结束扫描。 */
			break;                   /* 已经找到插入/目标位置，或已满足忙等预算，结束当前循环。 */
		}
	}

	return ticks - elapsed();                /* 扣除软件基准之后已经经过但未上报的tick。 */
}

/**
  * @brief  查询指定超时项的剩余tick。
  * @param  timeout：[输入] 非空有效超时对象。
  * @retval 未激活返回0，否则返回锁内计算的剩余tick快照。
  * @note   查询不改变队列；离开锁后时间与状态可立即变化。
  */
k_ticks_t z_timeout_remaining(const struct _timeout *timeout)
{
	k_ticks_t ticks = 0;                     /* 从零开始累计差分或接收锁内查询快照。 */

	LOCKED(&timeout_lock) {                  /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
		ticks = timeout_rem(timeout);    /* 在同一锁内读取队列与硬件经过量。 */
	}

	return ticks;                            /* 返回锁内计算的tick快照。 */
}

/**
  * @brief  查询超时项相对系统启动的截止tick。
  * @param  timeout：[输入] 非空有效超时对象。
  * @retval curr_tick加timeout_rem的结果；未激活时该实现返回curr_tick。
  * @note   锁内快照；保留实际公式，不把结果描述为始终含硬件elapsed的绝对时刻。
  */
k_ticks_t z_timeout_expires(const struct _timeout *timeout)
{
	k_ticks_t ticks = 0;                                 /* 从零开始累计差分或接收锁内查询快照。 */

	LOCKED(&timeout_lock) {                              /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
		ticks = curr_tick + timeout_rem(timeout);    /* 按原实现公式把剩余量加到软件累计基准。 */
	}

	return ticks;                                        /* 返回锁内计算的tick快照。 */
}

/**
  * @brief  查询内核希望下一次时钟事件发生的相对tick。
  * @param  无。
  * @retval 最近超时/时间片预算；无定时事件返回MAX_WAIT配置值。
  * @note   内部加锁；MAX_WAIT可能是K_TICKS_FOREVER或INT_MAX。
  */
int32_t z_get_next_timeout_expiry(void)
{
	int32_t ret = (int32_t) K_TICKS_FOREVER;    /* 初始化为无限等待哨兵，锁内用实际下一预算替换。 */

	LOCKED(&timeout_lock) {                     /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
		ret = next_timeout();               /* 综合队列与时间片限制查询下一事件。 */
	}
	return ret;                                 /* 返回本路径计算的预算或取消状态。 */
}

/**
  * @brief  根据更早预算和空闲提示请求更新硬件时钟事件。
  * @param  ticks：[输入] 新请求的相对tick预算。
  * @param  is_idle：[输入] 是否即将进入空闲，原样传驱动。
  * @retval 无。
  * @note   不改变软件超时链表；临近到期时避免反复重设，SMP另考虑切换/时间片条件。
  */
void z_set_timeout_expiry(int32_t ticks, bool is_idle)
{
	LOCKED(&timeout_lock) {                                                 /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
		int next_to = next_timeout();                                   /* 读取已经安排的最近事件预算。 */
		/* 比较新请求与当前最近事件，避免把已有的更早截止时间推迟。 */
		bool sooner = (next_to == K_TICKS_FOREVER)
			      || (ticks <= next_to);
		bool imminent = next_to <= 1;                                   /* 一个tick以内视为即将到期，避免重设推迟。 */

		/* 只把硬件事件提前；已经迫近到期时不再重设，防止驱动将它推迟到下一tick。
		 * SMP需在中断退出才知道线程切换，时间片约束可能改变，因此不能只靠更早条件优化。 */
		if (!imminent && (sooner || IS_ENABLED(CONFIG_SMP))) {          /* 新预算更早或SMP需重算，且当前事件未迫近时才写硬件。 */
			sys_clock_set_timeout(MIN(ticks, next_to), is_idle);    /* 选择更早预算，并传递即将空闲的提示。 */
		}
	}
}

/**
  * @brief  消费驱动报告的tick并执行所有已到期超时回调。
  * @param  ticks：[输入] 自上次上报后经过的完整tick数，驱动须正确串行报告。
  * @retval 无。
  * @note   回调前释放timeout_lock，回调后重新取得；通常由定时器ISR调用，因此回调不应阻塞。睡眠补偿分支还会转移暂存超时。
  */
void sys_clock_announce(int32_t ticks)
{
#ifdef CONFIG_TIMESLICING
	z_time_slice(ticks);                                                             /* 启用时间片时同步扣减本次报告的tick。 */
#endif

	k_spinlock_key_t key = k_spin_lock(&timeout_lock);                               /* 锁住差分队列与软件tick，保存后续回调交接所需的钥匙。 */

	announce_remaining = ticks;                                                      /* 建立本轮尚待消费的tick预算。 */

	/* 按截止顺序处理本轮全部到期项；每次回调都可能改变队列，因此返回后重新取队首。 */
	while (first() != NULL && first()->dticks <= announce_remaining) {               /* 只要队首已落在本轮经过时间内，就逐个执行到期事件。 */
		struct _timeout *t = first();                                            /* 取出本轮最近到期对象，回调期间可能被重新入队。 */
		int dt = t->dticks;                                                      /* 保存队首差分，随后先消费这段时间。 */

		curr_tick += dt;                                                         /* 把内核时间推进到该事件的截止点。 */
		announce_remaining -= dt;                                                /* 本轮剩余量扣除已推进的时间。 */
		t->dticks = 0;                                                           /* 标记该项正好到期，移除时无需再增加后继差分。 */
		remove_timeout(t);                                                       /* 先从队列摘除，允许回调重新使用同一对象。 */

		k_spin_unlock(&timeout_lock, key);                                       /* 执行外部回调前释放锁，允许回调操作超时队列。 */
		t->fn(t);                                                                /* 调用到期处理，如唤醒等待线程；此时不持超时锁。 */
		key = k_spin_lock(&timeout_lock);                                        /* 回调返回后重新锁住队列，再继续检查最新队首。 */
	}
	#ifdef CONFIG_TIME_SLEEP_COMP
	if(b_time_exit_sleep){//
		struct _timeout *t0, *next;                                              /* 保存睡眠暂存队列的当前对象与安全遍历后继。 */
		b_time_exit_sleep = false;                                               /* 关闭暂存模式，使重插请求走正常主队列。 */
		if (first() != NULL){                                                    /* 主队列仍有项时先把本轮余量扣到新队首。 */
			//printk("t_isr: next tick=%d, cur-t=%d\n\n", (uint32_t)(first()->dticks), ticks);
			first()->dticks -= announce_remaining;                           /* 把尚未消费的tick反映到最近未来事件的差分。 */
		}else{
			//printk("t_isr: list null,  cur-t=%d\n", ticks);
		}
		curr_tick += announce_remaining;                                         /* 消费本轮剩余tick并推进软件累计量。 */
		announce_remaining = 0;                                                  /* 补偿阶段已经消费本轮余量，后面的公共路径不可再累计。 */
		SYS_DLIST_FOR_EACH_CONTAINER_SAFE(&timeout_list_sl, t0, next, node) {    /* 把补偿阶段暂存的每个请求重新按当前时间基准入主队列。 */
			sys_dlist_remove(&t0->node);                                     /* 先断开暂存节点，满足z_add_timeout不能重复链接的条件。 */
			//printk("t_isr:remove time, %p, fun=%p, tick=%d\n", t0, t0->fn, (uint32_t)t0->dticks);
			z_add_timeout(t0, t0->fn, Z_TIMEOUT_TICKS(t0->dticks));          /* 沿用原预算编码重插；此调用发生在当前外层锁范围内。 */
		}
	}
	#endif
	if (first() != NULL) {                                                           /* 还有未来事件时扣除本轮余量以维护差分基准。 */
		first()->dticks -= announce_remaining;                                   /* 把尚未消费的tick反映到最近未来事件的差分。 */
	}

	curr_tick += announce_remaining;                                                 /* 消费本轮剩余tick并推进软件累计量。 */
	announce_remaining = 0;                                                          /* 本轮所有tick已消费，恢复正常elapsed查询语义。 */

	sys_clock_set_timeout(next_timeout(), false);                                    /* 让定时器驱动安排新的最近事件，空队列则使用远期预算。 */

	k_spin_unlock(&timeout_lock, key);                                               /* 执行外部回调前释放锁，允许回调操作超时队列。 */
}

/**
  * @brief  读取包含硬件未报告时间的64位系统tick。
  * @param  无。
  * @retval 自启动的tick快照，单位由CONFIG_SYS_CLOCK_TICKS_PER_SEC确定。
  * @note   用timeout_lock保护64位软件累计量及硬件elapsed配对读取。
  */
int64_t sys_clock_tick_get(void)
{
	uint64_t t = 0U;                                /* 保存锁内64位tick快照，避免32位CPU读取撕裂。 */

	LOCKED(&timeout_lock) {                         /* 在作用域内持全局超时锁，退出时由LOCKED宏恢复。 */
		t = curr_tick + sys_clock_elapsed();    /* 加上定时器尚未报告的完整tick，提供较新时间。 */
	}
	return t;                                       /* 返回锁内采样的系统tick。 */
}

/**
  * @brief  读取系统tick低32位。
  * @param  无。
  * @retval tickless返回完整查询的低32位，周期模式直接返回curr_tick低32位。
  * @note   32位会回绕，查询后时间仍继续变化。
  */
uint32_t sys_clock_tick_get_32(void)
{
#ifdef CONFIG_TICKLESS_KERNEL
	return (uint32_t)sys_clock_tick_get();    /* tickless模式将包含硬件elapsed的64位快照截为低32位。 */
#else
	return (uint32_t)curr_tick;               /* 周期模式直接读取已上报累计量的低32位。 */
#endif
}

/**
  * @brief  提供内核uptime的tick系统调用实现。
  * @param  无。
  * @retval 64位系统tick快照。
  * @note   直接调用sys_clock_tick_get，毫秒换算在公共接口层进行。
  */
int64_t z_impl_k_uptime_ticks(void)
{
	return sys_clock_tick_get();    /* 沿用系统tick查询，不在此层换算毫秒。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  转接用户态uptime tick查询。
  * @param  无。
  * @retval 与内核实现相同的64位tick快照。
  * @note   仅CONFIG_USERSPACE编译，无对象指针需要验证。
  */
static inline int64_t z_vrfy_k_uptime_ticks(void)
{
	return z_impl_k_uptime_ticks();    /* 用户态转接到相同的内核tick查询实现。 */
}
#include <syscalls/k_uptime_ticks_mrsh.c>
#endif

/**
  * @brief  持续占用当前CPU等待指定微秒数。
  * @param  usec_to_wait：[输入] 等待时长，单位微秒；0立即返回。
  * @retval 无。
  * @note   默认轮询32位cycle计数；不挂起线程，也不主动屏蔽中断。中断可延长实际耗时，硬件计时停止会使轮询无法结束。架构自定义配置改调arch_busy_wait。
  */
void z_impl_k_busy_wait(uint32_t usec_to_wait)
{
	SYS_PORT_TRACING_FUNC_ENTER(k_thread, busy_wait, usec_to_wait);           /* 向可选跟踪后端记录忙等开始与微秒预算。 */
	if (usec_to_wait == 0U) {                                                 /* 零预算无需读取硬件计数或占用轮询时间。 */
		SYS_PORT_TRACING_FUNC_EXIT(k_thread, busy_wait, usec_to_wait);    /* 记录忙等结束，零预算和正常路径都可到达。 */
		return;                                                           /* 此路径不再安排任何超时或继续忙等。 */
	}

#if !defined(CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT)
	uint32_t start_cycles = k_cycle_get_32();                                 /* 采样硬件cycle起点，默认不屏蔽中断。 */

	/* 先用64位乘法换算微秒，避免中间乘积溢出；最终仍截取32位周期预算。 */
	/* 把微秒换算成硬件周期；使用配置/运行期cycle频率，最终32位预算存在表示上限。 */
	uint32_t cycles_to_wait = (uint32_t)(
		(uint64_t)usec_to_wait *
		(uint64_t)sys_clock_hw_cycles_per_sec() /
		(uint64_t)USEC_PER_SEC
	);

	for (;;) {                                                                /* 持续读取硬件计数直到达到周期预算；不让出CPU。 */
		uint32_t current_cycles = k_cycle_get_32();                       /* 采样当前cycle，计时源须持续运行。 */

		/* 无符号差值处理32位计数回绕；预算及采样跨度仍须在可表示范围内。 */
		if ((current_cycles - start_cycles) >= cycles_to_wait) {          /* 用无符号差达到预算作为唯一退出条件。 */
			break;                                                    /* 已经找到插入/目标位置，或已满足忙等预算，结束当前循环。 */
		}
	}
#else
	arch_busy_wait(usec_to_wait);                                             /* 架构提供专用忙等时采用其微秒实现。 */
#endif                                                                            /* CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT */
	SYS_PORT_TRACING_FUNC_EXIT(k_thread, busy_wait, usec_to_wait);            /* 记录忙等结束，零预算和正常路径都可到达。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  转接用户态微秒忙等请求。
  * @param  usec_to_wait：[输入] 微秒预算，0无需等待。
  * @retval 无。
  * @note   仅CONFIG_USERSPACE编译，沿用内核实现的CPU占用和计时限制。
  */
static inline void z_vrfy_k_busy_wait(uint32_t usec_to_wait)
{
	z_impl_k_busy_wait(usec_to_wait);    /* 用户态请求直接使用内核微秒忙等实现。 */
}
#include <syscalls/k_busy_wait_mrsh.c>
#endif    /* CONFIG_USERSPACE */

/* 以一次未锁住的当前时刻把用户预算换算为截止tick；应在收到预算时计算一次，
 * 循环重算会把相对预算不断向后推，破坏原来的总超时限制。 */

/**
  * @brief  把一个超时参数转换为本次等待的绝对截止tick。
  * @param  timeout：[输入] K_FOREVER、K_NO_WAIT、相对预算或64位绝对超时编码。
  * @retval 无限等待为UINT64_MAX；不等待为当前tick；绝对编码直接解码；相对值加到当前tick且至少1 tick。
  * @note   以未锁住的现在为基准，仅在接收新预算时调用一次，避免等待循环延长截止时刻。
  */
uint64_t sys_clock_timeout_end_calc(k_timeout_t timeout)
{
	k_ticks_t dt;                                                             /* 保存待解码的tick预算。 */

	if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {                                   /* 无限等待不需要在硬件定时队列安排事件。 */
		return UINT64_MAX;                                                /* 无限等待用最大64位值表示无有限截止时间。 */
	} else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {                            /* 不等待的截止点就是查询时的当前tick。 */
		return sys_clock_tick_get();                                      /* 沿用系统tick查询，不在此层换算毫秒。 */
	} else {

		dt = timeout.ticks;                                               /* 提取内部超时编码，供绝对/相对分支解析。 */

		if (IS_ENABLED(CONFIG_TIMEOUT_64BIT) && Z_TICK_ABS(dt) >= 0) {    /* 仅64位配置识别负值区间编码的绝对截止时间。 */
			return Z_TICK_ABS(dt);                                    /* 解码后直接返回绝对截止tick，不再加当前时间。 */
		}
		return sys_clock_tick_get() + MAX(1, dt);                         /* 相对预算加到当前时间，最少等待一个tick。 */
	}
}
