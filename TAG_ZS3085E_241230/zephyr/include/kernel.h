/* 包含保护避免重复声明；_ASMLANGUAGE 分支允许汇编读取线程选项，C/C++ 分支提供类型和函数。kernel_includes 提供线程、队列、堆等类型，尾部 syscall 头由构建生成，本次不修改生成物。 */
/*
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 内核公共接口：线程、超时、同步、队列、工作项及内存管理。SPI 驱动通过信号量等待，测试命令通过休眠和计时接口控制节奏。 */

#ifndef ZEPHYR_INCLUDE_KERNEL_H_
#define ZEPHYR_INCLUDE_KERNEL_H_

/* 当前目标使用单核 Cortex-M33、TIMEOUT_64BIT、SCHED_DUMB、WAITQ_DUMB 和 1000 tick/s；SMP、USERSPACE、SCHED_DEADLINE、SCHED_CPU_MASK 分支仅作可选接口保留。 */
#if !defined(_ASMLANGUAGE)
#include <kernel_includes.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <toolchain.h>
#include <tracing/tracing_macros.h>

#ifdef CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS
#include <timing/timing.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 内核公共接口。
 * @defgroup kernel_apis 内核接口
 * @{
 * @}
 */

/* 邮箱来源或目标线程的通配标识，值为 NULL；具体匹配由邮箱接口处理。 */
#define K_ANY NULL
/* 以 NULL 表示结束标记，使用含义由具体内核接口决定。 */
#define K_END NULL

#if CONFIG_NUM_COOP_PRIORITIES + CONFIG_NUM_PREEMPT_PRIORITIES == 0
#error Zero available thread priorities defined!
#endif

/* 把从 0 起的协作优先级序号映射到负数优先级区间；x 必须位于协作配置范围。 */
#define K_PRIO_COOP(x) (-(CONFIG_NUM_COOP_PRIORITIES - (x)))
/* 抢占优先级直接使用非负序号；x 必须小于可用抢占优先级数。 */
#define K_PRIO_PREEMPT(x) (x)

/* 最高线程优先级为协作优先级区间的最小数值。 */
#define K_HIGHEST_THREAD_PRIO (-CONFIG_NUM_COOP_PRIORITIES)
/* 最低线程优先级槽保留给 idle；应用最低等级另由 APPLICATION 宏排除此槽。 */
#define K_LOWEST_THREAD_PRIO CONFIG_NUM_PREEMPT_PRIORITIES
/* idle 固定使用内核最低优先级。 */
#define K_IDLE_PRIO K_LOWEST_THREAD_PRIO
/* 应用可使用的最高优先级，与线程最高优先级相同。 */
#define K_HIGHEST_APPLICATION_THREAD_PRIO (K_HIGHEST_THREAD_PRIO)
/* 应用最低优先级比 idle 高一级，避免与空闲线程同级。 */
#define K_LOWEST_APPLICATION_THREAD_PRIO (K_LOWEST_THREAD_PRIO - 1)

#ifdef CONFIG_POLL
/* POLL 分支初始化对象内部的轮询等待事件链表；未启用分支展开为空。 */
#define _POLL_EVENT_OBJ_INIT(obj) \
	.poll_events = SYS_DLIST_STATIC_INIT(&obj.poll_events),
/* POLL 分支把事件链表成员嵌入同步对象；未启用时不增加成员。 */
#define _POLL_EVENT sys_dlist_t poll_events
#else
/* POLL 分支初始化对象内部的轮询等待事件链表；未启用分支展开为空。 */
#define _POLL_EVENT_OBJ_INIT(obj)
#define _POLL_EVENT
#endif

/* 下面仅前向声明各类内核对象，使指针接口可提前引用；完整布局由各结构定义提供。 */
struct k_thread;
struct k_mutex;
struct k_sem;
struct k_msgq;
struct k_mbox;
struct k_pipe;
struct k_queue;
struct k_fifo;
struct k_lifo;
struct k_stack;
struct k_mem_slab;
struct k_mem_pool;
struct k_timer;
struct k_poll_event;
struct k_poll_signal;
struct k_mem_domain;
struct k_mem_partition;
struct k_futex;

enum execution_context_types {
	K_ISR = 0,           /* 中断上下文类别。 */
	K_COOP_THREAD,       /* 协作线程类别，普通抢占线程不能直接抢占它。 */
	K_PREEMPT_THREAD,    /* 可抢占线程类别。 */
};

/* k_poll 与 k_work_poll 共用的内部轮询回调：event 为被通知事件，state 为新状态；返回值供内部轮询处理使用。 */
struct k_work_poll;
typedef int (*_poller_cb_t)(struct k_poll_event *event, uint32_t state);

/**
 * @addtogroup thread_apis
 * @{
 */

/* 线程遍历回调：thread 为本轮只读线程对象，user_data 为调用方上下文；无返回值。锁内/锁外执行约定由 foreach 入口决定。 */
typedef void (*k_thread_user_cb_t)(const struct k_thread *thread,
				   void *user_data);

/**
  * @brief  持线程监控锁遍历全部线程。
  * @param  user_cb：[输入]每个线程调用一次的非空回调，接收只读线程指针和 user_data。
  * @param  user_data：[输入]原样传给回调的应用上下文，可为空。
  * @retval 无。
  * @note   需要 CONFIG_THREAD_MONITOR；持自旋锁期间线程创建和终止受到阻塞，回调不得阻塞或执行依赖该锁的操作。
  */
extern void k_thread_foreach(k_thread_user_cb_t user_cb, void *user_data);

/**
  * @brief  遍历线程，并在执行应用回调时释放监控锁。
  * @param  user_cb：[输入]非空遍历回调；其线程参数只在相应对象仍有效时可访问。
  * @param  user_data：[输入]透传上下文，可为空。
  * @retval 无。
  * @note   需要 CONFIG_THREAD_MONITOR；遍历期间新建线程可能遗漏，已终止线程仍可能被回调。遍历完成前不得复用其中已终止线程的控制块内存，否则可能追随失效链指针并导致异常或无法返回。
  */
extern void k_thread_foreach_unlocked(
	k_thread_user_cb_t user_cb, void *user_data);

/** @} */

/**
 * @defgroup thread_apis 线程接口
 * @ingroup kernel_apis
 * @{
 */

#endif    /* !_ASMLANGUAGE */


/* 线程选项低位供公共内核使用，高位留给架构扩展；汇编也会使用这些宏。 */

/* 标记不可被正常中止的系统关键线程。 */
#define K_ESSENTIAL (BIT(0))

#if defined(CONFIG_FPU_SHARING)
/* 线程使用浮点寄存器，需要切换代码保存和恢复 FPU 现场；只在 CONFIG_FPU_SHARING 下定义。 */
#define K_FP_REGS (BIT(1))
#endif

/* 用户模式线程选项；线程降权后受用户态访问限制。 */
#define K_USER (BIT(2))

/* 创建线程时继承创建者的内核对象访问权限；仅 CONFIG_USERSPACE 有效。 */
#define K_INHERIT_PERMS (BIT(3))

/* 为回调管理器保留一个线程状态位，记录当前是否位于应用回调中，相当于一个低成本线程局部标志。 */
#define K_CALLBACK_STATE (BIT(4))

#ifdef CONFIG_X86
/* 以下为 x86 专用线程选项，当前 ARM 目标不使用。 */

#if defined(CONFIG_FPU_SHARING) && defined(CONFIG_X86_SSE)
/* x86 浮点及 SSE 寄存器由线程切换代码保存和恢复，仅在 FPU 共享和 X86_SSE 分支定义。 */
#define K_SSE_REGS (BIT(7))
#endif
#endif

/* 公共及架构线程选项定义结束。 */

#if !defined(_ASMLANGUAGE)

/**
  * @brief  初始化线程控制块和栈，并按指定延时使线程参与调度。
  * @param  new_thread：[输出]尚未初始化的非空线程控制块，在线程存续期间保持有效。
  * @param  stack：[输入/输出]使用 K_THREAD_STACK_DEFINE 或 K_KERNEL_STACK_DEFINE 声明的非空栈对象，不可提前释放。
  * @param  stack_size：[输入]可用栈大小，字节；使用声明宏的原始容量或对应 STACK_SIZEOF 值，不能随意用 sizeof(stack)。
  * @param  entry：[输入]非空线程入口函数。
  * @param  p1：[输入]入口第一个参数，按入口约定允许为空。
  * @param  p2：[输入]入口第二个参数，按入口约定允许为空。
  * @param  p3：[输入]入口第三个参数，按入口约定允许为空。
  * @param  prio：[输入]线程优先级，数值越小优先级越高，须在配置范围内。
  * @param  options：[输入]架构支持的线程选项位或组合。
  * @param  delay：[输入]启动延时对象，K_NO_WAIT 立即就绪，K_FOREVER 等待显式启动。
  * @retval 新线程的 k_tid_t 标识。
  * @note   立即启动时，新线程若更优先可在本调用返回前抢占调用者；用户栈与仅内核栈的内存布局不同，不能混用容量宏。
  */
__syscall k_tid_t k_thread_create(struct k_thread *new_thread,
				  k_thread_stack_t *stack,
				  size_t stack_size,
				  k_thread_entry_t entry,
				  void *p1, void *p2, void *p3,
				  int prio, uint32_t options, k_timeout_t delay);

/**
  * @brief  不可逆地把当前特权线程转换为用户线程并转入指定入口。
  * @param  entry：[输入]非空用户线程入口。
  * @param  p1：[输入]入口参数一，可按入口约定为空。
  * @param  p2：[输入]入口参数二，可按入口约定为空。
  * @param  p3：[输入]入口参数三，可按入口约定为空。
  * @retval 不返回调用点。
  * @note   转换前清空栈内容并重建线程局部存储；保留内存域、资源池、对象权限、优先级及线程选项。
  */
extern FUNC_NORETURN void k_thread_user_mode_enter(k_thread_entry_t entry,
						   void *p1, void *p2,
						   void *p3);

/**
  * @brief  向已初始化线程授予若干内核对象的访问权限。
  * @param  thread：[输入]非空已初始化线程对象。
  * @param  ...：[输入]一个或多个非空内核对象指针，对象本身可尚未初始化。
  * @retval 无。
  * @note   宏按参数逐个调用 k_object_access_grant；不允许以 NULL 充当对象参数。
  */
#define k_thread_access_grant(thread, ...) \
	FOR_EACH_FIXED_ARG(k_object_access_grant, (;), thread, __VA_ARGS__)

/**
  * @brief  为线程指定后续资源申请所用的堆。
  * @param  thread：[输入/输出]非空线程控制块。
  * @param  heap：[输入]堆对象地址，NULL 表示取消资源池分配。
  * @retval 无。
  * @note   默认继承父线程资源池；多个线程可共享同一堆。此操作不迁移旧堆中的已有分配。
  */
static inline void k_thread_heap_assign(struct k_thread *thread,
					struct k_heap *heap)
{
	thread->resource_pool = heap;    /* 替换后续资源申请的堆指针，不迁移旧分配。 */
}

#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)

/**
  * @brief  检查线程栈并返回未使用空间。
  * @param  thread：[输入]非空有效线程，用户态须有访问权限。
  * @param  unused_ptr：[输出]非空可写 size_t 对象，接收未用字节数。
  * @retval 0 成功；用户态可能返回 -EBADF 对象不正确、-EPERM 权限不足、-EINVAL 未初始化或已退出、-EFAULT 输出地址错误；-ENOTSUP 表示硬件策略禁止检查。
  * @note   仅 INIT_STACKS 和 THREAD_STACK_INFO 同时启用时声明；部分硬件禁止检查正在使用的当前线程栈。
  */
__syscall int k_thread_stack_space_get(const struct k_thread *thread,
				       size_t *unused_ptr);
#endif

#if (CONFIG_HEAP_MEM_POOL_SIZE > 0)

/**
  * @brief  将系统内核堆指定为线程的资源池。
  * @param  thread：[输入/输出]非空目标线程对象。
  * @retval 无。
  * @note   仅系统堆容量大于零时声明；不受限的线程申请可能耗尽系统堆。
  */
void k_thread_system_pool_assign(struct k_thread *thread);
#endif    /* (CONFIG_HEAP_MEM_POOL_SIZE > 0) */

/**
  * @brief  等待目标线程退出，并处理立即返回或死锁情况。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 0：目标已死亡或等待完成；-EBUSY：不等待；-EDEADLK：自等待或直接互等；超时通常-EAGAIN。
  * @note   ISR仅可走不阻塞路径；在join队列排队后由z_swap交接调度锁。
  */
__syscall int k_thread_join(struct k_thread *thread, k_timeout_t timeout);

/**
  * @brief  让当前线程按超时对象睡眠，并把剩余量转换为毫秒。
  * @param  timeout：[输入] k_timeout_t等待预算；K_FOREVER不安排有限超时。
  * @retval 常规预算到期为0，提前唤醒为剩余毫秒向下取整；K_FOREVER恢复后返回K_TICKS_FOREVER的int32_t值。
  * @note   线程上下文；K_FOREVER通过suspend实现，保留本实现与公共文档可能不同的无限等待返回值。
  */
__syscall int32_t k_sleep(k_timeout_t timeout);

/**
  * @brief  把当前线程休眠时间从毫秒转换为内核超时对象。
  * @param  ms：[输入]期望休眠时间，毫秒，使用非负有限值。
  * @retval 计时到期返回 0；提前被 k_wakeup 唤醒时返回剩余毫秒数。
  * @note   内部调用 k_sleep；线程会让出 CPU，实际唤醒时刻受 tick 分辨率和调度延迟影响，不允许在中断里阻塞。
  */
static inline int32_t k_msleep(int32_t ms)
{
	return k_sleep(Z_TIMEOUT_MS(ms));    /* 把毫秒请求编码成超时对象，再进入让出 CPU 的休眠路径。 */
}

/**
  * @brief  把微秒请求向上换算tick后挂起当前线程。
  * @param  us：[输入] 希望睡眠的时长，单位微秒；非负，实际精度受tick约束。
  * @retval 到期0，提前唤醒时为剩余微秒向下换算的int32_t值。
  * @note   线程上下文，精度受tick限制，不能把微秒参数当成微秒级硬件唤醒保证。
  */
__syscall int32_t k_usleep(int32_t us);

/**
  * @brief  按微秒数忙等，期间不主动让出 CPU。
  * @param  usec_to_wait：[输入]忙等长度，微秒。
  * @retval 无。
  * @note   硬件延时钟与系统超时钟可能存在频偏，因此 busy_wait(1000) 与 sleep(K_MSEC(1)) 的实际时间未必完全相同。
  */
__syscall void k_busy_wait(uint32_t usec_to_wait);

/**
  * @brief  将当前线程排到同优先级末尾并主动重新选择运行者。
  * @param  无。
  * @retval 无。
  * @note   线程上下文调用；没有更合适候选时可继续执行当前线程，不保证其他线程必运行。
  */
__syscall void k_yield(void);

/**
  * @brief  取消普通睡眠的超时并清挂起状态，使线程重新可运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   对象等待中的PENDING线程不在此唤醒；ISR只更新状态，线程上下文随后重调度。
  */
__syscall void k_wakeup(k_tid_t thread);

/**
  * @brief  读取本CPU当前线程对象地址。
  * @param  无。
  * @retval 当前k_tid_t线程句柄，不转移所有权。
  * @note   SMP读取期间屏蔽本CPU普通中断以避免抢占迁移带来的不一致。
  */
__attribute_const__
__syscall k_tid_t z_current_get(void);

#ifdef CONFIG_THREAD_LOCAL_STORAGE
/* 当前线程标识的线程局部缓存，在 z_thread_entry 中设置。 */
extern __thread k_tid_t z_tls_current;
#endif

/**
  * @brief  查询调用线程的标识。
  * @param  无。
  * @retval 当前线程的 k_tid_t。
  * @note   启用线程局部存储时直接读缓存；否则调用 z_current_get。线程语境使用。
  */
__attribute_const__
static inline k_tid_t k_current_get(void)
{
#ifdef CONFIG_THREAD_LOCAL_STORAGE
	return z_tls_current;      /* 启用线程局部存储时直接读取当前线程缓存。 */
#else
	return z_current_get();    /* 没有线程局部缓存时经内核接口查询当前线程。 */
#endif
}

/**
  * @brief  为通用线程终止入口添加跟踪并调用调度终止。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无；终止当前线程后不应继续执行。
  * @note   仅未提供CONFIG_ARCH_HAS_THREAD_ABORT的架构编译此实现。
  */
__syscall void k_thread_abort(k_tid_t thread);


/**
  * @brief  使此前延后启动的线程就绪。
  * @param  thread：[输入/输出]非空已创建线程标识。
  * @retval 无。
  * @note   适用于用 K_FOREVER 创建而尚未启动的线程；就绪后何时运行取决于调度。
  */
__syscall void k_thread_start(k_tid_t thread);

/**
  * @brief  查询超时项相对系统启动的截止tick。
  * @param  timeout：[输入] 非空有效超时对象。
  * @retval curr_tick加timeout_rem的结果；未激活时该实现返回curr_tick。
  * @note   锁内快照；保留实际公式，不把结果描述为始终含硬件elapsed的绝对时刻。
  */
extern k_ticks_t z_timeout_expires(const struct _timeout *timeout);

/**
  * @brief  查询指定超时项的剩余tick。
  * @param  timeout：[输入] 非空有效超时对象。
  * @retval 未激活返回0，否则返回锁内计算的剩余tick快照。
  * @note   查询不改变队列；离开锁后时间与状态可立即变化。
  */
extern k_ticks_t z_timeout_remaining(const struct _timeout *timeout);

#ifdef CONFIG_SYS_CLOCK_EXISTS

/**
  * @brief  读取线程等待超时的到期 tick 查询值。
  * @param  t：[输入]非空有效线程控制块，只读取其超时节点。
  * @retval z_timeout_expires 对该线程超时节点的查询结果，单位 tick。
  * @note   到期查询按 timeout.c 当前公式计算，不能据此保证线程将在该时刻实际获得 CPU。
  */
__syscall k_ticks_t k_thread_timeout_expires_ticks(const struct k_thread *t);

/**
  * @brief  读取线程等待超时的到期 tick 查询值。
  * @param  t：[输入]非空有效线程控制块，只读取其超时节点。
  * @retval z_timeout_expires 对该线程超时节点的查询结果，单位 tick。
  * @note   到期查询按 timeout.c 当前公式计算，不能据此保证线程将在该时刻实际获得 CPU。
  */
static inline k_ticks_t z_impl_k_thread_timeout_expires_ticks(
						const struct k_thread *t)
{


	return z_timeout_expires(&t->base.timeout);    /* 针对该线程的超时节点查询当前实现计算的到期 tick 值。 */
}

/**
  * @brief  读取线程等待超时的剩余 tick 数。
  * @param  t：[输入]非空有效线程控制块。
  * @retval 剩余 tick 数；无活动超时返回 0。
  * @note   返回的是定时队列剩余时间，不含到期后的调度等待。
  */
__syscall k_ticks_t k_thread_timeout_remaining_ticks(const struct k_thread *t);

/**
  * @brief  读取线程等待超时的剩余 tick 数。
  * @param  t：[输入]非空有效线程控制块。
  * @retval 剩余 tick 数；无活动超时返回 0。
  * @note   返回的是定时队列剩余时间，不含到期后的调度等待。
  */
static inline k_ticks_t z_impl_k_thread_timeout_remaining_ticks(
						const struct k_thread *t)
{


	return z_timeout_remaining(&t->base.timeout);    /* 查询该线程超时节点剩余的 tick 数。 */
}

#endif    /* CONFIG_SYS_CLOCK_EXISTS */

/**
 * @cond INTERNAL_HIDDEN
 */

/* 超时已发生且节点已离开超时队列的内部标记值。 */
#define _EXPIRED (-2)

struct _static_thread_data {
	struct k_thread *init_thread;    /* 目标线程控制块地址。 */
	k_thread_stack_t *init_stack;    /* 线程专用栈地址，启动后仍由线程使用。 */
	unsigned int init_stack_size;    /* 启动记录中的栈容量，字节。 */
	k_thread_entry_t init_entry;     /* 线程入口回调，接收 p1、p2、p3，无返回值。 */
	void *init_p1;                   /* 传入入口的第一个上下文参数。 */
	void *init_p2;                   /* 传入入口的第二个上下文参数。 */
	void *init_p3;                   /* 传入入口的第三个上下文参数。 */
	int init_prio;                   /* 初始调度优先级，数值越小越优先。 */
	uint32_t init_options;           /* 创建时的线程选项位组合。 */
	int32_t init_delay;              /* 旧版静态启动记录的延时，毫秒。 */
	void (*init_abort)(void);        /* 可选的线程中止回调，无参数、无返回值；具体触发时机由线程模块决定。 */
	const char *init_name;           /* 线程名称字符串。 */
};

/* 生成静态线程记录：绑定控制块、栈容量、入口及三个参数、优先级、选项、延时、中止回调和字符串化名称；不在宏内启动调度。 */
#define Z_THREAD_INITIALIZER(thread, stack, stack_size,           \
			    entry, p1, p2, p3,                   \
			    prio, options, delay, abort, tname)  \
	{                                                        \
	.init_thread = (thread),				 \
	.init_stack = (stack),					 \
	.init_stack_size = (stack_size),                         \
	.init_entry = (k_thread_entry_t)entry,			 \
	.init_p1 = (void *)p1,                                   \
	.init_p2 = (void *)p2,                                   \
	.init_p3 = (void *)p3,                                   \
	.init_prio = (prio),                                     \
	.init_options = (options),                               \
	.init_delay = (delay),                                   \
	.init_abort = (abort),                                   \
	.init_name = STRINGIFY(tname),                           \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
  * @brief  静态分配线程控制块、栈和启动记录。
  * @param  name：[输入]线程标识符，生成 const k_tid_t 名称。
  * @param  stack_size：[输入]栈容量，字节。
  * @param  entry：[输入]线程入口。
  * @param  p1：[输入]入口参数一。
  * @param  p2：[输入]入口参数二。
  * @param  p3：[输入]入口参数三。
  * @param  prio：[输入]合法线程优先级。
  * @param  options：[输入]架构支持的选项位组合。
  * @param  delay：[输入]本版本启动记录使用的延时值，毫秒；0 表示不延时。
  * @retval 无。
  * @note   初始化器进入可迭代链接段，由启动流程创建线程；外部可 extern const k_tid_t name 引用。原 SDK 注释要求减少 x86 启动记录对齐浪费，实际对齐由可迭代段宏及链接规则决定。
  */
#define K_THREAD_DEFINE(name, stack_size,                                \
			entry, p1, p2, p3,                               \
			prio, options, delay)                            \
	K_THREAD_STACK_DEFINE(_k_thread_stack_##name, stack_size);	 \
	struct k_thread _k_thread_obj_##name;				 \
	STRUCT_SECTION_ITERABLE(_static_thread_data, _k_thread_data_##name) = \
		Z_THREAD_INITIALIZER(&_k_thread_obj_##name,		 \
				    _k_thread_stack_##name, stack_size,  \
				entry, p1, p2, p3, prio, options, delay, \
				NULL, name);				 	 \
	const k_tid_t name = (k_tid_t)&_k_thread_obj_##name

/**
  * @brief  读取线程的基础优先级数值。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 当前base.prio，数值越小优先级越高。
  * @note   瞬时读取，不保证返回后优先级不变化。
  */
__syscall int k_thread_priority_get(k_tid_t thread);

/**
  * @brief  检查调用上下文和合法优先级后更新目标线程。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval 无。
  * @note   不能从ISR调用；不允许修改idle为普通应用优先级。
  */
__syscall void k_thread_priority_set(k_tid_t thread, int prio);


#ifdef CONFIG_SCHED_DEADLINE

/**
  * @brief  按当前cycle加相对预算设置线程截止排序键。
  * @param  thread：[输入输出] 非空有效目标线程句柄。
  * @param  deadline：[输入] 相对截止距离，单位硬件cycle；须为正且队列截止跨度小于2^31周期。
  * @retval 无。
  * @note   仅CONFIG_SCHED_DEADLINE编译；同基础优先级的辅助排序，不是硬件到期通知，也不保证实时截止。
  */
__syscall void k_thread_deadline_set(k_tid_t thread, int deadline);
#endif

#ifdef CONFIG_SCHED_CPU_MASK

/**
  * @brief  清除线程允许运行的全部CPU位。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   需先使目标处于不能运行状态，否则无法更改亲和设置。
  */
int k_thread_cpu_mask_clear(k_tid_t thread);

/**
  * @brief  允许目标线程在掩码可表示的所有CPU运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   只修改亲和掩码，不自动启动/恢复线程。
  */
int k_thread_cpu_mask_enable_all(k_tid_t thread);

/**
  * @brief  允许目标线程在指定CPU运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  cpu：[输入] 合法CPU编号，须处于当前配置及掩码表示范围。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   调用方保证cpu为合法CPU编号和移位范围，本包装不检查。
  */
int k_thread_cpu_mask_enable(k_tid_t thread, int cpu);

/**
  * @brief  禁止目标线程在指定CPU运行。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @param  cpu：[输入] 合法CPU编号，须处于当前配置及掩码表示范围。
  * @retval 0：成功；-EINVAL：目标仍可运行。
  * @note   调用方保证cpu合法；掩码清空后线程将无可运行CPU。
  */
int k_thread_cpu_mask_disable(k_tid_t thread, int cpu);
#endif

/**
  * @brief  取消线程定时等待并设置挂起状态。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   挂起当前线程会主动重调度；不释放线程对象/栈。调用者维护生命周期及接口上下文约束。
  */
__syscall void k_thread_suspend(k_tid_t thread);

/**
  * @brief  清除线程挂起标志并重新评估其可运行条件。
  * @param  thread：[输入输出] 非空有效线程对象，存储须在本操作及异步排队期间保持有效。
  * @retval 无。
  * @note   非挂起线程立即返回；其他等待条件仍可能阻止其就绪，内部锁最终交给z_reschedule释放。
  */
__syscall void k_thread_resume(k_tid_t thread);

/**
  * @brief  配置时间片长度及参与轮转的优先级门限。
  * @param  slice：[输入] 时间片长度，单位毫秒；0关闭时间片，负值不由本实现显式检查。
  * @param  prio：[输入] 基础优先级数值，越小越优先；时间片接口中表示参与轮转的门限。
  * @retval 无。
  * @note   仅CONFIG_TIMESLICING编译；tickless正时间片至少2 tick，内部锁住调度状态。
  */
extern void k_sched_time_slice_set(int32_t slice, int prio);

/** @} */

/**
 * @addtogroup isr_apis
 * @{
 */

/**
  * @brief  判断当前执行环境是否为中断服务程序。
  * @param  无。
  * @retval 中断上下文返回 true，线程上下文返回 false。
  * @note   可在中断内查询，用于选择允许的同步和等待方式。
  */
extern bool k_is_in_isr(void);

/**
  * @brief  判断当前执行上下文是否为可抢占线程。
  * @param  无。
  * @retval 非0：不在ISR且线程preempt编码允许；否则0。
  * @note   不会改变调度锁状态。
  */
__syscall int k_is_preempt_thread(void);

/**
  * @brief  判断内核是否仍处于 POST_KERNEL 初始化之前。
  * @param  无。
  * @retval z_sys_post_kernel 尚未置位返回 true，否则 false。
  * @note   仅查询启动阶段标志，可从中断调用，不表示所有设备均已初始化完成。
  */
static inline bool k_is_pre_kernel(void)
{
	extern bool z_sys_post_kernel;    /* 该启动阶段标志的定义在 kernel/init.c。 */

	return !z_sys_post_kernel;        /* 标志尚未置位时报告仍处于 POST_KERNEL 之前。 */
}

/**
 * @}
 */

/**
 * @addtogroup thread_apis
 * @{
 */

/**
  * @brief  增加当前线程的调度锁嵌套，暂禁普通线程抢占。
  * @param  无。
  * @retval 无。
  * @note   线程上下文调用；调度锁不同于中断屏蔽，不能据此保证硬件寄存器互斥。
  */
extern void k_sched_lock(void);

/**
  * @brief  减少当前线程调度锁嵌套并重新评估调度。
  * @param  无。
  * @retval 无。
  * @note   必须与k_sched_lock匹配且不在ISR，最后一层解锁后可被就绪高优先级线程切走。
  */
extern void k_sched_unlock(void);

/**
  * @brief  设置当前线程自用的上下文指针。
  * @param  value：[输入]应用自定义指针，可为空；内核仅保存此值，不管理指向对象的生命周期。
  * @retval 无。
  * @note   内核本身不解释该数据，可用于构建线程局部状态。
  */
__syscall void k_thread_custom_data_set(void *value);

/**
  * @brief  取回当前线程自用的上下文指针。
  * @param  无。
  * @retval 此前设置的自定义指针。
  * @note   返回指针的所有权不转移。
  */
__syscall void *k_thread_custom_data_get(void);

/**
  * @brief  为指定线程设置可供调试和跟踪使用的名称。
  * @param  thread：[输入]目标线程标识，NULL 表示当前线程。
  * @param  str：[输入]非空、以零结束的可读名称字符串，长度须符合线程名称配置。
  * @retval 0 成功；-EFAULT 字符串访问错误；-ENOSYS 未启用线程命名；-EINVAL 名称过长。
  */
__syscall int k_thread_name_set(k_tid_t thread, const char *str);

/**
  * @brief  读取线程名称的内部存储地址。
  * @param  thread：[输入]非空目标线程标识。
  * @retval 线程名称指针；未启用该功能返回 NULL。
  * @note   不复制字符串；调用方不得释放或改写内部存储。
  */
const char *k_thread_name_get(k_tid_t thread);

/**
  * @brief  将线程名称复制到调用方缓冲区。
  * @param  thread：[输入]非空目标线程标识。
  * @param  buf：[输出]非空可写目标缓冲区，容量至少 size 字节。
  * @param  size：[输入]目标容量，字节，须容纳名称和结束零。
  * @retval 0 成功；-ENOSPC 容量不足；-EFAULT 地址访问错误；-ENOSYS 未启用线程命名。
  */
__syscall int k_thread_name_copy(k_tid_t thread, char *buf,
				 size_t size);

/**
  * @brief  取得便于调试的线程状态文字。
  * @param  thread_id：[输入]非空有效线程标识。
  * @retval 状态字符串地址；未设置状态标志时为空字符串。
  * @note   返回内部字符串，不转移所有权。
  */
const char *k_thread_state_str(k_tid_t thread_id);

/**
 * @}
 */

/**
 * @addtogroup clock_apis
 * @{
 */

/**
  * @brief  生成不允许阻塞等待的超时值。
  * @param  无。
  * @retval Z_TIMEOUT_NO_WAIT 超时对象。
  * @note   操作不能立即满足时，由对应内核 API 返回其非阻塞失败结果。
  */
#define K_NO_WAIT Z_TIMEOUT_NO_WAIT

/**
  * @brief  按纳秒生成相对超时值。
  * @param  t：[输入]非负等待时长，单位纳秒。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_NSEC(t)     Z_TIMEOUT_NS(t)

/**
  * @brief  按微秒生成相对超时值。
  * @param  t：[输入]非负等待时长，单位微秒。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_USEC(t)     Z_TIMEOUT_US(t)

/**
  * @brief  按硬件周期生成相对超时值。
  * @param  t：[输入]非负等待时长，单位硬件周期。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_CYC(t)     Z_TIMEOUT_CYC(t)

/**
  * @brief  按系统 tick生成相对超时值。
  * @param  t：[输入]非负等待时长，单位系统 tick。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_TICKS(t)     Z_TIMEOUT_TICKS(t)

/**
  * @brief  按毫秒生成相对超时值。
  * @param  ms：[输入]非负等待时长，单位毫秒。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_MSEC(ms)     Z_TIMEOUT_MS(ms)

/**
  * @brief  按秒生成相对超时值。
  * @param  s：[输入]非负等待时长，单位秒。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_SECONDS(s)   K_MSEC((s) * MSEC_PER_SEC)

/**
  * @brief  按分钟生成相对超时值。
  * @param  m：[输入]非负等待时长，单位分钟。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_MINUTES(m)   K_SECONDS((m) * 60)

/**
  * @brief  按小时生成相对超时值。
  * @param  h：[输入]非负等待时长，单位小时。
  * @retval 可传给内核等待接口的超时对象。
  * @note   最终分辨率受系统 tick 限制；较大单位宏先做整数乘法，调用方须避免计算溢出。
  */
#define K_HOURS(h)     K_MINUTES((h) * 60)

/**
  * @brief  生成永久等待的特殊超时值。
  * @param  无。
  * @retval Z_FOREVER 超时对象。
  * @note   含义由具体 API 决定：同步对象可永久等待，线程创建可延后到显式启动。
  */
#define K_FOREVER Z_FOREVER

#ifdef CONFIG_TIMEOUT_64BIT

/**
  * @brief  按启动后累计系统 tick数生成绝对到期超时值。
  * @param  t：[输入]从系统启动计起的目标时间，单位系统 tick。
  * @retval 编码了绝对到期时刻的超时对象。
  * @note   仅 CONFIG_TIMEOUT_64BIT 分支提供；实际时间精度受 tick 限制，单位转换向上取整，tick 宏把负目标裁到 0。
  */
#define K_TIMEOUT_ABS_TICKS(t) \
	Z_TIMEOUT_TICKS(Z_TICK_ABS((k_ticks_t)MAX(t, 0)))

/**
  * @brief  按启动后累计毫秒数生成绝对到期超时值。
  * @param  t：[输入]从系统启动计起的目标时间，单位毫秒。
  * @retval 编码了绝对到期时刻的超时对象。
  * @note   仅 CONFIG_TIMEOUT_64BIT 分支提供；实际时间精度受 tick 限制，单位转换向上取整，tick 宏把负目标裁到 0。
  */
#define K_TIMEOUT_ABS_MS(t) K_TIMEOUT_ABS_TICKS(k_ms_to_ticks_ceil64(t))

/**
  * @brief  按启动后累计微秒数生成绝对到期超时值。
  * @param  t：[输入]从系统启动计起的目标时间，单位微秒。
  * @retval 编码了绝对到期时刻的超时对象。
  * @note   仅 CONFIG_TIMEOUT_64BIT 分支提供；实际时间精度受 tick 限制，单位转换向上取整，tick 宏把负目标裁到 0。
  */
#define K_TIMEOUT_ABS_US(t) K_TIMEOUT_ABS_TICKS(k_us_to_ticks_ceil64(t))

/**
  * @brief  按启动后累计纳秒数生成绝对到期超时值。
  * @param  t：[输入]从系统启动计起的目标时间，单位纳秒。
  * @retval 编码了绝对到期时刻的超时对象。
  * @note   仅 CONFIG_TIMEOUT_64BIT 分支提供；实际时间精度受 tick 限制，单位转换向上取整，tick 宏把负目标裁到 0。
  */
#define K_TIMEOUT_ABS_NS(t) K_TIMEOUT_ABS_TICKS(k_ns_to_ticks_ceil64(t))

/**
  * @brief  按启动后累计硬件周期数生成绝对到期超时值。
  * @param  t：[输入]从系统启动计起的目标时间，单位硬件周期。
  * @retval 编码了绝对到期时刻的超时对象。
  * @note   仅 CONFIG_TIMEOUT_64BIT 分支提供；实际时间精度受 tick 限制，单位转换向上取整，tick 宏把负目标裁到 0。
  */
#define K_TIMEOUT_ABS_CYC(t) K_TIMEOUT_ABS_TICKS(k_cyc_to_ticks_ceil64(t))

#endif

/**
 * @}
 */

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_timer {
	/* 超时结构必须放首成员以支持动态定时器布局；timeout.node 还供空闲定时器双向链表使用。 */
	struct _timeout timeout;

	/* 等待此定时器状态的线程队列；本接口约定只支持单个同步等待者。 */
	_wait_q_t wait_q;

	/* 到期回调：系统时钟中断调用，timer 指向本定时器，无返回值；不可阻塞。 */
	void (*expiry_fn)(struct k_timer *timer);

	/* 提前停止回调：由 k_timer_stop 的调用者同步调用，timer 指向本对象，无返回值；调用者可能处于中断。 */
	void (*stop_fn)(struct k_timer *timer);

	/* 定时器周期超时对象，用于到期后重新装载。 */
	k_timeout_t period;

	/* 自上次读取并清零后累计的到期次数。 */
	uint32_t status;

	/* 应用上下文指针，也为旧兼容接口提供存储。 */
	void *user_data;

};

/* 构造静态定时器：超时回调绑定 z_timer_expiration_handler，等待队列为空，应用到期/停止回调来自参数，周期、状态及应用上下文先清零。 */
#define Z_TIMER_INITIALIZER(obj, expiry, stop) \
	{ \
	.timeout = { \
		.node = {},\
		.fn = z_timer_expiration_handler, \
		.dticks = 0, \
	}, \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q), \
	.expiry_fn = expiry, \
	.stop_fn = stop, \
	.status = 0, \
	.user_data = 0, \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup timer_apis 定时器接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  定义定时器到期回调的函数类型。
  * @param  timer：[输入/输出]非空、已初始化的到期定时器对象。
  * @retval 无。
  * @note   系统时钟中断每次到期时调用；可以不注册此回调，回调不得阻塞。
  */
typedef void (*k_timer_expiry_t)(struct k_timer *timer);

/**
  * @brief  定义定时器提前停止回调的函数类型。
  * @param  timer：[输入/输出]非空、正在被提前停止的定时器。
  * @retval 无。
  * @note   只有注册后才调用，执行于停止操作的调用上下文；停止操作可能由中断发起，回调须适用于中断。
  */
typedef void (*k_timer_stop_t)(struct k_timer *timer);

/**
  * @brief  静态定义并初始化一个定时器对象。
  * @param  name：[输入]定时器对象标识符，外部可按 struct k_timer 声明引用。
  * @param  expiry_fn：[输入]到期回调或 NULL。
  * @param  stop_fn：[输入]提前停止回调或 NULL。
  * @retval 无。
  * @note   记录放入内核可迭代对象段；定义本身不启动计时。
  */
#define K_TIMER_DEFINE(name, expiry_fn, stop_fn) \
	STRUCT_SECTION_ITERABLE(k_timer, name) = \
		Z_TIMER_INITIALIZER(name, expiry_fn, stop_fn)

/**
  * @brief  在首次使用前初始化定时器和回调。
  * @param  timer：[输出]非空定时器存储，后续使用期间保持有效。
  * @param  expiry_fn：[输入]到期回调，可为空。
  * @param  stop_fn：[输入]提前停止回调，可为空。
  * @retval 无。
  * @note   初始化后仍需 k_timer_start 启动。
  */
extern void k_timer_init(struct k_timer *timer,
			 k_timer_expiry_t expiry_fn,
			 k_timer_stop_t stop_fn);

/**
  * @brief  清零状态并按给定首轮时长和周期启动定时器。
  * @param  timer：[输入/输出]非空已初始化定时器。
  * @param  duration：[输入]首次到期所需超时对象。
  * @param  period：[输入]后续周期超时对象，K_NO_WAIT 表示单次。
  * @retval 无。
  * @note   允许重启已运行的定时器，重启会重新清零累计到期状态并采用新参数。
  */
__syscall void k_timer_start(struct k_timer *timer,
			     k_timeout_t duration, k_timeout_t period);

/**
  * @brief  提前停止运行中的定时器。
  * @param  timer：[输入/输出]非空已初始化定时器。
  * @retval 无。
  * @note   已停止对象不受影响；若注册 stop_fn 则同步调用。可在中断调用，停止回调也必须适用于该上下文。
  */
__syscall void k_timer_stop(struct k_timer *timer);

/**
  * @brief  读取并清零定时器累计到期次数。
  * @param  timer：[输入/输出]非空已初始化定时器。
  * @retval 自上次读取以来的到期次数。
  * @note   读取会清零状态，对其他状态读取者有影响。
  */
__syscall uint32_t k_timer_status_get(struct k_timer *timer);

/**
  * @brief  阻塞到定时器至少到期一次或被停止，再读取并清零状态。
  * @param  timer：[输入/输出]非空已初始化定时器。
  * @retval 累计到期次数；停止且无待处理次数时为 0。
  * @note   已有到期状态或已经停止时立即返回；不可在中断中使用。
  */
__syscall uint32_t k_timer_status_sync(struct k_timer *timer);

#ifdef CONFIG_SYS_CLOCK_EXISTS

/**
  * @brief  查询定时器的下一到期 tick 值。
  * @param  timer：[输入]非空已初始化定时器。
  * @retval 内部 z_timeout_expires 查询值，单位系统 tick。
  * @note   不保证线程或回调能在查询值对应时刻立即运行，计算遵循当前 timeout.c 实现。
  */
__syscall k_ticks_t k_timer_expires_ticks(const struct k_timer *timer);

/**
  * @brief  查询定时器的下一到期 tick 值。
  * @param  timer：[输入]非空已初始化定时器。
  * @retval 内部 z_timeout_expires 查询值，单位系统 tick。
  * @note   不保证线程或回调能在查询值对应时刻立即运行，计算遵循当前 timeout.c 实现。
  */
static inline k_ticks_t z_impl_k_timer_expires_ticks(
				       const struct k_timer *timer)
{


	return z_timeout_expires(&timer->timeout);    /* 把定时器内部节点交给超时系统查询到期值。 */
}

/**
  * @brief  查询定时器下一次到期的剩余 tick 数。
  * @param  timer：[输入]非空已初始化定时器。
  * @retval 剩余系统 tick 数；未运行返回 0。
  */
__syscall k_ticks_t k_timer_remaining_ticks(const struct k_timer *timer);

/**
  * @brief  查询定时器下一次到期的剩余 tick 数。
  * @param  timer：[输入]非空已初始化定时器。
  * @retval 剩余系统 tick 数；未运行返回 0。
  */
static inline k_ticks_t z_impl_k_timer_remaining_ticks(
				       const struct k_timer *timer)
{


	return z_timeout_remaining(&timer->timeout);    /* 只查询内部超时节点，不启动或停止定时器。 */
}

/**
  * @brief  把定时器剩余时间换算为毫秒。
  * @param  timer：[输入]非空已初始化定时器。
  * @retval 向下取整后的剩余毫秒数；未运行返回 0。
  * @note   这是查询时的近似剩余值，时间会继续推进。
  */
static inline uint32_t k_timer_remaining_get(struct k_timer *timer)
{
	return k_ticks_to_ms_floor32(k_timer_remaining_ticks(timer));    /* 先取得剩余 tick，再向下取整转换为毫秒。 */
}

#endif    /* CONFIG_SYS_CLOCK_EXISTS */

/**
  * @brief  设置可在回调中取回的定时器应用上下文。
  * @param  timer：[输入/输出]非空已初始化定时器。
  * @param  user_data：[输入]应用数据地址，可为空，由应用维护其生命周期。
  * @retval 无。
  * @note   定时器不复制或释放指向的数据。
  */
__syscall void k_timer_user_data_set(struct k_timer *timer, void *user_data);

/**
  * @brief  保存定时器的应用上下文指针。
  * @param  timer：[输入/输出]非空可写定时器。
  * @param  user_data：[输入]应用数据地址，可为空。
  * @retval 无。
  * @note   k_timer_user_data_set 的内联实现，仅写成员，不取得数据所有权。
  */
static inline void z_impl_k_timer_user_data_set(struct k_timer *timer,
					       void *user_data)
{
	timer->user_data = user_data;    /* 仅保存应用指针，不复制或释放其数据。 */
}

/**
  * @brief  读取定时器关联的应用上下文。
  * @param  timer：[输入]非空已初始化定时器。
  * @retval 此前保存的上下文指针，可为 NULL。
  * @note   不复制数据，不转移所有权。
  */
__syscall void *k_timer_user_data_get(const struct k_timer *timer);

/**
  * @brief  读取定时器关联的应用上下文。
  * @param  timer：[输入]非空已初始化定时器。
  * @retval 此前保存的上下文指针，可为 NULL。
  * @note   不复制数据，不转移所有权。
  */
static inline void *z_impl_k_timer_user_data_get(const struct k_timer *timer)
{
	return timer->user_data;    /* 返回此前保存的上下文地址，不转移所有权。 */
}

/** @} */

/**
 * @addtogroup clock_apis
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  读取启动后累计的系统 tick 数。
  * @param  无。
  * @retval 64 位启动后 tick 计数。
  * @note   时间尺度由 CONFIG_SYS_CLOCK_TICKS_PER_SEC 决定，是内核计时的基本粒度。
  */
__syscall int64_t k_uptime_ticks(void);

/**
  * @brief  将启动后 tick 数换算为 64 位毫秒数。
  * @param  无。
  * @retval 启动后累计毫秒数，向下取整。
  * @note   毫秒是返回单位，实际分辨率由 tick 频率决定。当前目标 1000 tick/s；不能把 CPU 主频用作该换算频率。
  */
static inline int64_t k_uptime_get(void)
{
	return k_ticks_to_ms_floor64(k_uptime_ticks());    /* 以配置的 tick 频率换算 64 位启动后毫秒数。 */
}

/**
  * @brief  读取启动后毫秒计数的低 32 位。
  * @param  无。
  * @retval k_uptime_get 的低 32 位，约每 49.7 天回绕。
  * @note   仍可能进行 64 位换算和短时中断屏蔽，不能推定比 64 位接口更快；精度受 tick 限制。
  */
static inline uint32_t k_uptime_get_32(void)
{
	return (uint32_t)k_uptime_get();    /* 截取毫秒计数低 32 位，保留无符号回绕行为。 */
}

/**
  * @brief  计算距离上次参考时刻的毫秒差并更新参考值。
  * @param  reftime：[输入/输出]非空可读写 int64_t，入参为此前毫秒时间，出参为本次采样时间。
  * @retval 本次与传入参考时刻之差，单位毫秒。
  * @note   共享同一参考对象时调用方须自行同步。
  */
static inline int64_t k_uptime_delta(int64_t *reftime)
{
	int64_t uptime, delta;        /* 分别保存本次采样时间和与调用方参考时刻的差。 */

	/* 采样当前时间、求差并更新调用方参考值。 */
	uptime = k_uptime_get();      /* 采样一次，保证差值与更新后的参考值对应同一时刻。 */
	delta = uptime - *reftime;    /* 求相对旧参考时刻的毫秒差，允许调用方给出未来参考值。 */
	*reftime = uptime;    /* 保存本次采样值，供下一次计算间隔。 */

	return delta;                 /* 返回更新参考值之前计算的间隔。 */
}

/**
  * @brief  读取架构提供的 32 位硬件累计周期计数。
  * @param  无。
  * @retval 当前硬件上计数值的低 32 位，单位 cycle，按无符号数回绕。
  * @note   转交 arch_k_cycle_get_32；当前 Actions 实现来自 TIMER2 并叠加休眠补偿，频率由系统硬件周期配置决定。
  */
static inline uint32_t k_cycle_get_32(void)
{
	return arch_k_cycle_get_32();    /* 委托架构硬件周期读取，单位与系统周期配置一致。 */
}

/**
 * @}
 */

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_queue {
	sys_sflist_t data_q;       /* 带标志单向数据链，区分直接节点与动态包装节点。 */
	struct k_spinlock lock;    /* 保护队列数据和等待状态的自旋锁。 */
	_wait_q_t wait_q;          /* 等待取数的线程队列。 */

	_POLL_EVENT;
};

/* 构造空通用队列：初始化带标志链表、自旋锁、线程等待队列和可选轮询链表。 */
#define Z_QUEUE_INITIALIZER(obj) \
	{ \
	.data_q = SYS_SFLIST_STATIC_INIT(&obj.data_q), \
	.lock = { }, \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q),	\
	_POLL_EVENT_OBJ_INIT(obj)		\
	}

/**
  * @brief  把内部队列节点解析为应用数据地址。
  * @param  node：[输入]带标志节点，可为 NULL。
  * @param  needs_free：[输入]true 时释放动态包装节点，false 仅查看。
  * @retval 动态节点返回其 data，直接节点返回节点地址；NULL 节点返回 NULL。
  * @note   只释放动态链接元数据，不释放应用数据；调用者保证节点访问同步。
  */
extern void *z_queue_node_peek(sys_sfnode_t *node, bool needs_free);

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup queue_apis 通用队列接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  首次使用前初始化通用队列。
  * @param  queue：[输出]非空队列对象，使用期间保持有效。
  * @retval 无。
  */
__syscall void k_queue_init(struct k_queue *queue);

/**
  * @brief  取消队列上的首个等待者。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @retval 无。
  * @note   k_queue_get 等待者返回 NULL；k_poll 等待返回 -EINTR 和 CANCELLED 状态，后续 get 也可能为空。可在中断调用。
  */
__syscall void k_queue_cancel_wait(struct k_queue *queue);

/**
  * @brief  把数据项加入队列尾部。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  data：[输入/输出]非空字对齐的数据项，首字供内核链接使用，排队期间保持有效。
  * @retval 无。
  * @note   不复制数据，首字不能同时用于其他队列；可在中断调用。
  */
extern void k_queue_append(struct k_queue *queue, void *data);

/**
  * @brief  申请链接元数据后把应用数据地址加入队列尾部。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  data：[输入]应用数据地址，出队前须保持指向对象有效。
  * @retval 0 成功；-ENOMEM 调用线程资源池空间不足。
  * @note   从调用线程资源池申请元数据，出队时自动释放元数据；应用数据本身不复制、不自动释放。原接口允许中断调用，仍须满足底层资源池前提。
  */
__syscall int32_t k_queue_alloc_append(struct k_queue *queue, void *data);

/**
  * @brief  把数据项加入队列头部。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  data：[输入/输出]非空字对齐的数据项，首字供内核链接使用，排队期间保持有效。
  * @retval 无。
  * @note   不复制数据，首字不能同时用于其他队列；可在中断调用。
  */
extern void k_queue_prepend(struct k_queue *queue, void *data);

/**
  * @brief  申请链接元数据后把应用数据地址加入队列头部。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  data：[输入]应用数据地址，出队前须保持指向对象有效。
  * @retval 0 成功；-ENOMEM 调用线程资源池空间不足。
  * @note   从调用线程资源池申请元数据，出队时自动释放元数据；应用数据本身不复制、不自动释放。原接口允许中断调用，仍须满足底层资源池前提。
  */
__syscall int32_t k_queue_alloc_prepend(struct k_queue *queue, void *data);

/**
  * @brief  把数据项插在队列指定前项之后。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  prev：[输入]前项地址；是否为空的具体语义由队列实现决定。
  * @param  data：[输入/输出]非空字对齐数据项，首字供内核使用，排队期间保持有效。
  * @retval 无。
  * @note   不复制数据，可在中断调用。
  */
extern void k_queue_insert(struct k_queue *queue, void *prev, void *data);

/**
  * @brief  原子地将一条单向链表附加到队列尾部。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  head：[输入/输出]非空首节点，每节点首字指向下一节点。
  * @param  tail：[输入/输出]非空尾节点，其下一指针必须为 NULL。
  * @retval 0 成功；-EINVAL 链表数据不合法。
  * @note   接管该批节点的排队关系，数据不复制；可在中断调用。
  */
extern int k_queue_append_list(struct k_queue *queue, void *head, void *tail);

/**
  * @brief  把 sys_slist 的全部节点合入队列。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  list：[输入/输出]非空单向链表对象，成功合并后原链表为空。
  * @retval 0 成功；-EINVAL 链表数据无效。
  * @note   可在中断调用，不复制节点数据。
  */
extern int k_queue_merge_slist(struct k_queue *queue, sys_slist_t *list);

/**
  * @brief  取得并移除队首数据项，必要时等待。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  timeout：[输入]非负等待时长对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 成功返回数据项地址；立即未取到、等待超时或取消等待返回 NULL。
  * @note   中断只能用 K_NO_WAIT；嵌入节点的首字保留给内核使用。
  */
__syscall void *k_queue_get(struct k_queue *queue, k_timeout_t timeout);

/**
  * @brief  查找并移除队列中的指定数据项。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  data：[输入/输出]待移除数据项，其首字保留给内核。
  * @retval 找到并移除返回 true，否则 false。
  * @note   底层需要遍历链表，耗时随队列长度增长；可在中断调用。本接口没有 timeout 参数。
  */
bool k_queue_remove(struct k_queue *queue, void *data);

/**
  * @brief  仅在队列尚不含指定项时将其追加。
  * @param  queue：[输入/输出]非空已初始化队列。
  * @param  data：[输入/输出]非空字对齐数据项，首字供内核使用。
  * @retval 实际加入返回 true，已存在返回 false。
  * @note   检查需要遍历，非恒定耗时；可在中断调用。
  */
bool k_queue_unique_append(struct k_queue *queue, void *data);

/**
  * @brief  查询队列当前是否为空。
  * @param  queue：[输入]非空已初始化队列。
  * @retval 非零表示空，0 表示有数据。
  * @note   其他线程可能立即改变队列，查询结果不能保证随后的 get 一定成功；可在中断调用。
  */
__syscall int k_queue_is_empty(struct k_queue *queue);

/**
  * @brief  查询队列当前是否为空。
  * @param  queue：[输入]非空已初始化队列。
  * @retval 非零表示空，0 表示有数据。
  * @note   其他线程可能立即改变队列，查询结果不能保证随后的 get 一定成功；可在中断调用。
  */
static inline int z_impl_k_queue_is_empty(struct k_queue *queue)
{
	return (int)sys_sflist_is_empty(&queue->data_q);    /* 检查带标志数据链是否为空，并转换为接口整数结果。 */
}

/**
  * @brief  查看队首项但不移除。
  * @param  queue：[输入]非空已初始化队列。
  * @retval 对应数据项地址；空队列返回 NULL。
  * @note   并发访问时调用方须考虑节点可能随后被其他线程移除。
  */
__syscall void *k_queue_peek_head(struct k_queue *queue);

/**
  * @brief  查看队尾项但不移除。
  * @param  queue：[输入]非空已初始化队列。
  * @retval 对应数据项地址；空队列返回 NULL。
  * @note   并发访问时调用方须考虑节点可能随后被其他线程移除。
  */
__syscall void *k_queue_peek_tail(struct k_queue *queue);

/**
  * @brief  静态定义并初始化一个通用队列。
  * @param  name：[输入]对象标识符，可从其他模块以 extern struct k_queue 引用。
  * @retval 无。
  * @note   对象进入可迭代链接段。
  */
#define K_QUEUE_DEFINE(name) \
	STRUCT_SECTION_ITERABLE(k_queue, name) = \
		Z_QUEUE_INITIALIZER(name)

/** @} */

#ifdef CONFIG_USERSPACE
/* 用户态 futex 对象：无争用时仅通过共享内存原子操作同步；有争用时进入内核等待。它可置于用户内存，地址访问检查不同于普通内核对象权限。 */
struct k_futex {
	atomic_t val;    /* 用户共享的原子条件值，无争用时可直接进行原子操作。 */
};

/* futex 的内核侧辅助状态，保存等待队列及自旋锁；每个 futex 对应的此结构不向用户态公开。 */
struct z_futex_data {
	_wait_q_t wait_q;          /* 争用时阻塞线程的内核等待队列。 */
	struct k_spinlock lock;    /* 保护等待队列和条件检查的自旋锁。 */
};

/* 构造空 futex 内核等待队列；其余成员依静态初始化规则清零。 */
#define Z_FUTEX_DATA_INITIALIZER(obj) \
	{ \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q) \
	}

/**
 * @defgroup futex_apis 用户态原子等待接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  仅在 futex 仍等于期望值时挂起当前线程。
  * @param  futex：[输入/输出]可读的有效 futex 地址。
  * @param  expected：[输入]预期原子值，不相等时不等待。
  * @param  timeout：[输入]非负等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 表示曾等待并被唤醒；-EACCES 地址无读权限；-EAGAIN 值不匹配；-EINVAL 内核不识别对象；-ETIMEDOUT 等待超时。
  * @note   成功唤醒不代表共享条件仍成立，调用方必须重新检查 futex 值；仅 USERSPACE 分支声明。
  */
__syscall int k_futex_wait(struct k_futex *futex, int expected,
			   k_timeout_t timeout);

/**
  * @brief  唤醒指定 futex 上的一个或全部等待线程。
  * @param  futex：[输入/输出]有效 futex 地址。
  * @param  wake_all：[输入]true 唤醒全部，false 只唤醒最高优先级等待者。
  * @retval 非负值为实际唤醒数；-EACCES 无访问权限；-EINVAL 内核不识别地址。
  * @note   仅 USERSPACE 分支声明。
  */
__syscall int k_futex_wake(struct k_futex *futex, bool wake_all);

/** @} */
#endif

struct k_fifo {
	struct k_queue _queue;    /* 通过尾插和队首取出形成先进先出顺序的通用队列。 */
};

/**
 * @cond INTERNAL_HIDDEN
 */
/* 把内嵌通用队列初始化为空，FIFO 顺序由其操作包装保证。 */
#define Z_FIFO_INITIALIZER(obj) \
	{ \
	._queue = Z_QUEUE_INITIALIZER(obj._queue) \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup fifo_apis 先进先出队列接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  首次使用前初始化FIFO 队列。
  * @param  fifo：[输出]非空队列对象。
  * @retval 无。
  * @note   包装通用队列初始化，并保留端口跟踪入口和出口。
  */
#define k_fifo_init(fifo) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, init, fifo); \
	k_queue_init(&(fifo)->_queue); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, init, fifo); \
	})

/**
  * @brief  取消 FIFO 上首个阻塞取数线程的等待。
  * @param  fifo：[输入/输出]非空已初始化 FIFO。
  * @retval 无。
  * @note   等待者从 get 返回 NULL；可在中断调用。
  */
#define k_fifo_cancel_wait(fifo) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, cancel_wait, fifo); \
	k_queue_cancel_wait(&(fifo)->_queue); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, cancel_wait, fifo); \
	})

/**
  * @brief  把一个数据项加入FIFO。
  * @param  fifo：[输入/输出]非空已初始化队列。
  * @param  data：[输入/输出]非空字对齐数据项，首字供内核链接使用，出队前保持有效。
  * @retval 无。
  * @note   数据不复制；FIFO 尾插、LIFO 头插以形成各自顺序；可在中断调用。
  */
#define k_fifo_put(fifo, data) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, put, fifo, data); \
	k_queue_append(&(fifo)->_queue, data); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, put, fifo, data); \
	})

/**
  * @brief  申请链接元数据后把数据地址加入FIFO。
  * @param  fifo：[输入/输出]非空已初始化队列。
  * @param  data：[输入]应用数据地址，出队前保持有效。
  * @retval 0 成功；-ENOMEM 调用线程资源池不足。
  * @note   元数据从线程资源池申请，出队时释放；应用数据不复制。保留接口原有中断调用能力及资源池前提。
  */
#define k_fifo_alloc_put(fifo, data) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, alloc_put, fifo, data); \
	int ret = k_queue_alloc_append(&(fifo)->_queue, data); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, alloc_put, fifo, data, ret); \
	ret; \
	})

/**
  * @brief  把整条链表一次性追加到 FIFO。
  * @param  fifo：[输入/输出]非空已初始化 FIFO。
  * @param  head：[输入/输出]非空首项，每项首字是下一指针。
  * @param  tail：[输入/输出]非空末项，下一指针为 NULL。
  * @retval 无。
  * @note   接管节点排队关系而不复制数据；可在中断调用。底层 append_list 的状态没有由此包装宏显式返回。
  */
#define k_fifo_put_list(fifo, head, tail) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, put_list, fifo, head, tail); \
	k_queue_append_list(&(fifo)->_queue, head, tail); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, put_list, fifo, head, tail); \
	})

/**
  * @brief  把 sys_slist 中全部节点转入 FIFO。
  * @param  fifo：[输入/输出]非空已初始化 FIFO。
  * @param  list：[输入/输出]非空源链表。
  * @retval 无。
  * @note   调用后源对象应重新 sys_slist_init 再复用；节点数据不复制，可在中断调用。
  */
#define k_fifo_put_slist(fifo, list) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, put_slist, fifo, list); \
	k_queue_merge_slist(&(fifo)->_queue, list); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, put_slist, fifo, list); \
	})

/**
  * @brief  按先进先出顺序取得并移除队列项。
  * @param  fifo：[输入/输出]非空已初始化FIFO。
  * @param  timeout：[输入]非负等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 数据项地址；未取到或等待超时返回 NULL。
  * @note   中断中只允许 K_NO_WAIT；嵌入节点的首字供内核使用。
  */
#define k_fifo_get(fifo, timeout) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, get, fifo, timeout); \
	void *ret = k_queue_get(&(fifo)->_queue, timeout); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, get, fifo, timeout, ret); \
	ret; \
	})

/**
  * @brief  查询 FIFO 当前是否为空。
  * @param  fifo：[输入]非空已初始化 FIFO。
  * @retval 非零为空，0 表示存在数据。
  * @note   只是瞬时快照；其他线程可随后取走数据，可在中断调用。
  */
#define k_fifo_is_empty(fifo) \
	k_queue_is_empty(&(fifo)->_queue)

/**
  * @brief  查看 FIFO 队首项但保留其排队位置。
  * @param  fifo：[输入]非空已初始化 FIFO。
  * @retval 队首地址，空队列返回 NULL。
  * @note   适合逐次处理作为数据容器的队首项，仅容器耗尽后再移除；并发移除需外部同步。
  */
#define k_fifo_peek_head(fifo) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, peek_head, fifo); \
	void *ret = k_queue_peek_head(&(fifo)->_queue); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, peek_head, fifo, ret); \
	ret; \
	})

/**
  * @brief  查看 FIFO 队尾项但不移除。
  * @param  fifo：[输入]非空已初始化 FIFO。
  * @retval 队尾地址，空队列返回 NULL。
  * @note   可用于向末尾容器继续填充数据；并发访问需同步。
  */
#define k_fifo_peek_tail(fifo) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_fifo, peek_tail, fifo); \
	void *ret = k_queue_peek_tail(&(fifo)->_queue); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_fifo, peek_tail, fifo, ret); \
	ret; \
	})

/**
  * @brief  静态定义并初始化一个FIFO对象。
  * @param  name：[输入]对象标识符，外部可按 struct k_fifo 声明引用。
  * @retval 无。
  * @note   对象进入内核可迭代链接段。
  */
#define K_FIFO_DEFINE(name) \
	STRUCT_SECTION_ITERABLE_ALTERNATE(k_queue, k_fifo, name) = \
		Z_FIFO_INITIALIZER(name)

/** @} */

struct k_lifo {
	struct k_queue _queue;    /* 通过头插和队首取出形成后进先出顺序的通用队列。 */
};

/**
 * @cond INTERNAL_HIDDEN
 */

/* 把内嵌通用队列初始化为空，LIFO 顺序由其头插操作保证。 */
#define Z_LIFO_INITIALIZER(obj) \
	{ \
	._queue = Z_QUEUE_INITIALIZER(obj._queue) \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup lifo_apis 后进先出队列接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  首次使用前初始化LIFO 队列。
  * @param  lifo：[输出]非空队列对象。
  * @retval 无。
  * @note   包装通用队列初始化，并保留端口跟踪入口和出口。
  */
#define k_lifo_init(lifo) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_lifo, init, lifo); \
	k_queue_init(&(lifo)->_queue); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_lifo, init, lifo); \
	})

/**
  * @brief  把一个数据项加入LIFO。
  * @param  lifo：[输入/输出]非空已初始化队列。
  * @param  data：[输入/输出]非空字对齐数据项，首字供内核链接使用，出队前保持有效。
  * @retval 无。
  * @note   数据不复制；FIFO 尾插、LIFO 头插以形成各自顺序；可在中断调用。
  */
#define k_lifo_put(lifo, data) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_lifo, put, lifo, data); \
	k_queue_prepend(&(lifo)->_queue, data); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_lifo, put, lifo, data); \
	})

/**
  * @brief  申请链接元数据后把数据地址加入LIFO。
  * @param  lifo：[输入/输出]非空已初始化队列。
  * @param  data：[输入]应用数据地址，出队前保持有效。
  * @retval 0 成功；-ENOMEM 调用线程资源池不足。
  * @note   元数据从线程资源池申请，出队时释放；应用数据不复制。保留接口原有中断调用能力及资源池前提。
  */
#define k_lifo_alloc_put(lifo, data) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_lifo, alloc_put, lifo, data); \
	int ret = k_queue_alloc_prepend(&(lifo)->_queue, data); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_lifo, alloc_put, lifo, data, ret); \
	ret; \
	})

/**
  * @brief  按后进先出顺序取得并移除队列项。
  * @param  lifo：[输入/输出]非空已初始化LIFO。
  * @param  timeout：[输入]非负等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 数据项地址；未取到或等待超时返回 NULL。
  * @note   中断中只允许 K_NO_WAIT；嵌入节点的首字供内核使用。
  */
#define k_lifo_get(lifo, timeout) \
	({ \
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_lifo, get, lifo, timeout); \
	void *ret = k_queue_get(&(lifo)->_queue, timeout); \
	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_lifo, get, lifo, timeout, ret); \
	ret; \
	})

/**
  * @brief  静态定义并初始化一个LIFO对象。
  * @param  name：[输入]对象标识符，外部可按 struct k_lifo 声明引用。
  * @retval 无。
  * @note   对象进入内核可迭代链接段。
  */
#define K_LIFO_DEFINE(name) \
	STRUCT_SECTION_ITERABLE_ALTERNATE(k_queue, k_lifo, name) = \
		Z_LIFO_INITIALIZER(name)

/** @} */

/**
 * @cond INTERNAL_HIDDEN
 */
#define K_STACK_FLAG_ALLOC	((uint8_t)1)    /* 标记数据栈的缓冲区由动态申请获得，清理时据此判断是否释放。 */

typedef uintptr_t stack_data_t;    /* 数据栈元素的无符号整型，宽度足以容纳地址。 */

struct k_stack {
	_wait_q_t wait_q;                   /* 等待弹出数据的线程队列。 */
	struct k_spinlock lock;             /* 保护栈指针和等待状态的自旋锁。 */
	stack_data_t *base, *next, *top;    /* base 为数组首址，next 为下一空槽，top 为容量末端边界。 */

	uint8_t flags;    /* 缓冲区来源标志，动态缓冲才由 cleanup 释放。 */
};

/* 将数组起点同时赋给 base 和 next，top 指向容量末端；等待队列为空，表示尚无已压入数据。 */
#define Z_STACK_INITIALIZER(obj, stack_buffer, stack_num_entries) \
	{ \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q),	\
	.base = stack_buffer, \
	.next = stack_buffer, \
	.top = stack_buffer + stack_num_entries, \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup stack_apis 数据栈接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  用调用方提供的数组初始化数据栈。
  * @param  stack：[输出]非空数据栈对象；它不是线程运行栈。
  * @param  buffer：[输入/输出]非空 stack_data_t 数组，容量至少 num_entries 项，使用期间保持有效。
  * @param  num_entries：[输入]最多可保存的值数，单位项。
  * @retval 无。
  */
void k_stack_init(struct k_stack *stack,
		  stack_data_t *buffer, uint32_t num_entries);


/**
  * @brief  申请内部数组并初始化数据栈。
  * @param  stack：[输出]非空数据栈对象。
  * @param  num_entries：[输入]最多可保存的值数，单位项。
  * @retval 0 成功；-ENOMEM 无法分配数组。
  * @note   使用调用线程资源池；通过 cleanup 释放，USERSPACE 下丢失最后引用也可触发对象清理。
  */

__syscall int32_t k_stack_alloc_init(struct k_stack *stack,
				   uint32_t num_entries);

/**
  * @brief  释放数据栈动态申请的内部数组。
  * @param  stack：[输入/输出]非空已初始化数据栈。
  * @retval 0 成功；-EAGAIN 对象仍在使用。
  * @note   使用调用方静态数组的对象不会释放数组。
  */
int k_stack_cleanup(struct k_stack *stack);

/**
  * @brief  把一个整型值压入数据栈。
  * @param  stack：[输入/输出]非空已初始化数据栈。
  * @param  data：[输入]本次压入的 stack_data_t 值。
  * @retval 0 成功；-ENOMEM 数据栈已满。
  * @note   可在中断调用，不把 data 解释为有所有权的内存块。
  */
__syscall int k_stack_push(struct k_stack *stack, stack_data_t data);

/**
  * @brief  弹出最新压入的数据值。
  * @param  stack：[输入/输出]非空已初始化数据栈。
  * @param  data：[输出]非空可写 stack_data_t，接收弹出值。
  * @param  timeout：[输入]等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 成功；-EBUSY 未等待即失败；-EAGAIN 等待超时。
  * @note   中断只允许 K_NO_WAIT。
  */
__syscall int k_stack_pop(struct k_stack *stack, stack_data_t *data,
			  k_timeout_t timeout);

/**
  * @brief  静态定义数据栈对象及其数组。
  * @param  name：[输入]对象标识符，外部可按 struct k_stack 引用。
  * @param  stack_num_entries：[输入]数组可容纳的 stack_data_t 项数。
  * @retval 无。
  * @note   数组放在不初始化段，控制对象的初始指针界定空栈，不要求先清空每个数组项。
  */
#define K_STACK_DEFINE(name, stack_num_entries)                \
	stack_data_t __noinit                                  \
		_k_stack_buf_##name[stack_num_entries];        \
	STRUCT_SECTION_ITERABLE(k_stack, name) =               \
		Z_STACK_INITIALIZER(name, _k_stack_buf_##name, \
				    stack_num_entries)

/** @} */

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_work;
struct k_work_q;
struct k_work_queue_config;
struct k_delayed_work;
/* 系统工作队列的外部声明，真正对象由工作队列启动模块定义，不在此分配。 */
extern struct k_work_q k_sys_work_q;

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup mutex_apis 互斥锁接口
 * @ingroup kernel_apis
 * @{
 */

/* 互斥锁对象；具有线程所有权、递归计数和优先级继承状态。 */
struct k_mutex {
	/* 等待获得互斥锁的线程队列。 */
	_wait_q_t wait_q;
	/* 当前持有锁的线程，空闲时为 NULL。 */
	struct k_thread *owner;

	/* 当前所有者递归获取次数，全部释放后才可交给其他线程。 */
	uint32_t lock_count;

	/* 所有者获取锁前的优先级，供优先级继承结束时恢复。 */
	int owner_orig_prio;
};

/**
 * @cond INTERNAL_HIDDEN
 */
/* 初始化空等待队列、无所有者、递归计数 0，原优先级槽预设为应用最低等级。 */
#define Z_MUTEX_INITIALIZER(obj) \
	{ \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q), \
	.owner = NULL, \
	.lock_count = 0, \
	.owner_orig_prio = K_LOWEST_APPLICATION_THREAD_PRIO, \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
  * @brief  静态定义并初始化一个互斥锁对象。
  * @param  name：[输入]对象标识符，外部可按 struct k_mutex 声明引用。
  * @retval 无。
  * @note   对象进入内核可迭代链接段。
  */
#define K_MUTEX_DEFINE(name) \
	STRUCT_SECTION_ITERABLE(k_mutex, name) = \
		Z_MUTEX_INITIALIZER(name)

/**
  * @brief  初始化为没有所有者的可用互斥锁。
  * @param  mutex：[输出]非空互斥锁对象。
  * @retval 0：初始化完成。
  */
__syscall int k_mutex_init(struct k_mutex *mutex);


/**
  * @brief  取得互斥锁，必要时等待其他线程释放。
  * @param  mutex：[输入/输出]非空已初始化互斥锁。
  * @param  timeout：[输入]等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 成功；-EBUSY 未等待即失败；-EAGAIN 等待超时。
  * @note   同一线程可递归获取，每次增加计数；不能从中断调用。
  */
__syscall int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);

/**
  * @brief  释放当前线程持有的一级互斥锁计数。
  * @param  mutex：[输入/输出]非空已初始化互斥锁。
  * @retval 0 成功；-EPERM 当前线程不是所有者；-EINVAL 锁未处于获取状态。
  * @note   递归获取多少次就必须释放多少次；所有权及优先级继承只适用于线程上下文，禁止中断调用。
  */
__syscall int k_mutex_unlock(struct k_mutex *mutex);

/**
 * @}
 */


struct k_condvar {
	_wait_q_t wait_q;    /* 等待该条件变化的线程队列，实际共享条件由应用维护。 */
};

/* 初始化条件变量的空等待队列；条件本身由应用在关联互斥锁保护下维护。 */
#define Z_CONDVAR_INITIALIZER(obj)                                             \
	{                                                                      \
		.wait_q = Z_WAIT_Q_INIT(&obj.wait_q),                          \
	}

/**
 * @defgroup condvar_apis 条件变量接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  初始化条件变量等待队列。
  * @param  condvar：[输出]非空条件变量对象。
  * @retval 0：创建成功。
  */
__syscall int k_condvar_init(struct k_condvar *condvar);

/**
  * @brief  通知条件变量上的一个等待线程。
  * @param  condvar：[输入/输出]非空已初始化条件变量。
  * @retval 0：完成通知。
  */
__syscall int k_condvar_signal(struct k_condvar *condvar);

/**
  * @brief  通知条件变量上的所有等待线程。
  * @param  condvar：[输入/输出]非空已初始化条件变量。
  * @retval 成功时返回被唤醒线程数。
  */
__syscall int k_condvar_broadcast(struct k_condvar *condvar);

/**
  * @brief  释放已持有的互斥锁并等待条件通知，返回前重新获取锁。
  * @param  condvar：[输入/输出]非空已初始化条件变量。
  * @param  mutex：[输入/输出]非空且由当前线程持有的互斥锁。
  * @param  timeout：[输入]等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 成功；-EAGAIN 等待超时。
  * @note   等待入队与释放锁作为一个同步步骤；由 signal/broadcast 或超时结束等待。返回后仍须检查共享条件；不可在中断中阻塞。
  */
__syscall int k_condvar_wait(struct k_condvar *condvar, struct k_mutex *mutex,
			     k_timeout_t timeout);

/**
  * @brief  静态定义并初始化一个条件变量对象。
  * @param  name：[输入]对象标识符，外部可按 struct k_condvar 声明引用。
  * @retval 无。
  * @note   对象进入内核可迭代链接段。
  */
#define K_CONDVAR_DEFINE(name)                                                 \
	STRUCT_SECTION_ITERABLE(k_condvar, name) =                             \
		Z_CONDVAR_INITIALIZER(name)
/**
 * @}
 */

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_sem {
	_wait_q_t wait_q;      /* 等待计数可用的线程队列。 */
	unsigned int count;    /* 当前可用次数；成功 take 减一，give 唤醒等待者或增加此值。 */
	unsigned int limit;    /* 最大允许计数，give 不会使 count 超过它。 */

	_POLL_EVENT;

};

/* 用参数设置 count 和 limit，并初始化线程等待队列和可选轮询事件链表。 */
#define Z_SEM_INITIALIZER(obj, initial_count, count_limit) \
	{ \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q), \
	.count = initial_count, \
	.limit = count_limit, \
	_POLL_EVENT_OBJ_INIT(obj) \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup semaphore_apis 信号量接口
 * @ingroup kernel_apis
 * @{
 */

/* 信号量计数允许的最大上限，适用于纯计数而不设较小资源限额的场景。 */
#define K_SEM_MAX_LIMIT UINT_MAX

/**
  * @brief  初始化计数信号量及等待队列。
  * @param  sem：[输出] 非空可写信号量对象，不能在仍被其他线程使用时重初始化。
  * @param  initial_count：[输入] 初始许可数0～limit。
  * @param  limit：[输入] 计数上限1～K_SEM_MAX_LIMIT。
  * @retval 0：初始化完成；-EINVAL：启用参数检查时检测到无效范围。
  * @note   仅初始化状态，不分配堆内存；初值0的SPI总线锁须在驱动初始化结束时首次give。
  */
__syscall int k_sem_init(struct k_sem *sem, unsigned int initial_count,
			  unsigned int limit);

/**
  * @brief  取得一个许可，必要时按超时参数挂起当前线程。
  * @param  sem：[输入输出] 已初始化信号量，不可为空。
  * @param  timeout：[输入] K_NO_WAIT立即尝试，K_FOREVER无限等，其他为k_timeout_t编码的tick预算。
  * @retval 0：取得许可；-EBUSY：无许可且不等待；-EAGAIN：等待超时或被reset中断。
  * @note   中断上下文只能K_NO_WAIT；挂起由z_pend_curr原子释放锁并切换线程，不在自旋锁内忙等。
  */
__syscall int k_sem_take(struct k_sem *sem, k_timeout_t timeout);

/**
  * @brief  交付一个信号量许可并在需要时重新调度。
  * @param  sem：[输入输出] 已初始化且仍有效的信号量，不可为空。
  * @retval 无。
  * @note   可由线程或中断调用；有等待线程时直接令其成功就绪，否则计数饱和增加。释放自旋锁由z_reschedule负责。
  */
__syscall void k_sem_give(struct k_sem *sem);

/**
  * @brief  清零许可并使所有等待线程以-EAGAIN结束。
  * @param  sem：[输入输出] 已初始化且有效的信号量，不可为空。
  * @retval 无。
  * @note   在锁内清空等待队列并唤醒线程，之后处理poll及重新调度；不销毁对象。
  */
__syscall void k_sem_reset(struct k_sem *sem);

/**
  * @brief  读取信号量的当前可用计数。
  * @param  sem：[输入]非空已初始化信号量。
  * @retval 当前 count，无符号计数值。
  * @note   内联实现直接读取成员；其他上下文可立即改变计数，不能用此查询代替 take。
  */
__syscall unsigned int k_sem_count_get(struct k_sem *sem);

/**
  * @brief  读取信号量的当前可用计数。
  * @param  sem：[输入]非空已初始化信号量。
  * @retval 当前 count，无符号计数值。
  * @note   内联实现直接读取成员；其他上下文可立即改变计数，不能用此查询代替 take。
  */
static inline unsigned int z_impl_k_sem_count_get(struct k_sem *sem)
{
	return sem->count;    /* 直接读取当前可用计数，不消耗信号量。 */
}

/**
  * @brief  静态定义信号量及其初始计数和上限。
  * @param  name：[输入]信号量对象标识符。
  * @param  initial_count：[输入]初始可用次数，不得超过 count_limit。
  * @param  count_limit：[输入]允许的最大计数，必须大于 0。
  * @retval 无。
  * @note   对象进入内核可迭代段；SPI 上下文以初值 1 的锁信号量串行化访问，以初值 0 的完成信号量等待通知。
  */
#define K_SEM_DEFINE(name, initial_count, count_limit) \
	STRUCT_SECTION_ITERABLE(k_sem, name) = \
		Z_SEM_INITIALIZER(name, initial_count, count_limit); \
	BUILD_ASSERT(((count_limit) != 0) && \
		     ((initial_count) <= (count_limit)) && \
			 ((count_limit) <= K_SEM_MAX_LIMIT));

/** @} */

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_work_delayable;
struct k_work_sync;

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup workqueue_apis 工作队列接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  定义工作项在线程上下文执行的处理函数。
  * @param  work：[输入/输出]非空触发此回调的工作项，在回调和可能重提交期间保持有效。
  * @retval 无。
  * @note   由相应工作队列的线程调用，不在提交者或定时中断中直接执行。
  */
typedef void (*k_work_handler_t)(struct k_work *work);

/**
  * @brief  为普通工作项绑定处理函数。
  * @param  work：[输出]非空工作项对象。
  * @param  handler：[输入]非空工作回调。
  * @retval 无。
  * @note   首次提交前初始化；仅空闲时可再次初始化以更改处理函数。可在中断调用初始化。
  */
void k_work_init(struct k_work *work,
		  k_work_handler_t handler);

/**
  * @brief  读取普通工作项的瞬时忙状态位图。
  * @param  work：[输入]非空已初始化工作项。
  * @retval DELAYED、QUEUED、RUNNING、CANCELING 的组合；0 表示此刻空闲。
  * @note   可从中断查询；状态可能在返回前后变化，需稳定判断时自行同步。
  */
int k_work_busy_get(const struct k_work *work);

/**
  * @brief  判断普通工作项是否处于非空闲状态。
  * @param  work：[输入]非空已初始化工作项。
  * @retval busy_get 非零返回 true，否则 false。
  * @note   只是瞬时快照，不能用作释放对象的同步凭据；可在中断调用。
  */
static inline bool k_work_is_pending(const struct k_work *work);

/**
  * @brief  将工作项提交给指定队列。
  * @param  queue：[输入/输出]工作队列，NULL 表示最近一次提交所用的队列。
  * @param  work：[输入/输出]非空已初始化工作项。
  * @retval 0 已提交；1 本次新入队；2 正在运行并再次排到其运行队列；-EBUSY 正在取消、队列正在排空或封闭；-EINVAL 未指定队列且没有历史队列；-ENODEV 队列未启动。
  * @note   可在中断调用；提交不复制工作对象，执行完成前不得释放。
  */
int k_work_submit_to_queue(struct k_work_q *queue,
			   struct k_work *work);

/**
  * @brief  将工作项提交给系统工作队列。
  * @param  work：[输入/输出]非空已初始化工作项。
  * @retval 0 已提交；1 本次新入队；2 正在运行并再次排到其运行队列；-EBUSY 正在取消、队列正在排空或封闭；-EINVAL 未指定队列且没有历史队列；-ENODEV 队列未启动。
  * @note   是 submit_to_queue 的系统队列包装，可在中断调用。
  */
extern int k_work_submit(struct k_work *work);

/**
  * @brief  等待最近提交的工作实例完成。
  * @param  work：[输入/输出]非空已初始化工作项。
  * @param  sync：[输入/输出]非空同步状态对象，调用返回前持续有效且调用线程和工作线程均可访问；操作完成前不得复用于其他 flush/cancel；KERNEL_COHERENCE 架构须放在一致性内存中。
  * @retval 曾等待返回 true，原已空闲返回 false。
  * @note   只供可阻塞线程使用；必须考虑调用线程和工作线程的优先级，进度依赖工作线程继续运行。不得由正在执行该工作项的队列线程对自身调用，否则行为未定义。 等待期间仍可能发生重提交，包括处理函数内的链式提交。
  */
bool k_work_flush(struct k_work *work,
		  struct k_work_sync *sync);

/**
  * @brief  尝试从队列取消普通工作项。
  * @param  work：[输入/输出]非空已初始化普通工作项。
  * @retval 返回取消步骤完成后的 busy 状态位图；0 表示取消完成，非零表示仍被引用。
  * @note   不等待正在运行的处理函数结束；取消完成前拒绝重提交。可在中断调用，需要等待时用 cancel_sync。
  */
int k_work_cancel(struct k_work *work);

/**
  * @brief  取消普通工作项并等待取消完成。
  * @param  work：[输入/输出]非空已初始化工作项。
  * @param  sync：[输入/输出]非空同步状态对象，调用返回前持续有效且调用线程和工作线程均可访问；操作完成前不得复用于其他 flush/cancel；KERNEL_COHERENCE 架构须放在一致性内存中。
  * @retval 原工作待处理或等待运行回调结束返回 true，原已空闲返回 false。
  * @note   只供可阻塞线程使用；必须考虑调用线程和工作线程的优先级，进度依赖工作线程继续运行。不得由正在执行该工作项的队列线程对自身调用，否则行为未定义。 返回时工作空闲，但其他上下文仍可在取消完成后重新提交。
  */
bool k_work_cancel_sync(struct k_work *work, struct k_work_sync *sync);

/**
  * @brief  首次启动前初始化工作队列对象。
  * @param  queue：[输出]非空工作队列对象。
  * @retval 无。
  * @note   同一对象通常只初始化一次；可在中断调用本初始化函数。
  */
void k_work_queue_init(struct k_work_q *queue);

/**
  * @brief  配置并启动工作队列的专用线程。
  * @param  queue：[输入/输出]非空，须先位于清零内存中或经 queue_init 初始化。
  * @param  stack：[输入/输出]非空合规线程栈，队列线程存续期间保持有效。
  * @param  stack_size：[输入]可用栈容量，字节，须符合线程栈宏的容量约定。
  * @param  prio：[输入]队列线程初始优先级。
  * @param  cfg：[输入]可选队列配置，NULL 使用配置结构所述默认值。
  * @retval 无。
  * @note   不能对同一队列重复启动。
  */
void k_work_queue_start(struct k_work_q *queue,
			k_thread_stack_t *stack, size_t stack_size,
			int prio, const struct k_work_queue_config *cfg);

/**
  * @brief  取得驱动工作队列执行的线程对象。
  * @param  queue：[输入]非空已初始化工作队列。
  * @retval 该队列内嵌线程的 k_tid_t。
  * @note   可据此向工作线程授予所需内核对象权限，不转移线程对象所有权。
  */
static inline k_tid_t k_work_queue_thread_get(struct k_work_q *queue);

/**
  * @brief  阻塞至工作队列排空，并可保持禁止外部提交。
  * @param  queue：[输入/输出]非空已启动队列。
  * @param  plug：[输入]true 表示排空后继续封闭队列，直至 unplug。
  * @retval 1 曾等待排空；0 无需等待；负值表示等待被中断或失败。
  * @note   排空时仅队列线程自身可继续提交。尚处于延时阶段的工作未加入队列，若要持续为空需另外取消，或用 plug 拒绝其后续提交。
  */
int k_work_queue_drain(struct k_work_q *queue, bool plug);

/**
  * @brief  解除由 drain 设置的工作队列封闭状态。
  * @param  queue：[输入/输出]非空工作队列。
  * @retval 0 解封成功；-EALREADY 原本未封闭。
  * @note   排空未结束时调用，外部提交仍须等排空完成才能进入；可在中断调用。
  */
int k_work_queue_unplug(struct k_work_q *queue);

/**
  * @brief  初始化支持延时提交的工作对象。
  * @param  dwork：[输出]非空延时工作对象。
  * @param  handler：[输入]非空工作处理函数。
  * @retval 无。
  * @note   首次调度前初始化；仅空闲时可重复初始化更换回调，可在中断调用。
  */
void k_work_init_delayable(struct k_work_delayable *dwork,
			   k_work_handler_t handler);

/**
  * @brief  由内嵌普通工作项取得其延时工作容器。
  * @param  work：[输入]必须指向 k_work_delayable.work 成员的非空地址。
  * @retval 外围 k_work_delayable 地址。
  * @note   供处理回调由 work 取得扩展数据；普通独立工作项不能传入。
  */
static inline struct k_work_delayable *
k_work_delayable_from_work(struct k_work *work);

/**
  * @brief  读取延时工作项的瞬时忙状态。
  * @param  dwork：[输入]非空已初始化延时工作。
  * @retval DELAYED、QUEUED、RUNNING、CANCELING 位图；0 表示当前空闲。
  * @note   可在中断调用；状态可立即变化，必要时加锁。
  */
int k_work_delayable_busy_get(const struct k_work_delayable *dwork);

/**
  * @brief  判断延时工作是否处于非空闲状态。
  * @param  dwork：[输入]非空已初始化延时工作。
  * @retval busy_get 非零返回 true，否则 false。
  * @note   瞬时查询不能替代取消完成同步，可在中断调用。
  */
static inline bool k_work_delayable_is_pending(
	const struct k_work_delayable *dwork);

/**
  * @brief  查询延时工作提交定时器的到期 tick 值。
  * @param  dwork：[输入]非空已初始化延时工作。
  * @retval z_timeout_expires 对其内部超时节点的查询值，单位 tick。
  * @note   不是工作回调的保证执行时刻；查询遵循当前超时实现公式，只是瞬时快照，可在中断调用。
  */
static inline k_ticks_t k_work_delayable_expires_get(
	const struct k_work_delayable *dwork);

/**
  * @brief  查询延时工作距离提交的剩余 tick 数。
  * @param  dwork：[输入]非空已初始化延时工作。
  * @retval 未调度返回 0，否则返回超时节点剩余 tick。
  * @note   不包括提交后的队列排队时间；只是瞬时快照，可在中断调用。
  */
static inline k_ticks_t k_work_delayable_remaining_get(
	const struct k_work_delayable *dwork);

/**
  * @brief  仅为空闲延时工作安排向指定队列的提交。
  * @param  queue：[输入/输出]非空目标工作队列。
  * @param  dwork：[输入/输出]非空已初始化延时工作。
  * @param  delay：[输入]提交前的超时对象，K_NO_WAIT 表示立即提交。
  * @retval 0 原已调度或提交；1 本次安排成功；立即提交时还可能返回 -EBUSY 正在取消/队列排空或封闭、-EINVAL 无有效队列、-ENODEV 队列未启动。
  * @note   即使 delay 为 K_NO_WAIT，已调度或已提交的工作也不会被改动；可在中断调用。
  */
int k_work_schedule_for_queue(struct k_work_q *queue,
			       struct k_work_delayable *dwork,
			       k_timeout_t delay);

/**
  * @brief  仅为空闲延时工作安排向系统队列的提交。
  * @param  dwork：[输入/输出]非空已初始化延时工作。
  * @param  delay：[输入]提交延时对象，K_NO_WAIT 立即提交。
  * @retval 0 原已调度或提交；1 本次安排成功；立即提交时还可能返回 -EBUSY 正在取消/队列排空或封闭、-EINVAL 无有效队列、-ENODEV 队列未启动。
  * @note   沿用 schedule_for_queue 的空闲条件和并发约定。
  */
extern int k_work_schedule(struct k_work_delayable *dwork,
				   k_timeout_t delay);

/**
  * @brief  重新安排延时工作的提交期限。
  * @param  queue：[输入/输出]非空目标工作队列。
  * @param  dwork：[输入/输出]非空已初始化延时工作。
  * @param  delay：[输入]新的提交延时对象，K_NO_WAIT 取消旧延时后立即提交。
  * @retval 延时安排成功返回 1；立即提交返回 0 已入队、1 新入队、2 运行中再次入队，或 -EBUSY 正在取消/队列排空或封闭、-EINVAL 无有效队列、-ENODEV 队列未启动。
  * @note   可改动已有延时，也可为已提交或运行中的工作安排后续提交；不会撤销已在队列中的实例，可在中断调用。
  */
int k_work_reschedule_for_queue(struct k_work_q *queue,
				 struct k_work_delayable *dwork,
				 k_timeout_t delay);

/**
  * @brief  重新安排延时工作向系统队列提交的期限。
  * @param  dwork：[输入/输出]非空已初始化延时工作。
  * @param  delay：[输入]新的提交延时对象。
  * @retval 延时安排成功返回 1；立即提交返回 0 已入队、1 新入队、2 运行中再次入队，或 -EBUSY 正在取消/队列排空或封闭、-EINVAL 无有效队列、-ENODEV 队列未启动。
  * @note   沿用 reschedule_for_queue 的并发约定，不撤销已入队实例。
  */
extern int k_work_reschedule(struct k_work_delayable *dwork,
				     k_timeout_t delay);

/**
  * @brief  立即提交尚在延时的工作，并等待其完成。
  * @param  dwork：[输入/输出]非空已初始化延时工作。
  * @param  sync：[输入/输出]非空同步状态对象，调用返回前持续有效且调用线程和工作线程均可访问；操作完成前不得复用于其他 flush/cancel；KERNEL_COHERENCE 架构须放在一致性内存中。
  * @retval 曾等待返回 true，原已空闲返回 false。
  * @note   只供可阻塞线程使用；必须考虑调用线程和工作线程的优先级，进度依赖工作线程继续运行。不得由正在执行该工作项的队列线程对自身调用，否则行为未定义。
  */
bool k_work_flush_delayable(struct k_work_delayable *dwork,
			    struct k_work_sync *sync);

/**
  * @brief  取消延时或已提交的工作，立即返回。
  * @param  dwork：[输入/输出]非空已初始化延时工作。
  * @retval 取消步骤后该项的 delayable_busy_get 状态；0 表示当前空闲。
  * @note   回调仍可能运行；取消期间拒绝提交，但不禁止重新安排延时。需要确认不再执行时用 flush_delayable 或 cancel_delayable_sync；可在中断调用。
  */
int k_work_cancel_delayable(struct k_work_delayable *dwork);

/**
  * @brief  取消延时工作并等待其成为空闲。
  * @param  dwork：[输入/输出]非空已初始化延时工作。
  * @param  sync：[输入/输出]非空同步状态对象，调用返回前持续有效且调用线程和工作线程均可访问；操作完成前不得复用于其他 flush/cancel；KERNEL_COHERENCE 架构须放在一致性内存中。
  * @retval 原工作非空闲返回 true，原已空闲返回 false。
  * @note   只供可阻塞线程使用；必须考虑调用线程和工作线程的优先级，进度依赖工作线程继续运行。不得由正在执行该工作项的队列线程对自身调用，否则行为未定义。 取消期间拒绝提交，但不阻止其他上下文重新安排延时。
  */
bool k_work_cancel_delayable_sync(struct k_work_delayable *dwork,
				  struct k_work_sync *sync);

enum {
/**
 * @cond INTERNAL_HIDDEN
 */

	/* 工作及队列标志通过原子接口更新，以满足 SMP 下的顺序一致性。 */

	/* 工作状态位可组合，至少有九种不同的稳定有效状态。 */
	K_WORK_RUNNING_BIT = 0,      /* 运行状态的位索引。 */
	K_WORK_CANCELING_BIT = 1,    /* 取消状态的位索引。 */
	K_WORK_QUEUED_BIT = 2,       /* 已入队状态的位索引。 */
	K_WORK_DELAYED_BIT = 3,      /* 延时倒计时状态的位索引。 */

	K_WORK_MASK = BIT(K_WORK_DELAYED_BIT) | BIT(K_WORK_QUEUED_BIT)
		| BIT(K_WORK_RUNNING_BIT) | BIT(K_WORK_CANCELING_BIT),    /* 合并全部动态忙状态；首行还包括延时和已入队状态。 */

	/* 工作项的静态属性位。 */
	K_WORK_DELAYABLE_BIT = 8,                        /* 延时能力静态属性的位索引，与动态忙状态分开。 */
	K_WORK_DELAYABLE = BIT(K_WORK_DELAYABLE_BIT),    /* 标记工作项允许使用延时提交接口。 */

	/* 工作队列的动态状态位。 */
	K_WORK_QUEUE_STARTED_BIT = 0,                            /* 队列已经启动的状态位索引。 */
	K_WORK_QUEUE_STARTED = BIT(K_WORK_QUEUE_STARTED_BIT),    /* 队列执行线程已完成启动配置。 */
	K_WORK_QUEUE_BUSY_BIT = 1,                               /* 队列忙状态的位索引。 */
	K_WORK_QUEUE_BUSY = BIT(K_WORK_QUEUE_BUSY_BIT),          /* 工作线程当前正在处理工作。 */
	K_WORK_QUEUE_DRAIN_BIT = 2,                              /* 队列排空状态的位索引。 */
	K_WORK_QUEUE_DRAIN = BIT(K_WORK_QUEUE_DRAIN_BIT),        /* 排空期间拒绝外部提交，等待已有工作处理完成。 */
	K_WORK_QUEUE_PLUGGED_BIT = 3,                            /* 队列封闭状态的位索引。 */
	K_WORK_QUEUE_PLUGGED = BIT(K_WORK_QUEUE_PLUGGED_BIT),    /* 排空后继续拒绝新提交，直到显式解封。 */

	/* 工作队列的静态配置位。 */
	K_WORK_QUEUE_NO_YIELD_BIT = 8,                             /* 禁止工作项之间主动让出的配置位索引。 */
	K_WORK_QUEUE_NO_YIELD = BIT(K_WORK_QUEUE_NO_YIELD_BIT),    /* 设置后跳过队列处理循环的主动 yield。 */

/**
 * INTERNAL_HIDDEN @endcond
 */
	/* 以下为对外查询的瞬时工作状态位。 */

	/* 处理函数正在工作队列线程内运行；可与其他状态同时置位，通过 busy_get 查询。 */
	K_WORK_RUNNING = BIT(K_WORK_RUNNING_BIT),

	/* 工作项正在取消；可与其他状态组合，通过 busy_get 查询。 */
	K_WORK_CANCELING = BIT(K_WORK_CANCELING_BIT),

	/* 工作项已提交入队但尚未开始本次运行；可与其他状态组合。 */
	K_WORK_QUEUED = BIT(K_WORK_QUEUED_BIT),

	/* 延时工作已安排定时提交，但尚在等待定时器；可与其他状态组合。 */
	K_WORK_DELAYED = BIT(K_WORK_DELAYED_BIT),
};

/* 普通工作项，用于把回调交给工作队列线程处理。 */
struct k_work {
	/* 所有字段由工作模块自旋锁保护，只能通过内核接口访问。 */

	/* 链接到 k_work_q 待处理链表的单向节点。 */
	sys_snode_t node;

	/* 工作队列线程实际调用的处理函数，参数为本工作项。 */
	k_work_handler_t handler;

	/* 最近一次提交所使用的工作队列。 */
	struct k_work_q *queue;

	/* 工作状态位图：DELAYED、QUEUED、RUNNING 可同时存在；RUNNING 和 CANCELING 也可同时存在。 */
	uint32_t flags;
};

/* 静态普通工作项只显式指定 handler，其余成员按静态对象规则清零，初始为空闲。 */
#define Z_WORK_INITIALIZER(work_handler) { \
	.handler = work_handler, \
}

/* 延时工作容器，包含普通工作项、提交定时器和目标队列。 */
struct k_work_delayable {
	/* 真正被工作线程执行的内嵌普通工作项。 */
	struct k_work work;

	/* 控制延时提交的内核超时节点。 */
	struct _timeout timeout;

	/* 延时到期后要提交到的目标工作队列。 */
	struct k_work_q *queue;
};

/* 静态延时工作项在内嵌 work 中绑定回调并置 DELAYABLE 属性；其余状态先清零，不安排超时。 */
#define Z_WORK_DELAYABLE_INITIALIZER(work_handler) { \
	.work = { \
		.handler = work_handler, \
		.flags = K_WORK_DELAYABLE, \
	}, \
}

/**
  * @brief  静态定义并初始化延时工作项。
  * @param  work：[输入]对象标识符。
  * @param  work_handler：[输入]非空处理函数。
  * @retval 无。
  * @note   可在宏前加 static；初始化镜像会占用 ROM 并在启动时复制到 RAM，运行时 init_delayable 可避免该初始化镜像。
  */
#define K_WORK_DELAYABLE_DEFINE(work, work_handler) \
	struct k_work_delayable work \
	  = Z_WORK_DELAYABLE_INITIALIZER(work_handler)

/**
 * @cond INTERNAL_HIDDEN
 */

/* flush 等待记录：把哨兵工作排到处理目标项的队列，目标完成后执行哨兵并给信号量，唤醒等待 flush 的线程。 */
struct z_work_flusher {
	struct k_work work;    /* 排在目标实例之后执行的同步哨兵工作。 */
	struct k_sem sem;      /* 哨兵完成后给出，用于唤醒 flush 调用者。 */
};

/* 同步取消等待记录：挂到全局取消等待链表；目标工作变为空闲时移除匹配项并唤醒等待者。 */
struct z_work_canceller {
	sys_snode_t node;       /* 挂入取消等待者全局链表的节点。 */
	struct k_work *work;    /* 正在等待完成取消的目标工作项。 */
	struct k_sem sem;       /* 目标变空闲后给出，用于唤醒同步取消调用者。 */
};

/**
 * INTERNAL_HIDDEN @endcond
 */

/* flush/cancel 的调用方同步状态，调用返回前必须持续有效，且调用线程与工作线程均能访问。KERNEL_COHERENCE 架构需使用一致性内存，普通栈通常不满足；运行时断言检查该要求。 */
struct k_work_sync {
	union {
		struct z_work_flusher flusher;        /* flush 使用的同步视图。 */
		struct z_work_canceller canceller;    /* cancel 使用的同步视图；与 flusher 共用存储，不可同时使用。 */
	};
};

/* 工作队列启动的可选配置；queue_start 不保留此结构及其引用的值。 */
struct k_work_queue_config {
	/* 队列线程名称；NULL 不设置名称。 */
	const char *name;

	/* true 禁止工作项之间主动 yield；默认 false 允许让出，避免饿死其他就绪线程，包括工作回调刚唤醒的协作线程。 */
	bool no_yield;
};

/* 工作队列状态及其执行线程。 */
struct k_work_q {
	/* 实际执行工作回调的内嵌线程控制块。 */
	struct k_thread thread;

	/* 后续字段只能在持有工作模块自旋锁时访问。 */

	/* 等待执行的工作项单向链表。 */
	sys_slist_t pending;

	/* 队列为空时执行线程等待通知的队列。 */
	_wait_q_t notifyq;

	/* 等待工作队列排空的调用线程队列。 */
	_wait_q_t drainq;

	/* 队列启动、忙、排空、封闭等状态位。 */
	uint32_t flags;
};

/* 下面给出此前声明的内联接口实现；约定与声明处一致。 */

/**
  * @brief  判断普通工作项是否处于非空闲状态。
  * @param  work：[输入]非空已初始化工作项。
  * @retval busy_get 非零返回 true，否则 false。
  * @note   只是瞬时快照，不能用作释放对象的同步凭据；可在中断调用。
  */
static inline bool k_work_is_pending(const struct k_work *work)
{
	return k_work_busy_get(work) != 0;    /* 任一动态忙状态存在即判定非空闲。 */
}

/**
  * @brief  由内嵌普通工作项取得其延时工作容器。
  * @param  work：[输入]必须指向 k_work_delayable.work 成员的非空地址。
  * @retval 外围 k_work_delayable 地址。
  * @note   供处理回调由 work 取得扩展数据；普通独立工作项不能传入。
  */
static inline struct k_work_delayable *
k_work_delayable_from_work(struct k_work *work)
{
	return CONTAINER_OF(work, struct k_work_delayable, work);    /* 按成员偏移还原延时工作容器，不分配新对象。 */
}

/**
  * @brief  判断延时工作是否处于非空闲状态。
  * @param  dwork：[输入]非空已初始化延时工作。
  * @retval busy_get 非零返回 true，否则 false。
  * @note   瞬时查询不能替代取消完成同步，可在中断调用。
  */
static inline bool k_work_delayable_is_pending(
	const struct k_work_delayable *dwork)
{
	return k_work_delayable_busy_get(dwork) != 0;    /* 检查延时工作任一忙状态，而非仅检查是否在倒计时。 */
}

/**
  * @brief  查询延时工作提交定时器的到期 tick 值。
  * @param  dwork：[输入]非空已初始化延时工作。
  * @retval z_timeout_expires 对其内部超时节点的查询值，单位 tick。
  * @note   不是工作回调的保证执行时刻；查询遵循当前超时实现公式，只是瞬时快照，可在中断调用。
  */
static inline k_ticks_t k_work_delayable_expires_get(
	const struct k_work_delayable *dwork)
{


	return z_timeout_expires(&dwork->timeout);    /* 查询控制提交时机的超时节点到期值。 */
}

/**
  * @brief  查询延时工作距离提交的剩余 tick 数。
  * @param  dwork：[输入]非空已初始化延时工作。
  * @retval 未调度返回 0，否则返回超时节点剩余 tick。
  * @note   不包括提交后的队列排队时间；只是瞬时快照，可在中断调用。
  */
static inline k_ticks_t k_work_delayable_remaining_get(
	const struct k_work_delayable *dwork)
{


	return z_timeout_remaining(&dwork->timeout);    /* 查询尚未提交阶段的剩余 tick 数。 */
}

/**
  * @brief  取得驱动工作队列执行的线程对象。
  * @param  queue：[输入]非空已初始化工作队列。
  * @retval 该队列内嵌线程的 k_tid_t。
  * @note   可据此向工作线程授予所需内核对象权限，不转移线程对象所有权。
  */
static inline k_tid_t k_work_queue_thread_get(struct k_work_q *queue)
{
	return &queue->thread;    /* 返回内嵌线程控制块地址，其生命周期跟随队列对象。 */
}

/* 旧版接口兼容包装，保留弃用属性及原返回语义。 */

/**
  * @brief  以旧接口名查询工作是否忙。
  * @param  work：[输入]非空已初始化普通工作项。
  * @retval 非空闲返回 true，否则 false。
  * @note   已弃用，直接委托 k_work_is_pending，结果仍只是快照。
  */
__deprecated
static inline bool k_work_pending(const struct k_work *work)
{
	return k_work_is_pending(work);    /* 保留旧名称，复用普通工作状态判断。 */
}

/**
  * @brief  以旧接口启动专用工作队列。
  * @param  work_q：[输入/输出]非空已初始化工作队列。
  * @param  stack：[输入/输出]合规非空线程栈，队列线程存续期间保持有效。
  * @param  stack_size：[输入]合法可用栈容量，字节。
  * @param  prio：[输入]线程优先级。
  * @retval 无。
  * @note   已弃用，cfg 固定 NULL，采用默认配置；不得重复启动。
  */
__deprecated
static inline void k_work_q_start(struct k_work_q *work_q,
				  k_thread_stack_t *stack,
				  size_t stack_size, int prio)
{
	k_work_queue_start(work_q, stack, stack_size, prio, NULL);    /* 将旧参数转交新启动接口，附加配置固定使用默认值。 */
}

/* 旧延时工作类型，待对应兼容接口移除时一并移除。 */
struct k_delayed_work {
	struct k_work_delayable work;    /* 旧 API 的兼容容器，委托新的延时工作实现。 */
};

/* 旧延时工作容器包装新的延时工作初始化器，保留原类型布局。 */
#define Z_DELAYED_WORK_INITIALIZER(work_handler)  { \
	.work = Z_WORK_DELAYABLE_INITIALIZER(work_handler), \
}

/**
  * @brief  初始化旧版延时工作容器。
  * @param  work：[输出]非空旧版延时工作对象。
  * @param  handler：[输入]非空工作处理函数。
  * @retval 无。
  * @note   实际委托 k_work_init_delayable；首次使用前初始化，只能在空闲时重初始化。
  */
void k_delayed_work_init(struct k_delayed_work *work,
				       k_work_handler_t handler);

/**
  * @brief  以旧接口重新安排向指定队列提交延时工作。
  * @param  work_q：[输入/输出]非空已启动目标工作队列。
  * @param  work：[输入/输出]非空已初始化旧延时工作对象。
  * @param  delay：[输入]新的超时对象，K_NO_WAIT 表示立即提交。
  * @retval 非负提交状态统一转为 0；错误保留为 -EBUSY 正在取消/队列排空或封闭、-EINVAL 无有效队列、-ENODEV 队列未启动。
  * @note   委托 reschedule_for_queue，可改变旧期限，不撤销已经入队的实例；与旧版仅首次提交语义不同。
  */
int k_delayed_work_submit_to_queue(struct k_work_q *work_q,
						 struct k_delayed_work *work,
						 k_timeout_t delay);

/**
  * @brief  以旧接口重新安排向系统工作队列提交延时工作。
  * @param  work：[输入/输出]非空已初始化旧延时工作对象。
  * @param  delay：[输入]新的超时对象，K_NO_WAIT 表示立即提交。
  * @retval 非负提交状态统一转为 0；错误保留为 -EBUSY 正在取消/队列排空或封闭、-EINVAL 无有效队列、-ENODEV 队列未启动。
  * @note   实际委托 k_work_reschedule，保留当前 SDK 的兼容行为。
  */
int k_delayed_work_submit(struct k_delayed_work *work,
					k_timeout_t delay);

/**
  * @brief  按旧返回规则包装延时工作的非阻塞取消。
  * @param  work：[输入/输出]非空已初始化旧延时工作对象。
  * @retval 调用前待处理且取消后空闲返回 0；原待处理但仍忙返回 -EINVAL；原已空闲返回 -EALREADY。
  * @note   不等待运行中的回调完成；旧状态无法完全重建，结果还受并发竞争影响，需同步完成时使用新 cancel_sync。
  */
int k_delayed_work_cancel(struct k_delayed_work *work);

/**
  * @brief  查询旧延时工作容器是否待处理。
  * @param  work：[输入]非空已初始化旧延时工作对象。
  * @retval 非空闲返回 true，否则 false。
  * @note   已弃用，转交内嵌的新延时工作查询，结果是瞬时快照。
  */
__deprecated
static inline bool k_delayed_work_pending(struct k_delayed_work *work)
{
	return k_work_delayable_is_pending(&work->work);    /* 查询兼容容器内的新延时工作对象。 */
}

/**
  * @brief  以旧接口查询延时工作的剩余毫秒数。
  * @param  work：[输入]非空已初始化旧延时工作对象。
  * @retval 剩余 tick 向下取整换算为毫秒。
  * @note   已弃用，保留旧取整行为；未安排延时返回 0。
  */
__deprecated
static inline int32_t k_delayed_work_remaining_get(struct k_delayed_work *work)
{
	k_ticks_t rem = k_work_delayable_remaining_get(&work->work);    /* 读取旧容器所含延时工作的剩余 tick。 */

	/* 保留旧接口的向下取整；即使向上取整可能更符合等待语义，本次也不改变实现。 */
	return k_ticks_to_ms_floor32(rem);                              /* 按旧约定向下取整为毫秒，不修改精度行为。 */
}

/**
  * @brief  以旧接口查询延时工作的到期 tick 值。
  * @param  work：[输入]非空已初始化旧延时工作对象。
  * @retval 内部到期查询结果，单位 tick。
  * @note   已弃用，委托新的到期查询，不保证实际处理时刻。
  */
__deprecated
static inline k_ticks_t k_delayed_work_expires_ticks(
	struct k_delayed_work *work)
{
	return k_work_delayable_expires_get(&work->work);    /* 委托新接口取得定时提交的到期查询值。 */
}

/**
  * @brief  以旧接口查询延时工作剩余 tick 数。
  * @param  work：[输入]非空已初始化旧延时工作对象。
  * @retval 剩余 tick 数，未安排返回 0。
  * @note   已弃用，只查询定时提交阶段，不含队列执行等待。
  */
__deprecated
static inline k_ticks_t k_delayed_work_remaining_ticks(
	struct k_delayed_work *work)
{
	return k_work_delayable_remaining_get(&work->work);    /* 委托新接口读取尚余 tick。 */
}

/** @} */

struct k_work_user;

/**
 * @addtogroup workqueue_apis
 * @{
 */

/**
  * @brief  定义用户工作队列线程调用的处理函数类型。
  * @param  work：[输入/输出]非空触发本回调的用户工作对象。
  * @retval 无。
  * @note   回调在用户工作队列线程内运行，对象必须可由该线程访问。
  */
typedef void (*k_work_user_handler_t)(struct k_work_user *work);

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_work_user_q {
	struct k_queue queue;      /* 保存用户工作项地址的通用队列。 */
	struct k_thread thread;    /* 在用户模式执行回调的线程控制块。 */
};

enum {
	K_WORK_USER_STATE_PENDING,    /* 用户工作项的待处理标志位索引。 */
};

struct k_work_user {
	void *_reserved;                  /* 供通用队列实现使用的保留链接字段。 */
	k_work_user_handler_t handler;    /* 用户工作线程消费该项时调用的回调。 */
	atomic_t flags;                   /* 原子待处理标志，防止重复入队。 */
};

/**
 * INTERNAL_HIDDEN @endcond
 */

/* 用户工作项的链接保留指针设 NULL、绑定处理函数、原子标志清零。 */
#define Z_WORK_USER_INITIALIZER(work_handler) \
	{ \
	._reserved = NULL, \
	.handler = work_handler, \
	.flags = 0 \
	}

/**
  * @brief  静态定义并初始化用户工作项。
  * @param  work：[输入]对象标识符。
  * @param  work_handler：[输入]非空处理函数。
  * @retval 无。
  * @note   仅初始化对象，不立即提交；宏前可加 static。
  */
#define K_WORK_USER_DEFINE(work, work_handler) \
	struct k_work_user work = Z_WORK_USER_INITIALIZER(work_handler)

/**
  * @brief  初始化用户工作项的回调和状态。
  * @param  work：[输出]非空可写用户工作项。
  * @param  handler：[输入]非空用户工作处理函数。
  * @retval 无。
  * @note   首次提交前调用，正在排队或执行时不得重新初始化。
  */
static inline void k_work_user_init(struct k_work_user *work,
				    k_work_user_handler_t handler)
{
	*work = (struct k_work_user)Z_WORK_USER_INITIALIZER(handler);    /* 整体初始化回调、链接保留字段和原子状态为未排队。 */
}

/**
  * @brief  原子查询用户工作项是否已经挂起待处理。
  * @param  work：[输入]非空已初始化用户工作项。
  * @retval 待处理位为 1 返回 true，否则 false。
  * @note   可在中断调用；结果只是快照，使用时状态可能已变。
  */
static inline bool k_work_user_is_pending(struct k_work_user *work)
{
	return atomic_test_bit(&work->flags, K_WORK_USER_STATE_PENDING);    /* 原子读取待处理位，不改变排队状态。 */
}

/**
  * @brief  向用户模式工作队列提交一个工作项。
  * @param  work_q：[输入/输出]非空用户工作队列，调用方须有其 queue 对象权限。
  * @param  work：[输入/输出]非空已初始化工作项，队列线程须能访问且处理结束前保持有效。
  * @retval 0 成功；-EBUSY 工作已在某队列中；-ENOMEM 调用线程资源池不足。
  * @note   临时链接元数据从调用方资源池申请，消费工作时释放；原接口允许中断调用但仍需满足权限和资源池前提。
  */
static inline int k_work_user_submit_to_queue(struct k_work_user_q *work_q,
					      struct k_work_user *work)
{
	int ret = -EBUSY;                                            /* 先按已排队处理，只有成功抢到待处理位才尝试入队。 */

	/* 原子检查并占用待处理位，只有此前未排队的提交者可以继续。 */
	if (!atomic_test_and_set_bit(&work->flags,
				     K_WORK_USER_STATE_PENDING)) {
		ret = k_queue_alloc_append(&work_q->queue, work);    /* 分配链接元数据并排到用户队列，结果可能为资源不足。 */

		/* 入队失败时清除待处理位，使调用方以后可以重试提交。 */
		if (ret != 0) {                                      /* 排队失败则撤销占位，使后续提交可以重试。 */
			/* 释放失败提交占用的待处理位；续行给出具体位索引。 */
			atomic_clear_bit(&work->flags,
					 K_WORK_USER_STATE_PENDING);
		}
	}

	return ret;                                                  /* 返回重复提交、分配失败或成功状态。 */
}

/**
  * @brief  创建并启动用户模式工作队列线程。
  * @param  work_q：[输入/输出]非空用户队列，调用者须有其 thread 和 queue 权限。
  * @param  stack：[输入/输出]K_THREAD_STACK_DEFINE 创建的非空用户可用栈。
  * @param  stack_size：[输入]声明栈时的原始容量或 K_THREAD_STACK_SIZEOF 值，字节。
  * @param  prio：[输入]合法线程优先级，受 thread_create 的限制。
  * @param  name：[输入]可选名称字符串，非空时复制到线程名称缓冲。
  * @retval 无。
  * @note   可从用户模式调用，创建的执行线程也运行于用户模式。
  */
extern void k_work_user_queue_start(struct k_work_user_q *work_q,
				    k_thread_stack_t *stack,
				    size_t stack_size, int prio,
				    const char *name);

/** @} */

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_work_poll {
	struct k_work work;               /* 轮询完成后提交给工作队列的内嵌工作项。 */
	struct k_work_q *workq;           /* 触发后采用的工作队列。 */
	struct z_poller poller;           /* 内部轮询等待者状态。 */
	struct k_poll_event *events;      /* 监听事件数组，监听和提交期间不得修改。 */
	int num_events;                   /* 事件数组项数。 */
	k_work_handler_t real_handler;    /* 事件满足后由工作线程执行的原始应用回调。 */
	struct _timeout timeout;          /* 即使无事件也可触发执行的超时节点。 */
	int poll_result;                  /* 轮询完成状态，供触发工作内部处理。 */
};

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @addtogroup workqueue_apis
 * @{
 */

/**
  * @brief  静态定义并初始化普通工作项。
  * @param  work：[输入]对象标识符。
  * @param  work_handler：[输入]非空处理函数。
  * @retval 无。
  * @note   仅初始化对象，不立即提交；宏前可加 static。
  */
#define K_WORK_DEFINE(work, work_handler) \
	struct k_work work = Z_WORK_INITIALIZER(work_handler)

/**
  * @brief  静态定义并初始化旧版延时工作项。
  * @param  work：[输入]对象标识符。
  * @param  work_handler：[输入]非空处理函数。
  * @retval 无。
  * @note   仅初始化对象，不立即提交；宏前可加 static。
  */
#define K_DELAYED_WORK_DEFINE(work, work_handler)  \
	struct k_delayed_work work = Z_DELAYED_WORK_INITIALIZER(work_handler)

/**
  * @brief  初始化由轮询事件触发的工作对象。
  * @param  work：[输出]非空 k_work_poll 对象。
  * @param  handler：[输入]非空工作处理函数。
  * @retval 无。
  * @note   首次提交前调用；事件等待和回调执行阶段使用同一对象。
  */
extern void k_work_poll_init(struct k_work_poll *work,
			     k_work_handler_t handler);

/**
  * @brief  监听事件并在触发或超时后提交到指定队列。
  * @param  work_q：[输入/输出]非空目标工作队列。
  * @param  work：[输入/输出]非空触发工作对象，执行或取消前保持有效且不得修改。
  * @param  events：[输入/输出]非空事件数组，至少 num_events 项，执行或取消前存续且不得修改。
  * @param  num_events：[输入]事件数量，单位项。
  * @param  timeout：[输入]等待触发的超时对象；超时也会将工作提交执行。
  * @retval 0 已开始监听；-EINVAL 工作正在处理或已完成；-EADDRINUSE 工作正挂在其他工作队列。
  * @note   先启动轮询并返回，事件到来后才真正入队；重提尚在等待的对象会取消旧监听并采用新列表，与旧事件/队列存在竞争，必须外部同步。可在中断调用。
  */
extern int k_work_poll_submit_to_queue(struct k_work_q *work_q,
				       struct k_work_poll *work,
				       struct k_poll_event *events,
				       int num_events,
				       k_timeout_t timeout);

/**
  * @brief  监听事件并在触发或超时后提交到系统工作队列。
  * @param  work：[输入/输出]非空触发工作对象，执行或取消前保持有效且不得修改。
  * @param  events：[输入/输出]非空事件数组，至少 num_events 项，执行或取消前存续且不得修改。
  * @param  num_events：[输入]事件数量，单位项。
  * @param  timeout：[输入]等待触发的超时对象；超时也会将工作提交执行。
  * @retval 0 已开始监听；-EINVAL 工作正在处理或已完成；-EADDRINUSE 工作正挂在其他工作队列。
  * @note   先启动轮询并返回，事件到来后才真正入队；重提尚在等待的对象会取消旧监听并采用新列表，与旧事件/队列存在竞争，必须外部同步。可在中断调用。
  */
extern int k_work_poll_submit(struct k_work_poll *work,
				     struct k_poll_event *events,
				     int num_events,
				     k_timeout_t timeout);

/**
  * @brief  取消尚未触发入队的轮询工作。
  * @param  work：[输入/输出]非空触发工作对象。
  * @retval 0 取消成功；-EINVAL 已在处理或已完成。
  * @note   只能在事件尚未触发提交时取消；可在中断调用。
  */
extern int k_work_poll_cancel(struct k_work_poll *work);

/** @} */

/**
 * @defgroup msgq_apis 定长消息队列接口
 * @ingroup kernel_apis
 * @{
 */

/* 定长消息队列：消息内容复制到环形缓冲区，与仅传递节点地址的 k_queue 不同。 */
struct k_msgq {
	/* 等待发送或接收消息的线程队列。 */
	_wait_q_t wait_q;
	/* 保护环形缓冲区和计数的自旋锁。 */
	struct k_spinlock lock;
	/* 每条消息的字节数。 */
	size_t msg_size;
	/* 缓冲区最多容纳的消息条数。 */
	uint32_t max_msgs;
	/* 环形消息缓冲区起始地址。 */
	char *buffer_start;
	/* 缓冲区末端边界地址。 */
	char *buffer_end;
	/* 下一条接收消息的读取位置。 */
	char *read_ptr;
	/* 下一条发送消息的写入位置。 */
	char *write_ptr;
	/* 当前已占用的消息槽数。 */
	uint32_t used_msgs;

	_POLL_EVENT;

	/* 消息队列标志，含内部缓冲区是否动态申请。 */
	uint8_t flags;
};
/**
 * @cond INTERNAL_HIDDEN
 */


/* 初始化空消息队列：设置消息字节数和总槽数，首尾指针界定完整缓冲，读写位置均指向开头，已用数为 0。 */
#define Z_MSGQ_INITIALIZER(obj, q_buffer, q_msg_size, q_max_msgs) \
	{ \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q), \
	.msg_size = q_msg_size, \
	.max_msgs = q_max_msgs, \
	.buffer_start = q_buffer, \
	.buffer_end = q_buffer + (q_max_msgs * q_msg_size), \
	.read_ptr = q_buffer, \
	.write_ptr = q_buffer, \
	.used_msgs = 0, \
	_POLL_EVENT_OBJ_INIT(obj) \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */


/* 标记消息环形缓冲由内部动态申请，清理代码据此判断所有权。 */
#define K_MSGQ_FLAG_ALLOC	BIT(0)

/* 供查询返回的消息队列属性快照。 */
struct k_msgq_attrs {
	/* 单条消息大小，字节。 */
	size_t msg_size;
	/* 最多消息槽数，单位条。 */
	uint32_t max_msgs;
	/* 当前已用消息槽数，单位条。 */
	uint32_t used_msgs;
};


/**
  * @brief  静态定义定长消息队列及其环形缓冲区。
  * @param  q_name：[输入]对象标识符。
  * @param  q_msg_size：[输入]每条消息字节数，须是 q_align 的倍数才能保证每槽同样对齐。
  * @param  q_max_msgs：[输入]最大消息条数。
  * @param  q_align：[输入]缓冲区字节对齐，须为 2 的幂。
  * @retval 无。
  * @note   外部可用 extern struct k_msgq 引用对象；缓冲区放不初始化段。
  */
#define K_MSGQ_DEFINE(q_name, q_msg_size, q_max_msgs, q_align)		\
	static char __noinit __aligned(q_align)				\
		_k_fifo_buf_##q_name[(q_max_msgs) * (q_msg_size)];	\
	STRUCT_SECTION_ITERABLE(k_msgq, q_name) =			\
	       Z_MSGQ_INITIALIZER(q_name, _k_fifo_buf_##q_name,	\
				  q_msg_size, q_max_msgs)

/**
  * @brief  用调用方提供的环形缓冲初始化消息队列。
  * @param  msgq：[输出]非空消息队列对象。
  * @param  buffer：[输入/输出]非空缓冲区，容量至少 msg_size * max_msgs 字节，使用期间持续有效。
  * @param  msg_size：[输入]消息字节数，须与所需对齐相容。
  * @param  max_msgs：[输入]最大消息条数。
  * @retval 无。
  * @note   缓冲区按 2 的幂字节边界对齐；每消息大小须是该边界的倍数才能保持逐槽对齐。
  */
void k_msgq_init(struct k_msgq *msgq, char *buffer, size_t msg_size,
		 uint32_t max_msgs);

/**
  * @brief  申请内部环形缓冲并初始化消息队列。
  * @param  msgq：[输出]非空消息队列对象。
  * @param  msg_size：[输入]每条消息字节数。
  * @param  max_msgs：[输入]最大消息条数。
  * @retval 0 成功；-ENOMEM 线程资源池不足；-EINVAL 容量乘积溢出。
  * @note   cleanup 或 USERSPACE 下丢失最后对象引用时释放动态缓冲。
  */
__syscall int k_msgq_alloc_init(struct k_msgq *msgq, size_t msg_size,
				uint32_t max_msgs);

/**
  * @brief  清理消息队列动态申请的环形缓冲。
  * @param  msgq：[输入/输出]非空已初始化消息队列。
  * @retval 0 成功；-EBUSY 队列仍非空。
  */
int k_msgq_cleanup(struct k_msgq *msgq);

/**
  * @brief  复制一条定长消息到队列。
  * @param  msgq：[输入/输出]非空已初始化消息队列。
  * @param  data：[输入]非空可读缓冲，至少 msgq->msg_size 字节，调用完成前有效。
  * @param  timeout：[输入]等待空槽的超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 成功；-ENOMSG 未等待即失败或队列被清空；-EAGAIN 等待超时。
  * @note   不保留 data 指针、不改动消息内容；中断调用须不阻塞。
  */
__syscall int k_msgq_put(struct k_msgq *msgq, const void *data, k_timeout_t timeout);

/**
  * @brief  按先进先出顺序取出并复制一条消息。
  * @param  msgq：[输入/输出]非空已初始化消息队列。
  * @param  data：[输出]非空可写缓冲，至少 msgq->msg_size 字节。
  * @param  timeout：[输入]等待消息的超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 成功；-ENOMSG 未等待即失败；-EAGAIN 等待超时。
  * @note   中断只能使用 K_NO_WAIT。
  */
__syscall int k_msgq_get(struct k_msgq *msgq, void *data, k_timeout_t timeout);

/**
  * @brief  复制队首消息而不将其移除。
  * @param  msgq：[输入]非空已初始化消息队列。
  * @param  data：[输出]非空可写缓冲，至少 msgq->msg_size 字节。
  * @retval 0 成功；-ENOMSG 当前没有消息。
  * @note   可在中断调用。
  */
__syscall int k_msgq_peek(struct k_msgq *msgq, void *data);

/**
  * @brief  丢弃所有未接收消息。
  * @param  msgq：[输入/输出]非空已初始化消息队列。
  * @retval 无。
  * @note   等待发送的线程被解除等待并收到 -ENOMSG。
  */
__syscall void k_msgq_purge(struct k_msgq *msgq);

/**
  * @brief  查询消息队列空闲槽数。
  * @param  msgq：[输入]非空已初始化消息队列。
  * @retval max_msgs - used_msgs，单位条。
  * @note   只是瞬时计数，不替代实际发送操作。
  */
__syscall uint32_t k_msgq_num_free_get(struct k_msgq *msgq);

/**
  * @brief  取得消息队列容量和占用情况。
  * @param  msgq：[输入]非空已初始化消息队列。
  * @param  attrs：[输出]非空可写属性结构，接收消息大小、最大和已用条数。
  * @retval 无。
  */
__syscall void  k_msgq_get_attrs(struct k_msgq *msgq,
				 struct k_msgq_attrs *attrs);


/**
  * @brief  查询消息队列空闲槽数。
  * @param  msgq：[输入]非空已初始化消息队列。
  * @retval max_msgs - used_msgs，单位条。
  * @note   只是瞬时计数，不替代实际发送操作。
  */
static inline uint32_t z_impl_k_msgq_num_free_get(struct k_msgq *msgq)
{
	return msgq->max_msgs - msgq->used_msgs;    /* 总槽数减去已用槽数，得到当前空闲消息条数。 */
}

/**
  * @brief  查询已占用消息槽数。
  * @param  msgq：[输入]非空已初始化消息队列。
  * @retval used_msgs，单位条。
  * @note   只是瞬时计数。
  */
__syscall uint32_t k_msgq_num_used_get(struct k_msgq *msgq);

/**
  * @brief  查询已占用消息槽数。
  * @param  msgq：[输入]非空已初始化消息队列。
  * @retval used_msgs，单位条。
  * @note   只是瞬时计数。
  */
static inline uint32_t z_impl_k_msgq_num_used_get(struct k_msgq *msgq)
{
	return msgq->used_msgs;    /* 读取当前排队消息数，不取出消息。 */
}

/** @} */

/**
 * @defgroup mailbox_apis 邮箱接口
 * @ingroup kernel_apis
 * @{
 */

/* 邮箱消息描述符，用于发送者和接收者匹配及数据交付。 */
struct k_mbox_msg {
	/* 旧版接口兼容的内部邮箱标识。 */
	uint32_t _mailbox;
	/* 消息数据长度，字节。 */
	size_t size;
	/* 由应用定义的附加信息值。 */
	uint32_t info;
	/* 发送者提供的消息缓冲区，消息处理结束前须保持有效。 */
	void *tx_data;
	/* 旧版接口兼容的内部接收指针。 */
	void *_rx_data;
	/* 消息使用的内存池数据块描述符。 */
	struct k_mem_block tx_block;
	/* 接收方匹配的来源线程标识。 */
	k_tid_t rx_source_thread;
	/* 发送方指定的目标线程标识。 */
	k_tid_t tx_target_thread;
	/* 内部同步发送等待线程，可能为异步消息使用的虚拟线程对象。 */
	k_tid_t _syncing_thread;
#if (CONFIG_NUM_MBOX_ASYNC_MSGS > 0)
	/* 异步消息处理完成后给出的可选信号量。 */
	struct k_sem *_async_sem;
#endif
};
/* 邮箱对象，分别保存待发送、待接收线程及同步锁。 */
struct k_mbox {
	/* 携带待交付消息的发送者等待队列。 */
	_wait_q_t tx_msg_queue;
	/* 等待匹配消息的接收者等待队列。 */
	_wait_q_t rx_msg_queue;
	struct k_spinlock lock;

	_POLL_EVENT;
};
/**
 * @cond INTERNAL_HIDDEN
 */

/* 初始化邮箱的发送/接收等待队列，以及可选的轮询事件链表。 */
#define Z_MBOX_INITIALIZER(obj) \
	{ \
	.tx_msg_queue = Z_WAIT_Q_INIT(&obj.tx_msg_queue), \
	.rx_msg_queue = Z_WAIT_Q_INIT(&obj.rx_msg_queue), \
	_POLL_EVENT_OBJ_INIT(obj)		\
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
  * @brief  静态定义并初始化邮箱。
  * @param  name：[输入]对象标识符，外部可按 struct k_mbox 引用。
  * @retval 无。
  */
#define K_MBOX_DEFINE(name) \
	STRUCT_SECTION_ITERABLE(k_mbox, name) = \
		Z_MBOX_INITIALIZER(name) \

/**
  * @brief  首次使用前初始化邮箱等待队列和锁。
  * @param  mbox：[输出]非空邮箱对象。
  * @retval 无。
  */
extern void k_mbox_init(struct k_mbox *mbox);

/**
  * @brief  发送邮箱消息并等待接收方完成处理。
  * @param  mbox：[输入/输出]非空已初始化邮箱。
  * @param  tx_msg：[输入/输出]非空发送描述符，数据可以来自普通缓冲、内存池块或为空消息。
  * @param  timeout：[输入]等待接收者匹配的超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 发送完成；-ENOMSG 未等待即失败；-EAGAIN 等待接收超时。
  * @note   一旦接收者匹配，仍会无限等待其完成消息处理；timeout 不限制后半阶段。描述符及数据须在操作期间有效。
  */
extern int k_mbox_put(struct k_mbox *mbox, struct k_mbox_msg *tx_msg,
		      k_timeout_t timeout);

/**
  * @brief  异步发送邮箱消息并可在处理完成时给信号量。
  * @param  mbox：[输入/输出]非空已初始化邮箱。
  * @param  tx_msg：[输入/输出]非空发送描述符，承载缓冲、内存池块或空消息。
  * @param  sem：[输入/输出]完成后给出的已初始化信号量，NULL 表示不通知。
  * @retval 无。
  * @note   不等待接收方完成处理；消息数据及非空通知对象必须存续到异步处理完成。
  */
extern void k_mbox_async_put(struct k_mbox *mbox, struct k_mbox_msg *tx_msg,
			     struct k_sem *sem);

/**
  * @brief  接收匹配的邮箱消息，并可立即获取数据。
  * @param  mbox：[输入/输出]非空已初始化邮箱。
  * @param  rx_msg：[输入/输出]非空接收描述符，包含匹配条件和接收结果。
  * @param  buffer：[输出]足以容纳协商消息长度的可写区；NULL 延后取数和完成处理。
  * @param  timeout：[输入]等待消息的超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 接收成功；-ENOMSG 未等待即失败；-EAGAIN 等待超时。
  * @note   传 NULL 后须调用 data_get 完成后续处理，避免发送方一直等待。
  */
extern int k_mbox_get(struct k_mbox *mbox, struct k_mbox_msg *rx_msg,
		      void *buffer, k_timeout_t timeout);

/**
  * @brief  取回已接收邮箱消息的数据并完成消息处理。
  * @param  rx_msg：[输入/输出]非空、已接收但尚未完成处理的描述符。
  * @param  buffer：[输出]足够容量的可写缓冲；NULL 表示丢弃数据。
  * @retval 无。
  * @note   结束处理可释放同步发送者或触发异步完成通知；同一消息只能完成一次。
  */
extern void k_mbox_data_get(struct k_mbox_msg *rx_msg, void *buffer);

/**
  * @brief  清除邮箱中尚未交付的消息。
  * @param  mbox：[输入/输出]非空已初始化邮箱。
  * @retval 无。
  * @note   SDK 扩展接口，用于异步消息积压时丢弃旧消息；具体唤醒和资源处理以邮箱实现为准。
  */
extern void k_mbox_clear_msg(struct k_mbox *mbox);

/**
  * @brief  查询面向指定线程的邮箱待处理消息数。
  * @param  mbox：[输入]非空已初始化邮箱。
  * @param  target_thread：[输入]要筛选的目标线程标识。
  * @retval 对应线程的新消息数量。
  * @note   SDK 扩展接口；特殊线程标识的匹配规则需结合实现。
  */
extern int k_mbox_get_pending_msg_cnt(struct k_mbox *mbox, k_tid_t target_thread);


/** @} */

/**
 * @defgroup pipe_apis 管道接口
 * @ingroup kernel_apis
 * @{
 */

/* 字节流管道对象，含可选环形缓冲和读写等待队列。 */
struct k_pipe {
	unsigned char *buffer;         /* 环形缓冲区地址；无缓冲管道允许 NULL。 */
	size_t         size;           /* 缓冲区容量，字节。 */
	size_t         bytes_used;     /* 缓冲区已使用字节数。 */
	size_t         read_index;     /* 下一次从缓冲区读取的字节索引。 */
	size_t         write_index;    /* 下一次向缓冲区写入的字节索引。 */
	struct k_spinlock lock;        /* 管道读写及等待状态的同步自旋锁。 */

	struct {
		_wait_q_t      readers;    /* 等待可读数据的线程队列。 */
		_wait_q_t      writers;    /* 等待可写空间的线程队列。 */
	} wait_q;                          /* 管道读写双方的等待队列组合。 */

	uint8_t	       flags;    /* 管道属性标志。 */
};

/**
 * @cond INTERNAL_HIDDEN
 */
#define K_PIPE_FLAG_ALLOC	BIT(0)    /* 标记环形缓冲区由动态申请获得。 */

/* 设置管道缓冲地址和容量，将已用字节、读写索引及标志清零，初始化读写等待队列与锁。 */
#define Z_PIPE_INITIALIZER(obj, pipe_buffer, pipe_buffer_size)     \
	{                                                           \
	.buffer = pipe_buffer,                                      \
	.size = pipe_buffer_size,                                   \
	.bytes_used = 0,                                            \
	.read_index = 0,                                            \
	.write_index = 0,                                           \
	.lock = {},                                                 \
	.wait_q = {                                                 \
		.readers = Z_WAIT_Q_INIT(&obj.wait_q.readers),       \
		.writers = Z_WAIT_Q_INIT(&obj.wait_q.writers)        \
	},                                                          \
	.flags = 0                                                  \
	}

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
  * @brief  静态定义管道及可选环形缓冲区。
  * @param  name：[输入]管道对象标识符。
  * @param  pipe_buffer_size：[输入]环形缓冲字节容量，0 表示无缓冲。
  * @param  pipe_align：[输入]缓冲区对齐字节数，须为 2 的幂。
  * @retval 无。
  * @note   外部可按 struct k_pipe 引用；无缓冲仍可在读写者之间直接交换数据。
  */
#define K_PIPE_DEFINE(name, pipe_buffer_size, pipe_align)		\
	static unsigned char __noinit __aligned(pipe_align)		\
		_k_pipe_buf_##name[pipe_buffer_size];			\
	STRUCT_SECTION_ITERABLE(k_pipe, name) =				\
		Z_PIPE_INITIALIZER(name, _k_pipe_buf_##name, pipe_buffer_size)

/**
  * @brief  用可选外部缓冲初始化管道。
  * @param  pipe：[输出]非空管道对象。
  * @param  buffer：[输入/输出]使用期间持续有效的环形缓冲，容量至少 size；无缓冲时为 NULL。
  * @param  size：[输入]环形缓冲字节数，无缓冲时为 0。
  * @retval 无。
  */
void k_pipe_init(struct k_pipe *pipe, unsigned char *buffer, size_t size);

/**
  * @brief  释放管道动态申请的缓冲区。
  * @param  pipe：[输入/输出]非空已初始化管道。
  * @retval 0 成功；-EAGAIN 没有可清理资源。
  * @note   静态或外部提供的缓冲区不被释放。
  */
int k_pipe_cleanup(struct k_pipe *pipe);

/**
  * @brief  申请环形缓冲并初始化管道。
  * @param  pipe：[输出]非空且尚未初始化的管道对象。
  * @param  size：[输入]环形缓冲字节容量，0 不使用缓冲。
  * @retval 0 成功；-ENOMEM 线程资源池不足。
  * @note   cleanup 或 USERSPACE 下丢失最后对象引用时释放缓冲。
  */
__syscall int k_pipe_alloc_init(struct k_pipe *pipe, size_t size);

/**
  * @brief  向字节流管道写入最多指定长度的数据。
  * @param  pipe：[输入/输出]非空已初始化管道。
  * @param  data：[输入]至少 bytes_to_write 字节的可读源区，调用完成前有效。
  * @param  bytes_to_write：[输入]最大写入字节数。
  * @param  bytes_written：[输出]非空可写 size_t，接收实际写入字节数。
  * @param  min_xfer：[输入]本次成功所需最少字节数，不得超过最大写入数。
  * @param  timeout：[输入]等待传输的超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 至少写入 min_xfer 字节；-EIO 未等待且写入 0 字节；-EAGAIN 超时且写入不足 min_xfer。
  * @note   即使失败也应查看实际写入数，可能已发生部分传输。
  */
__syscall int k_pipe_put(struct k_pipe *pipe, void *data,
			 size_t bytes_to_write, size_t *bytes_written,
			 size_t min_xfer, k_timeout_t timeout);

/**
  * @brief  从字节流管道读取最多指定长度的数据。
  * @param  pipe：[输入/输出]非空已初始化管道。
  * @param  data：[输出]至少 bytes_to_read 字节的可写目标区。
  * @param  bytes_to_read：[输入]最大读取字节数。
  * @param  bytes_read：[输出]非空可写 size_t，接收实际读取字节数。
  * @param  min_xfer：[输入]本次成功所需最少字节数，不得超过最大读取数。
  * @param  timeout：[输入]等待数据的超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 至少读取 min_xfer 字节；-EINVAL 参数不合法；-EIO 未等待且读取 0 字节；-EAGAIN 超时且读取不足 min_xfer。
  * @note   失败也可能有部分数据已读出，应检查 bytes_read。
  */
__syscall int k_pipe_get(struct k_pipe *pipe, void *data,
			 size_t bytes_to_read, size_t *bytes_read,
			 size_t min_xfer, k_timeout_t timeout);

/**
  * @brief  查询管道缓冲区可读字节数。
  * @param  pipe：[输入]非空已初始化管道。
  * @retval 0 到 pipe->size 之间的字节数，无缓冲管道为 0。
  * @note   返回瞬时快照，不包含未来写入。
  */
__syscall size_t k_pipe_read_avail(struct k_pipe *pipe);

/**
  * @brief  查询管道缓冲区可写字节数。
  * @param  pipe：[输入]非空已初始化管道。
  * @retval 0 到 pipe->size 之间的字节数，无缓冲管道为 0。
  * @note   返回瞬时快照，不代表无缓冲管道无法向等待读者直接传输。
  */
__syscall size_t k_pipe_write_avail(struct k_pipe *pipe);

/** @} */

/**
 * @cond INTERNAL_HIDDEN
 */

struct k_mem_slab {
	_wait_q_t wait_q;          /* 等待空闲内存块的线程队列。 */
	struct k_spinlock lock;    /* 保护空闲链和使用统计的自旋锁。 */
	uint32_t num_blocks;       /* 内存池总块数。 */
	size_t block_size;         /* 每块容量，字节。 */
	char *buffer;              /* 所有块所在的连续缓冲区。 */
	char *free_list;           /* 空闲块链表首址，空闲块本身容纳链接字段。 */
	uint32_t num_used;         /* 当前已经分配的块数。 */
#ifdef CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION
	uint32_t max_used;         /* 启用利用率跟踪时的历史最大占用块数。 */
#endif

};

/* 设置块总数、每块大小和缓冲地址，等待队列/锁初始化，空闲链暂空、已用块数 0；空闲块链由内存池初始化阶段建立。 */
#define Z_MEM_SLAB_INITIALIZER(obj, slab_buffer, slab_block_size, \
			       slab_num_blocks) \
	{ \
	.wait_q = Z_WAIT_Q_INIT(&obj.wait_q), \
	.lock = {}, \
	.num_blocks = slab_num_blocks, \
	.block_size = slab_block_size, \
	.buffer = slab_buffer, \
	.free_list = NULL, \
	.num_used = 0, \
	}


/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @defgroup mem_slab_apis 定块内存池接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  静态定义定块内存池及缓冲区。
  * @param  name：[输入]内存池对象标识符。
  * @param  slab_block_size：[输入]每块字节数，须是 slab_align 的倍数。
  * @param  slab_num_blocks：[输入]总块数。
  * @param  slab_align：[输入]缓冲对齐字节数，须为 2 的幂。
  * @retval 无。
  * @note   实际宏用 WB_UP 确保至少按字边界布局；外部可按 struct k_mem_slab 引用。
  */
#define K_MEM_SLAB_DEFINE(name, slab_block_size, slab_num_blocks, slab_align) \
	char __noinit_named(k_mem_slab_buf_##name) \
	   __aligned(WB_UP(slab_align)) \
	   _k_mem_slab_buf_##name[(slab_num_blocks) * WB_UP(slab_block_size)]; \
	STRUCT_SECTION_ITERABLE(k_mem_slab, name) = \
		Z_MEM_SLAB_INITIALIZER(name, _k_mem_slab_buf_##name, \
					WB_UP(slab_block_size), slab_num_blocks)

/**
  * @brief  用外部缓冲初始化固定大小块的内存池。
  * @param  slab：[输出]非空定块池对象。
  * @param  buffer：[输入/输出]非空缓冲，至少 block_size * num_blocks 字节，存续到池停用。
  * @param  block_size：[输入]每块字节数，须满足字对齐及所需更大对齐的倍数关系。
  * @param  num_blocks：[输入]内存块数。
  * @retval 0 成功；-EINVAL 参数或对齐无效。
  * @note   缓冲按不小于机器字宽且为 2 的幂的字节数对齐；各块须保持同样对齐。
  */
extern int k_mem_slab_init(struct k_mem_slab *slab, void *buffer,
			   size_t block_size, uint32_t num_blocks);

/**
  * @brief  从定块内存池取得一块内存。
  * @param  slab：[输入/输出]非空已初始化定块池。
  * @param  mem：[输出]非空可写指针对象，成功时接收块首址。
  * @param  timeout：[输入]非负超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 成功；-ENOMEM 未等待即失败；-EAGAIN 等待超时；-EINVAL 参数无效。
  * @note   中断仅用 K_NO_WAIT；无多线程配置时所有等待值均按不等待处理。取得块由调用方使用，最终归还原池。
  */
extern int k_mem_slab_alloc(struct k_mem_slab *slab, void **mem,
			    k_timeout_t timeout);

/**
  * @brief  把先前取得的内存块归还原定块池。
  * @param  slab：[输入/输出]非空、分配该块的原池。
  * @param  mem：[输入]非空指针对象，其内容为此前 alloc 返回的有效块地址。
  * @retval 无。
  * @note   不得归还其他池内存、偏移地址或重复释放。
  */
extern void k_mem_slab_free(struct k_mem_slab *slab, void **mem);

/**
  * @brief  查询定块池当前已分配块数。
  * @param  slab：[输入]非空已初始化定块池。
  * @retval num_used，单位块。
  * @note   瞬时计数。
  */
static inline uint32_t k_mem_slab_num_used_get(struct k_mem_slab *slab)
{
	return slab->num_used;    /* 读取当前已经分配出去的块数。 */
}

/**
  * @brief  查询定块池历史最大使用块数。
  * @param  slab：[输入]非空已初始化定块池。
  * @retval 启用 MAX_UTILIZATION 返回 max_used；未启用返回 0。
  * @note   未启用时的 0 不代表从未分配。
  */
static inline uint32_t k_mem_slab_max_used_get(struct k_mem_slab *slab)
{
#ifdef CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION
	return slab->max_used;    /* 跟踪配置启用时返回历史最大占用。 */
#else
	ARG_UNUSED(slab);         /* 未启用历史跟踪时不访问池对象，抑制未使用参数告警。 */
	return 0;                 /* 未启用历史跟踪，返回接口的缺省统计值。 */
#endif
}

/**
  * @brief  查询定块池当前空闲块数。
  * @param  slab：[输入]非空已初始化定块池。
  * @retval num_blocks - num_used，单位块。
  * @note   瞬时计数，不能替代实际分配操作。
  */
static inline uint32_t k_mem_slab_num_free_get(struct k_mem_slab *slab)
{
	return slab->num_blocks - slab->num_used;    /* 总块数减去已分配块数，得到当前空闲块数。 */
}

/** @} */

/**
 * @addtogroup heap_apis
 * @{
 */

/* 带内核同步的堆对象：sys_heap 负责分块管理，等待队列和自旋锁负责线程间同步。 */

struct k_heap {
	struct sys_heap heap;      /* 实际管理可变大小内存块的 sys_heap。 */
	_wait_q_t wait_q;          /* 等待堆空间可用的线程队列。 */
	struct k_spinlock lock;    /* 使底层非同步堆操作具备内核并发保护的自旋锁。 */
};

/**
  * @brief  在调用方提供的内存区上初始化同步堆。
  * @param  h：[输出]非空堆对象。
  * @param  mem：[输入/输出]可写内存区，容量至少 bytes，堆使用期间持续有效。
  * @param  bytes：[输入]内存区总字节数。
  * @retval 无。
  * @note   允许传入任意起始对齐和容量，但 sys_heap 的内部对齐和元数据会占用部分字节，实际可分配容量更小。
  */
void k_heap_init(struct k_heap *h, void *mem, size_t bytes);

/**
  * @brief  从指定堆申请满足对齐要求的内存。
  * @param  h：[输入/输出]非空已初始化堆。
  * @param  align：[输入]地址对齐字节数，必须是 2 的幂。
  * @param  bytes：[输入]所需字节数。
  * @param  timeout：[输入]等待释放空间的超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 成功返回可用内存首址，无法满足返回 NULL。
  * @note   中断仅 K_NO_WAIT；无多线程配置时均不等待。最终用 k_heap_free 归还同一个堆。
  */
void *k_heap_aligned_alloc(struct k_heap *h, size_t align, size_t bytes,
			k_timeout_t timeout);

/**
  * @brief  从指定堆申请内存，必要时等待空间释放。
  * @param  h：[输入/输出]非空已初始化堆。
  * @param  bytes：[输入]所需字节数。
  * @param  timeout：[输入]等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 成功返回内存首址，超时或无法分配返回 NULL。
  * @note   中断仅 K_NO_WAIT；无多线程配置时均不等待。返回区的释放由调用方负责。
  */
void *k_heap_alloc(struct k_heap *h, size_t bytes,
				 k_timeout_t timeout);

/**
  * @brief  把从指定堆申请的内存归还原堆。
  * @param  h：[输入/输出]非空原分配堆。
  * @param  mem：[输入]原堆分配返回的首址，NULL 不执行操作。
  * @retval 无。
  * @note   不得重复释放、传偏移地址或跨堆释放。
  */
void k_heap_free(struct k_heap *h, void *mem);

/* 至少能完成一次 1 字节分配所需的手工计算最小堆容量；布局细节参见 lib/os/heap 的实现。 */
#define Z_HEAP_MIN_SIZE (sizeof(void *) > 4 ? 56 : 44)

/**
  * @brief  在指定链接段中定义静态堆缓冲和对象。
  * @param  name：[输入]堆对象标识符。
  * @param  bytes：[输入]期望内存区字节数。
  * @param  in_section：[输入]指定内存区归属的段属性。
  * @retval 无。
  * @note   启动后可像已调用 heap_init 一样使用；小容量自动扩展到可容纳元数据的最小值。
  */
#define Z_HEAP_DEFINE_IN_SECT(name, bytes, in_section)		\
	char in_section						\
	     __aligned(8) /* sys_heap 内部分块单位所需的 8 字节对齐。 */			\
	     kheap_##name[MAX(bytes, Z_HEAP_MIN_SIZE)];		\
	STRUCT_SECTION_ITERABLE(k_heap, name) = {		\
		.heap = {					\
			.init_mem = kheap_##name,		\
			.init_bytes = MAX(bytes, Z_HEAP_MIN_SIZE), \
		 },						\
	}

/**
  * @brief  在不初始化段中静态定义堆。
  * @param  name：[输入]堆对象标识符。
  * @param  bytes：[输入]期望内存区字节数。
  * @retval 无。
  * @note   启动后可直接使用；小容量自动扩展以容纳内部元数据。
  */
#define K_HEAP_DEFINE(name, bytes)				\
	Z_HEAP_DEFINE_IN_SECT(name, bytes,			\
			      __noinit_named(kheap_buf_##name))

/**
  * @brief  在不经过缓存的内存段中静态定义堆。
  * @param  name：[输入]堆对象标识符。
  * @param  bytes：[输入]期望内存区字节数。
  * @retval 无。
  * @note   启动后可直接使用；小容量自动扩展以容纳内部元数据。
  */
#define K_HEAP_DEFINE_NOCACHE(name, bytes)			\
	Z_HEAP_DEFINE_IN_SECT(name, bytes, __nocache)

/**
 * @}
 */

/**
 * @defgroup heap_apis 堆接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  从系统堆申请指定对齐的内存。
  * @param  align：[输入]地址对齐字节数，须满足堆接口支持的对齐要求。
  * @param  size：[输入]所需字节数，使用非零值。
  * @retval 成功返回内存首址，失败返回 NULL。
  * @note   与 C11 aligned_alloc 类似，但 size 不要求是 align 的整数倍；最终用 k_free 释放。
  */
extern void *k_aligned_alloc(size_t align, size_t size);

/**
  * @brief  从系统堆申请一段内存。
  * @param  size：[输入]所需字节数。
  * @retval 成功返回内存首址，失败返回 NULL。
  * @note   类似 malloc，未保证清零，由调用方最终 k_free。
  */
extern void *k_malloc(size_t size);

/**
  * @brief  释放系统堆或兼容 mem_pool_malloc 申请的内存。
  * @param  ptr：[输入]先前分配返回的首址，NULL 不执行操作。
  * @retval 无。
  * @note   不得释放静态内存、线程栈或已经释放的地址。
  */
extern void k_free(void *ptr);

/**
  * @brief  申请数组内存并清零。
  * @param  nmemb：[输入]元素数量。
  * @param  size：[输入]单个元素字节数。
  * @retval 成功返回清零区域首址，失败返回 NULL。
  * @note   容量为 nmemb * size，调用方也应避免容量溢出；最终 k_free。
  */
extern void *k_calloc(size_t nmemb, size_t size);

/** @} */

/* 内部轮询支持宏；未启用 POLL 时初始化宏为空操作。 */

#ifdef CONFIG_POLL
#define _INIT_OBJ_POLL_EVENT(obj) do { (obj)->poll_event = NULL; } while (false)
#else
#define _INIT_OBJ_POLL_EVENT(obj) do { } while (false)
#endif

/* 内部轮询类型的位索引。 */
enum _poll_types_bits {
	/* 忽略该事件，不参与本轮等待。 */
	_POLL_TYPE_IGNORE,

	/* 由 k_poll_signal_raise 触发的信号类型。 */
	_POLL_TYPE_SIGNAL,

	/* 信号量当前有可用计数。 */
	_POLL_TYPE_SEM_AVAILABLE,

	/* 通用队列、FIFO 或 LIFO 当前存在数据项。 */
	_POLL_TYPE_DATA_AVAILABLE,

	/* 消息队列当前存在消息。 */
	_POLL_TYPE_MSGQ_DATA_AVAILABLE,
	
	/* 邮箱当前存在可接收消息。 */
	_POLL_TYPE_MBOX_DATA_AVAILABLE,

	_POLL_NUM_TYPES    /* 内部轮询类型数量，用于事件位域宽度。 */
};

/* 把非零内部类型索引转换为公共位掩码；IGNORE 使用独立的 0，不能把 0 索引传入此移位式。 */
#define Z_POLL_TYPE_BIT(type) (1U << ((type) - 1U))

/* 内部轮询状态的位索引。 */
enum _poll_states_bits {
	/* 新建或复用事件时的未就绪状态。 */
	_POLL_STATE_NOT_READY,

	/* 轮询信号已触发。 */
	_POLL_STATE_SIGNALED,

	/* 信号量有可用计数。 */
	_POLL_STATE_SEM_AVAILABLE,

	/* 通用队列、FIFO 或 LIFO 有可读数据。 */
	_POLL_STATE_DATA_AVAILABLE,

	/* 队列等待被取消。 */
	_POLL_STATE_CANCELLED,

	/* 消息队列有可读消息。 */
	_POLL_STATE_MSGQ_DATA_AVAILABLE,
	
	/* 邮箱有可读消息。 */
	_POLL_STATE_MBOX_DATA_AVAILABLE,

	_POLL_NUM_STATES    /* 内部轮询状态数量，用于事件位域宽度。 */
};

/* 把非零内部状态索引转换为公共位掩码；NOT_READY 独立为 0。 */
#define Z_POLL_STATE_BIT(state) (1U << ((state) - 1U))

/* 由 32 位总宽减去标签、类型、状态和模式字段的位数，计算保留位宽。 */
#define _POLL_EVENT_NUM_UNUSED_BITS \
	(32 - (0 \
	       + 8 /* 应用标签占 8 位。 */ \
	       + _POLL_NUM_TYPES \
	       + _POLL_NUM_STATES \
	       + 1 /* 操作模式占 1 位。 */ \
	      ))

/* 内部轮询定义结束。 */


/**
 * @defgroup poll_apis 异步轮询接口
 * @ingroup kernel_apis
 * @{
 */

/* 对外轮询接口。 */

/* k_poll_event.type 的公共取值，使用对象匹配的 K_POLL_TYPE 位组合。 */
#define K_POLL_TYPE_IGNORE 0
#define K_POLL_TYPE_SIGNAL Z_POLL_TYPE_BIT(_POLL_TYPE_SIGNAL)
#define K_POLL_TYPE_SEM_AVAILABLE Z_POLL_TYPE_BIT(_POLL_TYPE_SEM_AVAILABLE)
#define K_POLL_TYPE_DATA_AVAILABLE Z_POLL_TYPE_BIT(_POLL_TYPE_DATA_AVAILABLE)
#define K_POLL_TYPE_FIFO_DATA_AVAILABLE K_POLL_TYPE_DATA_AVAILABLE
#define K_POLL_TYPE_MSGQ_DATA_AVAILABLE Z_POLL_TYPE_BIT(_POLL_TYPE_MSGQ_DATA_AVAILABLE)
#define K_POLL_TYPE_MBOX_DATA_AVAILABLE Z_POLL_TYPE_BIT(_POLL_TYPE_MBOX_DATA_AVAILABLE)

/* 公共轮询操作模式。 */
enum k_poll_modes {
	/* 只通知对象曾经可用，不将其所有权或资源计数交给轮询线程。 */
	K_POLL_MODE_NOTIFY_ONLY = 0,

	K_POLL_NUM_MODES    /* 轮询模式枚举边界，不能作为有效模式传入。 */
};

/* k_poll_event.state 的公共状态位。 */
#define K_POLL_STATE_NOT_READY 0
#define K_POLL_STATE_SIGNALED Z_POLL_STATE_BIT(_POLL_STATE_SIGNALED)
#define K_POLL_STATE_SEM_AVAILABLE Z_POLL_STATE_BIT(_POLL_STATE_SEM_AVAILABLE)
#define K_POLL_STATE_DATA_AVAILABLE Z_POLL_STATE_BIT(_POLL_STATE_DATA_AVAILABLE)
#define K_POLL_STATE_FIFO_DATA_AVAILABLE K_POLL_STATE_DATA_AVAILABLE
#define K_POLL_STATE_MSGQ_DATA_AVAILABLE Z_POLL_STATE_BIT(_POLL_STATE_MSGQ_DATA_AVAILABLE)
#define K_POLL_STATE_CANCELLED Z_POLL_STATE_BIT(_POLL_STATE_CANCELLED)
#define K_POLL_STATE_MBOX_DATA_AVAILABLE Z_POLL_STATE_BIT(_POLL_STATE_MBOX_DATA_AVAILABLE)

/* 公共轮询信号对象。 */
struct k_poll_signal {
	/* 内部轮询等待事件链表，应用不得直接修改。 */
	sys_dlist_t poll_events;

	/* 触发标志：raise 后保持为 1，应用显式 reset 后才恢复 0。 */
	unsigned int signaled;

	/* 通过 raise 保存的应用结果值，供 check 取回。 */
	int result;
};

/* 静态轮询信号初始化空事件链表，触发位与结果值均为 0。 */
#define K_POLL_SIGNAL_INITIALIZER(obj) \
	{ \
	.poll_events = SYS_DLIST_STATIC_INIT(&obj.poll_events), \
	.signaled = 0, \
	.result = 0, \
	}
/* 轮询事件对象，记录监听对象、事件类型和返回状态。 */
struct k_poll_event {
	/* 内部事件链接节点，应用不得直接修改。 */
	sys_dnode_t _node;

	/* 内部轮询等待者指针，应用不得直接修改。 */
	struct z_poller *poller;

	/* 应用自定义的 8 位标签，接口不解释、不修改。 */
	uint32_t tag:8;

	/* 监听类型位图，只能组合适用于同一被监听对象的类型。 */
	uint32_t type:_POLL_NUM_TYPES;

	/* 就绪、取消等事件状态位图。 */
	uint32_t state:_POLL_NUM_STATES;

	/* enum k_poll_modes 中的操作模式，目前采用仅通知模式。 */
	uint32_t mode:1;

	/* 32 位组合字中未使用的保留位。 */
	uint32_t unused:_POLL_EVENT_NUM_UNUSED_BITS;

	/* 按事件类型选择对应对象指针，联合体成员共享同一地址存储。 */
	union {
		void *obj;                       /* 不区分类型的对象地址视图。 */
		struct k_poll_signal *signal;    /* SIGNAL 类型对应的轮询信号。 */
		struct k_sem *sem;               /* SEM_AVAILABLE 类型对应的信号量。 */
		struct k_fifo *fifo;             /* FIFO 数据可用类型对应对象。 */
		struct k_queue *queue;           /* 通用队列数据可用类型对应对象。 */
		struct k_msgq *msgq;             /* 消息可用类型对应的定长消息队列。 */
		struct k_mbox *mbox;             /* 邮箱消息可用类型对应的邮箱。 */
	};
};

/* 构造事件：等待者为空、指定类型/模式/对象，初始状态 NOT_READY 且保留位为 0；未显式指定的标签在聚合初始化中清零。 */
#define K_POLL_EVENT_INITIALIZER(_event_type, _event_mode, _event_obj) \
	{ \
	.poller = NULL, \
	.type = _event_type, \
	.state = K_POLL_STATE_NOT_READY, \
	.mode = _event_mode, \
	.unused = 0, \
	{ \
		.obj = _event_obj, \
	}, \
	}

/* 构造带应用标签的静态轮询事件：保存类型/模式/对象和标签，状态 NOT_READY，保留位清零。 */
#define K_POLL_EVENT_STATIC_INITIALIZER(_event_type, _event_mode, _event_obj, \
					event_tag) \
	{ \
	.tag = event_tag, \
	.type = _event_type, \
	.state = K_POLL_STATE_NOT_READY, \
	.mode = _event_mode, \
	.unused = 0, \
	{ \
		.obj = _event_obj, \
	}, \
	}

/**
  * @brief  初始化可放入 k_poll 数组的单个事件。
  * @param  event：[输出]非空事件对象。
  * @param  type：[输入]适用于同一 obj 的类型位组合，K_POLL_TYPE_IGNORE 禁用该事件。
  * @param  mode：[输入]当前使用 K_POLL_MODE_NOTIFY_ONLY。
  * @param  obj：[输入/输出]与 type 对应的有效内核对象或轮询信号，监听期间存续。
  * @retval 无。
  */

extern void k_poll_event_init(struct k_poll_event *event, uint32_t type,
			      int mode, void *obj);

/**
  * @brief  同时等待多个事件中的一个或多个就绪。
  * @param  events：[输入/输出]非空、已初始化的事件数组，容量至少 num_events 项。
  * @param  num_events：[输入]事件条数。
  * @param  timeout：[输入]等待超时对象、K_NO_WAIT 或 K_FOREVER。
  * @retval 0 至少一个事件就绪；-EAGAIN 超时；-EINTR 至少一个事件被取消；用户态还可能 -ENOMEM 资源池不足、-EINVAL 参数错误。
  * @note   仅通知，不占有对象；直接等待该对象的线程优先于轮询者。返回后遍历所有 state，-EINTR 时其他输出状态仍有效。再次使用前把 state 重置 NOT_READY；用户态需要临时资源池分配。
  */

__syscall int k_poll(struct k_poll_event *events, int num_events,
		     k_timeout_t timeout);

/**
  * @brief  初始化轮询信号以供 raise 触发。
  * @param  sig：[输出]非空轮询信号对象。
  * @retval 无。
  */

__syscall void k_poll_signal_init(struct k_poll_signal *sig);

/**
  * @brief  将轮询信号复位为未触发。
  * @param  sig：[输入/输出]非空已初始化轮询信号。
  * @retval 无。
  * @note   复用信号前由应用显式复位，避免后续 poll 因旧触发立即返回。
  */
__syscall void k_poll_signal_reset(struct k_poll_signal *sig);

/**
  * @brief  查询轮询信号的触发标志和结果值。
  * @param  sig：[输入]非空已初始化轮询信号。
  * @param  signaled：[输出]非空可写无符号整数，非零表示已触发。
  * @param  result：[输出]非空可写整数，未触发时其结果值未定义。
  * @retval 无。
  */
__syscall void k_poll_signal_check(struct k_poll_signal *sig,
				   unsigned int *signaled, int *result);

/**
  * @brief  触发轮询信号并保存应用结果值。
  * @param  sig：[输入/输出]非空已初始化轮询信号。
  * @param  result：[输入]保存到信号对象的应用结果。
  * @retval 0 通知成功；-EAGAIN 轮询者的超时正在到期。
  * @note   即使返回 -EAGAIN 也会写结果并置触发位，下一次 poll 能看到；触发位一直保留到应用 reset，复用前须复位。
  */

__syscall int k_poll_signal_raise(struct k_poll_signal *sig, int result);

/**
  * @brief  把指定状态分发给内核对象的轮询等待者。
  * @param  events：[输入/输出]非空轮询事件链表。
  * @param  state：[输入]要发布的 K_POLL_STATE 位图。
  * @retval 无。
  * @note   内部通知入口；两个事件分发函数的目标等待者差异由 poll 实现决定，调用方必须遵守其锁约定。
  */
extern void z_handle_obj_poll_events(sys_dlist_t *events, uint32_t state);

/**
  * @brief  通知事件链表中属于指定线程的首个匹配轮询者。
  * @param  events：[输入/输出]非空事件链表。
  * @param  state：[输入]要通知的事件状态位图。
  * @param  tid：[输入]待匹配的轮询线程标识。
  * @retval 无。
  * @note   匹配后摘除对应事件并通知；无匹配不操作，内部调用者须遵守轮询锁约定。
  */
extern void z_handle_obj_poll_thread_events(sys_dlist_t *events, uint32_t state, k_tid_t tid);

/** @} */

/**
 * @defgroup cpu_idle_apis CPU 空闲接口
 * @ingroup kernel_apis
 * @{
 */

/**
  * @brief  让 CPU 进入空闲等待，直至有事件唤醒。
  * @param  无。
  * @retval 无。
  * @note   普通系统应由 idle 线程统一负责；单线程系统可由唯一线程调用。部分架构返回前无条件解除中断屏蔽，不能假定保留原屏蔽状态。
  */
static inline void k_cpu_idle(void)
{
	arch_cpu_idle();    /* 进入架构空闲流程，等待可唤醒 CPU 的事件。 */
}

/**
  * @brief  从已锁中断状态原子地解除屏蔽并进入空闲。
  * @param  key：[输入]之前 irq_lock 返回的屏蔽键。
  * @retval 无。
  * @note   避免先开中断、后入低功耗之间丢失唤醒机会；唤醒后恢复效果相当于 irq_unlock(key)，具体由架构实现。
  */
static inline void k_cpu_atomic_idle(unsigned int key)
{
	arch_cpu_atomic_idle(key);    /* 由架构保证解除屏蔽和进入空闲之间不丢失唤醒。 */
}

/**
 * @}
 */

/**
 * @internal
 */
#ifdef ARCH_EXCEPT
/* 当前架构可直接通过 ARCH_EXCEPT 触发处理器异常。 */
#define z_except_reason(reason)	ARCH_EXCEPT(reason)
#else

#if !defined(CONFIG_ASSERT_NO_FILE_INFO)
/* 允许文件信息时打印异常触发的文件和行号；禁止文件信息时宏不产生输出。 */
#define __EXCEPT_LOC() __ASSERT_PRINT("@ %s:%d\n", __FILE__, __LINE__)
#else
/* 允许文件信息时打印异常触发的文件和行号；禁止文件信息时宏不产生输出。 */
#define __EXCEPT_LOC()
#endif

/* 不支持 ARCH_EXCEPT 的架构使用此后备路径；没有真实异常栈帧可确定故障 PC，因此先打印文件和行号，再进入致命错误处理。 */
#define z_except_reason(reason) do { \
		__EXCEPT_LOC();              \
		z_fatal_error(reason, NULL); \
	} while (false)

#endif    /* _ARCH__EXCEPT */

/**
  * @brief  因不可恢复的线程运行错误进入致命处理。
  * @param  无。
  * @retval 最终行为由致命错误处理器决定，正常情况下不继续原线程。
  * @note   传递 K_ERR_KERNEL_OOPS；中断中使用时默认处理器会按不可恢复系统错误处理。
  */
#define k_oops()	z_except_reason(K_ERR_KERNEL_OOPS)

/**
  * @brief  因不可恢复的内核错误进入系统致命处理。
  * @param  无。
  * @retval 最终行为由致命错误处理器决定，正常情况下不返回。
  * @note   向架构异常或后备 fatal 处理传递 K_ERR_KERNEL_PANIC。
  */
#define k_panic()	z_except_reason(K_ERR_KERNEL_PANIC)

/* 下列为一个或多个公共接口使用的私有内核入口。 */

/**
  * @brief  初始化线程调度基础字段。
  * @param  thread_base：[输出]非空线程基础控制块。
  * @param  priority：[输入]初始调度优先级。
  * @param  initial_state：[输入]初始线程状态位图。
  * @param  options：[输入]线程选项位图。
  * @retval 无。
  * @note   私有初始化入口，须在线程进入运行或等待队列前调用。
  */
extern void z_init_thread_base(struct _thread_base *thread_base,
			      int priority, uint32_t initial_state,
			      unsigned int options);

#ifdef CONFIG_MULTITHREADING

/**
  * @brief  创建链接段内静态声明的线程。
  * @param  无。
  * @retval 无。
  * @note   MULTITHREADING 启用时由启动流程调用；具体创建和延时启动由线程实现负责。
  */
extern void z_init_static_threads(void);
#else
/* 无多线程构建时，静态线程初始化宏不执行任何操作。 */
#define z_init_static_threads() do { } while (false)
#endif

/**
  * @brief  查询当前线程是否属于关键系统线程。
  * @param  无。
  * @retval 关键线程返回 true，否则 false。
  * @note   私有线程选项查询。
  */
extern bool z_is_thread_essential(void);

#ifdef CONFIG_SMP

/**
  * @brief  等待启动标志后为从 CPU 建立虚拟当前线程。
  * @param  arg：[输入]非空 atomic_t 启动标志地址，由启动 CPU 更新。
  * @param  thread：[输出]非空临时线程控制块。
  * @retval 无。
  * @note   仅 SMP 多 CPU 启动分支；标志未置位时持续自旋，无超时。
  */
void z_smp_thread_init(void *arg, struct k_thread *thread);

/**
  * @brief  使从 CPU 从虚拟线程切入正常调度。
  * @param  无。
  * @retval 无。
  * @note   仅 SMP 分支，内部调用 z_swap_unlocked。
  */
void z_smp_thread_swap(void);
#endif

/**
  * @brief  处理内核定时器超时节点到期。
  * @param  t：[输入/输出]非空、属于 k_timer 的超时节点。
  * @retval 无。
  * @note   由超时系统调用，触发到期状态、可选回调及周期重装；具体锁和回调时序以定时器实现为准。
  */
extern void z_timer_expiration_handler(struct _timeout *t);

#ifdef CONFIG_PRINTK

/**
  * @brief  向控制台输出指定长度字符区。
  * @param  c：[输入]可读字符区，至少 n 字节，不要求以零结束。
  * @param  n：[输入]输出字节数。
  * @retval 无。
  * @note   仅 CONFIG_PRINTK 分支声明，字符串内容原样输出。
  */
__syscall void k_str_out(char *c, size_t n);
#endif

/**
  * @brief  关闭指定线程的浮点上下文保存。
  * @param  thread：[输入/输出]非空、当前已启用浮点支持的线程对象。
  * @retval 0 成功；-ENOTSUP 架构未实现；-EINVAL 不满足关闭条件。
  * @note   架构可能限制能对哪个线程或何种上下文调用，须遵守 arch_float_disable 的约定。
  */
__syscall int k_float_disable(struct k_thread *thread);

/**
  * @brief  启用指定线程的浮点上下文管理。
  * @param  thread：[输入/输出]非空目标线程。
  * @param  options：[输入]架构相关浮点选项；x86 的 K_FP_REGS 涵盖 x87/MMX，K_SSE_REGS 还涵盖 SSE。
  * @retval 0 成功；-ENOTSUP 架构未实现；-EINVAL 不满足启用条件。
  * @note   架构可初始化浮点现场，并在后续调度时提供有效 FPU 状态或恢复线程自有状态；限制以 arch_float_enable 为准。当前 Cortex-M 的 arch_float_enable 直接返回 -ENOTSUP，不能按旧公共说明推定可动态启用。
  */
__syscall int k_float_enable(struct k_thread *thread, unsigned int options);

#ifdef CONFIG_THREAD_RUNTIME_STATS

/**
  * @brief  取得指定线程的运行时间统计。
  * @param  thread：[输入]非空目标线程标识。
  * @param  stats：[输出]非空可写统计对象。
  * @retval 0 成功；-EINVAL 参数为空。
  * @note   仅 THREAD_RUNTIME_STATS 启用时声明。
  */
int k_thread_runtime_stats_get(k_tid_t thread,
			       k_thread_runtime_stats_t *stats);

/**
  * @brief  取得所有线程的汇总运行时间统计。
  * @param  stats：[输出]非空可写统计对象。
  * @retval 0 成功；-EINVAL 输出对象为空。
  * @note   仅 THREAD_RUNTIME_STATS 启用时声明。
  */
int k_thread_runtime_stats_all_get(k_thread_runtime_stats_t *stats);

#endif

#ifdef __cplusplus
}
#endif

#include <tracing/tracing.h>
#include <syscalls/kernel.h>

#endif    /* !_ASMLANGUAGE */

#endif    /* ZEPHYR_INCLUDE_KERNEL_H_ */
