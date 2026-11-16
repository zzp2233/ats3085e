/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Actions SoC 引脚复用寄存器访问实现。
 */

#include <errno.h>
#include <kernel.h>
#include "soc.h"

/**
  * @brief  以中断临界区保护单个 GPIO 控制寄存器的复用配置更新。
  * @param  pin：[输入]GPIO 编号，必须小于 GPIO_MAX_PIN_NUM。
  * @param  mode：[输入]复用模式及所需电气属性位；调用者提供合法字段组合。
  * @retval 0：已写入；-EINVAL：引脚编号越界。
  * @note   先清除 PINMUX_MODE_MASK 再合入 mode；不等价于重置整个寄存器，也不保证清除掩码外的旧属性。
  */
int acts_pinmux_set(unsigned int pin, unsigned int mode)
{
	unsigned int key, ctl_reg, val;                            /* 保存中断状态、目标寄存器地址及读改写后的值。 */

	if (pin >= GPIO_MAX_PIN_NUM)                               /* 阻止越界编号换算成无效外设地址。 */
		return -EINVAL;                                    /* 无效引脚不执行寄存器写入。 */

	ctl_reg = GPIO_REG_CTL(GPIO_REG_BASE, pin);                /* 根据 GPIO 基址与编号定位该引脚控制寄存器。 */

	key = irq_lock();                                          /* 保存原中断状态，防止同核中断打断寄存器读改写。 */

	val = (sys_read32(ctl_reg) & ~PINMUX_MODE_MASK) | mode;    /* 保留掩码外原值，并合入调用者提供的模式与属性位。 */
	sys_write32(val, ctl_reg);                                 /* 提交新的引脚控制值。 */

	irq_unlock(key);                                           /* 恢复进入临界区前的中断状态。 */

	return 0;                                                  /* 本次寄存器访问流程已结束。 */
}

/**
  * @brief  读取一个 GPIO 控制寄存器中的复用模式字段。
  * @param  pin：[输入]有效 GPIO 编号；本函数未作边界检查。
  * @param  mode：[输出]用于保存模式字段的可写整数，不可为空。
  * @retval 固定返回 0。
  * @note   只回读 PINMUX_MODE_MASK 覆盖的字段，不返回全部电气属性。
  */
int acts_pinmux_get(unsigned int pin, unsigned int *mode)
{
	unsigned int ctl_reg = GPIO_REG_CTL(GPIO_REG_BASE, pin);    /* 定位要回读的引脚控制寄存器。 */

	*mode = sys_read32(ctl_reg) & PINMUX_MODE_MASK;    /* 只提取复用模式位，写入调用者的输出对象。 */

	return 0;                                                   /* 本次寄存器访问流程已结束。 */
}

/**
  * @brief  按表项顺序设置一组引脚，供板级初始化使用。
  * @param  pinconf：[输入]至少 pins 项的只读配置表；pins 大于 0 时不可为空。
  * @param  pins：[输入]待应用的配置项数，单位为项。
  * @retval 无。
  * @note   逐项调用 acts_pinmux_set；未汇总或返回单项配置错误。
  */
void acts_pinmux_setup_pins(const struct acts_pin_config *pinconf, int pins)
{
	int i;                                                           /* 配置表索引，按表内顺序推进。 */

	for (i = 0; i < pins; i++) {                                     /* 遍历指定数量的引脚配置，不处理表外内容。 */
		acts_pinmux_set(pinconf[i].pin_num, pinconf[i].mode);    /* 把当前表项交给带临界区保护的单引脚写入函数。 */
		//printk("pin=%d, ctl_reg=0x%x\n", pinconf[i].pin_num,
		//	pinconf[i].mode);
	}
}
