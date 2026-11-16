/* Actions系统定时器：TIMER0负责向内核报告tick并安排下次超时，TIMER2提供高分辨率cycle读数。当前板为1000 tick/s、32000000 cycle/s；cycle不是CPU指令数，也不是毫秒。 */
/*
 * Copyright (c) 2017 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <drivers/timer/system_timer.h>
#include <sys_clock.h>
#include <spinlock.h>
#include <soc.h>

/* 本文件固定启用独立TIMER2计时，避免TIMER0超时重设影响应用的cycle读数。 */
#define USE_T2_FOR_CYCLE

/* 每tick包含的硬件周期数，要求配置的硬件频率与真实计数源一致。 */
#define CYC_PER_TICK (sys_clock_hw_cycles_per_sec()	\
		      / CONFIG_SYS_CLOCK_TICKS_PER_SEC)
/* 将单次比较范围限制在0x40000000周期以内，为32位计数回绕保留余量。 */
#define MAX_TICKS ((0x40000000 / CYC_PER_TICK) - 1)
#define MAX_CYCLES (MAX_TICKS * CYC_PER_TICK)    /* 上述最大tick预算对应的周期数。 */

static struct k_spinlock lock;    /* 保护TIMER0软件累计值与比较值更新，静态零初始化。 */
/* TIMER0累计周期：中断中加上距last_cycle_count的差值；睡眠补偿分支也可更新。
 * 瞬时软件时间为cycle_count + acts_timer_elapsed()，所有32位累计值按无符号模运算回绕。 */
static uint32_t cycle_count;
/* 已换算成完整tick并报告内核的周期数；不足一tick的余量留到后续中断。 */
static uint32_t announced_cycles;

/* 上次计入cycle_count时的TIMER0硬件读数，作为差分基准。 */
static uint32_t last_cycle_count;



/**
  * @brief  读取尚未计入软件累计值的TIMER0周期差。
  * @param  无。
  * @retval 当前计数减last_cycle_count的32位无符号差，单位为当前TIMER0计数周期。
  * @note   调用方须提供与累计值更新一致的锁保护；跨一次32位回绕用模运算处理。
  */
static uint32_t acts_timer_elapsed(void)
{
	return	sys_read32(T0_CNT)-last_cycle_count;    /* 读取连续计数寄存器，按无符号差值计算本轮增量。 */
}

#ifdef USE_T2_FOR_CYCLE

/**
  * @brief  配置TIMER2为独立高分辨率周期计数器。
  * @param  无。
  * @retval 无。
  * @note   直接访问寄存器；保留原acts_reset_peripheral(CLOCK_ID_TIMER2)实参，不另行推断复位编号。
  */
static void timer_cyc_init(void)
{
#ifdef CONFIG_SOC_SERIES_LEOPARD
	acts_clock_peripheral_enable(CLOCK_ID_TIMER2);    /* Leopard分支打开TIMER2外设时钟。 */
	acts_reset_peripheral(CLOCK_ID_TIMER2);           /* 保留原复位调用，参数沿用时钟编号常量。 */
#endif
	sys_write32(0x0, CMU_TIMER2CLK);                  /* 选择HOSC高速晶振作为计数时钟。 */
	sys_write32(0x1, T2_CTL);                         /* 清中断挂起状态并停止计数，便于重新配置。 */
	timer_reg_wait();                                 /* 用固定次数volatile循环留出寄存器配置间隔，不是精确微秒延时。 */
	sys_write32(TIMER_MAX_CYCLES_VALUE, T2_VAL);      /* 把TIMER2重装/比较值设为32位最大值，拉长计数窗口。 */
	sys_write32(0x824, T2_CTL);                       /* 配置向上计数、自动重装，不启用TIMER2中断。 */
}
#endif

/**
  * @brief  将下一次TIMER0比较事件安排在当前计数之后。
  * @param  cycle：[输入] 相对硬件周期数，先限制到CYC_PER_TICK/100～MAX_CYCLES。
  * @retval 无。
  * @note   读取T0_CNT后写比较值，计算与写入之间存在执行延迟；由调用方保证串行访问。
  */
static void acts_timer_set_next_irq(uint32_t cycle)
{
	/* 把比较跨度限制在驱动可处理的范围内，再基于当前计数安排硬件事件。 */
	if(cycle < CYC_PER_TICK/100)                      /* 最小比较间隔为1/100 tick；当前1000 tick/s时为10微秒。 */
		cycle = CYC_PER_TICK/100;                 /* 过短间隔提升到最小值，减少比较值写入前已经越过目标的风险。 */
	if(cycle > MAX_CYCLES)                            /* 超过安全比较窗口时截到最大预算。 */
		cycle = MAX_CYCLES;                       /* 限制单次比较跨度。 */
	sys_write32(sys_read32(T0_CNT)+cycle, T0_VAL);    /* 以当前硬件读数为基准写入下一比较值，32位加法可自然回绕。 */
}

/* 本驱动在sys_clock_driver_init中通过IRQ_CONNECT连接TIMER0中断；不是通用SysTick处理器。 */

/**
  * @brief  响应TIMER0比较中断并向内核报告经过的tick。
  * @param  arg：[输入] 中断连接参数，本实现不使用，可为空。
  * @retval 无。
  * @note   在中断上下文执行；tickless一次可报告多个tick，周期模式每次固定报告1。
  */
void acts_timer_isr(void *arg)
{
	ARG_UNUSED(arg);                                                     /* 本中断不需要外部上下文。 */
	uint32_t dticks, cycle;                                              /* 保存本次完整tick数和TIMER0采样值。 */
	/* 先确认并累计硬件经过的周期，再把完整tick报告内核，驱动软件超时队列。 */
	sys_write32(sys_read32(T0_CTL), T0_CTL);                             /* 按原控制值回写，清除中断挂起位。 */
	cycle = sys_read32(T0_CNT);                                          /* 采样当前TIMER0计数，后续用同一读数更新累计及基准。 */
	cycle_count += cycle-last_cycle_count;                               /* 把距上次基准的差值加入软件周期累计。 */
	last_cycle_count = cycle;                                            /* 推进差分基准，避免同一段时间重复累计。 */

	#if defined(CONFIG_TICKLESS_KERNEL)
		dticks = (cycle_count - announced_cycles) / CYC_PER_TICK;    /* 只报告已完整经过的tick，余数留待后续中断。 */
		announced_cycles += dticks * CYC_PER_TICK;                   /* 记录这次已交给内核的周期份额。 */
		//acts_timer_set_next_irq(sys_clock_hw_cycles_per_sec()*10); // 原调试方案：保证每10秒触发一次中断，当前未启用。
		sys_clock_announce(dticks);                                  /* 推进内核时钟并处理到期的睡眠/信号量等待。 */
	#else
		acts_timer_set_next_irq(CYC_PER_TICK);                       /* 安排一个tick后的下一次比较事件。 */
		sys_clock_announce(1);                                       /* 非tickless分支每次中断固定报告一个tick。 */
	#endif
	//printk("-ie=0x%x, 0x%x, 0x%x\n", cycle_count, sys_read32(T0_VAL), sys_read32(T0_CNT));
}


/**
  * @brief  建立系统时钟事件、周期计数器和TIMER0中断。
  * @param  device：[输入] 框架传入的时钟设备，本实现不访问其内容。
  * @retval 固定返回0，未检测寄存器写入或时钟启动失败。
  * @note   启动阶段调用，首次比较间隔为一个tick。
  */
int sys_clock_driver_init(const struct device *device)
{
	/* 建立TIMER0时钟事件源；比较中断负责推进内核调度和等待超时。 */
	sys_write32(0x0, CMU_TIMER0CLK);                        /* 选择HOSC高速晶振作为计数时钟。 */
#ifdef CONFIG_SOC_SERIES_LEOPARD
	acts_clock_peripheral_enable(CLOCK_ID_TIMER0);          /* 打开TIMER0时钟门控。 */
#else
	acts_clock_peripheral_enable(CLOCK_ID_TIMER);           /* 非Leopard系列使用其通用定时器时钟编号。 */
#endif
	sys_write32(0x1, T0_CTL);                               /* 清中断挂起状态并停止计数，便于重新配置。 */
	timer_reg_wait();                                       /* 用固定次数volatile循环留出寄存器配置间隔，不是精确微秒延时。 */
	sys_write32(0xe22, T0_CTL);                             /* 配置向上连续计数并启用比较中断。 */

	#ifdef USE_T2_FOR_CYCLE
	timer_cyc_init();                                       /* 建立与TIMER0事件调度分离的TIMER2计时源。 */
	#endif
	acts_timer_set_next_irq(CYC_PER_TICK);                  /* 安排一个tick后的下一次比较事件。 */
	//printk("sys_clock_driver_init, cnt=0x%x, val=0x%x\n", sys_read32(T0_CNT), sys_read32(T0_VAL));
	IRQ_CONNECT(IRQ_ID_TIMER0, 0, acts_timer_isr, 0, 0);    /* 将TIMER0中断连接到处理函数，优先级和附加参数沿用0。 */
	irq_enable(IRQ_ID_TIMER0);                              /* 允许TIMER0中断进入CPU。 */

	return 0;                                               /* 原实现无错误检查，本路径始终报告成功。 */
}

/* RC32K计数到HOSC周期的校准倍率，由SoC校准函数提供。 */
#define RC32K_MUTIPLE soc_rc32K_mutiple_hosc()

/**
  * @brief  按内核下一次超时预算更新TIMER0比较事件。
  * @param  ticks：[输入] 距下次事件的tick数；K_TICKS_FOREVER改用MAX_TICKS，0改用1，其他负值本实现未单独归一化。
  * @param  idle：[输入] 即将空闲的提示，本实现未使用。
  * @retval 无。
  * @note   仅CONFIG_TICKLESS_KERNEL启用时更新硬件；短临界区保护软件计数，不能据通用接口契约假定本实现处理了所有负值。
  */
void sys_clock_set_timeout(int32_t ticks, bool idle)
{

#if defined(CONFIG_TICKLESS_KERNEL)
	k_spinlock_key_t key = k_spin_lock(&lock);                                        /* 保存中断状态并锁住TIMER0计数快照/比较更新。 */
	uint32_t delay;                                                                   /* 保存扣除已流逝时间后的周期预算。 */
	ticks = (ticks == K_TICKS_FOREVER) ? MAX_TICKS : ticks;                           /* 无限等待也安排有限远期比较，避免丢失硬件回绕。 */
	if(!ticks)                                                                        /* 零预算改成一个tick，再扣除本tick已经经过的部分。 */
		ticks = 1;                                                                /* 避免直接使用零长度比较预算。 */
	uint32_t unannounced = cycle_count - announced_cycles +  acts_timer_elapsed();    /* 合并已累计未报告和当前尚未累计的周期数。 */
	/* 扣除已经经过但尚未报告内核的时间，避免把相对超时再延长一次。 */
	delay = ticks * CYC_PER_TICK;                                                     /* 把内核tick预算换算为计数周期，沿用原32位运算范围。 */
	if(unannounced > delay){                                                          /* 目标已早于当前未上报时间，要求尽快产生事件。 */
		acts_timer_set_next_irq(20);                                              /* 已逾期则尽快触发，实际仍受最小比较间隔限制。 */
	}else{
		delay = delay - unannounced + 1;                                          /* 扣除流逝量，并保留一个周期的比较余量。 */
		acts_timer_set_next_irq(delay);                                           /* 写入按剩余预算计算的比较事件。 */
	}
	k_spin_unlock(&lock, key);                                                        /* 结束短临界区，恢复进入前中断状态。 */
#endif

}

#if defined(CONFIG_PM_DEVICE)
#include <wait_q.h>
static uint32_t sleep_st_cycle;    /* 切换低速时钟时保存TIMER0读数，供恢复补偿使用。 */

/**
  * @brief  通知内核超时模块进入睡眠补偿后的队列重建阶段。
  * @param  无。
  * @retval 无。
  * @note   实现在kernel/timeout.c；仅补偿分支调用。
  */
void acts_exit_sleep(void);

/**
  * @brief  在设备挂起和恢复时切换TIMER0计数源并可选补偿睡眠时间。
  * @param  device：[输入] 电源框架设备参数，本实现未访问。
  * @param  action：[输入] RESUME恢复HOSC，SUSPEND切RC32K，其他动作不处理。
  * @retval 固定返回0，包括未处理的动作。
  * @note   仅CONFIG_PM_DEVICE编译；CONFIG_TIME_SLEEP_COMP决定是否把睡眠期间计数换算入软件累计值。
  */
int sys_clock_device_ctrl(const struct device *device, enum pm_device_action action)
{
	uint32_t cycle;                                                   /* 保存切换时的计数或睡眠期间的低速计数差。 */
	#ifdef CONFIG_TIME_SLEEP_COMP
	uint32_t st_cycle;                                                /* 保存折算后的HOSC周期，仅睡眠补偿分支使用。 */
	#endif

	/* 电源状态切换涉及计数源变化；低速计数必须显式换算后才能加入HOSC单位的累计值。 */
	if(action == PM_DEVICE_ACTION_RESUME){                            /* 从低功耗状态恢复时重建高速计数基准。 */
		cycle = sys_read32(T0_CNT);                               /* 采样当前TIMER0计数，后续用同一读数更新累计及基准。 */
		sys_write32(0x0, CMU_TIMER0CLK);                          /* 选择HOSC高速晶振作为计数时钟。 */
		last_cycle_count = cycle;                                 /* 更新差分基准；是否累计睡眠时间由下方CONFIG_TIME_SLEEP_COMP决定。 */
		#ifdef CONFIG_TIME_SLEEP_COMP
		cycle -= sleep_st_cycle;                                  /* 计算睡眠期间RC32K计数增量。 */
		st_cycle = cycle*RC32K_MUTIPLE+(RC32K_MUTIPLE/3);         /* 按原公式加入倍率1/3的校正项，物理误差来源此处未说明。 */
		sl_dbg("t0:sleep=%d us, cycle=%d(32k)\n", k_cyc_to_us_ceil32(st_cycle), cycle);
		cycle_count += st_cycle;                                  /* 将低速时钟经过的时间折算为HOSC周期并累计。 */
		acts_exit_sleep();                                        /* 通知内核随后重新插入补偿阶段新建的超时项。 */
		#endif
		sys_clock_set_timeout(z_get_next_timeout_expiry(), 0);    /* 以当前最近超时重新配置硬件比较事件。 */
	} else if (action == PM_DEVICE_ACTION_SUSPEND){                   /* 挂起前切换到可在低功耗阶段运行的RC32K。 */
		acts_clock_peripheral_disable(CLOCK_ID_TIMER0);           /* 切换计数源前先关闭外设时钟门控。 */
		sys_write32(0x4, CMU_TIMER0CLK);                          /* 选择低功耗RC32K计数源。 */
		sleep_st_cycle = sys_read32(T0_CNT);                      /* 记录切换低速时钟后的计数基准。 */
		acts_clock_peripheral_enable(CLOCK_ID_TIMER0);            /* 打开TIMER0时钟门控。 */
		#ifdef CONFIG_TIME_SLEEP_COMP
		cycle = sleep_st_cycle-last_cycle_count;                  /* 累计切换前尚未入账的HOSC周期。 */
		cycle_count += cycle;                                     /* 把切换前最后一段高速周期加入累计。 */
		last_cycle_count = sleep_st_cycle;                        /* 令后续差分从切换点开始，避免重复计数。 */
		sl_dbg("t0 suspend: tcnt=%d, t0 used cyc=%d\n", sleep_st_cycle, cycle);
		#endif
	}
	return 0;                                                         /* 原实现无错误检查，本路径始终报告成功。 */
}

#endif

/**
  * @brief  读取距上次向内核报告后已经经过的完整tick数。
  * @param  无。
  * @retval tickless模式返回未报告周期除以CYC_PER_TICK的整数部分；周期模式返回0。
  * @note   内部用自旋锁获得TIMER0软件计数快照。
  */
uint32_t sys_clock_elapsed(void)
{
	#if defined(CONFIG_TICKLESS_KERNEL)
		k_spinlock_key_t key = k_spin_lock(&lock);                               /* 保存中断状态并锁住TIMER0计数快照/比较更新。 */
		uint32_t cyc = acts_timer_elapsed() + cycle_count - announced_cycles;    /* 计算全部未向内核报告的周期数。 */
		k_spin_unlock(&lock, key);                                               /* 结束短临界区，恢复进入前中断状态。 */
		return cyc / CYC_PER_TICK;                                               /* 向下取整，只返回完整tick。 */
	#else
		return 0 ;                                                               /* 周期模式下无额外tick供查询。 */
	#endif
}


/**
  * @brief  读取供k_cycle_get_32使用的32位硬件周期时间。
  * @param  无。
  * @retval TIMER2读数加soc_sleep_cycle补偿值；备用分支返回TIMER0软件累计值。
  * @note   当前启用TIMER2；32MHz时约134.2秒回绕，差值比较须使用无符号运算并限制间隔。
  */
uint32_t sys_clock_cycle_get_32(void)
{
#ifdef USE_T2_FOR_CYCLE
	/* 独立计数路径不依赖TIMER0的tick中断频率；睡眠补偿的来源由SoC休眠模块维护。 */
	return sys_read32(T2_CNT) + soc_sleep_cycle();         /* 合并硬件计数与SoC维护的睡眠周期补偿，按32位回绕。 */
#else
	k_spinlock_key_t key = k_spin_lock(&lock);             /* 保存中断状态并锁住TIMER0计数快照/比较更新。 */
	uint32_t ret = acts_timer_elapsed() + cycle_count ;    /* 备用方案从TIMER0累计周期构造计时值。 */
	k_spin_unlock(&lock, key);                             /* 结束短临界区，恢复进入前中断状态。 */
	return ret;                                            /* 返回在锁内采样的周期快照。 */
#endif
}


/**
  * @brief  接收内核退出空闲通知。
  * @param  无。
  * @retval 无。
  * @note   当前为空实现，不执行硬件恢复或tick上报。
  */
void sys_clock_idle_exit(void)
{


}

/**
  * @brief  接收禁用系统时钟请求。
  * @param  无。
  * @retval 无。
  * @note   当前为空实现，调用后不会停止硬件计数或中断。
  */
void sys_clock_disable(void)
{

}


