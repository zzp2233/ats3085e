/* 板级外设接线、休眠引脚集合及查询接口。头文件保护避免重复展开；board_cfg.h给设备开关，soc.h给引脚字段，audio_common.h给音频类型。休眠列表含GPIO57/58，SPI测试依靠FULL_WAKE_LOCK阻止进入会覆盖这些脚的休眠流程。 */
/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __INC_BOARD_H
#define __INC_BOARD_H

#include <board_cfg.h>
#include <drivers/audio/audio_common.h>
#include <soc.h>


/*休眠前将列表中的GPIO置为高阻，唤醒后由电源流程恢复原配置；此宏只生成地址列表 */
#define SLEEP_GPIO_REG_SET_HIGHZ      				\
	/*传感器引脚*/ \
	/*//GPION_CTL(18), 当前未使用，默认已为高阻*/ 		\
	/*//GPION_CTL(20), 当前未使用，默认已为高阻*/ 		\
	GPION_CTL(25), /*音频功放1使能 */	\
	/*TP*/ \
	/*//GPION_CTL(26), 当前未使用，默认已为高阻*/ 		\
	/* //GPION_CTL(27), 当前未使用，默认已为高阻*/ 		\
	GPION_CTL(50), /*触摸中断*/				\
	/*I2C0充电器相关引脚 */  \
	GPION_CTL(57), /*休眠时不使用，置为高阻*/  \
	GPION_CTL(58), /*休眠时不使用，置为高阻*/  \
	/*I2C3音频功放、LCD及电量计相关引脚*/  \
	GPION_CTL(51), /*休眠时不使用，置为高阻*/  \
	GPION_CTL(52), /*休眠时不使用，置为高阻*/  \

#define CONFIG_SD_USE_GPIO_DET      0     /* 1使用GPIO卡检测，0不采用该方案。 */
#define CONFIG_SD_GPIO_DET_NUM      64    /*GPIO64*/
#define CONFIG_SD_GPIO_DET_LEVEL    0     /* SD卡检测有效电平：0低电平，1高电平 */
#define CONFIG_SD_USE_GPIO_POWER    1     /* SD供电GPIO控制使能。 */
#define CONFIG_SD_GPIO_POWER_NUM    64    /*GPIO64*/
#define CONFIG_SD_GPIO_POWER_LEVEL  0     /* SD/eMMC供电有效电平：0低电平，1高电平 */

#define CONFIG_TPKEY_GPIO_ISR_NUM	 		50    /* 触摸中断绝对GPIO号。 */
#define CONFIG_TPKEY_GPIO_SCL_NUM	 		51    /* 触摸I2C时钟绝对GPIO号。 */
#define CONFIG_TPKEY_GPIO_SDA_NUM	 		52    /* 触摸I2C数据绝对GPIO号。 */

#define CONFIG_PANEL_TE_GPIO		GPIO_CFG_MAKE(CONFIG_GPIO_B_NAME, 3, GPIO_ACTIVE_HIGH, 1)     /* LCD的TE同步信号，GPIOB组内3即GPIO35，高有效。 */
#define CONFIG_PANEL_RESET_GPIO		GPIO_CFG_MAKE(CONFIG_GPIO_B_NAME, 0, GPIO_ACTIVE_LOW, 1)      /* LCD复位信号，GPIOB组内0即GPIO32，低有效。 */
//#define CONFIG_PANEL_POWER_GPIO		GPIO_CFG_MAKE(CONFIG_GPIO_B_NAME, 1, GPIO_ACTIVE_HIGH, 1)
#define CONFIG_PANEL_POWER1_GPIO	GPIO_CFG_MAKE(CONFIG_GPIO_A_NAME, 21, GPIO_ACTIVE_HIGH, 1)    /* LCD供电控制GPIO21，高有效。 */

#define CONFIG_TPKEY_ISR_GPIO		GPIO_CFG_MAKE(CONFIG_GPIO_B_NAME, 18, GPIO_ACTIVE_LOW, 1)    /* 触摸中断GPIO50，低有效。 */
#define CONFIG_TPKEY_RESET_GPIO		GPIO_CFG_MAKE(CONFIG_GPIO_B_NAME, 17, GPIO_ACTIVE_LOW, 1)    /* 触摸复位GPIO49，低有效。 */
//#define CONFIG_TPKEY_POWER_GPIO		GPIO_CFG_MAKE(CONFIG_GPIO_A_NAME, 26, GPIO_ACTIVE_HIGH, 1)

#if IS_ENABLED(CONFIG_GPIOKEY)
	/* GPIO按键输入复用配置；按有效电平选择是否上拉 */
#if CONFIG_GPIOKEY_PRESSED_VOLTAGE_LEVEL
#define CONFIG_GPIOKEY_MFP    PIN_MFP_SET(GPIO_24,   GPIOKEY_MFP_CFG)       /* GPIO24按键输入配置，当前分支选择的上下拉策略须匹配按下电平。 */
#else
#define CONFIG_GPIOKEY_MFP    PIN_MFP_SET(GPIO_24,   GPIOKEY_MFP_PU_CFG)    /* GPIO24按键输入配置，当前分支选择的上下拉策略须匹配按下电平。 */
#endif
#endif

#define CONFIG_SPINAND_USE_GPIO_POWER       1     /* SPI NAND供电GPIO控制开关。 */
#define CONFIG_SPINAND_POWER_GPIO           64    /* GPIO64 */
#define CONFIG_SPINAND_GPIO_POWER_LEVEL     0     /* SPI NAND供电有效电平：0低电平，1高电平 */

/* 加速度传感器GPIO配置 */
#define CONFIG_SENSOR_ACC_POWER_GPIO        0    /* 传感器电源控制引脚配置。 */
#define CONFIG_SENSOR_ACC_RESET_GPIO        0    /* 传感器复位引脚配置。 */
#define CONFIG_SENSOR_ACC_ISR_GPIO          0    /* 原用GPIO54，改为0以释放UART2接收脚；加速度采样退回定时触发 */
#define CONFIG_SENSOR_ACC_TRIG_IO           5    /* PPI事件触发通道5 */

/* 心率传感器GPIO配置 */
#define CONFIG_SENSOR_HR_POWER_GPIO        22    /* GPIO22 */
#define CONFIG_SENSOR_HR_RESET_GPIO        0     /* GPIO62 */
#define CONFIG_SENSOR_HR_ISR_GPIO          74    /* GPIO74 */
#define CONFIG_SENSOR_HR_TRIG_IO           6     /* PPI事件触发通道6 */

/* 磁场传感器GPIO配置 */
#define CONFIG_SENSOR_MAG_POWER_GPIO        0    /* 传感器电源控制引脚配置。 */
#define CONFIG_SENSOR_MAG_RESET_GPIO        0    /* 传感器复位引脚配置。 */
#define CONFIG_SENSOR_MAG_ISR_GPIO          0    /* 传感器中断引脚配置；0的禁用语义由传感器驱动解释。 */
#define CONFIG_SENSOR_MAG_TRIG_IO           0    /* 传感器PPI事件触发通道配置，非普通GPIO号。 */

/* 气压传感器GPIO配置 */
#define CONFIG_SENSOR_BARO_POWER_GPIO        0    /* 传感器电源控制引脚配置。 */
#define CONFIG_SENSOR_BARO_RESET_GPIO        0    /* 传感器复位引脚配置。 */
#define CONFIG_SENSOR_BARO_ISR_GPIO          0    /* 传感器中断引脚配置；0的禁用语义由传感器驱动解释。 */
#define CONFIG_SENSOR_BARO_TRIG_IO           0    /* 传感器PPI事件触发通道配置，非普通GPIO号。 */


/* 电池容量估算表，每项为{百分比,电压mV}，按电压递增；仅是板级经验映射。多行宏的反斜杠必须保持行末。 */
#define BOARD_BATTERY_CAP_MAPPINGS      \
	{0, 3200},  \
	{5, 3300},  \
	{10, 3400}, \
	{20, 3550}, \
	{30, 3650}, \
	{40, 3750}, \
	{50, 3800}, \
	{60, 3850}, \
	{70, 3900}, \
	{80, 3950}, \
	{90, 4000}, \
	{100, 4050},

/* 模拟按键映射表，每项为{键码,ADC比较值}；阈值按本板分压电阻设计，不能作为GPIO电平解释。 */
#define BOARD_ADCKEY_KEY_MAPPINGS	\
	{KEY_ADFU,          0x05}, \
	{KEY_PREVIOUSSONG,	0x376},	\
	{KEY_MENU,		    0x532},	\
	{KEY_TBD,	        0x69C},	\
	{KEY_VOLUMEDOWN,    0x86D}, \
	{KEY_VOLUMEUP,	    0x9EE}, \
	{KEY_NEXTSONG,	    0xB87},

/* @brief 音频设备编号由设备类型与实例索引按位或组成 */
#define AUDIO_LINE_IN0 (AUDIO_DEV_TYPE_LINEIN | 0)      /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_LINE_IN1 (AUDIO_DEV_TYPE_LINEIN | 1)      /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_LINE_IN2 (AUDIO_DEV_TYPE_LINEIN | 2)      /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_ANALOG_MIC0 (AUDIO_DEV_TYPE_AMIC | 0)     /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_ANALOG_MIC1 (AUDIO_DEV_TYPE_AMIC | 1)     /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_ANALOG_MIC2 (AUDIO_DEV_TYPE_AMIC | 2)     /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_ANALOG_FM0 (AUDIO_DEV_TYPE_FM | 0)        /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_DIGITAL_MIC0 (AUDIO_DEV_TYPE_DMIC | 0)    /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */
#define AUDIO_DIGITAL_MIC1 (AUDIO_DEV_TYPE_DMIC | 1)    /* 音频设备类型与实例索引组合，供board_audio_device_mapping查表。 */

/* 普通外设引脚表的只读视图，查询函数返回静态表而非复制数据。 */
struct board_pinmux_info{
	const struct acts_pin_config *pins_config;    /* 指向静态普通引脚表，调用者不得改写或释放。 */
	int pins_num;                                 /* 表项数，单位项；配置关闭时查询函数不改此值。 */
};

/* PWM板级表项将引脚、PWM通道和电气模式关联。 */
struct pwm_acts_pin_config{
	unsigned int pin_num;     /* 普通GPIO绝对编号。 */
	unsigned int pin_chan;    /* PWM通道编号，不是GPIO组内位号。 */
	unsigned int mode;        /* 引脚复用与电气配置组合。 */
};

/* PWM引脚表视图，指针指向长期有效静态配置。 */
struct board_pwm_pinmux_info{
	const struct pwm_acts_pin_config *pins_config;    /* 指向静态PWM表；当前禁用电机后表为空，不转移所有权。 */
	int pins_num;                                     /* 表项数，单位项；配置关闭时查询函数不改此值。 */
};


/**
  * @brief  取得 mmc0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_MMC_0 关闭时不写输出对象；调用者应先初始化。
  */
extern void board_get_mmc0_pinmux_info(struct board_pinmux_info *pinmux_info);

/**
  * @brief  取得 spdiftx0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_AUDIO_SPDIFTX_0 关闭时不写输出对象；调用者应先初始化。
  */
extern void board_get_spdiftx0_pinmux_info(struct board_pinmux_info *pinmux_info);

/**
  * @brief  取得 spdifrx0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_AUDIO_SPDIFRX_0 关闭时不写输出对象；调用者应先初始化。
  */
extern void board_get_spdifrx0_pinmux_info(struct board_pinmux_info *pinmux_info);

/**
  * @brief  取得 i2stx0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_AUDIO_I2STX_0 关闭时不写输出对象；调用者应先初始化。
  */
extern void board_get_i2stx0_pinmux_info(struct board_pinmux_info *pinmux_info);

/**
  * @brief  取得 pwm 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_PWM 关闭时不写输出对象；调用者应先初始化。
  */
extern void board_get_pwm_pinmux_info(struct board_pwm_pinmux_info *pinmux_info);

/**
  * @brief  在 NTC 测量、参考测量及关闭状态之间切换引脚。
  * @param  enable：[输入] true 接通测量，false 将两端置高阻。
  * @param  use_as_ntc：[输入] enable 为 true 时选择 NTC 端或参考端。
  * @retval 无。
  * @note   仅 CONFIG_ACTS_BATTERY_NTC 使能时写寄存器；GPIO20/23 不可同时接入 LRADC2。
  */
extern void board_set_ntc_pinmux_info(bool enable, bool use_as_ntc);

#if 1
/* @brief 查询音频设备到板级ADC硬件输入的映射 */

/**
  * @brief  按设备类型填充四路音频输入映射。
  * @param  input_map：[输入输出] 非空可写对象；输入 audio_dev，输出 ch0_input～ch3_input。
  * @retval 0：找到映射；-EINVAL：指针为空；-ENOENT：设备不在表中。
  */
int board_audio_device_mapping(audio_input_map_t *input_map);

/**
  * @brief  通过 GPIO 控制外部功放并等待上电或关断时序。
  * @param  pa_class：[输入] 功放类别，当前实现未使用。
  * @param  is_on：[输入] true 开启，false 关闭。
  * @retval 0：控制流程完成；-EINVAL：配置的 GPIO 设备绑定失败。
  * @note   仅功放配置使能时存在；使用 k_sleep，须在线程上下文调用，GPIO 操作结果未检查。
  */
int board_extern_pa_ctl(u8_t pa_class, bool is_on);
#endif

#ifdef CONFIG_PANEL

/**
  * @brief  分阶段把 LCD 信号设为高阻并备份需恢复的引脚。
  * @param  aod_en：[输入] true 表示保留常亮显示所需供电和复位。
  * @param  early_suspend：[输入] true 时先备份 TE 及非 AOD 供电、复位引脚。
  * @retval 无。
  * @note   先早期挂起再执行后续阶段；数据与时钟每次都置高阻。备份只有一份，不支持嵌套挂起。
  */
void board_lcd_suspend(bool aod_en, bool early_suspend);

/**
  * @brief  恢复 LCD 总线配置并在晚期阶段恢复备份引脚。
  * @param  aod_en：[输入] 与挂起阶段一致的 AOD 状态。
  * @param  late_resume：[输入] true 时恢复有效备份中的 TE 和非 AOD 引脚。
  * @retval 无。
  * @note   须先经过对应挂起以建立备份；总线配置每次由静态表重建。
  */
void board_lcd_resume(bool aod_en, bool late_resume);
#endif

#endif    /* __INC_BOARD_H */
