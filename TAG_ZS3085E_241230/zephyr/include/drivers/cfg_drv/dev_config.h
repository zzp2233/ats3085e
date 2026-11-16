/* 头文件保护使其可与board_cfg.h相互包含。当前Leopard构建进入CONFIG_SOC_SERIES_LEOPARD分支，选用pinctrl_leopard.h和dev_leopard.h；Lark分支保留供另一芯片使用。这些结构保存静态配置，不执行DMA或GPIO操作。 */
/**
 * @file
 *
 * @brief 驱动共享的DMA、GPIO和PWM板级配置结构及初始化器。
 */


#ifndef ZEPHYR_INCLUDE_DEV_CONFIG_H_
#define ZEPHYR_INCLUDE_DEV_CONFIG_H_

#include <board_cfg.h>
#if defined(CONFIG_SOC_SERIES_LARK)
#include <drivers/cfg_drv/pinctrl_lark.h>
#include <drivers/cfg_drv/dev_lark.h>
#elif defined(CONFIG_SOC_SERIES_LEOPARD)
#include <drivers/cfg_drv/pinctrl_leopard.h>
#include <drivers/cfg_drv/dev_leopard.h>
#else
#errro "not soc series"
#endif


struct dma_cfg {
	const char *dma_dev_name;    /* DMA控制器注册名，只读字符串须随配置长期有效。 */
	uint8_t dma_id;              /* 外设DMA请求槽编号，不是软件分配的通道号。 */
	uint8_t dma_chan;            /* 0xff选择虚拟通道，其余按驱动物理通道编号约定。 */
	uint8_t use_dma;             /* 0禁用该资源，非0启用。 */
};

struct gpio_cfg {
	const char *gpio_dev_name;    /* GPIO控制器名，由板级分组确定，字符串不转移所有权。 */
	uint8_t		gpion;        /* GPIO组内引脚或PWM通道编号，原接口范围0～31。 */
	uint8_t		flag;         /* GPIO_ACTIVE_HIGH高有效或GPIO_ACTIVE_LOW低有效。 */
	uint8_t		use_gpio;     /* 0禁用该资源，非0启用。 */
};

struct pwm_cfg {
	const char *dev_name;      /* PWM控制器注册名，指向长期有效只读字符串。 */
	uint16_t	period;    /* PWM周期，单位为PWM时钟周期，也是最大占空计数。 */
	uint8_t		chan;      /* GPIO组内引脚或PWM通道编号，原接口范围0～31。 */
	uint8_t		flag;      /* 1高电平有效，0低电平有效。 */
};

/* 构造dma_cfg花括号初始化器：设备名、请求槽、通道、使能；仅生成数据。 */
#define DMA_CFG_MAKE(dev_name, id, chan, use) {\
		.dma_dev_name =  dev_name, \
		.dma_id = id,\
		.dma_chan = chan,\
		.use_dma = use,	\
	}

/* 供条件宏展开的DMA字段序列，以括号包裹逗号，不作为普通运行时表达式。 */
#define DMA_CFG_SET(dev_name, id, chan, use) (\
		.dma_dev_name =  dev_name, \
		.dma_id = id,\
		.dma_chan = chan,\
		.use_dma = use,	\
		)

/* 编译时按_flag选择DMA字段；关闭时写NULL、0、0、0，不申请通道。 */
#define COND_DMA_CODE(_flag, id, chan) COND_CODE_1(_flag,DMA_CFG_SET(CONFIG_DMA_0_NAME, id, chan, 1), DMA_CFG_SET(NULL, 0, 0, 0))

/* 构造gpio_cfg初始化器：设备名、组内引脚、有效极性、是否使用。 */
#define GPIO_CFG_MAKE(dev_name, gpio, gflag, use) {\
		.gpio_dev_name =  dev_name, \
		.gpion = gpio,\
		.flag = gflag,\
		.use_gpio = use,\
	}

/* 供条件宏展开的GPIO字段序列，保持宏续行与逗号语义。 */
#define GPIO_CFG_SET(dev_name, gpio, gflag, use) (\
		.gpio_dev_name =  dev_name, \
		.gpion = gpio,\
		.flag = gflag,\
		.use_gpio = use,\
		)

/* 编译时选择GPIO配置，关闭时填空设备及零值。 */
#define COND_GPIO_CODE(_flag, gpio_dev, gpio, gflag) COND_CODE_1(_flag,GPIO_CFG_SET(gpio_dev, gpio, gflag, 1), GPIO_CFG_SET(NULL, 0, 0, 0))

/* 构造PWM初始化器：控制器名、通道、周期计数、有效电平；不启动PWM。 */
#define PWM_CFG_MAKE(_dev_name, _chan, _period, _flag) {\
		.dev_name = _dev_name, \
		.chan = _chan,\
		.period = _period,\
		.flag = _flag,	\
	}

#endif //ZEPHYR_INCLUDE_DEV_CONFIG_H_
