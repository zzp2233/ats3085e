/* SPI 驱动通过 acts_clock_peripheral_enable 打开门控，再通过 clk_set_rate 设置控制器内部时钟。寄存器按 CLOCK_ID 映射；门控编号与各时钟源选择字段不是同一概念。 */
/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Leopard 外设时钟门控、父时钟选择与分频实现。
 */

#include <kernel.h>
#include <device.h>
#include <soc.h>

/**
  * @brief  声明底层时钟使能入口。
  * @param  id：[输入] 外设时钟编号。
  * @retval 无。
  * @note   本文件没有调用此 extern；此处不展开其他实现。
  */
extern void cmu_dev_clk_enable(uint32_t id);

/**
  * @brief  声明底层时钟关闭入口。
  * @param  id：[输入] 外设时钟编号。
  * @retval 无。
  * @note   本文件没有调用此 extern。
  */
extern void cmu_dev_clk_disable(uint32_t id);

/**
  * @brief  在临界区内修改一位外设时钟门控。
  * @param  clock_id：[输入] 门控编号0～63；实现只拒绝大于上限的值，负值由调用者排除。
  * @param  enable：[输入] 非0开门控，0关门控。
  * @retval 无。
  * @note   关中断保护同一寄存器的读改写，避免本 CPU 中断并发覆盖。
  */
static void acts_clock_peripheral_control(int clock_id, int enable)
{
	unsigned int key;                                               /* 保存关中断前状态，退出时恢复。 */

	if (clock_id > CLOCK_ID_MAX_ID)                                 /* 超出门控寄存器覆盖范围时不执行移位。 */
		return;                                                 /* 结束当前无返回值接口；参数检查路径不继续写硬件。 */

	key = irq_lock();                                               /* 禁止本CPU中断以保护门控寄存器读改写。 */

	if (enable) {                                                   /* 非0请求设置时钟使能位。 */
		if (clock_id < 32) {                                    /* 编号0～31映射第一组门控。 */
			sys_set_bit(CMU_DEVCLKEN0, clock_id);           /* 置第一组门控对应位。 */
		} else {
			sys_set_bit(CMU_DEVCLKEN1, clock_id - 32);      /* 第二组编号减32得到组内位号。 */
		}
	} else {
		if (clock_id < 32) {                                    /* 编号0～31映射第一组门控。 */
			sys_clear_bit(CMU_DEVCLKEN0, clock_id);         /* 清第一组门控对应位。 */
		} else {
			sys_clear_bit(CMU_DEVCLKEN1, clock_id - 32);    /* 清第二组门控对应位。 */
		}
	}
	irq_unlock(key);                                                /* 恢复进入临界区前的中断状态。 */
}

/**
  * @brief  打开指定外设的时钟门控。
  * @param  clock_id：[输入] CLOCK_ID_* 编号，调用者须保证为 0～63。
  * @retval 无。
  * @note   门控读改写在本地关中断临界区内；不配置频率。
  */
void acts_clock_peripheral_enable(int clock_id)
{
	acts_clock_peripheral_control(clock_id, 1);    /* 统一经门控读改写函数打开时钟。 */
}

/**
  * @brief  关闭指定外设的时钟门控。
  * @param  clock_id：[输入] CLOCK_ID_* 编号，调用者须保证为 0～63。
  * @retval 无。
  * @note   调用者须先停止外设传输；关时钟不会自动完成 SPI 数据收发。
  */
void acts_clock_peripheral_disable(int clock_id)
{
	acts_clock_peripheral_control(clock_id, 0);    /* 统一经门控读改写函数关闭时钟。 */
}

/**
  * @brief  根据 COREPLL 倍频字段计算频率。
  * @param  无。
  * @retval COREPLL 当前频率，单位 Hz。
  * @note   取低6位乘 8 MHz，不检测 PLL 锁定状态。
  */
uint32_t clk_rate_get_corepll(void)
{
	return MHZ(((sys_read32(COREPLL_CTL)&0x3F)*8));    /* 读取倍频字段，按8 MHz步长换算为Hz。 */
}

/**
  * @brief  把时钟源和分频编码换算为频率。
  * @param  divisor：[输入] 分频字段编码；0～13 表示除1～14，14/15 表示除1.5/2.5。
  * @param  clk_src：[输入] 时钟源编码：0 RC4M、1 HOSC、2 COREPLL、3 RC64M、4 RC96M、5 RC128M、6 RC32K。
  * @retval 计算频率，单位 Hz；未识别时钟源时为0。
  * @note   此换算按代码使用 HOSC=32 MHz 等标称值，不测量实际振荡误差。
  */
uint32_t soc_freq_calculate(uint8_t divisor, uint8_t clk_src)
{
	uint32_t freq = 0;                        /* 未知源编码默认得到0 Hz。 */

	if (clk_src == 0) {                       /* 内部4 MHz RC时钟。 */
		freq = 4000000UL;                 /* 选4 MHz标称频率。 */
	} else if (clk_src == 1) {                /* 高速晶振按32 MHz换算。 */
		freq = 32000000UL;                /* 选32 MHz标称频率。 */
	} else if (clk_src == 3) {                /* 内部64 MHz RC时钟。 */
		freq = 64000000UL;                /* 选64 MHz标称频率。 */
	} else if (clk_src == 4) {                /* 内部96 MHz RC时钟。 */
		freq = 96000000UL;                /* 选96 MHz标称频率。 */
	} else if (clk_src == 5) {                /* 内部128 MHz RC时钟。 */
		freq = 128000000UL;               /* 选128 MHz标称频率。 */
	} else if (clk_src == 6) {                /* 内部32 kHz RC时钟。 */
		freq = 32000UL;                   /* 选32 kHz标称频率。 */
	} else if (clk_src == 2) {                /* 使用 COREPLL 当前倍频计算值。 */
		freq = clk_rate_get_corepll();    /* 从寄存器计算COREPLL频率。 */
	}

	if (divisor == 14) {                      /* 区分1.5的特殊分频编码。 */
		/* 编码14：除1.5。 */
		freq = freq * 10 / 15;            /* 使用整数运算完成除1.5。 */
	} else if (divisor == 15) {               /* 区分2.5的特殊分频编码。 */
		/* 编码15：除2.5。 */
		freq = freq * 10 / 25;            /* 使用整数运算完成除2.5。 */
	} else {
		freq = freq / (divisor + 1);      /* 普通编码加1得到真实整数除数。 */
	}

	return freq;                              /* 返回分频后的Hz值。 */
}

/**
  * @brief  选择接近目标且按向上取整规则限频的分频编码。
  * @param  freq_mhz：[输入] 目标频率，单位 MHz，必须大于0。
  * @param  max_mhz：[输入] 父时钟频率，单位 MHz；调用者须保证范围合理。
  * @retval 0～15 的分频编码，14/15 为1.5/2.5分频。
  * @note   整数分频最大除14，目标过低时会钳位，不能保证始终不超过请求频率。
  */
uint8_t soc_freq_divisor_calculate(uint32_t freq_mhz, uint32_t max_mhz)
{
	uint8_t divisor;                                              /* 保存4位时钟分频编码。 */

	if (max_mhz > freq_mhz && max_mhz <= (freq_mhz * 3 / 2)) {
		/* 父时钟在目标的1～1.5倍区间，选1.5分频。 */
		divisor = 14;                                         /* 编码14代表1.5分频。 */
	} else if ((max_mhz > 2 * freq_mhz) && max_mhz <= (freq_mhz * 5 / 2)) {
		/* 父时钟在目标的2～2.5倍区间，选2.5分频。 */
		divisor = 15;                                         /* 编码15代表2.5分频。 */
	} else {
		/* 其他区间使用整数分频并钳位编码。 */
		divisor = (max_mhz + freq_mhz - 1) / freq_mhz - 1;    /* 除数向上取整后减1，尽可能不超目标。 */
		if (divisor > 13) {                                   /* 普通整数分频编码最大13。 */
			divisor = 13;                                 /* 钳位到除14，可能仍高于过低请求。 */
		}
	}

	return divisor;                                               /* 返回可写入时钟分频字段的编码。 */
}

/**
  * @brief  读取 DSP 时钟源及分频字段并换算频率。
  * @param  无。
  * @retval DSP 频率，单位 Hz。
  */
uint32_t soc_freq_get_dsp_freq(void)
{
	uint8_t dspclk_div = (sys_read32(CMU_DSPCLK) & (0xF << 4)) >> 4;    /* 提取DSP分频编码位7:4。 */
	uint8_t dspclk_src = sys_read32(CMU_DSPCLK) & 0x7;                  /* 提取DSP时钟源编码位2:0。 */

	return soc_freq_calculate(dspclk_div, dspclk_src);                  /* 按DSP源和分频计算Hz。 */
}

/**
  * @brief  读取 CPU 时钟源及分频字段并换算频率。
  * @param  无。
  * @retval CPU 频率，单位 Hz。
  */
uint32_t soc_freq_get_cpu_freq(void)
{
	uint8_t cpuclk_div = (sys_read32(CMU_SYSCLK) & (0xF << 4)) >> 4;    /* 提取CPU分频编码位7:4。 */
	uint8_t cpuclk_src = sys_read32(CMU_SYSCLK) & 0x7;                  /* 提取CPU时钟源编码位2:0。 */

	return soc_freq_calculate(cpuclk_div, cpuclk_src);                  /* 按CPU源和分频计算Hz。 */
}

/**
  * @brief  在关中断临界区内设置 GPU、显示引擎及 JPEG 时钟。
  * @param  gpu_mhz：[输入] GPU 目标频率，单位 MHz；0 表示不改。
  * @param  de_mhz：[输入] 显示引擎目标频率，单位 MHz；0 表示不改。
  * @param  jpeg_mhz：[输入] JPEG 目标频率，单位 MHz；0 表示不改。
  * @retval 无；任一请求高于 COREPLL 时不写配置。
  * @note   实际频率受分频量化限制，调用者需协调相关外设。
  */
void soc_freq_set_gpu_clk(uint32_t gpu_mhz, uint32_t de_mhz, uint32_t jpeg_mhz)
{
	uint32_t val, flags, divisor;                                           /* 寄存器工作值、中断状态和分频编码。 */
	uint32_t corepll_mhz;                                                   /* 父时钟频率，使用MHz与请求比较。 */

	flags = irq_lock();                                                     /* 保护多个关联时钟寄存器的配置过程。 */

	corepll_mhz = clk_rate_get_corepll() / 1000000UL;                       /* 把COREPLL的Hz转换为MHz。 */

	if ((gpu_mhz > corepll_mhz)
		|| (de_mhz > corepll_mhz)
		|| (jpeg_mhz > corepll_mhz)) {
		irq_unlock(flags);                                              /* 恢复调用前中断状态。 */
		return ;                                                        /* 超出父时钟范围，不应用本次GPU/DE/JPEG请求。 */
	}

	/* GPU非零请求：保留其他字段，切COREPLL并写分频。 */
	if (gpu_mhz) {                                                          /* GPU请求为0时保留当前配置。 */
		divisor = soc_freq_divisor_calculate(gpu_mhz, corepll_mhz);     /* 由目标MHz与父频MHz得到分频编码。 */

		val = sys_read32(CMU_GPUCLK);                                   /* 读取当前配置，后续只替换源和分频字段。 */
		val &= ~((0xf << 0) | (0x3 << 8));                              /* 清分频字段及源选择字段，保留其他位。 */
		val |= (0x1 << 8);                                              /* 时钟源选择COREPLL。 */
		val |= (divisor << 0);                                          /* 合入本次分频编码。 */
		sys_write32(val, CMU_GPUCLK);                                   /* 提交该外设的新源和分频配置。 */
	}

	/* 显示引擎非零请求：切COREPLL并更新分频。 */
	if (de_mhz) {                                                           /* DE请求为0时保留当前配置。 */
		divisor = soc_freq_divisor_calculate(de_mhz, corepll_mhz);      /* 由目标MHz与父频MHz得到分频编码。 */

		val = sys_read32(CMU_DECLK);                                    /* 读取当前配置，后续只替换源和分频字段。 */
		val &= ~((0xf << 0) | (0x3 << 8));                              /* 清分频字段及源选择字段，保留其他位。 */
		val |= (0x1 << 8);                                              /* 时钟源选择COREPLL。 */
		val |= (divisor << 0);                                          /* 合入本次分频编码。 */
		sys_write32(val, CMU_DECLK);                                    /* 提交该外设的新源和分频配置。 */
	}

	/* JPEG非零请求：切COREPLL并更新分频。 */
	if (jpeg_mhz) {                                                         /* JPEG请求为0时保留当前配置。 */
		divisor = soc_freq_divisor_calculate(jpeg_mhz, corepll_mhz);    /* 由目标MHz与父频MHz得到分频编码。 */

		val = sys_read32(CMU_JPEGCLK);                                  /* 读取当前配置，后续只替换源和分频字段。 */
		val &= ~((0xf << 0) | (0x3 << 8));                              /* 清分频字段及源选择字段，保留其他位。 */
		val |= (0x1 << 8);                                              /* 时钟源选择COREPLL。 */
		val |= (divisor << 0);                                          /* 合入本次分频编码。 */
		sys_write32(val, CMU_JPEGCLK);                                  /* 提交该外设的新源和分频配置。 */
	}

	irq_unlock(flags);                                                      /* 恢复调用前中断状态。 */
}

/**
  * @brief  将 CPU、DSP 时钟切到 COREPLL 并设置总线分频。
  * @param  dsp_mhz：[输入] DSP 目标频率，单位 MHz，须大于0且不高于 COREPLL。
  * @param  cpu_mhz：[输入] CPU 目标频率，单位 MHz，须大于0且不高于 COREPLL。
  * @retval 无。
  * @note   现有实现先关中断再检查参数；参数非法时直接返回，未恢复中断状态。这里只记录该行为。
  */
void soc_freq_set_cpu_clk(uint32_t dsp_mhz, uint32_t cpu_mhz)
{
	uint32_t val, flags, divisor;                                  /* 寄存器工作值、中断状态和分频编码。 */
	uint32_t corepll_mhz;                                          /* 父时钟频率，使用MHz与请求比较。 */

	flags = irq_lock();                                            /* 保护多个关联时钟寄存器的配置过程。 */

	corepll_mhz = clk_rate_get_corepll() / 1000000UL;              /* 把COREPLL的Hz转换为MHz。 */

	if ((cpu_mhz > corepll_mhz) || (!cpu_mhz))                     /* CPU目标非法时结束；当前路径未恢复先前锁住的中断。 */
		return;                                                /* 结束当前无返回值接口；参数检查路径不继续写硬件。 */

	if ((dsp_mhz > corepll_mhz) || (!dsp_mhz))                     /* DSP目标非法时结束；当前路径同样缺少中断恢复。 */
		return;                                                /* 结束当前无返回值接口；参数检查路径不继续写硬件。 */

	divisor = soc_freq_divisor_calculate(cpu_mhz, corepll_mhz);    /* 计算该处理器的分频编码。 */

	/* CPU选择COREPLL，写分频并按DVFS状态配置AHB。 */
	val = sys_read32(CMU_SYSCLK);                                  /* 读取当前处理器时钟配置。 */
	val &= ~((0xf << 4) | (0x7 << 0) | (3<<8));                    /* 清CPU分频、时钟源及AHB字段。 */
	val |= (0x2 << 0);                                             /* CPU时钟源选择COREPLL。 */
	val |= divisor << 4;                                           /* 把分频编码移入位7:4。 */
	if (soc_dvfs_opt())                                            /* 根据SoC DVFS配置选择AHB字段。 */
		val |= (0x1 << 8);                                     /* DVFS优化路径的AHB字段配置。 */
	else
		val |= (0x2 << 8);                                     /* 非优化路径的AHB字段配置；保留当前编码。 */
	sys_write32(val, CMU_SYSCLK);                                  /* 提交处理器及关联总线时钟字段。 */

	divisor = soc_freq_divisor_calculate(dsp_mhz, corepll_mhz);    /* 计算该处理器的分频编码。 */

	/* DSP选择COREPLL并设置DSP及APB分频字段。 */
	val = sys_read32(CMU_DSPCLK);                                  /* 读取当前处理器时钟配置。 */
	val &= ~((0x3f << 4) | (0x7 << 0));                            /* 清DSP与APB分频相关位以及源字段。 */
	val |= (0x2 << 0);                                             /* DSP时钟源选择COREPLL。 */
	val |= divisor << 4;                                           /* 把分频编码移入位7:4。 */
	val |= (0x2 << 8);                                             /* APB配置为二分频。 */
	sys_write32(val, CMU_DSPCLK);                                  /* 提交处理器及关联总线时钟字段。 */

	irq_unlock(flags);                                             /* 恢复调用前中断状态。 */
}

#ifdef CONFIG_MMC_ACTS

/**
  * @brief  设置 SD 控制器时钟源与分频。
  * @param  sd：[输入] SD控制器索引0或1。
  * @param  rate_hz：[输入] 目标Hz，必须大于0。
  * @retval 无。
  * @note   仅 CONFIG_MMC_ACTS 分支执行写寄存器，否则为空实现。
  */
static void  acts_clk_set_rate_sd(int sd, unsigned int rate_hz)
{
	unsigned int core_pll, hosc_hz, val, real_rate, div;      /* 保存父时钟Hz、寄存器值、实际Hz和除数。 */
	core_pll = clk_rate_get_corepll();                        /* 读取当前COREPLL父时钟Hz。 */
	hosc_hz = CONFIG_HOSC_CLK_MHZ*1000000;                    /* 按板级MHz配置换算晶振Hz。 */

	/* 按请求频率选择父时钟及分频；此函数实际只写时钟寄存器，不写SD读写延迟寄存器。 */
	if (rate_hz < hosc_hz/16) {                               /* 低频请求采用额外128预分频以扩大可用范围。 */
		/* 低速SD请求：HOSC先除128再整数分频，实际值随晶振配置变化。 */
		div = (hosc_hz+(128*rate_hz)-1)/(128*rate_hz);    /* 按预分频后的目标向上取整除数。 */
		val = 0x40 + div-1 ;                              /* 选择低速预分频并编码整数除数。 */
		real_rate = hosc_hz/(128*div);                    /* 计算实际SD时钟Hz用于日志。 */

	} else if (rate_hz <= hosc_hz) {                          /* 目标不超过HOSC时无需改用PLL。 */
		/* 中速SD请求：直接由HOSC整数分频，不采用旧注释中的固定频率范围。 */
		div = (hosc_hz+rate_hz-1)/rate_hz;                /* 由HOSC计算向上取整除数。 */
		val = div - 1;                                    /* 寄存器使用除数减1编码。 */
		real_rate = hosc_hz/div;                          /* 计算HOSC分频后的实际Hz。 */
	} else {
		/* 高速SD请求：改用COREPLL，实际频率由其当前值及分频数决定。 */
		div = (core_pll+rate_hz-1)/rate_hz;               /* 由COREPLL计算向上取整除数。 */
		real_rate = core_pll/div;                         /* 计算PLL分频后的实际Hz。 */
		val = (div-1)|(1<<8);                             /* 编码除数并选择COREPLL。 */
	}
	sys_write32(val, CMU_SD0CLK+sd*4);                        /* 按SD实例偏移写入时钟寄存器。 */

	printk("mmc%d: set rate %d Hz, real rate %d Hz, core pll=%d HZ\n",
		sd, rate_hz, real_rate, core_pll);

}
#else

/**
  * @brief  设置 SD 控制器时钟源与分频。
  * @param  sd：[输入] SD控制器索引0或1。
  * @param  rate_hz：[输入] 目标Hz，必须大于0。
  * @retval 无。
  * @note   仅 CONFIG_MMC_ACTS 分支执行写寄存器，否则为空实现。
  */
static void  acts_clk_set_rate_sd(int sd, unsigned int rate_hz)
{

}
#endif


#ifdef CONFIG_SOC_SPI0_USE_CK64M

/**
  * @brief  计算 CK64M 方案的 SPI 分频编码。
  * @param  max_freq：[输入] 父时钟Hz。
  * @param  spi_freq：[输入] 请求Hz，必须大于0。
  * @retval 0～15的编码，14/15表示1.5/2.5分频。
  * @note   仅 CONFIG_SOC_SPI0_USE_CK64M 分支编译。
  */
static unsigned int calc_spi_clk_div(unsigned int max_freq, unsigned int spi_freq)
{
	unsigned int div;                                          /* 保存CK64M的4位分频编码。 */

	if (max_freq > spi_freq && max_freq <= (spi_freq * 3 / 2)) {
		/* 父时钟在目标的1～1.5倍区间，选1.5分频。 */
		div = 14;                                          /* 选择1.5特殊分频编码。 */
	} else if ((max_freq > 2 * spi_freq) && max_freq <= (spi_freq * 5 / 2)) {
		/* 父时钟在目标的2～2.5倍区间，选2.5分频。 */
		div = 15;                                          /* 选择2.5特殊分频编码。 */
	} else {
		/* 其他区间使用整数分频并钳位编码。 */
		div = (max_freq + spi_freq - 1) / spi_freq - 1;    /* 普通除数向上取整后转换为编码。 */
		if (div > 13) {                                    /* 普通分频超过硬件整数编码范围时钳位。 */
			div = 13;                                  /* 最大普通除数为14。 */
		}
	}

	return div;                                                /* 返回分频编码或先前保存的AHB字段。 */
}

/**
  * @brief  使能并校准 CK64M 后设置 SPI 时钟。
  * @param  clock_id：[输入] SPI时钟编号，此分支由 SPI0 调用。
  * @param  rate_hz：[输入] 请求Hz，必须大于0。
  * @retval 无。
  * @note   校准完成位采用无超时轮询；10的忙等参数单位为微秒。
  */
static void __acts_clk_set_rate_spi_ck64m(int clock_id, unsigned int rate_hz)
{
	unsigned int div, reg_val, real_rate;                                          /* 保存分频编码、寄存器值和实际Hz。 */

	div = calc_spi_clk_div(MHZ(64), rate_hz) & 0xf;                                /* 为64 MHz父时钟计算4位分频编码。 */

	/* 仅在S1域尚未使能CK64M时执行使能及校准。 */
	if (!(sys_read32(CMU_S1CLKCTL) & (1 << 2))) {
		/* 打开S1域CK64M。 */
		sys_write32(sys_read32(CMU_S1CLKCTL) | (1 << 2), CMU_S1CLKCTL);        /* 打开S1的CK64M时钟控制位，保留其他位。 */
		/* 打开S1BT域CK64M。 */
		sys_write32(sys_read32(CMU_S1BTCLKCTL) | (1 << 2), CMU_S1BTCLKCTL);    /* 打开蓝牙域CK64M时钟控制位。 */
		k_busy_wait(10);                                                       /* 忙等10微秒让时钟使能稳定。 */
		/* 写入CK64M校准启动值。 */
		sys_write32(0xe, CK64M_CTL);                                           /* 写校准控制值启动CK64M校准。 */
		/* 等待位8报告校准完成，无超时退出。 */
		while(!(sys_read32(CK64M_CTL) & (1 << 8))) {                           /* 等待CK64M状态位就绪，原实现没有超时。 */
			;
		}
	}

	/* 保留其他位，配置SPI实例的时钟源及分频编码。 */
	reg_val = sys_read32(CMU_SPI0CLK + ((clock_id - CLOCK_ID_SPI0) * 4));          /* 读取指定SPI实例时钟寄存器。 */
	reg_val &= ~0x30f;                                                             /* 只清时钟源和分频字段，保留其他位。 */
	reg_val |= (0x3 << 8) | (div << 0);                                            /* 选择CK64M源并写入分频编码。 */

	if (div == 14)                                                                 /* 特殊编码14对应除1.5。 */
		real_rate = MHZ(64) * 2 / 3;                                           /* 按1.5分频计算实际Hz。 */
	else if (div == 15)                                                            /* 特殊编码15对应除2.5。 */
		real_rate = MHZ(64) * 2 / 5;                                           /* 按2.5分频计算实际Hz。 */
	else
		real_rate = MHZ(64) / (div + 1);                                       /* 普通编码加1得到整数除数。 */

	sys_write32(reg_val, CMU_SPI0CLK + ((clock_id - CLOCK_ID_SPI0) * 4));          /* 把源和分频一次写回该SPI实例。 */

	printk("SPI%d: set rate %d Hz real rate %d Hz\n",
			clock_id - CLOCK_ID_SPI0, rate_hz, real_rate);
}

#endif

/**
  * @brief  选择 HOSC 或 COREPLL 并配置 SPI0～3 分频。
  * @param  clock_id：[输入] CLOCK_ID_SPI0～CLOCK_ID_SPI3。
  * @param  rate_hz：[输入] 目标Hz，必须大于0。
  * @retval 无。
  * @note   SPI0 可由编译配置改用 CK64M；其他实例按相邻4字节寄存器寻址。
  */
static void  acts_clk_set_rate_spi(int clock_id, unsigned int rate_hz)
{
	unsigned int core_pll, val, real_rate, div;                              /* 保存父频Hz、时钟配置值、实际Hz和整数除数。 */

#ifdef CONFIG_SOC_SPI0_USE_CK64M
	if (CLOCK_ID_SPI0 == clock_id)                                           /* 仅SPI0使用可选CK64M路径。 */
		return __acts_clk_set_rate_spi_ck64m(CLOCK_ID_SPI0, rate_hz);    /* 执行CK64M方案后结束，不再进入HOSC/PLL选择。 */
#endif

	if(rate_hz < CONFIG_HOSC_CLK_MHZ*1000000/2){                             /* 低于晶振一半的请求改用HOSC分频。 */
		div = (CONFIG_HOSC_CLK_MHZ*1000000+rate_hz-1)/rate_hz;           /* 除数向上取整以限制实际频率。 */
		real_rate = CONFIG_HOSC_CLK_MHZ*1000000/div;                     /* 计算HOSC路径实际Hz。 */
		val = (div-1);                                                   /* 源字段为0，分频字段为除数减1。 */
	}else{	
		core_pll = clk_rate_get_corepll();                               /* 读取当前COREPLL父时钟Hz。 */
		if((rate_hz >= 186000000) && (rate_hz < core_pll)){ // 此区间使用1.5分频。
			real_rate = core_pll*2/3;                                /* 特殊高速请求使用PLL的1.5分频。 */
			val = (0xe)|(1<<8);                                      /* 分频编码14并选择COREPLL源。 */
		}else{
			div = (core_pll+rate_hz-1)/rate_hz;                      /* 由COREPLL计算向上取整除数。 */
			real_rate = core_pll/div;                                /* 计算PLL分频后的实际Hz。 */
			val = (div-1)|(1<<8);                                    /* 编码除数并选择COREPLL。 */
		}
	}

	sys_write32(val, CMU_SPI0CLK + (clock_id - CLOCK_ID_SPI0)*4);            /* 普通SPI实例的时钟寄存器相隔4字节。 */

	printk("SPI%d: set rate %d Hz, real rate %d Hz,CMU_SPICLK=0x%x\n",
		clock_id - CLOCK_ID_SPI0, rate_hz, real_rate, val);
}


#ifdef CONFIG_SPIMT_ACTS

/**
  * @brief  设置独立 SPIMT 控制器的4 MHz父时钟分频。
  * @param  clock_id：[输入] CLOCK_ID_SPIMT0或1。
  * @param  rate_hz：[输入] 目标Hz，须大于0；高于4 MHz被限制为4 MHz。
  * @retval 无。
  * @note   仅 CONFIG_SPIMT_ACTS 分支有实际实现，整数除法不保证实际频率不超请求。
  */
static void  acts_clk_set_rate_spimt(int clock_id, unsigned int rate_hz)
{
	unsigned int val, div, real_rate;                                    /* 保存SPIMT时钟字段、除数和实际Hz。 */

	if (rate_hz > 4000000) {                                             /* SPIMT本路径父时钟只有4 MHz。 */
		rate_hz = 4000000;                                           /* 把过高请求限制到父频。 */
	}
	div = 4000000 / rate_hz;                                             /* 使用整数除法求除数；实际频率可能高于请求。 */
	real_rate = 4000000 / div;                                           /* 按最终除数计算实际Hz。 */
	val = (0 << 8) | (div - 1);                                          /* 选择4 MHz源并编码分频数。 */
	sys_write32(val, CMU_SPIMT0CLK + (clock_id - CLOCK_ID_SPIMT0)*4);    /* 写SPIMT实例时钟字段。 */

	printk("SPIMT%d: set rate %d Hz, real rate %d Hz\n",
		clock_id - CLOCK_ID_SPIMT0, rate_hz, real_rate);
}
#else

/**
  * @brief  设置独立 SPIMT 控制器的4 MHz父时钟分频。
  * @param  clock_id：[输入] CLOCK_ID_SPIMT0或1。
  * @param  rate_hz：[输入] 目标Hz，须大于0；高于4 MHz被限制为4 MHz。
  * @retval 无。
  * @note   仅 CONFIG_SPIMT_ACTS 分支有实际实现，整数除法不保证实际频率不超请求。
  */
static void  acts_clk_set_rate_spimt(int clock_id, unsigned int rate_hz)
{

}
#endif

#ifdef CONFIG_DISPLAY_LCDC

/**
  * @brief  选择 LCD 父时钟及两级分频。
  * @param  rate_hz：[输入] 目标Hz，必须大于0。
  * @retval 无。
  * @note   仅 CONFIG_DISPLAY_LCDC 分支编译，分频有范围钳位。
  */
static void acts_clk_set_rate_lcd(unsigned int rate_hz)
{
	uint32_t real_rate, div, pre_div;                                                 /* 保存LCD实际Hz、整数除数及预分频数。 */
	uint32_t src_sel, parent_clk;                                                     /* 保存时钟源字段和父频Hz。 */

	parent_clk = clk_rate_get_corepll();                                              /* 优先使用COREPLL作为LCD父频。 */
	if (rate_hz < parent_clk / 72) {                                                  /* 请求低于PLL可分频范围时改用晶振。 */
		parent_clk = CONFIG_HOSC_CLK_MHZ * 1000000u;                              /* HOSC的MHz配置转换为Hz。 */
		src_sel = 0;                                                              /* 源编码0选择HOSC。 */
	} else {
		src_sel = 1 << 8;                                                         /* 源编码1选择COREPLL。 */
	}

	pre_div = (rate_hz < parent_clk / 12) ? 6 : 1;                                    /* 低频时额外除6，其他情况不预分频。 */

	div = (parent_clk + rate_hz * pre_div - 1) / (rate_hz * pre_div);                 /* 计算包含预分频的向上取整整数除数。 */
	if (div < 1) div = 1;                                                             /* 除数下限为1。 */
	else if (div > 12) div = 12;                                                      /* LCD整数除数上限为12。 */

	real_rate = parent_clk / (div * pre_div);                                         /* 计算LCD实际频率Hz。 */
	sys_write32(src_sel | (div - 1) | ((pre_div > 1) ? (1 << 4) : 0), CMU_LCDCLK);    /* 合并源、整数除数和除6开关后写入。 */

	printk("LCD: set rate %d Hz, real rate %d Hz\n", rate_hz, real_rate);             /* 报告请求值与量化后的LCD频率。 */
}
#endif    /* CONFIG_DISPLAY_LCDC */

#ifdef CONFIG_DISPLAY_ENGINE

/**
  * @brief  配置显示引擎时钟并保留半整数分频编码。
  * @param  rate_hz：[输入] 目标Hz，必须大于0。
  * @retval 无。
  * @note   仅 CONFIG_DISPLAY_ENGINE 分支编译。
  */
static void acts_clk_set_rate_de(unsigned int rate_hz)
{
	uint32_t core_pll, real_rate, div2, div;          /* 保存PLL/实际Hz、两倍除数及编码中间值。 */

	core_pll = clk_rate_get_corepll();                /* 读取当前COREPLL父时钟Hz。 */
	div2 = (core_pll * 2 + rate_hz - 1) / rate_hz;    /* 按两倍父频计算除数以表达半整数分频。 */

	switch (div2) {                                   /* 单独映射1.5与2.5两种特殊分频。 */
	case 3:                                           /* 两倍除数3表示除1.5。 */
		div = 15;                                 /* 特殊分频的寄存器值在写入时减1。 */
		break;                                    /* 结束本分支，不落入后续case。 */
	case 5:                                           /* 两倍除数5表示除2.5。 */
		div = 16;                                 /* 特殊分频的寄存器值在写入时减1。 */
		break;                                    /* 结束本分支，不落入后续case。 */
	default:
		div = div2 / 2;                           /* 其余情况转回整数除数。 */
		if (div < 1) div = 1;                     /* 除数下限为1。 */
		else if (div > 14) div = 14;              /* 普通整数除数最大14。 */

		div2 = div * 2;                           /* 按钳位后的除数重算实际分频。 */
		break;                                    /* 结束本分支，不落入后续case。 */
	}

	real_rate = core_pll * 2 / div2;                  /* 计算最终分频下的实际Hz。 */

	sys_write32((1 << 8) | (div - 1), CMU_DECLK);     /* 选择PLL并写DE分频编码。 */

	printk("DE: set rate %d Hz, real rate %d Hz\n", rate_hz, real_rate);
}
#endif    /* CONFIG_DISPLAY_ENGINE */

#ifdef CONFIG_VG_LITE

/**
  * @brief  配置 GPU 的 COREPLL 分频及源选择。
  * @param  rate_hz：[输入] 目标Hz，必须大于0。
  * @retval 无。
  * @note   仅 CONFIG_VG_LITE 分支编译。
  */
static void acts_clk_set_rate_gpu(unsigned int rate_hz)
{
	uint32_t core_pll, real_rate, div2, div;                      /* 保存PLL/实际Hz、两倍除数及编码中间值。 */

	core_pll = clk_rate_get_corepll();                            /* 读取当前COREPLL父时钟Hz。 */
	div2 = (core_pll * 2 + rate_hz - 1) / rate_hz;                /* 按两倍父频计算除数以表达半整数分频。 */

	switch (div2) {                                               /* 单独映射1.5与2.5两种特殊分频。 */
	case 3:                                                       /* 两倍除数3表示除1.5。 */
		div = 15;                                             /* 特殊分频的寄存器值在写入时减1。 */
		break;                                                /* 结束本分支，不落入后续case。 */
	case 5:                                                       /* 两倍除数5表示除2.5。 */
		div = 16;                                             /* 特殊分频的寄存器值在写入时减1。 */
		break;                                                /* 结束本分支，不落入后续case。 */
	default:
		div = div2 / 2;                                       /* 其余情况转回整数除数。 */
		if (div < 1) div = 1;                                 /* 除数下限为1。 */
		else if (div > 14) div = 14;                          /* 普通整数除数最大14。 */

		div2 = div * 2;                                       /* 按钳位后的除数重算实际分频。 */
		break;                                                /* 结束本分支，不落入后续case。 */
	}

	real_rate = core_pll * 2 / div2;                              /* 计算最终分频下的实际Hz。 */

	sys_write32((1 << 12) | (1 << 8) | (div - 1), CMU_GPUCLK);    /* 写GPU额外控制位、PLL选择及分频编码。 */

	printk("GPU: set rate %d Hz, real rate %d Hz\n", rate_hz, real_rate);
}
#endif    /* CONFIG_VG_LITE */

#ifdef CONFIG_JPEG_HW

/**
  * @brief  配置 JPEG 的 COREPLL 分频。
  * @param  rate_hz：[输入] 目标Hz，必须大于0。
  * @retval 无。
  * @note   仅 CONFIG_JPEG_HW 分支编译。
  */
static void acts_clk_set_rate_jpeg(unsigned int rate_hz)
{
	uint32_t core_pll, real_rate, div2, div;           /* 保存PLL/实际Hz、两倍除数及编码中间值。 */

	core_pll = clk_rate_get_corepll();                 /* 读取当前COREPLL父时钟Hz。 */
	div2 = (core_pll * 2 + rate_hz - 1) / rate_hz;     /* 按两倍父频计算除数以表达半整数分频。 */

	switch (div2) {                                    /* 单独映射1.5与2.5两种特殊分频。 */
	case 3:                                            /* 两倍除数3表示除1.5。 */
		div = 15;                                  /* 特殊分频的寄存器值在写入时减1。 */
		break;                                     /* 结束本分支，不落入后续case。 */
	case 5:                                            /* 两倍除数5表示除2.5。 */
		div = 16;                                  /* 特殊分频的寄存器值在写入时减1。 */
		break;                                     /* 结束本分支，不落入后续case。 */
	default:
		div = div2 / 2;                            /* 其余情况转回整数除数。 */
		if (div < 1) div = 1;                      /* 除数下限为1。 */
		else if (div > 14) div = 14;               /* 普通整数除数最大14。 */

		div2 = div * 2;                            /* 按钳位后的除数重算实际分频。 */
		break;                                     /* 结束本分支，不落入后续case。 */
	}

	real_rate = core_pll * 2 / div2;                   /* 计算最终分频下的实际Hz。 */

	sys_write32((1 << 8) | (div - 1), CMU_JPEGCLK);    /* 选择PLL并写JPEG分频编码。 */

	//printk("jpeg : set rate %d Hz, real rate %d Hz\n", rate_hz, real_rate);
}
#endif    /* CONFIG_JPEG_HW */

/**
  * @brief  按外设时钟编号分派频率配置。
  * @param  clock_id：[输入] CLOCK_ID_* 编号；SPI0～3 对应普通 SPI 时钟。
  * @param  rate_hz：[输入] 目标频率，单位 Hz，分频计算要求大于0。
  * @retval 固定返回0；包括不支持的编号，不能据此判断配置成功。
  * @note   实际频率由父时钟及分频编码决定；从机线上的 SCK 仍由外部主机提供。
  */
int clk_set_rate(int clock_id,  uint32_t rate_hz)
{
	int ret = 0;                                                     /* 保留原局部状态；末尾未返回此变量。 */
	switch(clock_id) {                                               /* 按时钟编号选择对应硬件配置路径。 */

	case CLOCK_ID_SD0:
	case CLOCK_ID_SD1:
		acts_clk_set_rate_sd(clock_id-CLOCK_ID_SD0, rate_hz);    /* 把SD时钟编号转换为控制器索引。 */
		break;                                                   /* 结束本分支，不落入后续case。 */
	case CLOCK_ID_DMA:
		break;                                                   /* 结束本分支，不落入后续case。 */
	case CLOCK_ID_SPI0:
	case CLOCK_ID_SPI1:
	case CLOCK_ID_SPI2:
	case CLOCK_ID_SPI3:
		acts_clk_set_rate_spi(clock_id, rate_hz);                /* 设置普通SPI源和分频。 */
		break;                                                   /* 结束本分支，不落入后续case。 */
	case CLOCK_ID_SPIMT0:
	case CLOCK_ID_SPIMT1:
		acts_clk_set_rate_spimt(clock_id, rate_hz);              /* 设置独立SPIMT时钟，未使能时为空实现。 */
		break;                                                   /* 结束本分支，不落入后续case。 */

#ifdef CONFIG_DISPLAY_LCDC
	case CLOCK_ID_LCD:
		acts_clk_set_rate_lcd(rate_hz);                          /* 配置LCD时钟。 */
		break;                                                   /* 结束本分支，不落入后续case。 */
#endif

#ifdef CONFIG_DISPLAY_ENGINE
	case CLOCK_ID_DE:
		acts_clk_set_rate_de(rate_hz);                           /* 配置显示引擎时钟。 */
		break;                                                   /* 结束本分支，不落入后续case。 */
#endif

#ifdef CONFIG_VG_LITE
	case CLOCK_ID_GPU:
		acts_clk_set_rate_gpu(rate_hz);                          /* 配置GPU时钟。 */
		break;                                                   /* 结束本分支，不落入后续case。 */
#endif
#ifdef CONFIG_JPEG_HW
	case CLOCK_ID_JPEG:
		acts_clk_set_rate_jpeg(rate_hz);                         /* 配置JPEG时钟。 */
		break;                                                   /* 结束本分支，不落入后续case。 */
#endif

	case CLOCK_ID_I2CMT0:
	case CLOCK_ID_I2CMT1:
	case CLOCK_ID_SPI0CACHE:
	case CLOCK_ID_SPI1CACHE:
	case CLOCK_ID_USB:
	case CLOCK_ID_USB2:
	case CLOCK_ID_SE:
	case CLOCK_ID_LRADC:
	case CLOCK_ID_UART0:
	case CLOCK_ID_UART1:
	case CLOCK_ID_UART2:
	case CLOCK_ID_I2C0:
	case CLOCK_ID_I2C1:
	case CLOCK_ID_DSP:
	case CLOCK_ID_ASRC:
	case CLOCK_ID_DAC:
	case CLOCK_ID_ADC:
	case CLOCK_ID_I2STX:
	case CLOCK_ID_I2SRX:
		printk("clkid=%d not support clk set\n",clock_id);       /* 不支持的设置请求只打印诊断。 */
		ret = -1;                                                /* 记录局部失败，但原实现末尾固定返回0。 */
		break;                                                   /* 结束本分支，不落入后续case。 */

	}

	return 0;                                                        /* 原实现固定返回0，不反映局部ret或是否支持。 */
}

/**
  * @brief  保留外设频率查询接口。
  * @param  clock_id：[输入] 外设时钟编号。
  * @retval 当前固定返回0 Hz，尚未实现实际频率读取。
  */
uint32_t clk_get_rate(int clock_id)
{

	uint32_t rate = 0;                                            /* 查询接口尚未实现，结果保持0 Hz。 */
	switch(clock_id) {                                            /* 按时钟编号选择对应硬件配置路径。 */
	case CLOCK_ID_SD0:
	case CLOCK_ID_SD1:
	case CLOCK_ID_DMA:
	case CLOCK_ID_SPI0:
	case CLOCK_ID_SPI1:
	case CLOCK_ID_SPI2:
	case CLOCK_ID_SPI3:
	case CLOCK_ID_SPI0CACHE:
	case CLOCK_ID_SPI1CACHE:
	case CLOCK_ID_USB:
	case CLOCK_ID_USB2:
	case CLOCK_ID_DE:
	case CLOCK_ID_LCD:
	case CLOCK_ID_SE:
	case CLOCK_ID_LRADC:
	case CLOCK_ID_UART0:
	case CLOCK_ID_UART1:
	case CLOCK_ID_UART2:
	case CLOCK_ID_I2C0:
	case CLOCK_ID_I2C1:
	case CLOCK_ID_DSP:
	case CLOCK_ID_ASRC:
	case CLOCK_ID_DAC:
	case CLOCK_ID_ADC:
	case CLOCK_ID_I2STX:
	case CLOCK_ID_I2SRX:
		printk("clkid=%d not support clk get\n",clock_id);    /* 已列出的编号输出尚不支持查询的诊断。 */
		break;                                                /* 结束本分支，不落入后续case。 */

	}

	return rate;                                                  /* 返回未更新的0 Hz。 */

}


/* 以下旧版CPU调频试验固定禁用，原实现不完整。 */
#if 0

/**
  * @brief  保留已禁用的旧版 CPU 调频试验。
  * @param  mhz：[输入] 旧接口目标MHz。
  * @retval 此禁用实现没有有效返回语句。
  * @note   位于 #if 0 中，不参与当前构建；引用 old/val 等未定义变量，不可直接启用。
  */
uint32_t clk_cpu_set(uint32_t mhz)
{

	unsigned int core_pll, div, tmp, real_rate;             /* 旧试验保存PLL十倍频率、分频编码、临时比值及估计结果。 */
	core_pll = (sys_read32(COREPLL_CTL)&0x3F)*80;           /* 按原试验比例读取PLL编码，乘80形成十倍MHz标度。 */
	tmp = core_pll/mhz;                                     /* 估算十倍分频比，原试验没有检查mhz为0。 */
	if(tmp > 13){                                           /* 原试验分支标称1.5分频，但保留其tmp比较条件，不据此确认硬件结果。 */
		real_rate = core_pll/15;                        /* 原试验计算特殊分频后的频率估值。 */
		div = 14;                                       /* 选择1.5特殊分频编码。 */
	} else if ( (tmp < 28) && (tmp > 22) ) {                /* 原试验分支标称2.5分频，当前不参与构建。 */
		real_rate = core_pll/25;                        /* 原试验计算2.5分频后的频率估值。 */
		div = 15;                                       /* 选择2.5特殊分频编码。 */

	}else if( tmp > 140){
		real_rate = core_pll/14;                        /* 原试验采用普通最大除数14估算结果。 */
		div = 13;                                       /* 最大普通除数为14。 */
	}else{
		div = (tmp/10)-1;                               /* 将十倍分频比转为除数减1的编码，沿用原整数截断。 */
		real_rate = core_pll/10;                        /* 原试验直接取PLL标度除10，未应用本分支div。 */
	}
	tmp = sys_read32(CMU_SYSCLK);                           /* 旧试验读取当前系统时钟，后续却使用未定义old/val，不能直接启用。 */
	sys_write32((old & (~(0x3<<8))) | val , CMU_SYSCLK);    /* 原试验引用未定义old/val，不能断言此写法产生有效调频。 */
	printk("cpu: set rate %d MHz, real rate %d MHz, core pll=%d MHZ\n", mhz, real_rate, core_pll);

}
#endif
/* AHB接口返回旧字段编码；不是实际频率。 */

/**
  * @brief  修改 AHB 分频字段并返回修改前的字段编码。
  * @param  div：[输入] 请求值；1 写编码1，4 写编码3，其他值写编码0。
  * @retval 原 CMU_SYSCLK 位9:8的原始编码0～3，并非实际分频数。
  * @note   本函数没有关中断保护；调用者须协调同一寄存器的并发写入。
  */
uint32_t clk_ahb_set(uint32_t div)
{
	uint32_t val, old;                                      /* 保存新AHB字段与整个旧寄存器。 */
	old = sys_read32(CMU_SYSCLK);                           /* 读取旧配置以保留非AHB字段。 */
	if(div == 1)                                            /* 请求1时使用现有代码规定的编码1。 */
		val = 1<<8;                                     /* AHB字段写入编码1。 */
	else if (div == 4)                                      /* 请求4时使用现有代码规定的编码3。 */
		val = 3<<8;                                     /* AHB字段写入编码3。 */
	else
		val = 0;                                        /* 其他请求对应编码0。 */
	div = (old >>8) & 0x03;                                 /* 提取旧AHB原始字段作为返回值。 */
	sys_write32((old & (~(0x3<<8))) | val , CMU_SYSCLK);    /* 仅替换AHB字段，其余时钟字段保持原值。 */
	return div;                                             /* 返回分频编码或先前保存的AHB字段。 */
}




