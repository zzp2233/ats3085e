/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Actions定时器寄存器字段及写寄存器后的短等待。
 */

#ifndef SOC_TIMER_H_
#define SOC_TIMER_H_    /* 防止定时器类型及内联函数重复定义。 */

#define     T0_CTL_EN                                                         5    /* TIMER0控制寄存器计数使能位的位置，使用时须移位构造掩码。 */
#define     T0_CTL_RELO                                                       2    /* 重装控制位的位置，不是已移位掩码。 */
#define     T0_CTL_ZIEN                                                       1    /* 计数事件中断使能位的位置。 */
#define     T0_CTL_ZIPD                                                       0    /* 中断挂起状态位的位置，驱动回写控制值清除。 */

#define TIMER_MAX_CYCLES_VALUE			0xfffffffful    /* 32位计数/重装范围的最大无符号值。 */


/* 定时器连续三个32位寄存器的布局；volatile不是CPU内存屏障，也不保证固定延时。 */
typedef struct {
    volatile uint32_t ctl;    /* 偏移0x00控制寄存器；volatile要求每次按C访问规则实际读写。 */
    volatile uint32_t val;    /* 偏移0x04重装/比较值寄存器。 */
    volatile uint32_t cnt;    /* 偏移0x08当前计数寄存器。 */
} timer_register_t;

/**
  * @brief  在定时器寄存器配置之间执行短暂的软件等待。
  * @param  无。
  * @retval 无。
  * @note   执行10次volatile循环；实际时间受编译器与CPU时钟影响，不能按微秒计时使用。
  */
static inline void timer_reg_wait(void)
{
	volatile int i;             /* volatile循环变量保留循环读写，避免简单优化删除等待。 */

	for (i = 0; i < 10; i++)    /* 固定执行10轮空循环，给前面的寄存器写入留出间隔。 */
		;
}


#endif    /* SOC_TIMER_H_ */
