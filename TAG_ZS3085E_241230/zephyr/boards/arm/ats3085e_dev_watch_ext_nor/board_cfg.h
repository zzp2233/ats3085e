/* 板级静态配置：此文件的CONFIG宏选择设备实例、名称、DMA资源及引脚相关参数；不同于运行时spi_config。SPI2当前启用CPU轮询，本机作主机产生SCLK，振动设备关闭以释放GPIO57。头文件保护避免与dev_config.h相互包含时重复展开。 */
/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BOARD_CFG_H
#define __BOARD_CFG_H

#define LCD_PADDRV_LEVEL (1)        /* LCD数据信号驱动等级。 */
#define LCD_CLK_PADDRV_LEVEL (2)    /* LCD时钟信号驱动等级。 */

#include <drivers/cfg_drv/dev_config.h>
#include <soc.h>

/* 设备开关：1纳入对应实例及板级GPIO配置，0不配置该外设；名称用于device_get_binding检索。
 * 关闭外设不代表其所有资源必然空闲，仍须检查其他复用表和低功耗表。 */
#define CONFIG_GPIO_A							1
#define CONFIG_GPIO_A_NAME      			"GPIOA"    /* 设备绑定或存储选择使用的名称；须与注册实例一致。 */
#define CONFIG_GPIO_B							1
#define CONFIG_GPIO_B_NAME      			"GPIOB"    /* 设备绑定或存储选择使用的名称；须与注册实例一致。 */
#define CONFIG_GPIO_C							1
#define CONFIG_GPIO_C_NAME      			"GPIOC"    /* 设备绑定或存储选择使用的名称；须与注册实例一致。 */
#define CONFIG_WIO      						1
#define CONFIG_WIO_NAME         			"WIO"      /* 设备绑定或存储选择使用的名称；须与注册实例一致。 */
#define CONFIG_EXTEND_GPIO						0
#define CONFIG_EXTEND_GPIO_NAME             "GPIOD"                /* 设备绑定或存储选择使用的名称；须与注册实例一致。 */

#define CONFIG_GPIO_PIN2NAME(x)          (((x) < 32) ? CONFIG_GPIO_A_NAME : (((x) < 64) ? CONFIG_GPIO_B_NAME : CONFIG_GPIO_C_NAME))    /* 按绝对GPIO编号选设备名：0～31为A组，32～63为B组，其余为C组；调用者仍须保证编号有效。 */

#define CONFIG_SPI_FLASH_0						1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPI_FLASH_NAME      			"spi_flash"
#define CONFIG_SPI_FLASH_1                      0                            /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPI_FLASH_1_NAME      		"spi_flash_1"
#define CONFIG_SPI_FLASH_2                      0                            /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPI_FLASH_2_NAME      		"spi_flash_2"
#define CONFIG_SIM_FLASH                        1                            /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SIM_FLASH_NAME               "sim_flash"
#define CONFIG_ACTLOG_STORAGE_NAME           CONFIG_SPI_FLASH_NAME           /* 设备绑定或存储选择使用的名称；须与注册实例一致。 */

#define CONFIG_SPINAND_3                    	0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPINAND_FLASH_NAME      	    "spinand"

#define CONFIG_MMC_0                			0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_MMC_0_NAME           		"MMC_0"
#define CONFIG_MMC_1                			0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_MMC_1_NAME           		"MMC_1"

#define CONFIG_SD                   			1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SD_NAME              		"sd"

#define CONFIG_UART_0           				1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_UART_0_NAME      			"UART_0"
#define CONFIG_UART_1           				0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_UART_1_NAME      			"UART_1"
#define CONFIG_UART_2           				1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_UART_2_NAME      			"UART_2"
#define CONFIG_UART_3           				0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_UART_3_NAME      			"UART_3"
#define CONFIG_UART_4           				0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_UART_4_NAME      			"UART_4"

#define CONFIG_PWM          					1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_PWM_NAME      				"PWM"

#define CONFIG_I2C_0           					0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_I2C_0_NAME      				"I2C_0"
#define CONFIG_I2C_1           					1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_I2C_1_NAME      				"I2C_1"
#define CONFIG_I2C_2           					0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_I2C_2_NAME      				"I2C_2"
#define CONFIG_I2C_3           					0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_I2C_3_NAME      				"I2C_3"

#define CONFIG_SPI_1           					0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPI_1_NAME      				"SPI_1"
#define CONFIG_SPI_2                            1                    /* 启用板级 SPI2 设备实例。 */
#define CONFIG_SPI_2_NAME      				"SPI_2"
#define CONFIG_SPI_3           					0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPI_3_NAME      				"SPI_3"

#define CONFIG_I2CMT_0           				1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_I2CMT_0_NAME      			"I2CMT_0"
#define CONFIG_I2CMT_1           				1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_I2CMT_1_NAME      			"I2CMT_1"

#define CONFIG_SPIMT_0           				0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPIMT_0_NAME      			"SPIMT_0"
#define CONFIG_SPIMT_1           				0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_SPIMT_1_NAME      			"SPIMT_1"


#define CONFIG_AUDIO_DAC_0                      1                                      /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_AUDIO_DAC_0_NAME             "DAC_0"
#define CONFIG_AUDIO_ADC_0						          1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_AUDIO_ADC_0_NAME             "ADC_0"
#define CONFIG_AUDIO_I2STX_0					          0            /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_AUDIO_I2STX_0_NAME           "I2STX_0"
#define CONFIG_AUDIO_I2SRX_0					          0            /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_AUDIO_I2SRX_0_NAME           "I2SRX_0"
#define CONFIG_AUDIO_SPDIFRX_0					0                      /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_AUDIO_SPDIFRX_0_NAME         "SPDIFRX_0"
#define CONFIG_AUDIO_SPDIFTX_0                  0                                      /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_AUDIO_SPDIFTX_0_NAME         "SPDIFTX_0"

#define CONFIG_PANEL							1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_LCD_DISPLAY_DEV_NAME			"lcd_panel"

#define CONFIG_DISPLAY_ENGINE_DEV				1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_DISPLAY_ENGINE_DEV_NAME		"de_acts"

#define CONFIG_DMA2D_LITE_DEV					1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_DMA2D_LITE_DEV_NAME			"dma2d_lite_acts"

#define CONFIG_JPEG_HW_DEV                   1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_JPEG_HW_DEV_NAME          	"jpeg_hw_acts"

#define CONFIG_LCDC_DEV							1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_LCDC_DEV_NAME				"lcdc_acts"

#define CONFIG_GPU_DEV							1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_GPU_DEV_NAME					"gpu"

#define CONFIG_ADCKEY                           0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_INPUT_DEV_ACTS_ADCKEY_NAME   "keyadc"

#define CONFIG_GPIOKEY                          1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_INPUT_DEV_ACTS_GPIOKEY_NAME  "keygpio"

#define CONFIG_ONOFFKEY                         1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_INPUT_DEV_ACTS_ONOFF_KEY_NAME "onoffkey"

#define CONFIG_TPKEY							1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_TPKEY_DEV_NAME				 "tpkey"

#define CONFIG_ACTS_BATTERY						1    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_ACTS_BATTERY_DEV_NAME         "batadc"

#define CONFIG_VIBRATOR                         0    /* 关闭占用 GPIO57 的振动设备。 */
#define CONFIG_VIBRATOR_DEV_NAME             "VIBRATOR"

#define CONFIG_CEC				 				0    /* 对应板级设备开关：1配置该实例，0不启用。 */
#define CONFIG_ACTS_BATTERY_NTC 				1

#define CONFIG_UART_0_USE_TX_DMA   1       /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_UART_0_TX_DMA_CHAN  0x2
#define CONFIG_UART_0_TX_DMA_ID    1       /* 外设DMA硬件请求槽编号，不是软件通道号。 */
#define CONFIG_UART_0_USE_RX_DMA   1
#define CONFIG_UART_0_RX_DMA_CHAN  0xff    /* DMA通道选择；0xff由驱动申请动态通道。 */
#define CONFIG_UART_0_RX_DMA_ID    1

#define CONFIG_UART_1_USE_TX_DMA   0    /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_UART_1_TX_DMA_CHAN  0xff
#define CONFIG_UART_1_TX_DMA_ID    2    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_MMC_0_USE_DMA        1    /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_MMC_0_DMA_CHAN       0xff
#define CONFIG_MMC_0_DMA_ID         5    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_MMC_1_USE_DMA        0    /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_MMC_1_DMA_CHAN       0xff
#define CONFIG_MMC_1_DMA_ID         6    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_PWM_USE_DMA   		1     /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_PWM_DMA_CHAN  		0xff
#define CONFIG_PWM_DMA_ID    		21    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_I2C_0_USE_DMA   0     /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_I2C_0_DMA_CHAN  0xff
#define CONFIG_I2C_0_DMA_ID    19    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_I2C_1_USE_DMA   0     /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_I2C_1_DMA_CHAN  0xff
#define CONFIG_I2C_1_DMA_ID    20    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_I2C_2_USE_DMA   0     /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_I2C_2_DMA_CHAN  0xff
#define CONFIG_I2C_2_DMA_ID    24    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_I2C_3_USE_DMA   0     /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_I2C_3_DMA_CHAN  0xff
#define CONFIG_I2C_3_DMA_ID    25    /* 外设DMA硬件请求槽编号，不是软件通道号。 */

#define CONFIG_I2CMT_0_USE_DMA   0    /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_I2CMT_0_DMA_CHAN  0xff
#define CONFIG_I2CMT_1_USE_DMA   0    /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_I2CMT_1_DMA_CHAN  0xff

#define CONFIG_SPI_1_USE_DMA   1         /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_SPI_1_TXDMA_CHAN  0xff
#define CONFIG_SPI_1_RXDMA_CHAN  0xff    /* DMA通道选择；0xff由驱动申请动态通道。 */
#define CONFIG_SPI_1_DMA_ID    8

#define CONFIG_SPI_2_USE_DMA   0         /* SPI2 从机使用 CPU FIFO 路径，关闭 DMA 收发。 */
#define CONFIG_SPI_2_TXDMA_CHAN  0xff
#define CONFIG_SPI_2_RXDMA_CHAN  0xff    /* DMA通道选择；0xff由驱动申请动态通道。 */
#define CONFIG_SPI_2_DMA_ID    9

#define CONFIG_SPI_3_USE_DMA   1         /* 1使用DMA路径，0使用非DMA路径；仅对应实例启用时生效。 */
#define CONFIG_SPI_3_TXDMA_CHAN  0xff
#define CONFIG_SPI_3_RXDMA_CHAN  0xff    /* DMA通道选择；0xff由驱动申请动态通道。 */
#define CONFIG_SPI_3_DMA_ID    10

#define CONFIG_SPIMT_0_DMA_CHAN  0xff    /* DMA通道选择；0xff由驱动申请动态通道。 */
#define CONFIG_SPIMT_1_DMA_CHAN  0xff

/* DAC FIFO0的DMA通道；0xff请求动态分配。 */
#define CONFIG_AUDIO_DAC_0_FIFO0_DMA_CHAN       (0xff)
/* DAC FIFO0的DMA硬件请求槽编号。 */
#define CONFIG_AUDIO_DAC_0_FIFO0_DMA_ID         (0xb)
/* DAC FIFO1的DMA通道；0xff请求动态分配。 */
#define CONFIG_AUDIO_DAC_0_FIFO1_DMA_CHAN       (0xff)
/* DAC FIFO1的DMA硬件请求槽编号。 */
#define CONFIG_AUDIO_DAC_0_FIFO1_DMA_ID         (0xc)
/* ADC FIFO0的DMA通道；0xff请求动态分配。 */
#define CONFIG_AUDIO_ADC_0_FIFO0_DMA_CHAN       (0xff)
/* ADC FIFO0的DMA硬件请求槽编号。 */
#define CONFIG_AUDIO_ADC_0_FIFO0_DMA_ID         (0xb)
/* ADC FIFO1的DMA通道；0xff请求动态分配。 */
#define CONFIG_AUDIO_ADC_0_FIFO1_DMA_CHAN       (0xff)
/* ADC FIFO1的DMA硬件请求槽编号。 */
#define CONFIG_AUDIO_ADC_0_FIFO1_DMA_ID         (0xc)
/* I2STX FIFO0的DMA通道；0xff请求动态分配。 */
#define CONFIG_AUDIO_I2STX_0_FIFO0_DMA_CHAN     (0xff)
/* I2STX FIFO0的DMA硬件请求槽编号。 */
#define CONFIG_AUDIO_I2STX_0_FIFO0_DMA_ID       (0xe)
/* I2SRX FIFO0的DMA通道；0xff请求动态分配。 */
#define CONFIG_AUDIO_I2SRX_0_FIFO0_DMA_CHAN     (0xff)
/* I2SRX FIFO0的DMA硬件请求槽编号。 */
#define CONFIG_AUDIO_I2SRX_0_FIFO0_DMA_ID       (0xe)
/* SPDIFRX FIFO0的DMA通道；0xff请求动态分配。 */
#define CONFIG_AUDIO_SPDIFRX_0_FIFO0_DMA_CHAN    (0xff)
/* SPDIFRX FIFO0的DMA硬件请求槽编号。 */
#define CONFIG_AUDIO_SPDIFRX_0_FIFO0_DMA_ID      (0x10)

/*
 *  外设中断优先级配置；此处数值按SDK中断控制器规则解释。
 */
#define CONFIG_BTC_IRQ_PRI                		0

#define CONFIG_TWS_IRQ_PRI                		0    /* 对应外设的中断优先级配置。 */

#define CONFIG_UART_0_IRQ_PRI   				0    /* 对应外设的中断优先级配置。 */

#define CONFIG_UART_1_IRQ_PRI   				0    /* 对应外设的中断优先级配置。 */
#define CONFIG_UART_2_IRQ_PRI   				0

#define CONFIG_MMC_0_IRQ_PRI        			0    /* 对应外设的中断优先级配置。 */

#define CONFIG_MMC_1_IRQ_PRI        			0    /* 对应外设的中断优先级配置。 */

#define CONFIG_DMA_IRQ_PRI                      0    /* 对应外设的中断优先级配置。 */

#define CONFIG_MPU_IRQ_PRI                      0    /* 对应外设的中断优先级配置。 */

#define CONFIG_GPIO_IRQ_PRI                     0    /* 对应外设的中断优先级配置。 */

#define CONFIG_I2C_0_IRQ_PRI   					0    /* 对应外设的中断优先级配置。 */

#define CONFIG_I2C_1_IRQ_PRI   					0    /* 对应外设的中断优先级配置。 */

#define CONFIG_I2C_2_IRQ_PRI   					0    /* 对应外设的中断优先级配置。 */

#define CONFIG_I2C_3_IRQ_PRI   					0    /* 对应外设的中断优先级配置。 */

#define CONFIG_SPI_1_IRQ_PRI   					0    /* 对应外设的中断优先级配置。 */

#define CONFIG_SPI_2_IRQ_PRI  					0    /* 对应外设的中断优先级配置。 */

#define CONFIG_SPI_3_IRQ_PRI   					0    /* 对应外设的中断优先级配置。 */

#define CONFIG_I2CMT_0_IRQ_PRI   			    0    /* 对应外设的中断优先级配置。 */

#define CONFIG_I2CMT_1_IRQ_PRI   				0    /* 对应外设的中断优先级配置。 */

#define CONFIG_SPIMT_0_IRQ_PRI   				0    /* 对应外设的中断优先级配置。 */

#define CONFIG_SPIMT_1_IRQ_PRI   				0    /* 对应外设的中断优先级配置。 */

#define CONFIG_AUDIO_DAC_0_IRQ_PRI              0    /* 对应外设的中断优先级配置。 */

#define CONFIG_AUDIO_ADC_0_IRQ_PRI              0    /* 对应外设的中断优先级配置。 */

#define CONFIG_AUDIO_I2STX_0_IRQ_PRI            0    /* 对应外设的中断优先级配置。 */

#define CONFIG_AUDIO_I2SRX_0_IRQ_PRI            0    /* 对应外设的中断优先级配置。 */

#define CONFIG_AUDIO_SPDIFRX_0_IRQ_PRI          0    /* 对应外设的中断优先级配置。 */

#define CONFIG_PMU_IRQ_PRI                      0    /* 对应外设的中断优先级配置。 */

#define CONFIG_RTC_IRQ_PRI                      0    /* 对应外设的中断优先级配置。 */

#define CONFIG_WDT_0_IRQ_PRI                    0    /* 对应外设的中断优先级配置。 */

#define CONFIG_DSP_IRQ_PRI                		0    /* 对应外设的中断优先级配置。 */

#define CONFIG_PMUADC_IRQ_PRI                   0    /* 对应外设的中断优先级配置。 */

/*
spi nor flash配置
*/
#define CONFIG_SPI_FLASH_CHIP_SIZE      0x2000000    /* NOR Flash容量，单位字节，当前32 MiB。 */
#define CONFIG_SPI_FLASH_BUS_WIDTH      4            /* NOR总线数据线数，当前四线。 */
#define CONFIG_SPI_FLASH_DELAY_CHAIN    (28)  //每档0.25 ns
#define CONFIG_SPI_FLASH_NO_IRQ_LOCK    1            /* NOR访问的中断锁配置开关，1选择不加该锁。 */
#define CONFIG_SPI_FLASH_FREQ_MHZ    	100          /* NOR请求时钟，单位MHz。 */

//#define CONFIG_SPI0_NOR_DTR_MODE
//#define CONFIG_SPI0_NOR_QPI_MODE
#define CONFIG_SPI_XIP_READ                /* 启用外部Flash的XIP地址读取路径。 */
#define CONFIG_SPI_XIP_VADDR 0x12000000    /*XIP映射最多32 MB，可按地址直接读取外部Flash*/



/*
spi nand flash配置
*/
#define CONFIG_SPINAND_USE_SPICONTROLER     3         /* SPI NAND所用普通SPI控制器编号。 */
#define CONFIG_SPINAND_FLASH_BUS_WIDTH      4         /* NAND总线线数。 */
#define CONFIG_SPINAND_FLASH_FREQ_MHZ    	96    /* NAND请求时钟，单位MHz。 */

/*
mmc板级配置
*/

#define CONFIG_MMC_0_BUS_WIDTH      4    /* 总线数据线数。 */
#define CONFIG_MMC_0_CLKSEL         0    /*取0或1，须与引脚配置选择一致*/
#define CONFIG_MMC_0_DATA_REG_WIDTH 4

#define CONFIG_MMC_0_USE_GPIO_IRQ   0                     /* 1使用GPIO中断辅助卡检测，0关闭该方案。 */
#define CONFIG_MMC_0_GPIO_IRQ_DEV   CONFIG_GPIO_A_NAME    /*选择GPIOA、GPIOB或GPIOC设备名*/
#define CONFIG_MMC_0_GPIO_IRQ_NUM   10                    /*GPIOA组内第10脚*/
#define CONFIG_MMC_0_GPIO_IRQ_FLAG  0                     /*0为高电平有效，1为低电平有效*/
#define CONFIG_MMC_0_ENABLE_SDIO_IRQ 0                    /* 1使能SD0的SDIO中断 */


#define CONFIG_MMC_1_BUS_WIDTH      4     /* 总线数据线数。 */
#define CONFIG_MMC_1_CLKSEL         0
#define CONFIG_MMC_1_DATA_REG_WIDTH 1     /* 数据寄存器访问宽度，单位字节。 */
#define CONFIG_MMC_1_MFP
#define CONFIG_MMC_1_USE_GPIO_IRQ   0     /* 1使用GPIO中断辅助卡检测，0关闭该方案。 */
#define CONFIG_MMC_1_ENABLE_SDIO_IRQ 0    /* 1使能SD1的SDIO中断 */

#define CONFIG_MMC_ACTS_ERROR_DETAIL    1    /* 1在错误时打印详细诊断 */
#define CONFIG_MMC_WAIT_DAT1_BUSY       1    /* 1等待SD/MMC卡DATA1忙状态结束 */
#define CONFIG_MMC_YIELD_WAIT_DMA_DONE  1    /* 1在等待DMA完成时让出线程 */
#define CONFIG_MMC_SD0_FIFO_WIDTH_8BITS 0    /* 1选择SD0 FIFO的8位传输宽度 */
#define CONFIG_MMC_STATE_FIFO           0    /* 1允许CPU按FIFO状态进行读写 */

/*
sd板级配置
*/
#define CONFIG_SD_MMC_DEV           CONFIG_MMC_0_NAME    /*选择MMC0或MMC1设备名*/


/*
uart板级配置
*/
#define CONFIG_UART_0_SPEED     2000000
#define CONFIG_UART_1_SPEED     115200    /* UART波特率，单位bit/s。 */
#define CONFIG_UART_2_SPEED     115200

/*
pwm板级配置
*/
#define CONFIG_PWM_CYCLE     8000    /* PWM周期配置值，按PWM驱动时钟周期计。 */


/*
I2C板级配置
*/
#define CONFIG_I2C_0_CLK_FREQ  100000
#define CONFIG_I2C_0_MAX_ASYNC_ITEMS 10    /* 异步传输队列允许的最大条目数。 */

#define CONFIG_I2C_1_CLK_FREQ  100000    /* 总线请求频率，单位Hz。 */
#define CONFIG_I2C_1_MAX_ASYNC_ITEMS 3

#define CONFIG_I2C_2_CLK_FREQ  100000    /* 总线请求频率，单位Hz。 */
#define CONFIG_I2C_2_MAX_ASYNC_ITEMS 3

#define CONFIG_I2C_3_CLK_FREQ  100000    /* 总线请求频率，单位Hz。 */
#define CONFIG_I2C_3_MAX_ASYNC_ITEMS 3

/*
SPI板级配置
*/

/*
I2CMT配置
*/
#define CONFIG_I2CMT_0_CLK_FREQ  400000

#define CONFIG_I2CMT_1_CLK_FREQ  400000    /* 总线请求频率，单位Hz。 */



/*
SPIMT配置
*/

/*
tp板级配置
*/
#define CONFIG_TP_RESET_GPIO  1                           /* 使能触摸复位GPIO方案。 */
#define CONFIG_TP_RESET_GPIO_NAME   CONFIG_GPIO_B_NAME    /* 触摸复位所在GPIO设备。 */
#define CONFIG_TP_RESET_GPIO_NUM    17                    /* 触摸复位的组内引脚编号。 */
#define CONFIG_TP_RESET_GPIO_FLAG   GPIO_ACTIVE_LOW       /* 触摸复位低电平有效。 */

/*
audio板级配置
*/

/**
 * DAC工作模式须匹配PCB模拟输出接线。
 * - 0: 单端非直驱模式
 * - 1: 单端VRO直驱模式
 * - 2: 差分模式
 */
#define CONFIG_AUDIO_DAC_0_LAYOUT               (2)

/* 1使能DAC高性能模式，仅支持差分接线 */
#define CONFIG_AUDIO_DAC_HIGH_PERFORMACE_DIFF_EN (1)

#if (CONFIG_AUDIO_DAC_HIGH_PERFORMACE_DIFF_EN == 1)
#define CONFIG_AUDIO_DAC_HIGH_PERFORMANCE_SHCL_PW      (0xc8)
#define CONFIG_AUDIO_DAC_HIGH_PERFORMANCE_SHCL_SET     (0xe1)
#define CONFIG_AUDIO_DAC_HIGH_PERFORMANCE_SHCL_CURBIAS (0)
#endif

/**
 * Leopard DAC功放增益与DARSET设置对应的峰峰值如下：
 * Leopard DAC功放增益与DARSET设置对应的峰峰值如下：
 * 增益档位   DARSET = 0    DARSET = 1
 *   7          2.72VPP       ------
 *   6	        2.23VPP	      ------
 *   5	        1.74VPP       ------
 *   4	        1.20VPP	      2.71VPP
 *   3	        0.98VPP	      2.22VPP
 *   2	        0.76VPP	      1.72VPP
 *   1	        0.54VPP	      1.23VPP
 *   0	        0.32VPP	      0.74VPP
 */
#define CONFIG_AUDIO_DAC_0_PA_VOL               (4)

/* 1使能DAC左右声道混合。 */
#define CONFIG_AUDIO_DAC_0_LR_MIX               (0)

/* 1使能DAC的SDM噪声检测静音。 */
#define CONFIG_AUDIO_DAC_0_NOISE_DETECT_MUTE    (1)

/* SDM静音计数阈值配置。 */
#define CONFIG_AUDIO_DAC_0_SDM_CNT              (0x1000)

/* SDM噪声检测阈值。 */
#define CONFIG_AUDIO_DAC_0_SDM_THRES            (0x800)

/* 1使能连续输出零样本后的DAC自动静音；检测长度由计数配置决定，原说明以512倍样本为单位。 */
#define CONFIG_AUDIO_DAC_0_AUTOMUTE             (0)

/* 1使能ADC到DAC的内部回环。 */
#define CONFIG_AUDIO_DAC_0_LOOPBACK             (0)

/* 1静音DAC左声道。 */
#define CONFIG_AUDIO_DAC_0_LEFT_MUTE            (0)

/* 1静音DAC右声道。 */
#define CONFIG_AUDIO_DAC_0_RIGHT_MUTE           (0)

/* 自动静音检测的计数配置。 */
#define CONFIG_AUDIO_DAC_0_AM_CNT               (0x1000)

/* 自动静音的噪声阈值。 */
#define CONFIG_AUDIO_DAC_0_AM_THRES             (0)

/* 1使能DAC自动静音中断。 */
#define CONFIG_AUDIO_DAC_0_AM_IRQ               (0)

/* PCM缓冲区半空中断阈值。 */
#define CONFIG_AUDIO_DAC_0_PCMBUF_HE_THRES      (0xE0)

/* PCM缓冲区半满中断阈值。 */
#define CONFIG_AUDIO_DAC_0_PCMBUF_HF_THRES      (0xF0)

/* 1在上电时开启外部功放 */
#define CONFIG_POWERON_OPEN_EXTERNAL_PA         (0)

/* 1选择DAC低功耗优先配置  */
#define CONFIG_AUDIO_DAC_POWER_PREFERRED        (1)

/* 1等待PCM缓冲区写入完成 */
#define CONFIG_AUDIO_DAC_WAIT_WRITE_PCMBUF_FINISH     (1)

/* PCM缓冲区写入超时，单位微秒 */
#define CONFIG_AUDIO_DAC_WAIT_WRITE_PCMBUF_TIMEOUT_US (1000000)

/* 等待PCM缓冲区写入的休眠间隔，单位毫秒 */
#define CONFIG_AUDIO_DAC_WAIT_WRITE_PCMBUF_SLEEP_MS   (0)

#if (CONFIG_AUDIO_DAC_WAIT_WRITE_PCMBUF_FINISH != 0)
/* 下次写PCM缓冲区时等待上次提交的数据写完 */
#define CONFIG_AUDIO_DAC_WAIT_WRITE_PCMBUF_NEXT_TIME  (1)
#endif

/********************************** I2STX 配置 **********************************/

/**
 * I2STX 声道数选择。
 *   - 2: 2声道
 *   - 4: 4声道TDM时分复用
 *   - 8: 8声道TDM时分复用
 */
#define CONFIG_AUDIO_I2STX_0_CHANNEL_NUM        (2)

/**
 * I2STX 传输格式选择。
 *   - 0: I2S格式
 *   - 1: 左对齐格式
 *   - 2: 右对齐格式
 *   - 3: TDM格式
 */
#define CONFIG_AUDIO_I2STX_0_FORMAT             (0)

/**
 * I2STX 每个BCLK字槽的数据宽度。
 *   - 0: 32位
 *   - 1: 16位
 */
#define CONFIG_AUDIO_I2STX_0_BCLK_WIDTH         (0)

/* 1使能SRD采样率检测。 */
#define CONFIG_AUDIO_I2STX_0_SRD_EN             (0)

/**
 * I2STX 主从模式选择。
 *   - 0：主机，输出总线时钟
 *   - 1：从机，使用外部总线时钟
 */
#define CONFIG_AUDIO_I2STX_0_MODE               (0)

/* 1允许从模式的MCLK使用内部时钟。 */
#define CONFIG_AUDIO_I2STX_0_SLAVE_INTERNAL_CLK (0)

/**
 * I2STX 左右声道时钟的波形选择。
 *   - 0: I2S的50%占空比
 *   - 1: 宽度为1个BCLK周期
 */
#define CONFIG_AUDIO_I2STX_0_LRCLK_PROC         (0)

/**
 * I2STX MCLK极性选择。
 *   - 0：正常极性
 *   - 1：反相
 */
#define CONFIG_AUDIO_I2STX_0_MCLK_REVERSE       (0)

/* 1在I2STX主模式保持BCLK/LRCLK持续输出。 */
#define CONFIG_AUDIO_I2STX_0_ALWAYS_OPEN        (0)

/**
 * I2STX TDM传输格式选择。
 *   - 0: I2S格式
 *   - 1: 左对齐格式
 */
#define CONFIG_AUDIO_I2STX_0_TDM_FORMAT         (0)

/**
 *  I2STX TDM帧起始位置选择。
 *   - 0: 脉冲LRCLK的上升沿
 *   - 1: 50%占空比LRCLK的上升沿
 *   - 2: 50%占空比LRCLK的下降沿
 */
#define CONFIG_AUDIO_I2STX_0_TDM_FRAME          (0)

/**
 * I2STX 数据输出相对时钟的延迟选择。
 *   - 0: BCLK上升沿之后2个MCLK周期
 *   - 1: BCLK上升沿之后3个MCLK周期
 *   - 2: BCLK上升沿之后4个MCLK周期
 *   - 3: BCLK上升沿之后5个MCLK周期
 */
#define CONFIG_AUDIO_I2STX_0_TX_DELAY           (0)

/**
*   PCM模式使能
*   选择PCM或I2S接口
*   0：禁用PCM，启用I2S
*   1：启用PCM，禁用I2S
*/
#define CONFIG_AUDIO_PCMTX_0_EN                    (0)

/**
*   first PCM模式使能
*   PCM帧格式选择
*   0: 短帧
*   1: 长帧
*/
#define CONFIG_AUDIO_PCMTX_0_FORMART               (1)

/**
*   first PCM模式使能
*   PCM时隙数选择
*   0: 1个时隙
*   1: 2个时隙
*   2: 4个时隙
*   3: 8个时隙
*/
#define CONFIG_AUDIO_PCMTX_0_SLOT                    (1)


/********************************** SPDIFTX 配置 **********************************/

/* 1使用I2STX时钟的二分频作为SPDIFTX时钟源。 */
#define CONFIG_AUDIO_SPDIFTX_0_CLK_I2STX_DIV2       (0)


/********************************** ADC 配置 **********************************/
/* DMA交织模式使用的SRAM结束地址 */
#define AUDIO_IN_DMA_RESERVED_ADDRESS           (0x02000000)



/**
 * ADC通道0高通滤波器自动稳定时间选择。
 *   - 0: 1.3ms（采样率48 kHz时）
 *   - 1: 5ms（采样率48 kHz时）
 *   - 2: 10ms（采样率48 kHz时）
 *   - 3: 20ms（采样率48 kHz时）
 */
#define CONFIG_AUDIO_ADC_0_CH0_HPF_TIME         (1)

/* ADC通道0高通截止频率字段，编码范围0～63。 */
#define CONFIG_AUDIO_ADC_0_CH0_FREQUENCY        (0)

/* 1选择ADC通道0高通滤波器高频范围。 */
#define CONFIG_AUDIO_ADC_0_CH0_HPF_FC_HIGH      (0)

/**
 * ADC通道1高通滤波器自动稳定时间选择。
 *   - 0: 1.3ms（采样率48 kHz时）
 *   - 1: 5ms（采样率48 kHz时）
 *   - 2: 10ms（采样率48 kHz时）
 *   - 3: 20ms（采样率48 kHz时）
 */
#define CONFIG_AUDIO_ADC_0_CH1_HPF_TIME         (1)

/* ADC通道1高通截止频率字段，编码范围0～63。 */
#define CONFIG_AUDIO_ADC_0_CH1_FREQUENCY        (0)

/* 1选择ADC通道1高通滤波器高频范围。 */
#define CONFIG_AUDIO_ADC_0_CH1_HPF_FC_HIGH      (0)

/**
 * ADC通道2高通滤波器自动稳定时间选择。
 *   - 0: 1.3ms（采样率48 kHz时）
 *   - 1: 5ms（采样率48 kHz时）
 *   - 2: 10ms（采样率48 kHz时）
 *   - 3: 20ms（采样率48 kHz时）
 */
#define CONFIG_AUDIO_ADC_0_CH2_HPF_TIME         (1)

/* ADC通道2高通截止频率字段，编码范围0～63。 */
#define CONFIG_AUDIO_ADC_0_CH2_FREQUENCY        (0)

/* 1选择ADC通道2高通滤波器高频范围。 */
#define CONFIG_AUDIO_ADC_0_CH2_HPF_FC_HIGH      (0)

/**
 * ADC通道3高通滤波器自动稳定时间选择。
 *   - 0: 1.3ms（采样率48 kHz时）
 *   - 1: 5ms（采样率48 kHz时）
 *   - 2: 10ms（采样率48 kHz时）
 *   - 3: 20ms（采样率48 kHz时）
 */
#define CONFIG_AUDIO_ADC_0_CH3_HPF_TIME         (1)

/* ADC通道3高通截止频率字段，编码范围0～63。 */
#define CONFIG_AUDIO_ADC_0_CH3_FREQUENCY        (0)

/* 1选择ADC通道3高通滤波器高频范围。 */
#define CONFIG_AUDIO_ADC_0_CH3_HPF_FC_HIGH      (0)

/**
 * 音频LDO输出电压选择。
 *   - 0: 1.6v
 *   - 1: 1.7v
 *   - 2: 1.8v
 *   - 3: 1.9v
 */
#define CONFIG_AUDIO_ADC_0_LDO_VOLTAGE          (1)

/*
 * 三路麦克风VMIC供电控制，数组依次对应控制0、1、2。
 *   - 0x：禁用VMIC运放（原接口编码表示法）
 *   - 2：旁路VMIC运放
 *   - 3：启用VMIC运放
 */
#define CONFIG_AUDIO_ADC_0_VMIC_CTL_ARRAY       {3, 3, 3}

/**
 * VMIC电压数组按各路供电顺序填写；当前数组提供三个元素。
 *   - 0: 0.8 AVCC
 *   - 1: 0.85 AVCC
 *   - 2: 0.9 AVCC
 *   - 3: 0.95 AVCC
 */
#define CONFIG_AUDIO_ADC_0_VMIC_VOLTAGE_ARRAY   {2, 2, 2}

/* 1使能ADC电容快速充电。 */
#define CONFIG_AUDIO_ADC_0_FAST_CAP_CHARGE      (0)

/**
 * 回声消除AEC的ADC通道选择。
 *   - 0: 选择ADC0用于AEC
 *   - 1: 选择ADC1用于AEC
 */
#define CONFIG_AUDIO_ADC_0_AEC_SEL              (1)
/********************************** I2SRX 配置 **********************************/
/**
 * I2SRX 声道数选择。
 *   - 2: 2声道
 *   - 4: 4声道TDM时分复用
 *   - 8: 8声道TDM时分复用
 */
#define CONFIG_AUDIO_I2SRX_0_CHANNEL_NUM        (2)

/**
 * I2SRX 传输格式选择。
 *   - 0: I2S格式
 *   - 1: 左对齐格式
 *   - 2: 右对齐格式
 *   - 3: TDM格式
 */
#define CONFIG_AUDIO_I2SRX_0_FORMAT             (0)

/**
 * I2SRX 每个BCLK字槽的数据宽度。
 *   - 0: 32位
 *   - 1: 16位
 */
#define CONFIG_AUDIO_I2SRX_0_BCLK_WIDTH         (0)

/* 1使能SRD采样率检测。 */
#define CONFIG_AUDIO_I2SRX_0_SRD_EN             (1)

/**
 * I2SRX 主从模式选择。
 *   - 0：主机，输出总线时钟
 *   - 1：从机，使用外部总线时钟
 */
#define CONFIG_AUDIO_I2SRX_0_MODE               (0)

/* 1允许从模式的MCLK使用内部时钟。 */
#define CONFIG_AUDIO_I2SRX_0_SLAVE_INTERNAL_CLK    (0)

/**
 * I2STX 左右声道时钟的波形选择。
 *   - 0: I2S的50%占空比
 *   - 1: 宽度为1个BCLK周期
 */
#define CONFIG_AUDIO_I2SRX_0_LRCLK_PROC         (0)

/**
 * I2SRX MCLK极性选择。
 *   - 0：正常极性
 *   - 1：反相
 */
#define CONFIG_AUDIO_I2SRX_0_MCLK_REVERSE       (0)

/**
 * I2SRX TDM传输格式选择。
 *   - 0: I2S格式
 *   - 1: 左对齐格式
 */
#define CONFIG_AUDIO_I2SRX_0_TDM_FORMAT         (0)

/**
 *  I2SRX TDM帧起始位置选择。
 *   - 0: 脉冲LRCLK的上升沿
 *   - 1: 50%占空比LRCLK的上升沿
 *   - 2: 50%占空比LRCLK的下降沿
 */
#define CONFIG_AUDIO_I2SRX_0_TDM_FRAME          (0)

/* 1让I2SRX时钟来自I2STX。 */
#define CONFIG_AUDIO_I2SRX_0_CLK_FROM_I2STX     (0)

/**
*   PCM模式使能
*   选择PCM或I2S接口
*   0：禁用PCM，启用I2S
*   1：启用PCM，禁用I2S
*/
#define CONFIG_AUDIO_PCMRX_0_EN                    (0)

/**
*   first PCM模式使能
*   PCM帧格式选择
*   0: 长帧
*   1: 短帧
*/
#define CONFIG_AUDIO_PCMRX_0_FORMART               (1)

/**
*   first PCM模式使能
*   PCM时隙数选择
*   0: 1个时隙
*   1: 2个时隙
*   2: 4个时隙
*   3: 8个时隙
*/
#define CONFIG_AUDIO_PCMRX_0_SLOT                    (1)


/********************************** SPDIFRX 配置 **********************************/

/* SPDIF接收所需COREPLL最低频率，单位Hz。 */
#define CONFIG_AUDIO_SPDIFRX_0_MIN_COREPLL_CLOCK (50000000)

/*
 * dma2d lite配置
 */
#define CONFIG_DMA2D_LITE_SDMA_CHAN  1    /* DMA2D Lite使用的SDMA通道。 */

/*
 * jpeg hw配置
 */
#define CONFIG_JPEG_HW_INPUT_SDMA_CHAN   2    /* JPEG输入使用的SDMA通道。 */

#define CONFIG_JPEG_HW_OUTPUT_SDMA_CHAN  3    /* JPEG输出使用的SDMA通道。 */

#define CONFIG_JPEG_HW_COUPLE_SDMA_CHAN  4    /* JPEG耦合模式使用的SDMA通道。 */

#define CONFIG_MEM_OPT_SDMA_CHAN  		4    /* 内存操作复用的SDMA通道，须与JPEG资源协调。 */

/*
 * jpeg配置
 */
/* JPEG时钟，单位kHz */
#define CONFIG_JPEG_CLOCK_KHZ  (200000)

/*
 * GPU配置
 */
/* GPU时钟，单位kHz */
#define CONFIG_GPU_CLOCK_KHZ  (100000)

/*
 * DE配置
 */
/* 显示引擎时钟，单位kHz */
#define CONFIG_DISPLAY_ENGINE_CLOCK_KHZ  (200000)

/*
 * LCDC配置
 */
/* 1使能LCDC纵向翻转 */
#define CONFIG_LCDC_Y_FLIP  0

/*
 * panel配置
 */
#define CONFIG_PANEL_PORT_TYPE		PANEL_PORT_QSPI    /* 面板总线使用QSPI接口。 */
#define CONFIG_PANEL_PORT_CS		(0)                /* 面板片选索引。 */
#define CONFIG_PANEL_PORT_SPI_CPOL  (1)                    /* 面板时钟空闲极性。 */
#define CONFIG_PANEL_PORT_SPI_CPHA  (1)                    /* 面板采样相位。 */
#define CONFIG_PANEL_PORT_SPI_DUAL_LANE	(1)                /* 面板双数据线传输开关。 */
/* 可选AHB时钟除数：1、2、4、8 */
#define CONFIG_PANEL_PORT_SPI_AHB_CLK_DIVISION (2)
/* 面板物理水平像素数 */
#define CONFIG_PANEL_TIMING_HACTIVE	(466)
/* 面板物理垂直像素数 */
#define CONFIG_PANEL_TIMING_VACTIVE	(466)
/* 像素传输时钟，单位kHz */
#define CONFIG_PANEL_TIMING_PIXEL_CLK_KHZ (60000)
/* 刷新率，单位Hz */
#define CONFIG_PANEL_TIMING_REFRESH_RATE_HZ (40)
/* 面板具有TE同步信号 */
#define CONFIG_PANEL_TIMING_TE_ACTIVE	(1)

//#define CONFIG_PANEL_BACKLIGHT_PWM		PWM_CFG_MAKE(CONFIG_PWM_NAME, 7, 255, 1)
//#define CONFIG_PANEL_BACKLIGHT_GPIO	GPIO_CFG_MAKE(CONFIG_GPIO_C_NAME, 0, GPIO_ACTIVE_HIGH, 1)

#define CONFIG_PANEL_BRIGHTNESS_DELAY_PERIODS (0)    /* 亮度设置延迟的刷新周期数。 */

/* 亮度范围0～255 */
#define CONFIG_PANEL_BRIGHTNESS		(255)
#define CONFIG_PANEL_AOD_BRIGHTNESS (128)        /* AOD常亮显示亮度，范围与普通亮度一致。 */
#define CONFIG_PANEL_TE_SCANLINE	(300)    /* TE同步扫描线位置，单位行。 */

/* 由面板材料等原因引入的固定像素偏移 */
#define CONFIG_PANEL_FIX_OFFSET_X (6)
#define CONFIG_PANEL_FIX_OFFSET_Y (0)
/* 向上层报告的逻辑显示区域 */
#define CONFIG_PANEL_HOR_RES	(CONFIG_PANEL_TIMING_HACTIVE)    /* 上报水平逻辑分辨率，单位像素。 */
#define CONFIG_PANEL_VER_RES	(CONFIG_PANEL_TIMING_VACTIVE)    /* 上报垂直逻辑分辨率，单位像素。 */
#define CONFIG_PANEL_OFFSET_X	(0)                              /* 逻辑区域水平起始偏移，单位像素。 */
#define CONFIG_PANEL_OFFSET_Y	(0)                              /* 逻辑区域垂直起始偏移，单位像素。 */
/* 1表示圆形面板 */
#define CONFIG_PANEL_ROUND_SHAPE	(1)
/* 静电异常ESD检查周期，单位毫秒 */
#define CONFIG_PANEL_ESD_CHECK_PERIOD 3000

/* 全屏刷新优化：按从上到下排列3～7个矩形区域，格式(x1,y1,x2,y2)。
 * 位置及尺寸都必须为偶数；#if 0保留七区域方案，当前采用三个区域。 */
#if 0
#define CONFIG_PANEL_FULL_SCREEN_OPT_AREA \
	{ \
		{ 124 - CONFIG_PANEL_OFFSET_X,   0 - CONFIG_PANEL_OFFSET_Y, 341 - CONFIG_PANEL_OFFSET_X,  27 - CONFIG_PANEL_OFFSET_Y }, \
		{  68 - CONFIG_PANEL_OFFSET_X,  28 - CONFIG_PANEL_OFFSET_Y, 397 - CONFIG_PANEL_OFFSET_X,  67 - CONFIG_PANEL_OFFSET_Y }, \
		{  28 - CONFIG_PANEL_OFFSET_X,  68 - CONFIG_PANEL_OFFSET_Y, 437 - CONFIG_PANEL_OFFSET_X, 123 - CONFIG_PANEL_OFFSET_Y }, \
		{   0 - CONFIG_PANEL_OFFSET_X, 124 - CONFIG_PANEL_OFFSET_Y, 465 - CONFIG_PANEL_OFFSET_X, 341 - CONFIG_PANEL_OFFSET_Y }, \
		{  28 - CONFIG_PANEL_OFFSET_X, 342 - CONFIG_PANEL_OFFSET_Y, 437 - CONFIG_PANEL_OFFSET_X, 397 - CONFIG_PANEL_OFFSET_Y }, \
		{  68 - CONFIG_PANEL_OFFSET_X, 398 - CONFIG_PANEL_OFFSET_Y, 397 - CONFIG_PANEL_OFFSET_X, 437 - CONFIG_PANEL_OFFSET_Y }, \
		{ 124 - CONFIG_PANEL_OFFSET_X, 438 - CONFIG_PANEL_OFFSET_Y, 341 - CONFIG_PANEL_OFFSET_X, 465 - CONFIG_PANEL_OFFSET_Y }, \
	}
#else
#define CONFIG_PANEL_FULL_SCREEN_OPT_AREA \
	{ \
		{ 68 - CONFIG_PANEL_OFFSET_X,   0 - CONFIG_PANEL_OFFSET_Y, 397 - CONFIG_PANEL_OFFSET_X,  67 - CONFIG_PANEL_OFFSET_Y }, \
		{  0 - CONFIG_PANEL_OFFSET_X,  68 - CONFIG_PANEL_OFFSET_Y, 465 - CONFIG_PANEL_OFFSET_X, 397 - CONFIG_PANEL_OFFSET_Y }, \
		{ 68 - CONFIG_PANEL_OFFSET_X, 398 - CONFIG_PANEL_OFFSET_Y, 397 - CONFIG_PANEL_OFFSET_X, 465 - CONFIG_PANEL_OFFSET_Y }, \
	}
#endif

/*
 * tp配置
 */
#define CONFIG_TPKEY_I2C_NAME		CONFIG_I2C_1_NAME    /* 触摸通信使用I2C1设备。 */
#define CONFIG_TPKEY_LOWPOWER		(1)                  /* 触摸低功耗支持开关。 */


/*
PMU配置
*/

/* 1使能电源键短按检测。 */
#define CONFIG_PMU_ONOFF_SHORT_DETECT            (1)

/* 1表示电源键与遥控键共用同一WIO */
#define CONFIG_PMU_ONOFF_REMOTE_SAME_WIO         (1)

/*
PMUADC配置
*/
#define CONFIG_PMUADC_DEBOUNCE                   (1)    /* PMU ADC消抖开关。 */

/** PMUADC 电池通道过采样次数
 *   - 0: 关闭过采样
 *   - 1: 4次采样
 *   - 2: 8次采样
 *   - 3: 16次采样
 */
#define CONFIG_PMUADC_BAT_AVG_CNT                (2)

/* 1等待PMUADC平均采样完成  */
#define CONFIG_PMUADC_BAT_WAIT_AVG_COMPLETE      (0)

/** PMUADC LRADC1通道过采样次数
 *   - 0: 关闭过采样
 *   - 1: 4次采样
 *   - 2: 8次采样
 *   - 3: 16次采样
 */
#define CONFIG_PMUADC_LRADC1_AVG                 (0)

/** PMUADC LRADC2通道过采样次数
 *   - 0: 关闭过采样
 *   - 1: 4次采样
 *   - 2: 8次采样
 *   - 3: 16次采样
 */
#define CONFIG_PMUADC_LRADC2_AVG                 (2)

/**
 * PMU ADC低速通道的时钟源选择。
 *   - 0: RC32K
 *   - 1: CK32K768
 *   - 2: RC4M/16
 *   - 3: HOSC/128
 */
#define CONFIG_PMUADC_CLOCK_SOURCE               (3)

/**
 * PMU ADC低速通道的时钟分频选择。
 *   - 0: /1
 *   - 1: /2
 *   - 2: /4
 *   - 3: /8
 */
#define CONFIG_PMUADC_CLOCK_DIV                  (0)

/**
 * PMU ADC前级缓冲偏置电流选择。
 *   - 0: 0.25uA
 *   - 1: 0.5uA
 *   - 2: 0.75uA
 *   - 3: 1uA
 */
#define CONFIG_PMUADC_IBIAS_BUF_SEL              (0)

/**
 * PMU ADC核心偏置电流选择。
 *   - 0: 0.25uA
 *   - 1: 0.5uA
 *   - 2: 0.75uA
 *   - 3: 1uA
 */
#define CONFIG_PMUADC_IBIAS_ADC_SEL              (1)

/* 8 Hz计数器同步超时，单位微秒 */
#define CONFIG_PMU_COUNTER8HZ_SYNC_TIMEOUT_US    (2000000)

/* 1在关机时备份时间 */
#define CONFIG_PM_BACKUP_TIME_FUNCTION_EN        (1)

#define CONFIG_PM_BACKUP_TIME_NVRAM_ITEM_NAME    "PM_BAK_TIME"    /* 时间备份使用的NVRAM键名，字符串保持原值。 */

/*
ADCKEY配置
*/
/* PMU ADC按键轮询间隔，单位毫秒。 */
#define CONFIG_ADCKEY_POLL_INTERVAL_MS           (20)

/* PMU ADC按键轮询总时长，单位毫秒。 */
#define CONFIG_ADCKEY_POLL_TOTAL_MS              (1000)

/* 采样滤波所需连续稳定次数。 */
#define CONFIG_ADCKEY_SAMPLE_FILTER_CNT          (3)

/* 模拟按键使用的LRADC通道 */
#define CONFIG_ADCKEY_LRADC_CHAN                 (PMUADC_ID_LRADC3)

/*
ONOFFKEY配置
*/
/* 电源键长按阈值编码0～7依次对应0.125、0.25、0.5、1、1.5、2、3、4秒。
 * 各档中按下超过50 ms且小于阈值为短按，大于等于阈值为长按。 */
#define CONFIG_ONOFFKEY_LONG_PRESS_TIME                (3)

/*
 * 电源键功能选择。
 *   - 0：无功能
 *   - 1：复位
 *   - 2：重新启动
 */
#define CONFIG_ONOFFKEY_FUNCTION                       (1)

/* PMU ADC按键轮询间隔，单位毫秒。 */
#define CONFIG_ONOFFKEY_POLL_INTERVAL_MS               (20)

/* 电源按键轮询总时长，单位毫秒 */
#define CONFIG_ONOFFKEY_POLL_TOTAL_MS                  (6000)

/* 电源按键采样滤波的连续稳定次数 */
#define CONFIG_ONOFFKEY_SAMPLE_FILTER_CNT              (3)

/* 上报的电源按键键值 */
#define CONFIG_ONOFFKEY_USER_KEYCODE                   (1)    /* input_dev.h中的KEY_POWER键码 */

/*
GPIOKEY配置
*/
/* GPIO按键轮询间隔，单位毫秒。 */
#define CONFIG_GPIOKEY_POLL_INTERVAL_MS                (20)

/* GPIO按键轮询总时长，单位毫秒 */
#define CONFIG_GPIOKEY_POLL_TOTAL_MS                   (6000)

/* GPIO按键采样滤波的连续稳定次数 */
#define CONFIG_GPIOKEY_SAMPLE_FILTER_CNT               (3)

/* GPIO按下时的有效电平 */
#define CONFIG_GPIOKEY_PRESSED_VOLTAGE_LEVEL           (1)

/* 上报的GPIO按键键值 */
#define CONFIG_GPIOKEY_USER_KEYCODE                    (9)    /* input_dev.h中的KEY_TBD键码 */

/*
RTC配置
*/
/**
 *  RTC时钟源选择。
 * - 0: RTC_CLKSRC_HOSC_4HZ
 * - 1: RTC_CLKSRC_LOSC_100HZ
 * - 2: RTC_CLKSRC_HCL_RC32K_100HZ
 */
#define CONFIG_RTC_CLK_SOURCE                    (2)

/* 1使能关机RTC时钟校准，仅CONFIG_RTC_CLK_SOURCE=2时允许。 */
#define CONFIG_RTC_ENABLE_CALIBRATION              1

/*
Watchdog配置
*/

/*
Battery配置
*/

/* 电池电压调试输出间隔，单位秒。 */
#define CONFIG_BATTERY_DEBUG_INTERVAL_SEC        (60)

#ifdef CONFIG_ACTS_BATTERY_SUPPLY_EXT_COULOMETER
/* 外部电量计设备名 */
#define CONFIG_ACTS_EXT_COULOMETER_DEV_NAME     "coulometer"

/* 外部电量计使用的I2C设备名 */
#define CONFIG_COULOMETER_I2C_NAME		CONFIG_I2C_1_NAME

/* 外部电量计轮询间隔，单位毫秒 */
#define CONFIG_COULOMETER_INTERVAL_MSEC        (1000)

#endif

#ifdef CONFIG_ACTS_BATTERY_SUPPLY_EXTERNAL
/* 外部充电器使用的I2C设备名 */
#define CONFIG_EXT_CHARGER_I2C_NAME		CONFIG_I2C_1_NAME

#define CONFIG_EXT_CHARGER_ISR_GPIO		GPIO_CFG_MAKE(CONFIG_WIO_NAME, 1, GPIO_ACTIVE_LOW, 1)    // WIO1

#endif


#endif    /* __BOARD_CFG_H */
