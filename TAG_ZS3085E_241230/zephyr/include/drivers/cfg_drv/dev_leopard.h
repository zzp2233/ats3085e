/* Leopard片上资源编号、设备名及显示接口编码；头文件保护避免重复定义。DMA物理通道、虚拟通道和请求槽为不同概念。面板接口编码应与display_controller.h及display.h保持一致。 */
/*
 * Copyright (c) 2020 Linaro Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DEV_LEOPARD_H_
#define DEV_LEOPARD_H_

/*
dma 配置
*/
#define  CONFIG_DMA_0_NAME                  "DMA_0"    /* DMA控制器设备名。 */
#define  CONFIG_DMA_0_PCHAN_NUM              10        /* DMA物理通道总数10。 */
#define  CONFIG_DMA_0_VCHAN_NUM              10        /* 软件虚拟DMA通道数10。 */
#define  CONFIG_DMA_0_VCHAN_PCHAN_NUM        6         /* 用于虚拟通道调度的物理通道数6。 */
#define  CONFIG_DMA_0_VCHAN_PCHAN_START      4         /* 虚拟通道池从物理通道4开始。 */

/*
pwm 配置
*/
#define  CONFIG_PWM_CHANS                    4    /* 此芯片配置的PWM通道数。 */

/*
DSP 配置
*/
#define  CONFIG_DSP_NAME                  "DSP"    /* DSP设备注册名。 */


/*
BTC 配置
*/
#define  CONFIG_BTC_NAME                  "BTC"    /* 蓝牙控制器设备名。 */

/*
 * Audio 配置
 */
#ifndef CONFIG_AUDIO_OUT_ACTS_DEV_NAME
#define CONFIG_AUDIO_OUT_ACTS_DEV_NAME  "audio_out"    /* 未由上层覆盖时使用的音频输出设备名。 */
#endif
#ifndef CONFIG_AUDIO_IN_ACTS_DEV_NAME
#define CONFIG_AUDIO_IN_ACTS_DEV_NAME   "audio_in"     /* 未由上层覆盖时使用的音频输入设备名。 */
#endif

/*
 * DSP 配置
 */
#ifndef CONFIG_DSP_ACTS_DEV_NAME
#define CONFIG_DSP_ACTS_DEV_NAME  "dsp_acts"    /* 未由上层覆盖时使用的DSP驱动名。 */
#endif


/*
PMUADC 配置
*/
#define	PMUADC_ID_CHARGI		(0)     /* PMU ADC的CHARGI逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_BATV			(1)     /* PMU ADC的BATV逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_DC5V			(2)     /* PMU ADC的DC5V逻辑通道编号；不是GPIO号。 */
#define PMUADC_ID_SENSOR		(3)     /* PMU ADC的SENSOR逻辑通道编号；不是GPIO号。 */
#define PMUADC_ID_SVCC			(4)     /* PMU ADC的SVCC逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_LRADC1		(5)     /* PMU ADC的LRADC1逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_VCCI		    (6)         /* PMU ADC的VCCI逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_LRADC2		(7)     /* PMU ADC的LRADC2逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_LRADC3		(8)     /* PMU ADC的LRADC3逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_LRADC4		(9)     /* PMU ADC的LRADC4逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_LRADC5		(10)    /* PMU ADC的LRADC5逻辑通道编号；不是GPIO号。 */
#define	PMUADC_ID_LRADC6		(11)    /* PMU ADC的LRADC6逻辑通道编号；不是GPIO号。 */

/**
 * LCD面板 配置 （与drivers/display/display_controller.h及drivers/display.h同步）
 */
/* 显示接口大类编码 */
#define PANEL_PORT_Unknown  (0)    /* 未知显示接口编码。 */
#define PANEL_PORT_MCU      (1)    /* MCU并口接口大类。 */
#define PANEL_PORT_TR       (2)    /* TR接口大类。 */
#define PANEL_PORT_SPI      (4)    /* SPI接口大类。 */

/* MCU并口子类编码 */
#define PANEL_MCU_8080  (0)    /* Intel 8080 */
#define PANEL_MCU_6800  (1)    /* Moto 6800 */

/* SPI显示接口子类编码 */
#define PANEL_SPI_3LINE_1  (0)    /* SPI_3LINE_1显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_SPI_3LINE_2  (1)    /* SPI_3LINE_2显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_SPI_4LINE_1  (2)    /* SPI_4LINE_1显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_SPI_4LINE_2  (3)    /* SPI_4LINE_2显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_QSPI         (4)    /* QSPI显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_QSPI_SYNC    (5)    /* QSPI_SYNC显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_QSPI_DDR_0   (6)    /* QSPI_DDR_0显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_QSPI_DDR_1   (7)    /* QSPI_DDR_1显示接口子类编号，具体线序由显示驱动解释。 */
#define PANEL_QSPI_DDR_2   (8)    /* QSPI_DDR_2显示接口子类编号，具体线序由显示驱动解释。 */

/* 完整接口编码：高字节大类，低字节子类 */
#define PANEL_PORT_TYPE(major, minor) (((major) << 8) | (minor))    /* major大类左移8位并合并minor子类；输入须避免字段互相覆盖。 */

#define PANEL_PORT_MCU_8080  PANEL_PORT_TYPE(PANEL_PORT_MCU, PANEL_MCU_8080)    /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_MCU_6800  PANEL_PORT_TYPE(PANEL_PORT_MCU, PANEL_MCU_6800)    /* 组合显示接口类型，使用右侧对应大类和子类编码。 */

#define PANEL_PORT_SPI_3LINE_1  PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_SPI_3LINE_1)    /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_SPI_3LINE_2  PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_SPI_3LINE_2)    /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_SPI_4LINE_1  PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_SPI_4LINE_1)    /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_SPI_4LINE_2  PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_SPI_4LINE_2)    /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_QSPI         PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_QSPI)           /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_QSPI_SYNC    PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_QSPI_SYNC)      /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_QSPI_DDR_0   PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_QSPI_DDR_0)     /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_QSPI_DDR_1   PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_QSPI_DDR_1)     /* 组合显示接口类型，使用右侧对应大类和子类编码。 */
#define PANEL_PORT_QSPI_DDR_2   PANEL_PORT_TYPE(PANEL_PORT_SPI, PANEL_QSPI_DDR_2)     /* 组合显示接口类型，使用右侧对应大类和子类编码。 */

/* TR显示接口子类编码 */
#define PANEL_PORT_TR_LCD  PANEL_PORT_TYPE(PANEL_PORT_TR, 0)    /* 组合显示接口类型，使用右侧对应大类和子类编码。 */

/* 显示像素格式位图，RGB各分量位数由名称给出。 */
#define PANEL_PIXEL_FORMAT_RGB_888   (0x1 << 0)    /* RGB_888像素格式标志位，用于选择缓冲区像素布局。 */
#define PANEL_PIXEL_FORMAT_ARGB_8888 (0x1 << 3)    /* ARGB_8888像素格式标志位，用于选择缓冲区像素布局。 */
#define PANEL_PIXEL_FORMAT_RGB_565   (0x1 << 4)    /* RGB_565像素格式标志位，用于选择缓冲区像素布局。 */
#define PANEL_PIXEL_FORMAT_BGR_565   (0x1 << 5)    /* BGR_565像素格式标志位，用于选择缓冲区像素布局。 */
#define PANEL_PIXEL_FORMAT_BGR_888   (0x1 << 6)    /* BGR_888像素格式标志位，用于选择缓冲区像素布局。 */
#define PANEL_PIXEL_FORMAT_XRGB_8888 (0x1 << 7)    /* XRGB_8888像素格式标志位，用于选择缓冲区像素布局。 */

#endif    /* DEV_LEOPARD_H_ */
