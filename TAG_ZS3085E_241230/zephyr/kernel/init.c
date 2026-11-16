/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 内核启动、全局调度对象以及主线程和空闲线程的建立。
 * SPI 设备经 PRE_KERNEL/POST_KERNEL 等初始化级别注册；应用线程运行前必须先完成调度器和设备状态准备。
 */

/* 包含内核类型、架构接口、链接布局和配置条件下的启动辅助接口；offsets_short.h 属于构建生成输入，不在此修改。 */
#include <zephyr.h>
#include <offsets_short.h>
#include <kernel.h>
#include <sys/printk.h>
#include <debug/stack.h>
#include <random/rand32.h>
#include <linker/sections.h>
#include <toolchain.h>
#include <kernel_structs.h>
#include <device.h>
#include <init.h>
#include <linker/linker-defs.h>
#include <ksched.h>
#include <string.h>
#include <sys/dlist.h>
#include <kernel_internal.h>
#include <drivers/entropy.h>
#include <logging/log_ctrl.h>
#include <tracing/tracing.h>
#include <stdbool.h>
#include <debug/gcov.h>
#include <kswap.h>
#include <timing/timing.h>
#include <logging/log.h>
/* 注册内核启动日志模块，日志级别取构建配置。 */
LOG_MODULE_REGISTER(os, CONFIG_KERNEL_LOG_LEVEL);

/* 全系统唯一的 z_kernel 实体；kernel_structs.h 中的 extern 仅是声明。 */
struct z_kernel _kernel;

/* 主线程栈和控制块：#if 0 保留通用栈宏；当前分支将栈放入指定 noinit 段并按架构要求对齐。 */
#if 0
K_THREAD_STACK_DEFINE(z_main_stack, CONFIG_MAIN_STACK_SIZE);
#else
__in_section_unique(main.noinit.stack) struct z_thread_stack_element __aligned(Z_KERNEL_STACK_OBJ_ALIGN) \
			z_main_stack[CONFIG_MAIN_STACK_SIZE];
#endif
/* 主线程控制块的唯一实体；栈空间由上方 z_main_stack 单独提供。 */
struct k_thread z_main_thread;

#ifdef CONFIG_MULTITHREADING
__pinned_bss
/* 每个 CPU 一个空闲线程控制块；__pinned_bss 指定其存储段。 */
struct k_thread z_idle_threads[CONFIG_MP_NUM_CPUS];

#if 0
static K_KERNEL_PINNED_STACK_ARRAY_DEFINE(z_idle_stacks,
					  CONFIG_MP_NUM_CPUS,
					  CONFIG_IDLE_STACK_SIZE);
#else
__in_section_unique(ram.noinit.stack) struct z_thread_stack_element __aligned(Z_KERNEL_STACK_OBJ_ALIGN) \
		  /* 每个 CPU 的空闲线程静态栈，当前分支保留原段属性和对齐要求。 */
		  z_idle_stacks[CONFIG_MP_NUM_CPUS][CONFIG_IDLE_STACK_SIZE];

#endif

#endif    /* CONFIG_MULTITHREADING 分支结束。 */

/* 中断栈按 CPU 分配，启动期间也作为系统栈。首次切换至主线程以前中断保持关闭，因此这两个用途不会同时占用该区域。#if 0 保留通用栈定义，当前使用指定段布局。 */

#if 0
K_KERNEL_STACK_ARRAY_DEFINE(z_interrupt_stacks, CONFIG_MP_NUM_CPUS,
			    CONFIG_ISR_STACK_SIZE);
#else
__in_section_unique(interrupt.noinit.stack) struct z_thread_stack_element __aligned(Z_KERNEL_STACK_OBJ_ALIGN) \
			z_interrupt_stacks[CONFIG_MP_NUM_CPUS][CONFIG_ISR_STACK_SIZE];
#endif

/* 存在系统时钟时初始化超时双向链表；无时钟配置用空宏保留调用兼容性。 */
#ifdef CONFIG_SYS_CLOCK_EXISTS
	#define initialize_timeouts() do { \
		sys_dlist_init(&_timeout_q); \
	} while (false)
#else
	#define initialize_timeouts() do { } while ((0))
#endif

/**
  * @brief  空闲线程入口，在没有可运行应用线程时处理 CPU 空闲。
  * @param  unused1：[输入]由线程创建流程传入的 CPU 上下文指针。
  * @param  unused2：[输入]保留参数，可为空。
  * @param  unused3：[输入]保留参数，可为空。
  * @retval 通常不返回。
  * @note   实现在其他源文件；此处仅声明，运行于各 CPU 的空闲线程。
  */
extern void idle(void *unused1, void *unused2, void *unused3);


/* LCOV_EXCL_START
 * 此处在启动极早期执行，覆盖率机制尚未可靠工作；部分架构还会改用汇编实现，因此保留覆盖率排除标记。
 */

/**
  * @brief  清零当前内存布局中的 BSS，让未显式初始化的静态对象取得零值。
  * @param  无。
  * @retval 无。
  * @note   在设备和调度器初始化之前调用；区间及大小由链接符号提供。PSRAM、传感器、CCM、DTCM 和覆盖率区域按各自配置处理。
  */
__boot_func
void z_bss_zero(void)
{

#ifndef CONFIG_SOC_NO_PSRAM
	if ((uint32_t)&__psram_bss_end - (uint32_t)&__psram_bss_start > 0) {                                       /* PSRAM BSS 区间非空才写入，避免空段处理。 */
		(void)memset(&__psram_bss_start, 0, (uint32_t)&__psram_bss_end - (uint32_t)&__psram_bss_start);    /* 按链接地址差清零 PSRAM 的未初始化静态数据。 */
	}
	if ((uint32_t)&__sram_bss_end - (uint32_t)&__sram_bss_start > 0) {                                         /* 独立检查内部 SRAM BSS 是否有内容。 */
		(void)memset(&__sram_bss_start, 0, (uint32_t)&__sram_bss_end - (uint32_t)&__sram_bss_start);       /* 按 SRAM BSS 边界清零。 */
	}
#else
	if ((uint32_t)&__bss_end - (uint32_t)&__bss_start > 0) {                                                   /* 无 PSRAM 布局只需处理统一 BSS 区间。 */
		(void)memset(&__bss_start, 0, (uint32_t)&__bss_end - (uint32_t)&__bss_start);                      /* 清零统一 BSS，包含 _kernel 等静态对象。 */
	}
#endif
	
#ifdef CONFIG_SENSOR_ALGO
	if (__sensor_bss_size > 0) {                                                                               /* 传感器算法配置存在时检查其独立 BSS 大小。 */
		(void)memset(__sensor_bss_start, 0, (uint32_t)__sensor_bss_size);                                  /* 清零传感器算法区，大小单位为字节。 */
	}
#endif

#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_ccm), okay)
	/* 设备树启用 CCM 紧耦合内存时，按链接边界清零该区 BSS。 */
	(void)memset(&__ccm_bss_start, 0,
		     ((uint32_t) &__ccm_bss_end - (uint32_t) &__ccm_bss_start));
#endif
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_dtcm), okay)
	/* 设备树启用 DTCM 数据紧耦合内存时，清零其独立 BSS。 */
	(void)memset(&__dtcm_bss_start, 0,
		     ((uint32_t) &__dtcm_bss_end - (uint32_t) &__dtcm_bss_start));
#endif
#ifdef CONFIG_CODE_DATA_RELOCATION
	/* 代码/数据重定位配置额外声明需清零的重定位 BSS 入口。 */
	extern void bss_zeroing_relocation(void);

	/* 清零重定位区域的未初始化静态数据。 */
	bss_zeroing_relocation();
#endif                                                                                                             /* CONFIG_CODE_DATA_RELOCATION 分支结束。 */
#ifdef CONFIG_COVERAGE_GCOV
	/* 覆盖率配置下清零计数器 BSS，区间由链接脚本决定。 */
	(void)memset(&__gcov_bss_start, 0,
		 ((uintptr_t) &__gcov_bss_end - (uintptr_t) &__gcov_bss_start));
#endif
}

#ifdef CONFIG_LINKER_USE_BOOT_SECTION

/**
  * @brief  清零分页建立前需要使用的 boot 区域 BSS。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_LINKER_USE_BOOT_SECTION 启用时存在，与普通 BSS 清零分开以适应早期映射次序。
  */
__boot_func
void z_bss_zero_boot(void)
{
	/* 按 boot 段链接边界清零，启动时不依赖普通分页区。 */
	(void)memset(&lnkr_boot_bss_start, 0,
		     (uintptr_t)&lnkr_boot_bss_end
		     - (uintptr_t)&lnkr_boot_bss_start);
}
#endif    /* CONFIG_LINKER_USE_BOOT_SECTION 分支结束。 */

#ifdef CONFIG_LINKER_USE_PINNED_SECTION

/**
  * @brief  清零必须常驻内存的 pinned 区域 BSS。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_LINKER_USE_PINNED_SECTION 启用时存在；分页可用以前所需的常驻对象在此初始化。
  */
#ifdef CONFIG_LINKER_USE_BOOT_SECTION
__boot_func
#else
__pinned_func
#endif


void z_bss_zero_pinned(void)
{
	/* 按 pinned 段链接边界清零常驻静态对象。 */
	(void)memset(&lnkr_pinned_bss_start, 0,
		(uintptr_t)&lnkr_pinned_bss_end
		- (uintptr_t)&lnkr_pinned_bss_start);
}
#endif    /* CONFIG_LINKER_USE_PINNED_SECTION 分支结束。 */

#ifdef CONFIG_STACK_CANARIES
/* 编译器栈保护使用的全局哨兵值声明；实体由运行库提供，不是线程栈边界哨兵。 */
extern volatile uintptr_t __stack_chk_guard;
#endif    /* CONFIG_STACK_CANARIES 分支结束。 */

/* LCOV_EXCL_STOP */

__pinned_bss
/* 后期内核可用状态标记；启动时为 false，进入主线程后置 true。 */
bool z_sys_post_kernel;

/**
  * @brief  按构建配置输出启动标识。
  * @param  无。
  * @retval 无。
  * @note   在 POST_KERNEL 初始化完成后由主线程调用；此处仅声明。
  */
extern void boot_banner(void);

/**
  * @brief  在主线程上下文完成后期系统初始化，然后调用应用 main。
  * @param  unused1：[输入]保留的线程入口参数，未使用，可为空。
  * @param  unused2：[输入]保留的线程入口参数，未使用，可为空。
  * @param  unused3：[输入]保留的线程入口参数，未使用，可为空。
  * @retval 无；main 返回后此入口可返回并结束主线程。
  * @note   在 PRE_KERNEL 阶段和调度器准备完成后运行；POST_KERNEL、APPLICATION 和本项目 POWER_MAX 级别在调用应用之前执行。
  */
__boot_func
static void bg_thread_main(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);                                        /* 明确入口第一个保留参数不参与处理。 */
	ARG_UNUSED(unused2);                                        /* 明确入口第二个保留参数不参与处理。 */
	ARG_UNUSED(unused3);                                        /* 明确入口第三个保留参数不参与处理。 */

#ifdef CONFIG_MMU
	/* MMU 启用时，在调度环境建立后初始化后备存储和换出策略，允许 POST_KERNEL 及后续阶段创建内核对象并使用内存管理。z_phys_map() 不受此阶段限制。 */
	z_mem_manage_init();                                        /* 建立 MMU 内存管理的后备存储等运行机制。 */
#endif                                                              /* CONFIG_MMU 分支结束。 */
	z_sys_post_kernel = true;                                   /* 标记后续初始化已运行于可用的线程环境。 */

	z_sys_init_run_level(_SYS_INIT_LEVEL_POST_KERNEL);          /* 运行需要内核服务的设备初始化；SPI 驱动通过注册项参与相应级别。 */
#if CONFIG_STACK_POINTER_RANDOM
	z_stack_adjust_initialized = 1;                             /* 允许后续线程创建使用已初始化的栈随机偏移机制。 */
#endif
	boot_banner();                                              /* 输出配置允许的启动标识。 */

#if defined(CONFIG_CPLUSPLUS) && !defined(CONFIG_ARCH_POSIX)
	void z_cpp_init_static(void);                               /* 声明 C++ 静态对象构造入口，仅此配置需要。 */
	z_cpp_init_static();                                        /* 在进入应用前执行 C++ 静态构造。 */
#endif

	/* 应用 main 之前的最后一个常规设备初始化级别。 */
	z_sys_init_run_level(_SYS_INIT_LEVEL_APPLICATION);          /* 运行应用级注册初始化项。 */

	z_init_static_threads();                                    /* 初始化构建时声明的静态线程对象。 */

#ifdef CONFIG_KERNEL_COHERENCE
	__ASSERT_NO_MSG(arch_mem_coherent(&_kernel));               /* 一致性配置下验证全局内核对象符合架构内存要求。 */
#endif

#ifdef CONFIG_SMP
	z_smp_init();                                               /* 多核配置下启动其他 CPU。 */
	z_sys_init_run_level(_SYS_INIT_LEVEL_SMP);                  /* 其他 CPU 就绪后执行 SMP 初始化级别。 */
#endif
	if(!soc_pmu_lowpower_check()){                              /* PMU 未判定为低功耗启动时才执行项目的最大功耗级别。 */
		z_sys_init_run_level(_SYS_INIT_LEVEL_POWER_MAX);    /* 执行本项目补充的 POWER_MAX 初始化项。 */
	}

#ifdef CONFIG_MMU
	z_mem_manage_boot_finish();                                 /* MMU 配置下结束启动专用内存管理阶段。 */
#endif                                                              /* CONFIG_MMU 分支结束。 */

	extern int main(void);                                      /* 声明应用入口，具体实现在应用源文件。 */

	main();                                                     /* 进入应用；返回值在此不使用。 */

	/* main 返回后去掉关键线程标志；之后结束此线程不再按关键线程退出处理。 */
	z_main_thread.base.user_options &= ~K_ESSENTIAL;            /* 允许 main 返回后主线程正常终止。 */

#ifdef CONFIG_COVERAGE_DUMP
	/* 应用退出后输出覆盖率数据。 */
	gcov_coverage_dump();                                       /* 按配置输出最终覆盖率统计。 */
#endif
}                                                                   /* LCOV_EXCL_LINE：最终覆盖率数据已输出。 */




#if defined(CONFIG_MULTITHREADING)

/**
  * @brief  为指定 CPU 建立不参与普通应用工作的空闲线程。
  * @param  i：[输入]CPU 索引，范围 0 至 CONFIG_MP_NUM_CPUS-1。
  * @retval 无。
  * @note   多线程配置使用；栈和控制块为静态数组，入口为 idle，采用 K_IDLE_PRIO 与 K_ESSENTIAL。
  */
__boot_func
static void init_idle_thread(int i)
{
	struct k_thread *thread = &z_idle_threads[i];    /* 取得该 CPU 的静态空闲线程控制块。 */
	k_thread_stack_t *stack = z_idle_stacks[i];      /* 取得对应静态栈，不进行动态分配。 */

#ifdef CONFIG_THREAD_NAME
	char tname[8];                                   /* 临时保存格式化后的空闲线程名。 */

	snprintk(tname, 8, "idle %02d", i);              /* 生成包含 CPU 索引的名称。 */
#else
	char *tname = NULL;                              /* 未启用线程名时不提供名称。 */
#endif                                                   /* CONFIG_THREAD_NAME 分支结束。 */

	/* 建立空闲线程初始上下文，首个参数指向对应 CPU，另外两个参数为空。 */
	z_setup_new_thread(thread, stack,
			  CONFIG_IDLE_STACK_SIZE, idle, &_kernel.cpus[i],
			  NULL, NULL, K_IDLE_PRIO, K_ESSENTIAL,
			  tname);                        /* 以空闲优先级、关键线程属性完成控制块和初始栈设置。 */
	z_mark_thread_as_started(thread);                /* 将空闲线程标记为已经启动。 */

#ifdef CONFIG_SMP
	thread->base.is_idle = 1U;                       /* SMP 调度器据此识别特殊空闲线程。 */
#endif
}

/**
  * @brief  复用空闲线程初始化流程，重建指定 CPU 的空闲线程。
  * @param  i：[输入]有效 CPU 索引，范围 0 至 CONFIG_MP_NUM_CPUS-1。
  * @retval 无。
  * @note   调用方须确保原线程不再运行，且其控制块和静态栈可安全重新初始化；函数自身不加锁。
  */
void z_reinit_idle_thread(int i)
{
	init_idle_thread(i);    /* 为当前索引建立或重新建立空闲线程。 */
}

/**
  * @brief  初始化调度队列、主线程和各 CPU 的空闲线程及中断栈。
  * @param  无。
  * @retval 主线程的初始栈指针，供架构首次切换使用。
  * @note   进入时 _kernel 已由 BSS 清零；此函数在普通多线程调度开始前运行。
  */
__boot_func
static char *prepare_multithreading(void)
{
	char *stack_ptr;                                                   /* 保存主线程栈初始化后的起始上下文指针。 */

	/* _kernel.ready_q 在清 BSS 后全零；在此建立所选队列实现的有效初态。 */
	z_sched_init();                                                    /* 初始化当前配置选择的就绪队列。 */

#ifndef CONFIG_SMP
	/* 单核调度缓存不能为 NULL；先指向最早运行的主线程，避免以尚未初始化的其他线程优先级参与选择。 */
	_kernel.ready_q.cache = &z_main_thread;                            /* 单核初始候选线程固定为主线程，避免空缓存。 */
#endif
	/* 建立主线程栈和入口，主线程首先执行 bg_thread_main 而非直接进入应用 main。 */
	stack_ptr = z_setup_new_thread(&z_main_thread, z_main_stack,
				       CONFIG_MAIN_STACK_SIZE, bg_thread_main,
				       NULL, NULL, NULL,
				       CONFIG_MAIN_THREAD_PRIORITY,
				       K_ESSENTIAL, "main");               /* 创建主线程初始上下文；执行入口是后期初始化函数。 */
	z_mark_thread_as_started(&z_main_thread);                          /* 标记主线程已启动。 */
	z_ready_thread(&z_main_thread);                                    /* 将主线程加入就绪队列，首次切换即可选中。 */

	for (int i = 0; i < CONFIG_MP_NUM_CPUS; i++) {                     /* 逐 CPU 配置空闲线程和中断栈，循环次数由配置决定。 */
		init_idle_thread(i);                                       /* 为当前索引建立或重新建立空闲线程。 */
		_kernel.cpus[i].idle_thread = &z_idle_threads[i];          /* 建立 CPU 到其空闲线程的关联。 */
		_kernel.cpus[i].id = i;                                    /* 保存 CPU 数组索引。 */
		/* 以该 CPU 中断栈可用区域的末端作为初始栈顶。 */
		_kernel.cpus[i].irq_stack =
			(Z_KERNEL_STACK_BUFFER(z_interrupt_stacks[i]) +
			 K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[i]));    /* 中断栈向下增长，初始指针取可用区域末端。 */
	}

	return stack_ptr;                                                  /* 交付首次切换所需主线程栈指针。 */
}

/**
  * @brief  离开启动上下文，首次进入已准备就绪的主线程。
  * @param  stack_ptr：[输入]主线程初始栈指针；专用架构入口使用，通用切换分支忽略，不在此解引用。
  * @retval 不返回。
  * @note   调用前必须完成 prepare_multithreading；首次切换由架构专用入口或 z_swap_unlocked 完成。
  */
__boot_func
static FUNC_NORETURN void switch_to_main_thread(char *stack_ptr)
{
#ifdef CONFIG_ARCH_HAS_CUSTOM_SWAP_TO_MAIN
	arch_switch_to_main_thread(&z_main_thread, stack_ptr, bg_thread_main);    /* 由支持专用首次切换的架构进入主线程。 */
#else
	ARG_UNUSED(stack_ptr);                                                    /* 通用调度切换直接使用线程已保存的栈上下文。 */
	/* 首次切换离开启动用的虚拟线程。它不在就绪队列或等待队列中，以后不会再被调度；真正的主线程入口是 bg_thread_main。 */
	z_swap_unlocked();                                                        /* 切出启动虚拟线程，开始调度主线程。 */
#endif
	CODE_UNREACHABLE;                                                         /* LCOV_EXCL_LINE */
}
#endif    /* CONFIG_MULTITHREADING 分支结束。 */

#if defined(CONFIG_ENTROPY_HAS_DRIVER) || defined(CONFIG_TEST_RANDOM_GENERATOR)

/**
  * @brief  在调度设施尚不可依赖的启动早期填充随机字节。
  * @param  buf：[输出]至少 length 字节的可写区域；length 大于 0 时不可为空。
  * @param  length：[输入]需要填充的字节数，0 表示不写数据。
  * @retval 无；硬件接口失败时使用 sys_rand32_get 回退。
  * @note   仅熵驱动或测试随机源配置启用时编译；不得让回退随机源依赖尚未可用的锁或阻塞机制，不保证密码学质量。
  */
__boot_func
void z_early_boot_rand_get(uint8_t *buf, size_t length)
{
	int n = sizeof(uint32_t);                                                             /* 每次从一个 32 位随机值最多复制 4 字节。 */
#ifdef CONFIG_ENTROPY_HAS_DRIVER
	const struct device *entropy = device_get_binding(DT_CHOSEN_ZEPHYR_ENTROPY_LABEL);    /* 查找设备树指定的硬件熵设备。 */
	int rc;                                                                               /* 保存驱动取熵结果。 */

	if (entropy == NULL) {                                                                /* 找不到设备则使用软件回退。 */
		goto sys_rand_fallback;                                                       /* 跳过硬件访问，进入备用随机字节填充。 */
	}

	/* 优先尝试驱动提供的中断上下文取熵接口，并允许忙等。 */
	rc = entropy_get_entropy_isr(entropy, buf, length, ENTROPY_BUSYWAIT);                 /* 先尝试允许忙等的中断取熵接口。 */
	if (rc == -ENOTSUP) {                                                                 /* 只在明确报告接口不支持时尝试普通接口。 */
		/* 驱动未实现中断专用接口时，原流程假定普通接口可用于此上下文；这是驱动适配前提，并非所有实现都天然满足。 */
		rc = entropy_get_entropy(entropy, buf, length);                               /* 按原适配假设使用普通取熵接口。 */
	}

	if (rc >= 0) {                                                                        /* 硬件取熵成功则输出区已填充。 */
		return;                                                                       /* 完成填充，跳过软件随机数回退。 */
	}

	/* 熵设备不可用或取熵失败时，转入下面的软件随机数回退路径。 */

sys_rand_fallback:                                                                            /* 硬件熵不可用时的公共回退入口。 */
#endif

	/* FIXME：此极早期路径假定 sys_rand32_get() 不使用信号量、互斥量等同步机制，此时还不能依赖它们。理想情况下应使用硬件熵驱动；此回退仅供缺少硬件随机源的目标使用。 */

	while (length > 0U) {                                                                 /* 持续填充剩余字节，剩余长度归零后结束。 */
		uint32_t rndbits;                                                             /* 暂存本轮 32 位随机结果。 */
		uint8_t *p_rndbits = (uint8_t *)&rndbits;                                     /* 按本机内存字节顺序取出随机值。 */

		rndbits = sys_rand32_get();                                                   /* 调用不能依赖早期不可用同步设施的回退源。 */

		if (length < sizeof(uint32_t)) {                                              /* 最后不足 4 字节时限制本轮复制数量。 */
			n = length;                                                           /* 只复制调用者剩余容量。 */
		}

		for (int i = 0; i < n; i++) {                                                 /* 逐字节复制当前随机值的有效部分。 */
			*buf = *p_rndbits;    /* 写入一个随机字节。 */
			buf++;                                                                /* 推进输出地址。 */
			p_rndbits++;                                                          /* 推进随机值内的读取地址。 */
		}

		length -= n;                                                                  /* 扣除已填充字节，保证循环有界。 */
	}
}
/* CONFIG_ENTROPY_HAS_DRIVER 或 CONFIG_TEST_RANDOM_GENERATOR 分支结束。 */
#endif

/**
  * @brief  从已可执行 C 代码的架构入口建立内核，并进入应用主线程。
  * @param  无。
  * @retval 不返回。
  * @note   处理器已进入适当的 32 位运行状态且 BSS 已清零；先运行 PRE_KERNEL 初始化，再建立多线程，后期初始化由 bg_thread_main 完成。
  */
__boot_func
FUNC_NORETURN void z_cstart(void)
{
	/* 建立覆盖率静态记录；未启用覆盖率的配置由头文件提供空操作。 */
	gcov_static_init();                                    /* 建立已编译的覆盖率静态对象信息。 */

	LOG_CORE_INIT();                                       /* 初始化日志核心设施。 */

	/* 建立架构相关的中断、CPU 等内核初态。 */
	arch_kernel_init();                                    /* 执行当前架构的内核早期设置。 */

#if defined(CONFIG_MULTITHREADING)
	/* 即使架构支持专用的首次切换，prepare_multithreading 中的 z_ready_thread 也需要当前虚拟线程作为启动上下文。 */
	struct k_thread dummy_thread;                          /* 在当前启动栈上保存首次调度前的虚拟线程状态。 */

	z_dummy_thread_init(&dummy_thread);                    /* 让调度辅助函数拥有有效的当前线程上下文。 */
#endif
	/* 初始化静态设备对象的运行状态，随后才按级别执行注册的初始化函数。 */
	z_device_state_init();                                 /* 准备所有静态设备的状态对象。 */

	/* 先完成两个早期硬件初始化级别；此时还不能假定已具备可阻塞的线程环境。 */
	z_sys_init_run_level(_SYS_INIT_LEVEL_PRE_KERNEL_1);    /* 运行第一阶段早期硬件初始化。 */
	z_sys_init_run_level(_SYS_INIT_LEVEL_PRE_KERNEL_2);    /* 继续运行第二阶段早期硬件初始化。 */

#ifdef CONFIG_STACK_CANARIES
	uintptr_t stack_guard;                                 /* 暂存编译器栈保护哨兵初始值。 */

	//z_early_boot_rand_get((uint8_t *)&stack_guard, sizeof(stack_guard));
	stack_guard = (uintptr_t)0xdeadbeef;                   /* 当前项目使用固定值；上方随机初始化调用被注释，不能称为随机哨兵。 */
	__stack_chk_guard = stack_guard;                       /* 发布全局栈保护比较值。 */
	__stack_chk_guard <<= 8;                               /* 最低字节置零以保留原栈保护值布局。 */
#endif                                                         /* CONFIG_STACK_CANARIES 分支结束。 */

#ifdef CONFIG_TIMING_FUNCTIONS_NEED_AT_BOOT
	timing_init();                                         /* 需要启动期计时的配置初始化计时设施。 */
	timing_start();                                        /* 开始启动期性能计时。 */
#endif

#ifdef CONFIG_MULTITHREADING
	switch_to_main_thread(prepare_multithreading());       /* 先建调度环境，再首次进入主线程，调用不返回。 */
#else
#ifdef ARCH_SWITCH_TO_MAIN_NO_MULTITHREADING
	/* 无多线程时，架构可提供专用入口切换到应用主流程。 */
	/* 无多线程配置下，由架构专用宏转入后期初始化入口。 */
	ARCH_SWITCH_TO_MAIN_NO_MULTITHREADING(bg_thread_main,
		NULL, NULL, NULL);                             /* 无多线程专用架构入口传入三个空参数。 */
#else
	bg_thread_main(NULL, NULL, NULL);                      /* 无专用切换时直接运行后期初始化和应用。 */

	/* LCOV_EXCL_START
	 * 此时最终覆盖率数据已经输出，下面的停机循环不再统计。
	 */
	irq_lock();                                            /* 无多线程且应用返回后屏蔽普通中断进入停机。 */
	while (true) {                                         /* 永久停留，避免无可调度线程时继续执行。 */
	}
	/* LCOV_EXCL_STOP */
#endif
#endif                                                         /* CONFIG_MULTITHREADING 分支结束。 */

	/* 显式告知编译器控制流不会到达此处，避免因无法推断上方入口永不返回而产生诊断。 */

	CODE_UNREACHABLE;                                      /* LCOV_EXCL_LINE */
}
