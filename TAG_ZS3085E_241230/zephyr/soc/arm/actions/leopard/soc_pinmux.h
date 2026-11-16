/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Actions SoC 引脚复用接口与设备树配置表生成宏。
 */

/* 包含保护；非汇编分支才公开 C 结构体及函数。devicetree.h 提供下方 DT_* 宏。 */
#ifndef	_ACTIONS_SOC_PINMUX_H_
#define	_ACTIONS_SOC_PINMUX_H_

#ifndef _ASMLANGUAGE

#include <devicetree.h>


/* 单引脚配置表项；板级静态数组交给 acts_pinmux_setup_pins 顺序写入。 */
struct acts_pin_config {
	unsigned int pin_num;    /* GPIO 编号，不是端口内相对引脚位。 */
	unsigned int mode;       /* 硬件复用及电气属性的组合控制值。 */
};

/**
  * @brief  在中断临界区内更新单引脚的复用配置。
  * @param  pin：[输入]小于 GPIO_MAX_PIN_NUM 的 GPIO 编号。
  * @param  mode：[输入]合法复用及电气属性组合。
  * @retval 0：写入结束；-EINVAL：编号越界。
  * @note   清除 PINMUX_MODE_MASK 后合入 mode，掩码外旧位可能保留。
  */
int acts_pinmux_set(unsigned int pin, unsigned int mode);

/**
  * @brief  回读单引脚的复用模式字段。
  * @param  pin：[输入]有效 GPIO 编号，本函数不检查边界。
  * @param  mode：[输出]可写整数地址，不可为空。
  * @retval 固定为 0。
  * @note   只返回 PINMUX_MODE_MASK 覆盖的字段。
  */
int acts_pinmux_get(unsigned int pin, unsigned int *mode);

/**
  * @brief  顺序应用引脚配置数组。
  * @param  pinconf：[输入]至少 pins 项的可读数组，pins 大于 0 时不可为空。
  * @param  pins：[输入]配置项数量。
  * @retval 无。
  * @note   不汇总单项错误。
  */
void acts_pinmux_setup_pins(const struct acts_pin_config *pinconf, int pins);

/**
  * @brief  声明 ROM 提供的批量引脚配置入口。
  * @param  pinconf：[输入]至少 pins 项的可读配置表。
  * @param  pins：[输入]配置项数量。
  * @retval 无。
  * @note   本仓库未提供可核实的 ROM 实现；不能保证与 C 实现完全相同的错误处理。
  */
void rom_pinmux_setup_pins(const struct acts_pin_config *pinconf, int pins);


/* 从实例 inst 的 pinctrl_0 属性提取第 i 个引用节点。 */
#define NODE_ID_PINCTRL_0(inst, i) \
	DT_INST_PHANDLE_BY_IDX(inst, pinctrl_0, i)

#if 0
/* 未启用 #if 0 分支：读取引用节点的 pinnum 属性。 */
#define ACTS_PIN_PROP_PINNUM(inst, i) \
		DT_PROP(NODE_ID_PINCTRL_0(inst,i), pinnum)
/* 未启用 #if 0 分支：读取引用节点的 modes 属性。 */
#define ACTS_PIN_PROP_MODES(inst, i) \
		DT_PROP(NODE_ID_PINCTRL_0(inst,i), modes)	

/* 未启用 #if 0 分支：将设备树属性组装为引脚表项。 */
#define ACTS_DT_PIN(inst, i)				\
	{							\
		ACTS_PIN_PROP_PINNUM(inst, i),			\
		DT_PROP(NODE_ID_PINCTRL_0(inst,i), modes), \
	}
#endif

/* 把 node 的 pinnum 与 modes 属性组装为一个 C 初始化项。 */
#define PIN_MFP_CONFIG(node)				\
	{.pin_num = DT_PROP(node, pinnum),		\
	 .mode = DT_PROP(node, modes) },


/* 仅当实例 n 有 pinctrl_0 属性时遍历其首引用节点子项，展开配置表项。 */
#define FOREACH_PIN_MFP(n) IF_ENABLED(DT_INST_NODE_HAS_PROP(n, pinctrl_0), (DT_FOREACH_CHILD(NODE_ID_PINCTRL_0(n, 0), PIN_MFP_CONFIG)))


#endif    /* !_ASMLANGUAGE */

#endif    /* _ACTIONS_SOC_PINMUX_H_	*/
