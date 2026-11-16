/* 头文件保护避免重复声明；_ASMLANGUAGE分支不引入C类型和函数。C++调用者使用extern C保持C链接名。包含其他模块不代表SPI实际调用其全部接口。 */
/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2016 Linaro Limited.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Leopard系列SoC公共入口，汇集寄存器、时钟、复位、引脚和电源接口。
 *
 */

#ifndef _LEOPARD_SOC_H_
#define _LEOPARD_SOC_H_

#ifdef __cplusplus
extern "C" {
#endif


#include <sys/util.h>

#ifndef _ASMLANGUAGE

#include <soc_regs.h>

/* 引入构建生成的设备树信息；生成文件不在本次注释修改范围内。 */
#include <devicetree.h>

#include "soc_clock.h"
#include "soc_reset.h"
#include "soc_irq.h"
#include "soc_gpio.h"
#include "soc_pinmux.h"
#include "soc_pmu.h"
#include "soc_pm.h"
#include "soc_sleep.h"
#include "soc_timer.h"
#include "soc_boot.h"
#include "soc_se.h"
#include "soc_memctrl.h"
#include "soc_ppi.h"
#include "soc_pstore.h"
#include "soc_psram.h"
#include "soc_powergate.h"


/**
  * @brief  把调试口切换到板级默认组0。
  * @param  无。
  * @retval 无。
  * @note   实现读取并修改JTAG_CTL；仅在调试配置需要时调用。
  */
void jtag_set(void);

/**
  * @brief  读取UID1中供时钟策略使用的芯片配置字段。
  * @param  无。
  * @retval UID1位19:17的原始值0～7。
  * @note   调用方按是否为0分支，不应把该接口理解为测量当前CPU频率。
  */
int soc_dvfs_opt(void);

/**
  * @brief  写看门狗控制寄存器位0以执行喂狗。
  * @param  无。
  * @retval 无。
  * @note   硬件周期与使能状态由其他初始化代码决定。
  */
void soc_watchdog_clear(void);

/**
  * @brief  把持久存储中的看门狗复位计数清零。
  * @param  无。
  * @retval 无。
  * @note   与硬件喂狗不同，更新SOC_PSTORE_TAG_WD_RESET_CNT。
  */
void wd_clear_wdreset_cnt(void);

/**
  * @brief  读取芯片封装标识字段。
  * @param  无。
  * @retval UID1位27:24的原始4位封装编码。
  * @note   此接口不发送蓝牙数据包。
  */
uint8_t ipmsg_btc_get_ic_pkt(void);

//#define CONFIG_SLEEP_DBG
#ifdef CONFIG_SLEEP_DBG
#define sl_dbg      printk    /* 启用休眠调试时使用内核打印接口。 */
#else
#define sl_dbg(...)           /* 关闭休眠调试时展开为空，不计算参数表达式。 */
#endif

#define __FPU_PRESENT			1    /* 声明本SoC具有FPU，供体系结构头使用。 */
#define __MPU_PRESENT           1            /* 声明本SoC具有MPU。 */
#define CHIP_LEOPARD            1            /* 选择Leopard芯片专用编译分支。 */

#endif    /* !_ASMLANGUAGE */

#ifdef __cplusplus
}
#endif

#endif    /* _LEOPARD_SOC_H_ */
