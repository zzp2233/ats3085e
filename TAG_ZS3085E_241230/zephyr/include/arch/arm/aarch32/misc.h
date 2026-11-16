/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32公开的计时和单指令辅助接口。
 *
 * 由ARM架构头引入；cycle接口最终转交当前板的系统定时器驱动。
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_MISC_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_MISC_H_    /* 头文件保护，避免重复声明。 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASMLANGUAGE
/* 仅C/C++可见；汇编包含此头时不展开类型与内联函数。 */

/**
  * @brief  读取系统定时器驱动的32位周期计数。
  * @param  无。
  * @retval 硬件周期快照，计数源与回绕特性由实际驱动决定。
  * @note   当前ATS板实现在acts_timer.c，使用TIMER2加睡眠补偿。
  */
extern uint32_t sys_clock_cycle_get_32(void);

/**
  * @brief  把架构层周期查询转交系统定时器实现。
  * @param  无。
  * @retval 驱动返回的32位硬件周期数。
  * @note   本接口不换算时间单位，不一定等于CPU主频计数。
  */
static inline uint32_t arch_k_cycle_get_32(void)
{


	return sys_clock_cycle_get_32();    /* 保留驱动计数单位和32位回绕行为。 */
}

/**
  * @brief  执行一条ARM空操作指令。
  * @param  无。
  * @retval 无。
  * @note   volatile内联汇编保留指令；没有memory破坏列表，不能把它当作内存屏障或精确延时。
  */
static ALWAYS_INLINE void arch_nop(void)
{
	__asm__ volatile("nop");    /* 发出一条nop，具体耗时取决于处理器流水线和时钟。 */
}

#if defined(CONFIG_USERSPACE)

/**
  * @brief  查询当前线程是否处于ARM用户执行模式。
  * @param  无。
  * @retval true：用户模式；false：特权模式。
  * @note   仅CONFIG_USERSPACE声明，实际判定由架构代码提供。
  */
extern bool z_arm_thread_is_in_user_mode(void);
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_MISC_H_ */
