/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM 32 位线程上下文：k_thread 内嵌的软件保存寄存器和架构状态。
 * Cortex-M 的异常入栈另外保存 r0-r3、r12、LR、PC、xPSR；下列结构保存
 * 不能仅依赖异常栈帧恢复的状态。实际字段偏移由构建工具生成，切换汇编据此访问。
 */

/* 包含保护避免重复定义寄存器保存区；保留全部非当前构建分支。 */
#ifndef ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_THREAD_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_THREAD_H_

#ifndef _ASMLANGUAGE         /* C 类型不向汇编源文件展开。 */
#include <zephyr/types.h>    /* 固定宽度整数类型用于匹配寄存器和汇编偏移。 */

/* 按 ARM 调用约定由被调用者保持的 r4-r11，以及线程栈指针。 */
struct _callee_saved {
	uint32_t v1;     /* r4：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t v2;     /* r5：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t v3;     /* r6：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t v4;     /* r7：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t v5;     /* r8：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t v6;     /* r9：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t v7;     /* r10：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t v8;     /* r11：线程切换时由软件保存和恢复的通用寄存器。 */
	uint32_t psp;    /* r13：线程进程栈指针；与异常处理使用的主栈指针区分。 */
};

typedef struct _callee_saved _callee_saved_t;    /* k_thread 的通用寄存器保存区类型别名。 */

#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
/* 仅同时启用 FPU 和共享上下文时存在；s0-s15 等由浮点异常栈帧保存。 */
struct _preempt_float {
	float  s16;    /* s16：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s17;    /* s17：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s18;    /* s18：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s19;    /* s19：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s20;    /* s20：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s21;    /* s21：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s22;    /* s22：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s23;    /* s23：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s24;    /* s24：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s25;    /* s25：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s26;    /* s26：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s27;    /* s27：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s28;    /* s28：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s29;    /* s29：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s30;    /* s30：切出线程时软件保存的浮点寄存器，切入时恢复。 */
	float  s31;    /* s31：切出线程时软件保存的浮点寄存器，切入时恢复。 */
};
#endif

struct _thread_arch {

	/* 主动切换时保存的中断屏蔽键；当前 Mainline 内核使用 BASEPRI。 */
	uint32_t basepri;    /* arch_swap 保存的锁键，切回线程时恢复其原屏蔽状态。 */

	/* 用专用字段传递阻塞操作的返回状态，避免依赖修改异常栈帧中的 r0。 */
	uint32_t swap_return_value;    /* 阻塞等待的默认/唤醒结果，arch_swap 在恢复执行后返回此值。 */

#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
	/* Cortex-M 的异常入栈机制保存低半浮点现场；这里补存高半现场 s16-s31。
	 * 不再额外定义协作切换浮点保存区。
	 */
	struct _preempt_float  preempt_float;    /* FPU 共享分支的高半浮点寄存器保存区。 */
#endif

#if defined(CONFIG_ARM_STORE_EXC_RETURN) || defined(CONFIG_USERSPACE)
	/* mode 是架构状态组合字：
	 * 位 0 对应 CONTROL.nPRIV，表示线程当前的特权/用户模式。
	 * 位 2 的旧浮点活动标志已弃用，使用 EXC_RETURN.FType；FType 与 FPCA 语义相反。
	 * 位 3 标记是否采用较大的浮点 MPU 栈保护区；其余低字节位保留。
	 * 位 8-15 保存 EXC_RETURN 的最低字节：PendSV 入口从 LR 取出，出口恢复给 LR。
	 * 该字节按架构含 ES、SPSel、Mode、FType、DCRS、S 等字段，具体有效位由 ARM 架构决定。
	 */
	union {
		uint32_t mode;    /* 整个架构状态字；与下面的分字节视图共享存储。 */

#if defined(CONFIG_ARM_STORE_EXC_RETURN)
		struct {
			uint8_t mode_bits;          /* 低字节：特权模式、浮点栈保护等状态位。 */
			uint8_t mode_exc_return;    /* EXC_RETURN 低字节，用于异常返回时恢复栈和模式。 */
			uint16_t mode_reserved2;    /* 状态字高半字保留区。 */
		};
#endif
	};

#if defined(CONFIG_USERSPACE)
	uint32_t priv_stack_start;    /* USERSPACE 分支的特权栈起始地址。 */
#if defined(CONFIG_CPU_CORTEX_R)
	uint32_t priv_stack_end;      /* Cortex-R 用户线程的特权栈末端地址。 */
	uint32_t sp_usr;              /* Cortex-R 用户模式栈指针保存值。 */
#endif
#endif
#endif
};

#if defined(CONFIG_FPU_SHARING) && defined(CONFIG_MPU_STACK_GUARD)
/* FPU 共享和 MPU 栈保护同时启用时，用位 3 标记浮点保护区大小。 */
#define Z_ARM_MODE_MPU_GUARD_FLOAT_Msk (1 << 3)
#endif
typedef struct _thread_arch _thread_arch_t;    /* k_thread 内嵌的 ARM 架构扩展类型别名。 */

#endif    /* _ASMLANGUAGE */

#endif    /* ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_THREAD_H_ */
