/*
 * Copyright (c) 2016 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 提供不依赖kernel.h的最小内核结构定义，避免公共接口和底层状态相互包含。
 * 维护约束：本头不能直接或间接包含kernel.h；反之包含kernel.h应自动取得本头类型。
 * SPI信号量等待依赖_wait_q_t，定时超时依赖_timeout，调度器通过_kernel维护CPU和就绪队列。 */

#ifndef ZEPHYR_KERNEL_INCLUDE_KERNEL_STRUCTS_H_
#define ZEPHYR_KERNEL_INCLUDE_KERNEL_STRUCTS_H_    /* 防止基础结构与状态宏重复定义。 */

#if !defined(_ASMLANGUAGE)
#include <sys/atomic.h>
#include <zephyr/types.h>
#include <kernel/sched_priq.h>
#include <sys/dlist.h>
#include <sys/util.h>
#include <sys/sys_heap.h>
#include <arch/structs.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* k_thread基础状态的thread_state位掩码。必须先于可能引用它们的架构数据声明，
 * 多个标志可同时存在；清一个标志不自动满足其余就绪条件。 */

/* 状态低位供公共内核使用，高位按架构约定使用。 */

/* 启动占位对象，不是普通完整线程。 */
#define _THREAD_DUMMY (BIT(0))    /* 占位线程标志，位0。 */

/* 正在等待信号量等内核对象条件。 */
#define _THREAD_PENDING (BIT(1))    /* 对象等待标志，位1。 */

/* 尚未启动，不能加入可运行候选。 */
#define _THREAD_PRESTART (BIT(2))    /* 未启动标志，位2。 */

/* 已结束，超时不能使其重新运行。 */
#define _THREAD_DEAD (BIT(3))    /* 死亡标志，位3。 */

/* 被显式挂起或用于睡眠，须解除后才可能就绪。 */
#define _THREAD_SUSPENDED (BIT(4))    /* 挂起标志，位4。 */

/* 跨CPU终止正在协调中，尚待安全清理。 */
#define _THREAD_ABORTING (BIT(5))    /* 终止进行中标志，位5。 */

/* 就绪队列资格；SMP当前线程可带标志而暂不在实体队列。 */
#define _THREAD_QUEUED (BIT(7))    /* 就绪排队资格标志，位7。 */

/* 线程基本状态位定义结束。 */

#ifdef CONFIG_STACK_SENTINEL
/* 栈最低地址处的哨兵字，覆盖可提示向下增长的栈已越界，但不能保证检测所有破坏。 */
#define STACK_SENTINEL 0xF0F0F0F0
#endif

/* preempt合并字段达到本门限后不可普通抢占。 */
#define _NON_PREEMPT_THRESHOLD 0x0080U

/* 可普通抢占的preempt合并编码上限，包含优先级与调度锁字节。 */
#define _PREEMPT_THRESHOLD (_NON_PREEMPT_THRESHOLD - 1U)

#if !defined(_ASMLANGUAGE)

struct _ready_q {
#ifndef CONFIG_SMP
	/* 单核维护下一应运行线程缓存，初始化后不能为NULL，至少指向idle。 */
	struct k_thread *cache;
#endif

#if defined(CONFIG_SCHED_DUMB)
	sys_dlist_t runq;        /* DUMB后端的有序双向运行队列。 */
#elif defined(CONFIG_SCHED_SCALABLE)
	struct _priq_rb runq;    /* SCALABLE后端的优先级红黑树。 */
#elif defined(CONFIG_SCHED_MULTIQ)
	struct _priq_mq runq;    /* MULTIQ后端的优先级子队列与非空位图。 */
#endif
};

typedef struct _ready_q _ready_q_t;    /* 运行队列类型别名，实际布局由后端配置决定。 */

struct _cpu {
	/* 当前CPU中断嵌套深度，进入/退出中断路径维护。 */
	uint32_t nested;

	/* 本CPU中断专用栈的基址/架构所需指针，存储由内核启动层管理。 */
	char *irq_stack;

	/* 本CPU当前调度线程，不等同于就绪队列的首项。 */
	struct k_thread *current;

	/* 每CPU一个专用空闲线程，普通队列空时兜底运行。 */
	struct k_thread *idle_thread;

#if (CONFIG_NUM_METAIRQ_PRIORITIES > 0) && (CONFIG_NUM_COOP_PRIORITIES > 0)
	/* 被当前meta-IRQ介入的协作线程，退出后应优先恢复；无记录为NULL。 */
	struct k_thread *metairq_preempted;
#endif

#ifdef CONFIG_TIMESLICING
	/* 当前时间片剩余tick数，由时钟上报路径扣减。 */
	int slice_ticks;
#endif

	uint8_t id;    /* 当前CPU的0起始编号。 */

#ifdef CONFIG_SMP
	/* SMP允许当前线程主动切换的标记，实际切换时消费。 */
	uint8_t swap_ok;
#endif

	/* 每CPU的架构私有状态，由对应架构结构定义。 */
	struct _cpu_arch arch;
};

typedef struct _cpu _cpu_t;    /* 每CPU状态类型别名。 */

struct z_kernel {
	struct _cpu cpus[CONFIG_MP_NUM_CPUS];    /* 静态CPU状态数组，长度由构建支持的CPU数决定。 */

#ifdef CONFIG_PM
	int32_t idle;    /* 内核预计空闲的tick数，不是毫秒。 */
#endif

	/* 就绪队列可能较大，放在小字段之后，使某些架构汇编仍能用有限范围的
	 * 偏移编码访问前面的关键字段。结构布局改变须同步架构偏移生成。 */
	struct _ready_q ready_q;

#ifdef CONFIG_FPU_SHARING
	/* 原IA-32说明：浮点保存/恢复同时覆盖X87和XMM，不能把两部分分给不同线程，
	 * 因此只用current_fp记录统一所有者，不另设current_sse。此说明不代表ATS使用IA-32。 */

	/* 当前浮点寄存器内容的所属线程，未必等于当前运行线程。 */
	struct k_thread *current_fp;
#endif

#if defined(CONFIG_THREAD_MONITOR)
	struct k_thread *threads;    /* 启用监控时全部线程的单向链表首项。 */
#endif
};

typedef struct z_kernel _kernel_t;    /* 全局内核状态类型别名。 */

extern struct z_kernel _kernel;    /* 仅声明全局内核状态，实际唯一对象由内核启动源文件定义。 */

#ifdef CONFIG_SMP

/* 查询当前执行是否可能被抢占并迁移到另一CPU；可迁移时不能长期保留当前CPU结构指针。 */

/**
  * @brief  查询当前执行上下文是否可被抢占迁移到另一CPU。
  * @param  无。
  * @retval true：可抢占迁移；false：当前CPU身份在本临界区稳定。
  * @note   SMP内部检查，供_current_cpu宏防止保存可过时CPU指针。
  */
bool z_smp_cpu_mobile(void);

/* SMP读取当前CPU结构前断言当前不能迁移，否则返回地址可能在随后抢占迁移后过时。 */
#define _current_cpu ({ __ASSERT_NO_MSG(!z_smp_cpu_mobile()); \
			arch_curr_cpu(); })
#define _current z_current_get()    /* SMP经接口读取当前线程，接口负责读取期间的本CPU同步。 */

#else
#define _current_cpu (&_kernel.cpus[0])     /* 单核固定选择CPU0状态地址。 */
#define _current _kernel.cpus[0].current    /* 单核直接读取CPU0当前线程字段。 */
#endif

/* 内核对象等待队列，与按时间排序的超时队列分离。 */

#ifdef CONFIG_WAITQ_SCALABLE

typedef struct {
	struct _priq_rb waitq;    /* SCALABLE等待队列的红黑树状态。 */
} _wait_q_t;

/**
  * @brief  比较两个有效线程红黑树节点的调度先后。
  * @param  a：[输入] 嵌入有效k_thread的非空树节点。
  * @param  b：[输入] 嵌入另一有效k_thread的非空树节点。
  * @retval true：a应在b前；false：否则。
  * @note   实现在sched.c，先比较基础优先级/可选截止，再按order_key维持同级FIFO；调用方同步树操作。
  */
extern bool z_priq_rb_lessthan(struct rbnode *a, struct rbnode *b);

#define Z_WAIT_Q_INIT(wait_q) { { { .lessthan_fn = z_priq_rb_lessthan } } }    /* 树后端静态空队列初始化，绑定线程比较回调。 */

#else

typedef struct {
	sys_dlist_t waitq;    /* 当前DUMB等待队列的有序双向链表。 */
} _wait_q_t;

#define Z_WAIT_Q_INIT(wait_q) { SYS_DLIST_STATIC_INIT(&(wait_q)->waitq) }    /* 链表后端静态空队列初始化，首尾指向自身哨兵。 */

#endif

/* 超时对象嵌入线程、定时器等宿主，由超时队列管理链接与到期回调。 */

struct _timeout;
/* 到期回调类型：超时模块调用fn(t)，t为已摘除的有效宿主内嵌对象；无返回值，通常在时钟ISR上下文，不应阻塞。调用者维护对象生命周期，可在回调里重新登记。 */
typedef void (*_timeout_func_t)(struct _timeout *t);

struct _timeout {
	sys_dnode_t node;      /* 嵌入差分超时队列的节点，未激活时链接为空。 */
	_timeout_func_t fn;    /* 到期回调，超时模块先摘节点并释放timeout_lock再调用。 */
#ifdef CONFIG_TIMEOUT_64BIT
	/* 为避免依赖sys_clock.h产生包含环，直接使用与64位tick配置一致的int64_t。 */
	int64_t dticks;        /* 64位配置下距前驱到期点的tick差，主队列首项相对curr_tick。 */
#else
	int32_t dticks;        /* 32位配置下有符号tick差，不是毫秒或硬件cycle。 */
#endif
};

#ifdef __cplusplus
}
#endif

#endif    /* _ASMLANGUAGE */

#endif    /* ZEPHYR_KERNEL_INCLUDE_KERNEL_STRUCTS_H_ */
