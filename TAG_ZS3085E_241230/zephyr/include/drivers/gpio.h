/*
 * Copyright (c) 2019-2020 Nordic Semiconductor ASA
 * Copyright (c) 2019 Piotr Mienkowski
 * Copyright (c) 2017 ARM Ltd
 * Copyright (c) 2015-2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GPIO 公共接口。端口对象提供一组引脚，raw 接口按实际电平取值，逻辑接口根据 GPIO_ACTIVE_LOW 换算。
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_GPIO_H_    /* 避免同一翻译单元重复声明 GPIO 接口。 */
#define ZEPHYR_INCLUDE_DRIVERS_GPIO_H_

#include <sys/__assert.h>    /* 断言用于检查配置互斥条件与支持的引脚范围。 */
#include <sys/slist.h>       /* 提供驱动回调对象使用的单向链表节点。 */

#include <zephyr/types.h>             /* 引入定宽整数类型以保存位图和标志。 */
#include <stddef.h>                   /* 提供空指针等标准定义。 */
#include <device.h>                   /* 操作经端口设备对象的 api、config 和 data 分派。 */
#include <dt-bindings/gpio/gpio.h>    /* 引入与设备树绑定共用的极性、上下拉等配置位。 */

#ifdef __cplusplus
/* 让 C++ 调用者按 C 链接约定访问公共接口。 */
extern "C" {
#endif

/**
 * @defgroup gpio_apis GPIO 应用接口
 * @ingroup driver_apis
 * @{
 */
/**
 * @brief Actions 端口的输出、输入与中断用法。
 *
 *    GPIO0～31 对应 GPIOA，GPIO32～63 对应 GPIOB；GPIO64 起的已实现普通引脚对应 GPIOC。
 *    调用公共接口时使用端口内编号。SPI READY 的 GPIO61 对应 CONFIG_GPIO_B_NAME 的 29 号引脚。
 *    先绑定板级设备，再检查绑定结果或读取错误，最后使用端口内编号和位图。
 *
 * @code
 *
 *    #include <drivers/gpio.h>
 *
 *    // 输出示例、输入示例和回调示例各自独立。以下先绑定端口并配置输出。
 *    uint8_t gpio_out_pin = 6, gpio_out_level = 1;
 *    const char *gpio_dev_name = CONFIG_GPIO_A_NAME;
 *    int ret;
 *
 *    // 上下拉互斥；Actions 驱动将 GPIO_INT_DEBOUNCE 映射为施密特输入。
 *    // 初始输出通过 GPIO_OUTPUT_INIT_HIGH/LOW 选择。
 *    // GPIO_OUTPUT 单独使用会打开输出，保持原锁存电平。
 *
 *    gpio_flags_t flags = GPIO_OUTPUT;
 *
 *    const struct device *gpio_dev = device_get_binding(gpio_dev_name);
 *    if (!gpio_dev) {
 *        printk("failed to bind device %s\n", gpio_dev_name);
 *        return -ENODEV;
 *    }
 *
 *    ret = gpio_pin_configure(gpio_dev, gpio_out_pin, flags);
 *    if (ret) {
 *        printk("failed to configure gpio:%d output\n", gpio_out_pin);
 *        return ret;
 *    }
 *
 *    // 这里未选择低有效，所以逻辑 1 输出高电平，逻辑 0 输出低电平。
 *    gpio_pin_set(gpio_dev, gpio_out_pin, gpio_out_level);
 *
 *    // 输入示例：将引脚配置为输入，读取逻辑电平。
 *    uint8_t gpio_in_pin = 6;
 *    const char *gpio_dev_name = CONFIG_GPIO_A_NAME;
 *    int gpio_in_level;
 *
 *    const struct device *gpio_dev = device_get_binding(gpio_dev_name);
 *    if (!gpio_dev) {
 *        printk("failed to bind device %s\n", gpio_dev_name);
 *        return -ENODEV;
 *    }
 *
 *    ret = gpio_pin_configure(gpio_dev, gpio_in_pin, GPIO_INPUT);
 *    if (ret) {
 *        printk("failed to configure gpio:%d input\n", gpio_in_pin);
 *        return ret;
 *    }
 *
 *    // 成功返回逻辑值 0 或 1，负数表示驱动读取失败。
 *    gpio_in_level = gpio_pin_get(gpio_dev, gpio_in_pin);
 *
 *    // 中断示例：回调函数和静态回调对象具有文件作用域；绑定、登记和使能操作在初始化函数内执行。
 *    uint8_t gpio_irq_pin = 6;
 *    const char *gpio_dev_name = CONFIG_GPIO_A_NAME;
 *    static struct gpio_callback gpio_irq_cb;
 *
 *    static void gpio_irq_callback(const struct device *port,
 *					struct gpio_callback *cb,
 *					uint32_t pins) {
 *        printk("GPIO IRQ callback\n");
 *    }
 *
 *    const struct device *gpio_dev = device_get_binding(gpio_dev_name);
 *    if (!gpio_dev) {
 *        printk("failed to bind device %s\n", gpio_dev_name);
 *        return -ENODEV;
 *    }
 *
 *    gpio_pin_configure(gpio_dev, gpio_irq_pin, GPIO_INPUT | GPIO_ACTIVE_HIGH);
 *
 *    gpio_init_callback(&gpio_irq_cb, gpio_irq_callback, BIT(gpio_irq_pin));
 *
 *    gpio_add_callback(gpio_dev, &gpio_irq_cb);
 *    gpio_pin_interrupt_configure(gpio_dev, gpio_irq_pin, GPIO_INT_EDGE_TO_INACTIVE);
 * @endcode
 */
/**
 * @}
 */

/**
 * @brief GPIO 端口、引脚配置与中断注册。
 * @defgroup gpio_interface GPIO 驱动接口
 * @ingroup io_interfaces
 * @{
 */

/**
 * @name 输入与输出方向及初值
 * @{
 */

/** 使能引脚输入。 */
#define GPIO_INPUT              (1U << 8)

/** 使能输出，保持原有输出锁存值。 */
#define GPIO_OUTPUT             (1U << 9)

/** 同时禁用输入和输出。 */
#define GPIO_DISCONNECTED	0

/** @cond INTERNAL_HIDDEN */

/** 设置输出的物理低初值。 */
#define GPIO_OUTPUT_INIT_LOW    (1U << 10)

/** 设置输出的物理高初值。 */
#define GPIO_OUTPUT_INIT_HIGH   (1U << 11)

/** 用逻辑极性解释输出初值。 */
#define GPIO_OUTPUT_INIT_LOGICAL (1U << 12)

/** @endcond */

/** 使能输出并初始化为物理低电平。 */
#define GPIO_OUTPUT_LOW         (GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW)
/** 使能输出并初始化为物理高电平。 */
#define GPIO_OUTPUT_HIGH        (GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH)
/** 使能输出并初始化为逻辑无效，实际电平取决于 GPIO_ACTIVE_LOW。 */
#define GPIO_OUTPUT_INACTIVE    (GPIO_OUTPUT |			\
				 GPIO_OUTPUT_INIT_LOW |		\
				 GPIO_OUTPUT_INIT_LOGICAL)
/** 使能输出并初始化为逻辑有效。 */
#define GPIO_OUTPUT_ACTIVE      (GPIO_OUTPUT |			\
				 GPIO_OUTPUT_INIT_HIGH |	\
				 GPIO_OUTPUT_INIT_LOGICAL)

/** @} */

/**
 * @name GPIO 中断触发方式
 * 物理触发直接使用引脚电平。逻辑触发参考 GPIO_ACTIVE_LOW，低有效时低电平表示有效（1），高电平表示无效（0）。
 * @{
 */

/** 关闭该引脚的中断检测。 */
#define GPIO_INT_DISABLE               (1U << 13)

/** @cond INTERNAL_HIDDEN */

/** 打开引脚中断检测，须配合具体触发条件。 */
#define GPIO_INT_ENABLE                (1U << 14)

/** 按配置好的有效极性解释触发方向，需与触发模式位组合。 */
#define GPIO_INT_LEVELS_LOGICAL        (1U << 15)

/** 按边沿触发，未指定本位时启用的中断按电平触发。 */
#define GPIO_INT_EDGE                  (1U << 16)

/** 针对低物理电平或逻辑 0 设置触发，配合模式可选电平或进入该状态的边沿。 */
#define GPIO_INT_LOW_0                 (1U << 17)

/** 针对高物理电平或逻辑 1 设置触发，需配合使能及模式位。 */
#define GPIO_INT_HIGH_1                (1U << 18)

/* 掩码包含中断使能、极性和触发模式；引脚方向配置接口用它识别误传的中断标志。 */
#define GPIO_INT_MASK                  (GPIO_INT_DISABLE | \
					GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_EDGE | \
					GPIO_INT_LOW_0 | \
					GPIO_INT_HIGH_1)

/** @endcond */

/** 使能物理上升沿中断。 */
#define GPIO_INT_EDGE_RISING           (GPIO_INT_ENABLE | \
					GPIO_INT_EDGE | \
					GPIO_INT_HIGH_1)

/** 使能物理下降沿中断。 */
#define GPIO_INT_EDGE_FALLING          (GPIO_INT_ENABLE | \
					GPIO_INT_EDGE | \
					GPIO_INT_LOW_0)

/** 使能物理双边沿中断，READY 用两种转换发布通知。 */
#define GPIO_INT_EDGE_BOTH             (GPIO_INT_ENABLE | \
					GPIO_INT_EDGE | \
					GPIO_INT_LOW_0 | \
					GPIO_INT_HIGH_1)

/** 使能物理低电平触发。 */
#define GPIO_INT_LEVEL_LOW             (GPIO_INT_ENABLE | \
					GPIO_INT_LOW_0)

/** 使能物理高电平触发。 */
#define GPIO_INT_LEVEL_HIGH            (GPIO_INT_ENABLE | \
					GPIO_INT_HIGH_1)

/** 使能变为逻辑无效的边沿，极性来自引脚配置。 */
#define GPIO_INT_EDGE_TO_INACTIVE      (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_EDGE | \
					GPIO_INT_LOW_0)

/** 使能变为逻辑有效的边沿。 */
#define GPIO_INT_EDGE_TO_ACTIVE        (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_EDGE | \
					GPIO_INT_HIGH_1)

/** 引脚逻辑无效期间触发中断。 */
#define GPIO_INT_LEVEL_INACTIVE        (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_LOW_0)

/** 引脚逻辑有效期间触发中断。 */
#define GPIO_INT_LEVEL_ACTIVE          (GPIO_INT_ENABLE | \
					GPIO_INT_LEVELS_LOGICAL | \
					GPIO_INT_HIGH_1)

/** @} */

/** 为引脚配置请求消抖。无此能力的驱动应忽略此标志；Actions 以此启用施密特输入，用于改善门限附近的波动。 */
#define GPIO_INT_DEBOUNCE              (1U << 19)

/**
 * @name 输入输出驱动强度选项
 * 通过 gpio_pin_configure 独立选择高、低输出时的强度。DFLT 和 ALT 分别指硬件支持的最低和最高强度；仅有一个档位时两者相同。具体支持情况由驱动决定。
 * @{
 */
/** @cond INTERNAL_HIDDEN */
#define GPIO_DS_LOW_POS 20                            /* 低电平驱动强度的起始位。 */
#define GPIO_DS_LOW_MASK (0x3U << GPIO_DS_LOW_POS)    /* 提取低电平输出强度字段。 */
/** @endcond */

/** 低电平输出时选择缺省驱动强度。 */
#define GPIO_DS_DFLT_LOW (0x0U << GPIO_DS_LOW_POS)

/** 低电平输出时请求另一档强度。仅有一个档位时沿用默认强度。 */
#define GPIO_DS_ALT_LOW (0x1U << GPIO_DS_LOW_POS)

/** @cond INTERNAL_HIDDEN */
#define GPIO_DS_HIGH_POS 22                             /* 高电平驱动强度的起始位。 */
#define GPIO_DS_HIGH_MASK (0x3U << GPIO_DS_HIGH_POS)    /* 提取高电平输出强度字段。 */
/** @endcond */

/** 高电平输出时选择缺省驱动强度。 */
#define GPIO_DS_DFLT_HIGH (0x0U << GPIO_DS_HIGH_POS)

/** 高电平输出时请求另一档强度。单一档位的控制器沿用默认。 */
#define GPIO_DS_ALT_HIGH (0x1U << GPIO_DS_HIGH_POS)
/** @} */

/** @cond INTERNAL_HIDDEN */
#define GPIO_DIR_MASK		(GPIO_INPUT | GPIO_OUTPUT)    /* 只筛取输入及输出方向位。 */
/** @endcond */

/** @brief 端口引脚集合，第 n 位置 1 表示选中端口内第 n 脚。 */
typedef uint32_t gpio_port_pins_t;

/** @brief 端口读写值，每位对应一脚。物理值的 1 表示高电平，逻辑值的 1 表示有效，可配合引脚掩码指定有效位置。 */
typedef uint32_t gpio_port_value_t;

/** @brief 端口内引脚索引，压缩为八位以便保存在设备树描述中。 */
typedef uint8_t gpio_pin_t;

/** @brief 设备树可保存的 GPIO 标志，取完整配置字段的低八位。 */
typedef uint8_t gpio_dt_flags_t;

/** @brief 引脚和中断配置共用的完整标志类型。 */
typedef uint32_t gpio_flags_t;

/** @brief 设备树定义的单脚描述，包含端口对象、组内编号和设备树标志。 */
struct gpio_dt_spec {
	const struct device *port;    /* 已绑定的 GPIO 控制器实例，使用前检查可用性。 */
	gpio_pin_t pin;               /* 端口组内的引脚编号。 */
	gpio_dt_flags_t dt_flags;     /* 设备树规定的低八位配置标志。 */
};

/**
 * @brief 按节点属性及索引生成 gpio_dt_spec 静态初始化器。
 *
 * node_id 和 prop 确定设备树属性，idx 是其中从 0 开始的索引。
 *
 * 设备树属性含两个引脚时：
 *
 *	n: node {
 *		foo-gpios = <&gpio0 1 GPIO_ACTIVE_LOW>,
 *			    <&gpio1 2 GPIO_ACTIVE_LOW>;
 *	}
 *
 * 取索引 1 对应的第二个引脚：
 *
 *	const struct gpio_dt_spec spec = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(n),
 *								 foo_gpios, 1);
 *	// 展开后的端口、引脚及有效极性如下。
 *	// {
 *	//         .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
 *	//         .pin = 2,
 *	//         .dt_flags = GPIO_ACTIVE_LOW
 *	// }
 *
 * 宏展开要求节点和属性有效，属性提供控制器、引脚和标志；获得端口后仍须检查设备是否就绪。
 * @param node_id 设备树节点。
 * @param prop 小写并用下划线表示的属性名。
 * @param idx 属性的零基索引。
 * @return 可静态初始化端口、引脚和标志的表达式。
 */
#define GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx)			       \
	{								       \
		.port = DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(node_id, prop, idx)),\
		.pin = DT_GPIO_PIN_BY_IDX(node_id, prop, idx),		       \
		.dt_flags = DT_GPIO_FLAGS_BY_IDX(node_id, prop, idx),	       \
	}

/**
  * @brief  从节点的 GPIO 属性按索引构造引脚描述。
  * @param  node_id：[输入]设备树节点标识。
  * @param  prop：[输入]GPIO 属性名，按设备树小写下划线写法。
  * @param  idx：[输入]属性中 GPIO 项的零基索引。
  * @param  default_value：[输入]未定义属性时使用的默认初始化器。
  * @retval gpio_dt_spec 的静态初始化器。节点或属性不存在时展开默认值。
  * @note   使用前检查描述中引用的端口是否就绪。
  */
#define GPIO_DT_SPEC_GET_BY_IDX_OR(node_id, prop, idx, default_value)	\
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, prop),			\
		    (GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx)),	\
		    (default_value))

/**
  * @brief  从节点的 GPIO 属性取首项构造引脚描述。
  * @param  node_id：[输入]设备树节点标识。
  * @param  prop：[输入]GPIO 属性名，按设备树小写下划线写法。
  * @retval gpio_dt_spec 的静态初始化器。
  * @note   使用前检查描述中引用的端口是否就绪。
  */
#define GPIO_DT_SPEC_GET(node_id, prop) \
	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, 0)

/**
  * @brief  从节点的 GPIO 属性取首项构造引脚描述。
  * @param  node_id：[输入]设备树节点标识。
  * @param  prop：[输入]GPIO 属性名，按设备树小写下划线写法。
  * @param  default_value：[输入]未定义属性时使用的默认初始化器。
  * @retval gpio_dt_spec 的静态初始化器。节点或属性不存在时展开默认值。
  * @note   使用前检查描述中引用的端口是否就绪。
  */
#define GPIO_DT_SPEC_GET_OR(node_id, prop, default_value) \
	GPIO_DT_SPEC_GET_BY_IDX_OR(node_id, prop, 0, default_value)

/**
  * @brief  从驱动实例的 GPIO 属性按索引构造引脚描述。
  * @param  inst：[输入]DT_DRV_COMPAT 匹配的实例编号。
  * @param  prop：[输入]GPIO 属性名，按设备树小写下划线写法。
  * @param  idx：[输入]属性中 GPIO 项的零基索引。
  * @retval gpio_dt_spec 的静态初始化器。
  * @note   使用前检查描述中引用的端口是否就绪。
  */
#define GPIO_DT_SPEC_INST_GET_BY_IDX(inst, prop, idx) \
	GPIO_DT_SPEC_GET_BY_IDX(DT_DRV_INST(inst), prop, idx)

/**
  * @brief  从驱动实例的 GPIO 属性按索引构造引脚描述。
  * @param  inst：[输入]DT_DRV_COMPAT 匹配的实例编号。
  * @param  prop：[输入]GPIO 属性名，按设备树小写下划线写法。
  * @param  idx：[输入]属性中 GPIO 项的零基索引。
  * @param  default_value：[输入]未定义属性时使用的默认初始化器。
  * @retval gpio_dt_spec 的静态初始化器。节点或属性不存在时展开默认值。
  * @note   使用前检查描述中引用的端口是否就绪。
  */
#define GPIO_DT_SPEC_INST_GET_BY_IDX_OR(inst, prop, idx, default_value)	\
	GPIO_DT_SPEC_GET_BY_IDX_OR(DT_DRV_INST(inst), prop, idx, default_value)

/**
  * @brief  从驱动实例的 GPIO 属性取首项构造引脚描述。
  * @param  inst：[输入]DT_DRV_COMPAT 匹配的实例编号。
  * @param  prop：[输入]GPIO 属性名，按设备树小写下划线写法。
  * @retval gpio_dt_spec 的静态初始化器。
  * @note   使用前检查描述中引用的端口是否就绪。
  */
#define GPIO_DT_SPEC_INST_GET(inst, prop) \
	GPIO_DT_SPEC_INST_GET_BY_IDX(inst, prop, 0)

/**
  * @brief  从驱动实例的 GPIO 属性取首项构造引脚描述。
  * @param  inst：[输入]DT_DRV_COMPAT 匹配的实例编号。
  * @param  prop：[输入]GPIO 属性名，按设备树小写下划线写法。
  * @param  default_value：[输入]未定义属性时使用的默认初始化器。
  * @retval gpio_dt_spec 的静态初始化器。节点或属性不存在时展开默认值。
  * @note   使用前检查描述中引用的端口是否就绪。
  */
#define GPIO_DT_SPEC_INST_GET_OR(inst, prop, default_value) \
	GPIO_DT_SPEC_INST_GET_BY_IDX_OR(inst, prop, 0, default_value)

/** 引脚位图类型能表示的每端口最大引脚数。 */
#define GPIO_MAX_PINS_PER_PORT (sizeof(gpio_port_pins_t) * __CHAR_BIT__)

/** 通用只读配置，必须置于各 GPIO 驱动设备 config 对象的首成员。 */
struct gpio_driver_config {
	/** 驱动创建设备实例时设置的有效引脚掩码。 */
	gpio_port_pins_t port_pin_mask;
};

/** 各 GPIO 驱动的可写 data 首成员，公共包装层可据此读取极性。 */
struct gpio_driver_data {
	/** 由公共配置接口维护的低有效引脚位图，供逻辑读、写及触发转换。 */
	gpio_port_pins_t invert;
};

struct gpio_callback;    /* 前向声明供回调函数指针引用。 */

/**
 * @typedef gpio_callback_handler_t
 * @brief 驱动在 GPIO 中断中向订阅该引脚的处理函数发出通知。
 * @param port [输入]驱动传来的端口设备。Actions 共用一个 IRQ，实际传参固定为端口 0。
 * @param cb [输入]已登记的回调对象，可用 CONTAINER_OF 找到含它的上下文。
 * @param pins [输入]本次待决位和订阅掩码的交集，单位为端口内位。
 * @retval 无。
 */
typedef void (*gpio_callback_handler_t)(const struct device *port,
					struct gpio_callback *cb,
					gpio_port_pins_t pins);

/** 用于登记 GPIO 事件的回调对象。每个订阅使用不同的持久对象，先由 gpio_init_callback 初始化；注册仍有效时不可让局部对象离开作用域。 */
struct gpio_callback {
	/** 由驱动维护的链表节点，调用者不可自行改链。 */
	sys_snode_t node;

	/** 订阅位与待决位相交时要调用的处理函数。 */
	gpio_callback_handler_t handler;

	/** 订阅的引脚位图。零掩码不接收通知，所有者可更新掩码，相关引脚须独立配置中断触发。 */
	gpio_port_pins_t pin_mask;
};

/**
 * @cond INTERNAL_HIDDEN
 *
 * 仅供驱动实现，经公共包装函数访问。
 */

/** 供驱动使用的两组枚举，与公共标志共用编码，经过掩码即可送到硬件接口。 */
enum gpio_int_mode {
	GPIO_INT_MODE_DISABLED = GPIO_INT_DISABLE,               /* 关闭中断模式。 */
	GPIO_INT_MODE_LEVEL = GPIO_INT_ENABLE,                   /* 已使能，按持续电平触发。 */
	GPIO_INT_MODE_EDGE = GPIO_INT_ENABLE | GPIO_INT_EDGE,    /* 已使能，按电平变化触发。 */
};

enum gpio_int_trig {
	/** 低电平触发，边沿模式下对应下降沿。 */
	GPIO_INT_TRIG_LOW = GPIO_INT_LOW_0,
	/** 高电平触发，边沿模式下对应上升沿。 */
	GPIO_INT_TRIG_HIGH = GPIO_INT_HIGH_1,
	/** 物理上升、下降两个方向都触发。 */
	GPIO_INT_TRIG_BOTH = GPIO_INT_LOW_0 | GPIO_INT_HIGH_1,
};

/* 公共层从 port->api 取得下列入口。除读取待决位图外，各操作返回 0 表示成功，负值报告错误。 */
__subsystem struct gpio_driver_api {
	int (*pin_configure)(const struct device *port, gpio_pin_t pin,
			     gpio_flags_t flags);                                  /* 配置端口内 pin 的方向和电气特性，传入已经换算的 flags。 */
	int (*port_get_raw)(const struct device *port,
			    gpio_port_value_t *value);                             /* 向可写 value 填入整组物理电平，失败返回负错误码。 */
	int (*port_set_masked_raw)(const struct device *port,
				   gpio_port_pins_t mask,
				   gpio_port_value_t value);                       /* 对 mask 选中的输出应用 value 的物理电平，其他引脚保持原值。 */
	int (*port_set_bits_raw)(const struct device *port,
				 gpio_port_pins_t pins);                           /* 以端口位图选取需要物理置高的输出。 */
	int (*port_clear_bits_raw)(const struct device *port,
				   gpio_port_pins_t pins);                         /* 以端口位图选取需要物理置低的输出。 */
	int (*port_toggle_bits)(const struct device *port,
				gpio_port_pins_t pins);                            /* 只翻转 pins 选中的引脚，返回底层状态。 */
	int (*pin_interrupt_configure)(const struct device *port,
				       gpio_pin_t pin,
				       enum gpio_int_mode, enum gpio_int_trig);    /* 按 pin 定位引脚，两个枚举依次指定开关或检测模式及物理触发方向。 */
	int (*manage_callback)(const struct device *port,
			       struct gpio_callback *cb,
			       bool set);                                          /* 登记或移除持久回调对象，set 为真时登记，返回状态。 */
	uint32_t (*get_pending_int)(const struct device *dev);                     /* 读取 dev 的待决位图，为可选操作。Actions 的操作表没有实现此项。 */
};

/**
 * @endcond
 */


/**
  * @brief  配置单个引脚的中断检测方式。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @param  flags：[输入]GPIO_INT_* 触发位，指定启用或禁用及所需方向。
  * @retval 0 表示成功，-ENOTSUP 表示配置不支持，-EINVAL 参数无效，-EBUSY 中断资源已占用，-EIO 为访问错误，-EWOULDBLOCK 表示当前上下文无法等待。具体错误由端口驱动提供。
  * @note   公共 API 可用于能保持外设复用的控制器；Actions 实现会将该脚 MFP 清成 GPIO。READY 使用物理双边沿。
  */
__syscall int gpio_pin_interrupt_configure(const struct device *port,
					   gpio_pin_t pin,
					   gpio_flags_t flags);

/**
  * @brief  配置单个引脚的中断检测方式。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @param  flags：[输入]GPIO_INT_* 触发位，指定启用或禁用及所需方向。
  * @retval 0 表示成功，-ENOTSUP 表示配置不支持，-EINVAL 参数无效，-EBUSY 中断资源已占用，-EIO 为访问错误，-EWOULDBLOCK 表示当前上下文无法等待。具体错误由端口驱动提供。
  * @note   公共 API 可用于能保持外设复用的控制器；Actions 实现会将该脚 MFP 清成 GPIO。READY 使用物理双边沿。
  */
static inline int z_impl_gpio_pin_interrupt_configure(const struct device *port,
						      gpio_pin_t pin,
						      gpio_flags_t flags)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;                                            /* 从端口设备取实际驱动操作表。 */
	const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;                                      /* 从只读配置首成员取得设备支持的引脚掩码。 */
	const struct gpio_driver_data *const data =
		(const struct gpio_driver_data *)port->data;                                          /* 读取该端口保存的低有效引脚位图。 */
	enum gpio_int_trig trig;                                                                      /* 保存传给驱动的物理触发方向。 */
	enum gpio_int_mode mode;                                                                      /* 保存开关、边沿或电平模式。 */

	/* 校验中断配置的使能、极性和触发条件，防止把方向配置位误作中断选项。 */
	__ASSERT_NO_MSG((flags & GPIO_INT_DEBOUNCE) == 0);                                            /* 消抖应随引脚属性设置，不属于中断触发位。 */

	__ASSERT((flags & (GPIO_INT_DISABLE | GPIO_INT_ENABLE))
		 != (GPIO_INT_DISABLE | GPIO_INT_ENABLE),
		 "Cannot both enable and disable interrupts");                                        /* 同时使能和禁用为矛盾请求。 */

	__ASSERT((flags & (GPIO_INT_DISABLE | GPIO_INT_ENABLE)) != 0U,
		 "Must either enable or disable interrupts");                                         /* 必须明确启用或禁用该引脚中断。 */

	__ASSERT(((flags & GPIO_INT_ENABLE) == 0) ||
		 ((flags & GPIO_INT_EDGE) != 0) ||
		 ((flags & (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)) !=
		  (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)),
		 "Only one of GPIO_INT_LOW_0, GPIO_INT_HIGH_1 can be "
		 "enabled for a level interrupt.");                                                   /* 持续电平触发只能选择高或低，边沿方式才可同时选择双向。 */

	__ASSERT(((flags & GPIO_INT_ENABLE) == 0) ||
		 ((flags & (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)) != 0),
		 "At least one of GPIO_INT_LOW_0, GPIO_INT_HIGH_1 has to be "
		 "enabled.");                                                                         /* 启用中断时必须选中至少一个触发方向。 */

	(void)cfg;                                                                                    /* 即使构建关闭断言也保留对校验配置变量的引用。 */
	__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
		 "Unsupported pin");                                                                  /* 确认该引脚在设备声明的有效位图内。 */

	/* 配置为逻辑触发且该脚低有效时，将逻辑方向换成物理触发位。 */
	if (((flags & GPIO_INT_LEVELS_LOGICAL) != 0) &&
	    ((data->invert & (gpio_port_pins_t)BIT(pin)) != 0)) {                                     /* 只有请求逻辑触发且该脚低有效时才换算方向。 */
		/* 对单个逻辑触发方向，交换低或高这两位以匹配管脚的实际极性。 */
		flags ^= (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1);                                          /* 用异或翻转高低两个标志位，对单边沿交换上升与下降。 */
	}

	/* 截取硬件中断接口需要的模式和触发方向，通过设备函数表提交。 */
	trig = (enum gpio_int_trig)(flags & (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1));                      /* 提取低、高方向的组合供驱动编码。 */
	mode = (enum gpio_int_mode)(flags & (GPIO_INT_EDGE | GPIO_INT_DISABLE | GPIO_INT_ENABLE));    /* 截取使能、禁用和边沿位来表示检测模式。 */

	return api->pin_interrupt_configure(port, pin, mode, trig);                                   /* 实际寄存器配置由此端口注册的驱动完成。 */
}


/**
  * @brief  从设备树描述取得端口和引脚并配置中断。
  * @param  spec：[输入]非空的引脚描述，内部 port 已就绪，pin 为端口内有效编号。
  * @param  flags：[输入]GPIO_INT_* 触发位，指定启用或禁用及所需方向。
  * @retval 0 表示成功，-ENOTSUP 表示配置不支持，-EINVAL 参数无效，-EBUSY 中断资源已占用，-EIO 为访问错误，-EWOULDBLOCK 表示当前上下文无法等待。具体错误由端口驱动提供。
  * @note   该包装只传递显式 flags，使用逻辑触发时极性取自先前的引脚配置。
  */
static inline int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *spec,
						  gpio_flags_t flags)
{
	return gpio_pin_interrupt_configure(spec->port, spec->pin, flags);    /* 按设备树记录的端口与引脚配置中断，使用显式 flags。 */
}


/**
  * @brief  配置引脚方向、初始输出电平和电气属性。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @param  flags：[输入]GPIO 方向、上下拉、极性及输出初值等标志。
  * @retval 0 表示成功，-ENOTSUP 表示配置不支持，-EINVAL 参数无效，-EIO 为访问错误，-EWOULDBLOCK 表示当前上下文无法等待。具体错误由端口驱动提供。
  * @note   中断触发另由 gpio_pin_interrupt_configure 配置。包装层在硬件调用之前记录软件极性，驱动失败时此处没有回滚。
  */
__syscall int gpio_pin_configure(const struct device *port,
				 gpio_pin_t pin,
				 gpio_flags_t flags);

/**
  * @brief  配置引脚方向、初始输出电平和电气属性。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @param  flags：[输入]GPIO 方向、上下拉、极性及输出初值等标志。
  * @retval 0 表示成功，-ENOTSUP 表示配置不支持，-EINVAL 参数无效，-EIO 为访问错误，-EWOULDBLOCK 表示当前上下文无法等待。具体错误由端口驱动提供。
  * @note   中断触发另由 gpio_pin_interrupt_configure 配置。包装层在硬件调用之前记录软件极性，驱动失败时此处没有回滚。
  */
static inline int z_impl_gpio_pin_configure(const struct device *port,
					    gpio_pin_t pin,
					    gpio_flags_t flags)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;                         /* 从端口设备取实际驱动操作表。 */
	const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;                   /* 从只读配置首成员取得设备支持的引脚掩码。 */
	struct gpio_driver_data *data =
		(struct gpio_driver_data *)port->data;                             /* 取得可写极性状态，以便保存本次配置。 */

	/* 检查方向、电气属性和初值的互斥约束。 */
	__ASSERT((flags & GPIO_INT_MASK) == 0,
		 "Interrupt flags are not supported");                             /* 检查引脚属性中没有混入独立中断配置标志。 */

	__ASSERT((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) !=
		 (GPIO_PULL_UP | GPIO_PULL_DOWN),
		 "Pull Up and Pull Down should not be enabled simultaneously");    /* 内部上拉和下拉不得同时请求。 */

	__ASSERT((flags & GPIO_OUTPUT) != 0 || (flags & GPIO_SINGLE_ENDED) == 0,
		 "Output needs to be enabled for 'Open Drain', 'Open Source' "
		 "mode to be supported");                                          /* 单端输出需要先请求输出方向。 */

	__ASSERT_NO_MSG((flags & GPIO_SINGLE_ENDED) != 0 ||
			(flags & GPIO_LINE_OPEN_DRAIN) == 0);                      /* 漏极开路必须同时包含单端输出标志。 */

	__ASSERT((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) == 0
		 || (flags & GPIO_OUTPUT) != 0,
		 "Output needs to be enabled to be initialized low or high");      /* 只有启用输出时才能预置高或低初值。 */

	__ASSERT((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH))
		 != (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH),
		 "Output cannot be initialized low and high");                     /* 初始输出不能同时指定高、低两种物理电平。 */

	/* 将逻辑初值转换成驱动可以直接施加的物理初值。 */
	if (((flags & GPIO_OUTPUT_INIT_LOGICAL) != 0)
	    && ((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) != 0)
	    && ((flags & GPIO_ACTIVE_LOW) != 0)) {                                 /* 只有给出逻辑初值且低有效时才交换高低含义。 */
		flags ^= GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH;             /* 翻转两个初值位，把有效或无效转换到物理电平。 */
	}

	flags &= ~GPIO_OUTPUT_INIT_LOGICAL;                                        /* 去掉仅供包装层解释的逻辑初值标志。 */

	(void)cfg;                                                                 /* 即使构建关闭断言也保留对校验配置变量的引用。 */
	__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
		 "Unsupported pin");                                               /* 断言该引脚由当前设备支持。 */

	/* 存储新极性，使后续逻辑读写和中断转换使用同一掩码。 */
	if ((flags & GPIO_ACTIVE_LOW) != 0) {                                      /* 根据本次电气配置更新软件反相位。 */
		data->invert |= (gpio_port_pins_t)BIT(pin);                        /* 低有效引脚后续逻辑读写需要反相。 */
	} else {
		data->invert &= ~(gpio_port_pins_t)BIT(pin);                       /* 取消该引脚先前的低有效标志。 */
	}

	return api->pin_configure(port, pin, flags);                               /* 硬件配置状态直接返回，失败时上面的 invert 已更新。 */
}


/**
  * @brief  合并设备树和运行时标志并配置单个 GPIO。
  * @param  spec：[输入]非空的引脚描述，内部 port 已就绪，pin 为端口内有效编号。
  * @param  extra_flags：[输入]运行时附加的方向、输出初值或其他引脚配置。
  * @retval 0 表示成功，-ENOTSUP 表示配置不支持，-EINVAL 参数无效，-EIO 为访问错误，-EWOULDBLOCK 表示当前上下文无法等待。具体错误由端口驱动提供。
  * @note   按位或合并，调用者应避免同时指定矛盾的电气属性。
  */
static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *spec,
					gpio_flags_t extra_flags)
{
	return gpio_pin_configure(spec->port,
				  spec->pin,
				  spec->dt_flags | extra_flags);    /* 将设备树电气属性与调用者附加选项按位合并。 */
}


/**
  * @brief  读取整个端口的物理输入位图。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  value：[输出]非空、可写的一个 gpio_port_value_t，每位代表一个引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   端口值的位 n 对应端口内引脚 n。此操作忽略 GPIO_ACTIVE_LOW。
  */
__syscall int gpio_port_get_raw(const struct device *port,
				gpio_port_value_t *value);

/**
  * @brief  读取整个端口的物理输入位图。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  value：[输出]非空、可写的一个 gpio_port_value_t，每位代表一个引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   端口值的位 n 对应端口内引脚 n。此操作忽略 GPIO_ACTIVE_LOW。
  */
static inline int z_impl_gpio_port_get_raw(const struct device *port,
					   gpio_port_value_t *value)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;    /* 从端口设备取实际驱动操作表。 */

	return api->port_get_raw(port, value);                /* 通过控制器函数表读取端口输入寄存器。 */
}


/**
  * @brief  按每脚有效极性读取整个端口的逻辑位图。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  value：[输出]非空、可写的一个 gpio_port_value_t，每位代表一个引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   端口值的位 n 对应端口内引脚 n。成功时低有效引脚的采样位会取反。
  */
static inline int gpio_port_get(const struct device *port,
				gpio_port_value_t *value)
{
	const struct gpio_driver_data *const data =
			(const struct gpio_driver_data *)port->data;    /* 读取该端口保存的低有效引脚位图。 */
	int ret;                                                        /* 保存物理读操作状态，失败时不读取输出变量。 */

	/* 取得整组物理电平后，仅在读取成功时按软件极性反转。 */
	ret = gpio_port_get_raw(port, value);                           /* 先读出未反相的整组物理电平。 */
	if (ret == 0) {                                                 /* 读取成功时输出指针内才有可转换的采样。 */
		*value ^= data->invert;                                 /* 按该端口低有效位图将物理电平转换为逻辑状态。 */
	}

	return ret;                                                     /* 把驱动实际读状态原样返回。 */
}


/**
  * @brief  以掩码更新一组引脚的物理电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  mask：[输入]待写入的端口引脚位图，未选中的位保持原值。
  * @param  value：[输入]待输出的物理电平位图，1 为高。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   不进行低有效换算。
  */
__syscall int gpio_port_set_masked_raw(const struct device *port,
				       gpio_port_pins_t mask,
				       gpio_port_value_t value);

/**
  * @brief  以掩码更新一组引脚的物理电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  mask：[输入]待写入的端口引脚位图，未选中的位保持原值。
  * @param  value：[输入]待输出的物理电平位图，1 为高。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   不进行低有效换算。
  */
static inline int z_impl_gpio_port_set_masked_raw(const struct device *port,
						  gpio_port_pins_t mask,
						  gpio_port_value_t value)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;     /* 从端口设备取实际驱动操作表。 */

	return api->port_set_masked_raw(port, mask, value);    /* 调用控制器的按掩码写，直接传物理电平。 */
}


/**
  * @brief  以掩码更新一组引脚的逻辑有效状态。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  mask：[输入]待写入的端口引脚位图，未选中的位保持原值。
  * @param  value：[输入]待输出的逻辑位图，1 表示有效。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   先用端口 invert 换算电平，再对 mask 选中位执行写入。
  */
static inline int gpio_port_set_masked(const struct device *port,
				       gpio_port_pins_t mask,
				       gpio_port_value_t value)
{
	const struct gpio_driver_data *const data =
			(const struct gpio_driver_data *)port->data;    /* 读取该端口保存的低有效引脚位图。 */

	value ^= data->invert;                                          /* 只在低有效位改变逻辑值对应的物理电平。 */

	return gpio_port_set_masked_raw(port, mask, value);             /* 用原掩码选择需要修改的输出位。 */
}


/**
  * @brief  将所选输出引脚置为物理高电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   低有效设置不影响写入的物理值。
  */
__syscall int gpio_port_set_bits_raw(const struct device *port,
				     gpio_port_pins_t pins);

/**
  * @brief  将所选输出引脚置为物理高电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   低有效设置不影响写入的物理值。
  */
static inline int z_impl_gpio_port_set_bits_raw(const struct device *port,
						gpio_port_pins_t pins)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;    /* 从端口设备取实际驱动操作表。 */

	return api->port_set_bits_raw(port, pins);            /* 由端口控制器将所选物理位全部置高。 */
}


/**
  * @brief  令选中引脚进入逻辑有效状态。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   低有效引脚输出低电平。
  */
static inline int gpio_port_set_bits(const struct device *port,
				     gpio_port_pins_t pins)
{
	return gpio_port_set_masked(port, pins, pins);    /* 用相同位图作掩码和值，把选中位逻辑置为有效。 */
}


/**
  * @brief  将所选输出引脚置为物理低电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   低有效设置不影响写入的物理值。
  */
__syscall int gpio_port_clear_bits_raw(const struct device *port,
				       gpio_port_pins_t pins);

/**
  * @brief  将所选输出引脚置为物理低电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   低有效设置不影响写入的物理值。
  */
static inline int z_impl_gpio_port_clear_bits_raw(const struct device *port,
						  gpio_port_pins_t pins)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;    /* 从端口设备取实际驱动操作表。 */

	return api->port_clear_bits_raw(port, pins);          /* 由控制器将指定物理位置低。 */
}


/**
  * @brief  令选中引脚进入逻辑无效状态。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   低有效引脚输出高电平。
  */
static inline int gpio_port_clear_bits(const struct device *port,
				       gpio_port_pins_t pins)
{
	return gpio_port_set_masked(port, pins, 0);    /* 选中位逻辑目标值全部取零。 */
}


/**
  * @brief  翻转所选输出引脚的当前状态。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   翻转对高有效和低有效引脚都改变逻辑状态。
  */
__syscall int gpio_port_toggle_bits(const struct device *port,
				    gpio_port_pins_t pins);

/**
  * @brief  翻转所选输出引脚的当前状态。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pins：[输入]待操作输出引脚的端口位图，位 1 选择对应引脚。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   翻转对高有效和低有效引脚都改变逻辑状态。
  */
static inline int z_impl_gpio_port_toggle_bits(const struct device *port,
					       gpio_port_pins_t pins)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;    /* 从端口设备取实际驱动操作表。 */

	return api->port_toggle_bits(port, pins);             /* 经控制器按位执行翻转。 */
}


/**
  * @brief  用一次带掩码的端口更新同时拉高和拉低两组引脚。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  set_pins：[输入]需置高的位图。
  * @param  clear_pins：[输入]需置低的位图。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   两组位图必须互不相交。
  */
static inline int gpio_port_set_clr_bits_raw(const struct device *port,
					     gpio_port_pins_t set_pins,
					     gpio_port_pins_t clear_pins)
{
	__ASSERT((set_pins & clear_pins) == 0, "Set and Clear pins overlap");      /* 同一个引脚不能既要求置高又要求置低。 */

	return gpio_port_set_masked_raw(port, set_pins | clear_pins, set_pins);    /* 合集作为更新范围，置高位图作为物理值。 */
}


/**
  * @brief  用一次端口更新同时设定两组引脚的逻辑状态。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  set_pins：[输入]需置为有效的位图。
  * @param  clear_pins：[输入]需置为无效的位图。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   两组位图必须互不相交，具体物理电平按每脚极性计算。
  */
static inline int gpio_port_set_clr_bits(const struct device *port,
					 gpio_port_pins_t set_pins,
					 gpio_port_pins_t clear_pins)
{
	__ASSERT((set_pins & clear_pins) == 0, "Set and Clear pins overlap");    /* 同一脚不能同时成为逻辑有效和逻辑无效。 */

	return gpio_port_set_masked(port, set_pins | clear_pins, set_pins);      /* 用集合并集作为掩码，逻辑置位集合提供有效值。 */
}


/**
  * @brief  读取一个输入引脚的物理电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @retval 0 为低，1 为高；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   从整端口原始采样中提取一位；READY 轮询借此直接观察 GPIO61。
  */
static inline int gpio_pin_get_raw(const struct device *port, gpio_pin_t pin)
{
	const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;            /* 从只读配置首成员取得设备支持的引脚掩码。 */
	gpio_port_value_t value;                                            /* 暂存整端口物理采样，用于提取 pin 位。 */
	int ret;                                                            /* 保存读状态，成功后改存 0 或 1。 */

	(void)cfg;                                                          /* 即使构建关闭断言也保留对校验配置变量的引用。 */
	__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
		 "Unsupported pin");                                        /* 端口有效掩码必须含目标引脚。 */

	/* 只在端口原始采样成功后提取单脚物理值，错误码直接传回。 */
	ret = gpio_port_get_raw(port, &value);                              /* 整端口物理读成功时再解码 pin 位。 */
	if (ret == 0) {                                                     /* 读失败时保持底层错误，不检查临时值。 */
		ret = (value & (gpio_port_pins_t)BIT(pin)) != 0 ? 1 : 0;    /* 提取组内 pin 位并归一到 0 或 1。 */
	}

	return ret;                                                         /* 返回该脚物理电平或原始读取错误。 */
}


/**
  * @brief  按已配置有效极性读取一个引脚。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @retval 0 为无效，1 为有效；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   低有效引脚的低电平会返回 1。
  */
static inline int gpio_pin_get(const struct device *port, gpio_pin_t pin)
{
	const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;            /* 从只读配置首成员取得设备支持的引脚掩码。 */
	gpio_port_value_t value;                                            /* 暂存已完成极性换算的端口位图。 */
	int ret;                                                            /* 保存状态或最终单脚逻辑值。 */

	(void)cfg;                                                          /* 即使构建关闭断言也保留对校验配置变量的引用。 */
	__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
		 "Unsupported pin");                                        /* 检查 pin 所指的是本实例支持的输入位。 */

	/* 先取得已换算极性的端口位图，再取单脚有效状态。 */
	ret = gpio_port_get(port, &value);                                  /* 取得考虑低有效映射的端口值。 */
	if (ret == 0) {                                                     /* 仅成功时从暂存的位图取值。 */
		ret = (value & (gpio_port_pins_t)BIT(pin)) != 0 ? 1 : 0;    /* 从映射后的目标位得到逻辑有效状态。 */
	}

	return ret;                                                         /* 返回逻辑 0、1 或驱动错误码。 */
}


/**
  * @brief  读取设备树描述的引脚逻辑值。
  * @param  spec：[输入]非空的引脚描述，内部 port 已就绪，pin 为端口内有效编号。
  * @retval 0 为无效，1 为有效；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   使用端口数据中由 gpio_pin_configure 记录的极性。
  */
static inline int gpio_pin_get_dt(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get(spec->port, spec->pin);    /* 以设备树的端口及局部编号调用逻辑读接口。 */
}


/**
  * @brief  向一个输出引脚写入物理电平。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @param  value：[输入]零为低电平，非零为高电平。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   通过端口置位或清零操作，只改该引脚。
  */
static inline int gpio_pin_set_raw(const struct device *port, gpio_pin_t pin,
				   int value)
{
	const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;                     /* 从只读配置首成员取得设备支持的引脚掩码。 */
	int ret;                                                                     /* 保存置位或清零驱动返回值。 */

	(void)cfg;                                                                   /* 即使构建关闭断言也保留对校验配置变量的引用。 */
	__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
		 "Unsupported pin");                                                 /* 所选索引必须在该端口有效引脚集合内。 */

	/* 把调用值归一为高或低，再通过端口置位或清零入口写单脚。 */
	if (value != 0)	{                                                            /* 任意非零输入都按物理高电平处理。 */
		ret = gpio_port_set_bits_raw(port, (gpio_port_pins_t)BIT(pin));      /* 单比特置位，使其他引脚保持原状态。 */
	} else {
		ret = gpio_port_clear_bits_raw(port, (gpio_port_pins_t)BIT(pin));    /* 清除端口中对应的一位。 */
	}

	return ret;                                                                  /* 返回输出操作结果。 */
}


/**
  * @brief  设置一个输出引脚的逻辑有效状态。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @param  value：[输入]零为无效，非零为有效。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   先根据软件记录的 GPIO_ACTIVE_LOW 决定实际物理电平。
  */
static inline int gpio_pin_set(const struct device *port, gpio_pin_t pin,
			       int value)
{
	const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;        /* 从只读配置首成员取得设备支持的引脚掩码。 */
	const struct gpio_driver_data *const data =
			(const struct gpio_driver_data *)port->data;    /* 读取该端口保存的低有效引脚位图。 */

	(void)cfg;                                                      /* 即使构建关闭断言也保留对校验配置变量的引用。 */
	__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
		 "Unsupported pin");                                    /* 断言所选引脚可在该端口使用。 */

	/* 先把逻辑有效状态换成物理电平，再走原始引脚写接口。 */
	if (data->invert & (gpio_port_pins_t)BIT(pin)) {                /* 仅低有效引脚需翻转请求的逻辑值。 */
		value = (value != 0) ? 0 : 1;                           /* 把逻辑有效映射到物理低，逻辑无效映射到物理高。 */
	}

	return gpio_pin_set_raw(port, pin, value);                      /* 写入已经完成极性换算的电平。 */
}


/**
  * @brief  设置设备树指定引脚的逻辑输出。
  * @param  spec：[输入]非空的引脚描述，内部 port 已就绪，pin 为端口内有效编号。
  * @param  value：[输入]零为无效，非零为有效。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   极性由已经生效的引脚配置决定。
  */
static inline int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
	return gpio_pin_set(spec->port, spec->pin, value);    /* 从引脚描述取得端口与编号，交给逻辑写接口。 */
}


/**
  * @brief  反转一个已配置输出的引脚。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  pin：[输入]端口内的有效编号，必须属于该设备的 port_pin_mask。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   按 pin 生成仅含一位的掩码，然后调用端口翻转操作。
  */
static inline int gpio_pin_toggle(const struct device *port, gpio_pin_t pin)
{
	const struct gpio_driver_config *const cfg =
		(const struct gpio_driver_config *)port->config;           /* 从只读配置首成员取得设备支持的引脚掩码。 */

	(void)cfg;                                                         /* 即使构建关闭断言也保留对校验配置变量的引用。 */
	__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
		 "Unsupported pin");                                       /* 校验翻转掩码落在支持的引脚内。 */

	return gpio_port_toggle_bits(port, (gpio_port_pins_t)BIT(pin));    /* 将单脚编号展开为一位掩码后调用端口翻转。 */
}


/**
  * @brief  翻转设备树描述中的单个输出。
  * @param  spec：[输入]非空的引脚描述，内部 port 已就绪，pin 为端口内有效编号。
  * @retval 0 表示成功；端口访问失败可返回 -EIO，当前上下文无法等待可返回 -EWOULDBLOCK。
  * @note   将端口和有效组内编号交给单引脚操作。
  */
static inline int gpio_pin_toggle_dt(const struct gpio_dt_spec *spec)
{
	return gpio_pin_toggle(spec->port, spec->pin);    /* 从设备树描述取端口与编号并执行单脚翻转。 */
}


/**
  * @brief  准备可登记到驱动的回调对象。
  * @param  callback：[输出]非空、可写的持久对象，生命周期覆盖登记期间。
  * @param  handler：[输入]非空函数指针，在所选引脚有中断时由驱动调用。
  * @param  pin_mask：[输入]感兴趣的端口内引脚位图，零表示不接收事件。
  * @retval 无。
  * @note   仅写入处理函数和订阅掩码，仍需登记对象、配置并打开中断。
  */
static inline void gpio_init_callback(struct gpio_callback *callback,
				      gpio_callback_handler_t handler,
				      gpio_port_pins_t pin_mask)
{
	__ASSERT(callback, "Callback pointer should not be NULL");           /* 必须提供足够生命周期的可写回调对象。 */
	__ASSERT(handler, "Callback handler pointer should not be NULL");    /* 驱动中断分发时需要有效处理函数地址。 */

	/* 保存处理函数和选择掩码，链表节点由后续登记动作维护。 */
	callback->handler = handler;                                         /* 保存中断事件到来时需要调用的入口。 */
	callback->pin_mask = pin_mask;                                       /* 只对位图中选中引脚的事件感兴趣。 */
}


/**
  * @brief  把已初始化的回调登记到目标端口。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  callback：[输入/输出]非空且已初始化的回调对象，在登记期间持续有效。
  * @retval 0 表示成功，-ENOTSUP 表示无回调管理接口，其他负值由驱动提供。
  * @note   同端口可登记多个不同对象。在现有回调中新增订阅时，是否收到正在处理的事件由实现决定。
  */
static inline int gpio_add_callback(const struct device *port,
				    struct gpio_callback *callback)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;    /* 从端口设备取实际驱动操作表。 */

	/* 检查该端口是否提供回调管理，再向驱动传递登记或撤销请求。 */
	if (api->manage_callback == NULL) {                   /* 先检查驱动是否提供可选订阅管理接口。 */
		return -ENOTSUP;                              /* 驱动未提供可用回调登记能力。 */
	}

	return api->manage_callback(port, callback, true);    /* 以 true 请求将该对象挂入本端口订阅链。 */
}


/**
  * @brief  撤销端口上一个回调对象的订阅。
  * @param  port：[输入]非空、已就绪的 GPIO 端口设备。
  * @param  callback：[输入/输出]非空且已初始化的回调对象，在登记期间持续有效。
  * @retval 0 表示完成，-ENOTSUP 为未实现回调管理；Actions 还可能以 -EINVAL 表示在非空链表中找不到订阅。
  * @note   回调内允许移除自身；在遍历期间移除同端口其他订阅可能导致漏调或触发已经移除的回调。
  */
static inline int gpio_remove_callback(const struct device *port,
				       struct gpio_callback *callback)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)port->api;     /* 从端口设备取实际驱动操作表。 */

	/* 检查该端口是否提供回调管理，再向驱动传递登记或撤销请求。 */
	if (api->manage_callback == NULL) {                    /* 检查端口是否能够管理回调订阅。 */
		return -ENOTSUP;                               /* 缺少回调管理时无法撤销订阅。 */
	}

	return api->manage_callback(port, callback, false);    /* 以 false 要求移除对应对象。 */
}


/**
  * @brief  读取 GPIO 端口中断待决状态。
  * @param  dev：[输入]非空、已就绪的 GPIO 端口。
  * @retval 无待决位返回 0，有待决位返回设备状态值，未提供此入口返回 -ENOTSUP。
  * @note   可供唤醒后定位中断来源。当前 Actions 设备表未注册 get_pending_int。
  */
__syscall int gpio_get_pending_int(const struct device *dev);

/**
  * @brief  读取 GPIO 端口中断待决状态。
  * @param  dev：[输入]非空、已就绪的 GPIO 端口。
  * @retval 无待决位返回 0，有待决位返回设备状态值，未提供此入口返回 -ENOTSUP。
  * @note   可供唤醒后定位中断来源。当前 Actions 设备表未注册 get_pending_int。
  */
static inline int z_impl_gpio_get_pending_int(const struct device *dev)
{
	const struct gpio_driver_api *api =
		(const struct gpio_driver_api *)dev->api;    /* 从端口设备取实际驱动操作表。 */

	/* 先检查可选入口，支持查询的设备才通过驱动读取待决状态。 */
	if (api->get_pending_int == NULL) {                  /* 检查驱动是否公开待决状态读取能力。 */
		return -ENOTSUP;                             /* 当前 Actions GPIO 走此分支。 */
	}

	return api->get_pending_int(dev);                    /* 取得已实现该操作的端口报告的待决位图。 */
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#include <syscalls/gpio.h>    /* 包含由当前构建生成的 GPIO 系统调用包装。 */

#endif    /* ZEPHYR_INCLUDE_DRIVERS_GPIO_H_ */
