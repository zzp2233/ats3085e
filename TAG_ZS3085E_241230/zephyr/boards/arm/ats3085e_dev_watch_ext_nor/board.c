/* 板级配置把逻辑外设映射到封装引脚；PRE_KERNEL_1 阶段先配置 SPI2 引脚，应用随后按设备名取得驱动。各条件分支仅在对应 CONFIG 使能时编译。 */
/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief 板级引脚复用、启动初始化及外设低功耗引脚管理。
 */

#include <kernel.h>
#include <init.h>
#include <device.h>
#include <soc.h>
#include "board.h"
#include <drivers/gpio.h>
#include <board_cfg.h>

/* 本板用 0x1000 表示引脚高阻配置；挂起时避免外设信号继续驱动线路。 */
#define CONFIG_GPIO_HIGHZ  (0x1000)
/* 上电引脚表；若多个外设选择同一引脚，后写入的配置覆盖前者，板级开关必须避免冲突。 */
static const struct acts_pin_config board_pin_config[] = {
    /* UART0 引脚组。 */
#if IS_ENABLED(CONFIG_UART_0)
    /* UART0 发送输出。 */
    PIN_MFP_SET(GPIO_28,  UART0_MFP_CFG),
    /* UART0 接收输入。 */
    PIN_MFP_SET(GPIO_29,  UART0_MFP_CFG),
#endif

    /* UART1 引脚组。 */
#if IS_ENABLED(CONFIG_UART_1)
    /* UART1 发送输出。 */
    PIN_MFP_SET(GPIO_16,  UART1_MFP_CFG),
    /* UART1 接收输入。 */
    PIN_MFP_SET(GPIO_17,  UART1_MFP_CFG),
#endif

    /* UART2 引脚组。 */
#if IS_ENABLED(CONFIG_UART_2)
    /* UART2 发送输出： 驱动强度拉到最大档 PADDRV=7。
     * 实测依据：PADDRV=0 与 PADDRV=4 时，"输出低"只能把线上拉到
     * 1.37V，因为 4G 模组的 UART_RXD 引脚内部有约 4.7K 上拉顶住；
     * 同一条件改 PADDRV=7 后能拉到 0.1V，模组才判得出低电平。
     */
    PIN_MFP_SET(GPIO_53,  (UART2_MFP_CFG | GPIO_CTL_PADDRV_LEVEL(7))),
    /* UART2 接收输入。 */
    PIN_MFP_SET(GPIO_54,  UART2_MFP_CFG),
#endif

#if IS_ENABLED(CONFIG_SPI_FLASH_0)
    /* SPI0 片选。 */
    PIN_MFP_SET(GPIO_0,   SPINOR_MFP_CFG),
    /* SPI0 从机输出/主机输入。 */
    PIN_MFP_SET(GPIO_1,   SPINOR_MFP_CFG),
    /* SPI0 时钟。 */
    PIN_MFP_SET(GPIO_2,   SPINOR_MFP_CFG),
    /* SPI0 主机输出/从机输入。 */
    PIN_MFP_SET(GPIO_3,   SPINOR_MFP_CFG),
    /* SPI0 数据2。 */
    PIN_MFP_SET(GPIO_6,   SPINOR_MFP_PU_CFG),
    /* SPI0 数据3。 */
    PIN_MFP_SET(GPIO_7,   SPINOR_MFP_PU_CFG),
#endif

#if IS_ENABLED(CONFIG_I2CMT_0)
    /* I2C 时钟。 */
    PIN_MFP_SET(GPIO_18, I2CMT_MFP_CFG(MFP0_I2CMT)),
    /* I2C 数据。 */
    PIN_MFP_SET(GPIO_19, I2CMT_MFP_CFG(MFP0_I2CMT)),
#endif

#if IS_ENABLED(CONFIG_I2CMT_1)
    /* I2C 时钟。 */
    PIN_MFP_SET(GPIO_55, I2CMT_MFP_CFG(MFP1_I2CMT)),
    /* I2C 数据。 */
    PIN_MFP_SET(GPIO_56, I2CMT_MFP_CFG(MFP1_I2CMT)),
#endif

#if IS_ENABLED(CONFIG_I2C_0)
    /* I2C 时钟。 */
    PIN_MFP_SET(GPIO_57, I2C_MFP_CFG(MFP0_I2C)),
    /* I2C 数据。 */
    PIN_MFP_SET(GPIO_58, I2C_MFP_CFG(MFP0_I2C)),
#endif

#if IS_ENABLED(CONFIG_I2C_1)
    /* I2C 时钟。 */
    PIN_MFP_SET(GPIO_51, I2C_MFP_CFG(MFP1_I2C)),
    /* I2C 数据。 */
    PIN_MFP_SET(GPIO_52, I2C_MFP_CFG(MFP1_I2C)),
#endif

#if IS_ENABLED(CONFIG_I2C_2)
    /* I2C 时钟。 */
    PIN_MFP_SET(GPIO_61, I2C_MFP_CFG(MFP2_I2C)),
    /* I2C 数据。 */
    PIN_MFP_SET(GPIO_62, I2C_MFP_CFG(MFP2_I2C)),
#endif

#if IS_ENABLED(CONFIG_I2C_3)
    /* I2C 时钟。 */
    PIN_MFP_SET(GPIO_60, I2C_MFP_CFG(MFP3_I2C)),
    /* I2C 数据。 */
    PIN_MFP_SET(GPIO_59, I2C_MFP_CFG(MFP3_I2C)),
#endif


#if IS_ENABLED(CONFIG_CEC)
    PIN_MFP_SET(GPIO_12,  CEC_MFP_CFG),    /* 启用 CEC 时在 GPIO12 连接控制总线。 */
#endif

#if IS_ENABLED(CONFIG_SPINAND_3)
    /* SPI3 数据2。 */
    PIN_MFP_SET(GPIO_8,   SPINAND_MFP_PU_CFG),
    /* SPI3 数据3。 */
    PIN_MFP_SET(GPIO_9,   SPINAND_MFP_PU_CFG),
    /* SPI3 片选。 */
    PIN_MFP_SET(GPIO_10,  SPINAND_MFP_CFG),
    /* SPI3 时钟。 */
    PIN_MFP_SET(GPIO_11,  SPINAND_MFP_CFG),
    /* SPI3 数据1。 */
    PIN_MFP_SET(GPIO_12,  SPINAND_MFP_CFG),
    /* SPI3 数据0。 */
    PIN_MFP_SET(GPIO_13,  SPINAND_MFP_CFG),
#endif

#if IS_ENABLED(CONFIG_AUDIO_I2SRX_0)
    /* I2SRX0 主时钟。 */
    PIN_MFP_SET(GPIO_53,   I2SRX_MFP_CFG),
    /* I2SRX0 位时钟。 */
    PIN_MFP_SET(GPIO_54,   I2SRX_MFP_CFG),
    /* I2SRX0 左右声道时钟。 */
    PIN_MFP_SET(GPIO_55,   I2SRX_MFP_CFG),
    /* I2SRX0 数据0。 */
    PIN_MFP_SET(GPIO_56,   I2SRX_MFP_CFG),
#endif

#if IS_ENABLED(CONFIG_SPI_1)
    /* SPI 片选。 */
    PIN_MFP_SET(GPIO_24,  SPI_MFP_CFG(MFP_SPI1)),
    /* SPI 时钟。 */
    PIN_MFP_SET(GPIO_25,  SPI_MFP_CFG(MFP_SPI1)),
    /* SPI 从机输出/主机输入。 */
    PIN_MFP_SET(GPIO_26,  SPI_MFP_CFG(MFP_SPI1)),
    /* SPI 主机输出/从机输入。 */
    PIN_MFP_SET(GPIO_27,  SPI_MFP_CFG(MFP_SPI1)),
#endif

#if IS_ENABLED(CONFIG_SPI_2)
    /* SPI 片选（主机侧）。 */
    PIN_MFP_SET(GPIO_57,  SPI_MFP_CFG(MFP_SPI2)),                                                           /* GPIO57 复用为 SPI2 片选，释放自原电机 PWM，避免与振动设备冲突。 */
    /* SPI 时钟（主机输出）。 */
    PIN_MFP_SET(GPIO_58,  SPI_MFP_CFG(MFP_SPI2)),                                                           /* GPIO58 复用为 SPI2 SCLK，由本机主机产生时钟。 */
    /* SPI 主机输入（从机→主机）。 */
    PIN_MFP_SET(GPIO_59,  ((SPI_MFP_CFG(MFP_SPI2) & ~GPIO_CTL_PADDRV_MASK) | GPIO_CTL_PADDRV_LEVEL(3))),    /* GPIO59 复用为 MISO 输入；先清除原驱动强度字段，再设置为等级 3。 */

    /* SPI 主机输出（主机→从机）。 */
    PIN_MFP_SET(GPIO_60,  SPI_MFP_CFG(MFP_SPI2)),    /* GPIO60 复用为 MOSI 输出，向 503 从机发送数据。 */
#endif

#if IS_ENABLED(CONFIG_SPI_3)
    /* SPI 片选。 */
    PIN_MFP_SET(GPIO_20,  SPI_MFP_CFG(MFP_SPI3)),
    /* SPI 时钟。 */
    PIN_MFP_SET(GPIO_21,  SPI_MFP_CFG(MFP_SPI3)),
    /* SPI 从机输出/主机输入。 */
    PIN_MFP_SET(GPIO_22,  SPI_MFP_CFG(MFP_SPI3)),
    /* SPI 主机输出/从机输入。 */
    PIN_MFP_SET(GPIO_23,  SPI_MFP_CFG(MFP_SPI3)),
#endif

#if IS_ENABLED(CONFIG_SPIMT_0)
    /* SPI 片选。 */
    PIN_MFP_SET(GPIO_49,  SPIMT_MFP_CFG(MFP0_SPIMT)),
    /* SPI 时钟。 */
    PIN_MFP_SET(GPIO_50,  SPIMT_MFP_CFG(MFP0_SPIMT)),
    /* SPI 从机输出/主机输入。 */
    PIN_MFP_SET(GPIO_51,  SPIMT_MFP_CFG(MFP0_SPIMT)),
    /* SPI 主机输出/从机输入。 */
    PIN_MFP_SET(GPIO_52,  SPIMT_MFP_CFG(MFP0_SPIMT)),
    /* SPI 第二片选。 */
    PIN_MFP_SET(GPIO_61,  SPIMT_MFP_CFG(MFP0_SPIMT)),
#endif

#if IS_ENABLED(CONFIG_SPIMT_1)
    /* SPI 片选。 */
    PIN_MFP_SET(GPIO_53,  SPIMT_MFP_CFG(MFP1_SPIMT)),
    /* SPI 时钟。 */
    PIN_MFP_SET(GPIO_54,  SPIMT_MFP_CFG(MFP1_SPIMT)),
    /* SPI 从机输出/主机输入。 */
    PIN_MFP_SET(GPIO_55,  SPIMT_MFP_CFG(MFP1_SPIMT)),
    /* SPI 主机输出/从机输入。 */
    PIN_MFP_SET(GPIO_56,  SPIMT_MFP_CFG(MFP1_SPIMT)),
#endif
};

#if IS_ENABLED(CONFIG_MMC_0)
/* MMC0 四线数据总线及命令、时钟引脚。 */
static const struct acts_pin_config board_mmc0_config[] = {
    /* MMC0 命令。 */
    PIN_MFP_SET(GPIO_25,   SDC0_MFP_CFG_VAL),
    /* MMC0 时钟。 */
    PIN_MFP_SET(GPIO_24,  (GPIO_CTL_MFP(SDC0_MFP_SEL)|GPIO_CTL_PADDRV_LEVEL(3))),
    /* MMC0 数据0。 */
    PIN_MFP_SET(GPIO_18,   SDC0_MFP_CFG_VAL),
    /* MMC0 数据1。 */
    PIN_MFP_SET(GPIO_19,	SDC0_MFP_CFG_VAL),
    /* MMC0 数据2。 */
    PIN_MFP_SET(GPIO_20,    SDC0_MFP_CFG_VAL),
    /* MMC0 数据3。 */
    PIN_MFP_SET(GPIO_21,    SDC0_MFP_CFG_VAL),
};
#endif

#if IS_ENABLED(CONFIG_AUDIO_SPDIFTX_0)
/* SPDIF 发射单线输出配置。 */
static const struct acts_pin_config board_spdiftx0_config[] = {
    PIN_MFP_SET(GPIO_9, SPDIFTX_MFP_CFG),    /* GPIO9 复用为 SPDIF 发射输出。 */
};
#endif

#if IS_ENABLED(CONFIG_AUDIO_SPDIFRX_0)
/* SPDIF 接收单线输入配置。 */
static const struct acts_pin_config board_spdifrx0_config[] = {
    PIN_MFP_SET(GPIO_13,  SPDIFRX_MFP_CFG)    /* GPIO13 复用为 SPDIF 接收输入。 */
};
#endif

#if IS_ENABLED(CONFIG_AUDIO_I2STX_0)
/* I2S 发射时钟与数据引脚。 */
static const struct acts_pin_config board_i2stx0_config[] = {
    /* I2STX0 主时钟。 */
    PIN_MFP_SET(GPIO_49, I2STX_MFP_CFG),
    /* I2STX0 位时钟。 */
    PIN_MFP_SET(GPIO_50, I2STX_MFP_CFG),
    /* I2STX0 左右声道时钟。 */
    PIN_MFP_SET(GPIO_51, I2STX_MFP_CFG),
    /* I2STX0 数据0。 */
    PIN_MFP_SET(GPIO_52, I2STX_MFP_CFG),
};
#endif

#if IS_ENABLED(CONFIG_PWM)
/* 可用 PWM 引脚须与 CONFIG_PWM_PIN_CHAN_MAP 通道映射匹配。 */
/* 电机已禁用，PWM 表保持为空，释放 GPIO57 给 SPI2 硬件片选。 */
static const struct pwm_acts_pin_config board_pwm_config[] = {
};
#endif

#if IS_ENABLED(CONFIG_ACTS_BATTERY_NTC)
/* GPIO20 与 GPIO23 共用 LRADC2 通道；每次只接通一端，另一端设为高阻。 */
/* 接通热敏电阻测量端并断开参考端。 */
static const struct acts_pin_config board_ntc_config[] = {
    PIN_MFP_SET(GPIO_20,  BATNTC_MFP_CFG),
    PIN_MFP_SET(GPIO_23,  CONFIG_GPIO_HIGHZ),
};

/* 接通参考测量端并断开热敏电阻端。 */
static const struct acts_pin_config board_ntc_ref_config[] = {
    PIN_MFP_SET(GPIO_20,  CONFIG_GPIO_HIGHZ),
    /* GPIO23 必须未被其他外设占用，此处连接 LRADC2 测量 NTC 参考比例。 */
    PIN_MFP_SET(GPIO_23,  BATNTC_REF_MFP_CFG),
};

/* 测温结束后断开两个模拟输入。 */
static const struct acts_pin_config board_ntc_disable_config[] = {
    PIN_MFP_SET(GPIO_20,  CONFIG_GPIO_HIGHZ),
    PIN_MFP_SET(GPIO_23,  CONFIG_GPIO_HIGHZ),
};
#endif

#if IS_ENABLED(CONFIG_PANEL)
/* LCD 总线及电源引脚，恢复时也使用此表。 */
static const struct acts_pin_config board_lcd_pin_config[] = {
    /* LCD 片选。 */
    PIN_MFP_SET(GPIO_30, (GPIO_CTL_MFP(LCD_MFP_SEL)|GPIO_CTL_PULLUP)),
    /* LCD 串行时钟。 */
    PIN_MFP_SET(GPIO_34, LCD_MFP_CLK_SEL),
    /* LCD_数据0。 */
    PIN_MFP_SET(GPIO_14, LCD_MFP_SEL),
    /* LCD_数据1。 */
    PIN_MFP_SET(GPIO_15, LCD_MFP_SEL),
    /* LCD_数据2。 */
    PIN_MFP_SET(GPIO_16, LCD_MFP_SEL),
    /* LCD_数据3。 */
    PIN_MFP_SET(GPIO_17, LCD_MFP_SEL),
    /* LCD 供电控制: IOVCC2 供电。 */
    PIN_MFP_SET(GPIO_33, 0x1F),
};
#endif

#if IS_ENABLED(CONFIG_ADCKEY)

#define CONFIG_ADCKEY_GPIO    /* 选择 GPIO 方式连接模拟按键；未选择时使用 WIO 备用分支。 */

#ifdef CONFIG_ADCKEY_GPIO
#define CONFIG_ADCKEY_GPIO_NUM (GPIO_21)    /* 模拟按键接 GPIO21。 */
#else
#define CONFIG_ADCKEY_WIO_NUM  (WIO_0)      /* 备用方案使用 WIO0。 */
#define CONFIG_ADCKEY_WIO_MFP  (3)          /* 备用方案的 WIO 复用选择值。 */
#endif

/**
  * @brief  配置模拟按键输入引脚。
  * @param  无。
  * @retval 无。
  * @note   仅 CONFIG_ADCKEY 使能时存在；GPIO 与 WIO 两个方案由编译宏选择。
  */
static void board_adckey_pinmux_init(void)
{
#ifdef CONFIG_ADCKEY_GPIO
    acts_pinmux_set(CONFIG_ADCKEY_GPIO_NUM, ADCKEY_MFP_CFG);                       /* 将 GPIO21 切到模拟按键输入。 */
#else
    sys_write32(CONFIG_ADCKEY_WIO_MFP, WIO0_CTL + (CONFIG_ADCKEY_WIO_NUM * 4));    /* 备用 WIO 方案直接写对应的复用控制寄存器。 */
#endif
}
#endif

/**
  * @brief  在内核设备初始化前设置板级外设引脚。
  * @param  arg：[输入] 初始化框架传入的设备指针，本函数不使用，可为空。
  * @retval 0：完成表项写入；底层无错误返回。
  * @note   PRE_KERNEL_1 优先级 5 执行；SPI2 的 GPIO57～60 在此接入硬件外设。
  */
static int board_early_init(const struct device *arg)
{
    ARG_UNUSED(arg);                                                                   /* 框架参数不参与板级配置，显式消除未使用告警。 */
    acts_pinmux_setup_pins(board_pin_config, ARRAY_SIZE(board_pin_config));            /* 按表顺序写入已使能外设的复用及电气属性。 */

#if IS_ENABLED(CONFIG_MMC_0)
    acts_pinmux_setup_pins(board_mmc0_config, ARRAY_SIZE(board_mmc0_config));          /* 接通 MMC0 命令、时钟与四条数据线。 */
#endif
#if IS_ENABLED(CONFIG_ADCKEY)
    board_adckey_pinmux_init();                                                        /* 选择按键模拟采样所用的 GPIO 或 WIO。 */
#endif
#if IS_ENABLED(CONFIG_PANEL)
    acts_pinmux_setup_pins(board_lcd_pin_config, ARRAY_SIZE(board_lcd_pin_config));    /* 恢复或初始化 LCD 总线及电源引脚。 */
#endif
#ifdef CONFIG_RTT_CONSOLE
	jtag_set();                                                                    /* RTT 调试配置下接通调试接口。 */
#endif

    return 0;                                                                          /* 本路径完成，返回成功状态。 */
}

/**
  * @brief  在内核启动后打印板级初始化位置。
  * @param  arg：[输入] 初始化框架参数，不解引用，可为空。
  * @retval 0：初始化钩子完成。
  * @note   POST_KERNEL 优先级 5 执行。
  */
static int board_later_init(const struct device *arg)
{
    ARG_UNUSED(arg);                            /* 框架参数不参与板级配置，显式消除未使用告警。 */
    printk("%s %d: \n", __func__, __LINE__);    /* 输出晚期初始化位置以便启动排查。 */
    return 0;                                   /* 本路径完成，返回成功状态。 */
}

/* UART0 连续寄存器布局；volatile 保证每次访问实际寄存器，不提供互斥或内存屏障。 */
struct acts_uart_reg {
    volatile uint32_t ctrl;    /* 偏移 0x00，控制寄存器。 */

    volatile uint32_t rxdat;    /* 偏移 0x04，接收数据。 */

    volatile uint32_t txdat;    /* 偏移 0x08，发送数据。 */

    volatile uint32_t stat;    /* 偏移 0x0C，状态寄存器。 */

    volatile uint32_t br;    /* 偏移 0x10，波特率配置。 */
} ;

/**
  * @brief  通过 UART0 寄存器轮询发送一个字符。
  * @param  c：[输入] 要写入发送寄存器的字符值，调用者提供有效字符。
  * @retval 无。
  * @note   UART0 时钟及波特率须已可用；无超时、无互斥，不等待整帧离开引脚。
  */
void uart_poll_out_ch(int c)
{
    struct acts_uart_reg *uart = (struct acts_uart_reg*)UART0_REG_BASE;    /* 使用 UART0 内存映射寄存器，无需设备对象。 */
    /* 等待状态位 6 清零后才写入发送数据；此忙等没有超时。 */

    /* 状态位保持置位时持续查询硬件；不调度线程，也未设置异常退出。 */
    while (uart->stat &  BIT(6));
    /* 写入发送寄存器，由 UART 硬件串行输出字符。 */
    uart->txdat = (uint32_t)c;

}
/* 内核早期打印钩子：不依赖设备绑定即可输出。 */

/**
  * @brief  为早期 printk 输出字符并将换行展开为 CR/LF。
  * @param  c：[输入] 字符值，换行字符前额外发送回车。
  * @retval 固定为 0；不提供硬件失败状态。
  * @note   同步忙等 UART0；多个打印调用者没有在此加锁。
  */
int arch_printk_char_out(int c)
{
    if ('\n' == c)                 /* 只对换行补充回车以兼容串口终端。 */
        uart_poll_out_ch('\r');    /* 先发送回车，将终端光标移到行首。 */
    uart_poll_out_ch(c);           /* 再输出原字符。 */
    return 0;                      /* 本路径完成，返回成功状态。 */
}

#if 1   // TODO: 此分支固定启用，保留原音频通道映射。
/* 每项依次指定音频设备及四路 ADC 输入；DISABLE 表示该通道不用。 */
static const audio_input_map_t board_audio_input_map[] =  {
    {AUDIO_LINE_IN0, ADC_CH_INPUT0P, ADC_CH_DISABLE, ADC_CH_INPUT0N, ADC_CH_DISABLE},
    {AUDIO_LINE_IN1, ADC_CH_INPUT0NP_DIFF, ADC_CH_INPUT1NP_DIFF, ADC_CH_DISABLE, ADC_CH_DISABLE},
    {AUDIO_LINE_IN2, ADC_CH_DISABLE, ADC_CH_INPUT1P, ADC_CH_DISABLE, ADC_CH_INPUT1N},
    {AUDIO_ANALOG_MIC0, ADC_CH_INPUT0NP_DIFF, ADC_CH_DISABLE, ADC_CH_DISABLE, ADC_CH_DISABLE},
    {AUDIO_ANALOG_MIC1, ADC_CH_INPUT0NP_DIFF, ADC_CH_DISABLE, ADC_CH_DISABLE, ADC_CH_DISABLE},
    {AUDIO_ANALOG_MIC2, ADC_CH_INPUT0NP_DIFF, ADC_CH_INPUT1NP_DIFF, ADC_CH_DISABLE, ADC_CH_DISABLE},
    {AUDIO_ANALOG_FM0, ADC_CH_INPUT0P, ADC_CH_DISABLE, ADC_CH_INPUT0N, ADC_CH_DISABLE},
    {AUDIO_DIGITAL_MIC0, ADC_CH_DMIC, ADC_CH_DMIC, ADC_CH_DISABLE, ADC_CH_DISABLE},
};

/**
  * @brief  按设备类型填充四路音频输入映射。
  * @param  input_map：[输入输出] 非空可写对象；输入 audio_dev，输出 ch0_input～ch3_input。
  * @retval 0：找到映射；-EINVAL：指针为空；-ENOENT：设备不在表中。
  */
int board_audio_device_mapping(audio_input_map_t *input_map)
{
    int i;                                                                    /* 保存映射表索引，循环结束后也用于判断是否命中。 */

    if (!input_map)                                                           /* 拒绝空对象，避免写入无效地址。 */
        return -EINVAL;                                                       /* 必需对象或 GPIO 设备不可用，终止当前流程。 */

    /* 匹配表项后逐通道复制输入选择，避免应用层了解板级接线。 */
    for (i = 0; i < ARRAY_SIZE(board_audio_input_map); i++) {                 /* 顺序查找设备类型，遍历到表尾即未命中。 */
        if (input_map->audio_dev == board_audio_input_map[i].audio_dev) {     /* 只复制与请求设备匹配的四路通道。 */
            input_map->ch0_input = board_audio_input_map[i].ch0_input;        /* 填充 ADC 通道0输入选择。 */
            input_map->ch1_input = board_audio_input_map[i].ch1_input;        /* 填充 ADC 通道1输入选择。 */
            input_map->ch2_input = board_audio_input_map[i].ch2_input;        /* 填充 ADC 通道2输入选择。 */
            input_map->ch3_input = board_audio_input_map[i].ch3_input;        /* 填充 ADC 通道3输入选择。 */
            break;                                                            /* 找到后退出查找，保留命中索引。 */
        }
    }

    /* 查找结束后区分有效映射与不支持的设备。 */
    if (i == ARRAY_SIZE(board_audio_input_map)) {                             /* 索引到达表尾表示没有对应设备。 */
        printk("can not find out audio dev 0x%x\n", input_map->audio_dev);    /* 报告未配置的音频设备编号。 */
        return -ENOENT;                                                       /* 表中没有对应设备，不伪造通道映射。 */
    }

    return 0;                                                                 /* 本路径完成，返回成功状态。 */
}
#endif

#ifdef CONFIG_BOARD_EXTERNAL_PA_ENABLE

#define EXTERN_PA_CTL1_PIN  53                 /* 功放使能控制脚，可能与 UART2 复用冲突，取决于配置。 */
#define EXTERN_PA_CTL_SLEEP_OFF_TIME_MS 160    /* 功放关断稳定时间，单位 ms。 */
#define EXTERN_PA_CTL_SLEEP_ON_TIME_MS 20      /* 功放开启稳定时间，单位 ms。 */
//#define EXTERN_PA_EIO_CTRL_PIN (0)
//#define EXTERN_PA_EIO_CTRL_PIN_NAME CONFIG_EXTEND_GPIO_NAME

/**
  * @brief  通过 GPIO 控制外部功放并等待上电或关断时序。
  * @param  pa_class：[输入] 功放类别，当前实现未使用。
  * @param  is_on：[输入] true 开启，false 关闭。
  * @retval 0：控制流程完成；-EINVAL：配置的 GPIO 设备绑定失败。
  * @note   仅功放配置使能时存在；使用 k_sleep，须在线程上下文调用，GPIO 操作结果未检查。
  */
int board_extern_pa_ctl(u8_t pa_class, bool is_on)
{
#ifdef EXTERN_PA_CTL1_PIN
    const struct device *pa_gpio_dev;                                               /* 保存 SoC GPIO 控制器设备对象。 */
    pa_gpio_dev = device_get_binding(CONFIG_GPIO_PIN2NAME(EXTERN_PA_CTL1_PIN));     /* 按引脚所在 GPIO 组取得驱动对象。 */
    if (!pa_gpio_dev)                                                               /* GPIO 设备不存在时不能控制功放。 */
        return -EINVAL;                                                             /* 必需对象或 GPIO 设备不可用，终止当前流程。 */
#endif

#ifdef EXTERN_PA_EIO_CTRL_PIN
    const struct device *egpio_dev;                                                 /* 可选扩展 GPIO 控制器对象。 */
    egpio_dev = device_get_binding(EXTERN_PA_EIO_CTRL_PIN_NAME);                    /* 仅启用扩展控制脚时绑定其控制器。 */
    if (!egpio_dev)                                                                 /* 扩展控制器不存在则终止。 */
        return -EINVAL;                                                             /* 必需对象或 GPIO 设备不可用，终止当前流程。 */
#endif

    if (!is_on) {                                                                   /* 进入关断流程并满足模式切换等待。 */
        /* 关闭外部功放，输出低电平并满足关断时序。 */
        printk("audio external PA power off\n");                                    /* 记录外部功放关闭。 */

#ifdef EXTERN_PA_CTL1_PIN
        gpio_pin_configure(pa_gpio_dev, EXTERN_PA_CTL1_PIN % 32, GPIO_OUTPUT);      /* 将组内引脚设为输出，组号由设备绑定确定。 */
        gpio_pin_set(pa_gpio_dev, EXTERN_PA_CTL1_PIN % 32, 0);                      /* 拉低使能信号关闭功放。 */
        /* 原板级时序要求 XA7191D 切换模式前关断超过 150 ms，此处等待 160 ms。 */
        k_sleep(K_MSEC(EXTERN_PA_CTL_SLEEP_OFF_TIME_MS));                           /* 让出 CPU 等待 160 ms 关断时序。 */
#endif

#ifdef EXTERN_PA_EIO_CTRL_PIN
        gpio_pin_configure(egpio_dev, EXTERN_PA_EIO_CTRL_PIN % 32, GPIO_OUTPUT);    /* 可选扩展 GPIO 方案先切为输出。 */
        gpio_pin_set(egpio_dev, EXTERN_PA_EIO_CTRL_PIN % 32, 0);                    /* 拉低扩展控制脚。 */
#endif
    } else {
        /* 开启外部功放，拉高控制脚后等待输出稳定。 */
        printk("audio external PA power on\n");                                     /* 记录外部功放开启。 */

#ifdef EXTERN_PA_CTL1_PIN
        gpio_pin_configure(pa_gpio_dev, EXTERN_PA_CTL1_PIN % 32, GPIO_OUTPUT);      /* 将组内引脚设为输出，组号由设备绑定确定。 */
        gpio_pin_set(pa_gpio_dev, EXTERN_PA_CTL1_PIN % 32, 1);                      /* 拉高使能信号开启功放。 */
        k_sleep(K_MSEC(EXTERN_PA_CTL_SLEEP_ON_TIME_MS));                            /* 让出 CPU 等待 20 ms 开启稳定。 */
#endif

#ifdef EXTERN_PA_EIO_CTRL_PIN
        gpio_pin_configure(egpio_dev, EXTERN_PA_EIO_CTRL_PIN % 32, GPIO_OUTPUT);    /* 可选扩展 GPIO 方案先切为输出。 */
        gpio_pin_set(egpio_dev, EXTERN_PA_EIO_CTRL_PIN % 32, 1);                    /* 拉高扩展控制脚。 */
#endif
    }

    return 0;                                                                       /* 本路径完成，返回成功状态。 */
}
#endif

/**
  * @brief  取得 mmc0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_MMC_0 关闭时不写输出对象；调用者应先初始化。
  */
void board_get_mmc0_pinmux_info(struct board_pinmux_info *pinmux_info)
{
#if IS_ENABLED(CONFIG_MMC_0)
    pinmux_info->pins_config = board_mmc0_config;             /* 返回静态引脚配置表，不分配内存。 */
    pinmux_info->pins_num = ARRAY_SIZE(board_mmc0_config);    /* 写入表项数量，单位为引脚。 */
#endif
}


/**
  * @brief  取得 spdiftx0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_AUDIO_SPDIFTX_0 关闭时不写输出对象；调用者应先初始化。
  */
void board_get_spdiftx0_pinmux_info(struct board_pinmux_info *pinmux_info)
{
#if IS_ENABLED(CONFIG_AUDIO_SPDIFTX_0)
    pinmux_info->pins_config = board_spdiftx0_config;             /* 返回静态引脚配置表，不分配内存。 */
    pinmux_info->pins_num = ARRAY_SIZE(board_spdiftx0_config);    /* 写入表项数量，单位为引脚。 */
#endif
}

/**
  * @brief  取得 spdifrx0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_AUDIO_SPDIFRX_0 关闭时不写输出对象；调用者应先初始化。
  */
void board_get_spdifrx0_pinmux_info(struct board_pinmux_info *pinmux_info)
{
#if IS_ENABLED(CONFIG_AUDIO_SPDIFRX_0)
    pinmux_info->pins_config = board_spdifrx0_config;             /* 返回静态引脚配置表，不分配内存。 */
    pinmux_info->pins_num = ARRAY_SIZE(board_spdifrx0_config);    /* 写入表项数量，单位为引脚。 */
#endif
}

/**
  * @brief  取得 i2stx0 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_AUDIO_I2STX_0 关闭时不写输出对象；调用者应先初始化。
  */
void board_get_i2stx0_pinmux_info(struct board_pinmux_info *pinmux_info)
{
#if IS_ENABLED(CONFIG_AUDIO_I2STX_0)
    pinmux_info->pins_config = board_i2stx0_config;             /* 返回静态引脚配置表，不分配内存。 */
    pinmux_info->pins_num = ARRAY_SIZE(board_i2stx0_config);    /* 写入表项数量，单位为引脚。 */
#endif
}

/**
  * @brief  取得 pwm 的板级引脚表。
  * @param  pinmux_info：[输出] 非空可写结构；接收静态表指针和元素数，不转移所有权。
  * @retval 无。
  * @note   CONFIG_PWM 关闭时不写输出对象；调用者应先初始化。
  */
void board_get_pwm_pinmux_info(struct board_pwm_pinmux_info *pinmux_info)
{
#if IS_ENABLED(CONFIG_PWM)
    pinmux_info->pins_config = board_pwm_config;             /* 返回静态引脚配置表，不分配内存。 */
    pinmux_info->pins_num = ARRAY_SIZE(board_pwm_config);    /* 写入表项数量，单位为引脚。 */
#endif
}

/**
  * @brief  在 NTC 测量、参考测量及关闭状态之间切换引脚。
  * @param  enable：[输入] true 接通测量，false 将两端置高阻。
  * @param  use_as_ntc：[输入] enable 为 true 时选择 NTC 端或参考端。
  * @retval 无。
  * @note   仅 CONFIG_ACTS_BATTERY_NTC 使能时写寄存器；GPIO20/23 不可同时接入 LRADC2。
  */
void board_set_ntc_pinmux_info(bool enable, bool use_as_ntc)
{
#if IS_ENABLED(CONFIG_ACTS_BATTERY_NTC)
    if (enable) {                                                                                  /* 使能时在测温端与参考端之间选择。 */
        if (use_as_ntc)                                                                            /* true 选择热敏电阻端，false 选择参考端。 */
            acts_pinmux_setup_pins(board_ntc_config, ARRAY_SIZE(board_ntc_config));                /* 仅接通 GPIO20 的测温通路。 */
        else
            acts_pinmux_setup_pins(board_ntc_ref_config, ARRAY_SIZE(board_ntc_ref_config));        /* 仅接通 GPIO23 的参考通路。 */
    } else {
        acts_pinmux_setup_pins(board_ntc_disable_config, ARRAY_SIZE(board_ntc_disable_config));    /* 测量关闭时两个引脚均高阻。 */
    }
#endif
}

#if IS_ENABLED(CONFIG_PANEL)
/* 早期挂起保存 TE、供电控制、复位三个引脚配置，供晚期恢复使用。 */
static uint32_t lcd_pin_backup[3];

/**
  * @brief  分阶段把 LCD 信号设为高阻并备份需恢复的引脚。
  * @param  aod_en：[输入] true 表示保留常亮显示所需供电和复位。
  * @param  early_suspend：[输入] true 时先备份 TE 及非 AOD 供电、复位引脚。
  * @retval 无。
  * @note   先早期挂起再执行后续阶段；数据与时钟每次都置高阻。备份只有一份，不支持嵌套挂起。
  */
void board_lcd_suspend(bool aod_en, bool early_suspend)
{

    /* 早期阶段先保存即将覆盖的引脚，AOD 模式保留显示维持所需信号。 */
    if (early_suspend) {                                      /* 仅早期阶段备份原配置，避免被高阻值覆盖。 */
        lcd_pin_backup[0] = sys_read32(GPION_CTL(35));        /* LCD 撕裂同步。 */
        sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(35));        /* 断开 TE 输入的引脚连接。 */

        if (aod_en == false) {                                /* 非 AOD 模式才断开或恢复供电控制与复位。 */
            lcd_pin_backup[1] = sys_read32(GPION_CTL(21));    /* LCD 供电控制。 */
            sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(21));    /* 断开此处使用的 LCD 供电控制引脚。 */

            lcd_pin_backup[2] = sys_read32(GPION_CTL(32));    /* LCD 复位。 */
            sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(32));    /* 断开 LCD 复位控制引脚。 */

            sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(30));    /* LCD 片选。 */
            sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(33));    /* LCD 供电控制 IOVCC2 供电。 */
        }
    }

    /* 所有挂起阶段都断开 LCD 时钟及四条数据线。 */
    sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(34));            /* LCD 串行时钟。 */
    sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(14));            /* LCD 数据0。 */
    sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(15));            /* LCD 数据1。 */
    sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(16));            /* LCD 数据2。 */
    sys_write32(CONFIG_GPIO_HIGHZ, GPION_CTL(17));            /* LCD 数据3。 */
}

/**
  * @brief  恢复 LCD 总线配置并在晚期阶段恢复备份引脚。
  * @param  aod_en：[输入] 与挂起阶段一致的 AOD 状态。
  * @param  late_resume：[输入] true 时恢复有效备份中的 TE 和非 AOD 引脚。
  * @retval 无。
  * @note   须先经过对应挂起以建立备份；总线配置每次由静态表重建。
  */
void board_lcd_resume(bool aod_en, bool late_resume)
{
    acts_pinmux_setup_pins(board_lcd_pin_config, ARRAY_SIZE(board_lcd_pin_config));    /* 恢复或初始化 LCD 总线及电源引脚。 */

    /* 晚期阶段补回不在常规总线表中的引脚状态。 */
    if (late_resume) {                                                                 /* 仅晚期恢复阶段使用早期挂起保存的寄存器值。 */
        sys_write32(lcd_pin_backup[0], GPION_CTL(35));                                 /* LCD 撕裂同步。 */

        if (aod_en == false) {                                                         /* 非 AOD 模式才断开或恢复供电控制与复位。 */
            sys_write32(lcd_pin_backup[1], GPION_CTL(21));                             /* LCD 背光地控制。 */
            sys_write32(lcd_pin_backup[2], GPION_CTL(32));                             /* LCD 复位。 */
        }
    }
}
#endif    /* CONFIG_PANEL */

SYS_INIT(board_early_init, PRE_KERNEL_1, 5);    /* 注册内核前优先级5的板级引脚初始化入口。 */

SYS_INIT(board_later_init, POST_KERNEL, 5);    /* 注册内核后优先级5的板级检查入口。 */
