/* 线程控制块布局，须先经内核包含链获得队列、架构和超时基础类型。SPI调用者的等待节点、超时节点与挂起/优先级状态都嵌入此对象，线程栈另行分配；控制块本身不拥有自动free所有资源的语义。所有条件分支保留，实际字段以构建配置为准。 */
/*
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_KERNEL_THREAD_H_
#define ZEPHYR_INCLUDE_KERNEL_THREAD_H_    /* 防止线程结构体及别名重复定义。 */

#ifdef CONFIG_DEMAND_PAGING_THREAD_STATS
#include <sys/mem_manage.h>
#endif

/**
 * @typedef k_thread_entry_t
 * @brief 线程启动入口类型的接口说明，类型本身由上层内核基础声明提供。
 * 线程开始时调用entry(p1,p2,p3)，无返回值；三个void*参数由创建者传入，允许空值与否由入口约定。
 * 参数指向内容的可读写性、长度及生命周期由创建者与入口共同管理，不转移所有权。
 * 入口返回将永久结束线程；返回前线程须自行释放互斥量、动态内存等持有资源。
 */

#ifdef CONFIG_THREAD_MONITOR
struct __thread_entry {
	k_thread_entry_t pEntry;    /* 监控记录的线程入口，启动层在开始执行时调用。 */
	void *parameter1;           /* 传给入口p1的指针值，内容与所有权由应用约定。 */
	void *parameter2;           /* 传给入口p2的指针值。 */
	void *parameter3;           /* 传给入口p3的指针值。 */
};
#endif

/* 最小线程基础状态，也可建立用于启动或对象等待的dummy占位对象。 */
struct _thread_base {

	/* 同一线程只能按当前状态使用一种就绪/等待队列节点，联合体复用链表或树布局。 */
	union {
		sys_dnode_t qnode_dlist;    /* 链表运行/等待队列使用的内嵌节点。 */
		struct rbnode qnode_rb;     /* 红黑树运行/等待队列使用的内嵌节点，与链表节点复用存储。 */
	};

	/* 线程所属对象等待队列，便于摘除；当前调度器不论链表/树后端都使用此记录。 */
	_wait_q_t *pended_on;

	/* 面向应用的K_*线程选项位，定义在kernel.h，不等于内部thread_state。 */
	uint8_t user_options;

	/* _THREAD_*内部状态组合，由调度器维护。 */
	uint8_t thread_state;

	/* 两个相邻字节合成preempt，供调度器快速判定普通抢占资格。
	 * sched_locked以无符号递减增加嵌套：0为未锁，0xff～0x01为已锁。
	 * prio为负表示协作线程，按无符号观察落在0x80～0xff。
	 * 按目标端序排列后，只要调度锁非零或优先级为负，16位preempt就>=0x0080；
	 * 因此一次门限比较即可判为不可普通抢占，meta-IRQ等例外由调度策略处理。 */
	union {
		struct {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			uint8_t sched_locked;    /* 普通调度锁嵌套的反向编码字节，0为未锁。 */
			int8_t prio;             /* 基础优先级数值，负为协作，非负为可抢占优先级。 */
#else                                            /* 小端和 PDP 字节序下的字段排列。 */
			int8_t prio;             /* 基础优先级数值，负为协作，非负为可抢占优先级。 */
			uint8_t sched_locked;    /* 普通调度锁嵌套的反向编码字节，0为未锁。 */
#endif
		};
		uint16_t preempt;                /* 同一存储的合并视图，用门限比较判定是否可普通抢占。 */
	};

#ifdef CONFIG_SCHED_DEADLINE
	int prio_deadline;    /* 同基础优先级的32位cycle截止排序键，仅启用截止调度时存在。 */
#endif

	uint32_t order_key;    /* 同有效优先级的入队先后键，维持FIFO。 */

#ifdef CONFIG_SMP
	/* 是否为每CPU专用idle线程。 */
	uint8_t is_idle;

	/* 上次运行所在CPU的编号。 */
	uint8_t cpu;

	/* irq_lock隐式全局锁的递归嵌套计数，切换时按目标线程恢复。 */
	uint8_t global_lock_count;

#endif

#ifdef CONFIG_SCHED_CPU_MASK
	/* CPU亲和许可位，第n位为1表示允许在CPU n运行，只有8位容量。 */
	uint8_t cpu_mask;
#endif

	/* IPC等待恢复时附带的数据指针，具体所有权由通信接口约定。 */
	void *swap_data;

#ifdef CONFIG_SYS_CLOCK_EXISTS
	/* 线程的独立超时节点，可同时关联对象等待队列；到期回调负责解除等待。 */
	struct _timeout timeout;
#endif
};

typedef struct _thread_base _thread_base_t;    /* 最小线程基础状态的类型别名。 */

#if defined(CONFIG_THREAD_STACK_INFO)
/* 可写线程栈范围与初始栈顶调整信息，不是栈存储本身。 */
struct _thread_stack_info {
	/* 线程可写栈区最低地址，不包括不可写保护区。 */
	uintptr_t start;

	/* 从start开始的可写总字节数，包含实际栈、局部线程数据预留以及随机栈顶调整
	 * 留出的区间，不能直接当作当前未使用栈容量。 */
	size_t size;

	/* 从size扣除的TLS/随机偏移占用字节，可为0；初始栈指针按start+size-delta计算。 */
	size_t delta;
};

typedef struct _thread_stack_info _thread_stack_info_t;    /* 可选栈范围元数据的类型别名。 */
#endif                                                     /* CONFIG_THREAD_STACK_INFO */

#if defined(CONFIG_USERSPACE)
struct _mem_domain_info {
	/** 内存域线程列表的内嵌节点，只有用户态配置存在。 */
	sys_dnode_t mem_domain_q_node;
	/** 所属内存域指针，不表示线程独占该域或拥有其内存。 */
	struct k_mem_domain *mem_domain;
};

#endif    /* CONFIG_USERSPACE */

#ifdef CONFIG_THREAD_USERSPACE_LOCAL_DATA
struct _thread_userspace_local_data {
#if defined(CONFIG_ERRNO) && !defined(CONFIG_ERRNO_IN_TLS)
	int errno_var;    /* 本配置下的每线程errno存储，非全局共享错误码。 */
#endif
};
#endif

#ifdef CONFIG_THREAD_RUNTIME_STATS
struct k_thread_runtime_stats {
	/* 线程实际执行累计cycle，计数源取决于是否使用timing后端。 */
#ifdef CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
	timing_t execution_cycles;    /* 采用timing后端类型保存执行累计cycle。 */
#else
	uint64_t execution_cycles;    /* 默认用64位保存线程累计执行cycle。 */
#endif
};

typedef struct k_thread_runtime_stats k_thread_runtime_stats_t;    /* 公开执行时间统计结构的类型别名。 */

struct _thread_runtime_stats {
	/* 最近一次被切入时的计时戳，单位须与累计cycle后端一致。 */
#ifdef CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
	timing_t last_switched_in;    /* timing后端的最近切入计时戳。 */
#else
	uint32_t last_switched_in;    /* 默认32位cycle计时戳，差分须考虑回绕。 */
#endif

	k_thread_runtime_stats_t stats;    /* 此线程对外提供的累计执行统计。 */
};
#endif

struct z_poller {
	bool is_polling;    /* 线程是否正在参与poll等待，细节由poll实现维护。 */
	uint8_t mode;       /* poll内部模式编码，不是SPI模式。 */
};

/**
 * @ingroup thread_apis
 * 线程控制块，供调度、上下文保存、监控与用户态对象管理共同使用。
 */
struct k_thread {

	struct _thread_base base;    /* 公共调度状态，置于首部以供节点反推和汇编偏移使用。 */

	/** 架构定义的被调用者保存寄存器状态，各架构均需提供此部分。 */
	struct _callee_saved callee_saved;

	/** 静态线程初始化数据视图，具体布局由启动层解释，不在此分配。 */
	void *init_data;

	/** 等待本线程退出的join等待者队列，线程终止时唤醒。 */
	_wait_q_t join_queue;

#if defined(CONFIG_POLL)
	struct z_poller poller;    /* 可选poll等待状态，SPI异步信号接口会涉及此机制。 */
#endif

#if defined(CONFIG_THREAD_MONITOR)
	/** 线程监控保存的启动入口及三个参数，用于检查和遍历。 */
	struct __thread_entry entry;

	/** 全部线程监控单链表的后继，无后继时为空。 */
	struct k_thread *next_thread;
#endif

#if defined(CONFIG_THREAD_NAME)
	/** 固定容量线程名字符数组，容量含零终止空间。 */
	char name[CONFIG_THREAD_MAX_NAME_LEN];
#endif

#ifdef CONFIG_THREAD_CUSTOM_DATA
	/** 应用自定义每线程指针，不自动分配或释放目标对象。 */
	void *custom_data;
#endif

#ifdef CONFIG_THREAD_USERSPACE_LOCAL_DATA
	struct _thread_userspace_local_data *userspace_local_data;    /* 用户空间可访问的线程局部数据区，存储所有权由线程建立层管理。 */
#endif

#if defined(CONFIG_ERRNO) && !defined(CONFIG_ERRNO_IN_TLS)
#ifndef CONFIG_USERSPACE
	/** 非TLS模式的每线程错误码存储。 */
	int errno_var;    /* 本配置下的每线程errno存储，非全局共享错误码。 */
#endif
#endif

#if defined(CONFIG_THREAD_STACK_INFO)
	/** 可写栈范围及初始栈顶调整信息，用于检查/监控。 */
	struct _thread_stack_info stack_info;
#endif    /* CONFIG_THREAD_STACK_INFO */

#if defined(CONFIG_USERSPACE)
	/** 用户态配置的内存域关联状态。 */
	struct _mem_domain_info mem_domain_info;
	/** 原线程栈对象地址，用于权限/对象管理，不等同于实时栈指针。 */
	k_thread_stack_t *stack_obj;
	/** 当前系统调用保存的帧指针，由用户态切换路径管理。 */
	void *syscall_frame;
#endif    /* CONFIG_USERSPACE */

#if defined(CONFIG_THREAD_TIMER)
	sys_dlist_t  thread_timer_q;    /* 厂商线程定时器队列，按CONFIG_THREAD_TIMER保留。 */
#endif

#if defined(CONFIG_USE_SWITCH)
	/* 新式USE_SWITCH把原架构私有的返回值和上下文句柄提升为公共内核字段。 */

	/** z_swap恢复后交给阻塞API的返回状态，唤醒者可写成功/错误。 */
	int swap_retval;

	/** arch_switch发布的可恢复上下文句柄；SMP活动线程保持NULL。 */
	void *switch_handle;
#endif
	/** 本线程分配资源时使用的堆指针，可共享；修改指针不迁移既有分配。 */
	struct k_heap *resource_pool;

#if defined(CONFIG_THREAD_LOCAL_STORAGE)
	/* 架构定义的线程本地存储区域地址。 */
	uintptr_t tls;
#endif    /* CONFIG_THREAD_LOCAL_STORAGE */

#ifdef CONFIG_THREAD_RUNTIME_STATS
	/** 当前线程执行累计量与上次切入时间戳。 */
	struct _thread_runtime_stats rt_stats;
#endif

#ifdef CONFIG_DEMAND_PAGING_THREAD_STATS
	/** 按需分页产生的每线程统计。 */
	struct k_mem_paging_stats_t paging_stats;
#endif

	/** 架构专有线程状态，布局约定要求始终位于结构末尾。 */
	struct _thread_arch arch;
};

typedef struct k_thread _thread_t;    /* 完整线程控制块类型别名。 */
typedef struct k_thread *k_tid_t;     /* 线程句柄直接为控制块指针，不是整数ID或栈地址。 */

#endif
