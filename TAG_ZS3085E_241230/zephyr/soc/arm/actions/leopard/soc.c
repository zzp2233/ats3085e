/* Leopard启动入口在PRE_KERNEL_1优先级0执行，早于board.c优先级5的引脚初始化。此文件也提供SPI时钟策略间接使用的soc_dvfs_opt；启动中其他硬件初始化在本文件完整说明，但不据其辅助调用无条件扩展所有外设。 */
/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2016 Linaro Limited.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Leopard系统启动、晶振切换及芯片配置读取。
 */

#include <device.h>
#include <init.h>
#include <arch/cpu.h>
#include "soc.h"
#include "soc_atp.h"
#include <linker/linker-defs.h>
//#include <arch/arm/aarch32/cortex_m/cmsis.h>


/**
  * @brief  切换调试引脚到指定JTAG组。
  * @param  group_id：[输入] 组号0～2，其他值只打印不写寄存器。
  * @retval 无。
  * @note   直接读改写JTAG_CTL，没有额外并发锁。
  */
static void jtag_config(unsigned int group_id)
{
	printk("jtag switch to group=%d\n", group_id);                                                     /* 输出请求的调试引脚组号。 */
	if (group_id < 3)                                                                                  /* 只允许三组已定义的调试映射。 */
		sys_write32((sys_read32(JTAG_CTL) & ~(3 << 0)) | (group_id << 0) | (1 << 4), JTAG_CTL);    /* 替换组选择并置调试使能位，保留其他字段。 */

}

/**
  * @brief  把调试口切换到默认组0。
  * @param  无。
  * @retval 无。
  * @note   供RTT等板级调试入口调用。
  */
void jtag_set(void)
{
	jtag_config(0);    /* 默认选择组0。 */
}

/**
  * @brief  置看门狗控制位0以执行喂狗。
  * @param  无。
  * @retval 无。
  * @note   与清除持久看门狗复位计数不同。
  */
void soc_watchdog_clear(void)
{
	sys_set_bit(WD_CTL, 0);    /* 通过控制位触发硬件看门狗清计数。 */
}

/**
  * @brief  检查连续看门狗复位计数并在超过阈值时转入OTA。
  * @param  无。
  * @retval 无。
  * @note   读取持久计数；超过10清零并请求OTA重启，否则加1。
  */
static void wd_check_wdreset_cnt(void)
{
	uint32_t reset_cnt;                                                   /* 保存持久存储的连续复位计数。 */
	soc_watchdog_clear();                                                 /* 启动检查前先喂狗，避免检查过程再次超时。 */
	soc_pstore_get(SOC_PSTORE_TAG_WD_RESET_CNT, &reset_cnt);              /* 读取跨复位保存的看门狗计数；原代码未检查读取状态。 */
	printk("wd cnt=%d, WD_CTL=0x%x\n", reset_cnt, sys_read32(WD_CTL));    /* 打印计数及硬件控制值辅助排查重启。 */
	if(reset_cnt > 10){                                                   /* 连续计数超过10时进入故障恢复流程。 */
		printk("reboot ota\n");                                       /* 报告即将进入OTA恢复。 */
		soc_pstore_set(SOC_PSTORE_TAG_WD_RESET_CNT, 0);               /* 清除持久计数，避免恢复流程重复累计。 */
		sys_pm_reboot(REBOOT_TYPE_GOTO_OTA);                          /* 请求重启到OTA入口。 */
	}else{
		reset_cnt++;                                                  /* 未超过阈值时记录本轮启动。 */
		soc_pstore_set(SOC_PSTORE_TAG_WD_RESET_CNT, reset_cnt);       /* 回写更新后的持久计数。 */
	}
}
 /**
  * @brief  清除持久存储中的看门狗复位计数。
  * @param  无。
  * @retval 无。
  * @note   在成功进入主应用后调用，表示本次启动已经推进到可运行阶段。
  */
 void wd_clear_wdreset_cnt(void)
{
	soc_pstore_set(SOC_PSTORE_TAG_WD_RESET_CNT, 0);    /* 清除持久计数，避免恢复流程重复累计。 */
}

/**
  * @brief  取得影响时钟策略的芯片配置编码。
  * @param  无。
  * @retval UID1位19:17，范围0～7。
  * @note   __sleepfunc指定休眠期间可访问的代码段，不代表本函数主动进入休眠。
  */
__sleepfunc int soc_dvfs_opt(void)
{
	return (sys_read32(UID1) >> 17) & 0x7;    /* 提取位19:17，供时钟策略按零/非零选择路径。 */
}

/**
  * @brief  读取芯片封装编码。
  * @param  无。
  * @retval UID1位27:24的4位编码。
  * @note   原接口名称不表示收发数据包。
  */
uint8_t ipmsg_btc_get_ic_pkt(void)
{
	uint32_t val;                           /* 寄存器工作值，用于保存或提取字段。 */

	val = sys_read32(UID1);                 /* 读取芯片标识字以提取封装编码。 */
	return (uint8_t)((val >> 24) & 0xF);    /* 位27:24：芯片封装编码。 */
}

#if 0

/**
  * @brief  保留固定禁用的旧RC192M校准试验。
  * @param  无。
  * @retval 无。
  * @note   仅#if 0中存在，不参与当前构建；校准等待没有超时。
  */
static void cpu_init(void)
{
	int i;                                                            /* 旧校准试验的连续完成检查计数。 */
	unsigned int val;                                                 /* 旧校准试验保存RC校准字段。 */
	printk("rc192 cal\n");                                            /* 输出旧试验的校准启动提示。 */
	sys_write32(0x80011481, RC192M_CTL);                              /* 旧试验第一阶段RC192M配置。 */
	soc_udelay(20);                                                   /* 忙等20微秒，满足旧校准序列间隔。 */
	sys_write32(0x8001148d, RC192M_CTL);                              /* 旧试验启动校准阶段配置。 */
	soc_udelay(200);                                                  /* 忙等200微秒等待配置稳定。 */
	while(1){                                                         /* 旧试验无总超时，持续等待稳定完成。 */
		if(sys_read32(RC192M_CTL)& (1<<24)){                      /* 检测RC校准完成状态位24。 */
			for(i = 0; i < 3; i++){ // 连续三次检查均完成才退出校准等待。
				soc_udelay(20);                           /* 忙等20微秒，满足旧校准序列间隔。 */
				if(!(sys_read32(RC192M_CTL)& (1<<24)))    /* 连续采样中任一次完成位消失则重新等待。 */
					break;                            /* 退出当前检查循环或校准等待，执行后续配置。 */
			}
			if(i == 3)                                        /* 连续三次状态有效才接受校准完成。 */
				break;                                    /* 退出当前检查循环或校准等待，执行后续配置。 */
		}
	}

	val = (sys_read32(RC192M_CTL) & (0x7f<<25)) >> 25;                /* 提取旧校准结果位31:25。 */
	if(val)                                                           /* 非零结果才减1，避免无符号下溢。 */
		val -= 1;                                                 /* 按旧校准经验规则微调结果。 */
	sys_write32(0x80011001 | (val<<4) , RC192M_CTL);                  /* 把调整值合入旧RC控制字。 */
	soc_udelay(200);                                                  /* 忙等200微秒等待配置稳定。 */
	sys_write32(0x215, CMU_SYSCLK);// 旧试验保留的CPU/AHB时钟字段，当前未执行。
}
#endif

/**
  * @brief  临时切至RC4M后更新HOSC控制并恢复原CPU时钟配置。
  * @param  无。
  * @retval 无。
  * @note   HOSC就绪轮询无超时；保留原电容调谐字段和整个CPU时钟控制字。
  */
__sleepfunc static void leopard_set_hosc_ctrl()
{
	/*
	IO_WRITE(HOSC_CTL, (0x601f1b6 | (CAP<<17)));
	如果是首次配置（HOSC_CTL[25]: 0->1）需要将CPUCLK切换到RC4M，等待HOSC_CTL[28]为1后再将CPUCLK切回HOSC/COREPLL
	*/
	uint32_t val;                                                                          /* 寄存器工作值，用于保存或提取字段。 */

	/* 修改晶振前先将CPU切至RC4M，原寄存器值留作恢复。 */
	val = sys_read32(CMU_SYSCLK);                                                          /* 保存CPU原时钟源及分频。 */
	sys_write32(val & ~0x7, CMU_SYSCLK);                                                   /* 仅把源字段置0，临时选RC4M。 */

	/* 保留电容字段更新HOSC控制，等待硬件就绪后才恢复CPU时钟。 */
	sys_write32(0x600f7f6 | (sys_read32(HOSC_CTL) & HOSC_CTL_HOSC_CAP_MASK), HOSC_CTL);    /* 保留电容调谐位并应用晶振控制配置。 */
	while(!(sys_read32(HOSC_CTL) & (1<< HOSC_CTL_HOSC_READY)));                            /* 无超时等待HOSC就绪，不在未稳定时恢复高速CPU源。 */

	/* 恢复修改前保存的CPU时钟源与分频，不强行假设原来一定是COREPLL。 */
	sys_write32(val, CMU_SYSCLK);                                                          /* 写回修改前的整个CPU时钟配置。 */
}

/**
  * @brief  在最早启动阶段建立SoC时钟、电源、缓存和SDMA基础状态。
  * @param  arg：[输入] 初始化框架设备参数，本函数不使用，可为空。
  * @retval 0：启动配置流程完成，多个底层操作未检查错误。
  * @note   PRE_KERNEL_1优先级0执行；包含无超时晶振等待和可能的OTA重启。
  */
static int leopard_init(const struct device *arg)
{
	uint32_t key;                                                                                            /* 保存临界区前中断状态。 */
	uint32_t val;                                                                                            /* 寄存器工作值，用于保存或提取字段。 */


	ARG_UNUSED(arg);                                                                                         /* 板级启动不使用框架传入的设备对象。 */
	soc_udelay(50);                                                                                          /* 早期启动忙等50微秒。 */
	leopard_set_hosc_ctrl();                                                                                 /* 安全更新晶振控制并恢复CPU源。 */
	soc_udelay(10);                                                                                          /* 晶振设置后额外等待10微秒。 */
	key = irq_lock();                                                                                        /* 安装NMI入口期间禁止普通中断并发。 */
	/* 若内核配置要求，安装复位CPU的默认NMI处理；否则NMI_INIT为空操作。 */
	NMI_INIT();                                                                                              /* 按内核配置安装默认NMI处理。 */

	irq_unlock(key);                                                                                         /* 恢复进入临界区前的中断状态。 */
	soc_powergate_init();                                                                                    /* 建立电源门控管理状态。 */
	sys_write32(0x10e, CMU_GPUCLK);                                                                          /* COREPLL的1.5分频编码。 */
	acts_clock_peripheral_enable(CLOCK_ID_GPU);                                                              /* GPU初态不稳定，先供时钟以执行复位序列。 */
	soc_udelay(1);                                                                                           /* 等待1微秒让GPU复位阶段稳定。 */
	acts_reset_peripheral_deassert(RESET_ID_GPU);                                                            /* 释放GPU复位以推进原初始化序列。 */
	soc_udelay(1);                                                                                           /* 等待1微秒让GPU复位阶段稳定。 */
	acts_reset_peripheral_assert(RESET_ID_GPU);//
	soc_udelay(2);                                                                                           /* 等待2微秒后关闭GPU时钟。 */
	acts_clock_peripheral_disable(CLOCK_ID_GPU);//
	/* 完成复位后关闭GPU电源域，具体由电源门控接口执行。 */
	//sys_write32(sys_read32(PWRGATE_DIG) & ~(0x1 << 25), PWRGATE_DIG);
	if(soc_powergate_is_poweron(POWERGATE_GPU_PG_DEV))                                                       /* GPU电源域仍打开时才请求关闭。 */
		soc_powergate_set(POWERGATE_GPU_PG_DEV, false);                                                  /* 关闭GPU电源域以进入默认低功耗状态。 */

	//cpu_init();

	wd_check_wdreset_cnt();                                                                                  /* 检查连续复位计数，必要时请求OTA恢复。 */
	/* 以下CMSIS频率赋值为旧平台保留代码，当前已注释。 */
	/* 原旧平台说明为HSI 16 MHz，不适用于推断本Leopard当前频率。 */
	//SystemCoreClock = 16000000;
	//while(!arg);

	//sys_write32(0x0, WIO0_CTL); // 旧方案将WIO0默认置GPIO功能，当前禁用。

	/* 以下低功耗试验配置当前禁用。 */
	//sys_write32(0x30F, SPI1_CLKGATING);

	/* 初始化外设事件互连PPI。 */
	ppi_init();                                                                                              /* 初始化外设事件互连。 */

	/* 复位并打开SDMA时钟，再释放复位。 */
	acts_reset_peripheral_assert(RESET_ID_SDMA);                                                             /* 先保持SDMA复位。 */
	acts_clock_peripheral_enable(CLOCK_ID_SDMA);                                                             /* 打开SDMA模块时钟门控。 */
	acts_reset_peripheral_deassert(RESET_ID_SDMA);                                                           /* 时钟就绪后释放SDMA复位。 */

	/* 配置SDMA、DE对SPI0 NOR的访问，再配置SPI1 PSRAM访问相关字段。 */
	//sys_write32(sys_read32(SPICACHE_CTL) | BIT(10) | BIT(11), SPICACHE_CTL);
	sys_write32((sys_read32(SPICACHE_CTL) & ~(0x3 << 5)) | (0x1 << 5) | BIT(10) | BIT(11), SPICACHE_CTL);    /* 替换缓存控制位6:5并使能原代码要求的访问控制位。 */

	if (soc_dvfs_opt()) {                                                                                    /* 按芯片配置选择PSRAM延迟及缓存访问选项。 */
		/* DVFS配置路径更新PSRAM输出时钟延迟字段；多行语句及续行保持原样。 */
		sys_write32((sys_read32(SPI1_DELAYCHAIN)  & ~(0x3f << 8)) \
					| (SPI1_DELAYCHAIN_CLKOUT << 8), SPI1_DELAYCHAIN);
		/* 打开DVFS路径需要的SPI1缓存访问位，包括额外的位15。 */
		sys_write32(sys_read32(SPI1_CACHE_CTL) | BIT(8) | BIT(10) | BIT(11) | BIT(14) | BIT(15), SPI1_CACHE_CTL);
	} else {
		/* 非DVFS路径保留位15不额外置位，其余访问位与前一分支一致。 */
		sys_write32(sys_read32(SPI1_CACHE_CTL) | BIT(8) | BIT(10) | BIT(11) | BIT(14) /*| BIT(15)*/, SPI1_CACHE_CTL);
	}

	sys_write32(BIT(24) | (0<<4) | (0xa<<8) | (0x1<<0), SPI1_GPU_CTL);                                       /* 写入GPU访问SPI1缓存的固定控制组合。 */

	/* 共享RAM选择HOSC 32 MHz时钟源，再打开其时钟门控。 */
	val = sys_read32(CMU_MEMCLKSRC0);                                                                        /* 读取共享RAM时钟源选择字。 */
	val = (val & ~(7 << 25)) | (1 << 25);                                                                    /* 把共享RAM源字段27:25置为HOSC编码1。 */
	sys_write32(val, CMU_MEMCLKSRC0);                                                                        /* 提交共享RAM时钟源。 */

	val = sys_read32(CMU_MEMCLKEN0);                                                                         /* 读取内存时钟门控字。 */
	val |= (1 << 25);                                                                                        /* 打开共享RAM时钟使能位25。 */
	sys_write32(val, CMU_MEMCLKEN0);                                                                         /* 提交共享RAM时钟门控。 */

	val = 0;                                                                                                 /* 校准结果默认0，作为失败时的初始值。 */
	if(soc_atp_get_pmu_calib(12, &val)){                                                                     /* 读取索引12的PMU校准信息，非0视为失败。 */
		printk("get psram pg fail\n");                                                                   /* 报告无法取得PSRAM电源校准配置。 */
	}else{
		printk("psram=%d, 0x%x\n", val, sys_read32(VOUT_CTL0));                                          /* 输出校准标志和当前电源控制字。 */
		if(val){                                                                                         /* 校准标志非0才调整对应供电控制位。 */
			sys_clear_bit(VOUT_CTL0, 31);                                                            /* 按原校准策略清电源控制位31。 */
			printk("VOUT0= 0x%x\n", sys_read32(VOUT_CTL0) );                                         /* 回读并打印更新后的电源控制值。 */
		}			
	}
	printk("apm=%d\n", soc_psram_is_apm());                                                                  /* 输出PSRAM类型检测结果。 */
	//val = sys_read32(VOUT_CTL1_S1M) ;
	//val = (val & ~(0xFff)) | 0xedd;   // 旧电压试验值标注为1.2 V，当前禁用。
	//sys_write32(val, VOUT_CTL1_S1M);
	//sys_write32(0x12, CMU_SYSCLK);// 旧CPU频率试验，当前禁用。
	return 0;                                                                                                /* 固定初始化流程完成，不汇总未检查的底层错误。 */
}

SYS_INIT(leopard_init, PRE_KERNEL_1, 0);    /* 注册优先级0的SoC早期初始化。 */


/* 若启用CONFIG_WDOG_ACTS，由看门狗驱动清除启动复位计数；否则使用下面的启动钩子。 */
#ifndef CONFIG_WDOG_ACTS

/**
  * @brief  在未编译看门狗驱动时清计数并关闭硬件看门狗。
  * @param  arg：[输入] 框架设备参数，未使用，可为空。
  * @retval 0：完成固定寄存器写入。
  * @note   仅CONFIG_WDOG_ACTS未定义时注册为APPLICATION优先级91。
  */
static int wd_reset_cnt_init(const struct device *arg)
{
	wd_clear_wdreset_cnt();    /* 进入应用初始化后清连续复位计数。 */
	sys_write32(0, WD_CTL);    /* 未使用看门狗驱动时关闭硬件看门狗。 */
	return 0;                  /* 固定初始化流程完成，不汇总未检查的底层错误。 */
}

SYS_INIT(wd_reset_cnt_init, APPLICATION, 91);    /* 未使用看门狗驱动时注册应用阶段清理钩子。 */
#endif

