/* CLOCK_ID 为门控位号：小于32访问CMU_DEVCLKEN0，其余访问CMU_DEVCLKEN1的编号减32位。频率设置只支持实现中列出的外设；时钟门控使能不等于外设复位或引脚配置。 */
/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Leopard 时钟门控编号及调频接口。
 */

#ifndef	_ACTIONS_SOC_CLOCK_H_
#define	_ACTIONS_SOC_CLOCK_H_

#define	CLOCK_ID_DMA			0             /* 门控位号，用于DMA模块。 */
#define	CLOCK_ID_SD0			1             /* 门控位号，用于SD0模块。 */
#define	CLOCK_ID_SD1			2             /* 门控位号，用于SD1模块。 */
#define	CLOCK_ID_OTFD			3             /* 门控位号，用于OTFD模块。 */
#define	CLOCK_ID_SPI0			4             /* 门控位号，用于SPI0模块。 */
#define	CLOCK_ID_SPI1			5             /* 门控位号，用于SPI1模块。 */
#define	CLOCK_ID_SPI2			6             /* 普通SPI2门控位6，本次从机驱动实际使用。 */
#define	CLOCK_ID_SPI3			7             /* 门控位号，用于SPI3模块。 */
#define	CLOCK_ID_SPI0CACHE		8             /* 门控位号，用于SPI0CACHE模块。 */
#define	CLOCK_ID_SPI1CACHE		9             /* 门控位号，用于SPI1CACHE模块。 */
#define	CLOCK_ID_USB			10            /* 门控位号，用于USB模块。 */
#define	CLOCK_ID_USB2			11            /* USB主机时钟门控。 */
#define	CLOCK_ID_DE				12    /* 门控位号，用于DE模块。 */
#define	CLOCK_ID_JPEG			13            /* JPEG时钟门控。 */
#define	CLOCK_ID_LCD			14            /* 门控位号，用于LCD模块。 */
#define	CLOCK_ID_GPU			15            /* 门控位号，用于GPU模块。 */
#define	CLOCK_ID_SE			    16        /* 门控位号，用于SE模块。 */
#define	CLOCK_ID_PWM0			17            /* 门控位号，用于PWM0模块。 */
#define	CLOCK_ID_AVS			18            /* 门控位号，用于AVS模块。 */
//#define	CLOCK_ID_TIMER			18
#define	CLOCK_ID_LRADC			19            /* 门控位号，用于LRADC模块。 */

#define	CLOCK_ID_CPUTIMER		20    /* 门控位号，用于CPUTIMER模块。 */
#define	CLOCK_ID_SDMA   		21    /* 门控位号，用于SDMA模块。 */

#define	CLOCK_ID_I2C3   		23    /* 门控位号，用于I2C3模块。 */

#define	CLOCK_ID_UART0			24    /* 门控位号，用于UART0模块。 */
#define	CLOCK_ID_UART1			25    /* 门控位号，用于UART1模块。 */
#define	CLOCK_ID_UART2			26    /* 门控位号，用于UART2模块。 */
#define	CLOCK_ID_I2C0			27    /* 门控位号，用于I2C0模块。 */
#define	CLOCK_ID_I2C1			28    /* 门控位号，用于I2C1模块。 */
#define	CLOCK_ID_I2C2			29    /* 门控位号，用于I2C2模块。 */
#define	CLOCK_ID_EXINT			30    /* 门控位号，用于EXINT模块。 */


#define	CLOCK_ID_DSP			32    /* 门控位号，用于DSP模块。 */
#define	CLOCK_ID_ASRC			33    /* 门控位号，用于ASRC模块。 */
#define	CLOCK_ID_AUDDSPTIMER	33            /* 门控位号，用于AUDDSPTIMER模块。 */
#define	CLOCK_ID_DAC			34    /* 门控位号，用于DAC模块。 */
#define	CLOCK_ID_ADC			35    /* 门控位号，用于ADC模块。 */
#define	CLOCK_ID_I2STX			36    /* 门控位号，用于I2STX模块。 */
#define	CLOCK_ID_I2SRX			37    /* 门控位号，用于I2SRX模块。 */
#define CLOCK_ID_I2SSRDCLK      38            /* 门控位号，用于I2SSRDCLK模块。 */
#define CLOCK_ID_I2SHCLKEN      39            /* 门控位号，用于I2SHCLKEN模块。 */

#define CLOCK_ID_DACANACLK      (32 + 8)    /* 门控位号，用于DACANACLK模块。 */

#define CLOCK_ID_TIMER0        (32 + 10)    /* 门控位号，用于TIMER0模块。 */
#define CLOCK_ID_TIMER1        (32 + 11)    /* 门控位号，用于TIMER1模块。 */
#define CLOCK_ID_TIMER2        (32 + 12)    /* 门控位号，用于TIMER2模块。 */
#define CLOCK_ID_TIMER3        (32 + 13)    /* 门控位号，用于TIMER3模块。 */
#define CLOCK_ID_TIMER4        (32 + 14)    /* 门控位号，用于TIMER4模块。 */
#define CLOCK_ID_TIMER5        (32 + 15)    /* 门控位号，用于TIMER5模块。 */

#define	CLOCK_ID_SPIMT0			48    /* 门控位号，用于SPIMT0模块。 */
#define	CLOCK_ID_SPIMT1			49    /* 门控位号，用于SPIMT1模块。 */
#define	CLOCK_ID_I2CMT0			50    /* 门控位号，用于I2CMT0模块。 */
#define	CLOCK_ID_I2CMT1			51    /* 门控位号，用于I2CMT1模块。 */

#define	CLOCK_ID_PWM1			(32 + 20)    /* 门控位号，用于PWM1模块。 */
#define	CLOCK_ID_PWM2			(32 + 21)    /* 门控位号，用于PWM2模块。 */
#define	CLOCK_ID_PWM3			(32 + 22)    /* 门控位号，用于PWM3模块。 */

#define	CLOCK_ID_MAX_ID			63    /* 门控编号上限，两组各32位共64位。 */

#ifndef _ASMLANGUAGE

/**
  * @brief  打开指定外设的时钟门控。
  * @param  clock_id：[输入] CLOCK_ID_* 编号，调用者须保证为 0～63。
  * @retval 无。
  * @note   门控读改写在本地关中断临界区内；不配置频率。
  */
void acts_clock_peripheral_enable(int clock_id);

/**
  * @brief  关闭指定外设的时钟门控。
  * @param  clock_id：[输入] CLOCK_ID_* 编号，调用者须保证为 0～63。
  * @retval 无。
  * @note   调用者须先停止外设传输；关时钟不会自动完成 SPI 数据收发。
  */
void acts_clock_peripheral_disable(int clock_id);

/**
  * @brief  根据 COREPLL 倍频字段计算频率。
  * @param  无。
  * @retval COREPLL 当前频率，单位 Hz。
  * @note   取低6位乘 8 MHz，不检测 PLL 锁定状态。
  */
uint32_t clk_rate_get_corepll(void);

/**
  * @brief  按外设时钟编号分派频率配置。
  * @param  clock_id：[输入] CLOCK_ID_* 编号；SPI0～3 对应普通 SPI 时钟。
  * @param  rate_hz：[输入] 目标频率，单位 Hz，分频计算要求大于0。
  * @retval SPI2：0、-ERANGE 或 -EIO；其他外设保留原有返回约定。
  * @note   实际频率由父时钟及分频编码决定；从机线上的 SCK 仍由外部主机提供。
  */
int clk_set_rate(int clock_id,  uint32_t rate_hz);

/**
  * @brief  读取由 HOSC 驱动的 SPI2 时钟分频结果。
  * @param  clock_id：[输入] 外设时钟编号。
  * @retval SPI2 分频回读结果，单位 Hz；非 HOSC 或其他外设返回 0。
  */
uint32_t clk_get_rate(int clock_id);

/**
  * @brief  修改 AHB 分频字段并返回修改前的字段编码。
  * @param  div：[输入] 请求值；1 写编码1，4 写编码3，其他值写编码0。
  * @retval 原 CMU_SYSCLK 位9:8的原始编码0～3，并非实际分频数。
  * @note   本函数没有关中断保护；调用者须协调同一寄存器的并发写入。
  */
uint32_t clk_ahb_set(uint32_t div);

/**
  * @brief  读取 DSP 时钟源及分频字段并换算频率。
  * @param  无。
  * @retval DSP 频率，单位 Hz。
  */
uint32_t soc_freq_get_dsp_freq(void);

/**
  * @brief  读取 CPU 时钟源及分频字段并换算频率。
  * @param  无。
  * @retval CPU 频率，单位 Hz。
  */
uint32_t soc_freq_get_cpu_freq(void);

/**
  * @brief  将 CPU、DSP 时钟切到 COREPLL 并设置总线分频。
  * @param  dsp_mhz：[输入] DSP 目标频率，单位 MHz，须大于0且不高于 COREPLL。
  * @param  cpu_mhz：[输入] CPU 目标频率，单位 MHz，须大于0且不高于 COREPLL。
  * @retval 无。
  * @note   现有实现先关中断再检查参数；参数非法时直接返回，未恢复中断状态。这里只记录该行为。
  */
void soc_freq_set_cpu_clk(uint32_t dsp_mhz, uint32_t cpu_mhz);

/**
  * @brief  在关中断临界区内设置 GPU、显示引擎及 JPEG 时钟。
  * @param  gpu_mhz：[输入] GPU 目标频率，单位 MHz；0 表示不改。
  * @param  de_mhz：[输入] 显示引擎目标频率，单位 MHz；0 表示不改。
  * @param  jpeg_mhz：[输入] JPEG 目标频率，单位 MHz；0 表示不改。
  * @retval 无；任一请求高于 COREPLL 时不写配置。
  * @note   实际频率受分频量化限制，调用者需协调相关外设。
  */
void soc_freq_set_gpu_clk(uint32_t gpu_mhz, uint32_t de_mhz, uint32_t jpeg_mhz);

/**
  * @brief  把时钟源和分频编码换算为频率。
  * @param  divisor：[输入] 分频字段编码；0～13 表示除1～14，14/15 表示除1.5/2.5。
  * @param  clk_src：[输入] 时钟源编码：0 RC4M、1 HOSC、2 COREPLL、3 RC64M、4 RC96M、5 RC128M、6 RC32K。
  * @retval 计算频率，单位 Hz；未识别时钟源时为0。
  * @note   此换算按代码使用 HOSC=32 MHz 等标称值，不测量实际振荡误差。
  */
uint32_t soc_freq_calculate(uint8_t divisor, uint8_t clk_src);

/**
  * @brief  选择接近目标且按向上取整规则限频的分频编码。
  * @param  freq_mhz：[输入] 目标频率，单位 MHz，必须大于0。
  * @param  max_mhz：[输入] 父时钟频率，单位 MHz；调用者须保证范围合理。
  * @retval 0～15 的分频编码，14/15 为1.5/2.5分频。
  * @note   整数分频最大除14，目标过低时会钳位，不能保证始终不超过请求频率。
  */
uint8_t soc_freq_divisor_calculate(uint32_t freq_mhz, uint32_t max_mhz);

#endif    /* _ASMLANGUAGE */

#endif    /* _ACTIONS_SOC_CLOCK_H_	*/
