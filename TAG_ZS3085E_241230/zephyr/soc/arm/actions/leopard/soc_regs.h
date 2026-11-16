/* 本表提供内存映射地址，不是存储变量；sys_read32/sys_write32才实际访问硬件。BASE加偏移单位均为字节。裸数字位号须用BIT等构造掩码，*_SHIFT为字段起点，*_MASK可直接用于按位保留或清除；不能将三者混用。访问副作用须以对应驱动或手册为准，此表不保证所有寄存器都可读写。 */
/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Leopard外设寄存器地址、共享内存边界和部分位字段定义。
 */

#ifndef	_ACTIONS_SOC_REGS_H_
#define	_ACTIONS_SOC_REGS_H_
#include "brom_interface.h"

#define RMU_REG_BASE				0x40000000    /* 复位管理单元寄存器基址。 */
#define CMUD_REG_BASE				0x40001000    /* 数字时钟管理寄存器基址。 */
#define CMUA_REG_BASE				0x40000100    /* 模拟时钟及振荡器寄存器基址。 */
#define PMU_REG_BASE				0x40004000    /* 电源管理寄存器基址。 */
#define GPIO_REG_BASE				0x40068000    /* GPIO复用及数据寄存器基址。 */
#define DMA_REG_BASE				0x4001c000    /* DMA寄存器组基址。 */
#define SDMA_REG_BASE				0x40078000    /* SDMA寄存器组基址。 */
#define SDMA0_REG_BASE				0x40078100    /* SDMA0寄存器组基址。 */
#define SDMA1_REG_BASE				0x40078200    /* SDMA1寄存器组基址。 */
#define SDMA2_REG_BASE				0x40078300    /* SDMA2寄存器组基址。 */
#define SDMA3_REG_BASE				0x40078400    /* SDMA3寄存器组基址。 */
#define SDMA4_REG_BASE				0x40078500    /* SDMA4寄存器组基址。 */
#define SDMA_LINE0_REG_BASE			0x40078C00    /* SDMA_LINE0寄存器组基址。 */
#define SDMA_LINE1_REG_BASE			0x40078C20    /* SDMA_LINE1寄存器组基址。 */
#define SDMA_LINE2_REG_BASE			0x40078C40    /* SDMA_LINE2寄存器组基址。 */
#define SDMA_LINE3_REG_BASE			0x40078C60    /* SDMA_LINE3寄存器组基址。 */
#define SDMA_LINE4_REG_BASE			0x40078C80    /* SDMA_LINE4寄存器组基址。 */
#define UART0_REG_BASE				0x40038000    /* UART0寄存器组基址。 */
#define UART1_REG_BASE				0x4003C000    /* UART1寄存器组基址。 */
#define UART2_REG_BASE				0x40040000    /* UART2寄存器组基址。 */

#define MEM_REG_BASE                0x40010000    /* 内存控制寄存器基址。 */
#define SE_REG_BASE                 0x40020000    /* 安全引擎寄存器基址。 */

#define I2C0_REG_BASE				0x40044000    /* I2C0寄存器组基址。 */
#define I2C1_REG_BASE				0x40048000    /* I2C1寄存器组基址。 */
#define I2C2_REG_BASE				0x4004c000    /* I2C2寄存器组基址。 */
#define I2C3_REG_BASE				0x40074000    /* I2C3寄存器组基址。 */

#define SPI0_REG_BASE				0x40028000    /* SPI0寄存器组基址。 */
#define SPI1_REG_BASE				0x4002c000    /* SPI1寄存器组基址。 */
#define SPI2_REG_BASE				0x40030000    /* SPI2寄存器组基址。 */
#define SPI3_REG_BASE				0x40034000    /* SPI3寄存器组基址。 */

#define I2CMT0_REG_BASE				0x40088000    /* I2CMT0寄存器组基址。 */
#define I2CMT1_REG_BASE				0x4008c000    /* I2CMT1寄存器组基址。 */

#define WIC_BASE                    0x40090000                /* 唤醒中断控制寄存器区基址。 */
#define PPI_BASE                    0x40090000                /* 外设事件互连寄存器区基址，与WIC地址区相同。 */
#define DE_REG_BASE                 0x4006C000                /* DE寄存器组基址。 */
#define LCDC_REG_BASE               0x40064000                /* LCDC寄存器组基址。 */
#define GPU_REG_BASE                0x40070000                /* GPU寄存器组基址。 */
#define SPIMT0_REG_BASE				0x40080000    /* SPIMT0寄存器组基址。 */
#define SPIMT1_REG_BASE				0x40084000    /* SPIMT1寄存器组基址。 */
#define JPEG_REG_BASE               0x40094000                /* JPEG寄存器组基址。 */
#define BTC_REG_BASE                0x01100000                /* 蓝牙控制器寄存器区基址。 */
#define SD0_REG_BASE				0x40054000    /* SD0寄存器组基址。 */
#define SD1_REG_BASE				0x40058000    /* SD1寄存器组基址。 */
#define PWM_REG_BASE                0x40060000                /* PWM寄存器组基址。 */
#define PWM_CLK0_BASE               0x40001068                /* PWM通道0时钟寄存器绝对地址。 */





#define HOSC_CTL					(CMUA_REG_BASE + 0x00)    /* 高速晶振控制字；后续宏给出其部分位字段。 */
#define HOSCLDO_CTL					(CMUA_REG_BASE + 0x04)    /* 高速振荡器LDO及RC32M相关控制字。 */
#define CK64M_CTL					(CMUA_REG_BASE + 0x08)    /* 64 MHz RC时钟控制及校准寄存器。 */
#define LOSC_CTL					(CMUA_REG_BASE + 0x10)    /* 低速晶振控制字。 */
#define RC4M_CTL					(CMUA_REG_BASE + 0x14)    /* 4 MHz RC时钟控制字。 */
#define RC192M_CTL                  (CMUA_REG_BASE + 0x18)                        /* 192 MHz RC时钟控制字。 */
#define AVDDLDO_CTL					(CMUA_REG_BASE + 0x1c)    /* 模拟电源LDO控制字。 */
#define COREPLL_CTL					(CMUA_REG_BASE + 0x20)    /* 主PLL控制字，时钟实现按低6位乘8 MHz换算频率。 */

#define RC32K_CTL					(CMUA_REG_BASE + 0x64)    /* 32 kHz RC时钟控制字。 */
#define RC32K_CAL					(CMUA_REG_BASE + 0x68)    /* 32 kHz RC校准配置寄存器。 */
#define RC32K_COUNT					(CMUA_REG_BASE + 0x6C)    /* 32 kHz RC校准计数寄存器。 */


/* 兼容旧代码构建的SPLL地址定义；原SDK说明该DSP PLL并不存在，不可仅凭此宏访问。 */
#define SPLL_CTL					(CMUA_REG_BASE + 0x24)    /* 兼容保留地址，不代表该芯片实际具有DSP PLL。 */
#define DISPLAYPLL_CTL				(CMUA_REG_BASE + 0x30)            /* 显示PLL控制地址定义。 */


#define AUDIOLDO_CTL				(CMUA_REG_BASE + 0x1C)    /* 音频LDO地址，与AVDDLDO_CTL为同一地址。 */
#define AUDIO_PLL0_CTL				(CMUA_REG_BASE + 0x28)    /* 音频PLL0控制字。 */
#define AUDIO_PLL1_CTL				(CMUA_REG_BASE + 0x2C)    /* 音频PLL1控制字。 */

#define HOSCOK_CTL					(CMUA_REG_BASE + 0x70)    /* 高速晶振就绪相关控制字。 */
#define HGLFCLK_CTL                 (CMUA_REG_BASE + 0x74)                        /* 高低频时钟相关控制字。 */






#define CMU_SYSCLK                                                        (CMUD_REG_BASE+0x0000)     /* CPU/系统时钟源、分频及AHB字段。 */
#define CMU_DEVCLKEN0                                                     (CMUD_REG_BASE+0x0004)     /* 时钟门控0组，CLOCK_ID 0～31映射对应位。 */
#define CMU_DEVCLKEN1                                                     (CMUD_REG_BASE+0x0008)     /* 时钟门控1组，CLOCK_ID 32～63映射编号减32位。 */
#define CMU_SD0CLK                                                        (CMUD_REG_BASE+0x0010)     /* SD0时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_SD1CLK                                                        (CMUD_REG_BASE+0x0014)     /* SD1时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_OTFDCLK                                                       (CMUD_REG_BASE+0x0018)     /* OTFD时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_JPEGCLK                                                       (CMUD_REG_BASE+0x001C)     /* JPEG时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_SPI0CLK                                                       (CMUD_REG_BASE+0x0020)     /* SPI0时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_SPI1CLK                                                       (CMUD_REG_BASE+0x0024)     /* SPI1时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_SPI2CLK                                                       (CMUD_REG_BASE+0x0028)     /* SPI2时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_SPI3CLK                                                       (CMUD_REG_BASE+0x002C)     /* SPI3时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_DECLK                                                         (CMUD_REG_BASE+0x0030)     /* DE时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_LCDCLK                                                        (CMUD_REG_BASE+0x0034)     /* LCD时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_GPUCLK                                                        (CMUD_REG_BASE+0x0038)     /* GPU时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_SECCLK                                                        (CMUD_REG_BASE+0x003C)     /* SEC时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_TIMER0CLK                                                     (CMUD_REG_BASE+0x0040)     /* TIMER0时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_TIMER1CLK                                                     (CMUD_REG_BASE+0x0044)     /* TIMER1时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_TIMER2CLK                                                     (CMUD_REG_BASE+0x0048)     /* TIMER2时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_TIMER3CLK                                                     (CMUD_REG_BASE+0x004c)     /* TIMER3时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_TIMER4CLK                                                     (CMUD_REG_BASE+0x0050)     /* TIMER4时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_TIMER5CLK                                                     (CMUD_REG_BASE+0x0054)     /* TIMER5时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_PWM0CLK                                                       (CMUD_REG_BASE+0x0068)     /* PWM0时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_PWM1CLK                                                       (CMUD_REG_BASE+0x006C)     /* PWM1时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_PWM2CLK                                                       (CMUD_REG_BASE+0x0070)     /* PWM2时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_PWM3CLK                                                       (CMUD_REG_BASE+0x0074)     /* PWM3时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_LRADCCLK                                                      (CMUD_REG_BASE+0x0078)     /* LRADC时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2C0CLK                                                       (CMUD_REG_BASE+0x0080)     /* I2C0时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2C1CLK                                                       (CMUD_REG_BASE+0x0084)     /* I2C1时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2C2CLK                                                       (CMUD_REG_BASE+0x0088)     /* I2C2时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2C3CLK                                                       (CMUD_REG_BASE+0x008C)     /* I2C3时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_DSPCLK                                                        (CMUD_REG_BASE+0x0090)     /* DSP时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_DACCLK                                                        (CMUD_REG_BASE+0x0098)     /* DAC时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_ADCCLK                                                        (CMUD_REG_BASE+0x009c)     /* ADC时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2STXCLK                                                      (CMUD_REG_BASE+0x00A0)     /* I2STX时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2SRXCLK                                                      (CMUD_REG_BASE+0x00A4)     /* I2SRX时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_MEMCLKEN0                                                     (CMUD_REG_BASE+0x00B0)     /* 内存时钟门控0组。 */
#define CMU_MEMCLKEN1                                                     (CMUD_REG_BASE+0x00B4)     /* 内存时钟门控1组。 */
#define CMU_MEMCLKSRC0                                                    (CMUD_REG_BASE+0x00C0)     /* 内存时钟源选择0组。 */
#define CMU_MEMCLKSRC1                                                    (CMUD_REG_BASE+0x00C4)     /* 内存时钟源选择1组。 */
#define CMU_S1CLKCTL                                                      (CMUD_REG_BASE+0x00D0)     /* S1状态域时钟控制地址。 */
#define CMU_S1BTCLKCTL                                                    (CMUD_REG_BASE+0x00D4)     /* S1BT状态域时钟控制地址。 */
#define CMU_S2SCLKCTL                                                      (CMUD_REG_BASE+0x00DC)    /* S2S状态域时钟控制地址。 */
#define CMU_S3CLKCTL                                                      (CMUD_REG_BASE+0x00E0)     /* S3状态域时钟控制地址。 */
#define CMU_WDCLK                                                         (CMUD_REG_BASE+0x00E4)     /* WD时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_GPIOCLKCTL                                                    (CMUD_REG_BASE+0x00E8)     /* GPIO时钟控制字。 */
#define CMU_PMUWKUPCLKCTL                                                 (CMUD_REG_BASE+0x00EC)     /* PMU唤醒时钟控制字。 */
#define CMU_DIGITALDEBUG                                                  (CMUD_REG_BASE+0x00F0)     /* 数字时钟调试寄存器。 */
#define CMU_TST_CTL                                                       (CMUD_REG_BASE+0x00F4)     /* 时钟测试控制字。 */
#define CMU_SPIMT0CLK                                                     (CMUD_REG_BASE+0x0100)     /* SPIMT0时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_SPIMT1CLK                                                     (CMUD_REG_BASE+0x0104)     /* SPIMT1时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2CMT0CLK                                                     (CMUD_REG_BASE+0x0110)     /* I2CMT0时钟源及分频配置地址；字段解释见对应驱动。 */
#define CMU_I2CMT1CLK                                                     (CMUD_REG_BASE+0x0114)     /* I2CMT1时钟源及分频配置地址；字段解释见对应驱动。 */

#define RMU_MRCR0					(RMU_REG_BASE + 0x00)    /* 复位控制0组，对应复位编号0～31。 */
#define RMU_MRCR1					(RMU_REG_BASE + 0x04)    /* 复位控制1组，对应复位编号32～63。 */
#define DSP_VCT_ADDR                (RMU_REG_BASE+0x00000080)                    /* DSP向量地址配置。 */
#define DSP_STATUS_EXT_CTL          (RMU_REG_BASE+0x00000084)                    /* DSP扩展状态控制字。 */
#define GPU_STATUS_EXT_CTL          (RMU_REG_BASE+0x00000090)                    /* GPU扩展状态控制字。 */
#define	UID0                        (RMU_REG_BASE+0x000000A0)                    /* 芯片标识字0。 */
#define	UID1                        (RMU_REG_BASE+0x000000A4)                    /* 芯片标识字1，时钟策略读取位19:17，封装标识读取位27:24。 */

#define INTC_BASE					0x40003000                /* 处理器间中断通信寄存器基址。 */
#define INT_TO_DSP					(INTC_BASE+0x00000000)    /* 主CPU向DSP发送中断的寄存器地址。 */
#define INFO_TO_DSP					(INTC_BASE+0x00000004)    /* 发送给DSP的附加信息地址。 */
#define INT_TO_BT_CPU				(INTC_BASE+0x00000010)            /* 主CPU向蓝牙CPU发送中断的地址。 */
#define PENDING_FROM_DSP			(INTC_BASE+0x00000014)            /* 来自DSP的中断待处理状态。 */
#define PENDING_FROM_BT_CPU         (INTC_BASE+0x0000001C)                        /* 来自蓝牙CPU的中断待处理状态。 */

#define MEMORY_CTL					(MEM_REG_BASE)       /* 内存控制字。 */
#define DSP_PAGE_ADDR0				(MEM_REG_BASE+0x00000080)    /* DSP分页映射地址配置项0。 */
#define DSP_PAGE_ADDR1				(MEM_REG_BASE+0x00000084)    /* DSP分页映射地址配置项1。 */
#define DSP_PAGE_ADDR2              (MEM_REG_BASE+0x00000088)                /* DSP分页映射地址配置项2。 */
#define DSP_PAGE_ADDR3              (MEM_REG_BASE+0x0000008c)                /* DSP分页映射地址配置项3。 */

#define WIO0_CTL					(GPIO_REG_BASE + 0x300)    /* WIO0复用及电气控制字，每个WIO相隔4字节。 */
#define WIO1_CTL					(GPIO_REG_BASE + 0x304)    /* WIO1复用及电气控制字，每个WIO相隔4字节。 */
#define WIO2_CTL					(GPIO_REG_BASE + 0x308)    /* WIO2复用及电气控制字，每个WIO相隔4字节。 */
#define WIO3_CTL					(GPIO_REG_BASE + 0x30c)    /* WIO3复用及电气控制字，每个WIO相隔4字节。 */

// Boot ROM API入口地址，由ROM接口表提供。
// FPGA阶段保留的旧固定地址，当前禁用。
//#define SPINOR_API_ADDR				(0x00005860) //(0x00005814)


#define SPINOR_API_ADDR				(p_brom_api->p_spinor_api)    /* 通过ROM接口表取得NOR操作函数表，不是普通外设寄存器地址。 */


// CPU与其他处理器交互使用的共享RAM区域。
#define INTER_RAM_ADDR				(0x2FF18000)                                   /* 处理器交互RAM起点。 */
#define DSP_SHARE_RAM_START_ADDR	(0x2FF1A800)                                           /* 与DSP约定的共享RAM起点，未在linker.ld反映，不可独自更改 */
#define INTER_RAM_SIZE				(DSP_SHARE_RAM_START_ADDR - INTER_RAM_ADDR)    /* 交互RAM长度，按两个边界之差计算，单位字节。 */

//--------------SPI缓存控制寄存器-------------------------------------------//
//--------------寄存器地址---------------------------------------//
#define     SPICACHE_Control_Register_BASE                                    0x40014000                                 /* SPICACHE_Control_Register寄存器组基址。 */
#define     SPICACHE_CTL                                                      (SPICACHE_Control_Register_BASE+0x0000)    /* SPI0缓存控制字。 */
#define     SPICACHE_INVALIDATE                                               (SPICACHE_Control_Register_BASE+0x0004)    /* SPI0缓存失效操作寄存器。 */
#define     SPICACHE_TOTAL_MISS_COUNT                                         (SPICACHE_Control_Register_BASE+0x0010)    /* 总计 未命中 计数寄存器地址。 */
#define     SPICACHE_TOTAL_HIT_COUNT                                          (SPICACHE_Control_Register_BASE+0x0014)    /* 总计 命中 计数寄存器地址。 */
#define     SPICACHE_PROFILE_INDEX_START                                      (SPICACHE_Control_Register_BASE+0x0018)    /* 统计窗口 索引 起点寄存器地址。 */
#define     SPICACHE_PROFILE_INDEX_END                                        (SPICACHE_Control_Register_BASE+0x001C)    /* 统计窗口 索引 终点寄存器地址。 */
#define     SPICACHE_RANGE_INDEX_MISS_COUNT                                   (SPICACHE_Control_Register_BASE+0x0020)    /* 范围内 索引 未命中 计数寄存器地址。 */
#define     SPICACHE_RANGE_INDEX_HIT_COUNT                                    (SPICACHE_Control_Register_BASE+0x0024)    /* 范围内 索引 命中 计数寄存器地址。 */
#define     SPICACHE_PROFILE_ADDR_START                                       (SPICACHE_Control_Register_BASE+0x0028)    /* 统计窗口 地址 起点寄存器地址。 */
#define     SPICACHE_PROFILE_ADDR_END                                         (SPICACHE_Control_Register_BASE+0x002C)    /* 统计窗口 地址 终点寄存器地址。 */
#define     SPICACHE_RANGE_ADDR_MISS_COUNT                                    (SPICACHE_Control_Register_BASE+0x0030)    /* 范围内 地址 未命中 计数寄存器地址。 */
#define     SPICACHE_RANGE_ADDR_HIT_COUNT                                     (SPICACHE_Control_Register_BASE+0x0034)    /* 范围内 地址 命中 计数寄存器地址。 */
#define     SPICACHE_DMA_READ_COUNT                                           (SPICACHE_Control_Register_BASE+0x0038)    /* DMA 读取 计数寄存器地址。 */
#define     SPICACHE_SDMA_READ_COUNT                                          (SPICACHE_Control_Register_BASE+0x003C)    /* SDMA 读取 计数寄存器地址。 */
#define     SPICACHE_DE_READ_COUNT                                            (SPICACHE_Control_Register_BASE+0x0040)    /* DE 读取 计数寄存器地址。 */
#define     MAPPING_MISS_ADDR                                                 (SPICACHE_Control_Register_BASE+0x0060)    /* SPI0地址映射未命中记录。 */

#define     SPI1_CACHE_REGISTER_BASE                                          0x40018000                           /* SPI1_CACHE寄存器组基址。 */
#define     SPI1_CACHE_CTL                                                    (SPI1_CACHE_REGISTER_BASE+0x0000)    /* SPI1缓存控制字。 */
#define     SPI1_CACHE_OPERATE                                                (SPI1_CACHE_REGISTER_BASE+0x0004)    /* SPI1缓存操作控制字。 */
#define     CACHE_OPERATE_ADDR_START                                          (SPI1_CACHE_REGISTER_BASE+0x0008)    /* SPI1缓存操作范围起点。 */
#define     CACHE_OPERATE_ADDR_END                                            (SPI1_CACHE_REGISTER_BASE+0x000c)    /* SPI1缓存操作范围终点。 */
#define     SPI1_CACHE_MCPU_MISS_COUNT                                        (SPI1_CACHE_REGISTER_BASE+0x0010)    /* MCPU 未命中 计数寄存器地址。 */
#define     SPI1_CACHE_MCPU_HIT_COUNT                                         (SPI1_CACHE_REGISTER_BASE+0x0014)    /* MCPU 命中 计数寄存器地址。 */
#define     SPI1_CACHE_MCPU_WRITEBACK_COUNT                                   (SPI1_CACHE_REGISTER_BASE+0x0018)    /* MCPU 回写 计数寄存器地址。 */
#define     SPI1_CACHE_DMA_MISS_COUNT                                         (SPI1_CACHE_REGISTER_BASE+0x0028)    /* DMA 未命中 计数寄存器地址。 */
#define     SPI1_CACHE_DMA_HIT_COUNT                                          (SPI1_CACHE_REGISTER_BASE+0x002c)    /* DMA 命中 计数寄存器地址。 */
#define     SPI1_CACHE_DMA_WRITEBACK_COUNT                                    (SPI1_CACHE_REGISTER_BASE+0x0030)    /* DMA 回写 计数寄存器地址。 */
#define     SPI1_CACHE_PROFILE_ADDR_START                                     (SPI1_CACHE_REGISTER_BASE+0x003c)    /* 统计窗口 地址 起点寄存器地址。 */
#define     SPI1_CACHE_PROFILE_ADDR_END                                       (SPI1_CACHE_REGISTER_BASE+0x0040)    /* 统计窗口 地址 终点寄存器地址。 */
#define     SPI1_CACHE_DE_READ_COUNT                                          (SPI1_CACHE_REGISTER_BASE+0x0050)    /* DE 读取 计数寄存器地址。 */
#define     SPI1_CACHE_DE_WRITE_COUNT                                         (SPI1_CACHE_REGISTER_BASE+0x0054)    /* DE 写入 计数寄存器地址。 */
#define     SPI1_CACHE_GPU_READ_COUNT                                         (SPI1_CACHE_REGISTER_BASE+0x0058)    /* GPU 读取 计数寄存器地址。 */
#define     SPI1_CACHE_GPU_WRITE_COUNT                                        (SPI1_CACHE_REGISTER_BASE+0x005C)    /* GPU 写入 计数寄存器地址。 */
#define     SPI1_CACHE_SDMA_READ_COUNT                                        (SPI1_CACHE_REGISTER_BASE+0x0060)    /* SDMA 读取 计数寄存器地址。 */
#define     SPI1_CACHE_SDMA_WRITE_COUNT                                       (SPI1_CACHE_REGISTER_BASE+0x0064)    /* SDMA 写入 计数寄存器地址。 */
#define     SPI1_CACHE_DMAUNCACHE_READ_COUNT                                  (SPI1_CACHE_REGISTER_BASE+0x0068)    /* DMAUNCACHE 读取 计数寄存器地址。 */
#define     SPI1_CACHE_DMAUNCACHE_WRITE_COUNT                                 (SPI1_CACHE_REGISTER_BASE+0x006C)    /* DMAUNCACHE 写入 计数寄存器地址。 */
#define     SPI1_PSRAM_MAPPING_MISS_ADDR                                      (SPI1_CACHE_REGISTER_BASE+0x0080)    /* SPI1 PSRAM映射未命中地址记录。 */
#define     SPI1_PSRAM_MASTER_WEIGHT                                          (SPI1_CACHE_REGISTER_BASE+0x0090)    /* SPI1 PSRAM各主设备访问权重配置。 */
#define     SPI1_GPU_CTL                                                      (SPI1_CACHE_REGISTER_BASE+0x0094)    /* SPI1缓存的GPU相关控制字。 */

//--------------PMUVDD-------------------------------------------//
//--------------寄存器地址---------------------------------------//
#define     PMUVDD_BASE                                                       0x40004000            /* PMU核心电源控制区基址。 */
#define     VOUT_CTL0                                                         (PMUVDD_BASE+0x00)    /* 电源输出总控制字。 */
#define     VOUT_CTL1_S1                                                      (PMUVDD_BASE+0x04)    /* S1状态电源输出控制字。 */
#define     VOUT_CTL1_S2                                                      (PMUVDD_BASE+0x08)    /* S2状态电源输出控制字。 */
#define     VOUT_CTL1_S3                                                      (PMUVDD_BASE+0X0C)    /* S3状态电源输出控制字。 */
#define     PMU_DET                                                           (PMUVDD_BASE+0x10)    /* PMU检测状态寄存器。 */
#define     VOUT_CTL1_S1M                                                     (PMUVDD_BASE+0x14)    /* S1M状态电源输出控制字。 */
#define     DCDC_VC18_CTL                                                     (PMUVDD_BASE+0X20)    /* VC18降压电源控制字。 */
#define     DCDC_VD12_CTL                                                     (PMUVDD_BASE+0X24)    /* VD12降压电源控制字。 */
// #define     DCDC_VDD_CTL                                                      (PMUVDD_BASE+0X28)
#define     PWRGATE_DIG                                                       (PMUVDD_BASE+0X30)    /* 数字模块电源门控控制字。 */
#define     PWRGATE_DIG_ACK                                                   (PMUVDD_BASE+0X34)    /* 数字模块电源门控响应状态。 */
#define     PWRGATE_RAM                                                       (PMUVDD_BASE+0X38)    /* RAM电源门控控制字。 */
// #define     PWRGATE_RAM_ACK                                                   (PMUVDD_BASE+0X3C)
#define     PMU_INTMASK                                                       (PMUVDD_BASE+0X40)    /* PMU中断屏蔽字。 */
#define     RAM_DEEPSLEEP                                                     (PMUVDD_BASE+0x44)    /* RAM深睡配置字。 */
#define     RAM_LIGHTSLEEP                                                    (PMUVDD_BASE+0x48)    /* RAM浅睡配置字。 */

//--------------PMUSVCC-------------------------------------------//
//--------------寄存器地址---------------------------------------//
#define     PMUSVCC_BASE                                                      0x40004000              /* PMU常供电域基址，SVCC寄存器从0x100偏移开始。 */
#define     CHG_CTL_SVCC                                                      (PMUSVCC_BASE+0X100)    /* 常供电域充电控制字。 */
#define     BDG_CTL_SVCC                                                      (PMUSVCC_BASE+0x104)    /* 常供电域基准相关控制字。 */
#define     SYSTEM_SET_SVCC                                                   (PMUSVCC_BASE+0x108)    /* 常供电域系统设置字。 */
#define     POWER_CTL_SVCC                                                    (PMUSVCC_BASE+0x10C)    /* 常供电域电源控制字。 */
#define     WKEN_CTL_SVCC                                                     (PMUSVCC_BASE+0x110)    /* 唤醒源使能控制字。 */
#define     WAKE_PD_SVCC                                                      (PMUSVCC_BASE+0x114)    /* 唤醒待处理状态寄存器。 */
#define     COUNTER8HZ_SVCC                                                   (PMUSVCC_BASE+0x118)    /* 常供电域8 Hz计数器。 */
#define     ALARM8HZ_SVCC                                                     (PMUSVCC_BASE+0x11C)    /* 8 Hz计数域闹钟比较值。 */
#define     REG_BKUP_SVCC                                                     (PMUSVCC_BASE+0x120)    /* 常供电域备份寄存器。 */


#define     PMUADC_BASE                                                      0x40004000                                                                        /* PMU ADC所在寄存器区基址，ADC控制从0x200偏移开始。 */
#define 	PMUADC_CTL 	  													(PMUADC_BASE+0x200)    /* PMU ADC模拟控制字。 */
#define     PMUADC_INTMASK                                                  (PMUADC_BASE+0X204)                                                                /* PMU ADC中断屏蔽字。 */
#define     PMUADC_PD                                                       (PMUADC_BASE+0X208)                                                                /* PMU ADC待处理状态字。 */
#define     PMUADCDIG_CTL                                                   (PMUADC_BASE+0X20C)                                                                /* PMU ADC数字控制字。 */
#define     CHARGI_DATA                                                     (PMUADC_BASE+0x210)                                                                /* 充电电流ADC原始数据。 */
#define     BATADC_DATA                                                     (PMUADC_BASE+0x214)                                                                /* 电池电压ADC原始数据。 */
#define     DC5VADC_DATA                                                    (PMUADC_BASE+0x218)                                                                /* 外部5 V输入ADC原始数据。 */
#define     SENSADC_DATA                                                    (PMUADC_BASE+0x21C)                                                                /* 传感输入ADC原始数据。 */
#define     SVCCADC_DATA                                                    (PMUADC_BASE+0x220)                                                                /* 常供电域电压ADC原始数据。 */
#define     LRADC1_DATA                                                     (PMUADC_BASE+0x224)                                                                /* 低速ADC通道1原始转换数据，未换算成电压。 */
#define     VCCI_DATA                                                       (PMUADC_BASE+0x228)                                                                /* VCCI通道ADC原始数据。 */
#define     LRADC2_DATA                                                     (PMUADC_BASE+0x22C)                                                                /* 低速ADC通道2原始转换数据，未换算成电压。 */
#define     LRADC3_DATA                                                     (PMUADC_BASE+0x230)                                                                /* 低速ADC通道3原始转换数据，未换算成电压。 */
#define     LRADC4_DATA                                                     (PMUADC_BASE+0x234)                                                                /* 低速ADC通道4原始转换数据，未换算成电压。 */
#define     LRADC5_DATA                                                     (PMUADC_BASE+0x238)                                                                /* 低速ADC通道5原始转换数据，未换算成电压。 */
#define     LRADC6_DATA                                                     (PMUADC_BASE+0x23C)                                                                /* 低速ADC通道6原始转换数据，未换算成电压。 */


//--------------EFUSE-------------------------------------------//

#define     EFUSE_BASE                                                        0x40008000           /* 芯片熔丝读取控制区基址。 */
#define     EFUSE_CTL0                                                        (EFUSE_BASE+0x00)    /* 熔丝读取控制字0。 */
#define     EFUSE_CTL1                                                        (EFUSE_BASE+0x04)    /* 熔丝读取控制字1。 */
#define     EFUSE_CTL2                                                        (EFUSE_BASE+0x08)    /* 熔丝读取控制字2。 */
#define     EFUSE_DATA0                                                       (EFUSE_BASE+0x10)    /* 熔丝数据字0，单位为32位字。 */
#define     EFUSE_DATA1                                                       (EFUSE_BASE+0x14)    /* 熔丝数据字1，单位为32位字。 */
#define     EFUSE_DATA2                                                       (EFUSE_BASE+0x18)    /* 熔丝数据字2，单位为32位字。 */
#define     EFUSE_DATA3                                                       (EFUSE_BASE+0x1C)    /* 熔丝数据字3，单位为32位字。 */
#define     EFUSE_DATA4                                                       (EFUSE_BASE+0x20)    /* 熔丝数据字4，单位为32位字。 */
#define     EFUSE_DATA5                                                       (EFUSE_BASE+0x24)    /* 熔丝数据字5，单位为32位字。 */
#define     EFUSE_DATA6                                                       (EFUSE_BASE+0x28)    /* 熔丝数据字6，单位为32位字。 */
#define     EFUSE_DATA7                                                       (EFUSE_BASE+0x2C)    /* 熔丝数据字7，单位为32位字。 */
#define     EFUSE_DATA8                                                       (EFUSE_BASE+0x30)    /* 熔丝数据字8，单位为32位字。 */
#define     EFUSE_DATA9                                                       (EFUSE_BASE+0x34)    /* 熔丝数据字9，单位为32位字。 */
#define     EFUSE_DATA10                                                      (EFUSE_BASE+0x38)    /* 熔丝数据字10，单位为32位字。 */
#define     EFUSE_DATA11                                                      (EFUSE_BASE+0x3C)    /* 熔丝数据字11，单位为32位字。 */



#define     InterruptController_BASE                                          0xe000e000                               /* Cortex-M系统控制及中断寄存器区基址。 */
#define     NVIC_ISER0                                                        (InterruptController_BASE+0x00000100)    /* 中断使能置位寄存器组0。 */
#define     NVIC_ISER1                                                        (InterruptController_BASE+0x00000104)    /* 中断使能置位寄存器组1。 */
#define     NVIC_ICER0                                                        (InterruptController_BASE+0x00000180)    /* 中断使能清除寄存器组0。 */
#define     NVIC_ICER1                                                        (InterruptController_BASE+0x00000184)    /* 中断使能清除寄存器组1。 */
#define     NVIC_ISPR0                                                        (InterruptController_BASE+0x00000200)    /* 中断待处理置位寄存器组0。 */
#define     NVIC_ISPR1                                                        (InterruptController_BASE+0x00000204)    /* 中断待处理置位寄存器组1。 */
#define     NVIC_ICPR0                                                        (InterruptController_BASE+0x00000280)    /* 中断待处理清除寄存器组0。 */
#define     NVIC_ICPR1                                                        (InterruptController_BASE+0x00000284)    /* 中断待处理清除寄存器组1。 */
#define     NVIC_IABR0                                                        (InterruptController_BASE+0x00000300)    /* 中断活动状态寄存器组0。 */
#define     NVIC_IABR1                                                        (InterruptController_BASE+0x00000304)    /* 中断活动状态寄存器组1。 */
#define     NVIC_IPR0                                                         (InterruptController_BASE+0x00000400)    /* 中断优先级寄存器组0。 */
#define     NVIC_IPR1                                                         (InterruptController_BASE+0x00000404)    /* 中断优先级寄存器组1。 */
#define     NVIC_IPR2                                                         (InterruptController_BASE+0x00000408)    /* 中断优先级寄存器组2。 */
#define     NVIC_IPR3                                                         (InterruptController_BASE+0x0000040c)    /* 中断优先级寄存器组3。 */
#define     NVIC_IPR4                                                         (InterruptController_BASE+0x00000410)    /* 中断优先级寄存器组4。 */
#define     NVIC_IPR5                                                         (InterruptController_BASE+0x00000414)    /* 中断优先级寄存器组5。 */
#define     NVIC_IPR6                                                         (InterruptController_BASE+0x00000418)    /* 中断优先级寄存器组6。 */
#define     NVIC_IPR7                                                         (InterruptController_BASE+0x0000041c)    /* 中断优先级寄存器组7。 */
#define     NVIC_IPR8                                                         (InterruptController_BASE+0x00000420)    /* 中断优先级寄存器组8。 */
#define     NVIC_IPR9                                                         (InterruptController_BASE+0x00000424)    /* 中断优先级寄存器组9。 */


#define     TIMER_REGISTER_BASE                                               0x4000C100    /* 通用定时器寄存器基址。 */

/* 定时器0供系统tick使用 */
#define     T0_CTL                                                            (TIMER_REGISTER_BASE+0x00)    /* 定时器0控制字。 */
#define     T0_VAL                                                            (TIMER_REGISTER_BASE+0x04)    /* 定时器0装载值。 */
#define     T0_CNT                                                            (TIMER_REGISTER_BASE+0x08)    /* 定时器0当前计数值。 */

/* 定时器1供高精度定时使用 */
#define     T1_CTL                                                            (TIMER_REGISTER_BASE+0x20)    /* 定时器1控制字。 */
#define     T1_VAL                                                            (TIMER_REGISTER_BASE+0x24)    /* 定时器1装载值。 */
#define     T1_CNT                                                            (TIMER_REGISTER_BASE+0x28)    /* 定时器1当前计数值。 */

/* 定时器2供系统周期计数使用 */
#define     T2_CTL                                                            (TIMER_REGISTER_BASE+0x40)    /* 定时器2控制字。 */
#define     T2_VAL                                                            (TIMER_REGISTER_BASE+0x44)    /* 定时器2装载值。 */
#define     T2_CNT                                                            (TIMER_REGISTER_BASE+0x48)    /* 定时器2当前计数值。 */

/* 定时器3寄存器组 */
#define     T3_CTL                                                            (TIMER_REGISTER_BASE+0x60)    /* 定时器3控制字。 */
#define     T3_VAL                                                            (TIMER_REGISTER_BASE+0x64)    /* 定时器3装载值。 */
#define     T3_CNT                                                            (TIMER_REGISTER_BASE+0x68)    /* 定时器3当前计数值。 */

/* 定时器4供TWS使用 */
#define     T4_CTL                                                            (TIMER_REGISTER_BASE+0x80)    /* 定时器4控制字。 */
#define     T4_VAL                                                            (TIMER_REGISTER_BASE+0x84)    /* 定时器4装载值。 */
#define     T4_CNT                                                            (TIMER_REGISTER_BASE+0x88)    /* 定时器4当前计数值。 */

#define     T5_CTL                                                            (TIMER_REGISTER_BASE+0xA0)    /* 定时器5控制字。 */
#define     T5_VAL                                                            (TIMER_REGISTER_BASE+0xA4)    /* 定时器5装载值。 */
#define     T5_CNT                                                            (TIMER_REGISTER_BASE+0xA8)    /* 定时器5当前计数值。 */

#define     RTC_REG_BASE      												  0x4000C000                     /* RTC及看门狗寄存器区基址。 */
#define     WD_CTL        													  (RTC_REG_BASE+0x20)    /* 看门狗控制字，喂狗函数置位bit0。 */
#define     HCL_CTL                                                           (RTC_REG_BASE+0x1D0)                                                       /* RTC时钟校准控制字。 */
#define     RTC_REMAIN0                                                       (RTC_REG_BASE+0x30)                                                        /* RTC保留数据字0。 */
#define     RTC_REMAIN1                                                       (RTC_REG_BASE+0x34)                                                        /* RTC保留数据字1。 */
#define     RTC_REMAIN2                                                       (RTC_REG_BASE+0x38)                                                        /* RTC保留数据字2。 */
#define     RTC_REMAIN3                                                       (RTC_REG_BASE+0x3C)                                                        /* RTC保留数据字3。 */
#define     RTC_REMAIN4                                                       (RTC_REG_BASE+0x40)                                                        /* RTC保留数据字4。 */
#define     RTC_REMAIN5                                                       (RTC_REG_BASE+0x44)                                                        /* RTC保留数据字5。 */

/* DAC控制寄存器基址 */
#define     AUDIO_DAC_REG_BASE                                                0x4005C000    /* AUDIO_DAC寄存器组基址。 */

/* ADC控制寄存器基址  */
#define     AUDIO_ADC_REG_BASE                                                0x4005C100                     /* AUDIO_ADC寄存器组基址。 */
#define     ADC_REF_LDO_CTL                                                   (AUDIO_ADC_REG_BASE + 0x48)    /* ADC参考电源LDO控制字。 */

/* I2S发送控制寄存器基址 */
#define     AUDIO_I2STX0_REG_BASE                                             0x4005C400    /* AUDIO_I2STX0寄存器组基址。 */

/* I2S接收控制寄存器基址 */
#define     AUDIO_I2SRX0_REG_BASE                                             0x4005C500    /* AUDIO_I2SRX0寄存器组基址。 */

/* SPI0专用寄存器。 */
#define     SPI0_REGISTER_BASE                                                0x40028000                     /* SPI0寄存器组基址。 */
#define     SPI0_CTL                                                          (SPI0_REGISTER_BASE+0x0000)    /* SPI0控制寄存器地址。 */
#define     SPI0_DELAYCHAIN                                                   (SPI0_REGISTER_BASE+0x0024)    /* SPI0信号延迟链配置地址。 */

/* SPI1专用寄存器。 */
#define     SPI1_REGISTER_BASE                                                0x4002C000                     /* SPI1寄存器组基址。 */
#define     SPI1_CTL                                                          (SPI1_REGISTER_BASE+0x0000)    /* SPI1控制寄存器地址。 */
#define     SPI1_DDR_MODE_CTL                                                 (SPI1_REGISTER_BASE+0x0018)    /* SPI1双沿传输模式控制地址。 */
#define     SPI1_CLKGATING                                                    (SPI1_REGISTER_BASE+0x0038)    /* SPI1时钟门控配置地址。 */
#define     SPI1_DELAYCHAIN                                                   (SPI1_REGISTER_BASE+0x003C)    /* SPI1信号延迟链配置地址。 */
#define     SPI1_DQS1_DELAYCHAIN                                              (SPI1_REGISTER_BASE+0x0040)    /* SPI1 DQS1采样选通信号的延迟链地址。 */

#define     CTK0_BASE                                                         0x4007C000          /* CTK控制区基址。 */
#define     CTK_CTL                                                           (CTK0_BASE+0x00)    /* CTK控制寄存器地址。 */

/* UART控制、数据、状态及波特率寄存器。 */
#define UART0_CTL			(UART0_REG_BASE+0x00)    /* UART0控制字。 */
#define UART0_RXDAT			(UART0_REG_BASE+0x04)    /* UART0接收数据。 */
#define UART0_TXDAT			(UART0_REG_BASE+0x08)    /* UART0发送数据。 */
#define UART0_STA			(UART0_REG_BASE+0x0c)    /* UART0状态字。 */
#define UART0_BR			(UART0_REG_BASE+0x10)    /* UART0波特率配置字。 */

#define UART1_CTL			(UART1_REG_BASE+0x00)    /* UART1控制字。 */
#define UART1_RXDAT			(UART1_REG_BASE+0x04)    /* UART1接收数据。 */
#define UART1_TXDAT			(UART1_REG_BASE+0x08)    /* UART1发送数据。 */
#define UART1_STA			(UART1_REG_BASE+0x0c)    /* UART1状态字。 */
#define UART1_BR			(UART1_REG_BASE+0x10)    /* UART1波特率配置字。 */

//--------------位号、字段起点和掩码------------------------------------------//
#define     HOSC_CTL_HOSC_CAPTUNE_DONE                                        30            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_GMTUNE_DONE                                         29            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_READY                                               28            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_READY_TIME_SET_e                                    27            /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSC_CTL_HOSC_READY_TIME_SET_SHIFT                                26            /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSC_CTL_HOSC_READY_TIME_SET_MASK                                 (0x3<<26)     /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSC_CTL_HOSC_READY_EN                                            25            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_CAP_e                                               24            /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSC_CTL_HOSC_CAP_SHIFT                                           17            /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSC_CTL_HOSC_CAP_MASK                                            (0xFF<<17)    /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSC_CTL_HOSC_CAPTUNE_EN                                          16            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_INJECT_EN                                           15            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_AMPLMT_e                                            14            /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSC_CTL_HOSC_AMPLMT_SHIFT                                        13            /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSC_CTL_HOSC_AMPLMT_MASK                                         (0x3<<13)     /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSC_CTL_HOSC_AMPLMT_EN                                           12            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_GM_e                                                11            /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSC_CTL_HOSC_GM_SHIFT                                            9             /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSC_CTL_HOSC_GM_MASK                                             (0x7<<9)      /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSC_CTL_HOSC_GMTUNE_EN                                           8             /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_INJECT_TIME_SET_e                                   7             /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSC_CTL_HOSC_INJECT_TIME_SET_SHIFT                               6             /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSC_CTL_HOSC_INJECT_TIME_SET_MASK                                (0x3<<6)      /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSC_CTL_HOSC_VDD_SEL                                             5             /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_BUF_SEL                                             4             /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_GMTUNE_TIME_SET_e                                   3             /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSC_CTL_HOSC_GMTUNE_TIME_SET_SHIFT                               2             /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSC_CTL_HOSC_GMTUNE_TIME_SET_MASK                                (0x3<<2)      /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSC_CTL_HOSC_CTL1                                                1             /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSC_CTL_HOSC_CTL0                                                0             /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */

#define     HOSCLDO_CTL_OSC32M_FRQCAL_e                                       31             /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSCLDO_CTL_OSC32M_FRQCAL_SHIFT                                   22             /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSCLDO_CTL_OSC32M_FRQCAL_MASK                                    (0x3FF<<22)    /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSCLDO_CTL_OSC32M_CALDONE                                        21             /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSCLDO_CTL_OSC32M_FRQMSET_e                                      20             /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSCLDO_CTL_OSC32M_FRQMSET_SHIFT                                  11             /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSCLDO_CTL_OSC32M_FRQMSET_MASK                                   (0x3FF<<11)    /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSCLDO_CTL_OSC32M_CTL_e                                          10             /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSCLDO_CTL_OSC32M_CTL_SHIFT                                      9              /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSCLDO_CTL_OSC32M_CTL_MASK                                       (0x3<<9)       /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSCLDO_CTL_OSC32M_CALMODE                                        8              /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSCLDO_CTL_OSC32M_CALEN                                          7              /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSCLDO_CTL_OSC32M_EN                                             6              /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSCLDO_CTL_OSCVDD0_PD_BIASSET                                    5              /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSCLDO_CTL_OSCVDD0_PD                                            4              /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     HOSCLDO_CTL_OSCVDD0_VSET_e                                        3              /* 该字段最高有效位的位置，须结合对应SHIFT和MASK。 */
#define     HOSCLDO_CTL_OSCVDD0_VSET_SHIFT                                    2              /* 该字段最低有效位的位置，移位时使用。 */
#define     HOSCLDO_CTL_OSCVDD0_VSET_MASK                                     (0x3<<2)       /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     HOSCLDO_CTL_OSCVDD0_EN                                            0              /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */

#define     AVDDLDO_CTL_AVDD_PD_EN                                            4    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     AVDDLDO_CTL_AVDD_EN                                               0    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */

#define     PWRGATE_DIG_MAINCPU_PG                                            31    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_RAM0_MAIN_FORCE                                       30    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_RAM_BT_FORCE                                          29    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_BT_PG                                                 28    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_BT_FORCE                                              27    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_DSP_AU_PG                                             26    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_GPU_PG                                                25    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_SHARERAM_FORCE                                        24    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_RAM_AUTOSLEEP                                         23    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_RAM_FASTWAKE                                          22    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_DISPLAY_PG                                            21    /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */

#define     PWRGATE_DIG_ACK_MAINCPU                                           31            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_ACK_BT                                                28            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_ACK_DSP_AU                                            26            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_ACK_GPU                                               25            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_ACK_DISPLAY                                           21            /* 该状态或控制项的位号；使用BIT构造单个位掩码。 */
#define     PWRGATE_DIG_ACK_DSP_GPU_DISP_MASK                                 (0x31<<21)    /* 该字段位掩码，用于提取或保留字段；不是位号。 */
#define     PWRGATE_DIG_ACK_BT_MASK                                           (0x1<<28)     /* 该字段位掩码，用于提取或保留字段；不是位号。 */

#endif    /* _ACTIONS_SOC_REGS_H_	*/
