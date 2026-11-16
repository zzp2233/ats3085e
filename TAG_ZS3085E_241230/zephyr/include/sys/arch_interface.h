/*
 * Copyright (c) 2019 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @defgroup arch-interface 架构接口
 * @brief 供公共内核头内部使用的架构接口合同。
 * 公共宏及内联函数需要的架构原型在此声明；其他内核私有接口在 kernel_arch_interface.h。
 * arch/cpu.h 最终须引入这些宏和内联函数的完整定义。此文件包含多架构能力合同，不能把全部分支视为当前 ATS 目标已启用。
 */
#ifndef ZEPHYR_INCLUDE_SYS_ARCH_INTERFACE_H_
#define ZEPHYR_INCLUDE_SYS_ARCH_INTERFACE_H_

#ifndef _ASMLANGUAGE
/* 非汇编编译时引入编译器属性、定宽类型、当前架构实现和中断测试回调类型。 */
#include <toolchain.h>
#include <stddef.h>
#include <zephyr/types.h>
#include <arch/cpu.h>
#include <irq_offload.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 为避免循环依赖，不在此包含 kernel.h；只前置声明所需结构体。 */
struct k_thread;
struct k_mem_domain;

/* 栈元素的公共类型别名；真实元素布局和对齐由架构定义，不能将栈字节数和元素数混用。 */
typedef struct z_thread_stack_element k_thread_stack_t;

/* 线程入口回调类型的实际 typedef：由调度器以 p1/p2/p3 三个输入指针调用，各参数可否为空及数据寿命由具体入口约定；无返回值，入口返回后线程结束。 */
typedef void (*k_thread_entry_t)(void *p1, void *p2, void *p3);

/**
 * @defgroup arch-timing 架构计时接口
 * @ingroup arch-interface
 * @{
 */

/**
  * @brief  取得架构提供的 32 位硬件周期计数。
  * @param  无。
  * @retval 当前周期值，溢出按 32 位无符号数回绕。
  * @note   当前 Actions 目标由系统定时器接口提供；频率依据构建配置，不能直接当作毫秒。
  */
static inline uint32_t arch_k_cycle_get_32(void);

/** @} */


/**
 * @addtogroup arch-threads
 * @{
 */

/**
 * @def ARCH_THREAD_STACK_RESERVED
 * 线程栈对象中为架构私有数据保留的容量，公共层通过 K_THREAD_STACK_RESERVED 使用。
 */

/**
 * @def ARCH_STACK_PTR_ALIGN
 * CPU 栈指针必须满足的字节对齐，同时受硬件和 ABI 调用规范约束。
 */

/**
 * @def ARCH_THREAD_STACK_OBJ_ALIGN(size)
 * 可选的线程栈对象最低地址对齐规则，参数 size 为栈容量。
 */

/**
 * @def ARCH_THREAD_STACK_SIZE_ADJUST(size)
 * 可选的可用栈容量向上调整规则，包含线程上下文和 TLS，不包含平台保留区域；重复调整必须得到同样结果。未定义时按 MPU 的二次幂约束或栈指针对齐规则处理。
 */

/**
 * @def ARCH_KERNEL_STACK_RESERVED
 * 内核专用栈为 MPU 溢出保护保留的字节数；可选，缺省为 0，若保护大小依上下文变化，此值取最小保留量，其余按需划出。
 */

/**
 * @def ARCH_KERNEL_STACK_OBJ_ALIGN
 * 内核专用栈对象最低地址的对齐要求。
 */

/** @} */

/**
 * @addtogroup arch-pm
 * @{
 */

/**
  * @brief  在当前 CPU 上进入架构支持的省电空闲状态。
  * @param  无。
  * @retval 无。
  * @note   由内核空闲流程调用；没有省电指令的架构可以直接返回。通常在唤醒中断处理后返回，但接口不作硬性保证。
  */
void arch_cpu_idle(void);

/**
  * @brief  原子地允许中断并进入省电等待，避免检查条件到睡眠之间丢失唤醒。
  * @param  key：[输入]先前 arch_irq_lock 返回的中断状态键。
  * @retval 无。
  * @note   允许中断与进入低功耗必须构成不可丢失唤醒的操作；醒来后按 key 恢复原锁定状态。
  */
void arch_cpu_atomic_idle(unsigned int key);

/** @} */


/**
 * @addtogroup arch-smp
 * @{
 */

/**
  * @brief  其他 CPU 启动完成后的入口回调。
  * @param  data：[输入]架构启动流程传入的上下文，可为空，语义由调用方定义。
  * @retval 不返回。
  * @note   由 arch_start_cpu 使用，不是普通可返回的任务函数。
  */
typedef FUNC_NORETURN void (*arch_cpustart_t)(void *data);

/**
  * @brief  启动指定的其他 CPU，并使其进入给定入口。
  * @param  cpu_num：[输入]CPU 编号；启动主 CPU 为 0，其他 CPU 连续编号。
  * @param  stack：[输入/输出]该 CPU 的有效栈区域，不可为空，运行期间必须持续有效。
  * @param  sz：[输入]栈容量，单位字节，须满足架构要求。
  * @param  fn：[输入]该 CPU 的不返回入口，不可为空。
  * @param  arg：[输入]原样传给 fn 的上下文指针，可为空，使用期须有效。
  * @retval 无；返回时目标 CPU 已开始运行。
  * @note   适用支持多 CPU 启动的架构；通常由 SMP 初始化调用。新 CPU 的中断已初始化但处于关闭状态，入口再按架构规则开放。
  */
void arch_start_cpu(int cpu_num, k_thread_stack_t *stack, int sz,
		    arch_cpustart_t fn, void *arg);

/**
  * @brief  查询指定 CPU 的运行/供电活动状态。
  * @param  cpu_num：[输入]平台支持的 CPU 编号。
  * @retval true 表示活动，false 表示未活动。
  * @note   状态定义由架构实现确定；当前单核 SPI 流程不使用其他 CPU 启动接口。
  */
bool arch_cpu_active(int cpu_num);

/** @} */


/**
 * @addtogroup arch-irq
 * @{
 */

#ifndef CONFIG_DISABLE_IRQ_STAT

/**
  * @brief  保存并锁定当前 CPU 的普通中断。
  * @param  无。
  * @retval 调用前的中断状态键，供对应 unlock 恢复。
  * @note   当前 ARM Mainline 实现使用 BASEPRI，仅屏蔽受其优先级门限控制的中断，不等同于屏蔽全部异常；CONFIG_DISABLE_IRQ_STAT 决定内联或外部包装。
  */
static inline unsigned int arch_irq_lock(void);

/**
  * @brief  按保存的状态键恢复当前 CPU 的中断屏蔽状态。
  * @param  key：[输入]同一 CPU 上配对 arch_irq_lock 返回的状态键。
  * @retval 无。
  * @note   嵌套锁须按逆序恢复；不会因为调用 unlock 就无条件开放全部中断。
  */
static inline void arch_irq_unlock(unsigned int key);

#else

/**
  * @brief  保存并锁定当前 CPU 的普通中断。
  * @param  无。
  * @retval 调用前的中断状态键，供对应 unlock 恢复。
  * @note   当前 ARM Mainline 实现使用 BASEPRI，仅屏蔽受其优先级门限控制的中断，不等同于屏蔽全部异常；CONFIG_DISABLE_IRQ_STAT 决定内联或外部包装。
  */
extern unsigned int arch_irq_lock(void);

/**
  * @brief  按保存的状态键恢复当前 CPU 的中断屏蔽状态。
  * @param  key：[输入]同一 CPU 上配对 arch_irq_lock 返回的状态键。
  * @retval 无。
  * @note   嵌套锁须按逆序恢复；不会因为调用 unlock 就无条件开放全部中断。
  */
extern void arch_irq_unlock(unsigned int key);

#endif

/**
  * @brief  判断给定状态键是否代表此前普通中断未锁定。
  * @param  key：[输入]arch_irq_lock 返回的状态键。
  * @retval true 表示按此键恢复会解除对应中断锁；false 表示仍锁定。
  * @note   仅检查键值，不修改硬件状态；与架构的屏蔽规则对应。
  */
static inline bool arch_irq_unlocked(unsigned int key);

/**
  * @brief  禁止指定的硬件中断线。
  * @param  irq：[输入]架构对应的有效中断线编号。
  * @retval 无。
  * @note   禁止期间到达的中断是否锁存由硬件决定，接口不保证重使能后补发；调用方须能容忍丢失事件。
  */
void arch_irq_disable(unsigned int irq);

/**
  * @brief  允许指定的硬件中断线。
  * @param  irq：[输入]有效中断线编号。
  * @retval 无。
  * @note   需先安装处理程序并完成设备配置；CPU 级中断锁仍可能阻止其执行。
  */
void arch_irq_enable(unsigned int irq);

/**
  * @brief  查询指定硬件中断线是否已使能。
  * @param  irq：[输入]有效中断线编号。
  * @retval 非零表示已使能，0 表示未使能。
  * @note   查询线路状态，不保证当前 CPU 屏蔽门限允许立即处理中断。
  */
int arch_irq_is_enabled(unsigned int irq);

/**
  * @brief  动态安装指定中断线的处理函数。
  * @param  irq：[输入]有效硬件中断线编号。
  * @param  priority：[输入]架构支持的中断优先级，数值规则由架构定义。
  * @param  routine：[输入]中断服务函数，不可为空；接收 parameter。
  * @param  parameter：[输入]传给服务函数的上下文，可为空，须在注册有效期内保持可访问。
  * @param  flags：[输入]架构定义的 IRQ 配置位。
  * @retval 分配给该中断的向量编号；错误约定须查对应架构实现。
  * @note   动态中断能力须由配置和架构支持；处理函数在中断上下文运行，不应阻塞。
  */
int arch_irq_connect_dynamic(unsigned int irq, unsigned int priority,
			     void (*routine)(const void *parameter),
			     const void *parameter, uint32_t flags);

/**
 * @def ARCH_IRQ_CONNECT(irq, pri, isr, arg, flags)
 * 静态中断连接的架构入口，参数依次是中断号、优先级、服务函数、上下文和标志，供 IRQ_CONNECT 使用。
 */

/**
 * @def ARCH_IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p)
 * 直接中断的架构连接宏，直接入口承担普通分派器原先的部分工作，参数及限制见 IRQ_DIRECT_CONNECT。
 */

/**
 * @def ARCH_ISR_DIRECT_PM()
 * 直接中断中按电源管理规则通知或恢复内核的架构钩子。
 */

/**
 * @def ARCH_ISR_DIRECT_HEADER()
 * 直接中断服务例程入口的架构准备操作。
 */

/**
 * @def ARCH_ISR_DIRECT_FOOTER(swap)
 * 直接中断退出操作；swap 参数控制是否需要考虑线程重新调度。
 */

/**
 * @def ARCH_ISR_DIRECT_DECLARE(name)
 * 按架构调用约定声明指定名称的直接中断服务函数。
 */

/**
 * @def ARCH_EXCEPT(reason_p)
 * 用 reason_p 指定 K_ERR_ 原因码触发软件致命异常。用户态只允许 K_ERR_KERNEL_OOPS 或 K_ERR_STACK_CHK_FAIL；应以软件陷阱保留调用点异常上下文，避免提前破坏通用寄存器。
 */

#ifdef CONFIG_IRQ_OFFLOAD

/**
  * @brief  通过异常入口在中断上下文执行测试回调。
  * @param  routine：[输入]要执行的回调，不可为空，须遵守中断上下文限制。
  * @param  parameter：[输入]传给回调的上下文指针，可为空，调用期间须有效。
  * @retval 无。
  * @note   仅 CONFIG_IRQ_OFFLOAD 启用时存在；应经过普通异常分派和中断栈切换，面向测试内核 API 的中断行为。
  */
void arch_irq_offload(irq_offload_routine_t routine, const void *parameter);
#endif    /* CONFIG_IRQ_OFFLOAD 条件或包含保护结束。 */

/** @} */


/**
 * @defgroup arch-smp 架构多核接口
 * @ingroup arch-interface
 * @{
 */
#ifdef CONFIG_SMP

/**
  * @brief  取得执行当前指令的 CPU 状态对象。
  * @param  无。
  * @retval 当前 CPU 的 _cpu 对象指针。
  * @note   仅 CONFIG_SMP 声明此内联接口；指针所指 CPU 身份可能因线程迁移而改变，调用方须按用途保持合适的调度约束。
  */
static inline struct _cpu *arch_curr_cpu(void);

/**
  * @brief  向其他 CPU 发送调度中断。
  * @param  无。
  * @retval 无。
  * @note   仅 SMP 配置使用；接收 CPU 经该中断运行 z_sched_ipi，促使重新评估调度。
  */
void arch_sched_ipi(void);
#endif    /* CONFIG_SMP 条件或包含保护结束。 */

/** @} */


/**
 * @defgroup arch-userspace 架构用户态接口
 * @ingroup arch-interface
 * @{
 */

#ifdef CONFIG_USERSPACE

/**
  * @brief  以 0 个机器字参数触发用户态系统调用。
  * @param  call_id：[输入]系统调用编号，内核分派前必须做边界检查。
  * @retval 具体系统调用的机器字返回结果；无返回值的系统调用在此返回 0。
  * @note   仅 CONFIG_USERSPACE 分支；参数通过寄存器传入，内核使用独立内核栈，分派处理允许中断及抢占。返回用户态须避免泄露除返回值外的内核通用寄存器内容。
  */
static inline uintptr_t arch_syscall_invoke0(uintptr_t call_id);

/**
  * @brief  以 1 个机器字参数触发用户态系统调用。
  * @param  arg1：[输入]第 1 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  call_id：[输入]系统调用编号，内核分派前必须做边界检查。
  * @retval 具体系统调用的机器字返回结果；无返回值的系统调用在此返回 0。
  * @note   仅 CONFIG_USERSPACE 分支；参数通过寄存器传入，内核使用独立内核栈，分派处理允许中断及抢占。返回用户态须避免泄露除返回值外的内核通用寄存器内容。
  */
static inline uintptr_t arch_syscall_invoke1(uintptr_t arg1,
					     uintptr_t call_id);

/**
  * @brief  以 2 个机器字参数触发用户态系统调用。
  * @param  arg1：[输入]第 1 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg2：[输入]第 2 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  call_id：[输入]系统调用编号，内核分派前必须做边界检查。
  * @retval 具体系统调用的机器字返回结果；无返回值的系统调用在此返回 0。
  * @note   仅 CONFIG_USERSPACE 分支；参数通过寄存器传入，内核使用独立内核栈，分派处理允许中断及抢占。返回用户态须避免泄露除返回值外的内核通用寄存器内容。
  */
static inline uintptr_t arch_syscall_invoke2(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t call_id);

/**
  * @brief  以 3 个机器字参数触发用户态系统调用。
  * @param  arg1：[输入]第 1 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg2：[输入]第 2 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg3：[输入]第 3 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  call_id：[输入]系统调用编号，内核分派前必须做边界检查。
  * @retval 具体系统调用的机器字返回结果；无返回值的系统调用在此返回 0。
  * @note   仅 CONFIG_USERSPACE 分支；参数通过寄存器传入，内核使用独立内核栈，分派处理允许中断及抢占。返回用户态须避免泄露除返回值外的内核通用寄存器内容。
  */
static inline uintptr_t arch_syscall_invoke3(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3,
					     uintptr_t call_id);

/**
  * @brief  以 4 个机器字参数触发用户态系统调用。
  * @param  arg1：[输入]第 1 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg2：[输入]第 2 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg3：[输入]第 3 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg4：[输入]第 4 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  call_id：[输入]系统调用编号，内核分派前必须做边界检查。
  * @retval 具体系统调用的机器字返回结果；无返回值的系统调用在此返回 0。
  * @note   仅 CONFIG_USERSPACE 分支；参数通过寄存器传入，内核使用独立内核栈，分派处理允许中断及抢占。返回用户态须避免泄露除返回值外的内核通用寄存器内容。
  */
static inline uintptr_t arch_syscall_invoke4(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3, uintptr_t arg4,
					     uintptr_t call_id);

/**
  * @brief  以 5 个机器字参数触发用户态系统调用。
  * @param  arg1：[输入]第 1 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg2：[输入]第 2 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg3：[输入]第 3 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg4：[输入]第 4 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg5：[输入]第 5 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  call_id：[输入]系统调用编号，内核分派前必须做边界检查。
  * @retval 具体系统调用的机器字返回结果；无返回值的系统调用在此返回 0。
  * @note   仅 CONFIG_USERSPACE 分支；参数通过寄存器传入，内核使用独立内核栈，分派处理允许中断及抢占。返回用户态须避免泄露除返回值外的内核通用寄存器内容。
  */
static inline uintptr_t arch_syscall_invoke5(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3, uintptr_t arg4,
					     uintptr_t arg5,
					     uintptr_t call_id);

/**
  * @brief  以 6 个机器字参数触发用户态系统调用。
  * @param  arg1：[输入]第 1 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg2：[输入]第 2 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg3：[输入]第 3 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg4：[输入]第 4 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg5：[输入]第 5 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  arg6：[输入]第 6 个机器字参数；若承载指针，其长度、权限和可空性由具体系统调用校验。
  * @param  call_id：[输入]系统调用编号，内核分派前必须做边界检查。
  * @retval 具体系统调用的机器字返回结果；无返回值的系统调用在此返回 0。
  * @note   仅 CONFIG_USERSPACE 分支；参数通过寄存器传入，内核使用独立内核栈，分派处理允许中断及抢占。返回用户态须避免泄露除返回值外的内核通用寄存器内容。
  */
static inline uintptr_t arch_syscall_invoke6(uintptr_t arg1, uintptr_t arg2,
					     uintptr_t arg3, uintptr_t arg4,
					     uintptr_t arg5, uintptr_t arg6,
					     uintptr_t call_id);

/**
  * @brief  检查当前执行上下文是否拥有用户态权限。
  * @param  无。
  * @retval true 表示用户态，false 表示特权态。
  * @note   仅 CONFIG_USERSPACE 分支声明，判断依据由架构实现。
  */
static inline bool arch_is_user_context(void);

/**
  * @brief  取得一个内存域最多支持的分区数。
  * @param  无。
  * @retval 非负数为最大分区数量，-1 表示无固定上限。
  * @note   仅用户态内存保护流程使用；具体上限取决于 MPU/MMU 实现。
  */
int arch_mem_domain_max_partitions_get(void);

#ifdef CONFIG_ARCH_MEM_DOMAIN_DATA

/**
  * @brief  初始化内存域中的架构私有数据。
  * @param  domain：[输入/输出]待初始化内存域，不可为空；除 arch 外的成员已初始化，尚未加入线程。
  * @retval 0 表示成功，-ENOMEM 表示页表等资源不足。
  * @note   仅 CONFIG_ARCH_MEM_DOMAIN_DATA 分支；同一内存域重复初始化属于未定义行为，通用接口不保证替实现检查。
  */
int arch_mem_domain_init(struct k_mem_domain *domain);
#endif    /* CONFIG_ARCH_MEM_DOMAIN_DATA 条件或包含保护结束。 */

#ifdef CONFIG_ARCH_MEM_DOMAIN_SYNCHRONOUS_API

/**
  * @brief  在线程加入内存域时更新架构状态。
  * @param  thread：[输入/输出]待配置线程，不可为空；其 mem_domain_info 指向本次涉及的内存域。
  * @retval 无。
  * @note   仅同步内存域 API 配置；add 时线程此前不属于此域，remove 时线程指针仍指向将移出的域。
  */
void arch_mem_domain_thread_add(struct k_thread *thread);

/**
  * @brief  在线程移出内存域时更新架构状态。
  * @param  thread：[输入/输出]待配置线程，不可为空；其 mem_domain_info 指向本次涉及的内存域。
  * @retval 无。
  * @note   仅同步内存域 API 配置；add 时线程此前不属于此域，remove 时线程指针仍指向将移出的域。
  */
void arch_mem_domain_thread_remove(struct k_thread *thread);

/**
  * @brief  在内存域移除分区时同步架构状态。
  * @param  domain：[输入/输出]有效内存域对象，不可为空。
  * @param  partition_id：[输入]该内存域中的有效分区索引。
  * @retval 无。
  * @note   仅 CONFIG_ARCH_MEM_DOMAIN_SYNCHRONOUS_API 分支；移除回调运行时，通用层尚未清除分区记录或递减分区数。
  */
void arch_mem_domain_partition_remove(struct k_mem_domain *domain,
				      uint32_t partition_id);

/**
  * @brief  在内存域加入分区时同步架构状态。
  * @param  domain：[输入/输出]有效内存域对象，不可为空。
  * @param  partition_id：[输入]该内存域中的有效分区索引。
  * @retval 无。
  * @note   仅 CONFIG_ARCH_MEM_DOMAIN_SYNCHRONOUS_API 分支；移除回调运行时，通用层尚未清除分区记录或递减分区数。
  */
void arch_mem_domain_partition_add(struct k_mem_domain *domain,
				   uint32_t partition_id);
#endif    /* CONFIG_ARCH_MEM_DOMAIN_SYNCHRONOUS_API 条件或包含保护结束。 */

/**
  * @brief  核对整个缓冲区是否具备用户态访问权限。
  * @param  addr：[输入]被检查区域首址，检查地址权限而非读取其内容。
  * @param  size：[输入]区域长度，单位字节，必须大于 0。
  * @param  write：[输入]非零表示同时要求可写，0 仅要求可读。
  * @retval 0 表示校验成功，非零表示权限不符或无法确认整个区域可访问。
  * @note   不能错误地接受部分不可访问的区域；单一启用保护区覆盖全缓冲时须准确校验。跨多个区即使均可访问，某些架构也可能保守失败。
  */
int arch_buffer_validate(void *addr, size_t size, int write);

/**
  * @brief  由特权线程单向进入用户态入口。
  * @param  user_entry：[输入]用户线程入口，不可为空，使用三个参数。
  * @param  p1：[输入]入口首参数，可为空；访问权限及寿命由入口约定。
  * @param  p2：[输入]入口第二参数，可为空。
  * @param  p3：[输入]入口第三参数，可为空。
  * @retval 不返回。
  * @note   仅用户态配置；重置用户栈指针、准备提权用内核栈、降低 CPU 权限后进入用户入口。原英文标题将目标误写为 kernel mode，此处按实际用户态接口纠正。
  */
FUNC_NORETURN void arch_user_mode_enter(k_thread_entry_t user_entry,
					void *p1, void *p2, void *p3);

/**
  * @brief  以系统调用原始调用点的上下文触发内核 oops。
  * @param  ssf：[输入]系统调用保存的架构栈帧指针，不可为空，布局由架构规定。
  * @retval 不返回。
  * @note   用于系统调用参数校验失败，让异常指向用户调用位置而非校验函数内部。
  */
FUNC_NORETURN void arch_syscall_oops(void *ssf);

/**
  * @brief  安全测量可能无效的用户字符串长度。
  * @param  s：[输入]待测字符串地址，可无效但接口不得因此产生未处理故障。
  * @param  maxsize：[输入]最多检查的字节数，不计结尾零字符。
  * @param  err：[输出]有效可写 int 指针，不可为空；成功写 0，访问失败写 -1。
  * @retval 不包含零字符的长度，最大 maxsize；失败时须结合 *err 判断。
  * @note   除安全处理无效地址外，语义类似 strnlen；用户态配置使用。
  */
size_t arch_user_string_nlen(const char *s, size_t maxsize, int *err);
#endif    /* CONFIG_USERSPACE 条件或包含保护结束。 */

/**
  * @brief  判断地址是否属于无需显式缓存维护的一致性区域。
  * @param  ptr：[输入]待查询字节地址，仅检查位置，不解引用。
  * @retval true 表示该字节具备架构要求的一致性；false 表示不具备。
  * @note   只查询单字节，不检查整段边界或对齐。当前提供的是 !CONFIG_ARCH_HAS_COHERENCE 的缺省实现，忽略地址并恒返回 true；有该能力的架构须自行实现。
  */
#ifndef CONFIG_ARCH_HAS_COHERENCE
static inline bool arch_mem_coherent(void *ptr)
{
	ARG_UNUSED(ptr);    /* 缺省一致性查询不检查地址。 */
	return true;        /* 没有架构特殊一致性要求时采用恒真的兼容结果。 */
}
#endif

/**
  * @brief  在线程切换前对旧栈和新栈进行所需的一致性维护。
  * @param  old_thread：[输入/输出]被切出的有效线程；缺省空实现不访问。
  * @param  old_switch_handle：[输入]将保存的旧上下文句柄，z_swap 尚未保存时可为空。
  * @param  new_thread：[输入/输出]将运行的有效线程；缺省空实现不访问。
  * @retval 无。
  * @note   !CONFIG_KERNEL_COHERENCE 时为空实现。启用时须先写回旧栈活跃数据再发布 switch_handle；新栈应失效而非写回以免覆盖其他 CPU 的新数据。真正切换前仍可能写旧栈，架构切换实现须配合。
  */
#ifndef CONFIG_KERNEL_COHERENCE
static inline void arch_cohere_stacks(struct k_thread *old_thread,
				      void *old_switch_handle,
				      struct k_thread *new_thread)
{
	ARG_UNUSED(old_thread);           /* 缺省实现不维护旧栈。 */
	ARG_UNUSED(old_switch_handle);    /* 缺省实现不使用切换句柄。 */
	ARG_UNUSED(new_thread);           /* 缺省实现不维护新栈。 */
}
#endif

/** @} */

/**
 * @defgroup arch-gdbstub 架构 GDB 调试接口
 * @ingroup arch-interface
 * @{
 */

/**
 * @def ARCH_GDB_NUM_REGISTERS
 * 架构 GDB 寄存器数量，实际宏必须由相应架构头提供。
 */

#ifdef CONFIG_GDBSTUB

/**
  * @brief  初始化架构的 GDB 调试支持。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_GDBSTUB 配置；具体断点、寄存器和执行控制机制由架构提供。
  */
void arch_gdb_init(void);

/**
  * @brief  从调试停止状态继续执行程序。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_GDBSTUB 配置；具体断点、寄存器和执行控制机制由架构提供。
  */
void arch_gdb_continue(void);

/**
  * @brief  按架构单步机制继续执行下一步。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_GDBSTUB 配置；具体断点、寄存器和执行控制机制由架构提供。
  */
void arch_gdb_step(void);

#endif
/** @} */

/**
 * @defgroup arch_cache 架构缓存接口
 * @ingroup arch-interface
 * @{
 */

#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_HAS_ARCH_CACHE)

/**
  * @brief  启用数据缓存。
  * @param  无。
  * @retval 无。
  * @note   仅缓存管理且存在架构缓存能力时声明；缓存脏数据处理及屏障要求须以架构实现为准。本头原有第二个 arch_dcache_disable 声明重复，保持代码原状。
  */
void arch_dcache_enable(void);

/**
  * @brief  关闭数据缓存。
  * @param  无。
  * @retval 无。
  * @note   仅缓存管理且存在架构缓存能力时声明；缓存脏数据处理及屏障要求须以架构实现为准。本头原有第二个 arch_dcache_disable 声明重复，保持代码原状。
  */
void arch_dcache_disable(void);

/**
  * @brief  启用指令缓存。
  * @param  无。
  * @retval 无。
  * @note   仅缓存管理且存在架构缓存能力时声明；缓存脏数据处理及屏障要求须以架构实现为准。本头原有第二个 arch_dcache_disable 声明重复，保持代码原状。
  */
void arch_icache_enable(void);

/**
  * @brief  关闭数据缓存。
  * @param  无。
  * @retval 无。
  * @note   仅缓存管理且存在架构缓存能力时声明；缓存脏数据处理及屏障要求须以架构实现为准。本头原有第二个 arch_dcache_disable 声明重复，保持代码原状。
  */
void arch_dcache_disable(void);

/**
  * @brief  对数据缓存的整个缓存执行维护操作。
  * @param  op：[输入]缓存维护操作，表示写回、失效或两者组合，值由缓存接口定义。
  * @retval 操作状态；具体错误码由所选架构实现规定，本头未列出统一枚举。
  * @note   仅 CONFIG_CACHE_MANAGEMENT 和 CONFIG_HAS_ARCH_CACHE 同时启用；失效操作可能丢弃未写回数据，调用方须按共享内存协议使用。
  */
int arch_dcache_all(int op);

/**
  * @brief  对数据缓存的指定地址区间执行维护操作。
  * @param  addr：[输入/输出]目标缓存区域首址；size 非零时须对应有效内存，具体对齐要求由架构规定。
  * @param  size：[输入]区域大小，单位字节。
  * @param  op：[输入]缓存维护操作，表示写回、失效或两者组合，值由缓存接口定义。
  * @retval 操作状态；具体错误码由所选架构实现规定，本头未列出统一枚举。
  * @note   仅 CONFIG_CACHE_MANAGEMENT 和 CONFIG_HAS_ARCH_CACHE 同时启用；失效操作可能丢弃未写回数据，调用方须按共享内存协议使用。
  */
int arch_dcache_range(void *addr, size_t size, int op);

/**
  * @brief  对指令缓存的整个缓存执行维护操作。
  * @param  op：[输入]缓存维护操作，表示写回、失效或两者组合，值由缓存接口定义。
  * @retval 操作状态；具体错误码由所选架构实现规定，本头未列出统一枚举。
  * @note   仅 CONFIG_CACHE_MANAGEMENT 和 CONFIG_HAS_ARCH_CACHE 同时启用；失效操作可能丢弃未写回数据，调用方须按共享内存协议使用。
  */
int arch_icache_all(int op);

/**
  * @brief  对指令缓存的指定地址区间执行维护操作。
  * @param  addr：[输入/输出]目标缓存区域首址；size 非零时须对应有效内存，具体对齐要求由架构规定。
  * @param  size：[输入]区域大小，单位字节。
  * @param  op：[输入]缓存维护操作，表示写回、失效或两者组合，值由缓存接口定义。
  * @retval 操作状态；具体错误码由所选架构实现规定，本头未列出统一枚举。
  * @note   仅 CONFIG_CACHE_MANAGEMENT 和 CONFIG_HAS_ARCH_CACHE 同时启用；失效操作可能丢弃未写回数据，调用方须按共享内存协议使用。
  */
int arch_icache_range(void *addr, size_t size, int op);

#ifdef CONFIG_DCACHE_LINE_SIZE_DETECT

/**
  * @brief  取得数据缓存行大小。
  * @param  无。
  * @retval 缓存行大小，单位字节。
  * @note   仅对应 CONFIG_DCACHE_LINE_SIZE_DETECT 或 CONFIG_ICACHE_LINE_SIZE_DETECT 配置；供地址区间对齐计算使用。
  */
size_t arch_dcache_line_size_get(void);
#endif    /* CONFIG_DCACHE_LINE_SIZE_DETECT 条件或包含保护结束。 */

#ifdef CONFIG_ICACHE_LINE_SIZE_DETECT

/**
  * @brief  取得指令缓存行大小。
  * @param  无。
  * @retval 缓存行大小，单位字节。
  * @note   仅对应 CONFIG_DCACHE_LINE_SIZE_DETECT 或 CONFIG_ICACHE_LINE_SIZE_DETECT 配置；供地址区间对齐计算使用。
  */
size_t arch_icache_line_size_get(void);
#endif    /* CONFIG_ICACHE_LINE_SIZE_DETECT 条件或包含保护结束。 */

#endif    /* CONFIG_CACHE_MANAGEMENT && CONFIG_HAS_ARCH_CACHE 条件或包含保护结束。 */

/** @} */

#ifdef CONFIG_TIMING_FUNCTIONS
/* 仅性能计时配置需要 timing_t 类型，与系统 tick 不是同一单位。 */
#include <timing/types.h>

/**
 * @ingroup arch-timing
 * @{
 */

/**
  * @brief  初始化性能计时设施。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_TIMING_FUNCTIONS 配置，具体计数器和频率由架构实现决定。
  */
void arch_timing_init(void);

/**
  * @brief  开始采集性能计时信息。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_TIMING_FUNCTIONS 配置，具体计数器和频率由架构实现决定。
  */
void arch_timing_start(void);

/**
  * @brief  停止采集性能计时信息。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_TIMING_FUNCTIONS 配置，具体计数器和频率由架构实现决定。
  */
void arch_timing_stop(void);

/**
  * @brief  读取性能计时计数器。
  * @param  无。
  * @retval 当前计数器的原始 timing_t 值。
  * @note   先完成计时初始化；原始值未必直接等于 CPU 周期数，应经 timing_cycles_get 处理。
  */
timing_t arch_timing_counter_get(void);

/**
  * @brief  计算开始和结束计数之间的周期数。
  * @param  start：[输入]有效开始计数地址，不可为空，至少一个 timing_t。
  * @param  end：[输入]有效结束计数地址，不可为空，至少一个 timing_t。
  * @retval 经过架构缩放后的周期差。
  * @note   部分平台原始计数须缩放，回绕和测量区间限制由架构实现决定；volatile 只约束访问，不提供线程同步。
  */
uint64_t arch_timing_cycles_get(volatile timing_t *const start,
				volatile timing_t *const end);

/**
  * @brief  取得性能计时器频率。
  * @param  无。
  * @retval 频率，单位 Hz。
  * @note   仅计时配置；应使用该频率换算对应计数器，不假定等于系统 tick 频率。
  */
uint64_t arch_timing_freq_get(void);

/**
  * @brief  将性能计时周期数换算成纳秒。
  * @param  cycles：[输入]周期数量，来源应与该计时器一致。
  * @retval 换算后的纳秒数。
  * @note   整数舍入、溢出范围由架构实现规定；先完成计时初始化。
  */
uint64_t arch_timing_cycles_to_ns(uint64_t cycles);

/**
  * @brief  将累计周期数换算成每次测量的平均纳秒数。
  * @param  cycles：[输入]多次测量累计周期数。
  * @param  count：[输入]累计测量次数，必须大于 0。
  * @retval 每次测量的平均时间，单位纳秒。
  * @note   整数结果可能截断小数；计时源须与本架构接口一致。
  */
uint64_t arch_timing_cycles_to_ns_avg(uint64_t cycles, uint32_t count);

/**
  * @brief  取得性能计时器频率。
  * @param  无。
  * @retval 频率，单位 MHz。
  * @note   仅计时配置；应使用该频率换算对应计数器，不假定等于系统 tick 频率。
  */
uint32_t arch_timing_freq_get_mhz(void);

/** @} */

#endif    /* CONFIG_TIMING_FUNCTIONS 条件或包含保护结束。 */

#ifdef CONFIG_PCIE_MSI_MULTI_VECTOR

/* PCIe MSI 向量前置声明；具体结构由对应平台提供。 */
struct msi_vector;
/* MSI 向量结构的类型别名，不分配向量对象。 */
typedef struct msi_vector msi_vector_t;

/**
  * @brief  为 PCIe 端点分配 MSI 中断向量。
  * @param  priority：[输入]MSI 向量的基准中断优先级。
  * @param  vectors：[输出]可写向量数组，容量至少 n_vector 项；非零申请时不可为空。
  * @param  n_vector：[输入]数组容量及申请向量数，单位项。
  * @retval 实际分配的向量数量。
  * @note   仅 CONFIG_PCIE_MSI_MULTI_VECTOR 配置；MSI 是由设备消息触发的中断，当前 ARM SPI 路径不使用此接口。
  */
uint8_t arch_pcie_msi_vectors_allocate(unsigned int priority,
				       msi_vector_t *vectors,
				       uint8_t n_vector);

/**
  * @brief  把分配的 MSI 向量连接到中断处理函数。
  * @param  vector：[输入/输出]有效 MSI 向量对象，不可为空。
  * @param  routine：[输入]有效服务回调，不可为空。
  * @param  parameter：[输入]传给回调的上下文，可为空，使用期间保持有效。
  * @param  flags：[输入]架构中断配置标志。
  * @retval true 表示成功，false 表示失败。
  * @note   仅 PCIe 多向量配置；回调运行于中断上下文。
  */
bool arch_pcie_msi_vector_connect(msi_vector_t *vector,
				  void (*routine)(const void *parameter),
				  const void *parameter,
				  uint32_t flags);

#endif    /* CONFIG_PCIE_MSI_MULTI_VECTOR 条件或包含保护结束。 */

#ifdef __cplusplus
}
#endif    /* __cplusplus 条件或包含保护结束。 */

/* 在所有所需类型声明完成后补入架构内联实现，避免声明和定义顺序循环。 */
#include <arch/arch_inlines.h>

#endif    /* _ASMLANGUAGE 条件或包含保护结束。 */

#endif    /* ZEPHYR_INCLUDE_SYS_ARCH_INTERFACE_H_ 条件或包含保护结束。 */
