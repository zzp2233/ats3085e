/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Actions SoC 外设复位控制；RMU 位清零表示施加复位。
 */

#include <kernel.h>
#include <soc.h>

/**
  * @brief  选择 RMU 寄存器组并施加或解除指定外设复位。
  * @param  reset_id：[输入]RESET_ID_* 编号，应在 0～RESET_ID_MAX_ID 内。
  * @param  assert：[输入]非零为施加复位，0 为解除复位。
  * @retval 无；超出上限直接返回。
  * @note   本实现只检查上限；调用者不得传负数。中断锁保护寄存器读改写。
  */
static void acts_reset_peripheral_control(int reset_id, int assert)
{
	unsigned int key;                                           /* 保存进入临界区前的中断状态。 */

	if (reset_id > RESET_ID_MAX_ID)                             /* 编号超过已定义的 RMU 控制范围时拒绝访问。 */
		return;                                             /* 无效编号不改变任何复位位。 */

	key = irq_lock();                                           /* 禁止同核中断介入复位寄存器的读改写。 */

	if (assert) {                                               /* 非零请求进入低有效复位的施加路径。 */
		if (reset_id < 32) {                                /* 前 32 个复位编号位于 RMU_MRCR0。 */
			sys_clear_bit(RMU_MRCR0, reset_id);         /* 清零第一组对应位，施加复位。 */
		} else {
			sys_clear_bit(RMU_MRCR1, reset_id - 32);    /* 将编号换算为第二组位号并施加复位。 */
		}
	} else {
		if (reset_id < 32) {                                /* 前 32 个复位编号位于 RMU_MRCR0。 */
			sys_set_bit(RMU_MRCR0, reset_id);           /* 置一第一组对应位，解除复位。 */
		} else {
			sys_set_bit(RMU_MRCR1, reset_id - 32);      /* 将编号换算为第二组位号并解除复位。 */
		}
	}
	irq_unlock(key);                                            /* 恢复进入前的中断状态。 */
}

/**
  * @brief  施加外设复位，使相应 RMU 位清零。
  * @param  reset_id：[输入]有效 RESET_ID_* 外设编号。
  * @retval 无。
  * @note   复位会使外设配置失效，调用者需安排后续重新配置。
  */
void acts_reset_peripheral_assert(int reset_id)
{
	acts_reset_peripheral_control(reset_id, 1);    /* 复用统一控制入口，选择施加复位。 */
}

/**
  * @brief  解除外设复位，使相应 RMU 位置一。
  * @param  reset_id：[输入]有效 RESET_ID_* 外设编号。
  * @retval 无。
  * @note   解除后仍需按驱动流程恢复寄存器配置。
  */
void acts_reset_peripheral_deassert(int reset_id)
{
	acts_reset_peripheral_control(reset_id, 0);    /* 复用统一控制入口，选择解除复位。 */
}

/**
  * @brief  依次施加和解除复位，恢复外设初始硬件状态。
  * @param  reset_id：[输入]有效 RESET_ID_* 外设编号。
  * @retval 无。
  * @note   本函数没有额外延时；SPI 驱动复位后还会清除软件配置缓存。
  */
void acts_reset_peripheral(int reset_id)
{
	acts_reset_peripheral_assert(reset_id);      /* 先清除目标复位位。 */
	acts_reset_peripheral_deassert(reset_id);    /* 随后置一目标复位位，使外设脱离复位。 */
}
