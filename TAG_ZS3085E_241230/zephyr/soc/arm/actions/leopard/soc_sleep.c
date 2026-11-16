/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* SoC 休眠、唤醒回调、寄存器保存恢复及低速时钟计时补偿。 */


/* 内核、当前板级配置、Cortex-M 寄存器、时钟定时器、链接段和缓存/MPU 接口为休眠保存恢复提供实际定义。 */
/* 当前 ATS 目标启用保留 SRAM 中的休眠代码和 CPU 掉电门控，CONFIG_SOC_NO_PSRAM=y；PSRAM、完整性检查、GPIO 测试及额外打印等分支仍保留说明，不表示本目标启用。 */
#include <zephyr.h>
#include <board.h>
#include <arch/arm/aarch32/cortex_m/cmsis.h>
#include <string.h>
#include <drivers/timer/system_timer.h>
#include <linker/linker-defs.h>
#include <partition/partition.h>
#include "spicache.h"
#include "sys_wakelock.h"
#include <act_arm_mpu.h>

/* HOSC 就绪状态位在寄存器 bit28。 */
#define HOSC_CTL_READY_SHIFT        28
/* 根据位号构造 HOSC 就绪掩码。 */
#define HOSC_CTL_READY_MASK         (0x1 << HOSC_CTL_READY_SHIFT)

//#define CONFIG_SLEEP_PRINTK_DEBUG

//#define CONFIG_SLEEP_STAGE_DEBUG

//#define CONFIG_SLEEP_DISABLE_BT

//#define CONFIG_JTAG_DEBUG

//#define CONFIG_SLEEP_WD_DIS_DEBUG

//#define CONFIG_SLEEP_DUMP_INFO

/* 休眠策略模式；调试开关均保留为注释，不能当作当前启用配置。 */
#define SLEEP_MODE_NORMAL  		0    /* 普通休眠。 */
#define SLEEP_MODE_BT_OFF 		1    /* 关闭蓝牙供电的休眠。 */
#define SLEEP_MODE_LOWPOWER 	2            /* 关闭蓝牙，只保留 ONOFF 唤醒的低功耗模式。 */
#define SLEEP_MODE_FORCE_SLEEP 	3            /* 用于功耗测试的强制休眠。 */

#define FORCE_SLEEP_WAKE_CNT 	20    /* 原设计按每次 200 ms 的 IIC1MT 唤醒累计，达到 CNT 次后唤醒系统；实际周期取决于外部采样配置。 */

/* 完整性检查区域的 PSRAM 基址。 */
#define PSRAM_START_ADDR                (0x38000000)
/* 可选完整性检查范围为 8 MiB。 */
#define MEMORY_CHECK_INTEGRITY_SIZE     (8 * 1024 * 1024)

struct sleep_wk_data {
	uint16_t wksrc;           /* 逻辑唤醒源编号，用于软件掩码和回调索引。 */
	uint16_t irq_en_bit;      /* A 版通过 NVIC 中断号配置唤醒。 */
	uint16_t wksrc_en_bit;    /* B 版额外配置的硬件唤醒位编号。 */
	const char *wksrc_msg;    /* 调试输出的源名称，字符串保留原文。 */
};

/* 每行依次为逻辑源、NVIC IRQ 编号、B 版额外唤醒位及调试名称；SPI0MT/SPI1MT 是这里的存储控制器源，不等同于本次应用 SPI2。 */
static const struct sleep_wk_data wk_msg[] = {    /* 逻辑源到 NVIC 中断号、B 版唤醒位及名称的固定映射表。 */
	{SLEEP_WK_SRC_PMU, 		IRQ_ID_PMU,  	WK_ID_PMU,    "PMU" },
	{SLEEP_WK_SRC_RTC,      IRQ_ID_RTC,     WK_ID_RTC,    "RTC" },
	{SLEEP_WK_SRC_BT, 		IRQ_ID_BT,  	WK_ID_BT,     "BT" },
	{SLEEP_WK_SRC_GPIO, 	IRQ_ID_GPIO,    WK_ID_GPIO,   "GPIO" },
	{SLEEP_WK_SRC_T0,		IRQ_ID_TIMER0, 	WK_ID_TIMER0, "T0" },
	{SLEEP_WK_SRC_T1,		IRQ_ID_TIMER1,	WK_ID_TIMER1, "T1" },
	{SLEEP_WK_SRC_T2,		IRQ_ID_TIMER2,	WK_ID_TIMER2, "T2" },
	{SLEEP_WK_SRC_T3,		IRQ_ID_TIMER3,	WK_ID_TIMER3, "T3" },
	{SLEEP_WK_SRC_T4,		IRQ_ID_TIMER4,	WK_ID_TIMER4, "T4" },
	{SLEEP_WK_SRC_T5,		IRQ_ID_TIMER5,	WK_ID_TIMER5, "T5" },
	{SLEEP_WK_SRC_TWS,		IRQ_ID_TWS, 	WK_ID_TWS0,    "TWS" },
	{SLEEP_WK_SRC_SPI0MT,	IRQ_ID_SPI0MT,	WK_ID_SPI0MT, "SPI0MT" },
	{SLEEP_WK_SRC_SPI1MT,	IRQ_ID_SPI1MT,	WK_ID_SPI1MT, "SPI1MT" },
	{SLEEP_WK_SRC_IIC0MT,	IRQ_ID_IIC0MT,	WK_ID_IIC0MT, "IIC0MT" },
	{SLEEP_WK_SRC_IIC1MT,	IRQ_ID_IIC1MT,	WK_ID_IIC1MT, "IIC1MT" },
};

/* 唤醒源总数取数组项数，不依赖手写常量。 */
#define SLEEP_WKSRC_NUM ARRAY_SIZE(wk_msg)

struct sleep_wk_cb {
	sleep_wk_callback_t wk_cb;    /* 完整系统恢复后待执行的唤醒回调。 */
	enum S_WK_SRC_TYPE src;       /* 传给该回调的唤醒源编号。 */
};

/* 休眠上下文，保存策略、累计补偿、唤醒结果和延后执行的回调。 */
struct sleep_context_t {
	uint32_t sleep_mode;                     /* 控制休眠策略模式。 */
	uint32_t g_sleep_cycle;                  /* 累计休眠期间 HOSC 等效周期，供系统周期计数补偿。 */
	enum S_WK_SRC_TYPE g_sleep_wksrc_src;    /* 最近记录的唤醒源，0 也对应 PMU。 */

	struct sleep_wk_cb g_syste_cb[SLEEP_WKSRC_NUM];    /* 受限环境无法执行、延后到完整系统恢复的回调表。 */
	uint32_t g_num_cb;                                 /* 待执行回调数，不超过表容量。 */

#ifdef CONFIG_SLEEP_MEMORY_CHECK_INTEGRITY
	uint32_t check_start_addr;    /* 完整性检查区首地址。 */
	uint32_t check_len;           /* 完整性检查长度，单位字节。 */
	uint32_t check_sum;           /* 保留的校验字段，当前保存休眠前值实际使用 suspend_sum。 */
	uint32_t resume_sum;          /* 恢复后重新计算的区域累加和。 */
#endif
};

static struct sleep_context_t sleep_context_obj;    /* 唯一休眠上下文实体，静态存储期随系统存在。 */
/* 本模块休眠状态的访问别名。 */
#define current_sleep (&sleep_context_obj)

static struct sleep_wk_fun_data __act_s2_sleep_data *g_wk_fun[SLEEP_WKSRC_NUM] ;    /* 按唤醒源索引的回调链表头，节点由调用者持有。 */

static volatile uint16_t __act_s2_sleep_data g_sleep_wksrc_en, g_sleep_wksrc_en_bak;    /* 当前允许的源掩码及特殊模式进入前的备份；volatile 不提供原子性。 */
static int64_t __act_s2_sleep_data g_sleep_update_time, g_sleep_ms;                     /* 进入休眠前系统时间与累计休眠毫秒补偿。 */
static uint32_t __act_s2_sleep_data g_sleep_st, check_cnt, g_sleep_t2cnt;               /* RC32K 休眠起点、PLL 就绪轮询次数及 T2 时钟同步参考值。 */

/* 汇编上下文备份容量，以 32 位项计。 */
#define SOC_DEEP_SLEEP_BACKUP_NUM (16)
/* CPU 上下文保存区：原汇编约定保存 R0～R14 中所需寄存器（不含 R12）及 MSP/PSP，R15 不在此作为普通寄存器保存。 */
uint32_t __act_s2_sleep_data soc_sleep_backup[SOC_DEEP_SLEEP_BACKUP_NUM];
static uint32_t __act_s2_sleep_data pwrgat_irq_en0, pwrgat_irq_en1;    /* 掉电门控后需要重新使能的两组 NVIC 中断位。 */
static uint32_t __act_s2_sleep_data pwrgat_wksrc_en;                   /* B 版额外硬件唤醒掩码的备份。 */
static uint32_t __act_s2_sleep_data wk_pd0, wk_pd1;                    /* 唤醒瞬间两组 NVIC 挂起位快照。 */

/**
  * @brief  保存 CPU 上下文并进入掉电门控流程。
  * @param  无。
  * @retval 唤醒恢复后返回。
  * @note   实现在架构汇编，使用 soc_sleep_backup 保留上下文。
  */
void __cpu_suspend();

/**
  * @brief  CPU 掉电唤醒后的架构恢复入口。
  * @param  无。
  * @retval 按汇编约定恢复上下文。
  * @note   其地址写入 RTC_REMAIN4，由底层启动路径接续；不是普通应用入口。
  */
void __cpu_resume();

/**
  * @brief  恢复唤醒后所需浮点架构状态。
  * @param  无。
  * @retval 无。
  * @note   具体架构实现在其他文件。
  */
void arm_floating_point_init(void);

/* 需要在系统休眠前保存、醒来后恢复的寄存器地址表。 */
static const uint32_t backup_regs_addr[] = {
	CMU_S1CLKCTL,
	PMUADC_CTL,
	CMU_MEMCLKEN0,
	CMU_MEMCLKEN1,
	NVIC_ISER0,
	NVIC_ISER1,
	PWRGATE_DIG,
	CMU_SPI0CLK,
};

static uint32_t __act_s2_sleep_data s2_reg_backups[ARRAY_SIZE(backup_regs_addr)];    /* 按地址表逐项存放公共寄存器原值。 */

/* GPIO36～48、0～3、6、7 由专用休眠流程处理。 */
/* 此表列出休眠期间不用、可暂置高阻的引脚寄存器。 */
static const uint32_t backup_regs_gpio[] = {


#if CONFIG_SPINAND_3 || CONFIG_SPI_FLASH_2
	/* SPI NAND/第二闪存配置使用的引脚。 */
	GPION_CTL(8),
	GPION_CTL(9),
	GPION_CTL(10),
	GPION_CTL(11),
	GPION_CTL(12),
	GPION_CTL(13),
#endif

#ifndef CONFIG_SENSOR_MANAGER
	/* 加速度传感器接口。 */
	GPION_CTL(54),    /* 传感器中断。 */
	GPION_CTL(55),    /* IICMT1 时钟。 */
	GPION_CTL(56),    /* IICMT1 数据。 */
	/* 心率传感器接口。 */
	GPION_CTL(18),    /* IICMT0 时钟。 */
	GPION_CTL(19),    /* IICMT0 数据。 */
	GPION_CTL(74),    /* 传感器中断。 */
#endif

#if 0
	/* 禁用的传感器引脚配置示例。 */
	//GPION_CTL(20), /*未使用，默认高阻*/
	GPION_CTL(21),    /* 传感器中断，休眠期间仍使用。 */
	//GPION_CTL(24), /* HR_PWR_EN，休眠期间使用*/
	GPION_CTL(25),    /* 1.8 V 电源使能，休眠期间仍使用。 */
	GPION_CTL(33),    /* GPS 唤醒主机信号，休眠期间仍使用。 */
#endif

#ifdef SLEEP_GPIO_REG_SET_HIGHZ
	SLEEP_GPIO_REG_SET_HIGHZ
#endif

#if IS_ENABLED(CONFIG_KNOB_ENCODER)
	SLEEP_KNOB_REG_SET_HIGHZ
#endif
};

#ifdef SLEEP_AOD_GPIO_REG_UNSET_HIGHZ
static const uint32_t backup_regs_aod_gpio[] = {    /* AOD 关闭时才可置高阻的额外引脚地址表。 */
	SLEEP_AOD_GPIO_REG_UNSET_HIGHZ
};
#endif

/**
  * @brief  判断此配置和芯片版本是否允许 CPU 掉电门控。
  * @param  无。
  * @retval CONFIG_CPU_PWRGAT 启用且 soc_dvfs_opt 非零时为 true，否则为 false。
  * @note   不是当前正在休眠的状态查询。
  */
__sleepfunc bool sleep_policy_is_pwrgat(void)
{
#ifdef CONFIG_CPU_PWRGAT
	if (soc_dvfs_opt())      /* 芯片版本支持相应 DVFS 方案时才采用 CPU 掉电。 */
		return true;     /* 允许使用 CPU 掉电路径。 */
	else
		return false;    /* 该芯片版本保持普通 WFI 路径。 */
#else
	return false;            /* 未编译 CPU 掉电能力时始终禁用该策略。 */
#endif
}

static uint32_t __act_s2_sleep_data s2_gpio_reg_backups[ARRAY_SIZE(backup_regs_gpio)];    /* 高阻 GPIO 对应的原配置备份，每项对应地址表一项。 */

#if defined(CONFIG_BOARD_NANDBOOT) || !defined(CONFIG_SPI_FLASH_ACTS)

/**
  * @brief  控制 NOR 闪存深度掉电。
  * @param  is_powerdown：[输入]非零请求掉电，0 请求唤醒。
  * @retval 无。
  * @note   NAND 启动或未启用 SPI_FLASH_ACTS 时本文件提供空实现；其余配置使用外部实现。
  */
void  sys_norflash_power_ctrl(uint32_t is_powerdown)
{
}
#else

/**
  * @brief  控制 NOR 闪存深度掉电。
  * @param  is_powerdown：[输入]非零请求掉电，0 请求唤醒。
  * @retval 无。
  * @note   NAND 启动或未启用 SPI_FLASH_ACTS 时本文件提供空实现；其余配置使用外部实现。
  */
extern void sys_norflash_power_ctrl(uint32_t is_powerdown);
#endif

/* 定点换算系数为 (1000 << 16) / soc_rc32K_freq()，低 16 位表示小数部分。 */
static uint32_t __act_s2_sleep_data g_cyc2ms_mul;  //

/**
  * @brief  以预计算的定点系数把 RC32K 周期换算为毫秒。
  * @param  cycle：[输入]RC32K 源的计数差，单位周期。
  * @retval 换算后的毫秒整数，低 16 位小数截断。
  * @note   系数由 soc_sleep_init 计算，实际频率取 soc_rc32K_freq，不强行假定精确 32768 Hz。
  */
static __sleepfunc uint32_t rc32k_cyc_to_ms(uint32_t cycle)
{
	//return (uint32_t)((uint64_t)cycle * 1000 / soc_rc32K_freq());
	uint64_t tmp = g_cyc2ms_mul;    /* 先扩展为 64 位，避免后续乘法在 32 位中溢出。 */
	tmp = tmp * cycle;              /* 把周期数乘以 Q16 定点毫秒系数。 */
	return (tmp >> 16);             /* 去掉低 16 位小数，返回整数毫秒。 */
}

/**
  * @brief  通过 UART0 轮询发送一个字符，换行前补回车。
  * @param  c：[输入]待发字符值。
  * @param  ctx：[输入]格式化输出回调上下文，未使用，可为空。
  * @retval 固定返回 0。
  * @note   运行在可用 SRAM/休眠代码段；发送等待无超时。
  */
__sleepfunc int uart_out_ch(int c, void *ctx)
{
	ARG_UNUSED(ctx);                                              /* UART 输出不使用格式化器传来的上下文。 */

	/* UART 输出遇到换行时先补一个回车，保留终端换行格式。 */
	if(c == '\n'){                                                /* 换行前补回车，便于串口终端正确显示。 */
		/* 等待发送 FIFO 可写，当前实现没有超时。 */
		while (sys_read32(UART0_REG_BASE + 12) &  BIT(6));    /* UART 状态 bit6 清零后才继续，当前轮询无超时。 */
		sys_write32('\r', UART0_REG_BASE + 8);                /* 写 UART 数据寄存器发送回车。 */
	}

	/* 等待发送 FIFO 可写后发送本字符，没有超时。 */
	while (sys_read32(UART0_REG_BASE + 12) &  BIT(6));            /* 等待 UART FIFO 可写，时钟异常会导致永久等待。 */
	sys_write32(c, UART0_REG_BASE+8);                             /* 将原字符写入 UART 数据寄存器。 */

	return 0;                                                     /* 字符输出回调固定报告 0。 */
}

/**
  * @brief  等待 UART0 发送路径完全空闲。
  * @param  无。
  * @retval 无。
  * @note   轮询状态位，没有超时，不适合设备时钟未恢复时调用。
  */
__sleepfunc void uart_out_flush(void)
{
	/* 等待 UART 发送完成，没有超时。 */
	while (sys_read32(UART0_REG_BASE + 12) &  BIT(21));    /* 轮询 bit21 直至发送不忙，没有超时。 */
}

/**
  * @brief  以可变参数形式调用休眠期打印接口。
  * @param  fmt：[输入]有效零结尾格式串，不可为空。
  * @param  ...：[输入]与格式串匹配的参数。
  * @retval 固定返回 0，不返回字符数。
  * @note   是否实际输出取决于 sl_vprintk 的调试与 panic 条件。
  */
__sleepfunc int sl_printk(const char *fmt, ...)
{
	va_list ap;             /* 保存格式串对应的可变参数读取状态。 */

	va_start(ap, fmt);      /* 从 fmt 后的参数建立参数列表。 */
	sl_vprintk(fmt, ap);    /* 把格式串和参数列表交给休眠期打印实现。 */
	va_end(ap);             /* 结束参数列表访问。 */

	return 0;               /* 此包装不返回打印字符数，固定返回 0。 */
}

/**
  * @brief  恢复 UART0 复位、时钟、控制和波特率。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
void uart_resume(void);

/**
  * @brief  恢复 UART TX/RX 引脚配置。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void pwrgat_gpio_uart_restore_registers(void);
#ifndef CONFIG_SLEEP_PRINTK_DEBUG

/**
  * @brief  将调试引脚复用切给蓝牙或主 CPU UART。
  * @param  bt_uart：[输入]true 选择蓝牙 UART，false 选择主 CPU UART。
  * @retval 无。
  * @note   调试常开或静默配置下由同名空操作宏替代；保留驱动强度和上下拉。
  */
static void soc_enter_sleep_switch_uart(bool bt_uart);
#endif

/**
  * @brief  查询是否进入 panic 输出场景。
  * @param  无。
  * @retval 非零表示 panic 路径，0 表示普通场景。
  * @note   此处仅声明，休眠打印据此决定是否恢复 UART。
  */
extern int check_panic_exe(void);

/**
  * @brief  在允许调试或 panic 输出时使用 BROM 格式化器打印。
  * @param  fmt：[输入]有效零结尾格式串，不可为空。
  * @param  ap：[输入/输出]已由 va_start 初始化并与格式串匹配的参数列表。
  * @retval 固定返回 0，包括未输出的情况。
  * @note   普通休眠配置可完全跳过打印；panic 路径按需恢复 HOSC 和 UART，输出等待无超时。
  */
__sleepfunc int sl_vprintk(const char *fmt, va_list ap)
{
#ifdef CONFIG_SLEEP_PRINTK_DEBUG
	uint32_t print_enable = 1;                                          /* 调试打印配置下默认允许输出。 */
#else
	uint32_t print_enable = 0;                                          /* 普通配置下默认静默。 */

#ifdef CONFIG_ACTIONS_PRINTK_DMA
	if (check_panic_exe()) {                                            /* 只有 panic 场景才强制恢复打印资源。 */
		if (!sys_test_bit(CMU_S1CLKCTL, 1)) {                       /* HOSC 未开启时先恢复时钟与 UART，避免访问停钟外设。 */
			/* 为休眠期间 UART 输出重新使能 HOSC。 */
			sys_set_bit(CMU_S1CLKCTL, 1);                       /* 开启 S1 状态的 HOSC。 */
			sys_set_bit(CMU_S3CLKCTL, 1);                       /* 开启 S3 状态的 HOSC。 */
			soc_udelay(1000);                                   /* 等待 HOSC 稳定 1000 微秒。 */
			
			/* 恢复 UART 引脚复用和控制寄存器。 */
			pwrgat_gpio_uart_restore_registers();               /* 还原主 CPU UART 的引脚配置。 */
			uart_resume();                                      /* 恢复 UART 时钟、控制和波特率。 */
			soc_enter_sleep_switch_uart(false);                 /* 将 UART 引脚复用切回主 CPU。 */
		}
		print_enable = 1;                                           /* panic 场景允许输出诊断。 */
	}
#endif
#endif
	if (print_enable) {                                                 /* 只有输出资源已经允许时才进入格式化和发送。 */
		pbrom_libc_api->p_cbvprintf(uart_out_ch, NULL, fmt, ap);    /* BROM 格式化器逐字符调用 uart_out_ch，字符串和参数不被复制持有。 */
		uart_out_flush();                                           /* 返回前等待 UART 发完，当前等待没有超时。 */
	}

	return 0;                                                           /* 包括静默分支在内均固定返回 0。 */
}

#ifdef CONFIG_SLEEP_STAGE_DEBUG

/**
  * @brief  按调试配置将休眠阶段写入 RTC 保留寄存器。
  * @param  index：[输入]阶段编号；启用时仅保留低 7 位并置标志位。
  * @retval 无。
  * @note   CONFIG_SLEEP_STAGE_DEBUG 分支保存阶段，另一分支为空函数。
  */
__sleepfunc void sleep_stage(unsigned int index)
{
	 unsigned int val,stage;                           /* 暂存阶段编码及 RTC 寄存器原值。 */
	 stage = (index&0x7f)+0x80;                        /* 阶段号截为低 7 位并加有效标志位。 */
	 val = sys_read32(RTC_REMAIN2)& (~(0xff<<8));      /* 保留 RTC_REMAIN2 中 bit8～15 以外的字段。 */
	 sys_write32( val | (stage << 8), RTC_REMAIN2);    /* 把阶段编码写入 bit8～15，供复位后排查。 */
}

#else

/**
  * @brief  按调试配置将休眠阶段写入 RTC 保留寄存器。
  * @param  index：[输入]阶段编号；启用时仅保留低 7 位并置标志位。
  * @retval 无。
  * @note   CONFIG_SLEEP_STAGE_DEBUG 分支保存阶段，另一分支为空函数。
  */
void sleep_stage(int index)
{

}
#endif

/**
  * @brief  按调试配置输出一次休眠耗时。
  * @param  st：[输入]开始时的 RC32K 计数。
  * @param  end：[输入]结束时的 RC32K 计数。
  * @retval 无。
  * @note   仅 CONFIG_SLEEP_DUMP_INFO 时输出，差值以无符号回绕方式计算。
  */
void dump_stage_timer(uint32_t st, uint32_t end)
{
#ifdef CONFIG_SLEEP_DUMP_INFO
	printk("sleep use %d ms, check_cnt=%d\n", rc32k_cyc_to_ms(end - st), check_cnt);    /* 打印按 RC32K 频率换算的休眠毫秒数和 PLL 轮询次数。 */
#endif
}

#ifdef CONFIG_SLEEP_MEMORY_CHECK_INTEGRITY
/* 检查 8 MiB PSRAM 时，校验值自身必须放在仍可用的 SRAM 中。 */
static uint32_t __act_s2_sleep_data suspend_sum;    /* 休眠前校验值保留在低功耗仍可用的 SRAM 区。 */

/**
  * @brief  将区域中完整的 32 位字逐项相加用于休眠完整性检查。
  * @param  buf：[输入]按 uint32_t 对齐的可读区域，len 大于等于 4 时不可为空。
  * @param  len：[输入]检查长度，单位字节，须非负；末尾不足 4 字节不参与。
  * @retval 按 32 位回绕的累加和。
  * @note   仅完整性检查配置，不是 CRC 或密码学完整性校验。
  */
static uint32_t check_sum(uint32_t *buf, int len)
{
	int i;                             /* 遍历被检查区域中完整 32 位字的索引。 */
	uint32_t chk = 0;                  /* 累加器从 0 开始。 */

	for (i = 0; i < (len / 4); i++)    /* 只计算 len/4 个完整字，余下不足 4 字节不参与。 */
		chk += buf[i];             /* 按无符号 32 位规则累计当前字。 */

	return chk;                        /* 返回回绕累加和，供前后比较。 */
}

/**
  * @brief  计算并保存休眠前的内存累加和。
  * @param  无。
  * @retval 无。
  * @note   配置的内存区必须可读；结果保留在 SRAM，不能依赖被检查的 PSRAM。
  */
static void suspend_check(void)
{
	uint32_t chk;         /* 暂存配置区域的休眠前累加和。 */
	/* 对配置首址和字节长度执行完整字累加。 */
	chk = check_sum((uint32_t *)current_sleep->check_start_addr,
					current_sleep->check_len);
	suspend_sum = chk;    /* 把结果写入可保留的 SRAM 变量。 */

	//printk("suspend checksum:0x%x\n", chk);
}

/**
  * @brief  重新计算唤醒后的内存累加和。
  * @param  无。
  * @retval 无。
  * @note   须等 PSRAM 供电及缓存访问恢复后调用。
  */
static void resume_check(void)
{
	uint32_t chk;                       /* 暂存恢复后计算的累加和。 */
	/* 读取同一配置区域，计算恢复后校验结果。 */
	chk = check_sum((uint32_t *)current_sleep->check_start_addr,
					current_sleep->check_len);
	current_sleep->resume_sum = chk;    /* 保存恢复后结果供诊断比较。 */
}

/**
  * @brief  打印休眠前后校验结果并报告不一致。
  * @param  无。
  * @retval 无。
  * @note   只打印错误，不终止或修复内存；原停机调试语句仍未启用。
  */
static void wakeup_check_dump(void)
{
	//uint32_t chk;

	//chk = check_sum((uint32_t *)current_sleep->check_start_addr,
					//current_sleep->check_len);
	printk("checksum resume:0x%x suspend:0x%x,len=0x%x\n", current_sleep->resume_sum, suspend_sum, current_sleep->check_len);    /* 打印恢复前后校验和及检查长度。 */

	if (suspend_sum != current_sleep->resume_sum){                                                                               /* 结果不一致只报告，原代码未启用停机动作。 */
		printk("\n----- error memory integrity check -----------\n");                                                        /* 输出内存完整性检查错误提示。 */
		//while(1);
	}
}
#endif

/**
  * @brief  将调用者的唤醒处理节点插入对应源的链表头。
  * @param  wk_src：[输入]有效唤醒源编号，0 至 SLEEP_WKSRC_NUM-1。
  * @param  fn_data：[输入/输出]持久可写节点，不可为空；wk_cb 必须有效，wk_prep 可为空，next 被修改。
  * @retval 0 表示已插入，-1 表示 fn_data 为空。
  * @note   不分配或复制节点，不校验源编号，不加锁、不去重；节点及回调在注册生效期间必须有效，避免重复注册形成环。
  */
int sleep_register_wk_callback(enum S_WK_SRC_TYPE wk_src, struct sleep_wk_fun_data *fn_data)
{
	/* 以链表头插入注册回调；对象由调用者持有，以下不分配内存。 */
	if (fn_data == NULL)                 /* 只检查节点指针，源编号和回调指针依赖调用方保证有效。 */
		return -1;                   /* 拒绝空节点，不改变原链表。 */

	fn_data->next = g_wk_fun[wk_src];    /* 把新节点接到该源原链表之前。 */
	g_wk_fun[wk_src] = fn_data;          /* 发布新链表头；没有加锁，须由上层串行注册。 */

	return 0;                            /* 节点已插入，返回成功。 */
}

/**
  * @brief  允许指定休眠唤醒源。
  * @param  src：[输入]有效唤醒源枚举值，作为 16 位掩码的位索引。
  * @retval 无。
  * @note   只修改软件掩码，实际硬件配置在休眠准备阶段应用；调用者负责并发串行化。
  */
void sys_s3_wksrc_set(enum S_WK_SRC_TYPE src)
{
	g_sleep_wksrc_en |= (1 << src);    /* 将指定源在软件允许掩码中置位。 */
}

/**
  * @brief  禁止指定休眠唤醒源。
  * @param  src：[输入]有效唤醒源枚举值，作为 16 位掩码的位索引。
  * @retval 无。
  * @note   只修改软件掩码，实际硬件配置在休眠准备阶段应用；调用者负责并发串行化。
  */
void sys_s3_wksrc_clr(enum S_WK_SRC_TYPE src)
{
	g_sleep_wksrc_en &= ~(1 << src);    /* 清除指定源的软件允许位。 */
}

/**
  * @brief  取得已记录的最近唤醒源。
  * @param  无。
  * @retval sleep_context 中保存的唤醒源枚举。
  * @note   无匹配时实现写 0，与 PMU 枚举值相同，不能据此区分“无来源”和 PMU。
  */
enum S_WK_SRC_TYPE sys_s3_wksrc_get(void)
{
	return current_sleep->g_sleep_wksrc_src;    /* 读取已记录的源，不重新扫描硬件。 */
}

/**
  * @brief  将初始唤醒原因记录设为 TIMER1。
  * @param  无。
  * @retval 无。
  * @note   仅设置结果字段，不使能 TIMER1 唤醒。
  */
void sys_s3_wksrc_init(void)
{
	current_sleep->g_sleep_wksrc_src = SLEEP_WK_SRC_T1;    /* 初始化来源字段为 TIMER1，并不使能该源。 */
}

/**
  * @brief  从已保存的 NVIC 挂起位中寻找首个已启用唤醒源。
  * @param  无。
  * @retval 首个匹配源；无匹配时返回 0。
  * @note   按 wk_msg 表顺序选择，多个同时发生时只保存第一个；0 同时是 PMU 值。
  */
static enum S_WK_SRC_TYPE sys_sleep_check_wksrc(void)
{
	int i;                                                         /* 用于按表顺序扫描唤醒源。 */
	uint32_t wkbit;                                                /* 保存当前源对应的 NVIC 位号。 */

	current_sleep->g_sleep_wksrc_src = 0;                          /* 先将来源字段复位为 0；它与 PMU 枚举值相同。 */

	//printk("WK NVIC_ISPR0=0x%x\n", wk_pd0);
	//printk("WK NVIC_ISPR1=0x%x\n", wk_pd1);

	for (i = 0; i < SLEEP_WKSRC_NUM; i++) {                        /* 扫描全部已知源，找到第一个已启用且挂起的源后停止。 */
		if ((1 << wk_msg[i].wksrc) & g_sleep_wksrc_en) {       /* 未允许的源即使挂起也不作为此次记录。 */
			wkbit = wk_msg[i].irq_en_bit;                  /* 从映射表取得 NVIC 中断号。 */
			if (wkbit >= 32) {                             /* 中断号大于等于 32 时查询第二组挂起寄存器。 */
				wkbit -= 32;                           /* 将全局中断号换成第二组内的位索引。 */
				if (wk_pd1 & (1 << wkbit))             /* 第二组中断挂起即认定匹配。 */
					break;                         /* 结束扫描，保留首个匹配项。 */
			} else {
				if (wk_pd0 & (1 << wkbit))             /* 低 32 条中断查询第一组挂起位。 */
					break;                         /* 结束扫描，保留首个匹配项。 */
			}
		}
	}

	if (i != SLEEP_WKSRC_NUM) {                                    /* 索引未到数组末尾说明找到了有效来源。 */
		current_sleep->g_sleep_wksrc_src = wk_msg[i].wksrc;    /* 记录匹配源的逻辑编号。 */
		sl_dbg("wksrc=%s\n", wk_msg[i].wksrc_msg);             /* 打印选中来源名称。 */
	} else {
		sl_dbg("no wksrc\n");                                  /* 没有匹配到已启用的挂起源。 */
		current_sleep->g_sleep_wksrc_src = 0;                  /* 沿用 0 作为无匹配结果，不能与 PMU 区分。 */
	}

	return current_sleep->g_sleep_wksrc_src;                       /* 返回保存的结果。 */
}

/**
  * @brief  按当前休眠模式临时限制唤醒源。
  * @param  无。
  * @retval 无。
  * @note   非普通模式先备份原掩码，退出由 sys_exit_wk_by_sleep_mode 还原。
  */
static void sys_set_wksrc_by_sleep_mode(void)
{
	if (current_sleep->sleep_mode != SLEEP_MODE_NORMAL) {                                       /* 非普通模式将临时替换唤醒源集合。 */
		g_sleep_wksrc_en_bak = g_sleep_wksrc_en;                                            /* 保存正常源集合，退出特殊模式时恢复。 */
	}

	switch (current_sleep->sleep_mode) {                                                        /* 按休眠策略选择允许的唤醒范围。 */
		case SLEEP_MODE_BT_OFF:                                                             /* 关闭蓝牙模式仍允许 PMU 和传感器 IICMT1。 */
			g_sleep_wksrc_en = (1 << SLEEP_WK_SRC_PMU) | (1 << SLEEP_WK_SRC_IIC1MT);    /* 仅保留 PMU 与 IICMT1 软件允许位。 */
			break;                                                                      /* 完成当前模式设置，避免进入后续 case。 */
		case SLEEP_MODE_LOWPOWER:                                                           /* 最低功耗模式只允许 PMU。 */
			g_sleep_wksrc_en = (1 << SLEEP_WK_SRC_PMU);                                 /* 仅保留按键等 PMU 唤醒。 */
			break;                                                                      /* 完成最低功耗模式设置。 */
		case SLEEP_MODE_FORCE_SLEEP:                                                        /* 功耗测试模式依赖 IICMT1 周期唤醒计数。 */
			g_sleep_wksrc_en = (1 << SLEEP_WK_SRC_IIC1MT);                              /* 仅保留 IICMT1 以控制测试次数。 */
			break;                                                                      /* 完成强制测试模式设置。 */
		default:
			break;                                                                      /* 普通或其他模式不改变原集合。 */
	}
}

/**
  * @brief  取得当前策略是否保留蓝牙供电。
  * @param  无。
  * @retval BT_OFF 和 LOWPOWER 返回 0，其余返回 1。
  * @note   供休眠准备调用；与参数 force_bt_pg 的原命名不应混淆。
  */
static int sys_get_bt_pwr_by_sleep_mode(void)
{
	int bt_pwr = 1;                         /* 默认保留蓝牙供电。 */

	switch (current_sleep->sleep_mode) {    /* 根据当前策略决定蓝牙供电。 */
		case SLEEP_MODE_BT_OFF:         /* 关闭蓝牙模式要求下电。 */
		case SLEEP_MODE_LOWPOWER:       /* 最低功耗模式同样要求蓝牙下电。 */
			bt_pwr = 0;             /* 输出 0 表示不保留蓝牙。 */
			break;                  /* 结束供电策略选择。 */
		default:
			break;                  /* 其余模式保留默认供电策略。 */
	}

	return bt_pwr;                          /* 交付给 soc_cmu_sleep_prepare 作为其原始参数。 */
}

/**
  * @brief  在强制休眠测试模式中按唤醒次数决定恢复系统。
  * @param  rc：[输入]现有回调汇总结果。
  * @retval 普通模式保留 rc；强制模式未达次数返回 SLEEP_AGAIN，达到阈值返回 RUN_SYSTEM。
  * @note   静态计数仅在阈值达到时清零，改变模式本身不在此清零。
  */
static enum WK_CB_RC sys_update_wk_by_sleep_mode(enum WK_CB_RC rc)
{
	static uint32_t s_sleep_cnt = 0;                              /* 跨调用保存强制测试的局部唤醒计数。 */

#if FORCE_SLEEP_WAKE_CNT > 0
	if (current_sleep->sleep_mode == SLEEP_MODE_FORCE_SLEEP) {    /* 只在强制测试模式覆盖原回调结果。 */
		s_sleep_cnt ++;                                       /* 累计一次 IIC1MT 唤醒。 */
		rc = WK_CB_SLEEP_AGAIN;                               /* 阈值未到以前要求继续休眠。 */
		if (s_sleep_cnt >= FORCE_SLEEP_WAKE_CNT) {            /* 达到预设次数后退出功耗测试。 */
			s_sleep_cnt = 0;                              /* 下一轮测试从零重新计数。 */
			rc = WK_CB_RUN_SYSTEM;                        /* 要求完整恢复系统。 */
		}
	}
#endif

	return rc;                                                    /* 返回保留或被测试模式覆盖的结果。 */
}

/**
  * @brief  退出特殊休眠模式并恢复原唤醒配置和唤醒锁管理。
  * @param  无。
  * @retval 无。
  * @note   强制模式改回 NORMAL，其余特殊模式值保留。
  */
static void sys_exit_wk_by_sleep_mode(void)
{
	if (current_sleep->sleep_mode != SLEEP_MODE_NORMAL) {                 /* 普通模式没有需要恢复的临时掩码。 */
		if (current_sleep->sleep_mode == SLEEP_MODE_FORCE_SLEEP) {    /* 强制测试退出时同时清除策略模式。 */
			current_sleep->sleep_mode = SLEEP_MODE_NORMAL;        /* 将模式改为普通休眠。 */
		}
		g_sleep_wksrc_en = g_sleep_wksrc_en_bak;                      /* 还原进入特殊模式前保存的源集合。 */
#ifdef CONFIG_SYS_WAKELOCK
		sys_wakelocks_enable(1);                                      /* 重新启用系统唤醒锁约束。 */
#endif
	}
}

/**
  * @brief  关闭普通外部中断并只启用允许的唤醒线路。
  * @param  无。
  * @retval 无。
  * @note   先清 NVIC 使能，按 A/B 版设置对应硬件位；未清挂起位的原调试代码保持禁用。
  */
static void sys_set_wksrc_before_sleep(void)
{
	int i;                                                                                          /* 扫描逻辑源到硬件唤醒位的映射索引。 */
	uint32_t irq_en0, irq_en1;                                                                      /* 累积第一组和第二组 NVIC 允许掩码。 */
	uint32_t wksrc_en;                                                                              /* 累积 B 版额外硬件唤醒掩码。 */

	sys_set_wksrc_by_sleep_mode();                                                                  /* 先根据特殊模式调整软件允许集合。 */

	sys_write32(sys_read32(NVIC_ISER0), NVIC_ICER0);                                                /* 向 NVIC 第一组清使能寄存器写入当前使能位，关闭原线路。 */
	sys_write32(sys_read32(NVIC_ISER1), NVIC_ICER1);                                                /* 同样关闭第二组原先使能的中断线。 */
	//sys_write32(0xffffffff, NVIC_ICPR0);
	//sys_write32(0xffffffff, NVIC_ICPR1);

	if (g_sleep_wksrc_en) {                                                                         /* 存在允许源时才重新建立唤醒掩码。 */

		irq_en0 = irq_en1 = wksrc_en = 0;                                                       /* 从空集合开始累积所有允许线路。 */

		for (i = 0; i < SLEEP_WKSRC_NUM; i++) {                                                 /* 遍历源表合并多个允许唤醒源。 */
			if ((1 << wk_msg[i].wksrc) & g_sleep_wksrc_en){                                 /* 仅选择软件掩码中允许的源。 */

				sl_dbg("%d wksrc=%s \n",i, wk_msg[i].wksrc_msg);                        /* 输出当前合并的唤醒源名称。 */

				if (wk_msg[i].irq_en_bit >= 32) {                                       /* 第二组中断号减 32 后放入 irq_en1。 */
					irq_en1 |=  1 << (wk_msg[i].irq_en_bit - 32);                   /* 累加第二组硬件中断允许位。 */
				} else {
					irq_en0 |=  1 << (wk_msg[i].irq_en_bit);                        /* 累加第一组硬件中断允许位。 */
				}

				wksrc_en |= 1 << (wk_msg[i].wksrc_en_bit);                              /* 累加 B 版源对应的额外硬件唤醒位。 */
			}
		}

		if(irq_en0)                                                                             /* 第一组存在允许位才写入 NVIC。 */
			sys_write32(irq_en0, NVIC_ISER0);                                               /* 开启第一组允许的唤醒中断线。 */

		if(irq_en1)                                                                             /* 第二组存在允许位才写入 NVIC。 */
			sys_write32(irq_en1, NVIC_ISER1);                                               /* 开启第二组允许的唤醒中断线。 */

		/* B 版芯片还需配置 MEMORYCTL2 的额外唤醒位。 */
		if (soc_dvfs_opt()) {                                                                   /* 具备 B 版适配能力时应用额外唤醒位。 */
			sys_write32((sys_read32(MEMORYCTL2) & ~(0x1ff<<15)) | wksrc_en, MEMORYCTL2);    /* 替换 MEMORYCTL2 中原唤醒字段并写入新集合。 */
			pwrgat_wksrc_en = wksrc_en;                                                     /* 缓存 B 版掩码供局部唤醒后再次入睡。 */
		}

		pwrgat_irq_en0 = irq_en0;                                                               /* 保存第一组唤醒允许掩码。 */
		pwrgat_irq_en1 = irq_en1;                                                               /* 保存第二组唤醒允许掩码。 */
	}

#if 0
	printk("NVIC_ISPR0-1=0x%x,0x%x\n", sys_read32(NVIC_ISPR0), sys_read32(NVIC_ISPR1));             /* 禁用调试分支中输出两组挂起位，日志原文保留。 */
	printk("NVIC_ISER0-1=0x%x,0x%x\n", sys_read32(NVIC_ISER0), sys_read32(NVIC_ISER1));             /* 输出两组中断允许位。 */
	printk("NVIC_IABR0-1=0x%x,0x%x\n", sys_read32(NVIC_IABR0), sys_read32(NVIC_IABR1));             /* 输出两组中断活动位。 */
#endif
}

/**
  * @brief  在再次睡眠前恢复缓存的唤醒中断配置。
  * @param  无。
  * @retval 无。
  * @note   使用此前保存的 NVIC 和 B 版唤醒位，不还原正常运行的全部外设中断。
  */
__sleepfunc static void sys_restore_wksrc(void)
{
	if(pwrgat_irq_en0)                                                                             /* 第一组有缓存允许位时重新使能。 */
		sys_write32(pwrgat_irq_en0, NVIC_ISER0);                                               /* 恢复第一组唤醒线路。 */

	if(pwrgat_irq_en1)                                                                             /* 第二组有缓存允许位时重新使能。 */
		sys_write32(pwrgat_irq_en1, NVIC_ISER1);                                               /* 恢复第二组唤醒线路。 */

	if (soc_dvfs_opt() && pwrgat_wksrc_en)                                                         /* 芯片支持且缓存了额外唤醒位时才恢复该字段。 */
		sys_write32((sys_read32(MEMORYCTL2) & ~(0x1ff<<15)) | pwrgat_wksrc_en, MEMORYCTL2);    /* 恢复 B 版 MEMORYCTL2 唤醒配置。 */
}

#ifdef SLEEP_AOD_GPIO_REG_UNSET_HIGHZ
static uint32_t __act_s2_sleep_data s2_aod_gpio_reg_backups[ARRAY_SIZE(backup_regs_aod_gpio)];    /* AOD 关闭时临时高阻引脚的原配置备份。 */
#endif

/**
  * @brief  保存 GPIO 和公共电源寄存器，并将不用的引脚置高阻。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void sys_pm_backup_registers(void)
{
	/* 先保存后置高阻，AOD 相关引脚按当前显示策略单独处理。 */
	int i;                                                                               /* 按保存表索引遍历引脚和公共寄存器。 */
	for (i = 0; i < ARRAY_SIZE(backup_regs_gpio); i++){                                  /* 保存后将引脚置高阻。 */
		s2_gpio_reg_backups[i] = sys_read32(backup_regs_gpio[i]);                    /* 保留该引脚原有复用、电气和中断配置。 */
		sys_write32(0x1000, backup_regs_gpio[i]);                                    /* 以原平台常量 0x1000 将该不用的引脚置高阻。 */
	}

#ifdef SLEEP_AOD_GPIO_REG_UNSET_HIGHZ
	if (soc_get_aod_mode() == 0) {                                                       /* 非 AOD 模式才允许把额外显示相关引脚置高阻。 */
		for (i = 0; i < ARRAY_SIZE(backup_regs_aod_gpio); i++) {                     /* 保存后将引脚置高阻。 */
			s2_aod_gpio_reg_backups[i] = sys_read32(backup_regs_aod_gpio[i]);    /* 保存该 AOD 引脚的原配置。 */
			sys_write32(0x1000, backup_regs_aod_gpio[i]);                        /* 将该引脚暂置高阻以减小漏电。 */
		}
	}
#endif

	for (i = 0; i < ARRAY_SIZE(backup_regs_addr); i++)                                   /* 顺序读取地址表中的每个公共寄存器。 */
		s2_reg_backups[i] = sys_read32(backup_regs_addr[i]);                         /* 将寄存器值放入对应保留数组。 */
}

/**
  * @brief  按逆序恢复 GPIO 和公共电源寄存器。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void sys_pm_restore_registers(void)
{
	/* 与保存阶段对应，先逆序恢复引脚，再恢复公共电源和时钟寄存器。 */

	int i;                                                                               /* 使用有符号索引支持逆序结束于 -1。 */
	for (i = ARRAY_SIZE(backup_regs_gpio) - 1; i >= 0; i--)                              /* 逆序恢复保存表中的普通 GPIO 配置。 */
		sys_write32(s2_gpio_reg_backups[i], backup_regs_gpio[i]);                    /* 恢复该引脚原配置。 */

#ifdef SLEEP_AOD_GPIO_REG_UNSET_HIGHZ
	if (soc_get_aod_mode() == 0) {                                                       /* 与保存时的 AOD 条件保持一致。 */
		for (i = ARRAY_SIZE(backup_regs_aod_gpio) - 1; i >= 0; i--) {                /* 逆序恢复额外 AOD GPIO。 */
			sys_write32(s2_aod_gpio_reg_backups[i], backup_regs_aod_gpio[i]);    /* 写回该额外引脚原配置。 */
		}
	}
#endif

	for (i = ARRAY_SIZE(backup_regs_addr) - 1; i >= 0; i--) {                            /* 逆序恢复公共电源、时钟及中断寄存器。 */
		sys_write32(s2_reg_backups[i], backup_regs_addr[i]);                         /* 将保存值写回原寄存器地址。 */
	}
}

/* 指令缓存和 SPI0 控制的保存地址表。 */
/* 各项分别保存 SPI0 控制、延迟链、缓存控制及三组存储器控制；相同索引在备份与恢复中一一对应。 */
static uint32_t __act_s2_sleep_data icache_backup_regs_addr[] = {
	SPI0_CTL,
	SPI0_DELAYCHAIN,
	SPICACHE_CTL,
	MEMORYCTL,
	MEMORYCTL2,
	MEMORYCTL3,
};

/* 数据缓存和 SPI1/PSRAM 控制的保存地址表。 */
/* 按原顺序备份 SPI1 控制、DDR/延迟链、缓存、操作区间、PSRAM 映射与 GPU 相关控制。 */
static uint32_t __act_s2_sleep_data dcache_backup_regs_addr[] = {
	SPI1_CTL,
	SPI1_DDR_MODE_CTL,
	SPI1_DELAYCHAIN,
	SPI1_DQS1_DELAYCHAIN,
	SPI1_CACHE_CTL,
	CACHE_OPERATE_ADDR_START,
	CACHE_OPERATE_ADDR_END,
	SPI1_PSRAM_MAPPING_MISS_ADDR,
	SPI1_GPU_CTL,
};

enum pwrgat_gpio_pin {    /* 掉电流程使用的片选、电源和调试 UART 引脚编号。 */
	NOR_CS_PIN        = GPIO_0,
	NOR_PWR_PIN       = GPIO_64,
	PSRAM_CS_PIN      = GPIO_40,
	DEBUG_UART_TX_PIN = GPIO_28,
	DEBUG_UART_RX_PIN = GPIO_29,
};

/* RAM0～RAM12 的连续物理块索引；RAM_REGION_NUM 是块数边界，不是可关闭的额外块。 */
enum ram_index {    /* 按物理 RAM 块顺序定义索引，末项表示可管理块数。 */
	RAM0_IDX = 0,
	RAM1_IDX,
	RAM2_IDX,
	RAM3_IDX,
	RAM4_IDX,
	RAM5_IDX,
	RAM6_IDX,
	RAM7_IDX,
	RAM8_IDX,
	RAM9_IDX,
	RAM10_IDX,
	RAM11_IDX,
	RAM12_IDX,
	RAM_REGION_NUM,
};

/* 可管理 SRAM 地址范围的起点。 */
#define  RAM_START_ADDR                       0x2FF60000
/* 相邻 RAM 块间隔 64 KiB。 */
#define  RAM_STEP                             0x00010000
/* 通用 DMA 中断使能寄存器地址。 */
#define  DMAIE                                (DMA_REG_BASE  + 0x04)
/* 通用 DMA 调试控制地址。 */
#define  DMADEBUG                             (DMA_REG_BASE  + 0x80)
/* 传感器 DMA 中断使能地址。 */
#define  SDMAIE                               (SDMA_REG_BASE + 0x04)
/* 传感器 DMA 优先级控制地址。 */
#define  SDMA_PRIORITY                        (SDMA_REG_BASE + 0x70)
/* 传感器 DMA 调试控制地址。 */
#define  SDMADEBUG                            (SDMA_REG_BASE + 0x80)
/* 传感器 DMA 耦合寄存器组起点。 */
#define  SDMA_COUPLE_REG_BASE                 (SDMA_REG_BASE + 0xa0)

/* SPI 缓存映射表备份 16 个寄存器。 */
#define  SPI_CACHE_MAPPING_BACKUP_REG_NUM     16
/* NVIC 优先级备份按 16 个 32 位寄存器组织。 */
#define  NVIC_IPR_REG_NUM                     16
/* 本平台备份 10 个通用 DMA 通道。 */
#define  DMA_NUM                              10
/* 本平台备份 5 个传感器 DMA 通道。 */
#define  SDMA_NUM                             5
/* 每个 DMA 通道保存 7 个连续寄存器。 */
#define  REG_NUM_PER_DMA                      7
/* 传感器 DMA 耦合寄存器组共 6 项。 */
#define  REG_NUM_SDMA_COUPLE                  6
/* 通用 DMA 所有通道的备份项总数。 */
#define  DMAX_REG_NUM                         (DMA_NUM * REG_NUM_PER_DMA)
/* 传感器 DMA 所有通道的备份项总数。 */
#define  SDMAX_REG_NUM                        (SDMA_NUM * REG_NUM_PER_DMA)

/* RAM 轻睡眠、深睡眠控制位掩码。 */
#define RAM0_DS                               (0x1 << 0)      /* RAM0 的睡眠控制位。 */
#define RAM1_12_DS                            (0xfff << 1)    /* RAM1～RAM12 的睡眠控制位集合。 */
#define RAM13_14_DS                           (0x3 << 13)     /* RAM13～RAM14 的睡眠控制位集合。 */
#define RAM15_16_DS                           (0x3 << 15)     /* RAM15～RAM16 的睡眠控制位集合。 */

/* CMU_MEMCLKEN0 中各存储区域的时钟位掩码，保持原常量取值。 */
#define ROM_CLK_EN                            (0x1 << 0)       /* ROM 时钟位。 */
#define RAM0_CLK_EN                           (0x1 << 1)       /* RAM0 时钟位。 */
#define RAM1_12_CLK_EN                        (0x3fff << 2)    /* 原平台 RAM1～RAM12 时钟集合，保留原掩码 0x3fff。 */
#define RAM15_16_CLK_EN                       (0xf << 20)      /* RAM15～RAM16 对应时钟集合，保留原掩码。 */
#define SRAM_CLK_EN                           (0xf << 24)      /* SRAM 时钟集合。 */
#define SPICACHE0_1_RAM_CLK_EN                (0xf << 28)      /* 两个 SPI 缓存 RAM 的时钟集合。 */

/* CMU_MEMCLKEN1 中蓝牙 ROM/RAM 时钟位。 */
#define BTROM_RAM_CLK_EN                      (0x1 << 16)    /* 蓝牙 ROM/RAM 时钟集合。 */

static uint32_t __act_s2_sleep_data uart_ctl, uart_br;                                             /* 保存 UART 控制和波特率寄存器。 */
static uint32_t __act_s2_sleep_data sleep_mode;                                                    /* 休眠受限环境中使用的策略副本。 */
static uint32_t __act_s2_sleep_data spi_cache_mapping_backup[SPI_CACHE_MAPPING_BACKUP_REG_NUM];    /* SPI0 缓存地址映射的备份。 */
static uint32_t __act_s2_sleep_data icache_backups[ARRAY_SIZE(icache_backup_regs_addr)];           /* 指令缓存相关寄存器备份。 */
static uint32_t __act_s2_sleep_data dcache_backups[ARRAY_SIZE(dcache_backup_regs_addr)];           /* 数据缓存与 PSRAM 控制备份。 */
static uint32_t __act_s2_sleep_data vector_tbloff_backup, nvic_ipr_backup[NVIC_IPR_REG_NUM];       /* 异常向量表地址和 NVIC 优先级备份。 */
/* 通用 DMA 通道及全局状态备份。 */
static uint32_t __act_s2_sleep_data dmax_backup[DMAX_REG_NUM];                                     /* 连续存放通用 DMA 每通道寄存器。 */
static uint32_t __act_s2_sleep_data dmaie_backup[3];                                               /* 保存 DMA 挂起、中断使能和调试寄存器。 */
/* 传感器 DMA 的全局、耦合及通道寄存器备份。 */
static uint32_t __act_s2_sleep_data sdmaie_backup;                                                 /* 保存传感器 DMA 中断允许位。 */
static uint32_t __act_s2_sleep_data sdmaprio_backup;                                               /* 保存传感器 DMA 通道优先级。 */
static uint32_t __act_s2_sleep_data sdmadebug_backup;                                              /* 保存传感器 DMA 调试控制。 */
static uint32_t __act_s2_sleep_data sdma_couple_backup[REG_NUM_SDMA_COUPLE];                       /* 保存传感器 DMA 耦合寄存器。 */
static uint32_t __act_s2_sleep_data sdmax_backup[SDMAX_REG_NUM];                                   /* 连续存放传感器 DMA 每通道寄存器。 */
/* PSRAM/NOR 片选与 UART 引脚的掉电前配置备份。 */
static uint32_t __act_s2_sleep_data pwrgat_gpio_backups[5];                                        /* 0/1 保存 PSRAM/NOR 片选，3/4 保存 UART TX/RX，索引 2 当前保留。 */

/**
  * @brief  保存 UART0 控制、波特率及当前休眠模式。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
void uart_suspend(void)
{
	sleep_mode = current_sleep->sleep_mode;         /* 复制策略供低功耗保留区代码判断是否关闭 UART。 */
	uart_ctl = sys_read32(UART0_REG_BASE);          /* 保存 UART 控制字。 */
	uart_br = sys_read32(UART0_REG_BASE + 0x10);    /* 保存 UART 波特率寄存器。 */
}

/**
  * @brief  恢复 UART0 复位、时钟、控制和波特率。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc void uart_resume(void)
{
	/* 先释放 UART0 复位并开时钟，再恢复控制和波特率。 */
	sys_write32(sys_read32(RMU_MRCR0) | (1 << RESET_ID_UART0), RMU_MRCR0);            /* 释放 UART0 复位。 */
	sys_write32(sys_read32(CMU_DEVCLKEN0) | (1 << CLOCK_ID_UART0), CMU_DEVCLKEN0);    /* 允许 UART0 外设时钟。 */
	sys_write32(uart_ctl, UART0_REG_BASE);                                            /* 还原 UART 控制字。 */
	sys_write32(uart_br, UART0_REG_BASE + 0x10);                                      /* 还原 UART 波特率设置。 */
}

/**
  * @brief  保存 SPI0 指令缓存控制与映射表。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void icache_backup_registers(void)
{
	unsigned int i;                                                                       /* 按映射表和缓存寄存器表循环的索引。 */

	for (i = 0; i < SPI_CACHE_MAPPING_BACKUP_REG_NUM; i++)                                /* 保存全部 16 项缓存地址映射。 */
		spi_cache_mapping_backup[i] = sys_read32(SPI_CACHE_MAPPING_ADDR0 + 4 * i);    /* 每项间隔 4 字节，按同一索引存放原值。 */

	for (i = 0; i < ARRAY_SIZE(icache_backup_regs_addr); i++)                             /* 遍历 SPI0 指令缓存控制地址表。 */
		icache_backups[i] = sys_read32(icache_backup_regs_addr[i]);                   /* 保存各控制寄存器值。 */
}

/**
  * @brief  保存 SPI1 数据缓存与 PSRAM 控制。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void dcache_backup_registers(void)
{
	unsigned int i;                                                        /* 遍历 SPI1 数据缓存寄存器的索引。 */

	for (i = 0; i < ARRAY_SIZE(dcache_backup_regs_addr); i++)              /* 按地址表项数读取全部数据缓存控制。 */
		dcache_backups[i] = sys_read32(dcache_backup_regs_addr[i]);    /* 保存该数据缓存寄存器的原值。 */
}


/**
  * @brief  恢复 SPI0 指令缓存控制与映射表。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void icache_restore_registers(void)
{
	unsigned int i;                                                                       /* 恢复指令缓存控制和映射的索引。 */

	for (i = 0; i < ARRAY_SIZE(icache_backup_regs_addr); i++)                             /* 先恢复指令缓存及 SPI0 控制。 */
		sys_write32(icache_backups[i], icache_backup_regs_addr[i]);                   /* 写回该缓存控制寄存器。 */

	for (i = 0; i < SPI_CACHE_MAPPING_BACKUP_REG_NUM; i++)                                /* 再恢复全部地址映射项。 */
		sys_write32(spi_cache_mapping_backup[i], SPI_CACHE_MAPPING_ADDR0 + 4 * i);    /* 把映射值写回间隔为 4 字节的对应地址。 */
}

/**
  * @brief  恢复 SPI1 数据缓存与 PSRAM 控制。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void dcache_restore_registers(void)
{
	unsigned int i;                                                        /* 恢复数据缓存控制的索引。 */

	for (i = 0; i < ARRAY_SIZE(dcache_backup_regs_addr); i++)              /* 按原表顺序恢复 SPI1/PSRAM 寄存器。 */
		sys_write32(dcache_backups[i], dcache_backup_regs_addr[i]);    /* 将备份值写回其控制寄存器。 */
}

/**
  * @brief  保存中断优先级与异常向量表地址。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void nvic_backup_registers(void)
{
	unsigned int i;                                                /* 按 32 位寄存器遍历 NVIC 优先级。 */

	for (i = 0; i < NVIC_IPR_REG_NUM; i++)                         /* 保存所有选定的 NVIC 优先级寄存器。 */
		nvic_ipr_backup[i] = sys_read32(NVIC_IPR0 + 4 * i);    /* 每项含若干中断优先级字段，按 4 字节间隔读取。 */

	vector_tbloff_backup = SCB->VTOR;                              /* 保存 ARM 异常向量表基地址。 */
}

/**
  * @brief  恢复异常向量表地址与中断优先级。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void nvic_restore_registers(void)
{
	unsigned int i;                                                /* 遍历 NVIC 优先级备份。 */

	SCB->VTOR = vector_tbloff_backup;                              /* 先恢复异常向量表地址，保证后续异常进入正确入口。 */

	for (i = 0; i < NVIC_IPR_REG_NUM; i++)                         /* 按原顺序写回 NVIC 优先级。 */
		sys_write32(nvic_ipr_backup[i], NVIC_IPR0 + 4 * i);    /* 恢复对应的 32 位优先级寄存器。 */
}

/**
  * @brief  保存两组 NVIC 挂起状态用于判定唤醒原因。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void nvic_save_pending_register(void)
{
	wk_pd0 = sys_read32(NVIC_ISPR0);    /* 保存中断号 0～31 的挂起状态。 */
	wk_pd1 = sys_read32(NVIC_ISPR1);    /* 保存中断号 32～63 的挂起状态。 */
}

/**
  * @brief  保存通用 DMA 全局状态和各通道配置。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void dma_backup_registers(void)
{
	unsigned int i, j;                                                                                    /* i 遍历通道，j 遍历通道内寄存器。 */

	for (i = 0; i < 2; i++) {                                                                             /* 保存 DMA 全局的前两个寄存器。 */
		/* 保存 DMA 挂起状态和中断使能。 */
		dmaie_backup[i] = sys_read32(DMA_REG_BASE + 4 * i);                                           /* 按 4 字节间隔保存 DMAIP 与 DMAIE。 */
	}

	/* 保存 DMA 调试控制。 */
	dmaie_backup[2] = sys_read32(DMADEBUG);                                                               /* 额外保存 DMA 调试控制。 */

	for (i = 0; i < DMA_NUM; i++) {                                                                       /* 遍历全部通用 DMA 通道。 */
		for (j = 0; j < REG_NUM_PER_DMA; j++) {                                                       /* 保存当前通道的 7 个连续寄存器。 */
			/* 逐通道保存控制至字节计数寄存器。 */
			dmax_backup[i*REG_NUM_PER_DMA + j] = sys_read32(DMA_REG_BASE + 0x100*(i+1) + 4*j);    /* 通道组间距 0x100，组内寄存器间距 4 字节。 */
		}
	}
}

/**
  * @brief  先恢复 DMA 配置，再恢复各通道启动位和中断使能。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void dma_restore_registers(void)
{
	/* DMA 恢复把启动位推迟到所有其他通道配置之后，避免恢复到一半就开始搬运。 */
	unsigned int i, j;                                                                                            /* i 为通道索引，j 为组内寄存器索引。 */

	for (i = 0; i < DMA_NUM; i++) {                                                                               /* 逐通道恢复通用 DMA。 */
		for (j = 0; j < REG_NUM_PER_DMA; j++) {                                                               /* 先遍历每通道配置寄存器，启动项随后单独恢复。 */
			/* 逐通道恢复控制至字节计数寄存器。 */
			if(j != 1)                                                                                    /* 先跳过启动寄存器，避免其他配置未恢复就启动传输。 */
				sys_write32(dmax_backup[i*REG_NUM_PER_DMA + j], DMA_REG_BASE + 0x100*(i+1) + 4*j);    /* 恢复非启动寄存器，防止过早恢复传输。 */
		}
	}

	for (i = 0; i < DMA_NUM; i++)                                                                                 /* 最后恢复各通道 DMA_START。 */
		sys_write32(dmax_backup[i*REG_NUM_PER_DMA + 1], DMA_REG_BASE + 0x100*(i+1) + 4);                      /* 全部其他配置已恢复后，再写回各通道启动项。 */

	sys_write32(dmaie_backup[2], DMADEBUG);                                                                       /* 恢复 DMA 调试控制。 */
	sys_write32(dmaie_backup[1], DMAIE);                                                                          /* 最后恢复 DMA 中断使能，不写回 DMAIP 快照。 */
}

/**
  * @brief  保存传感器 DMA 全局、耦合与通道寄存器。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void sdma_backup_registers(void)
{
	unsigned int i, j;                                                                                      /* i 遍历传感器 DMA 通道，j 遍历通道内寄存器。 */

	sdmaie_backup    = sys_read32(SDMAIE);                                                                  /* 保存传感器 DMA 中断使能。 */
	sdmaprio_backup  = sys_read32(SDMA_PRIORITY);                                                           /* 保存传感器 DMA 优先级。 */
	sdmadebug_backup = sys_read32(SDMADEBUG);                                                               /* 保存传感器 DMA 调试状态。 */

	for (i = 0; i < REG_NUM_SDMA_COUPLE; i++)                                                               /* 保存全部 6 个耦合寄存器。 */
		sdma_couple_backup[i] = sys_read32(SDMA_COUPLE_REG_BASE + 4*i);                                 /* 按 4 字节间距读取耦合配置。 */

	for (i = 0; i < SDMA_NUM; i++) {                                                                        /* 遍历全部传感器 DMA 通道。 */
		for (j = 0; j < REG_NUM_PER_DMA; j++) {                                                         /* 遍历当前通道的 7 个寄存器。 */
			/* 逐传感器 DMA 通道保存配置。 */
			sdmax_backup[i*REG_NUM_PER_DMA + j] = sys_read32(SDMA_REG_BASE + 0x100*(i+1) + 4*j);    /* 按通道步进和寄存器步进保存。 */
		}
	}
}

/**
  * @brief  按原次序恢复传感器 DMA 通道、耦合与全局寄存器。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void sdma_restore_registers(void)
{
	unsigned int i, j;                                                                                      /* 逐通道、逐寄存器恢复传感器 DMA。 */

	for (i = 0; i < SDMA_NUM; i++) {                                                                        /* 遍历所有传感器 DMA 通道。 */
		for (j = 0; j < REG_NUM_PER_DMA; j++) {                                                         /* 按原次序恢复该通道的所有 7 项，包括原启动项。 */
			/* 逐传感器 DMA 通道恢复配置，保留原写入顺序。 */
			sys_write32(sdmax_backup[i*REG_NUM_PER_DMA + j], SDMA_REG_BASE + 0x100*(i+1) + 4*j);    /* 写回该通道寄存器，保持原实现的恢复顺序。 */
		}
	}

	for (i = 0; i < REG_NUM_SDMA_COUPLE; i++)                                                               /* 恢复通道耦合寄存器组。 */
		sys_write32(sdma_couple_backup[i], SDMA_COUPLE_REG_BASE + 4*i);                                 /* 将该耦合配置写回原地址。 */

	sys_write32(sdmadebug_backup, SDMADEBUG);                                                               /* 恢复传感器 DMA 调试控制。 */
	sys_write32(sdmaprio_backup,  SDMA_PRIORITY);                                                           /* 恢复传感器 DMA 优先级。 */
	sys_write32(sdmaie_backup,    SDMAIE);                                                                  /* 最后恢复传感器 DMA 中断使能。 */
}

/**
  * @brief  保存 PSRAM 片选复用并将其固定为高电平。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void pwrgat_gpio_psram_backup_registers(void)
{
	/* 固定 PSRAM 片选为高，避免 CPU 掉电期间误选中。 */
	pwrgat_gpio_backups[0] = sys_read32(GPION_CTL(PSRAM_CS_PIN));      /* 保存 PSRAM 片选原复用与电气配置。 */
	sys_write32(1 << (PSRAM_CS_PIN % 32), GPION_BSR(PSRAM_CS_PIN));    /* 先置输出锁存为高，减少切换时片选毛刺。 */
	sys_write32(0x1040, GPION_CTL(PSRAM_CS_PIN));                      /* 用原常量切为 GPIO 输出配置。 */
	sys_write32(1 << (PSRAM_CS_PIN % 32), GPION_BSR(PSRAM_CS_PIN));    /* 切换配置后再次置高保持未选中。 */
}

/**
  * @brief  保存 NOR 片选复用并将其固定为高电平。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void pwrgat_gpio_nor_backup_registers(void)
{
	/* 固定 NOR 片选为高，避免 CPU 掉电期间误选中。 */
	pwrgat_gpio_backups[1] = sys_read32(GPION_CTL(NOR_CS_PIN));    /* 保存 NOR 片选原复用与电气配置。 */
	sys_write32(1 << (NOR_CS_PIN % 32), GPION_BSR(NOR_CS_PIN));    /* 先将 NOR 片选输出锁存置高。 */
	sys_write32(0x1040, GPION_CTL(NOR_CS_PIN));                    /* 切为 GPIO 输出，保留原常量。 */
	sys_write32(1 << (NOR_CS_PIN % 32), GPION_BSR(NOR_CS_PIN));    /* 再次置高以保持 NOR 未选中。 */
}

/**
  * @brief  在蓝牙关闭或特殊休眠模式下将 UART 引脚置高阻。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void pwrgat_set_unused_uart_highz(void)
{
	/* 不用调试 UART 时将引脚置高阻。原硬件备注指出，掉电门控时不这样处理会多消耗约 180 µA；该数值属于原平台测量记录。 */
#ifdef CONFIG_SLEEP_DISABLE_BT
	sys_write32(0x1000, GPION_CTL(DEBUG_UART_TX_PIN));            /* 调试禁用蓝牙时把 UART TX 置高阻。 */
	sys_write32(0x1000, GPION_CTL(DEBUG_UART_RX_PIN));            /* 将 UART RX 同样置高阻。 */
#endif
	if (sleep_mode != SLEEP_MODE_NORMAL) {                        /* 特殊休眠模式不保留 UART 引脚驱动。 */
		sys_write32(0x1000, GPION_CTL(DEBUG_UART_TX_PIN));    /* 将不使用的 UART TX 置高阻。 */
		sys_write32(0x1000, GPION_CTL(DEBUG_UART_RX_PIN));    /* 将不使用的 UART RX 置高阻。 */
	}
}

/**
  * @brief  恢复 NOR 片选引脚配置。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void pwrgat_gpio_nor_restore_registers(void)
{
	sys_write32(pwrgat_gpio_backups[1], GPION_CTL(NOR_CS_PIN));    /* 恢复 NOR 片选引脚原复用。 */
}

/**
  * @brief  恢复 PSRAM 片选引脚配置。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void pwrgat_gpio_psram_restore_registers(void)
{
	sys_write32(pwrgat_gpio_backups[0], GPION_CTL(PSRAM_CS_PIN));    /* 恢复 PSRAM 片选引脚原复用。 */
}

/**
  * @brief  恢复 UART TX/RX 引脚配置。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void pwrgat_gpio_uart_restore_registers(void)
{
	sys_write32(pwrgat_gpio_backups[3], GPION_CTL(DEBUG_UART_TX_PIN));    /* 恢复主 CPU UART TX 原配置。 */
	sys_write32(pwrgat_gpio_backups[4], GPION_CTL(DEBUG_UART_RX_PIN));    /* 恢复主 CPU UART RX 原配置。 */
}

/* NOR 双边沿模式下按供电电压匹配采样延迟链。 */
#ifdef CONFIG_SPI0_NOR_DTR_MODE
struct delaychain_tbl {
	uint16_t volt_mv;       /* 表项供电电压，单位 mV。 */
	uint16_t delaychain;    /* 匹配的 SPI0 延迟链设置。 */
};

static struct delaychain_tbl __act_s2_sleep_data delay_array[] = {    /* 电压与延迟链的原平台适配表，不按电压插值。 */
	{900, 19},  {950, 20},
	{1000, 22}, {1050, 23},
	{1100, 25}, {1150, 26},
	{1200, 28}
};

/**
  * @brief  按精确电压匹配 NOR DTR 延迟链。
  * @param  vdd_mv：[输入]供电电压，单位 mV，匹配 delay_array 的表项。
  * @retval 无。
  * @note   无匹配时不修改；只改 SPI0_DELAYCHAIN 低 6 位，电压表和常量保持原硬件适配值。
  */
__sleepfunc static void nor_dtr_set_matched_delaychain(uint16_t vdd_mv)
{
	int i;                                             /* 搜索电压匹配项的索引。 */

	for (i = 0; i < ARRAY_SIZE(delay_array); i++) {    /* 逐项检查直到表尾；无匹配则不修改。 */
		if (vdd_mv == delay_array[i].volt_mv) {    /* 只有电压精确相等才使用该表项。 */
			/* 保持高位配置，仅替换延迟链低 6 位。 */
			sys_write32((sys_read32(SPI0_DELAYCHAIN) & ~(0x3F)) \
				| delay_array[i].delaychain, SPI0_DELAYCHAIN);
		}
	}
}

/**
  * @brief  将当前 NOR 延迟链同步到指令缓存恢复备份。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
__sleepfunc static void nor_dtr_backup_delaychain(void)
{
	icache_backups[1] = sys_read32(SPI0_DELAYCHAIN);    /* SPI0_DELAYCHAIN 在指令缓存地址表索引 1 处。 */
}
#endif

/**
  * @brief  对链接器标记的完整 SRAM 块执行掉电或恢复。
  * @param  is_pwrdown：[输入]true 表示下电，false 表示恢复。
  * @retval 无。
  * @note   边界无效时直接返回；不处理边界所在的部分块，执行代码及备份数据不能位于将被关闭的块内。
  */
static void sys_pwrctrl_assigned_sram(bool is_pwrdown)
{
	/* 根据链接区间只关闭完整包含的 RAM 块，边界块继续保留。 */
	uint32_t start_addr    = (uint32_t)_sleep_shutdown_ram_start;                        /* 从链接符号取得允许关闭的 SRAM 区间起点。 */
	uint32_t end_addr      = (uint32_t)_sleep_shutdown_ram_end;                          /* 取得该区间终点。 */
	uint16_t ram_start_idx = (start_addr - RAM_START_ADDR) / RAM_STEP;                   /* 根据基址和 64 KiB 步长计算起点所在块。 */
	uint16_t ram_end_idx   = (end_addr   - RAM_START_ADDR) / RAM_STEP;                   /* 计算终点所在块。 */
	uint32_t ram_sleep_reg = 0;                                                          /* 累积要改变睡眠/掉电状态的 RAM 位。 */
	uint32_t ram_clken_reg = 0;                                                          /* 累积对应 RAM 时钟位。 */
	uint16_t i;                                                                          /* 遍历完整覆盖的 RAM 块。 */

	if (start_addr < RAM_START_ADDR)                                                     /* 起点低于可管理 SRAM 范围则拒绝操作。 */
		return ;                                                                     /* 不改电源控制，直接返回。 */

	if (end_addr >= (RAM_START_ADDR + RAM_REGION_NUM * RAM_STEP))                        /* 终点达到或超过可管理上界则拒绝操作。 */
		return ;                                                                     /* 范围无效时不继续关闭 RAM。 */

	/* 只处理起止索引之间完整覆盖的 RAM 区域，边界所在块不下电。 */
	for (i = ram_start_idx + 1; i < ram_end_idx; i++) {                                  /* 从起点块的下一块处理到终点块之前，保留边界块。 */
		/* 累加相应 RAM 区域的睡眠/掉电控制位。 */
		ram_sleep_reg |= (0x1 << i);                                                 /* 将当前完整块加入睡眠和掉电掩码。 */

		/* 按 RAM 索引对应的时钟位布局生成掩码。 */
		if (i <= RAM11_IDX)                                                          /* RAM0～RAM11 使用连续时钟位映射。 */
			ram_clken_reg |= (0x1 << (i + 1));                                   /* 块索引加 1 得到对应时钟位。 */
		else if (i < RAM_REGION_NUM)                                                 /* 后续 RAM 块使用双位步进的时钟映射。 */
			ram_clken_reg |= (0x1 << (RAM11_IDX + 1 + 2*(i - RAM11_IDX)));       /* 按原平台公式累加该块时钟位。 */
	}

	if (is_pwrdown) {                                                                    /* 下电与恢复使用相反顺序，避免访问停钟 RAM。 */
		/* 先使目标 RAM 进入轻/深睡眠并设置掉电控制。 */
		sys_write32(sys_read32(RAM_LIGHTSLEEP) | ram_sleep_reg, RAM_LIGHTSLEEP);     /* 允许目标块进入轻睡眠。 */
		sys_write32(sys_read32(RAM_DEEPSLEEP)  | ram_sleep_reg, RAM_DEEPSLEEP);      /* 允许目标块进入深睡眠。 */
		sys_write32(sys_read32(PWRGATE_RAM)    | ram_sleep_reg, PWRGATE_RAM);        /* 设置目标 RAM 块掉电控制位。 */

		/* 随后关闭这些 RAM 的时钟。 */
		sys_write32(sys_read32(CMU_MEMCLKEN0)  & ~ram_clken_reg, CMU_MEMCLKEN0);     /* 关闭对应 RAM 时钟。 */
	}
	else {
		/* 恢复时先打开 RAM 时钟。 */
		sys_write32(sys_read32(CMU_MEMCLKEN0)  | ram_clken_reg, CMU_MEMCLKEN0);      /* 恢复时先打开所需 RAM 时钟。 */

		/* 再撤销掉电及睡眠控制。 */
		sys_write32(sys_read32(PWRGATE_RAM)    & ~ram_sleep_reg, PWRGATE_RAM);       /* 撤销目标块掉电控制。 */
		sys_write32(sys_read32(RAM_DEEPSLEEP)  & ~ram_sleep_reg, RAM_DEEPSLEEP);     /* 撤销目标块深睡眠控制。 */
		sys_write32(sys_read32(RAM_LIGHTSLEEP) & ~ram_sleep_reg, RAM_LIGHTSLEEP);    /* 撤销目标块轻睡眠控制。 */
	}
}

/**
  * @brief  在完整系统环境处理之前延后的唤醒回调。
  * @param  无。
  * @retval 任一回调要求或没有待处理回调时返回 RUN_SYSTEM，否则返回 SLEEP_AGAIN。
  * @note   处理后将待回调数清零；回调节点和代码须仍有效。
  */
static enum WK_CB_RC wakeup_system_callback(void)
{
	int i;                                                                                      /* 遍历等待完整系统环境的回调项。 */
	enum WK_CB_RC rc;                                                                           /* 汇总是否还需要继续保持系统运行。 */

	if (current_sleep->g_num_cb) {                                                              /* 存在延后回调时逐一处理。 */
		rc = WK_CB_SLEEP_AGAIN;                                                             /* 先假定全部回调允许继续休眠。 */

		printk("wake call fun=%d\n", current_sleep->g_num_cb);                              /* 打印即将执行的延后回调数。 */

		for (i = 0; i < current_sleep->g_num_cb; i++){                                      /* 每个延后回调执行一次，不超过记录数量。 */

			printk("call wksrc=%d fun\n", current_sleep->g_syste_cb[i].src);            /* 打印当前回调对应的源。 */

			if (current_sleep->g_syste_cb[i].wk_cb(current_sleep->g_syste_cb[i].src)    /* 把记录源传给回调，只要它要求运行系统就保持 RUN_SYSTEM。 */
				== WK_CB_RUN_SYSTEM)                                                /* 任一回调要求恢复系统，则汇总结果为 RUN_SYSTEM。 */
				rc = WK_CB_RUN_SYSTEM;                                              /* 汇总要求恢复系统的结果，不被后续回调降级。 */
		}

		current_sleep->g_num_cb = 0;                                                        /* 回调表已消费，清除有效条目数。 */
	} else {
		rc = WK_CB_RUN_SYSTEM;                                                              /* 没有延后回调时默认让系统继续运行。 */
	}

	return rc;                                                                                  /* 返回本轮回调汇总结果。 */
}

/**
  * @brief  允许 PMU ONOFF 短按作为唤醒源。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void soc_pmu_onoff_wk_set(void)
{
	//sys_write32(0x3, WIO0_CTL);
	sys_write32(sys_read32(PMU_INTMASK) | (1 << 1), PMU_INTMASK);    /* 允许 ONOFF 按键短按唤醒。 */
	sys_s3_wksrc_set(SLEEP_WK_SRC_PMU);                              /* 将 PMU 加入软件允许源。 */
	//printk("PMUINTMASK=0X%X\n", sys_read32(PMU_INTMASK));
}

//#define CONFIG_GPIO_WAKEUP_TEST
#ifdef CONFIG_GPIO_WAKEUP_TEST
#define GPIO_N_WK  GPIO_21
//#define WIO_N_WK	WIO_1

/**
  * @brief  配置测试 GPIO/WIO 上升沿唤醒。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void soc_gpio_wakeup_test(void)
{
#ifdef GPIO_N_WK
	printk("gpio=%d wakeup test \n", GPIO_N_WK);    /* 打印所选 GPIO 测试编号。 */
	/* 设置 GPIO 输入、施密特、上拉、中断及上升沿触发，保留驱动等级 3。 */
	sys_write32(GPIO_CTL_GPIO_INEN|GPIO_CTL_SMIT|GPIO_CTL_PULLUP| GPIO_CTL_INTC_EN | GPIO_CTL_INTC_MASK |
					GPIO_CTL_INC_TRIGGER_RISING_EDGE|GPIO_CTL_PADDRV_LEVEL(3), GPION_CTL(GPIO_N_WK));

#else
	printk("wio =%d wakeup test \n", WIO_N_WK);     /* WIO 测试分支打印所选编号。 */
	/* 以相同电气和上升沿要求配置 WIO 测试输入。 */
	sys_write32(GPIO_CTL_GPIO_INEN|GPIO_CTL_SMIT|GPIO_CTL_PULLUP| GPIO_CTL_INTC_EN | GPIO_CTL_INTC_MASK |
					GPIO_CTL_INC_TRIGGER_RISING_EDGE|GPIO_CTL_PADDRV_LEVEL(3), WIO_REG_CTL(WIO_N_WK));
#endif
	sys_s3_wksrc_set(SLEEP_WK_SRC_GPIO);            /* 把 GPIO 加入软件允许唤醒集合。 */
}

/**
  * @brief  读取并处理唤醒测试引脚的挂起状态。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void soc_gpio_check(void)
{
#ifdef GPIO_N_WK
	printk("*******gpio=%d check----\n", GPIO_N_WK);                                        /* 打印 GPIO 检查标记。 */
	if(sys_read32(GPIO_REG_IRQ_PD(GPIO_REG_BASE, GPIO_N_WK)) & GPIO_BIT(GPIO_N_WK)){        /* 判断测试 GPIO 对应的中断挂起位。 */
		printk("*******gpio=%d wakeup----\n", GPIO_N_WK);                               /* 确认测试 GPIO 产生了唤醒。 */
		sys_write32(GPIO_BIT(GPIO_N_WK), GPIO_REG_IRQ_PD(GPIO_REG_BASE, GPIO_N_WK));    /* 向挂起位写 1 清除，保持其他引脚状态。 */
	}
#else
	printk("*******wigo=%d check----\n", WIO_N_WK);                                         /* 打印 WIO 检查标记。 */
	if(sys_read32(WIO_REG_CTL(WIO_N_WK)) & WIO_CTL_INT_PD_MASK){                            /* 检查 WIO 中断挂起字段。 */
		printk("*******wio=%d wakeup----\n", WIO_N_WK);                                 /* 记录 WIO 唤醒事件。 */
		sys_write32(WIO_REG_CTL(WIO_N_WK), sys_read32(WIO_REG_CTL(WIO_N_WK)));          /* 原代码参数顺序疑似颠倒，此处保留并说明，不能视为已核实的清挂起操作。 */
	}
#endif
}
#endif

/**
  * @brief  恢复公共寄存器、记录唤醒源并执行系统级回调。
  * @param  无。
  * @retval wakeup_system_callback 的汇总结果。
  * @note   撤销 SLEEPDEEP；上层深/浅睡眠入口未使用此返回值。
  */
static enum WK_CB_RC soc_cmu_sleep_exit(void)
{
	SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;    /* 清 Cortex-M SLEEPDEEP，后续 WFI 不再沿用深睡眠标志。 */
#ifdef CONFIG_GPIO_WAKEUP_TEST
	soc_gpio_check();                      /* 仅测试配置检查 GPIO 唤醒状态。 */
#endif
	sys_pm_restore_registers();            /* 恢复公共 GPIO 和电源时钟寄存器。 */
	sys_sleep_check_wksrc();               /* 从已保存的挂起快照选出唤醒来源。 */
#ifdef CONFIG_SLEEP_MEMORY_CHECK_INTEGRITY
	wakeup_check_dump();                   /* 完整性配置下输出检查结果。 */
#endif
	sys_exit_wk_by_sleep_mode();           /* 恢复特殊模式临时更改的源集合和唤醒锁。 */


	return wakeup_system_callback();       /* 执行延后回调并返回汇总结果。 */
}

/**
  * @brief  保存外设并配置休眠电源、时钟与唤醒源。
  * @param  force_bt_pg：[输入]实际为 0 时强制蓝牙下电，非零时保留蓝牙；名称不代表真假方向。
  * @retval 无。
  * @note   随后设置 ARM SLEEPDEEP；调用前须由电源管理流程保证外设已满足休眠条件。
  */
static void soc_cmu_sleep_prepare(int force_bt_pg)
{
	/* 先保存公共外设和 GPIO，再应用当前模式的唤醒源与电源策略。 */
	sys_pm_backup_registers();                                                           /* 保存休眠前的外设寄存器。 */
	sys_s3_wksrc_set(SLEEP_WK_SRC_BT);                                                   /* 加入蓝牙唤醒源。 */
	soc_pmu_onoff_wk_set();                                                              /* 加入 ONOFF 唤醒源。 */
#ifdef CONFIG_GPIO_WAKEUP_TEST
	soc_gpio_wakeup_test();                                                              /* 测试配置下加入 GPIO/WIO 唤醒。 */
#endif
	/* 参考寄存器快照：RMU_MRCR0=0x1b0d5fb1，RMU_MRCR1=0xc10c000c，CMU_DEVCLKEN0=0x5b0413b1，CMU_DEVCLKEN1=0x1f0c000c；后者 bit24～28 对应蓝牙。此处仅为历史备注，不写这些值。 */
	sys_write32(0x0, PMUADC_CTL);                                                        /* 关闭 PMU ADC。 */

	/* 对显示、GPU、DSP 音频外设执行掉电门控。 */
	sys_write32(sys_read32(PWRGATE_DIG)
				& ~(1 << PWRGATE_DIG_DISPLAY_PG)
				& ~(1 << PWRGATE_DIG_GPU_PG)
				& ~(1 << PWRGATE_DIG_DSP_AU_PG), PWRGATE_DIG);

	/* 调试禁用蓝牙或特定休眠模式下强制蓝牙掉电；原参数名 force_bt_pg 与判断相反，实际为 0 时关蓝牙。 */
	if (force_bt_pg == 0) {                                                              /* 此原参数实际为 0 时强制关闭蓝牙。 */
		sys_write32(sys_read32(RMU_MRCR1) & ~(1 << (RESET_ID_BT-32)), RMU_MRCR1);    /* 使蓝牙 hub 进入复位。 */

		/* 设置蓝牙强制掉电控制并撤销其供电使能。 */
		sys_write32((sys_read32(PWRGATE_DIG) | (1 << PWRGATE_DIG_BT_FORCE))
					& ~(1 << PWRGATE_DIG_BT_PG), PWRGATE_DIG);
	}

	/* SPI0 时钟切换为 HOSC 源，并清除分频字段。 */
	sys_write32(sys_read32(CMU_SPI0CLK) & ~(0x3 << 8) & ~(0xf << 0), CMU_SPI0CLK);       /* 清 SPI0 时钟源及分频字段，回到 HOSC 基准配置。 */

	sys_set_wksrc_before_sleep();                                                        /* 按策略应用可唤醒的中断线和硬件唤醒位。 */

	SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk ;                                                  /* 设置 Cortex-M 深睡眠标志。 */


}

#ifdef CONFIG_DISABLE_IRQ_STAT

/**
  * @brief  直接通过 BASEPRI 锁定普通可屏蔽中断。
  * @param  无。
  * @retval 先前 BASEPRI 值，用于配对恢复。
  * @note   CONFIG_DISABLE_IRQ_STAT 分支绕过统计包装；含 ISB 和编译器 memory 约束，不屏蔽全部异常。
  */
static inline  unsigned int n_irq_lock(void)
{
	unsigned int key;    /* 保存进入前 BASEPRI 中断优先级屏蔽门限。 */
	unsigned int tmp;    /* 为设置新门限提供临时通用寄存器。 */
	/* 读取旧 BASEPRI，写入默认 IRQ 屏蔽门限并执行 ISB；memory 约束防止编译器跨越重排。 */
	__asm__ volatile(
		"mov %1, %2;"
		"mrs %0, BASEPRI;"
		"msr BASEPRI, %1;"
		"isb;"
		: "=r"(key), "=r"(tmp)
		: "i"(_EXC_IRQ_DEFAULT_PRIO)
		: "memory");

	return key;          /* 返回旧门限供后续恢复。 */
}

/**
  * @brief  按保存键恢复 BASEPRI。
  * @param  key：[输入]配对锁保存的 BASEPRI 值，0 表示清除该门限。
  * @retval 无。
  * @note   含 ISB 与编译器 memory 约束；PRIMASK 可能仍独立屏蔽普通中断。
  */
static inline  void n_irq_unlock(unsigned int key)
{
	/* 写回 BASEPRI 并执行 ISB；不修改独立的 PRIMASK。 */
	__asm__ volatile(
		"msr BASEPRI, %0;"
		"isb;"
		:  : "r"(key) : "memory");
}
#endif

/**
  * @brief  在保留代码区切换低功耗时钟电压并执行 CPU 等待/掉电。
  * @param  无。
  * @retval 无。
  * @note   CPU 掉电时用 __cpu_suspend，否则执行 DSB/WFI。醒来先保存中断挂起位，再恢复传感器处理所需资源；须由外层保证调用顺序。
  */
__sleepfunc static void __cpu_enter_sleep(void)
{
	/* 本函数只恢复局部唤醒处理所需资源；完整运行配置由外层 cpu_enter_sleep 还原。 */
	uint32_t devclk[2];                                                                         /* 保存局部唤醒处理所需设备时钟集合。 */
	uint32_t val;                                                                               /* 备份 S1 电压控制寄存器原值。 */
	uint32_t wd_ctl;                                                                            /* 保存看门狗控制，唤醒后还原。 */
	uint32_t reg_trace_bak;                                                                     /* 保存跟踪控制以跨 CPU 掉电恢复。 */
#ifdef CONFIG_SPI0_NOR_DTR_MODE
	uint32_t spi0_delaychain_bak;                                                               /* DTR 配置保存临时调整前的 NOR 延迟链。 */
#endif

	//jtag_enable();
	/* 保留的 SPI0 缓存关闭调试代码未启用。 */
	//sys_clear_bit(SPICACHE_CTL, 0);

	/* 裁剪外设时钟，保留外部中断、传感器及定时器所需资源。 */
	sys_write32((1<<CLOCK_ID_EXINT), CMU_DEVCLKEN0);                                            /* 低功耗准备中只保留此组 EXINT 时钟。 */

	sys_write32(0x9f000000 |  (1 << (CLOCK_ID_I2CMT1 - 32)), CMU_DEVCLKEN1);                    /* 第二组保留原硬件常量及 IICMT1 所需时钟。 */
	/* 使能 TIMER0～TIMER5，维持休眠期间必要计数和唤醒。 */
	sys_write32(sys_read32(CMU_DEVCLKEN1) |
				(1 << (CLOCK_ID_TIMER0 - 32)) |
				(1 << (CLOCK_ID_TIMER1 - 32)) |
				(1 << (CLOCK_ID_TIMER2 - 32)) |
				(1 << (CLOCK_ID_TIMER3 - 32)) |
				(1 << (CLOCK_ID_TIMER4 - 32)) |
				(1 << (CLOCK_ID_TIMER5 - 32)), CMU_DEVCLKEN1);
	sys_write32(0x0, CMU_GPIOCLKCTL);                                                           /* GPIO 时钟选择 RC32K。 */

	devclk[0] = sys_read32(CMU_DEVCLKEN0);                                                      /* 保存第一组局部处理时钟配置。 */
	devclk[1] = sys_read32(CMU_DEVCLKEN1);                                                      /* 保存第二组局部处理时钟配置。 */

	/* 使能 RC64M，为 SPI0 时钟切换作准备。 */
	sys_set_bit(CMU_S1CLKCTL,2);                                                                /* 允许 RC64M 供时钟切换使用。 */
	/* 按芯片版本选择 CPU 的低速 RC 时钟配置。 */
	if (soc_dvfs_opt())                                                                         /* 按芯片版本选择低速系统时钟编码。 */
		sys_write32(0x0, CMU_SYSCLK);                                                       /* 支持 DVFS 方案的芯片使用该 RC 低速编码。 */
	else
		sys_write32(0x02000230, CMU_SYSCLK);                                                /* 另一芯片版本使用原平台 RC 低速编码。 */

	/* NOR 控制器改用 RC64M 派生时钟。 */
	sys_write32((sys_read32(CMU_SPI0CLK) & ~(0x3 << 8) & ~(0xf << 0)) | 0x301, CMU_SPI0CLK);    /* SPI0 选 RC64M 源并写入原分频编码。 */

#ifdef CONFIG_SPI0_NOR_DTR_MODE
	spi0_delaychain_bak = sys_read32(SPI0_DELAYCHAIN);                                          /* 保存 NOR 当前采样延迟链。 */
	nor_dtr_set_matched_delaychain(950);                                                        /* 适配 950 mV 工作点的 NOR 延迟链。 */
#endif
	val = sys_read32(VOUT_CTL1_S1M);                                                            /* 保存当前 S1 电压控制字。 */
	sys_write32((val & ~(0xfF)) | 0x88, VOUT_CTL1_S1M);                                         /* 原电压配置备注为 0.95 V。 */

	soc_udelay(3);                                                                              /* 等待 CPU 与 SPI0 时钟切换稳定，单位微秒。 */

	/* 同时使能 RC64M、RC4M 和 HOSC；原硬件要求 S1/S3 转换时保留 HOSC，防止 VC18 无输出。 */
	sys_write32(0x7, CMU_S1CLKCTL);                                                             /* 保留三种振荡源，满足原硬件 S1/S3 转换限制。 */

#ifdef CONFIG_SLEEP_PRINTK_DEBUG
	/* 为 UART 调试保持 HOSC 可用。 */
	sys_set_bit(CMU_S1CLKCTL, 1);                                                               /* 调试 UART 需要 S1 HOSC。 */
	sys_set_bit(CMU_S3CLKCTL, 1);                                                               /* 调试 UART 需要 S3 HOSC。 */
#endif

	if (sleep_policy_is_pwrgat()) {                                                             /* CPU 掉电时必须留下启动恢复入口信息。 */
		sys_write32((uint32_t)__cpu_resume, RTC_REMAIN4);                                   /* 向 RTC 保留区写入 CPU 恢复入口地址。 */
		sys_write32(0xb5, RTC_REMAIN5);                                                     /* 写原掉电恢复标志 0xb5，供底层启动逻辑识别。 */
	}

	/* 保存看门狗与跟踪状态，再在受控中断屏蔽状态下进入 CPU 睡眠。 */
	wd_ctl = sys_read32(WD_CTL);                                                                /* 保存看门狗控制状态。 */
	reg_trace_bak = sys_read32(0xe0043004);                                                     /* 保存跟踪控制寄存器。 */

	/* 看门狗原本启用时，使其深睡眠时钟继续运行。 */
	if(sys_test_bit(WD_CTL, 4))                                                                 /* 只有原先已使能看门狗才打开深睡眠时钟。 */
		sys_set_bit(CMU_WDCLK, 1);                                                          /* 使看门狗在低功耗阶段继续计数。 */

	/* 进入休眠：PRIMASK 暂停普通中断，清 BASEPRI 后执行掉电入口或 DSB/WFI。 */
	__asm__ volatile("cpsid	i");                                                                /* 通过 PRIMASK 暂时阻止普通可屏蔽中断进入。 */
#ifdef CONFIG_DISABLE_IRQ_STAT
	n_irq_unlock(0);                                                                            /* 无统计包装分支清 BASEPRI，PRIMASK 仍保持锁定。 */
#else
	irq_unlock(0);                                                                              /* 普通包装分支清 BASEPRI，PRIMASK 仍保持锁定。 */
#endif

	if (sleep_policy_is_pwrgat()) {                                                             /* 支持 CPU 掉电时走汇编保存/恢复入口。 */
		__cpu_suspend();                                                                    /* 保存上下文并进入 CPU 掉电，恢复后继续此处。 */
	}
	else {
		__asm__ volatile("dsb");                                                            /* 确保休眠前的显式内存操作已完成。 */
		__asm__ volatile("wfi");                                                            /* 等待中断事件唤醒，当前等待无软件超时。 */
	}

#ifdef CONFIG_DISABLE_IRQ_STAT
	n_irq_lock();                                                                               /* 唤醒后重新设 BASEPRI 屏蔽门限。 */
#else
    irq_lock();                                                                                     /* 由普通包装重新锁定中断优先级门限。 */
#endif
	__asm__ volatile("cpsie	i");                                                                /* 解除 PRIMASK；受 BASEPRI 限制的中断仍不进入。 */

	/* 先保存 NVIC 挂起位，供后续判断与打印唤醒原因。 */

	/* 醒来后先留存来源快照，再调整时钟电压，避免后续处理改变诊断依据。 */
	nvic_save_pending_register();                                                               /* 在处理和清除中断前先保存真实挂起状态。 */

	/* 醒来后的低功耗处理暂关 HOSC。 */
	sys_write32(0x5, CMU_S1CLKCTL);                                                             /* 局部恢复阶段暂关 HOSC 降低功耗。 */

	sys_write32(wd_ctl, WD_CTL);                                                                /* 还原看门狗原控制。 */
	sys_write32(reg_trace_bak, 0xe0043004);                                                     /* 恢复跟踪控制寄存器。 */

#ifdef CONFIG_SLEEP_PRINTK_DEBUG
	/* 调试输出配置重新允许 UART 使用 HOSC。 */
	sys_set_bit(CMU_S1CLKCTL, 1);                                                               /* 调试配置再次开启 S1 HOSC。 */
	sys_set_bit(CMU_S3CLKCTL, 1);                                                               /* 调试配置再次开启 S3 HOSC。 */
#endif

#ifdef CONFIG_SPI0_NOR_DTR_MODE
	sys_write32(spi0_delaychain_bak, SPI0_DELAYCHAIN);                                          /* 还原 NOR 延迟链。 */
#endif

	/* 恢复局部算法执行所需电压和 RC64M 频率，随后重新开放对应外设时钟。 */
	sys_write32(val, VOUT_CTL1_S1M);                                                            /* 还原局部休眠前的电压控制字。 */
	soc_udelay(30);                                                                             /* 等待电压稳定，单位微秒。 */

	/* CPU 改用 RC64M，AHB 分频依芯片版本选择。 */
	if (soc_dvfs_opt())                                                                         /* 按芯片版本使用对应 RC64M/总线分频编码。 */
		sys_write32(0x03, CMU_SYSCLK);                                                      /* AHB 二分频。 */
	else
		sys_write32(0x203, CMU_SYSCLK);                                                     /* AHB 不分频。 */

	sys_write32(0x1, CMU_GPIOCLKCTL);                                                           /* GPIO 时钟恢复为 RC4M。 */

	/* 恢复 NOR 取指所需 SPI0/缓存时钟，以及心率采样用 IICMT0 时钟。 */
	sys_write32(devclk[0] | (1<<CLOCK_ID_SPI0CACHE) | (1<<CLOCK_ID_SPI0), CMU_DEVCLKEN0);       /* 恢复局部传感器处理所需 NOR 和缓存时钟。 */
	sys_write32(devclk[1] | (1<<(CLOCK_ID_I2CMT0 - 32)), CMU_DEVCLKEN1);                        /* 恢复心率数据采样用 IICMT0 时钟。 */

	if(sys_test_bit(WD_CTL, 4))                                                                 /* 看门狗启用时执行喂狗。 */
		sys_set_bit(WD_CTL, 0);                                                             /* 向看门狗喂狗控制位置位。 */

#ifdef CONFIG_SLEEP_WD_DIS_DEBUG
	sys_clear_bit(WD_CTL, 4);                                                                   /* 仅禁用看门狗调试配置撤销使能位。 */
#endif

	g_sleep_t2cnt = sys_read32(T2_CNT);                                                         /* 记录 T2 在时钟同步前的值，后续读取等待其变化。 */

}

/**
  * @brief  在受限内存环境分派唤醒回调，决定继续睡眠或完整恢复。
  * @param  irq_en_bit：[输入]至少 SLEEP_WKSRC_NUM 项的 SRAM 中断号表，不可为空。
  * @param  cb：[输出]至少 SLEEP_WKSRC_NUM 项的可写延后回调表，不可为空。
  * @param  cb_num：[输出]有效 int 指针，不可为空，写入实际暂存条目数。
  * @retval RUN_SYSTEM 表示需要完整恢复；SLEEP_AGAIN 表示可继续睡眠。
  * @note   回调按 wk_prep 声明的可访问内存环境执行；无 prepare 的回调延后。保存表有容量上限，超过时仍要求系统恢复但不继续入表。
  */
__sleepfunc static enum WK_CB_RC check_wk_run_sram_nor(uint16_t *irq_en_bit,
				struct sleep_wk_cb *cb, int *cb_num)
{
	/* 先检查源的允许位和挂起快照，再按每个回调要求的内存环境分派；无法局部运行的回调入延后表。 */
	int i;                                                                                     /* 依次扫描源编号。 */
	enum WK_CB_RC rc = WK_CB_SLEEP_AGAIN;                                                      /* 默认本轮可继续休眠，遇到必须系统处理的源再提升结果。 */
	enum WK_CB_RC rc_t ;                                                                       /* 暂存当前链表节点回调的结果。 */
	enum WK_RUN_TYPE runt;                                                                     /* 记录 prepare 回调声明的可用内存环境。 */
	uint32_t wkbit;                                                                            /* 当前源对应的 NVIC 中断号。 */
	bool b_nor_wk = false;                                                                     /* 记录本轮是否曾执行 NOR 环境回调。 */
	struct sleep_wk_fun_data *p;                                                               /* 遍历当前源注册的回调链表。 */
	*cb_num = 0;    /* 清空输出回调数量，避免沿用上轮结果。 */

	for(i = 0; i < SLEEP_WKSRC_NUM; i++){                                                      /* 扫描全部已知源，逐项检查是否允许且确实挂起。 */
		if(!((1 << i) & g_sleep_wksrc_en))                                                 /* 不在允许集合中的源跳过。 */
			continue;                                                                  /* 跳过不允许或未挂起源，继续下一项。 */

		wkbit = irq_en_bit[i];                                                             /* 从 SRAM 副本读取当前源 NVIC 编号。 */
		if(wkbit >= 32){                                                                   /* 高于 31 的中断从第二组挂起快照查询。 */
			if(!(wk_pd1 & (1<<(wkbit - 32))))                                          /* 第二组未挂起则无需执行此源回调。 */
				continue;                                                          /* 继续检查后一个源。 */

		}else{
			if(!(wk_pd0 & (1<<wkbit)))                                                 /* 第一组未挂起同样跳过。 */
				continue;                                                          /* 继续检查后一个源。 */
		}
		if(g_wk_fun[i]){                                                                   /* 存在注册链表时尝试局部处理。 */
			p = g_wk_fun[i];                                                           /* 从该源链表头开始遍历。 */
			do{
				if(p->wk_prep){                                                    /* 有 prepare 才能声明此回调可在受限环境执行。 */
					runt = p->wk_prep(i);//
					if(runt == WK_RUN_IN_SRAM){                                /* 只需 SRAM 的回调可直接执行。 */
						rc_t = p->wk_cb(i);                                /* 把当前源编号传给实际回调。 */
						//if(WK_CB_RUN_SYSTEM == p->wk_cb(i))
							//rc = WK_CB_RUN_SYSTEM;
					}else if (runt == WK_RUN_IN_NOR) {
						if(!b_nor_wk){                                     /* 首次处理 NOR 环境回调时记录使用状态。 */
							b_nor_wk = true;                           /* 本轮已使用 NOR，若再次睡眠需重新关闭 NOR。 */
						}
						rc_t = p->wk_cb(i);                                /* 在当前已恢复的 NOR 环境执行回调。 */
						//if(WK_CB_RUN_SYSTEM == p->wk_cb(i))
							//rc = WK_CB_RUN_SYSTEM;

					}else{
						rc_t = WK_CB_RUN_SYSTEM;                           /* 需要完整系统环境，先要求系统恢复。 */
						if(*cb_num < SLEEP_WKSRC_NUM){                     /* 只有延后表仍有空间才保存此回调。 */
							cb[*cb_num].wk_cb = p->wk_cb;              /* 保存恢复系统后要调用的函数指针。 */
							cb[*cb_num].src = i;                       /* 保存届时传入的源编号。 */
							(*cb_num)++;                               /* 增加已保存条目数。 */
						}
					}
					if(rc_t == WK_CB_SLEEP_AGAIN){                             /* 回调已消化本源且允许再次休眠时，清除对应挂起位。 */
						if(wkbit < 32)                                     /* 选择第一组或第二组 NVIC 清挂起寄存器。 */
							sys_write32(1<<wkbit, NVIC_ICPR0);         /* 第一组对应位写 1 清挂起。 */
						else
							sys_write32(1<<(wkbit-32), NVIC_ICPR1);    /* 第二组对应位写 1 清挂起。 */
					}

				}else{
					rc_t = WK_CB_RUN_SYSTEM;                                   /* 无 prepare 的回调不能假定 SRAM/NOR 环境足够。 */
					if(*cb_num < SLEEP_WKSRC_NUM){                             /* 延后表未满才保存该回调。 */
						cb[*cb_num].wk_cb = p->wk_cb;                      /* 保存完整恢复后执行的回调。 */
						cb[*cb_num].src = i;                               /* 关联当前源编号。 */
						(*cb_num)++;                                       /* 增加延后回调条目数。 */
					}
				}
				p = p->next;                                                       /* 沿调用者提供的 next 遍历下一节点。 */
				if(rc_t == WK_CB_RUN_SYSTEM)                                       /* 当前回调要求系统恢复时，将汇总结果置为 RUN_SYSTEM。 */
					rc = WK_CB_RUN_SYSTEM;                                     /* 任一节点要求完整系统，汇总结果保持 RUN_SYSTEM。 */
			}while(p);                                                                 /* 处理完当前源全部链表节点才退出。 */

			if(rc_t == WK_CB_CARELESS)                                                 /* 原注释声称检查所有回调，实际这里只检查链表最后一次 rc_t；最后回调不关心该源时交给系统处理。 */
					rc = WK_CB_RUN_SYSTEM;                                     /* 最后回调不关心该源时，交完整系统处理。 */

		}else{
			rc = WK_CB_RUN_SYSTEM;                                                     /* 没有注册回调的唤醒源交给完整系统处理。 */
		}

		if (wk_msg[i].wksrc == SLEEP_WK_SRC_IIC1MT) {                                      /* IICMT1 唤醒可能属于强制功耗测试的周期计数。 */
			rc = sys_update_wk_by_sleep_mode(rc);                                      /* 按强制模式次数覆盖或保留回调结果。 */
		}
	}

	if(rc == WK_CB_SLEEP_AGAIN){                                                               /* 所有处理允许再次睡眠时才做局部清理。 */
		//sys_write32(0xffffffff, NVIC_ICPR0);
		//sys_write32(0xffffffff, NVIC_ICPR1);
		if(b_nor_wk){                                                                      /* 若本轮使用过 NOR，入睡前重新请求其掉电。 */
			sys_norflash_power_ctrl(1);                                                /* 让 NOR 再次进入深度掉电。 */
		}
	}
	return rc;                                                                                 /* 向外层返回继续睡眠或恢复系统的决定。 */
}

#ifndef CONFIG_SOC_NO_PSRAM

/**
  * @brief  写回 SPI1 写缓冲和缓存并等待完成。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void soc_psram_flush(void)
{
	spi1_cache_ops(SPI_WRITEBUF_FLUSH, (void *)SPI1_BASE_ADDR, 0x1000);     /* 先写回 SPI1 写缓冲。 */
	spi1_cache_ops(SPI_CACHE_FLUSH_ALL, (void *)SPI1_BASE_ADDR, 0x1000);    /* 再请求写回 SPI1 全部缓存。 */
	spi1_cache_ops_wait_finshed();                                          /* 等待缓存操作完成后才允许关闭 PSRAM。 */
}
#endif

/**
  * @brief  保存外围资源，循环处理局部唤醒，直到需要恢复完整系统。
  * @param  无。
  * @retval 无。
  * @note   核心状态和可执行代码须位于保留 SRAM。局部唤醒可再次入睡，完整退出再恢复时钟、电压、缓存、DMA 和回调表；部分忙等无超时。
  */
__sleepfunc static void cpu_enter_sleep(void)
{
	/* 保存正常运行配置，临时切换低功耗时钟和供电；唤醒后可只运行局部回调并再次睡眠。 */
	uint32_t corepll_backup, sysclk_bak;                                                                             /* 保存 CPU 主时钟和 COREPLL 原配置。 */
	unsigned int i, num_cb;                                                                                          /* i 用于表/就绪轮询，num_cb 接收待延后回调数。 */
	uint16_t irq_en_bit[SLEEP_WKSRC_NUM];                                                                            /* SRAM 中的源到 IRQ 编号副本。 */
	struct sleep_wk_cb cb[SLEEP_WKSRC_NUM];                                                                          /* 暂存最多 SLEEP_WKSRC_NUM 个系统级回调。 */
	uint32_t bk_clksrc0, bk_clksrc1, bk_vout_s1;                                                                     /* 保存存储时钟源与 S1 电压配置。 */
	uint32_t devclk[2], memclk[2];                                                                                   /* 保存两组设备时钟及两组存储器时钟。 */
#ifdef CONFIG_SPI0_NOR_DTR_MODE
	uint32_t bk_spi0_delaychain;                                                                                     /* NOR DTR 配置保存正常工作延迟链。 */
#endif

	devclk[0] = sys_read32(CMU_DEVCLKEN0);                                                                           /* 保存第一组正常设备时钟。 */
	devclk[1] = sys_read32(CMU_DEVCLKEN1);                                                                           /* 保存第二组正常设备时钟。 */
	memclk[0] = sys_read32(CMU_MEMCLKEN0);                                                                           /* 保存第一组存储器时钟。 */
	memclk[1] = sys_read32(CMU_MEMCLKEN1);                                                                           /* 保存第二组存储器时钟。 */

	/* 先保存 UART、缓存、NVIC 和 DMA 的正常运行状态，再关闭不保留的 SRAM。 */
	uart_suspend();                                                                                                  /* 保存 UART 状态，供局部或最终恢复。 */
	icache_backup_registers();                                                                                       /* 保存 NOR/SPI0 指令缓存状态。 */
	dcache_backup_registers();                                                                                       /* 保存 PSRAM/SPI1 数据缓存状态。 */
	nvic_backup_registers();                                                                                         /* 保存 NVIC 优先级与异常向量地址。 */
	dma_backup_registers();                                                                                          /* 保存通用 DMA 状态。 */
	sdma_backup_registers();                                                                                         /* 保存传感器 DMA 状态。 */
	sys_pwrctrl_assigned_sram(true);                                                                                 /* 关闭链接器指定的完整 SRAM 块。 */

	/* CMU_MEMCLKEN1 只保留蓝牙 ROM/RAM 的时钟。 */
	sys_write32(memclk[1] & BTROM_RAM_CLK_EN, CMU_MEMCLKEN1);                                                        /* 该组存储器时钟仅保留蓝牙 ROM/RAM。 */

#ifndef CONFIG_SOC_NO_PSRAM
	/* 先写回 SPI1 缓存，避免关闭 PSRAM 前丢失脏数据。 */
	soc_psram_flush();                                                                                               /* PSRAM 存在时先完成脏数据写回。 */
#ifdef CONFIG_SLEEP_MEMORY_CHECK_INTEGRITY
	suspend_check();                                                                                                 /* 可选地记录休眠前内存累加和。 */
#endif
	/* 关闭 PSRAM 的 SPI1 缓存。 */
	sys_clear_bit(SPI1_CACHE_CTL, 0);                                                                                /* 写回完成后关闭 PSRAM 缓存。 */
#endif

	for(i = 0; i < SLEEP_WKSRC_NUM; i++)                                                                             /* 把 NOR 中的唤醒中断号表复制到 SRAM，休眠处理阶段不依赖 NOR 表访问。 */
		irq_en_bit[i] = wk_msg[i].irq_en_bit;                                                                    /* 逐项复制对应 IRQ 编号到当前 SRAM 栈表。 */

#if	defined(CONFIG_BOARD_EMMCBOOT) || defined(CONFIG_BOARD_NANDBOOT)
	uint32_t spi1_clk, ddr_mode;                                                                                     /* eMMC/NAND 启动分支保存 PSRAM 时钟及读延迟配置。 */
	sys_set_bit(CMU_S1CLKCTL,2);                                                                                     /* 为 PSRAM 使能 RC64M。 */
	spi1_clk =	sys_read32(CMU_SPI1CLK);                                                                         /* 保存 SPI1 原时钟配置。 */
	ddr_mode =	sys_read32(SPI1_DDR_MODE_CTL);                                                                   /* 保存 SPI1 DDR 模式读延迟等字段。 */
	if(!soc_boot_is_mini())                                                                                          /* 非 mini 启动才调整 RLC 读延迟。 */
		sys_write32((ddr_mode & ~(0xf << 16)) | (4<<16), SPI1_DDR_MODE_CTL);                                     /* SPI1 改为 64 MHz 时将 RLC 读延迟字段设为 4，原默认值为 5。 */
	sys_write32(0x300, CMU_SPI1CLK);                                                                                 /* PSRAM 时钟改用 RC64M。 */
#endif

	/* 让 NOR 进入深度掉电。 */
	sys_norflash_power_ctrl(1);                                                                                      /* 请求 NOR 深度掉电，后续关键代码须可在保留区执行。 */

	sleep_stage(0);                                                                                                  /* 写入休眠调试阶段 0，未启用调试时为空调用。 */

	/* 让 PSRAM 进入低功耗自刷新。 */
	psram_self_refresh_control(true);                                                                                /* 使 PSRAM 进入自刷新状态。 */

	/* 让 PSRAM 进入低功耗供电状态。 */
	psram_power_control(true);                                                                                       /* 切入 PSRAM 低功耗状态。 */

	/* CPU 深睡眠前固定 PSRAM 片选引脚状态。 */
	pwrgat_gpio_psram_backup_registers();                                                                            /* 固定 PSRAM 片选，避免睡眠期间误操作。 */

	sysclk_bak =  sys_read32(CMU_SYSCLK);                                                                            /* 保存主时钟源和分频设置。 */

	/* 先把 CPU 主时钟源切为 HOSC。 */
	sys_write32((sysclk_bak&(~0x7)) | 0x1, CMU_SYSCLK);                                                              /* 保持其余字段，先把 CPU 时钟源改到 HOSC。 */
	soc_udelay(1);                                                                                                   /* 等待时钟切换稳定 1 微秒。 */
	/* 再按芯片版本设置 HOSC 对应的总线分频。 */
	if (soc_dvfs_opt())                                                                                              /* 依芯片版本选择正确总线分频。 */
		sys_write32(0x1, CMU_SYSCLK);                                                                            /* AHB 二分频。 */
	else
		sys_write32(0x201, CMU_SYSCLK);                                                                          /* AHB 不分频。 */

	corepll_backup = sys_read32(COREPLL_CTL);                                                                        /* 保存 COREPLL 参数以便最终恢复。 */
	bk_vout_s1 = sys_read32(VOUT_CTL1_S1M);                                                                          /* 保存正常 S1 电压设置。 */

	/* 撤销 COREPLL 使能并清控制寄存器。 */
	sys_write32(sys_read32(COREPLL_CTL) & ~(1 << 7), COREPLL_CTL);                                                   /* 先撤销 COREPLL 的使能位。 */
	sys_write32(0, COREPLL_CTL);                                                                                     /* 清 COREPLL 控制。 */

	/* 关闭 AVDD；恢复 COREPLL 以前必须重新开启。 */
	sys_clear_bit(AVDDLDO_CTL, 0);                                                                                   /* 关闭 PLL 不再使用的模拟供电。 */


	/* 以下实际写入已采用 0.95 V 相关配置；原先“以后改为 0.95 V”的备注已过时。 */
#ifdef CONFIG_SPI0_NOR_DTR_MODE
	/* 保存正常 S1 工作阶段的 NOR 延迟链。 */
	bk_spi0_delaychain = sys_read32(SPI0_DELAYCHAIN);                                                                /* 保存正常 NOR DTR 采样延迟链。 */
	/* 设置深睡眠期间运行传感器算法所用延迟链。 */
	nor_dtr_set_matched_delaychain(950);                                                                             /* 使用低压 950 mV 工作点匹配值。 */
	/* 将该延迟链同步到缓存恢复备份中。 */
	nor_dtr_backup_delaychain();                                                                                     /* 同步低压延迟链到局部缓存恢复备份。 */
#endif
	//sys_write32((sys_read32(VOUT_CTL1_S1M) & ~(0xff)) | 0xaa, VOUT_CTL1_S1M);
	sys_write32((sys_read32(VOUT_CTL1_S1M) & ~(0xfff)) | 0x988, VOUT_CTL1_S1M);                                      /* 原配置备注：VDD_S1M 为 0.95 V，VD12_S1M 为 0.95+0.1 V。 */
	soc_udelay(30);                                                                                                  /* 等待电压稳定，单位微秒。 */

	pwrgat_gpio_backups[3] = sys_read32(GPION_CTL(DEBUG_UART_TX_PIN));                                               /* 保存调试 UART TX 引脚原配置。 */
	pwrgat_gpio_backups[4] = sys_read32(GPION_CTL(DEBUG_UART_RX_PIN));                                               /* 保存调试 UART RX 引脚原配置。 */

	bk_clksrc0 = sys_read32(CMU_MEMCLKSRC0);                                                                         /* 保存第一组存储器时钟源。 */
	bk_clksrc1 = sys_read32(CMU_MEMCLKSRC1);                                                                         /* 保存第二组存储器时钟源。 */

	/* 每轮睡眠后先恢复最小执行环境并检查回调；需要完整系统时退出，否则重置唤醒位继续休眠。 */
	while(1) {                                                                                                       /* 每次唤醒先尝试局部处理，只有要求完整系统才退出循环。 */
		/* S2 期间共享 RAM4 时钟改为 RC4M。 */
		sys_write32((bk_clksrc0 & ~(7 << 25)) | (0 << 25), CMU_MEMCLKSRC0);                                      /* 共享 RAM 选择 RC4M。 */

		pwrgat_gpio_nor_backup_registers();                                                                      /* 固定 NOR 片选并保存原配置。 */
		pwrgat_set_unused_uart_highz();                                                                          /* 按模式将不用的调试 UART 置高阻。 */

		sleep_stage(1);                                                                                          /* 记录即将进入底层睡眠的阶段。 */
		__cpu_enter_sleep();                                                                                     /* 实际执行 CPU 等待/掉电与最小资源恢复。 */
		sleep_stage(2);                                                                                          /* 记录刚从底层睡眠返回的阶段。 */

		sys_write32((bk_clksrc0 & ~(7 << 25)) | (4 << 25), CMU_MEMCLKSRC0);                                      /* 唤醒处理阶段共享 RAM 改为 RC64M。 */
		//sys_write32((bk_clksrc1 & (~0xe)) | (0x4 << 1), CMU_MEMCLKSRC1); /* RC64M 及 ANX RAM */

		sleep_stage(3);                                                                                          /* 记录开始局部唤醒处理的阶段。 */

		pwrgat_gpio_nor_restore_registers();                                                                     /* 恢复 NOR 片选复用。 */
		icache_restore_registers();                                                                              /* 恢复 SPI0 缓存及映射以恢复 NOR 取指。 */
		sys_norflash_power_ctrl(0);                                                                              /* 唤醒 NOR 芯片。 */
		if (sleep_policy_is_pwrgat()) {                                                                          /* CPU 掉电会丢缓存状态，需使旧缓存内容失效。 */
			sys_write32(1, SPICACHE_INVALIDATE);                                                             /* 请求 SPI0 全缓存失效。 */
			while((sys_read32(SPICACHE_INVALIDATE) & 0x1) == 1);                                             /* 轮询失效完成，无超时。 */
		}
	#if	defined(CONFIG_BOARD_EMMCBOOT) || defined(CONFIG_BOARD_NANDBOOT)
		sys_write32(sys_read32(CMU_DEVCLKEN0) | (1<<CLOCK_ID_SPI1CACHE) | (1<<CLOCK_ID_SPI1), CMU_DEVCLKEN0);    /* eMMC/NAND 启动分支恢复 SPI1 和数据缓存时钟。 */
		pwrgat_gpio_psram_restore_registers();                                                                   /* 恢复 PSRAM 片选复用。 */
		dcache_restore_registers();                                                                              /* 恢复 SPI1/PSRAM 控制。 */
		sys_set_bit(CMU_S1CLKCTL,2);                                                                             /* 为 PSRAM 重新使能 RC64M。 */
		if(!soc_boot_is_mini())                                                                                  /* 非 mini 启动调整低压工作读延迟。 */
			sys_write32((ddr_mode & ~(0xf << 16)) | (4<<16), SPI1_DDR_MODE_CTL);                             /* SPI1 使用 64 MHz 时，RLC 读延迟设为 4，保留原适配值。 */
		sys_write32(0x300, CMU_SPI1CLK);                                                                         /* PSRAM 使用 RC64M。 */

		psram_power_control(false);                                                                              /* 恢复 PSRAM 以运行传感器算法。 */
	#endif
		arm_floating_point_init();                                                                               /* 恢复局部算法运行所需浮点环境。 */

		/* 传感器算法开始前恢复异常向量偏移，便于定位算法故障，避免进入 BROM 故障处理后冲刷 MTB 跟踪区。 */
		nvic_restore_registers();                                                                                /* 在算法回调前恢复异常向量表及优先级。 */

	#ifdef CONFIG_SLEEP_PRINTK_DEBUG
		pwrgat_gpio_uart_restore_registers();                                                                    /* 调试配置恢复主 CPU UART 引脚。 */
		uart_resume();                                                                                           /* 调试配置恢复 UART 控制。 */
	#endif

		if (WK_CB_RUN_SYSTEM == check_wk_run_sram_nor(irq_en_bit, cb, &num_cb)) {                                /* 局部回调要求完整系统时结束重睡循环。 */
			sleep_stage(4);                                                                                  /* 记录即将完整恢复的阶段。 */
			break;                                                                                           /* 停止重复入睡，转入完整资源恢复。 */
		}
		else {
			sys_restore_wksrc();                                                                             /* 准备再次睡眠时恢复允许唤醒的 IRQ 配置。 */
			SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;                                                               /* 重新设置 ARM 深睡眠标志。 */
		}
	#if	defined(CONFIG_BOARD_EMMCBOOT) || defined(CONFIG_BOARD_NANDBOOT)
		psram_power_control(true);                                                                               /* 准备继续休眠时再次挂起 PSRAM。 */
		/* 再次固定 PSRAM 片选状态。 */
		pwrgat_gpio_psram_backup_registers();                                                                    /* 再次休眠前固定 PSRAM 片选。 */
	#endif
	}

	/* 退出重睡循环，开始完整系统恢复。 */
	sleep_stage(5);                                                                                                  /* 记录进入完整系统恢复阶段。 */

	/* 完整系统恢复时还原所有存储器时钟源。 */
	sys_write32(bk_clksrc0, CMU_MEMCLKSRC0);                                                                         /* 还原第一组存储器时钟源。 */
	sys_write32(bk_clksrc1, CMU_MEMCLKSRC1);                                                                         /* 还原第二组存储器时钟源。 */

	/* 开启 HOSC、RC4M、RC64M。 */
	sys_write32(0x7, CMU_S1CLKCTL);                                                                                  /* 恢复三种基础振荡源使能。 */

	/* 恢复正常设备时钟与存储器时钟。 */
	sys_write32(devclk[0], CMU_DEVCLKEN0);                                                                           /* 还原正常第一组设备时钟。 */
	sys_write32(devclk[1], CMU_DEVCLKEN1);                                                                           /* 还原正常第二组设备时钟。 */
	sys_write32(memclk[0], CMU_MEMCLKEN0);                                                                           /* 还原正常第一组存储器时钟。 */
	sys_write32(memclk[1], CMU_MEMCLKEN1);                                                                           /* 还原正常第二组存储器时钟。 */

	pwrgat_gpio_psram_restore_registers();                                                                           /* 恢复 PSRAM 片选。 */
	pwrgat_gpio_uart_restore_registers();                                                                            /* 恢复调试 UART 引脚。 */
	dcache_restore_registers();                                                                                      /* 恢复数据缓存和 PSRAM 控制。 */
	uart_resume();                                                                                                   /* 恢复 UART 控制与波特率。 */
	dma_restore_registers();                                                                                         /* 恢复 DMA 配置及启动顺序。 */
	sdma_restore_registers();                                                                                        /* 恢复传感器 DMA。 */

	__DSB();                                                                                                         /* 等待显式内存访问完成，保证恢复写入已提交。 */
	__ISB();                                                                                                         /* 同步指令执行环境，使后续按新系统状态取指。 */

#ifndef CONFIG_SOC_NO_PSRAM
	/* 重新使能 SPI1 缓存。 */
	sys_set_bit(SPI1_CACHE_CTL, 0);                                                                                  /* 在相应配置下重新使能 SPI1 缓存。 */
#endif

#ifdef CONFIG_SPI0_NOR_DTR_MODE
	/* 还原正常 S1 工作阶段的 NOR 延迟链。 */
	sys_write32(bk_spi0_delaychain, SPI0_DELAYCHAIN);                                                                /* 还原正常 NOR DTR 延迟链。 */
#endif
	/* 恢复休眠前的供电电压设置。 */
	sys_write32(bk_vout_s1, VOUT_CTL1_S1M);                                                                          /* 还原正常 S1 供电配置。 */
	soc_udelay(30);                                                                                                  /* 等待电压稳定，单位微秒。 */

	/* 为恢复 PLL 重新开启 AVDD。 */
	sys_write32(((sys_read32(AVDDLDO_CTL)                                                                            /* 撤销 AVDD 掉电并允许模拟供电，为 COREPLL 恢复做准备。 */
				& (~(1<<AVDDLDO_CTL_AVDD_PD_EN)))
				| (1<<AVDDLDO_CTL_AVDD_EN)), AVDDLDO_CTL);
	soc_udelay(1);                                                                                                   /* 等待模拟供电稳定 1 微秒。 */

	/* 退出 PSRAM 低功耗供电状态。 */
	psram_power_control(false);                                                                                      /* 恢复 PSRAM 供电工作状态。 */

	/* 有界轮询基础时钟就绪；达到 300 次仍继续原恢复流程，不新增错误处理。 */
	for(i = 0; i < 300; i++){                                                                                        /* HOSC 最多轮询 300 次，每次未就绪等待 5 微秒。 */
		if(sys_read32(HOSC_CTL) & HOSC_CTL_READY_MASK)                                                           /* 检测 HOSC_READY 位，成功即提前退出。 */
			break;                                                                                           /* HOSC 就绪，不再继续等待。 */
		soc_udelay(5);                                                                                           /* 本次尚未就绪，延时 5 微秒后重试。 */
	}
	if (!soc_dvfs_opt())                                                                                             /* 旧芯片版本另需额外稳定等待。 */
		soc_udelay(200);                                                                                         /* 额外等待 200 微秒，保持原硬件时序。 */

	sys_write32(corepll_backup, COREPLL_CTL);                                                                        /* 写回休眠前 COREPLL 配置。 */
	/* SPI0 时钟源先切回 HOSC。 */
	sys_write32(sys_read32(CMU_SPI0CLK) & ~(0x3 << 8) & ~(0xf << 0), CMU_SPI0CLK);                                   /* 先保证 NOR 时钟可由 HOSC 提供。 */

	/* 有界轮询基础时钟就绪；达到 300 次仍继续原恢复流程，不新增错误处理。 */
	for(i = 0; i < 300; i++){                                                                                        /* COREPLL 最多轮询 300 次，超限仍沿用原流程继续。 */
		if(sys_read32(COREPLL_CTL) & (1<<8))                                                                     /* 检测 PLL 控制 bit8 的就绪状态。 */
			break;                                                                                           /* PLL 已就绪，提前结束轮询。 */
		soc_udelay(5);                                                                                           /* 尚未就绪时等待 5 微秒再检查。 */
	}

	check_cnt = i;                                                                                                   /* 保存实际检查次数供调试打印。 */

	/* 恢复主时钟前先还原总线分频字段。 */
	sys_write32((sys_read32(CMU_SYSCLK)&0x7) | (sysclk_bak & (~0x7)) , CMU_SYSCLK);                                  /* 保持当前源字段，先恢复原总线分频等字段。 */
	soc_udelay(1);                                                                                                   /* 等待分频切换稳定 1 微秒。 */
	sys_write32(sysclk_bak, CMU_SYSCLK);                                                                             /* 再恢复原主时钟源及全部配置。 */

	psram_self_refresh_control(false);                                                                               /* 退出 PSRAM 自刷新。 */

#if	defined(CONFIG_BOARD_EMMCBOOT) || defined(CONFIG_BOARD_NANDBOOT)
	sys_write32(spi1_clk, CMU_SPI1CLK);                                                                              /* 还原 PSRAM 的 SPI1 时钟。 */
	sys_write32(ddr_mode, SPI1_DDR_MODE_CTL);                                                                        /* 恢复正常 SPI1 DDR 模式控制。 */
#endif

#ifndef CONFIG_SOC_NO_PSRAM
	/* 按原顺序清置 SPI1 的 FIFO 控制位，复位接收路径。 */
	sys_clear_bit(SPI1_CTL, 4);                                                                                      /* 清 SPI1 接收 FIFO 控制位，启动原复位序列。 */
	sys_clear_bit(SPI1_CTL, 5);                                                                                      /* 清 SPI1 发送 FIFO 控制位。 */
	soc_udelay(1);                                                                                                   /* 维持复位间隔 1 微秒。 */
	sys_set_bit(SPI1_CTL, 4);                                                                                        /* 重新置接收 FIFO 控制位。 */
	sys_set_bit(SPI1_CTL, 5);                                                                                        /* 重新置发送 FIFO 控制位。 */

	/* 使 SPI1 缓存失效，避免读取休眠前旧内容。 */
	spi1_cache_ops(SPI_CACHE_INVALID_ALL, (void *)SPI1_BASE_ADDR, 0x1000);                                           /* 使 PSRAM 缓存失效，避免旧缓存遮蔽真实内存内容。 */
#ifdef CONFIG_SLEEP_MEMORY_CHECK_INTEGRITY
	resume_check();                                                                                                  /* 恢复后计算可选内存完整性累加和。 */
#endif
#endif

	/* 所有运行资源恢复后重新开放完整 SRAM 块，并将延后回调交给外层。 */
	sys_pwrctrl_assigned_sram(false);                                                                                /* 重新打开此前关闭的完整 SRAM 块。 */

	current_sleep->g_num_cb = num_cb;                                                                                /* 保存需要完整系统环境的回调数量。 */
	/* 按有效数量复制回调表，供上层 soc_cmu_sleep_exit 后处理。 */
	memcpy(current_sleep->g_syste_cb, cb,
			num_cb * sizeof(struct sleep_wk_cb));

}


#ifdef CONFIG_SLEEP_PRINTK_DEBUG
/* 当前配置不切换 UART 引脚，原参数也不求值。 */
#define soc_enter_sleep_switch_uart(x)		NULL
#else
#ifdef CONFIG_SLEEP_UART_SILENT
/* 当前配置不切换 UART 引脚，原参数也不求值。 */
#define soc_enter_sleep_switch_uart(x)		NULL
#else
/* 蓝牙掉电时 TX 被隔离为 0，TX 不加上拉以避免额外电流；保留 UART 引脚复用配置。 */
static const struct acts_pin_config bt_pin_cfg_uart[] = {    /* 调试引脚切给蓝牙 UART 的复用/驱动配置，TX 不设置上拉。 */
	{28, 23 | GPIO_CTL_PADDRV_LEVEL(1)},
	{29, 23 | GPIO_CTL_PADDRV_LEVEL(1) | GPIO_CTL_PULLUP},
};

static const struct acts_pin_config leopard_pin_cfg_uart[] = {    /* 调试引脚切回主 CPU UART 的复用、驱动和上拉配置。 */
	{28, 5 | GPIO_CTL_PADDRV_LEVEL(1) | GPIO_CTL_PULLUP},
	{29, 5 | GPIO_CTL_PADDRV_LEVEL(1) | GPIO_CTL_PULLUP},
};

/**
  * @brief  将调试引脚复用切给蓝牙或主 CPU UART。
  * @param  bt_uart：[输入]true 选择蓝牙 UART，false 选择主 CPU UART。
  * @retval 无。
  * @note   调试常开或静默配置下由同名空操作宏替代；保留驱动强度和上下拉。
  */
static void soc_enter_sleep_switch_uart(bool bt_uart)
{
	//if(current_sleep->sleep_mode)
	//	return;

	if (bt_uart) {                                                                             /* true 时选择蓝牙 UART 配置。 */
		acts_pinmux_setup_pins(bt_pin_cfg_uart, ARRAY_SIZE(bt_pin_cfg_uart));              /* 按数组项数设置蓝牙 UART 的两个引脚。 */
	} else {
		acts_pinmux_setup_pins(leopard_pin_cfg_uart, ARRAY_SIZE(leopard_pin_cfg_uart));    /* 否则设置主 CPU UART 的两个引脚。 */
	}
}
#endif    /* CONFIG_SLEEP_UART_SILENT 分支结束。 */
#endif    /* CONFIG_SLEEP_PRINTK_DEBUG 分支结束（原结束注释误写为 TIMER_DEBUG）。 */

/**
  * @brief  在调试配置下打印 GPIO、时钟、PMU 和中断状态。
  * @param  promt：[输入]有效零结尾输出前缀，开启打印时不可为空。
  * @retval 无。
  * @note   CONFIG_SLEEP_DUMP_INFO 未启用时不输出；日志字符串保留原文。
  */
void dump_reg(const char *promt)
{
#ifdef CONFIG_SLEEP_DUMP_INFO
	printk("%s LOSC_CTL=0x%x,sleep_mode=%d\n", promt, sys_read32(LOSC_CTL), current_sleep->sleep_mode);    /* 打印调用点前缀、低速晶振状态与策略值。 */

	int i;                                                                                                 /* 用于遍历全部 77 个 GPIO。 */
	for (i = 0; i < 77 ; i++)                                                                              /* 输出全部 0～76 号 GPIO 配置，包含 NOR 等引脚。 */
		printk("gpio%d=0x%x\n", i, sys_read32(GPION_CTL(i)));                                          /* 输出该 GPIO 的控制寄存器原值。 */
	printk("RMU_MRCR0=0x%x\n", sys_read32(RMU_MRCR0));                                                     /* 输出第一组外设复位状态寄存器。 */
	printk("RMU_MRCR1=0x%x\n", sys_read32(RMU_MRCR1));                                                     /* 输出第二组外设复位状态寄存器。 */
	printk("CMU_DEVCLKEN0=0x%x\n", sys_read32(CMU_DEVCLKEN0));                                             /* 输出第一组外设时钟使能寄存器。 */
	printk("CMU_DEVCLKEN1=0x%x\n", sys_read32(CMU_DEVCLKEN1));                                             /* 输出第二组外设时钟使能寄存器。 */
	printk("PMU_DET=0x%x\n", sys_read32(PMU_DET));                                                         /* 输出PMU 检测状态寄存器。 */
	printk("NVIC_ISPR0=0x%x\n", sys_read32(NVIC_ISPR0));                                                   /* 输出第一组中断挂起状态寄存器。 */
	printk("NVIC_ISPR1=0x%x\n", sys_read32(NVIC_ISPR1));                                                   /* 输出第二组中断挂起状态寄存器。 */
	printk("CMU_MEMCLKEN0=0x%x\n", sys_read32(CMU_MEMCLKEN0));                                             /* 输出第一组存储器时钟使能寄存器。 */
	printk("CMU_MEMCLKEN1=0x%x\n", sys_read32(CMU_MEMCLKEN1));                                             /* 输出第二组存储器时钟使能寄存器。 */
	printk("CMU_MEMCLKSRC0=0x%x\n", sys_read32(CMU_MEMCLKSRC0));                                           /* 输出第一组存储器时钟源寄存器。 */
	printk("CMU_MEMCLKSRC1=0x%x\n", sys_read32(CMU_MEMCLKSRC1));                                           /* 输出第二组存储器时钟源寄存器。 */
	printk("PWRGATE_DIG=0x%x\n", sys_read32(PWRGATE_DIG));                                                 /* 输出数字外设掉电门控寄存器。 */
	printk("VOUT_CTL1_S2=0x%x\n", sys_read32(VOUT_CTL1_S2));                                               /* 输出S2 供电配置寄存器。 */
	printk("VOUT_CTL1_S3=0x%x\n", sys_read32(VOUT_CTL1_S3));                                               /* 输出S3 供电配置寄存器。 */
	printk("PWRGATE_RAM=0x%x\n", sys_read32(PWRGATE_RAM));                                                 /* 输出RAM 掉电控制寄存器。 */
	printk("WIO0_CTL=0x%x\n", sys_read32(WIO0_CTL));                                                       /* 输出WIO0 引脚控制寄存器。 */
	printk("WIO1_CTL=0x%x\n", sys_read32(WIO1_CTL));                                                       /* 输出WIO1 引脚控制寄存器。 */
	printk("WIO2_CTL=0x%x\n", sys_read32(WIO2_CTL));                                                       /* 输出WIO2 引脚控制寄存器。 */
	printk("WIO3_CTL=0x%x\n", sys_read32(WIO3_CTL));                                                       /* 输出WIO3 引脚控制寄存器。 */
#endif
}

/**
  * @brief  读取休眠期间累计的 HOSC 等效周期补偿。
  * @param  无。
  * @retval 按 32 位无符号数累计的补偿周期，会回绕。
  * @note   不是当前 T2 原始值，也不是毫秒。soc_timer_exit_sleep 按 soc_rc32K_mutiple_hosc 乘 RC32K 计数差累加；系统定时器用它修正周期读数。
  */
unsigned int soc_sleep_cycle(void)
{
	return current_sleep->g_sleep_cycle;    /* 返回累计 HOSC 等效补偿，不访问 T2 寄存器。 */
}

/**
  * @brief  读取包含深睡眠补偿的系统运行时间。
  * @param  无。
  * @retval 累计运行毫秒数。
  * @note   休眠阶段用预存 uptime 加累计休眠时间及当前 RC32K 差值；正常阶段用 k_uptime_get 加补偿。g_sleep_st 为 0 用作非休眠标志，计数恰为 0 的边界沿用原实现；等待 T2 同步没有超时。
  */
 __sleepfunc int64_t soc_sys_uptime_get(void)
{
	if(g_sleep_st){                                                  /* 休眠计时起点非零时走 RC32K 差值路径。 */
		unsigned int ms;                                         /* 保存本次休眠已经过的整数毫秒数。 */
		while(g_sleep_t2cnt == sys_read32(T2_CNT));              /* 等待 T2 计数变化以越过时钟同步，没有超时。 */
		ms =  rc32k_cyc_to_ms(sys_read32(T2_CNT)-g_sleep_st);    /* 取当前 T2 与休眠起点的无符号差并换算毫秒。 */
		return (g_sleep_update_time+g_sleep_ms) + ms;            /* 进入前 uptime、历史休眠补偿与本次已过毫秒相加。 */
	}else{
		return (k_uptime_get()+g_sleep_ms);                      /* 非休眠时用内核运行毫秒数加历史休眠补偿。 */
	}
}

/**
  * @brief  记录系统毫秒基准并把 TIMER2 切到 RC32K。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void soc_timer_sleep_prepare(void)
{
	/* 进入深睡眠前保存正常时间基准，再关闭 TIMER2 时钟以安全切换源。 */
	g_sleep_update_time = k_uptime_get();              /* 在切低速源前保存内核 uptime 基准。 */
	acts_clock_peripheral_disable(CLOCK_ID_TIMER2);    /* 切换时钟源之前先关闭外设时钟。 */
	sys_write32(0x4, CMU_TIMER2CLK);                   /* S3 之前把 TIMER2 时钟切为 RC32K。 */
	g_sleep_st = sys_read32(T2_CNT);                   /* 记录 TIMER2 在休眠时钟切换附近的计数起点。 */
	acts_clock_peripheral_enable(CLOCK_ID_TIMER2);     /* 重新开 TIMER2，使其按 RC32K 继续计数。 */
}

/**
  * @brief  恢复 TIMER2 的 HOSC 时钟并累计休眠时间与周期补偿。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void soc_timer_exit_sleep(void)
{
	/* RC32K 差值分别换算为 HOSC 等效周期和毫秒，服务两个不同的时间读取接口。 */
	unsigned int end;                                                /* 暂存结束计数，随后复用为计数差。 */
	acts_clock_peripheral_disable(CLOCK_ID_TIMER2);                  /* 切换时钟源之前先关闭外设时钟。 */
	end =  sys_read32(T2_CNT);                                       /* 关闭 TIMER2 时钟后读取结束计数。 */
	sys_write32(0x0, CMU_TIMER2CLK);                                 /* TIMER2 恢复 HOSC，继续供 k_cycle_get 使用。 */
	acts_clock_peripheral_enable(CLOCK_ID_TIMER2);                   /* 重新开 TIMER2，使其按 HOSC 计数。 */
	dump_stage_timer(g_sleep_st, end);                               /* 调试配置下打印本轮 RC32K 时间差。 */

	/* 先算低速计数差，再按各接口的单位分别累加周期和毫秒补偿。 */
	end = end - g_sleep_st;                                          /* 以无符号减法求本轮低速计数差。 */
	current_sleep->g_sleep_cycle += soc_rc32K_mutiple_hosc()*end;    /* 按 RC32K 到 HOSC 的倍数换算并累加补偿周期，32 位会回绕。 */
	g_sleep_ms += rc32k_cyc_to_ms(end);                              /* 同时累加休眠毫秒数，供 soc_sys_uptime_get 使用。 */
	g_sleep_st = 0;                                                  /* 清零起点标志，后续不再走正在休眠的计时路径。 */
}

static uint8_t g_lcd_aod_mode;    /* 常亮显示软件状态，初始化为关闭。 */

/**
  * @brief  设置常亮显示模式并同步 RTC 唤醒允许状态。
  * @param  is_aod：[输入]非零启用 AOD，0 关闭。
  * @retval 无。
  * @note   AOD 用 0/1 保存；关闭时也清除 RTC 唤醒位，调用方须协调其他 RTC 用途。
  */
void soc_set_aod_mode(int is_aod)
{
	g_lcd_aod_mode = is_aod ? 1 : 0;               /* 把外部任意非零输入归一为 1。 */
	if (is_aod) {                                  /* AOD 启用时需 RTC 定时唤醒。 */
		sys_s3_wksrc_set(SLEEP_WK_SRC_RTC);    /* 将 RTC 加入软件唤醒允许集合。 */
	} else {
		sys_s3_wksrc_clr(SLEEP_WK_SRC_RTC);    /* AOD 关闭时清除 RTC 唤醒允许位。 */
	}
}

/**
  * @brief  读取常亮显示模式标志。
  * @param  无。
  * @retval 1 表示启用，0 表示关闭。
  * @note   返回软件状态，不读取显示硬件。
  */
int soc_get_aod_mode(void)
{
	return g_lcd_aod_mode;    /* 读取归一后的 AOD 状态。 */
}

/**
  * @brief  选择休眠策略并按需暂停唤醒锁管理。
  * @param  mode：[输入]模式 0～3，超出时钳制为 FORCE_SLEEP。
  * @retval 无。
  * @note   非普通模式禁用唤醒锁，恢复动作在特殊模式退出流程，不在简单设置 NORMAL 时立即执行。
  */
void soc_set_sleep_mode(uint8_t mode)
{
	if(mode > SLEEP_MODE_FORCE_SLEEP)                        /* 超过支持上限的模式按强制测试处理。 */
		mode = SLEEP_MODE_FORCE_SLEEP;                   /* 钳制为 SLEEP_MODE_FORCE_SLEEP。 */
	current_sleep->sleep_mode = mode;                        /* 保存后续休眠准备所用策略。 */
	printk("sleep mode=%d\n", current_sleep->sleep_mode);    /* 输出实际保存的模式值。 */
	
	if (mode != SLEEP_MODE_NORMAL) {                         /* 特殊模式需要绕过普通唤醒锁约束。 */
#ifdef CONFIG_SYS_WAKELOCK
		sys_wakelocks_enable(0);                         /* 临时禁用系统唤醒锁，退出特殊模式后再恢复。 */
#endif
	}
}

/**
  * @brief  按 TIMER2 休眠起点标志判断是否处于补偿计时阶段。
  * @param  无。
  * @retval g_sleep_st 大于 0 时返回 1，否则返回 0。
  * @note   不是 sleep_mode 策略枚举查询；原实现以 0 作标志，保留计数为 0 的边界限制。
  */
int soc_in_sleep_mode(void)
{
	return (g_sleep_st > 0);    /* 只以非零计时起点判断，不查询休眠策略值。 */
}

/* ARM 系统控制寄存器及异常优先级备份。 */

static unsigned int  scb_aircr_bak, scb_scr_bak, scb_ccr_bak, scb_shcsr_bak;    /* 保存 AIRCR、SCR、CCR、SHCSR 四个系统控制字。 */
static unsigned char scb_shpr_bak[12];                                          /* 保存 12 字节系统异常优先级。 */

/**
  * @brief  保存掉电前 ARM 系统控制和异常优先级。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void  soc_arm_reg_backup(void)
{

	memcpy(scb_shpr_bak, (void*)SCB->SHPR, 12);    /* 复制系统异常优先级字节，不改变其中位布局。 */
	scb_aircr_bak = SCB->AIRCR;                    /* 保存应用中断/复位控制字。 */
	scb_scr_bak = SCB->SCR;                        /* 保存系统睡眠控制字。 */
	scb_ccr_bak = SCB->CCR;                        /* 保存配置控制字。 */
	scb_shcsr_bak = SCB->SHCSR;                    /* 保存系统异常控制与状态。 */
}

/**
  * @brief  恢复 MPU 以及 ARM 系统控制和异常优先级。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
static void  soc_arm_reg_restore(void)
{
#ifdef CONFIG_ACTIONS_ARM_MPU
	arm_mpu_protect_init();                         /* MPU 配置下重新建立内存保护。 */
#endif
	memcpy((void*) SCB->SHPR, scb_shpr_bak, 12);    /* 恢复系统异常优先级字节。 */
	SCB->AIRCR = scb_aircr_bak;                     /* 写回 AIRCR；保留原平台写回方式。 */
	SCB->SCR = scb_scr_bak;                         /* 恢复睡眠控制。 */
	SCB->CCR = scb_ccr_bak;                         /* 恢复配置控制。 */
	SCB->SHCSR = scb_shcsr_bak;                     /* 恢复系统异常控制与状态。 */
}

/**
  * @brief  协调深睡眠准备、计时切换、CPU 休眠和完整系统恢复。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
void soc_enter_deep_sleep(void)
{
	dump_reg("before");                                       /* 可选打印休眠前寄存器。 */

	soc_pmu_check_onoff_reset_func();                         /* 执行 PMU 按键复位相关检查。 */
#ifdef CONFIG_SLEEP_DISABLE_BT
	soc_cmu_sleep_prepare(0);                                 /* 调试禁用蓝牙时明确要求关闭蓝牙供电。 */
#else
	soc_cmu_sleep_prepare(sys_get_bt_pwr_by_sleep_mode());    /* 按当前模式决定是否保留蓝牙供电。 */
#endif
	soc_timer_sleep_prepare();                                /* 保存计时基准并把 T2 改为低速休眠源。 */
	if (sleep_policy_is_pwrgat()) {                           /* CPU 掉电时需另存 ARM 系统寄存器。 */
		soc_arm_reg_backup();                             /* 备份 ARM 系统控制与异常优先级。 */
	}
	soc_enter_sleep_switch_uart(true);                        /* 休眠期间将调试 UART 引脚交给蓝牙。 */
	cpu_enter_sleep();                                        /* 执行底层等待中断/掉电及唤醒恢复流程。 */
	soc_enter_sleep_switch_uart(false);                       /* 完整唤醒后把调试 UART 交回主 CPU。 */
	if (sleep_policy_is_pwrgat()) {                           /* CPU 掉电路径需恢复系统寄存器。 */
		soc_arm_reg_restore();                            /* 恢复 MPU 和 ARM 系统寄存器。 */
	}

	/* 完整唤醒后先更新计时补偿，再恢复公共寄存器和系统级回调。 */
	soc_timer_exit_sleep();                                   /* 恢复 T2 的 HOSC 并累加本轮周期与毫秒补偿。 */
	soc_cmu_sleep_exit();                                     /* 恢复公共外设、记录来源并执行延后回调；原代码忽略其返回值。 */
	dump_reg("BT after");                                     /* 可选打印深睡眠后寄存器。 */

#ifdef CONFIG_SLEEP_STAGE_DEBUG
	soc_pstore_set(SOC_PSTORE_TAG_SLEEP_DBG_STAGE, 0);        /* 清除持久保存的休眠调试阶段。 */
#endif
}

/**
  * @brief  通过本项目简化入口执行同一底层睡眠流程。
  * @param  无。
  * @retval 无。
  * @note   仅在相应配置分支存在时编译；保存区为本模块静态对象，调用顺序由休眠流程保证，函数自身不提供并发保护。
  */
void soc_enter_light_sleep(void)
{
	dump_reg("before");          /* 可选打印简化睡眠前的寄存器。 */

	soc_cmu_sleep_prepare(0);    /* 简化入口始终要求蓝牙掉电。 */
	cpu_enter_sleep();           /* 执行同一底层等待中断/掉电流程；本入口没有深睡眠接口的计时补偿准备。 */
	soc_cmu_sleep_exit();        /* 恢复公共外设并处理回调，原代码不使用汇总返回值。 */

	dump_reg("after");           /* 可选打印简化睡眠后的寄存器。 */
}

/**
  * @brief  初始化休眠状态、回调链表和 RC32K 换算系数。
  * @param  dev：[输入]系统初始化框架传入的设备指针，本实现不使用，可为空。
  * @retval 固定返回 0。
  * @note   注册在 PRE_KERNEL_1、优先级 80；完整性检查配置另设 PSRAM 检查区。
  */
static int soc_sleep_init(const struct device *dev)
{
	int i;                                                     /* 清空每个唤醒源链表的索引。 */

	ARG_UNUSED(dev);                                           /* SYS_INIT 传入的设备指针在本实现中不使用。 */
#ifdef CONFIG_SLEEP_STAGE_DEBUG
	uint32_t val;                                              /* 保存上次持久化的休眠阶段。 */
	soc_pstore_get(SOC_PSTORE_TAG_SLEEP_DBG_STAGE, &val);      /* 读取复位前留下的调试阶段。 */
	printk("SLEEP_DBG_STAGE=0x%x\n", val);                     /* 打印上次睡眠阶段以辅助定位。 */
#endif
	g_lcd_aod_mode = 0;                                        /* 常亮显示初始关闭。 */
	current_sleep->g_sleep_cycle = 0;                          /* HOSC 等效休眠周期补偿从 0 开始。 */
	current_sleep->sleep_mode = 0;                             /* 初始选择普通休眠策略。 */
	g_sleep_ms = 0;                                            /* 累计休眠毫秒补偿从 0 开始。 */
	g_sleep_st = 0;                                            /* 初始不处于 RC32K 休眠计时阶段。 */
	g_sleep_wksrc_en = 0;                                      /* 初始不允许任何唤醒源，由各模块随后注册。 */
	g_sleep_wksrc_en_bak = 0;                                  /* 清除特殊模式源掩码备份。 */
	g_cyc2ms_mul = (1000<<16) / soc_rc32K_freq();              /* 根据 RC32K 实际频率预计算 Q16 毫秒系数。 */

	for(i = 0; i < SLEEP_WKSRC_NUM; i++)                       /* 初始化所有逻辑唤醒源的回调链表。 */
		g_wk_fun[i] = NULL;                                /* 清空当前源的链表头。 */

#ifdef CONFIG_JTAG_DEBUG
	extern void jtag_set(void);                                /* JTAG 调试配置的外部引脚设置入口声明。 */
	jtag_set();                                                /* 按调试配置设置 JTAG 引脚。 */
#endif

#ifdef CONFIG_SLEEP_MEMORY_CHECK_INTEGRITY
	current_sleep->check_start_addr = PSRAM_START_ADDR;        /* 完整性检查从配置的 PSRAM 基址开始。 */
	current_sleep->check_len = MEMORY_CHECK_INTEGRITY_SIZE;    /* 检查范围为宏指定的 8 MiB。 */
#endif

	return 0;                                                  /* 初始化无需动态资源申请，固定成功。 */
}

SYS_INIT(soc_sleep_init, PRE_KERNEL_1, 80);    /* 在 PRE_KERNEL_1、优先级 80 注册休眠基础状态初始化。 */


