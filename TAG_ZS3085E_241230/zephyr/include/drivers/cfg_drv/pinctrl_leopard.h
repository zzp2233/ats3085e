/* Leopard引脚复用配置模板。PIN_MFP_SET只生成acts_pin_config表项，board.c选择具体接线，acts_pinmux_setup_pins才写硬件；本文件列出的候选引脚不会自动全部启用。GPIO控制字段与soc_gpio.h的同名定义应保持一致。 */
/*
 * Copyright (c) 2020 Linaro Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PINCTRL_LEOPARD_H_
#define PINCTRL_LEOPARD_H_

/* pin为普通GPIO绝对编号，val为复用及电气字段；展开为含pin_num和mode的结构初始化器，不访问寄存器。 */
#define PIN_MFP_SET(pin, val)				\
	{.pin_num = pin,		\
	 .mode = val}


#define GPIO_CTL_MFP_SHIFT				(0)                                    /* 引脚功能选择字段从位0开始。 */
#define GPIO_CTL_MFP_MASK				(0x1f << GPIO_CTL_MFP_SHIFT)           /* 功能选择位4:0掩码；数值需查复用表。 */
#define GPIO_CTL_MFP_GPIO				(0x0 << GPIO_CTL_MFP_SHIFT)            /* 功能编码0选择普通GPIO。 */
#define GPIO_CTL_MFP(x)					(x << GPIO_CTL_MFP_SHIFT)              /* 把复用功能编码移入MFP字段，不自动限制输入范围。 */
#define GPIO_CTL_SMIT					(0x1 << 5)                             /* 输入施密特触发控制位5。 */
#define GPIO_CTL_GPIO_OUTEN				(0x1 << 6)                             /* 普通GPIO输出使能位6。 */
#define GPIO_CTL_GPIO_INEN				(0x1 << 7)                             /* 普通GPIO输入使能位7。 */
#define GPIO_CTL_PULL_MASK				(0xf << 8)                             /* 上下拉配置位11:8的组合掩码。 */
#define GPIO_CTL_PULLUP_STRONG			(0x1 << 8)                                     /* 强上拉配置位8。 */
#define GPIO_CTL_PULLDOWN				(0x1 << 9)                             /* 下拉配置位9。 */
#define GPIO_CTL_PULLUP					(0x1 << 11)                            /* 上拉配置位11。 */
#define GPIO_CTL_PULLUP_4K7					(0x1 << 10)                    /* 引脚约4.7 kΩ上拉选择位。 */
#define GPIO_CTL_PADDRV_SHIFT			(12)                                           /* 驱动强度字段从位12开始。 */
#define GPIO_CTL_PADDRV_LEVEL(x)		((x) << GPIO_CTL_PADDRV_SHIFT)                 /* 把等级x移入字段；宏本身不限制x，调用者须保证不覆盖其他位。 */
#define GPIO_CTL_PADDRV_MASK			GPIO_CTL_PADDRV_LEVEL(0x7)                     /* 代码定义的驱动强度掩码为位14:12，编码范围0～7。 */
#define GPIO_CTL_SR         			(0x1 << 15)                                    /* 引脚转换速率SR控制位15；具体电气效果以芯片手册为准。 */
#define GPIO_CTL_INTC_EN				(0x1 << 20)                            /* 引脚中断控制使能位20。 */
#define GPIO_CTL_INC_TRIGGER_SHIFT			(21)                                   /* 中断触发方式字段从位21开始。 */
#define GPIO_CTL_INC_TRIGGER(x)				((x) << GPIO_CTL_INC_TRIGGER_SHIFT)    /* 将触发方式编码移入字段，不自动截断。 */
#define GPIO_CTL_INC_TRIGGER_MASK			GPIO_CTL_INC_TRIGGER(0x7)              /* 中断触发方式三位字段掩码。 */
#define GPIO_CTL_INC_TRIGGER_RISING_EDGE	GPIO_CTL_INC_TRIGGER(0x0)                      /* 触发编码0：上升沿。 */
#define GPIO_CTL_INC_TRIGGER_FALLING_EDGE	GPIO_CTL_INC_TRIGGER(0x1)                      /* 触发编码1：下降沿。 */
#define GPIO_CTL_INC_TRIGGER_DUAL_EDGE		GPIO_CTL_INC_TRIGGER(0x2)                      /* 触发编码2：双边沿。 */
#define GPIO_CTL_INC_TRIGGER_HIGH_LEVEL		GPIO_CTL_INC_TRIGGER(0x3)                      /* 触发编码3：高电平。 */
#define GPIO_CTL_INC_TRIGGER_LOW_LEVEL		GPIO_CTL_INC_TRIGGER(0x4)                      /* 触发编码4：低电平。 */
#define GPIO_CTL_INTC_MASK					(0x1 << 25)                    /* 引脚中断屏蔽控制位25。 */


/*********I2C复用配置*****************/
#define MFP0_I2C	8                                                                 /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP1_I2C	9                                                                 /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP2_I2C	14                                                                /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP3_I2C	15                                                                /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define I2C_MFP_CFG(x)	(GPIO_CTL_MFP(x)|GPIO_CTL_PULLUP_4K7|GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含4.7 kΩ上拉、驱动等级3；不代表候选引脚已经接通。 */


#define gpio22_i2c0_clk_node	PIN_MFP_SET(22, I2C_MFP_CFG(MFP0_I2C))    /* GPIO22的i2c0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio23_i2c0_data_node	PIN_MFP_SET(23, I2C_MFP_CFG(MFP0_I2C))    /* GPIO23的i2c0 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio24_i2c0_clk_node	PIN_MFP_SET(24, I2C_MFP_CFG(MFP0_I2C))    /* GPIO24的i2c0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio25_i2c0_data_node	PIN_MFP_SET(25, I2C_MFP_CFG(MFP0_I2C))    /* GPIO25的i2c0 数据候选表项；实际模式以右侧配置值为准。 */


#define gpio28_i2c0_clk_node	PIN_MFP_SET(28, I2C_MFP_CFG(MFP0_I2C))    /* GPIO28的i2c0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio29_i2c0_data_node	PIN_MFP_SET(29, I2C_MFP_CFG(MFP0_I2C))    /* GPIO29的i2c0 数据候选表项；实际模式以右侧配置值为准。 */


#define gpio20_i2c1_clk_node	PIN_MFP_SET(20, I2C_MFP_CFG(MFP1_I2C))    /* GPIO20的i2c1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio21_i2c1_data_node	PIN_MFP_SET(21, I2C_MFP_CFG(MFP1_I2C))    /* GPIO21的i2c1 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio18_i2c1_clk_node	PIN_MFP_SET(18, I2C_MFP_CFG(MFP1_I2C))    /* GPIO18的i2c1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio19_i2c1_data_node	PIN_MFP_SET(19, I2C_MFP_CFG(MFP1_I2C))    /* GPIO19的i2c1 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio28_i2c1_clk_node	PIN_MFP_SET(28, I2C_MFP_CFG(MFP1_I2C))    /* GPIO28的i2c1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio29_i2c1_data_node	PIN_MFP_SET(29, I2C_MFP_CFG(MFP1_I2C))    /* GPIO29的i2c1 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio57_i2c0_clk_node	PIN_MFP_SET(57, I2C_MFP_CFG(MFP0_I2C))    /* GPIO57的i2c0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio58_i2c0_data_node	PIN_MFP_SET(58, I2C_MFP_CFG(MFP0_I2C))    /* GPIO58的i2c0 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio59_i2c1_clk_node	PIN_MFP_SET(59, I2C_MFP_CFG(MFP1_I2C))    /* GPIO59的i2c1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio60_i2c1_data_node	PIN_MFP_SET(60, I2C_MFP_CFG(MFP1_I2C))    /* GPIO60的i2c1 数据候选表项；实际模式以右侧配置值为准。 */

/*********I2CMT任务控制器复用配置*****************/
#define MFP0_I2CMT	26                                                                        /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP1_I2CMT	27                                                                        /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define I2CMT_MFP_CFG(x)	(GPIO_CTL_MFP(x)|GPIO_CTL_PULLUP_4K7|GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含4.7 kΩ上拉、驱动等级3；不代表候选引脚已经接通。 */

#define gpio22_i2cmt0_clk_node	PIN_MFP_SET(22, I2CMT_MFP_CFG(MFP0_I2CMT))    /* GPIO22的i2cmt0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio23_i2cmt0_data_node	PIN_MFP_SET(23, I2CMT_MFP_CFG(MFP0_I2CMT))    /* GPIO23的i2cmt0 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio24_i2cmt0_clk_node	PIN_MFP_SET(24, I2CMT_MFP_CFG(MFP0_I2CMT))    /* GPIO24的i2cmt0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio25_i2cmt0_data_node	PIN_MFP_SET(25, I2CMT_MFP_CFG(MFP0_I2CMT))    /* GPIO25的i2cmt0 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio49_i2cmt0_clk_node	PIN_MFP_SET(49, I2CMT_MFP_CFG(MFP0_I2CMT))    /* GPIO49的i2cmt0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio50_i2cmt0_data_node	PIN_MFP_SET(50, I2CMT_MFP_CFG(MFP0_I2CMT))    /* GPIO50的i2cmt0 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio51_i2cmt1_clk_node	PIN_MFP_SET(51,  I2CMT_MFP_CFG(MFP1_I2CMT))    /* GPIO51的i2cmt1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio52_i2cmt1_data_node	PIN_MFP_SET(52,  I2CMT_MFP_CFG(MFP1_I2CMT))    /* GPIO52的i2cmt1 数据候选表项；实际模式以右侧配置值为准。 */

#define gpio59_i2cmt1_clk_node	PIN_MFP_SET(59,  I2CMT_MFP_CFG(MFP1_I2CMT))    /* GPIO59的i2cmt1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio60_i2cmt1_data_node	PIN_MFP_SET(60,  I2CMT_MFP_CFG(MFP1_I2CMT))    /* GPIO60的i2cmt1 数据候选表项；实际模式以右侧配置值为准。 */

/*********普通SPI复用配置；本次SPI2接线由board.c选择GPIO57～60*****************/
#define MFP_SPI0	6    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP_SPI0_1	7    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP_SPI1	2    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP_SPI2	3    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP_SPI3	4    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */

#define SPI_MFP_CFG(x)	(GPIO_CTL_MFP(x)|GPIO_CTL_PULLUP_STRONG|GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含强上拉、驱动等级3；不代表候选引脚已经接通。 */
#define SPI2_MFP_CFG(x)	(GPIO_CTL_MFP(x)|GPIO_CTL_PULLUP_STRONG|GPIO_CTL_PADDRV_LEVEL(5))    /* 复用/电气字段组合，包含强上拉、驱动等级5；不代表候选引脚已经接通。 */

#define gpio24_spi1_ss_node		PIN_MFP_SET(24,  SPI_MFP_CFG(MFP_SPI1))    /* GPIO24的spi1 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio25_spi1_clk_node	PIN_MFP_SET(25,  SPI_MFP_CFG(MFP_SPI1))            /* GPIO25的spi1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio26_spi1_miso_node	PIN_MFP_SET(26,  SPI_MFP_CFG(MFP_SPI1))            /* GPIO26的spi1 从机输出/主机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio27_spi1_mosi_node	PIN_MFP_SET(27,  SPI_MFP_CFG(MFP_SPI1))            /* GPIO27的spi1 主机输出/从机输入候选表项；实际模式以右侧配置值为准。 */


#define gpio30_spi2_ss_node		PIN_MFP_SET(30,  SPI2_MFP_CFG(MFP_SPI2))    /* GPIO30的spi2 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio31_spi2_clk_node	PIN_MFP_SET(31,  SPI2_MFP_CFG(MFP_SPI2))            /* GPIO31的spi2 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio32_spi2_miso_node	PIN_MFP_SET(32,  SPI2_MFP_CFG(MFP_SPI2))            /* GPIO32的spi2 从机输出/主机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio33_spi2_mosi_node	PIN_MFP_SET(33,  SPI2_MFP_CFG(MFP_SPI2))            /* GPIO33的spi2 主机输出/从机输入候选表项；实际模式以右侧配置值为准。 */

#define gpio20_spi3_ss_node		PIN_MFP_SET(20,  SPI2_MFP_CFG(MFP_SPI3))    /* GPIO20的spi3 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio21_spi3_clk_node	PIN_MFP_SET(21,  SPI2_MFP_CFG(MFP_SPI3))            /* GPIO21的spi3 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio22_spi3_miso_node	PIN_MFP_SET(22,  SPI2_MFP_CFG(MFP_SPI3))            /* GPIO22的spi3 从机输出/主机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio23_spi3_mosi_node	PIN_MFP_SET(23,  SPI2_MFP_CFG(MFP_SPI3))            /* GPIO23的spi3 主机输出/从机输入候选表项；实际模式以右侧配置值为准。 */

/*********独立SPIMT控制器复用配置*****************/
#define MFP0_SPIMT	24    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define MFP1_SPIMT	25    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */

#define SPIMT_MFP_CFG(x)	(GPIO_CTL_MFP(x)|GPIO_CTL_PULLUP_STRONG|GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含强上拉、驱动等级3；不代表候选引脚已经接通。 */

#define gpio49_spimt0_ss_node	PIN_MFP_SET(49,  SPIMT_MFP_CFG(MFP0_SPIMT))    /* GPIO49的spimt0 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio50_spimt0_clk_node	PIN_MFP_SET(50,  SPIMT_MFP_CFG(MFP0_SPIMT))    /* GPIO50的spimt0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio51_spimt0_miso_node	PIN_MFP_SET(51,  SPIMT_MFP_CFG(MFP0_SPIMT))    /* GPIO51的spimt0 从机输出/主机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio52_spimt0_mosi_node	PIN_MFP_SET(52,  SPIMT_MFP_CFG(MFP0_SPIMT))    /* GPIO52的spimt0 主机输出/从机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio61_spimt0_ss1_node	PIN_MFP_SET(61,  SPIMT_MFP_CFG(MFP0_SPIMT))    /* GPIO61的spimt0 片选1候选表项；实际模式以右侧配置值为准。 */

#define gpio53_spimt1_ss_node	PIN_MFP_SET(53,  SPIMT_MFP_CFG(MFP1_SPIMT))    /* GPIO53的spimt1 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio54_spimt1_clk_node	PIN_MFP_SET(54,  SPIMT_MFP_CFG(MFP1_SPIMT))    /* GPIO54的spimt1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio55_spimt1_miso_node	PIN_MFP_SET(55,  SPIMT_MFP_CFG(MFP1_SPIMT))    /* GPIO55的spimt1 从机输出/主机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio56_spimt1_mosi_node	PIN_MFP_SET(56,  SPIMT_MFP_CFG(MFP1_SPIMT))    /* GPIO56的spimt1 主机输出/从机输入候选表项；实际模式以右侧配置值为准。 */

/* LCD控制器复用 */
#ifndef LCD_PADDRV_LEVEL
#  define LCD_PADDRV_LEVEL (1)
#endif
#ifndef LCD_CLK_PADDRV_LEVEL
#  define LCD_CLK_PADDRV_LEVEL (1)
#endif

#define LCD_MFP_SEL                  (19 | GPIO_CTL_PADDRV_LEVEL(LCD_PADDRV_LEVEL))                  /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define LCD_MFP_CLK_SEL              (19 | GPIO_CTL_PADDRV_LEVEL(LCD_CLK_PADDRV_LEVEL))              /* LCD接口复用选择值，供相应总线信号表项使用。 */
#define gpio30_lcd_ce0_hsync_node    PIN_MFP_SET(30, (GPIO_CTL_MFP(LCD_MFP_SEL)|GPIO_CTL_PULLUP))    /* GPIO30的lcd ce0 hsync候选表项；实际模式以右侧配置值为准。 */
#define gpio31_lcd_ce1_node          PIN_MFP_SET(31, (GPIO_CTL_MFP(LCD_MFP_SEL)|GPIO_CTL_PULLUP))    /* GPIO31的lcd ce1候选表项；实际模式以右侧配置值为准。 */
#define gpio32_lcd_rs_vsync_sda_node PIN_MFP_SET(32, LCD_MFP_SEL)                                    /* GPIO32的lcd rs vsync sda候选表项；实际模式以右侧配置值为准。 */
#define gpio33_lcd_rde_lde_cdx_node  PIN_MFP_SET(33, LCD_MFP_SEL)                                    /* GPIO33的lcd rde lde cdx候选表项；实际模式以右侧配置值为准。 */
#define gpio34_lcd_wr_dclk_scl_node  PIN_MFP_SET(34, LCD_MFP_SEL)                                    /* GPIO34的lcd wr d时钟 scl候选表项；实际模式以右侧配置值为准。 */
#define gpio35_lcd_te_sdo_node       PIN_MFP_SET(35, LCD_MFP_SEL)                                    /* GPIO35的lcd te sdo候选表项；实际模式以右侧配置值为准。 */

#define gpio14_lcd_d0_node           PIN_MFP_SET(14, LCD_MFP_SEL)    /* GPIO14的lcd d0候选表项；实际模式以右侧配置值为准。 */
#define gpio15_lcd_d1_node           PIN_MFP_SET(15, LCD_MFP_SEL)    /* GPIO15的lcd d1候选表项；实际模式以右侧配置值为准。 */
#define gpio16_lcd_d2_node           PIN_MFP_SET(16, LCD_MFP_SEL)    /* GPIO16的lcd d2候选表项；实际模式以右侧配置值为准。 */
#define gpio17_lcd_d3_node           PIN_MFP_SET(17, LCD_MFP_SEL)    /* GPIO17的lcd d3候选表项；实际模式以右侧配置值为准。 */
#define gpio18_lcd_d4_node           PIN_MFP_SET(18, LCD_MFP_SEL)    /* GPIO18的lcd d4候选表项；实际模式以右侧配置值为准。 */
#define gpio19_lcd_d5_node           PIN_MFP_SET(19, LCD_MFP_SEL)    /* GPIO19的lcd d5候选表项；实际模式以右侧配置值为准。 */
#define gpio20_lcd_d6_node           PIN_MFP_SET(20, LCD_MFP_SEL)    /* GPIO20的lcd d6候选表项；实际模式以右侧配置值为准。 */
#define gpio21_lcd_d7_node           PIN_MFP_SET(21, LCD_MFP_SEL)    /* GPIO21的lcd d7候选表项；实际模式以右侧配置值为准。 */

#define gpio14_lcd_ce0_hsync_node    PIN_MFP_SET(14,   (GPIO_CTL_MFP(LCD_MFP_SEL)|GPIO_CTL_PULLUP))    /* GPIO14的lcd ce0 hsync候选表项；实际模式以右侧配置值为准。 */
#define gpio15_lcd_ce1_node          PIN_MFP_SET(15,   LCD_MFP_SEL)                                    /* GPIO15的lcd ce1候选表项；实际模式以右侧配置值为准。 */
#define gpio16_lcd_d8_node           PIN_MFP_SET(16,   LCD_MFP_SEL)                                    /* GPIO16的lcd d8候选表项；实际模式以右侧配置值为准。 */
#define gpio17_lcd_d9_node           PIN_MFP_SET(17,   LCD_MFP_SEL)                                    /* GPIO17的lcd d9候选表项；实际模式以右侧配置值为准。 */
#define gpio18_lcd_d10_node          PIN_MFP_SET(18,   LCD_MFP_SEL)                                    /* GPIO18的lcd d10候选表项；实际模式以右侧配置值为准。 */
#define gpio19_lcd_d11_node          PIN_MFP_SET(19,   LCD_MFP_SEL)                                    /* GPIO19的lcd d11候选表项；实际模式以右侧配置值为准。 */
#define gpio20_lcd_d12_node          PIN_MFP_SET(20,   LCD_MFP_SEL)                                    /* GPIO20的lcd d12候选表项；实际模式以右侧配置值为准。 */
#define gpio21_lcd_d13_node          PIN_MFP_SET(21,   LCD_MFP_SEL)                                    /* GPIO21的lcd d13候选表项；实际模式以右侧配置值为准。 */
#define gpio22_lcd_d14_node          PIN_MFP_SET(22,   LCD_MFP_SEL)                                    /* GPIO22的lcd d14候选表项；实际模式以右侧配置值为准。 */
#define gpio23_lcd_d15_node          PIN_MFP_SET(23,   LCD_MFP_SEL)                                    /* GPIO23的lcd d15候选表项；实际模式以右侧配置值为准。 */
#define gpio24_lcd_d0_node           PIN_MFP_SET(24,   LCD_MFP_SEL)                                    /* GPIO24的lcd d0候选表项；实际模式以右侧配置值为准。 */
#define gpio25_lcd_d1_node           PIN_MFP_SET(25,   LCD_MFP_SEL)                                    /* GPIO25的lcd d1候选表项；实际模式以右侧配置值为准。 */
#define gpio26_lcd_d2_node           PIN_MFP_SET(26,   LCD_MFP_SEL)                                    /* GPIO26的lcd d2候选表项；实际模式以右侧配置值为准。 */
#define gpio27_lcd_d3_node           PIN_MFP_SET(27,   LCD_MFP_SEL)                                    /* GPIO27的lcd d3候选表项；实际模式以右侧配置值为准。 */
#define gpio28_lcd_d4_node           PIN_MFP_SET(28,   LCD_MFP_SEL)                                    /* GPIO28的lcd d4候选表项；实际模式以右侧配置值为准。 */
#define gpio29_lcd_d5_node           PIN_MFP_SET(29,   LCD_MFP_SEL)                                    /* GPIO29的lcd d5候选表项；实际模式以右侧配置值为准。 */
#define gpio30_lcd_d6_node           PIN_MFP_SET(30,   LCD_MFP_SEL)                                    /* GPIO30的lcd d6候选表项；实际模式以右侧配置值为准。 */
#define gpio31_lcd_d7_node           PIN_MFP_SET(31,   LCD_MFP_SEL)                                    /* GPIO31的lcd d7候选表项；实际模式以右侧配置值为准。 */
#define gpio32_lcd_rs_vsync_sda_node PIN_MFP_SET(32,   LCD_MFP_SEL)                                    /* GPIO32的lcd rs vsync sda候选表项；实际模式以右侧配置值为准。 */
#define gpio33_lcd_rde_lde_cdx_node  PIN_MFP_SET(33,   LCD_MFP_SEL)                                    /* GPIO33的lcd rde lde cdx候选表项；实际模式以右侧配置值为准。 */
#define gpio34_lcd_wr_dclk_scl_node  PIN_MFP_SET(34,   LCD_MFP_SEL)                                    /* GPIO34的lcd wr d时钟 scl候选表项；实际模式以右侧配置值为准。 */
#define gpio35_lcd_te_sdo_node       PIN_MFP_SET(35,   LCD_MFP_SEL)                                    /* GPIO35的lcd te sdo候选表项；实际模式以右侧配置值为准。 */
/* LCD控制器FPGA 1.8 V方案*/
#define gpio30_lcd_ce0_hsync_node    PIN_MFP_SET(30,  (GPIO_CTL_MFP(LCD_MFP_SEL)|GPIO_CTL_PULLUP))     /* GPIO30的lcd ce0 hsync候选表项；实际模式以右侧配置值为准。 */
#define gpio34_lcd_wr_dclk_scl_node  PIN_MFP_SET(34,  LCD_MFP_SEL)                                     /* GPIO34的lcd wr d时钟 scl候选表项；实际模式以右侧配置值为准。 */
#define gpio14_lcd_d0_node           PIN_MFP_SET(14,  LCD_MFP_SEL)                                     /* GPIO14的lcd d0候选表项；实际模式以右侧配置值为准。 */
#define gpio15_lcd_d1_node           PIN_MFP_SET(15,  LCD_MFP_SEL)                                     /* GPIO15的lcd d1候选表项；实际模式以右侧配置值为准。 */
#define gpio16_lcd_d2_node           PIN_MFP_SET(16,  LCD_MFP_SEL)                                     /* GPIO16的lcd d2候选表项；实际模式以右侧配置值为准。 */
#define gpio17_lcd_d3_node           PIN_MFP_SET(17,  LCD_MFP_SEL)                                     /* GPIO17的lcd d3候选表项；实际模式以右侧配置值为准。 */
#define gpio18_lcd_d4_node           PIN_MFP_SET(18,  LCD_MFP_SEL)                                     /* GPIO18的lcd d4候选表项；实际模式以右侧配置值为准。 */
#define gpio19_lcd_d5_node           PIN_MFP_SET(19,  LCD_MFP_SEL)                                     /* GPIO19的lcd d5候选表项；实际模式以右侧配置值为准。 */
#define gpio20_lcd_d6_node           PIN_MFP_SET(20,  LCD_MFP_SEL)                                     /* GPIO20的lcd d6候选表项；实际模式以右侧配置值为准。 */
#define gpio21_lcd_d7_node           PIN_MFP_SET(21,  LCD_MFP_SEL)                                     /* GPIO21的lcd d7候选表项；实际模式以右侧配置值为准。 */

//#define gpio24_lcd_d10_node          PIN_MFP_SET(24, LCD_MFP_SEL)
//#define gpio25_lcd_d11_node          PIN_MFP_SET(25, LCD_MFP_SEL)
//#define gpio26_lcd_d12_node          PIN_MFP_SET(26, LCD_MFP_SEL)
//#define gpio27_lcd_d13_node          PIN_MFP_SET(27, LCD_MFP_SEL)
//#define gpio28_lcd_d14_node          PIN_MFP_SET(28, LCD_MFP_SEL)
//#define gpio29_lcd_d15_node          PIN_MFP_SET(29, LCD_MFP_SEL)

/* SD0复用 */
#define SDC0_MFP_SEL             10                                                                      /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SDC0_MFP_CFG_VAL	(GPIO_CTL_MFP(SDC0_MFP_SEL)|GPIO_CTL_PULLUP|GPIO_CTL_PADDRV_LEVEL(5))    /* 复用/电气字段组合，包含上拉、驱动等级5；不代表候选引脚已经接通。 */

//#define gpio0_sdc0_cmd_node  PIN_MFP_SET(0, sdc0, cmd,  SDC0_MFP_CFG_VAL)
//#define gpio1_sdc0_clk_node  PIN_MFP_SET(1, sdc0, clk,  (GPIO_CTL_MFP(SDC0_MFP_SEL)|GPIO_CTL_PADDRV_LEVEL(3)))
//#define gpio2_sdc0_d0_node   PIN_MFP_SET(2, sdc0, d0,   SDC0_MFP_CFG_VAL)
//#define gpio3_sdc0_d1_node   PIN_MFP_SET(3, sdc0, d1,   SDC0_MFP_CFG_VAL)
//#define gpio4_sdc0_d2_node   PIN_MFP_SET(4, sdc0, d2,   SDC0_MFP_CFG_VAL)
//#define gpio5_sdc0_d3_node   PIN_MFP_SET(5, sdc0, d3,   SDC0_MFP_CFG_VAL)
//#define gpio6_sdc0_d4_node   PIN_MFP_SET(6, sdc0, d4,   SDC0_MFP_CFG_VAL)
//#define gpio7_sdc0_d5_node   PIN_MFP_SET(7, sdc0, d5,   SDC0_MFP_CFG_VAL)
//#define gpio8_sdc0_d6_node   PIN_MFP_SET(8, sdc0, d6,   SDC0_MFP_CFG_VAL)
//#define gpio9_sdc0_d7_node   PIN_MFP_SET(9, sdc0, d7,   SDC0_MFP_CFG_VAL)

#define gpio11_sdc0_cmd_node  PIN_MFP_SET(11,   SDC0_MFP_CFG_VAL)                                        /* GPIO11的sdc0 命令候选表项；实际模式以右侧配置值为准。 */
#define gpio10_sdc0_clk_node  PIN_MFP_SET(10,  (GPIO_CTL_MFP(SDC0_MFP_SEL)|GPIO_CTL_PADDRV_LEVEL(3)))    /* GPIO10的sdc0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio12_sdc0_d0_node   PIN_MFP_SET(12,   SDC0_MFP_CFG_VAL)                                        /* GPIO12的sdc0 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio13_sdc0_d1_node   PIN_MFP_SET(13,    SDC0_MFP_CFG_VAL)                                       /* GPIO13的sdc0 d1候选表项；实际模式以右侧配置值为准。 */
#define gpio8_sdc0_d2_node   PIN_MFP_SET(8,    SDC0_MFP_CFG_VAL)                                         /* GPIO8的sdc0 d2候选表项；实际模式以右侧配置值为准。 */
#define gpio9_sdc0_d3_node   PIN_MFP_SET(9,    SDC0_MFP_CFG_VAL)                                         /* GPIO9的sdc0 d3候选表项；实际模式以右侧配置值为准。 */

/* SD1复用 */
#define SDC1_MFP_SEL             20                                                                      /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SDC1_MFP_CFG_VAL	(GPIO_CTL_MFP(SDC1_MFP_SEL)|GPIO_CTL_PULLUP|GPIO_CTL_PADDRV_LEVEL(5))    /* 复用/电气字段组合，包含上拉、驱动等级5；不代表候选引脚已经接通。 */

#define gpio44_sdc1_cmd_node  PIN_MFP_SET(44,   SDC1_MFP_SEL)                                             /* GPIO44的sdc1 命令候选表项；实际模式以右侧配置值为准。 */
#define gpio43_sdc1_clk_node  PIN_MFP_SET(43,   (GPIO_CTL_MFP(SDC1_MFP_SEL)|GPIO_CTL_PADDRV_LEVEL(3)))    /* GPIO43的sdc1 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio45_sdc1_d0_node   PIN_MFP_SET(45,   SDC1_MFP_CFG_VAL)                                         /* GPIO45的sdc1 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio46_sdc1_d1_node   PIN_MFP_SET(46,   SDC1_MFP_CFG_VAL)                                         /* GPIO46的sdc1 d1候选表项；实际模式以右侧配置值为准。 */
#define gpio41_sdc1_d2_node   PIN_MFP_SET(41,   SDC1_MFP_CFG_VAL)                                         /* GPIO41的sdc1 d2候选表项；实际模式以右侧配置值为准。 */
#define gpio42_sdc1_d3_node   PIN_MFP_SET(42,   SDC1_MFP_CFG_VAL)                                         /* GPIO42的sdc1 d3候选表项；实际模式以右侧配置值为准。 */


/* UART0复用  */
#define UART0_MFP_SEL           5                                                                                    /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define UART0_MFP_CFG (GPIO_CTL_MFP(UART0_MFP_SEL)|GPIO_CTL_SMIT|GPIO_CTL_PULLUP_STRONG|GPIO_CTL_PADDRV_LEVEL(4))    /* 复用/电气字段组合，包含施密特输入、强上拉、驱动等级4；不代表候选引脚已经接通。 */
#define gpio10_uart0_tx_node    PIN_MFP_SET(10,  UART0_MFP_CFG)                                                      /* GPIO10的uart0 发送候选表项；实际模式以右侧配置值为准。 */
#define gpio11_uart0_rx_node    PIN_MFP_SET(11,  UART0_MFP_CFG)                                                      /* GPIO11的uart0 接收候选表项；实际模式以右侧配置值为准。 */
#define gpio28_uart0_tx_node    PIN_MFP_SET(28,  UART0_MFP_CFG)                                                      /* GPIO28的uart0 发送候选表项；实际模式以右侧配置值为准。 */
#define gpio29_uart0_rx_node    PIN_MFP_SET(29,  UART0_MFP_CFG)                                                      /* GPIO29的uart0 接收候选表项；实际模式以右侧配置值为准。 */
#define gpio37_uart0_tx_node    PIN_MFP_SET(37,  UART0_MFP_CFG)                                                      /* GPIO37的uart0 发送候选表项；实际模式以右侧配置值为准。 */
#define gpio38_uart0_rx_node    PIN_MFP_SET(38,  UART0_MFP_CFG)                                                      /* GPIO38的uart0 接收候选表项；实际模式以右侧配置值为准。 */
#define gpio63_uart0_tx_node    PIN_MFP_SET(63,  UART0_MFP_CFG)                                                      /* GPIO63的uart0 发送候选表项；实际模式以右侧配置值为准。 */
#define gpio62_uart0_rx_node    PIN_MFP_SET(62,  UART0_MFP_CFG)                                                      /* GPIO62的uart0 接收候选表项；实际模式以右侧配置值为准。 */

/* UART1复用 */
#define UART1_MFP_SEL			6                                                                                  /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define UART1_MFP_CFG (GPIO_CTL_MFP(UART1_MFP_SEL) | GPIO_CTL_SMIT | GPIO_CTL_PULLUP_STRONG | GPIO_CTL_PADDRV_LEVEL(4))    /* 复用/电气字段组合，包含施密特输入、强上拉、驱动等级4；不代表候选引脚已经接通。 */
#define gpio16_uart1_tx_node		PIN_MFP_SET(16, UART1_MFP_CFG)                                                     /* GPIO16的uart1 发送候选表项；实际模式以右侧配置值为准。 */
#define gpio17_uart1_rx_node		PIN_MFP_SET(17, UART1_MFP_CFG)                                                     /* GPIO17的uart1 接收候选表项；实际模式以右侧配置值为准。 */

/* UART2复用 */
#define UART2_MFP_SEL			7                                                                                  /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define UART2_MFP_CFG (GPIO_CTL_MFP(UART2_MFP_SEL) | GPIO_CTL_SMIT | GPIO_CTL_PULLUP_STRONG | GPIO_CTL_PADDRV_LEVEL(4))    /* 复用/电气字段组合，包含施密特输入、强上拉、驱动等级4；不代表候选引脚已经接通。 */
#define gpio53_uart2_tx_node		PIN_MFP_SET(53, UART2_MFP_CFG)                                                     /* GPIO53的uart2 发送候选表项；实际模式以右侧配置值为准。 */
#define gpio54_uart2_rx_node		PIN_MFP_SET(54, UART2_MFP_CFG)                                                     /* GPIO54的uart2 接收候选表项；实际模式以右侧配置值为准。 */


/* SPDIFTX */
#define SPDIFTX_MFP_SEL           14                                                                 /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SPDIFTX_MFP_CFG	(GPIO_CTL_MFP(SPDIFTX_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define gpio9_spdiftx0_d0_node   PIN_MFP_SET(9, SPDIFTX_MFP_CFG)                                     /* GPIO9的spdif发送0 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio62_spdiftx0_d0_node   PIN_MFP_SET(62, SPDIFTX_MFP_CFG)                                   /* GPIO62的spdif发送0 d0候选表项；实际模式以右侧配置值为准。 */

/* SPDIFRX */
#define SPDIFRX_MFP_SEL           15                                                                 /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SPDIFRX_MFP_CFG	(GPIO_CTL_MFP(SPDIFRX_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define gpio13_spdifrx0_d0_node   PIN_MFP_SET(13,  SPDIFRX_MFP_CFG)                                  /* GPIO13的spdif接收0 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio63_spdifrx0_d0_node   PIN_MFP_SET(63,  SPDIFRX_MFP_CFG)                                  /* GPIO63的spdif接收0 d0候选表项；实际模式以右侧配置值为准。 */

/* I2STX */
#define I2STX_MFP_SEL             12                                                             /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define I2STX_MFP_CFG (GPIO_CTL_MFP(I2STX_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define gpio39_i2stx0_d0_node     PIN_MFP_SET(39,   I2STX_MFP_CFG)                               /* GPIO39的i2s发送0 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio16_i2stx0_mclk_node   PIN_MFP_SET(16,   I2STX_MFP_CFG)                               /* GPIO16的i2s发送0 主时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio17_i2stx0_bclk_node   PIN_MFP_SET(17,   I2STX_MFP_CFG)                               /* GPIO17的i2s发送0 位时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio18_i2stx0_lrclk_node  PIN_MFP_SET(18,   I2STX_MFP_CFG)                               /* GPIO18的i2s发送0 声道时钟候选表项；实际模式以右侧配置值为准。 */

#define gpio9_i2stx0_d0_node     PIN_MFP_SET(9,  I2STX_MFP_CFG)    /* GPIO9的i2s发送0 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio6_i2stx0_mclk_node   PIN_MFP_SET(6,  I2STX_MFP_CFG)    /* GPIO6的i2s发送0 主时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio7_i2stx0_bclk_node   PIN_MFP_SET(7,  I2STX_MFP_CFG)    /* GPIO7的i2s发送0 位时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio8_i2stx0_lrclk_node  PIN_MFP_SET(8,  I2STX_MFP_CFG)    /* GPIO8的i2s发送0 声道时钟候选表项；实际模式以右侧配置值为准。 */

#define gpio49_i2stx0_mclk_node  PIN_MFP_SET(49, I2STX_MFP_CFG)    /* GPIO49的i2s发送0 主时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio50_i2stx0_bclk_node  PIN_MFP_SET(50, I2STX_MFP_CFG)    /* GPIO50的i2s发送0 位时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio51_i2stx0_lrclk_node PIN_MFP_SET(51, I2STX_MFP_CFG)    /* GPIO51的i2s发送0 声道时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio52_i2stx0_d0_node    PIN_MFP_SET(52, I2STX_MFP_CFG)    /* GPIO52的i2s发送0 d0候选表项；实际模式以右侧配置值为准。 */

/* I2SRX */
#define I2SRX_MFP_SEL             13                                                             /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define I2SRX_MFP_CFG (GPIO_CTL_MFP(I2SRX_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define gpio43_i2srx0_d0_node     PIN_MFP_SET(43,  I2SRX_MFP_CFG)                                /* GPIO43的i2s接收0 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio19_i2srx0_mclk_node   PIN_MFP_SET(19,  I2SRX_MFP_CFG)                                /* GPIO19的i2s接收0 主时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio20_i2srx0_bclk_node   PIN_MFP_SET(20,  I2SRX_MFP_CFG)                                /* GPIO20的i2s接收0 位时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio21_i2srx0_lrclk_node  PIN_MFP_SET(21,  I2SRX_MFP_CFG)                                /* GPIO21的i2s接收0 声道时钟候选表项；实际模式以右侧配置值为准。 */

#define gpio9_i2srx0_d0_node      PIN_MFP_SET(9,   I2SRX_MFP_CFG)    /* GPIO9的i2s接收0 d0候选表项；实际模式以右侧配置值为准。 */
#define gpio40_i2srx0_mclk_node   PIN_MFP_SET(40,  I2SRX_MFP_CFG)    /* GPIO40的i2s接收0 主时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio6_i2srx0_bclk_node    PIN_MFP_SET(6,   I2SRX_MFP_CFG)    /* GPIO6的i2s接收0 位时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio7_i2srx0_lrclk_node   PIN_MFP_SET(7,   I2SRX_MFP_CFG)    /* GPIO7的i2s接收0 声道时钟候选表项；实际模式以右侧配置值为准。 */

#define gpio53_i2srx0_mclk_node   PIN_MFP_SET(53,   I2SRX_MFP_CFG)    /* GPIO53的i2s接收0 主时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio54_i2srx0_bclk_node   PIN_MFP_SET(54,   I2SRX_MFP_CFG)    /* GPIO54的i2s接收0 位时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio55_i2srx0_lrclk_node  PIN_MFP_SET(55,   I2SRX_MFP_CFG)    /* GPIO55的i2s接收0 声道时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio56_i2srx0_d0_node     PIN_MFP_SET(56,   I2SRX_MFP_CFG)    /* GPIO56的i2s接收0 d0候选表项；实际模式以右侧配置值为准。 */

/* 触摸复位与中断候选引脚 */

#define gpio12_tp_rst_node	PIN_MFP_SET(12,  GPIO_CTL_GPIO_OUTEN)    /* GPIO12的tp 复位候选表项；实际模式以右侧配置值为准。 */
#define gpio30_tp_rst_node	PIN_MFP_SET(30,  GPIO_CTL_GPIO_OUTEN)    /* GPIO30的tp 复位候选表项；实际模式以右侧配置值为准。 */

#define gpio13_tp_isr_node	PIN_MFP_SET(13,  GPIO_CTL_GPIO_OUTEN)    /* GPIO13的tp 中断候选表项；实际模式以右侧配置值为准。 */
#define gpio31_tp_isr_node	PIN_MFP_SET(31,  GPIO_CTL_GPIO_OUTEN)    /* GPIO31的tp 中断候选表项；实际模式以右侧配置值为准。 */



/* GPIO按键输入及中断配置 */

#define GPIOKEY_MFP_PU_CFG (GPIO_CTL_SMIT | GPIO_CTL_PULLUP | GPIO_CTL_GPIO_INEN | GPIO_CTL_INTC_EN | GPIO_CTL_INC_TRIGGER_DUAL_EDGE)    /* 复用/电气字段组合，包含施密特输入、上拉、GPIO输入、引脚中断、双边沿触发；不代表候选引脚已经接通。 */
#define GPIOKEY_MFP_CFG (GPIO_CTL_SMIT | GPIO_CTL_GPIO_INEN | GPIO_CTL_INTC_EN | GPIO_CTL_INC_TRIGGER_DUAL_EDGE)                         /* 复用/电气字段组合，包含施密特输入、GPIO输入、引脚中断、双边沿触发；不代表候选引脚已经接通。 */

#define gpio17_keygpio_key0_node   PIN_MFP_SET(17,   GPIOKEY_MFP_CFG)    /* GPIO17的keygpio key0候选表项；实际模式以右侧配置值为准。 */
#define gpio18_keygpio_key1_node   PIN_MFP_SET(18,   GPIOKEY_MFP_CFG)    /* GPIO18的keygpio key1候选表项；实际模式以右侧配置值为准。 */
#define gpio19_keygpio_key2_node   PIN_MFP_SET(19,   GPIOKEY_MFP_CFG)    /* GPIO19的keygpio key2候选表项；实际模式以右侧配置值为准。 */
#define gpio20_keygpio_key3_node   PIN_MFP_SET(20,   GPIOKEY_MFP_CFG)    /* GPIO20的keygpio key3候选表项；实际模式以右侧配置值为准。 */
#define gpio21_keygpio_key4_node   PIN_MFP_SET(21,   GPIOKEY_MFP_CFG)    /* GPIO21的keygpio key4候选表项；实际模式以右侧配置值为准。 */
#define gpio22_keygpio_key5_node   PIN_MFP_SET(22,   GPIOKEY_MFP_CFG)    /* GPIO22的keygpio key5候选表项；实际模式以右侧配置值为准。 */
#define gpio23_keygpio_key6_node   PIN_MFP_SET(23,   GPIOKEY_MFP_CFG)    /* GPIO23的keygpio key6候选表项；实际模式以右侧配置值为准。 */
#define gpio24_keygpio_key7_node   PIN_MFP_SET(24,   GPIOKEY_MFP_CFG)    /* GPIO24的keygpio key7候选表项；实际模式以右侧配置值为准。 */
#define gpio25_keygpio_key8_node   PIN_MFP_SET(25,   GPIOKEY_MFP_CFG)    /* GPIO25的keygpio key8候选表项；实际模式以右侧配置值为准。 */
#define gpio26_keygpio_key9_node   PIN_MFP_SET(26,   GPIOKEY_MFP_CFG)    /* GPIO26的keygpio key9候选表项；实际模式以右侧配置值为准。 */

/* 旋钮GPIO配置 */
#define GPIOKNOB_MFP_INT_CFG (GPIO_CTL_SMIT | GPIO_CTL_PULLUP | GPIO_CTL_GPIO_INEN | GPIO_CTL_INTC_EN | GPIO_CTL_INC_TRIGGER_DUAL_EDGE)    /* 复用/电气字段组合，包含施密特输入、上拉、GPIO输入、引脚中断、双边沿触发；不代表候选引脚已经接通。 */
#define GPIOKNOB_MFP_CFG (GPIO_CTL_SMIT | GPIO_CTL_PULLUP | GPIO_CTL_GPIO_INEN )                                                           /* 复用/电气字段组合，包含施密特输入、上拉、GPIO输入；不代表候选引脚已经接通。 */

/* PWM */
#define PWM_MFP_CFG (0x12 | GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
/* pin为GPIO绝对编号，chan为PWM通道，val为电气配置；仅构造PWM板级表项，命名中的chan号不替代实参。 */
#define PWM_PIN_MFP_SET(pin, chan, val)				\
	{.pin_num = pin,		\
	 .pin_chan = chan,		\
	 .mode = val}

#define gpio03_pwm_chan0_node   PWM_PIN_MFP_SET(03, 0, PWM_MFP_CFG)    /* GPIO03的pwm 通道0候选表项；实际模式以右侧配置值为准。 */
#define gpio04_pwm_chan0_node   PWM_PIN_MFP_SET(04, 0, PWM_MFP_CFG)    /* GPIO04的pwm 通道0候选表项；实际模式以右侧配置值为准。 */
#define gpio14_pwm_chan0_node   PWM_PIN_MFP_SET(14, 0, PWM_MFP_CFG)    /* GPIO14的pwm 通道0候选表项；实际模式以右侧配置值为准。 */
#define gpio36_pwm_chan0_node   PWM_PIN_MFP_SET(36, 0, PWM_MFP_CFG)    /* GPIO36的pwm 通道0候选表项；实际模式以右侧配置值为准。 */
#define gpio49_pwm_chan0_node   PWM_PIN_MFP_SET(49, 0, PWM_MFP_CFG)    /* GPIO49的pwm 通道0候选表项；实际模式以右侧配置值为准。 */

#define gpio05_pwm_chan1_node   PWM_PIN_MFP_SET(05, 1, PWM_MFP_CFG)    /* GPIO05的pwm 通道1候选表项；实际模式以右侧配置值为准。 */
#define gpio15_pwm_chan1_node   PWM_PIN_MFP_SET(15, 1, PWM_MFP_CFG)    /* GPIO15的pwm 通道1候选表项；实际模式以右侧配置值为准。 */
#define gpio37_pwm_chan1_node   PWM_PIN_MFP_SET(37, 1, PWM_MFP_CFG)    /* GPIO37的pwm 通道1候选表项；实际模式以右侧配置值为准。 */
#define gpio50_pwm_chan1_node   PWM_PIN_MFP_SET(50, 1, PWM_MFP_CFG)    /* GPIO50的pwm 通道1候选表项；实际模式以右侧配置值为准。 */

#define gpio06_pwm_chan2_node   PWM_PIN_MFP_SET(06, 2, PWM_MFP_CFG)    /* GPIO06的pwm 通道2候选表项；实际模式以右侧配置值为准。 */
#define gpio21_pwm_chan2_node   PWM_PIN_MFP_SET(21, 2, PWM_MFP_CFG)    /* GPIO21的pwm 通道2候选表项；实际模式以右侧配置值为准。 */
#define gpio38_pwm_chan2_node   PWM_PIN_MFP_SET(38, 2, PWM_MFP_CFG)    /* GPIO38的pwm 通道2候选表项；实际模式以右侧配置值为准。 */
#define gpio51_pwm_chan2_node   PWM_PIN_MFP_SET(51, 2, PWM_MFP_CFG)    /* GPIO51的pwm 通道2候选表项；实际模式以右侧配置值为准。 */

#define gpio07_pwm_chan3_node   PWM_PIN_MFP_SET(07, 3, PWM_MFP_CFG)    /* GPIO07的pwm 通道3候选表项；实际模式以右侧配置值为准。 */
#define gpio17_pwm_chan3_node   PWM_PIN_MFP_SET(17, 3, PWM_MFP_CFG)    /* GPIO17的pwm 通道3候选表项；实际模式以右侧配置值为准。 */
#define gpio39_pwm_chan3_node   PWM_PIN_MFP_SET(39, 3, PWM_MFP_CFG)    /* GPIO39的pwm 通道3候选表项；实际模式以右侧配置值为准。 */
#define gpio52_pwm_chan3_node   PWM_PIN_MFP_SET(52, 3, PWM_MFP_CFG)    /* GPIO52的pwm 通道3候选表项；实际模式以右侧配置值为准。 */

#define gpio08_pwm_chan4_node   PWM_PIN_MFP_SET(08, 4, PWM_MFP_CFG)    /* GPIO08的pwm 通道4候选表项；实际模式以右侧配置值为准。 */
#define gpio18_pwm_chan4_node   PWM_PIN_MFP_SET(18, 4, PWM_MFP_CFG)    /* GPIO18的pwm 通道4候选表项；实际模式以右侧配置值为准。 */
#define gpio40_pwm_chan4_node   PWM_PIN_MFP_SET(40, 4, PWM_MFP_CFG)    /* GPIO40的pwm 通道4候选表项；实际模式以右侧配置值为准。 */
#define gpio53_pwm_chan4_node   PWM_PIN_MFP_SET(53, 4, PWM_MFP_CFG)    /* GPIO53的pwm 通道4候选表项；实际模式以右侧配置值为准。 */

#define gpio09_pwm_chan5_node   PWM_PIN_MFP_SET(09, 5, PWM_MFP_CFG)    /* GPIO09的pwm 通道5候选表项；实际模式以右侧配置值为准。 */
#define gpio19_pwm_chan5_node   PWM_PIN_MFP_SET(19, 5, PWM_MFP_CFG)    /* GPIO19的pwm 通道5候选表项；实际模式以右侧配置值为准。 */
#define gpio41_pwm_chan5_node   PWM_PIN_MFP_SET(41, 5, PWM_MFP_CFG)    /* GPIO41的pwm 通道5候选表项；实际模式以右侧配置值为准。 */
#define gpio54_pwm_chan5_node   PWM_PIN_MFP_SET(54, 5, PWM_MFP_CFG)    /* GPIO54的pwm 通道5候选表项；实际模式以右侧配置值为准。 */

#define gpio10_pwm_chan6_node   PWM_PIN_MFP_SET(10, 6, PWM_MFP_CFG)    /* GPIO10的pwm 通道6候选表项；实际模式以右侧配置值为准。 */
#define gpio20_pwm_chan6_node   PWM_PIN_MFP_SET(20, 6, PWM_MFP_CFG)    /* GPIO20的pwm 通道6候选表项；实际模式以右侧配置值为准。 */
#define gpio42_pwm_chan6_node   PWM_PIN_MFP_SET(42, 6, PWM_MFP_CFG)    /* GPIO42的pwm 通道6候选表项；实际模式以右侧配置值为准。 */
#define gpio55_pwm_chan6_node   PWM_PIN_MFP_SET(55, 6, PWM_MFP_CFG)    /* GPIO55的pwm 通道6候选表项；实际模式以右侧配置值为准。 */

#define gpio11_pwm_chan7_node   PWM_PIN_MFP_SET(11, 7, PWM_MFP_CFG)    /* GPIO11的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio21_pwm_chan7_node   PWM_PIN_MFP_SET(21, 7, PWM_MFP_CFG)    /* GPIO21的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio43_pwm_chan7_node   PWM_PIN_MFP_SET(43, 7, PWM_MFP_CFG)    /* GPIO43的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio45_pwm_chan7_node   PWM_PIN_MFP_SET(45, 7, PWM_MFP_CFG)    /* GPIO45的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio56_pwm_chan7_node   PWM_PIN_MFP_SET(56, 7, PWM_MFP_CFG)    /* GPIO56的pwm 通道7候选表项；实际模式以右侧配置值为准。 */

#define gpio12_pwm_chan7_node   PWM_PIN_MFP_SET(12, 8, PWM_MFP_CFG)    /* GPIO12的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio22_pwm_chan7_node   PWM_PIN_MFP_SET(22, 8, PWM_MFP_CFG)    /* GPIO22的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio44_pwm_chan7_node   PWM_PIN_MFP_SET(44, 8, PWM_MFP_CFG)    /* GPIO44的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio46_pwm_chan7_node   PWM_PIN_MFP_SET(46, 8, PWM_MFP_CFG)    /* GPIO46的pwm 通道7候选表项；实际模式以右侧配置值为准。 */
#define gpio57_pwm_chan7_node   PWM_PIN_MFP_SET(57, 8, PWM_MFP_CFG)    /* GPIO57的pwm 通道7候选表项；实际模式以右侧配置值为准。 */


/* SPI NOR */
#define SPINOR_MFP_SEL             1                                                                                                  /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SPINOR_MFP_CFG (GPIO_CTL_MFP(SPINOR_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3) | GPIO_CTL_SR)                         /* 复用/电气字段组合，包含施密特输入、SR控制、驱动等级3；不代表候选引脚已经接通。 */
#define SPINOR_MFP_PU_CFG (GPIO_CTL_MFP(SPINOR_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3) | GPIO_CTL_PULLUP | GPIO_CTL_SR)    /* 复用/电气字段组合，包含施密特输入、上拉、SR控制、驱动等级3；不代表候选引脚已经接通。 */

#define SPINOR2_MFP_SEL             3                                                                /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SPINOR2_MFP_CFG (GPIO_CTL_MFP(SPINOR2_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */

#define SPI3_MFP_SEL             4                                                             /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SPI3_MFP_CFG (GPIO_CTL_MFP(SPI3_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */


#define SPINOR_22_MFP_SEL            22                                                                                       /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SPINOR_22_MFP_CFG (GPIO_CTL_MFP(SPINOR_22_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))                         /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define SPINOR_22_MFP_PU_CFG (GPIO_CTL_MFP(SPINOR_22_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3) | GPIO_CTL_PULLUP)    /* 复用/电气字段组合，包含施密特输入、上拉、驱动等级3；不代表候选引脚已经接通。 */

#define gpio0_spinor0_cs_node	PIN_MFP_SET(0,   SPINOR_MFP_CFG)       /* GPIO0的spinor0 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio1_spinor0_miso_node	PIN_MFP_SET(1,   SPINOR_MFP_CFG)       /* GPIO1的spinor0 从机输出/主机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio2_spinor0_clk_node	PIN_MFP_SET(2,   SPINOR_MFP_CFG)       /* GPIO2的spinor0 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio3_spinor0_mosi_node	PIN_MFP_SET(3,   SPINOR_MFP_CFG)       /* GPIO3的spinor0 主机输出/从机输入候选表项；实际模式以右侧配置值为准。 */
#define gpio6_spinor0_io2_node	PIN_MFP_SET(6,   SPINOR_MFP_PU_CFG)    /* GPIO6的spinor0 io2候选表项；实际模式以右侧配置值为准。 */
#define gpio7_spinor0_io3_node	PIN_MFP_SET(7,   SPINOR_MFP_PU_CFG)    /* GPIO7的spinor0 io3候选表项；实际模式以右侧配置值为准。 */

/* HDMI CEC */
#define CEC_MFP_SEL             21                                                                             /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define CEC_MFP_CFG (GPIO_CTL_MFP(CEC_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3) | GPIO_CTL_PULLUP)    /* 复用/电气字段组合，包含施密特输入、上拉、驱动等级3；不代表候选引脚已经接通。 */
#define gpio12_cec0_d0_node	PIN_MFP_SET(12,  CEC_MFP_CFG)                                                  /* GPIO12的cec0 d0候选表项；实际模式以右侧配置值为准。 */

/* 模拟ADC按键复用 */
#define ADCKEY_MFP_SEL          28                                                                  /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define ADCKEY_MFP_CFG (GPIO_CTL_MFP(ADCKEY_MFP_SEL) | GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))    /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define gpio21_adckey_lradc3_node	PIN_MFP_SET(21,  ADCKEY_MFP_CFG)                            /* GPIO21的adckey 低速ADC3候选表项；实际模式以右侧配置值为准。 */

/* SPI NAND */
#define SPINAND_MFP_SEL         4                                                                                         /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define SPINAND_MFP_CFG (GPIO_CTL_MFP(SPINAND_MFP_SEL) | GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))                        /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define SPINAND_MFP_PU_CFG (GPIO_CTL_MFP(SPINAND_MFP_SEL)| GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3) | GPIO_CTL_PULLUP)    /* 复用/电气字段组合，包含施密特输入、上拉、驱动等级3；不代表候选引脚已经接通。 */
#define gpio8_spinand3_io2_node	    PIN_MFP_SET(8,   SPINAND_MFP_PU_CFG)                                                  /* GPIO8的spinand3 io2候选表项；实际模式以右侧配置值为准。 */
#define gpio9_spinand3_io3_node	    PIN_MFP_SET(9,   SPINAND_MFP_PU_CFG)                                                  /* GPIO9的spinand3 io3候选表项；实际模式以右侧配置值为准。 */
#define gpio10_spinand3_ss_node	    PIN_MFP_SET(10,  SPINAND_MFP_CFG)                                                     /* GPIO10的spinand3 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio11_spinand3_clk_node	PIN_MFP_SET(11,  SPINAND_MFP_CFG)                                                 /* GPIO11的spinand3 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio12_spinand3_io1_node	PIN_MFP_SET(12,  SPINAND_MFP_CFG)                                                 /* GPIO12的spinand3 io1候选表项；实际模式以右侧配置值为准。 */
#define gpio13_spinand3_io0_node	PIN_MFP_SET(13,  SPINAND_MFP_CFG)                                                 /* GPIO13的spinand3 io0候选表项；实际模式以右侧配置值为准。 */

#define gpio61_spinand3_io2_node	PIN_MFP_SET(61,  SPINAND_MFP_PU_CFG)    /* GPIO61的spinand3 io2候选表项；实际模式以右侧配置值为准。 */
#define gpio62_spinand3_io3_node	PIN_MFP_SET(62,  SPINAND_MFP_PU_CFG)    /* GPIO62的spinand3 io3候选表项；实际模式以右侧配置值为准。 */
#define gpio53_spinand3_ss_node     PIN_MFP_SET(53,  SPINAND_MFP_CFG)           /* GPIO53的spinand3 片选候选表项；实际模式以右侧配置值为准。 */
#define gpio54_spinand3_clk_node	PIN_MFP_SET(54,  SPINAND_MFP_CFG)       /* GPIO54的spinand3 时钟候选表项；实际模式以右侧配置值为准。 */
#define gpio55_spinand3_io1_node	PIN_MFP_SET(55,  SPINAND_MFP_CFG)       /* GPIO55的spinand3 io1候选表项；实际模式以右侧配置值为准。 */
#define gpio56_spinand3_io0_node	PIN_MFP_SET(56,  SPINAND_MFP_CFG)       /* GPIO56的spinand3 io0候选表项；实际模式以右侧配置值为准。 */

/* 电池热敏电阻NTC复用 */
#define BATNTC_MFP_SEL          28                                                                                                   /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
#define BATNTC_MFP_CFG (GPIO_CTL_MFP(BATNTC_MFP_SEL) | GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3))                                     /* 复用/电气字段组合，包含施密特输入、驱动等级3；不代表候选引脚已经接通。 */
#define BATNTC_REF_MFP_SEL      28                                                                                                   /* 引脚功能复用编码，须与芯片复用表及具体引脚匹配。 */
/* NTC参考配置按原板级要求上拉到3.4 V供电 */
#define BATNTC_REF_MFP_CFG (GPIO_CTL_MFP(BATNTC_REF_MFP_SEL) | GPIO_CTL_SMIT | GPIO_CTL_PADDRV_LEVEL(3) | GPIO_CTL_PULLUP_STRONG)    /* 复用/电气字段组合，包含施密特输入、强上拉、驱动等级3；不代表候选引脚已经接通。 */

#endif    /* PINCTRL_LEOPARD_H_ */
