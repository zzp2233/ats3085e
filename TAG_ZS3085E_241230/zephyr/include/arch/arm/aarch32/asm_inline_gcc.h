/* ARM AArch32供GCC编译器使用的内联汇编辅助，SPI及内核短临界区在此落到架构指令。 */

/*
 * Copyright (c) 2015, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 包含公开内联接口及公开接口调用的架构辅助。 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_ASM_INLINE_GCC_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_ASM_INLINE_GCC_H_    /* 防止内联函数重复定义。 */

/* 不应直接包含本文件，应经arch/cpu.h选择正确架构和编译器实现。 */

#ifndef _ASMLANGUAGE

#include <zephyr/types.h>
#include <arch/arm/aarch32/exc.h>
#include <irq.h>

#if defined(CONFIG_CPU_CORTEX_R)
#include <arch/arm/aarch32/cortex_a_r/cpu.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ARMv7-M/ARMv8-M Mainline用BASEPRI屏蔽普通优先级中断；NMI、Fault、SVC及
 * 支持的零延迟中断仍可能进入，不能宣称这里禁止了所有异常。
 * Baseline用PRIMASK，无法按普通中断优先级分级屏蔽；NMI等不可屏蔽异常仍存在。 */
#ifndef CONFIG_DISABLE_IRQ_STAT

/**
  * @brief  屏蔽本CPU的普通中断并返回原屏蔽状态。
  * @param  无。
  * @retval 架构恢复钥匙：Mainline为原BASEPRI，Baseline为原PRIMASK，ARMv7-R为原CPSR的I位。
  * @note   当前ATS选用ARMv7-M/ARMv8-M Mainline分支；BASEPRI_MAX不削弱已经更严格的屏蔽。memory破坏列表约束编译器重排，isb同步指令流，不能代替所有场景的DMB/DSB。
  */
static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	unsigned int key;    /* 接收加锁前的屏蔽寄存器值，供后续精确恢复。 */

#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
	/* Baseline先保存PRIMASK再执行cpsid i；memory破坏列表阻止编译器把普通内存访问跨越此点。 */
	__asm__ volatile("mrs %0, PRIMASK;"
		"cpsid i"
		: "=r" (key)
		:
		: "memory");
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	unsigned int tmp;    /* GCC输出临时寄存器，装载BASEPRI阈值。 */

	/* Mainline加锁保存BASEPRI并用BASEPRI_MAX提高屏蔽；解锁恢复钥匙，isb确保后续指令看到新的执行状态。 */
	__asm__ volatile(
		"mov %1, %2;"
		"mrs %0, BASEPRI;"
		"msr BASEPRI_MAX, %1;"
		"isb;"
		: "=r"(key), "=r"(tmp)
		: "i"(_EXC_IRQ_DEFAULT_PRIO)
		: "memory");
#elif defined(CONFIG_ARMV7_R)
	/* ARMv7-R使用CPSR的I位保存/控制IRQ屏蔽，memory及cc声明编译器可见的副作用。 */
	__asm__ volatile(
		"mrs %0, cpsr;"
		"and %0, #" TOSTR(I_BIT) ";"
		"cpsid i;"
		: "=r" (key)
		:
		: "memory", "cc");
#else
#error Unknown ARM architecture
#endif                       /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */

	return key;          /* 把原始中断状态交给调用者配对保存。 */
}


/* 解锁必须恢复匹配加锁前的状态：不能无条件开中断，否则会破坏外层临界区。 */

/**
  * @brief  恢复匹配arch_irq_lock之前的本CPU中断状态。
  * @param  key：[输入] 对应加锁调用返回的原始钥匙，须按嵌套逆序使用。
  * @retval 无。
  * @note   Mainline直接恢复BASEPRI；Baseline/ARMv7-R原来已关中断则保持关闭。仅在未启用CONFIG_DISABLE_IRQ_STAT时提供此处实现。
  */
static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
	if (key != 0U) {    /* 原先已禁止中断时直接保持屏蔽，保护外层临界区。 */
		return;     /* 无需启用中断，保留进入时的禁止状态。 */
	}
	/* Baseline恢复普通中断使能，并用isb同步指令执行；此处仅在原状态允许中断时到达。 */
	__asm__ volatile(
		"cpsie i;"
		"isb"
		: : : "memory");
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	/* Mainline加锁保存BASEPRI并用BASEPRI_MAX提高屏蔽；解锁恢复钥匙，isb确保后续指令看到新的执行状态。 */
	__asm__ volatile(
		"msr BASEPRI, %0;"
		"isb;"
		:  : "r"(key) : "memory");
#elif defined(CONFIG_ARMV7_R)
	if (key != 0U) {    /* 原先已禁止中断时直接保持屏蔽，保护外层临界区。 */
		return;     /* 无需启用中断，保留进入时的禁止状态。 */
	}
	/* ARMv7-R使用CPSR的I位保存/控制IRQ屏蔽，memory及cc声明编译器可见的副作用。 */
	__asm__ volatile(
		"cpsie i;"
		: : : "memory", "cc");
#else
#error Unknown ARM architecture
#endif                      /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */
}

#endif

/**
  * @brief  判断保存的中断钥匙是否表示原先未屏蔽普通中断。
  * @param  key：[输入] arch_irq_lock返回的状态值。
  * @retval true：key为0；false：原来存在屏蔽。
  * @note   检查的是保存值，不重新读取CPU当前屏蔽寄存器，也不代表NMI/Fault状态。
  */
static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	/* PRIMASK/BASEPRI均以0表示没有由该屏蔽寄存器禁止普通中断。 */
	return key == 0U;    /* 只判断保存状态为零与否，不读取当前CPU状态。 */
}

#ifdef __cplusplus
}
#endif

#endif    /* _ASMLANGUAGE */

#endif    /* ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_ASM_INLINE_GCC_H_ */
