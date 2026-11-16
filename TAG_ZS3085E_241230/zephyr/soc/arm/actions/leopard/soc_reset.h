/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Actions SoC 外设复位编号与接口；0～31 对应 RMU_MRCR0，32～63 对应 RMU_MRCR1。
 */

/* 包含保护；C 函数声明排除在 _ASMLANGUAGE 汇编包含分支之外。 */
#ifndef	_ACTIONS_SOC_RESET_H_
#define	_ACTIONS_SOC_RESET_H_

#define	RESET_ID_DMA			0             /* DMA 外设的复位编号，对应 RMU_MRCR0 位 0。 */
#define	RESET_ID_SD0			1             /* SD0 外设的复位编号，对应 RMU_MRCR0 位 1。 */
#define	RESET_ID_SD1			2             /* SD1 外设的复位编号，对应 RMU_MRCR0 位 2。 */
#define	RESET_ID_OTFD			3             /* OTFD 外设的复位编号，对应 RMU_MRCR0 位 3。 */
#define	RESET_ID_SPI0			4             /* SPI0 外设的复位编号，对应 RMU_MRCR0 位 4。 */
#define	RESET_ID_SPI1			5             /* SPI1 外设的复位编号，对应 RMU_MRCR0 位 5。 */
#define	RESET_ID_SPI2			6             /* SPI2 外设的复位编号，对应 RMU_MRCR0 位 6。 */
#define	RESET_ID_SPI3			7             /* SPI3 外设的复位编号，对应 RMU_MRCR0 位 7。 */
#define	RESET_ID_SPI0CACHE		8             /* SPI0CACHE 外设的复位编号，对应 RMU_MRCR0 位 8。 */
#define	RESET_ID_SPI1CACHE		9             /* SPI1CACHE 外设的复位编号，对应 RMU_MRCR0 位 9。 */
#define	RESET_ID_USB			10            /* USB 外设的复位编号，对应 RMU_MRCR0 位 10。 */
#define	RESET_ID_USB2			11            /* USB2 外设的复位编号，对应 RMU_MRCR0 位 11。 */
#define	RESET_ID_DE				12    /* DE 外设的复位编号，对应 RMU_MRCR0 位 12。 */
#define	RESET_ID_JPEG			13            /* JPEG 外设的复位编号，对应 RMU_MRCR0 位 13。 */
#define	RESET_ID_LCD			14            /* LCD 外设的复位编号，对应 RMU_MRCR0 位 14。 */
#define	RESET_ID_GPU			15            /* GPU 外设的复位编号，对应 RMU_MRCR0 位 15。 */
#define	RESET_ID_SE				16    /* SE 外设的复位编号，对应 RMU_MRCR0 位 16。 */
#define	RESET_ID_PWM			17            /* PWM 外设的复位编号，对应 RMU_MRCR0 位 17。 */
#define	RESET_ID_AVS			18            /* AVS 外设的复位编号，对应 RMU_MRCR0 位 18。 */
#define	RESET_ID_LRADC			19            /* LRADC 外设的复位编号，对应 RMU_MRCR0 位 19。 */

#define	RESET_ID_SDMA			21    /* SDMA 外设的复位编号，对应 RMU_MRCR0 位 21。 */
#define	RESET_ID_I2C3			23    /* I2C3 外设的复位编号，对应 RMU_MRCR0 位 23。 */

#define	RESET_ID_UART0			24    /* UART0 外设的复位编号，对应 RMU_MRCR0 位 24。 */
#define	RESET_ID_UART1			25    /* UART1 外设的复位编号，对应 RMU_MRCR0 位 25。 */
#define	RESET_ID_UART2			26    /* UART2 外设的复位编号，对应 RMU_MRCR0 位 26。 */
#define	RESET_ID_I2C0			27    /* I2C0 外设的复位编号，对应 RMU_MRCR0 位 27。 */
#define	RESET_ID_I2C1			28    /* I2C1 外设的复位编号，对应 RMU_MRCR0 位 28。 */
#define	RESET_ID_I2C2			29    /* I2C2 外设的复位编号，对应 RMU_MRCR0 位 29。 */

#define	RESET_ID_DSP			32    /* DSP 外设的复位编号，对应 RMU_MRCR1 位 0。 */
#define	RESET_ID_DSP_PART		33    /* DSP_PART 外设的复位编号，对应 RMU_MRCR1 位 1。 */
#define	RESET_ID_DAC			34    /* DAC 外设的复位编号，对应 RMU_MRCR1 位 2。 */
#define	RESET_ID_ADC			35    /* ADC 外设的复位编号，对应 RMU_MRCR1 位 3。 */
#define	RESET_ID_I2STX			36    /* I2STX 外设的复位编号，对应 RMU_MRCR1 位 4。 */
#define	RESET_ID_I2SRX			37    /* I2SRX 外设的复位编号，对应 RMU_MRCR1 位 5。 */
#define	RESET_ID_I2STX1			38    /* I2STX1 外设的复位编号，对应 RMU_MRCR1 位 6。 */

#define	RESET_ID_TIMER0			42    /* TIMER0 外设的复位编号，对应 RMU_MRCR1 位 10。 */
#define	RESET_ID_TIMER1			43    /* TIMER1 外设的复位编号，对应 RMU_MRCR1 位 11。 */
#define	RESET_ID_TIMER2			44    /* TIMER2 外设的复位编号，对应 RMU_MRCR1 位 12。 */
#define	RESET_ID_TIMER3			45    /* TIMER3 外设的复位编号，对应 RMU_MRCR1 位 13。 */
#define	RESET_ID_TIMER4			46    /* TIMER4 外设的复位编号，对应 RMU_MRCR1 位 14。 */
#define	RESET_ID_TIMER5			47    /* TIMER5 外设的复位编号，对应 RMU_MRCR1 位 15。 */

#define	RESET_ID_SPIMT0			48    /* SPIMT0 外设的复位编号，对应 RMU_MRCR1 位 16。 */
#define	RESET_ID_SPIMT1			49    /* SPIMT1 外设的复位编号，对应 RMU_MRCR1 位 17。 */
#define	RESET_ID_I2CMT0			50    /* I2CMT0 外设的复位编号，对应 RMU_MRCR1 位 18。 */
#define	RESET_ID_I2CMT1			51    /* I2CMT1 外设的复位编号，对应 RMU_MRCR1 位 19。 */

#define	RESET_ID_BT				56    /* BT 外设的复位编号，对应 RMU_MRCR1 位 24。 */

#define	RESET_ID_MAX_ID			63    /* 允许的复位编号上限，包含该值。 */

#ifndef _ASMLANGUAGE

/**
  * @brief  清零相应 RMU 位以施加外设复位。
  * @param  reset_id：[输入]0～RESET_ID_MAX_ID 内的有效 RESET_ID_*。
  * @retval 无；超出上限不操作。
  * @note   复位会使外设配置失效。
  */
void acts_reset_peripheral_assert(int reset_id);

/**
  * @brief  置一相应 RMU 位以解除外设复位。
  * @param  reset_id：[输入]0～RESET_ID_MAX_ID 内的有效 RESET_ID_*。
  * @retval 无；超出上限不操作。
  * @note   解除后仍需重新配置外设。
  */
void acts_reset_peripheral_deassert(int reset_id);

/**
  * @brief  依次施加和解除外设复位。
  * @param  reset_id：[输入]0～RESET_ID_MAX_ID 内的有效 RESET_ID_*。
  * @retval 无；超出上限不操作。
  * @note   内部不增加延时，调用者需安排重新配置及软件缓存失效。
  */
void acts_reset_peripheral(int reset_id);

#endif    /* _ASMLANGUAGE */

#endif    /* _ACTIONS_SOC_RESET_H_	*/
