/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Zephyr 应用使用的 SPI 公共接口；与 bootloader 同名头文件分属不同构建目标。包含保护防止重复定义。 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_SPI_H_
#define ZEPHYR_INCLUDE_DRIVERS_SPI_H_

/* 整数类型由 zephyr/types.h 提供；device.h 提供设备对象，gpio.h 提供软件片选所需的 GPIO 类型和操作。 */
#include <zephyr/types.h>
#include <stddef.h>
#include <device.h>
#include <drivers/gpio.h>

#ifdef __cplusplus
/* C++ 调用者使用 C 链接规则；仅 C 可用的设备树复合字面量宏另有条件保护。 */
extern "C" {
#endif

/* 主机角色编码为 0，由控制器产生串行时钟。 */
#define SPI_OP_MODE_MASTER	0
/* operation 位 0 选择从机角色，时钟由外部主机提供。 */
#define SPI_OP_MODE_SLAVE	BIT(0)
/* 主从角色字段掩码。 */
#define SPI_OP_MODE_MASK	0x1
/* 从 operation 中提取角色编码。 */
#define SPI_OP_MODE_GET(_operation_) ((_operation_) & SPI_OP_MODE_MASK)

/* 时钟极性：置位时空闲电平为高，否则为空闲低。 */
#define SPI_MODE_CPOL		BIT(1)

/* 采样相位：置位在第二个边沿采样，否则在第一个边沿采样；CPOL 决定边沿方向。 */
#define SPI_MODE_CPHA		BIT(2)

/* 请求控制器内部回环，仅供支持该模式的控制器自测使用。 */
#define SPI_MODE_LOOP		BIT(3)

/* 极性、相位和回环三个位的掩码。 */
#define SPI_MODE_MASK		(0xE)
/* 提取 operation 中的极性、相位和回环字段。 */
#define SPI_MODE_GET(_mode_)			\
	((_mode_) & SPI_MODE_MASK)

/* 每个数据字的最高有效位先传输。 */
#define SPI_TRANSFER_MSB	(0)
/* 请求最低有效位先传输，是否支持取决于驱动。 */
#define SPI_TRANSFER_LSB	BIT(4)

/* 数据字位宽字段从 operation 位 5 开始。 */
#define SPI_WORD_SIZE_SHIFT	(5)
/* 位 5～10 保存每个数据字的位数；不表示字节数。 */
#define SPI_WORD_SIZE_MASK	(0x3F << SPI_WORD_SIZE_SHIFT)
/* 提取数据字位宽；当前链路配置为 8 位。 */
#define SPI_WORD_SIZE_GET(_operation_)					\
	(((_operation_) & SPI_WORD_SIZE_MASK) >> SPI_WORD_SIZE_SHIFT)

/* 把位宽编码到 operation；调用者提供驱动支持的位数。 */
#define SPI_WORD_SET(_word_size_)		\
	((_word_size_) << SPI_WORD_SIZE_SHIFT)

/* 单数据线模式。 */
#define SPI_LINES_SINGLE	(0 << 11)
/* 双数据线模式，需控制器支持。 */
#define SPI_LINES_DUAL		(1 << 11)
/* 四数据线模式，需控制器支持。 */
#define SPI_LINES_QUAD		(2 << 11)
/* 八数据线模式，需控制器支持。 */
#define SPI_LINES_OCTAL		(3 << 11)

/* operation 位 11～12 的数据线模式掩码。 */
#define SPI_LINES_MASK		(0x3 << 11)

/* 请求传输结束后保持片选有效；驱动和硬件必须支持。 */
#define SPI_HOLD_ON_CS		BIT(13)

/* 请求传输后继续持有配置对应的总线锁；必须配对 spi_release，否则其他调用者可能永久等待。 */
#define SPI_LOCK_ON		BIT(14)

/* 请求高电平有效片选，默认低有效；硬件不支持时可用 GPIO 软件片选。 */
#define SPI_CS_ACTIVE_HIGH	BIT(15)

/* GPIO 软件片选描述；gpio_dev 为空可抑制上下文层的 GPIO 片选操作。gpio_dt_flags 极性应与 operation 的片选极性一致。 */
struct spi_cs_control {
	const struct device	*gpio_dev;        /* GPIO 控制器对象，软件片选使用；允许为空以禁用该操作。 */
	uint32_t		delay;            /* 片选拉有效后及释放前的等待时间，单位为微秒。 */
	gpio_pin_t		gpio_pin;         /* GPIO 控制器内部的片选号。 */
	gpio_dt_flags_t		gpio_dt_flags;    /* 设备树提供的 GPIO 属性，包括有效电平。 */
};

#ifndef __cplusplus

/* 仅 C：从 SPI 从设备 node_id 的 cs-gpios 构造片选对象地址；delay_ 单位为微秒。控制器由 DT_SPI_DEV_CS_GPIOS_CTLR 取得，引脚及有效电平由对应属性取得。对象生命周期遵循复合字面量所在作用域。 */
/* 原用法示例（节点GPIO配置取决于设备树；delay单位微秒）：
 *     struct spi_cs_control *ctrl =
 *             SPI_CS_CONTROL_PTR_DT(DT_NODELABEL(spidev), 2);
 * 原示例的等价展开：
 *     struct spi_cs_control *ctrl =
 *             &(struct spi_cs_control) {
 *                     .gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
 *                     .delay = 2,
 *                     .gpio_pin = 1,
 *                     .gpio_dt_flags = GPIO_ACTIVE_LOW
 *             };
 */
#define SPI_CS_CONTROL_PTR_DT(node_id, delay_)				\
	(&(struct spi_cs_control) {						\
		.gpio_dev = DEVICE_DT_GET(				\
			DT_SPI_DEV_CS_GPIOS_CTLR(node_id)),		\
		.delay = (delay_),					\
		.gpio_pin = DT_SPI_DEV_CS_GPIOS_PIN(node_id),		\
		.gpio_dt_flags = DT_SPI_DEV_CS_GPIOS_FLAGS(node_id),	\
	})

/* 仅 C：将实例 inst 换算为 DT_DRV_INST(inst) 后构造片选对象；须先定义 DT_DRV_COMPAT，delay_ 单位为微秒。 */
#define SPI_CS_CONTROL_PTR_DT_INST(inst, delay_)		\
	SPI_CS_CONTROL_PTR_DT(DT_DRV_INST(inst), delay_)
#endif

/* 总线参数。驱动通常通过配置对象地址判断是否重配，原地修改字段可能不被识别；连续调用间不应任意改动配置，仅按接口约定使用保持片选/持锁选项。当前 SPI2 从机专用路径会复位并清除此地址缓存。 */
struct spi_config {
	uint32_t		frequency;    /* 请求的串行时钟频率，单位 Hz；从机不能据此产生外部时钟。 */
	uint16_t		operation;    /* 主从、模式、位序、位宽、数据线和片选策略的组合位域。 */
	uint16_t		slave;        /* 从设备片选索引，必须在控制器支持范围内。 */

	const struct spi_cs_control *cs;    /* 可选 GPIO 片选描述；为空时不请求该软件片选。 */
};

#ifndef __cplusplus

/* 仅 C：从从设备 node_id 生成静态初始化项；读取 spi_max_frequency、reg 及可选 cs-gpios，operation_ 由调用者指定，delay_ 为片选延时微秒数。使用前须检查 GPIO 设备就绪状态。 */
#define SPI_CONFIG_DT(node_id, operation_, delay_)			\
	{								\
		.frequency = DT_PROP(node_id, spi_max_frequency),	\
		.operation = (operation_),				\
		.slave = DT_REG_ADDR(node_id),				\
		.cs = COND_CODE_1(					\
			DT_SPI_DEV_HAS_CS_GPIOS(node_id),		\
			(SPI_CS_CONTROL_PTR_DT(node_id, delay_)),	\
			(NULL)),					\
	}

/* 仅 C：以 DT_DRV_INST(inst) 调用 SPI_CONFIG_DT，operation_ 为操作位域，delay_ 单位为微秒。 */
#define SPI_CONFIG_DT_INST(inst, operation_, delay_)	\
	SPI_CONFIG_DT(DT_DRV_INST(inst), operation_, delay_)
#endif

/* 设备树形式的访问描述，同时保存控制器设备和该从设备的总线参数。 */
struct spi_dt_spec {
	const struct device *bus;    /* SPI 控制器设备；使用前检查就绪。 */
	struct spi_config config;    /* 访问该从设备时使用的配置对象。 */
};

#ifndef __cplusplus

/* 仅 C：将 node_id 对应的控制器和从设备配置一起构造成初始化项；operation_ 为操作位域，delay_ 单位为微秒。使用前用 spi_is_ready 检查总线和可选片选端口。 */
#define SPI_DT_SPEC_GET(node_id, operation_, delay_)		     \
	{							     \
		.bus = DEVICE_DT_GET(DT_BUS(node_id)),		     \
		.config = SPI_CONFIG_DT(node_id, operation_, delay_) \
	}

/* 仅 C：将实例 inst 转为设备树节点后生成总线及配置初始化项，其他参数与 SPI_DT_SPEC_GET 相同。 */
#define SPI_DT_SPEC_INST_GET(inst, operation_, delay_) \
	SPI_DT_SPEC_GET(DT_DRV_INST(inst), operation_, delay_)
#endif

/* 单块缓冲描述；公共 API 允许 buf 为空以表示发送占位或丢弃接收数据，len 仍是字节数。具体驱动可能另有限制，SPI2 CPU 从机要求双向有效且等长。 */
struct spi_buf {
	void *buf;     /* 发送时只读、接收时可写的数据区；空指针语义见结构体说明。 */
	size_t len;    /* 数据区长度，单位为字节。 */
};

/* 分散缓冲集合。count 为 spi_buf 项数；集合为空表示该方向没有数据，非空集合的数组与数据必须保持到传输完成。 */
struct spi_buf_set {
	const struct spi_buf *buffers;    /* 缓冲描述数组，实际数据的生命周期由调用者维护。 */
	size_t count;                     /* 数组中的缓冲描述项数。 */
};

/* 驱动同步收发入口类型：由公共 API 传入设备、只读配置、可选发送/接收集合；参数语义同 spi_transceive。返回驱动状态，具体成功计数语义由驱动决定。 */
typedef int (*spi_api_io)(const struct device *dev,
			  const struct spi_config *config,
			  const struct spi_buf_set *tx_bufs,
			  const struct spi_buf_set *rx_bufs);

/* 驱动异步入口类型：参数同同步入口，async 是可选完成信号。调用者须保持配置、信号及缓冲有效到完成；仅启用异步支持时才在 API 表中绑定。 */
typedef int (*spi_api_io_async)(const struct device *dev,
				const struct spi_config *config,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs,
				struct k_poll_signal *async);

/* 驱动释放入口类型：dev 为设备，config 为要释放的配置；公共约定返回 0 或负错误码。当前 Actions 实现忽略 config 并强制解锁。 */
typedef int (*spi_api_release)(const struct device *dev,
			       const struct spi_config *config);

/* 设备对象的 api 指向这张函数表；SPI_2 在 spi_acts.c 中将同步入口绑定到 spi_acts_transceive。 */
__subsystem struct spi_driver_api {
	spi_api_io transceive;                /* 同步收发函数指针。 */
#ifdef CONFIG_SPI_ASYNC
	spi_api_io_async transceive_async;    /* 仅 CONFIG_SPI_ASYNC 构建包含的异步入口。 */
#endif                                        /* CONFIG_SPI_ASYNC 结构成员分支结束。 */
	spi_api_release release;              /* 释放片选或总线锁的驱动入口。 */
};

/**
  * @brief  检查控制器及配置中可选 GPIO 片选端口是否就绪。
  * @param  spec：[输入]有效的总线及配置描述，不可为空。
  * @retval true：所检查设备均就绪；false：总线或可选 GPIO 端口未就绪。
  * @note   只检查设备初始化状态，不探测 SPI 远端是否连接。
  */
static inline bool spi_is_ready(const struct spi_dt_spec *spec)
{

	/* 先验证 SPI 控制器的设备初始化结果。 */
	if (!device_is_ready(spec->bus)) {
		return false;    /* 存在未就绪设备，不能据此描述开始通信。 */
	}

	/* 配置了软件片选时，还必须验证其 GPIO 控制器就绪。 */
	if (spec->config.cs &&
	    !device_is_ready(spec->config.cs->gpio_dev)) {
		return false;    /* 存在未就绪设备，不能据此描述开始通信。 */
	}
	return true;             /* 控制器与所需片选设备均已就绪。 */
}

/**
  * @brief  同步提交 SPI 收发请求并等待驱动完成。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @param  tx_bufs：[输入]发送缓冲集合，可为空；非空时数组及有效数据区须可读并保持到完成。
  * @param  rx_bufs：[输入/输出]接收缓冲集合，可为空；非空时数组可读、有效数据区须可写并保持到完成。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   同步等待可能阻塞，不应在不能等待的中断上下文调用；从机依赖外部时钟。
  */
__syscall int spi_transceive(const struct device *dev,
			     const struct spi_config *config,
			     const struct spi_buf_set *tx_bufs,
			     const struct spi_buf_set *rx_bufs);

/**
  * @brief  从设备 API 表分派到实际同步收发驱动。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @param  tx_bufs：[输入]发送缓冲集合，可为空；非空时数组及有效数据区须可读并保持到完成。
  * @param  rx_bufs：[输入/输出]接收缓冲集合，可为空；非空时数组可读、有效数据区须可写并保持到完成。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   不复制数据，不额外验证空设备或驱动函数指针；本函数是系统调用实现入口。
  */
static inline int z_impl_spi_transceive(const struct device *dev,
					const struct spi_config *config,
					const struct spi_buf_set *tx_bufs,
					const struct spi_buf_set *rx_bufs)
{
	const struct spi_driver_api *api =                        /* 从设备对象取得 SPI 驱动函数表。 */
		(const struct spi_driver_api *)dev->api;

	return api->transceive(dev, config, tx_bufs, rx_bufs);    /* 调用设备注册时绑定的同步收发入口并原样返回状态。 */
}

/**
  * @brief  取出设备树描述中的总线和配置，执行同步收发。
  * @param  spec：[输入]有效的总线及配置描述，不可为空。
  * @param  tx_bufs：[输入]发送缓冲集合，可为空；非空时数组及有效数据区须可读并保持到完成。
  * @param  rx_bufs：[输入/输出]接收缓冲集合，可为空；非空时数组可读、有效数据区须可写并保持到完成。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   调用前先检查 spi_is_ready；其他限制与 spi_transceive 相同。
  */
static inline int spi_transceive_dt(const struct spi_dt_spec *spec,
				    const struct spi_buf_set *tx_bufs,
				    const struct spi_buf_set *rx_bufs)
{


	return spi_transceive(spec->bus, &spec->config, tx_bufs, rx_bufs);    /* 复用同步接口，传入描述内的控制器及配置地址。 */
}

/**
  * @brief  以空发送集合执行同步接收。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @param  rx_bufs：[输入/输出]接收缓冲集合，可为空；非空时数组可读、有效数据区须可写并保持到完成。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   仅为 spi_transceive 包装；驱动必须支持单向请求，当前 SPI2 CPU 从机专用路径不接受此形式。
  */
static inline int spi_read(const struct device *dev,
			   const struct spi_config *config,
			   const struct spi_buf_set *rx_bufs)
{


	return spi_transceive(dev, config, NULL, rx_bufs);    /* 不提供发送集合，由驱动执行仅接收请求。 */
}

/**
  * @brief  使用设备树描述执行同步接收。
  * @param  spec：[输入]有效的总线及配置描述，不可为空。
  * @param  rx_bufs：[输入/输出]接收缓冲集合，可为空；非空时数组可读、有效数据区须可写并保持到完成。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   调用前检查设备就绪；其他限制同 spi_read。
  */
static inline int spi_read_dt(const struct spi_dt_spec *spec,
			      const struct spi_buf_set *rx_bufs)
{


	return spi_read(spec->bus, &spec->config, rx_bufs);    /* 将设备树参数展开后复用接收包装。 */
}

/**
  * @brief  以空接收集合执行同步发送。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @param  tx_bufs：[输入]发送缓冲集合，可为空；非空时数组及有效数据区须可读并保持到完成。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   仅为 spi_transceive 包装；驱动必须支持单向请求，当前 SPI2 CPU 从机专用路径不接受此形式。
  */
static inline int spi_write(const struct device *dev,
			    const struct spi_config *config,
			    const struct spi_buf_set *tx_bufs)
{


	return spi_transceive(dev, config, tx_bufs, NULL);    /* 不提供接收集合，由驱动执行仅发送请求。 */
}

/**
  * @brief  使用设备树描述执行同步发送。
  * @param  spec：[输入]有效的总线及配置描述，不可为空。
  * @param  tx_bufs：[输入]发送缓冲集合，可为空；非空时数组及有效数据区须可读并保持到完成。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   调用前检查设备就绪；其他限制同 spi_write。
  */
static inline int spi_write_dt(const struct spi_dt_spec *spec,
			       const struct spi_buf_set *tx_bufs)
{


	return spi_write(spec->bus, &spec->config, tx_bufs);    /* 将设备树参数展开后复用发送包装。 */
}

#ifdef CONFIG_SPI_ASYNC

/**
  * @brief  分派带可选完成信号的异步收发请求。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @param  tx_bufs：[输入]发送缓冲集合，可为空；非空时数组及有效数据区须可读并保持到完成。
  * @param  rx_bufs：[输入/输出]接收缓冲集合，可为空；非空时数组可读、有效数据区须可写并保持到完成。
  * @param  async：[输入/输出]已初始化的完成信号，可为空；为空时不会通过它报告完成结果。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   仅 CONFIG_SPI_ASYNC 构建提供；参数和缓冲须保持到完成。异步接口名不保证所有驱动内部都立即返回，需结合具体实现。
  */
static inline int spi_transceive_async(const struct device *dev,
				       const struct spi_config *config,
				       const struct spi_buf_set *tx_bufs,
				       const struct spi_buf_set *rx_bufs,
				       struct k_poll_signal *async)
{
	const struct spi_driver_api *api =                                     /* 从设备对象取得 SPI 驱动函数表。 */
		(const struct spi_driver_api *)dev->api;

	return api->transceive_async(dev, config, tx_bufs, rx_bufs, async);    /* 调用已绑定的异步入口，完成通知由驱动处理。 */
}

/**
  * @brief  以空发送集合提交异步接收。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @param  rx_bufs：[输入/输出]接收缓冲集合，可为空；非空时数组可读、有效数据区须可写并保持到完成。
  * @param  async：[输入/输出]已初始化的完成信号，可为空；为空时不会通过它报告完成结果。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   仅 CONFIG_SPI_ASYNC 构建提供；完成与生命周期要求同 spi_transceive_async。
  */
static inline int spi_read_async(const struct device *dev,
				 const struct spi_config *config,
				 const struct spi_buf_set *rx_bufs,
				 struct k_poll_signal *async)
{


	return spi_transceive_async(dev, config, NULL, rx_bufs, async);    /* 转交仅接收请求及可选完成信号。 */
}

/**
  * @brief  以空接收集合提交异步发送。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @param  tx_bufs：[输入]发送缓冲集合，可为空；非空时数组及有效数据区须可读并保持到完成。
  * @param  async：[输入/输出]已初始化的完成信号，可为空；为空时不会通过它报告完成结果。
  * @retval 公共约定为主机成功返回 0、从机成功可返回接收数据字数，失败返回负 errno；本 SDK 的 Actions CPU 收发成功返回 0。
  * @note   仅 CONFIG_SPI_ASYNC 构建提供；完成与生命周期要求同 spi_transceive_async。
  */
static inline int spi_write_async(const struct device *dev,
				  const struct spi_config *config,
				  const struct spi_buf_set *tx_bufs,
				  struct k_poll_signal *async)
{


	return spi_transceive_async(dev, config, tx_bufs, NULL, async);    /* 转交仅发送请求及可选完成信号。 */
}
#endif    /* CONFIG_SPI_ASYNC 接口分支结束。 */

/**
  * @brief  释放配置持有的总线资源，并按驱动约定解除保持的片选。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @retval 0：释放成功；失败为驱动返回的负错误码。
  * @note   持锁传输结束后须配对调用；当前 Actions 实现强制解除上下文锁。
  */
__syscall int spi_release(const struct device *dev,
			  const struct spi_config *config);

/**
  * @brief  释放配置持有的总线资源，并按驱动约定解除保持的片选。
  * @param  dev：[输入]已初始化的 SPI 控制器设备，不可为空。
  * @param  config：[输入]有效只读配置，不可为空；传输期间不得修改或销毁。
  * @retval 0：释放成功；失败为驱动返回的负错误码。
  * @note   持锁传输结束后须配对调用；当前 Actions 实现强制解除上下文锁。
  */
static inline int z_impl_spi_release(const struct device *dev,
				     const struct spi_config *config)
{
	const struct spi_driver_api *api =    /* 从设备对象取得 SPI 驱动函数表。 */
		(const struct spi_driver_api *)dev->api;

	return api->release(dev, config);     /* 转交驱动释放入口，返回其实际处理状态。 */
}

/**
  * @brief  释放配置持有的总线资源，并按驱动约定解除保持的片选。
  * @param  spec：[输入]有效的总线及配置描述，不可为空。
  * @retval 0：释放成功；失败为驱动返回的负错误码。
  * @note   持锁传输结束后须配对调用；当前 Actions 实现强制解除上下文锁。
  */
static inline int spi_release_dt(const struct spi_dt_spec *spec)
{


	return spi_release(spec->bus, &spec->config);    /* 释放描述内配置对应的总线资源。 */
}

#ifdef __cplusplus
}
#endif

#include <syscalls/spi.h>    /* 包含构建生成的系统调用包装；生成文件不在本次手工修改范围内。 */

#endif    /* SPI 头文件包含保护结束。 */
