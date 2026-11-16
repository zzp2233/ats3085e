/* 普通GPIO控制寄存器每脚4字节，输入/输出及置位/清零寄存器每32脚共用一个32位字。SPI引脚由MFP复用连接外设；GPIO输入输出使能与外设复用不是同一层控制。volatile访问不能替代并发保护。 */
/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Leopard GPIO编号、分组寄存器地址和引脚控制字段。
 */

#ifndef	_ACTIONS_SOC_GPIO_H_
#define	_ACTIONS_SOC_GPIO_H_
#include <device.h>


/** @brief 普通GPIO的绝对编号。当前枚举列出0～80，共81个编号；可用封装引脚还须核对板级接线。 */
typedef enum {
    GPIO_0 = 0,         /**< 普通GPIO绝对编号0。 */
    GPIO_1 = 1,         /**< 普通GPIO绝对编号1。 */
    GPIO_2 = 2,         /**< 普通GPIO绝对编号2。 */
    GPIO_3 = 3,         /**< 普通GPIO绝对编号3。 */
    GPIO_4 = 4,         /**< 普通GPIO绝对编号4。 */
    GPIO_5 = 5,         /**< 普通GPIO绝对编号5。 */
    GPIO_6 = 6,         /**< 普通GPIO绝对编号6。 */
    GPIO_7 = 7,         /**< 普通GPIO绝对编号7。 */
    GPIO_8 = 8,         /**< 普通GPIO绝对编号8。 */
    GPIO_9 = 9,         /**< 普通GPIO绝对编号9。 */
    GPIO_10 = 10,       /**< 普通GPIO绝对编号10。 */
    GPIO_11 = 11,       /**< 普通GPIO绝对编号11。 */
    GPIO_12 = 12,       /**< 普通GPIO绝对编号12。 */
    GPIO_13 = 13,       /**< 普通GPIO绝对编号13。 */
    GPIO_14 = 14,       /**< 普通GPIO绝对编号14。 */
    GPIO_15 = 15,       /**< 普通GPIO绝对编号15。 */
    GPIO_16 = 16,       /**< 普通GPIO绝对编号16。 */
    GPIO_17 = 17,       /**< 普通GPIO绝对编号17。 */
    GPIO_18 = 18,       /**< 普通GPIO绝对编号18。 */
    GPIO_19 = 19,       /**< 普通GPIO绝对编号19。 */
    GPIO_20 = 20,       /**< 普通GPIO绝对编号20。 */
    GPIO_21 = 21,       /**< 普通GPIO绝对编号21。 */
    GPIO_22 = 22,       /**< 普通GPIO绝对编号22。 */
    GPIO_23 = 23,       /**< 普通GPIO绝对编号23。 */
    GPIO_24 = 24,       /**< 普通GPIO绝对编号24。 */
    GPIO_25 = 25,       /**< 普通GPIO绝对编号25。 */
    GPIO_26 = 26,       /**< 普通GPIO绝对编号26。 */
    GPIO_27 = 27,       /**< 普通GPIO绝对编号27。 */
    GPIO_28 = 28,       /**< 普通GPIO绝对编号28。 */
    GPIO_29 = 29,       /**< 普通GPIO绝对编号29。 */
    GPIO_30 = 30,       /**< 普通GPIO绝对编号30。 */
    GPIO_31 = 31,       /**< 普通GPIO绝对编号31。 */
    GPIO_32 = 32,       /**< 普通GPIO绝对编号32。 */
    GPIO_33 = 33,       /**< 普通GPIO绝对编号33。 */
    GPIO_34 = 34,       /**< 普通GPIO绝对编号34。 */
    GPIO_35 = 35,       /**< 普通GPIO绝对编号35。 */
    GPIO_36 = 36,       /**< 普通GPIO绝对编号36。 */
    GPIO_37 = 37,       /**< 普通GPIO绝对编号37。 */
    GPIO_38 = 38,       /**< 普通GPIO绝对编号38。 */
    GPIO_39 = 39,       /**< 普通GPIO绝对编号39。 */
    GPIO_40 = 40,       /**< 普通GPIO绝对编号40。 */
    GPIO_41 = 41,       /**< 普通GPIO绝对编号41。 */
    GPIO_42 = 42,       /**< 普通GPIO绝对编号42。 */
    GPIO_43 = 43,       /**< 普通GPIO绝对编号43。 */
    GPIO_44 = 44,       /**< 普通GPIO绝对编号44。 */
    GPIO_45 = 45,       /**< 普通GPIO绝对编号45。 */
    GPIO_46 = 46,       /**< 普通GPIO绝对编号46。 */
    GPIO_47 = 47,       /**< 普通GPIO绝对编号47。 */
    GPIO_48 = 48,       /**< 普通GPIO绝对编号48。 */
    GPIO_49 = 49,       /**< 普通GPIO绝对编号49。 */
    GPIO_50 = 50,       /**< 普通GPIO绝对编号50。 */
    GPIO_51 = 51,       /**< 普通GPIO绝对编号51。 */
    GPIO_52 = 52,       /**< 普通GPIO绝对编号52。 */
    GPIO_53 = 53,       /**< 普通GPIO绝对编号53。 */
    GPIO_54 = 54,       /**< 普通GPIO绝对编号54。 */
    GPIO_55 = 55,       /**< 普通GPIO绝对编号55。 */
    GPIO_56 = 56,       /**< 普通GPIO绝对编号56。 */
    GPIO_57 = 57,       /**< 普通GPIO绝对编号57。 */
    GPIO_58 = 58,       /**< 普通GPIO绝对编号58。 */
    GPIO_59 = 59,       /**< 普通GPIO绝对编号59。 */
    GPIO_60 = 60,       /**< 普通GPIO绝对编号60。 */
    GPIO_61 = 61,       /**< 普通GPIO绝对编号61。 */
    GPIO_62 = 62,       /**< 普通GPIO绝对编号62。 */
    GPIO_63 = 63,       /**< 普通GPIO绝对编号63。 */
    GPIO_64 = 64,       /**< 普通GPIO绝对编号64。 */
    GPIO_65 = 65,       /**< 普通GPIO绝对编号65。 */
    GPIO_66 = 66,       /**< 普通GPIO绝对编号66。 */
    GPIO_67 = 67,       /**< 普通GPIO绝对编号67。 */
    GPIO_68 = 68,       /**< 普通GPIO绝对编号68。 */
    GPIO_69 = 69,       /**< 普通GPIO绝对编号69。 */
    GPIO_70 = 70,       /**< 普通GPIO绝对编号70。 */
    GPIO_71 = 71,       /**< 普通GPIO绝对编号71。 */
    GPIO_72 = 72,       /**< 普通GPIO绝对编号72。 */
    GPIO_73 = 73,       /**< 普通GPIO绝对编号73。 */
    GPIO_74 = 74,       /**< 普通GPIO绝对编号74。 */
    GPIO_75 = 75,       /**< 普通GPIO绝对编号75。 */
    GPIO_76 = 76,       /**< 普通GPIO绝对编号76。 */
    GPIO_77 = 77,       /**< 普通GPIO绝对编号77。 */
    GPIO_78 = 78,       /**< 普通GPIO绝对编号78。 */
    GPIO_79 = 79,       /**< 普通GPIO绝对编号79。 */
    GPIO_80 = 80,       /**< 普通GPIO绝对编号80。 */
    GPIO_MAX_PIN_NUM    /**< 普通GPIO枚举数量81，本项不是可用引脚编号。 */
} gpio_pin_e;

/** @brief 四个WIO低功耗引脚使用独立编号空间，不能直接当普通GPIO编号。 */
typedef enum {
    WIO_0 = 0,              /**< WIO组内编号0。 */
    WIO_1 = 1,              /**< WIO组内编号1。 */
    WIO_2 = 2,              /**< WIO组内编号2。 */
    WIO_3 = 3,              /**< WIO组内编号3。 */
    GPIO_WIO_MAX_PIN_NUM    /**< WIO枚举数量4，本项不是可用引脚编号。 */
} wio_pin_e;

#define GPIO_MAX_GROUPS				(((GPIO_MAX_PIN_NUM) + 31) / 32)    /* 按32脚向上取整的GPIO数据寄存器组数。 */
#define GPIO_MAX_IRQ_GRP			3                                   /* GPIO中断寄存器组数量配置。 */
#define GPIO_MAX_INT_PIN_NUM        64                                              /* 驱动定义的可中断引脚数量上限64，勿由总引脚数推断。 */

#define GPIO_WIO_REG_BASE			(GPIO_REG_BASE + 0x300)    /* WIO控制区位于普通GPIO基址之后0x300。 */

#define GPIO_CTL0				0x0      /* 第0脚复用与电气控制寄存器的偏移。 */
#define GPIO_ODAT0				0x200    /* 第0组输出数据寄存器偏移。 */
#define GPIO_BSR0				0x210    /* 第0组输出置位寄存器偏移。 */
#define GPIO_BRR0				0x220    /* 第0组输出清零寄存器偏移。 */
#define GPIO_IDAT0				0x230    /* 第0组输入电平寄存器偏移。 */
#define GPIO_IRQ_PD0			0x240            /* 第0组中断待处理寄存器偏移。 */

#define JTAG_CTL				0x40068400        /* 调试口控制寄存器绝对地址。 */
#define SC_SWEN_SHIFT			12                        /* 调试切换使能字段起始位。 */
#define SC_SWMAP_SHIFT			8                         /* 调试信号映射字段起始位。 */
#define JTAG_CTL_SC_SWEN(x)		((x) << SC_SWEN_SHIFT)    /* 将调用者提供的使能值左移到调试字段，不检查范围。 */
#define JTAG_CTL_SC_SWMAP(x)	((x) << SC_SWMAP_SHIFT)           /* 将映射值左移到调试映射字段，不自动掩码。 */

#define GPIO_REG_CTL(base, pin)			((base) + GPIO_CTL0 + (pin) * 4)            /* base为GPIO基址，pin为绝对编号；每脚控制字间隔4字节。 */
#define GPIO_REG_ODAT(base, pin)		((base) + GPIO_ODAT0 + (pin) / 32 * 4)      /* 按pin/32选输出数据组，组间隔4字节。 */
#define GPIO_REG_IDAT(base, pin)		((base) + GPIO_IDAT0 + (pin) / 32 * 4)      /* 按pin/32选输入数据组，组间隔4字节。 */
#define GPIO_REG_BSR(base, pin)			((base) + GPIO_BSR0 + (pin) / 32 * 4)       /* 选择对应组的输出置位地址；写入位图将选中脚置高。 */
#define GPIO_REG_BRR(base, pin)			((base) + GPIO_BRR0 + (pin) / 32 * 4)       /* 选择对应组的输出清零地址；写入位图将选中脚置低。 */
#define GPIO_REG_IRQ_PD(base, pin)		((base) + GPIO_IRQ_PD0 + (pin) / 32 * 4)    /* 选择引脚所在组的中断待处理地址。 */
#define GPIO_BIT(pin)					(1 << ((pin) % 32))                 /* pin%32转换为所在寄存器组内的单个位图。 */

#define GPION_CTL(n)					GPIO_REG_CTL(GPIO_REG_BASE,n)       /* 以SoC固定GPIO基址取得单脚控制寄存器地址，n为绝对引脚编号。 */
#define GPION_ODAT(n)					GPIO_REG_ODAT(GPIO_REG_BASE,n)      /* 以SoC固定GPIO基址取得分组输出数据寄存器地址，n为绝对引脚编号。 */
#define GPION_IDAT(n)					GPIO_REG_IDAT(GPIO_REG_BASE,n)      /* 以SoC固定GPIO基址取得分组输入数据寄存器地址，n为绝对引脚编号。 */
#define GPION_BSR(n)					GPIO_REG_BSR(GPIO_REG_BASE,n)       /* 以SoC固定GPIO基址取得分组置位寄存器地址，n为绝对引脚编号。 */
#define GPION_BRR(n)					GPIO_REG_BRR(GPIO_REG_BASE,n)       /* 以SoC固定GPIO基址取得分组清零寄存器地址，n为绝对引脚编号。 */
#define GPION_IRQ_PD(n)					GPIO_REG_IRQ_PD(GPIO_REG_BASE,n)    /* 以SoC固定GPIO基址取得分组中断待处理寄存器地址，n为绝对引脚编号。 */

#define WIO_REG_CTL(pin)				(GPIO_WIO_REG_BASE + (pin) * 4)    /* WIO编号0～3，每个控制寄存器间隔4字节。 */
#define WIO_CTL_INT_PD_SHIFT            (24)                                               /* WIO中断待处理字段位号24。 */
#define WIO_CTL_INT_PD_MASK             (1 << WIO_CTL_INT_PD_SHIFT)                        /* WIO中断待处理字段掩码。 */


#define GPIO_CTL_MFP_SHIFT				(0)                                    /* 引脚功能选择字段从位0开始。 */
#define GPIO_CTL_MFP_MASK				(0x1f << GPIO_CTL_MFP_SHIFT)           /* 功能选择位4:0掩码；数值需查复用表。 */
#define GPIO_CTL_MFP_GPIO				(0x0 << GPIO_CTL_MFP_SHIFT)            /* 功能编码0选择普通GPIO。 */
#define GPIO_CTL_SMIT					(0x1 << 5)                             /* 输入施密特触发控制位5。 */
#define GPIO_CTL_GPIO_OUTEN				(0x1 << 6)                             /* 普通GPIO输出使能位6。 */
#define GPIO_CTL_GPIO_INEN				(0x1 << 7)                             /* 普通GPIO输入使能位7。 */
#define GPIO_CTL_PULL_MASK				(0xf << 8)                             /* 上下拉配置位11:8的组合掩码。 */
#define GPIO_CTL_PULLUP_STRONG			(0x1 << 8)                                     /* 强上拉配置位8。 */
#define GPIO_CTL_PULLDOWN				(0x1 << 9)                             /* 下拉配置位9。 */
#define GPIO_CTL_PULLUP					(0x1 << 11)                            /* 上拉配置位11。 */
#define GPIO_CTL_PADDRV_SHIFT			(12)                                           /* 驱动强度字段从位12开始。 */
#define GPIO_CTL_PADDRV_LEVEL(x)		((x) << GPIO_CTL_PADDRV_SHIFT)                 /* 把等级x移入字段；宏本身不限制x，调用者须保证不覆盖其他位。 */
#define GPIO_CTL_PADDRV_MASK			GPIO_CTL_PADDRV_LEVEL(0x7)                     /* 代码定义的驱动强度掩码为位14:12，编码范围0～7。 */
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

/* 引脚复用更新允许修改MFP、上下拉、驱动强度和施密特字段；保留中断及GPIO输入输出使能等其他位。 */
#define PINMUX_MODE_MASK			(GPIO_CTL_MFP_MASK | \
                         GPIO_CTL_PULL_MASK | GPIO_CTL_PADDRV_MASK | \
                         GPIO_CTL_SMIT)


/**
  * @brief  先设置输出锁存值，再把引脚切为GPIO输出。
  * @param  pin：[输入] 普通GPIO绝对编号0～80，须为板级可用引脚。
  * @param  val：[输入] 非0输出高，0输出低。
  * @retval 无。
  * @note   直接写寄存器、不做参数检查；0x3840包含输出使能及本SDK的电气字段。最后整字覆盖引脚控制，不能与SPI复用并用。
  */
static inline void soc_gpio_output(uint32_t pin, uint32_t val)
{
	/* 先设置输出锁存器，减少切到输出时出现旧电平的机会。 */
	if (val) {                                             /* 非0请求通过置位寄存器输出高电平。 */
		sys_write32(GPIO_BIT(pin), GPION_BSR(pin));    /* 只写选中脚的位图，不读改写整组输出数据。 */
	} else {
		sys_write32(GPIO_BIT(pin), GPION_BRR(pin));    /* 只清选中脚的输出锁存位。 */
	}

	/* 再覆盖引脚控制字，接通GPIO输出；会改变原复用及电气设置。 */
	sys_write32(0x3840, GPION_CTL(pin));                   /* 一次写入GPIO输出及固定电气配置。 */
}

#endif    /* _ACTIONS_SOC_GPIO_H_	*/
