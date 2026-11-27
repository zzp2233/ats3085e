/*
 * Copyright (c) 2018-2019 Nordic Semiconductor ASA
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Zephyr UART 公共接口。调用端通过设备对象的驱动表选择轮询、中断 FIFO 或异步缓冲区操作。
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_UART_H_
#define ZEPHYR_INCLUDE_DRIVERS_UART_H_

/**
 * @brief 串口设备接口
 * @defgroup uart_interface UART 公共接口
 * @ingroup io_interfaces
 * @{
 */

#include <errno.h>     /* 提供 ENOTSUP、ENOSYS 等错误码。 */
#include <stddef.h>    /* 提供 size_t 和 NULL。 */

#include <device.h>    /* 提供设备描述及其 api 指针。 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 线路控制项，供 uart_line_ctrl_set/get 选择速率或握手信号。 */
enum uart_line_ctrl {
	UART_LINE_CTRL_BAUD_RATE = BIT(0),    /* 波特率选择项。 */
	UART_LINE_CTRL_RTS = BIT(1),          /* 请求发送线路。 */
	UART_LINE_CTRL_DTR = BIT(2),          /* 数据终端就绪线路。 */
	UART_LINE_CTRL_DCD = BIT(3),          /* 载波检测线路。 */
	UART_LINE_CTRL_DSR = BIT(4),          /* 数据设备就绪线路。 */
};

/**
 * @brief 异步 API 传给 uart_callback_t 的事件类型。
 * @details 接收先用 uart_rx_enable 交出首块缓冲；收到 UART_RX_BUF_REQUEST 后可用 uart_rx_buf_rsp 交出下一块。当前块填满前未提供下一块时，接收将停止。
 * 至少接收一个字节以后，空闲超时可触发 UART_RX_RDY。同一块可以多次就绪，每次只消费事件标出的 offset 和 len。缓冲填满时先报告可读数据，再报告 UART_RX_BUF_RELEASED。已交出的下一块随即接手。
 * 外部错误可触发 UART_RX_STOPPED。此时以及主动停收时，若有尚未报告的数据会先报告就绪，再逐块通知释放，最后是 UART_RX_DISABLED，之后可以重新启动。
 * 发送由 uart_tx 发起。整块完成报告 UART_TX_DONE；因流控超时或主动中止则报告 UART_TX_ABORTED，从事件读取已发送量。该异步事件体系与串口 Shell 使用的 IRQ FIFO 接口分开。
 */
enum uart_event_type {
	/** @brief 本次发送缓冲区的数据已全部发出。 */
	UART_TX_DONE,
	/** @brief 发送因超时或主动 uart_tx_abort 中止；开启流控时对端可能持续阻止发送。事件的 len 给出已经完成的字节数。 */
	UART_TX_ABORTED,
	/** @brief 有新的数据范围可读；触发时机包括接收空闲超时、缓冲已满、主动停收或外部原因停收。同一缓冲可以收到多次通知。 */
	UART_RX_RDY,
	/** @brief 驱动开始写当前缓冲时索取下一块。调用者可通过 uart_rx_buf_rsp 提供独立缓冲，让接收在当前块填满后继续。 */
	UART_RX_BUF_REQUEST,
	/** @brief 驱动已归还这一个接收缓冲，其所有者可以再次提交或复用。 */
	UART_RX_BUF_RELEASED,
	/** @brief 当前接收已经结束，可用 uart_rx_enable 开始新一轮接收。 */
	UART_RX_DISABLED,
	/** @brief 接收因外部事件停止，rx_stop.reason 给出 uart_rx_stop_reason 中对应的原因。 */
	UART_RX_STOPPED,
};


/** @brief 异步接收停止或错误检查所报告的原因，以位标志表示。 */
enum uart_rx_stop_reason {
	/** @brief 数据未能及时取走而溢出。 */
	UART_ERROR_OVERRUN = (1 << 0),
	/** @brief 收到的字符未通过奇偶校验。 */
	UART_ERROR_PARITY  = (1 << 1),
	/** @brief 字符的起止帧格式错误。 */
	UART_ERROR_FRAMING = (1 << 2),
	/** @brief 串行输入持续为低，时间超过起始位、数据位、校验位和停止位合计的字符长度。 */
	UART_BREAK = (1 << 3),
	/** @brief 支持碰撞检测的设备发现实际接收数据与已发送数据不符，例如 TX/RX 共线的半双工 RS-485。 */
	UART_ERROR_COLLISION = (1 << 4),
};

/** @brief 发送完成或中止时的事件数据。 */
struct uart_event_tx {
	/** @brief 指向提交给驱动的原始发送缓冲区。 */
	const uint8_t *buf;
	/** @brief 直到本次回调实际发出的字节数。 */
	size_t len;
};

/** @brief 接收就绪事件，新到字节位于 buf[offset] 至 buf[offset + len - 1]；len 是从 offset 起算的增量。 */
struct uart_event_rx {
	/** @brief 当前由驱动填充的接收缓冲首地址。 */
	uint8_t *buf;
	/** @brief 本次新数据在当前缓冲中的起始字节偏移。 */
	size_t offset;
	/** @brief 本次事件可消费的新字节数。 */
	size_t len;
};

/** @brief 接收缓冲释放通知。 */
struct uart_event_rx_buf {
	/** @brief 刚归还给调用者的缓冲首地址。 */
	uint8_t *buf;
};

/** @brief 接收停止时的原因及最后一段数据。 */
struct uart_event_rx_stop {
	/** @brief 触发停止的错误或线路状态。 */
	enum uart_rx_stop_reason reason;
	/** @brief 停收前最后一段已收数据的位置和长度。 */
	struct uart_event_rx data;
};

/** @brief 一次异步通知，通过 type 确定 data 中的有效成员。 */
struct uart_event {
	/** @brief 当前事件种类，调用者须先据此分派。 */
	enum uart_event_type type;
	/** @brief 各种事件数据复用这个联合体的存储。 */
	union uart_event_data {
		/** @brief UART_TX_DONE 或 UART_TX_ABORTED 对应的发送结果。 */
		struct uart_event_tx tx;
		/** @brief UART_RX_RDY 对应的接收缓冲和字节范围。 */
		struct uart_event_rx rx;
		/** @brief UART_RX_BUF_RELEASED 所归还的缓冲。 */
		struct uart_event_rx_buf rx_buf;
		/** @brief UART_RX_STOPPED 的原因和剩余数据。 */
		struct uart_event_rx_stop rx_stop;
	} data;
};

/**
 * @typedef uart_callback_t
 * @brief 由异步 UART 驱动在事件发生时调用的用户函数。
 * @param dev [输入] 产生事件的串口设备。
 * @param evt [输入] 本次回调可读取的事件，根据 type 选择联合体成员。
 * @param user_data [输入] 注册时提供的上下文，可为 NULL。
 * @retval 无。
 */
typedef void (*uart_callback_t)(const struct device *dev,
				struct uart_event *evt, void *user_data);

/** @brief 串口线路参数，供运行时设置和读回接口使用。 */
struct uart_config {
	uint32_t baudrate;    /* 线路速率，单位 bit/s。 */
	uint8_t parity;       /* uart_config_parity 中的校验方式。 */
	uint8_t stop_bits;    /* uart_config_stop_bits 中的停止位编码。 */
	uint8_t data_bits;    /* uart_config_data_bits 中的数据位编码。 */
	uint8_t flow_ctrl;    /* uart_config_flow_control 中的硬件握手方式。 */
};

/** @brief 每个字符的奇偶校验方式。 */
enum uart_config_parity {
	UART_CFG_PARITY_NONE,     /* 不使用奇偶位。 */
	UART_CFG_PARITY_ODD,      /* 奇校验。 */
	UART_CFG_PARITY_EVEN,     /* 偶校验。 */
	UART_CFG_PARITY_MARK,     /* 固定校验位为 1。 */
	UART_CFG_PARITY_SPACE,    /* 固定校验位为 0。 */
};

/** @brief 每个字符结尾的停止位长度。 */
enum uart_config_stop_bits {
	UART_CFG_STOP_BITS_0_5,    /* 半个停止位。 */
	UART_CFG_STOP_BITS_1,      /* 一个停止位。 */
	UART_CFG_STOP_BITS_1_5,    /* 一个半停止位。 */
	UART_CFG_STOP_BITS_2,      /* 两个停止位。 */
};

/** @brief 每个字符有效数据位数的枚举编码。 */
enum uart_config_data_bits {
	UART_CFG_DATA_BITS_5,    /* 五个有效数据位。 */
	UART_CFG_DATA_BITS_6,    /* 六个有效数据位。 */
	UART_CFG_DATA_BITS_7,    /* 七个有效数据位。 */
	UART_CFG_DATA_BITS_8,    /* 八个有效数据位。 */
	UART_CFG_DATA_BITS_9,    /* 九个有效数据位。 */
};

/** @brief 硬件流控方式。关闭时可用线路控制 API 管理相应信号，启用时由硬件或驱动管理。 */
enum uart_config_flow_control {
	UART_CFG_FLOW_CTRL_NONE,       /* 关闭硬件流控。 */
	UART_CFG_FLOW_CTRL_RTS_CTS,    /* 使用 RTS 与 CTS 握手。 */
	UART_CFG_FLOW_CTRL_DTR_DSR,    /* 使用 DTR 与 DSR 握手。 */
};

/**
 * @typedef uart_irq_callback_user_data_t
 * @brief UART 驱动在 ISR 内转调的用户函数。ATS 串口 Shell 注册 uart_callback，再由该函数检查 TX/RX 就绪。
 * @param dev [输入] 触发中断的 UART 设备。
 * @param user_data [输入] 注册时保存的上下文，Shell 在这里传入串口后端实例。
 * @retval 无。
 */
typedef void (*uart_irq_callback_user_data_t)(const struct device *dev,
					      void *user_data);

/**
 * @typedef uart_irq_config_func_t
 * @brief 端口初始化时绑定并使能该实例的中断。
 * @param dev [输入] 当前初始化的 UART 设备。
 * @retval 无。
 * @internal
 */
typedef void (*uart_irq_config_func_t)(const struct device *dev);

/** @brief 串口通用设备配置，联合体按具体硬件的寻址方式保存端口或基址。 */
struct uart_device_config {
	union {
		uint32_t port;    /* 以端口编号寻址时使用。 */
		uint8_t *base;    /* 通过指针映射寄存器时使用。 */
		uint32_t regs;    /* 以整数表示寄存器地址时使用。 */
	};

	uint32_t sys_clk_freq;    /* 外设时钟频率，单位 Hz。 */

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	uart_irq_config_func_t	irq_config_func;    /* 当前设备采用中断或异步配置时的中断连接函数。 */
#endif
};

/** @brief UART 操作表。具体驱动在设备注册时绑定此表；ATS 的 FIFO 读槽绑定 uart_acts_fifo_read。 */
__subsystem struct uart_driver_api {

#ifdef CONFIG_UART_ASYNC_API

	/* 登记异步事件处理函数和上下文。 */
	int (*callback_set)(const struct device *dev,
			    uart_callback_t callback,
			    void *user_data);

	/* 提交 len 字节发送缓冲，完成由异步事件通知。 */
	int (*tx)(const struct device *dev, const uint8_t *buf, size_t len,
		  int32_t timeout);
	/* 请求终止当前异步发送。 */
	int (*tx_abort)(const struct device *dev);

	/* 提交首个可写接收缓冲并启动接收。 */
	int (*rx_enable)(const struct device *dev, uint8_t *buf, size_t len,
			 int32_t timeout);
	/* 将另一个可写缓冲交给驱动，供当前块写满后切换。 */
	int (*rx_buf_rsp)(const struct device *dev, uint8_t *buf, size_t len);
	/* 请求停收并逐块归还缓冲。 */
	int (*rx_disable)(const struct device *dev);

#endif

	/** 控制台轮询输入、输出，分别传回收到的字节和写入发送寄存器。 */
	int (*poll_in)(const struct device *dev, unsigned char *p_char);
	void (*poll_out)(const struct device *dev, unsigned char out_char);

	/** 查询设备锁存的线路错误位。 */
	int (*err_check)(const struct device *dev);

	/** 运行时设置和读取配置。 */
	int (*configure)(const struct device *dev,
			 const struct uart_config *cfg);
	int (*config_get)(const struct device *dev, struct uart_config *cfg);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

	/** 中断内往发送 FIFO 尽量写入 len 字节，返回本次接受的数量。 */
	int (*fifo_fill)(const struct device *dev, const uint8_t *tx_data,
			 int len);

	/** 中断内将接收 FIFO 中最多 size 字节写入 rx_data，返回实际搬出的字节数。 */
	int (*fifo_read)(const struct device *dev, uint8_t *rx_data,
			 const int size);

	/** 使能发送中断，由 FIFO 可写事件推动排队的余下数据。 */
	void (*irq_tx_enable)(const struct device *dev);

	/** 暂时关闭发送中断。 */
	void (*irq_tx_disable)(const struct device *dev);

	/** 查询发送端是否还能接受至少一个字节。 */
	int (*irq_tx_ready)(const struct device *dev);

	/** 打开接收中断，使到达的字符能通知上层。 */
	void (*irq_rx_enable)(const struct device *dev);

	/** 关闭接收中断。 */
	void (*irq_rx_disable)(const struct device *dev);

	/** 查询传输完成状态，具体完成判据取决于驱动。 */
	int (*irq_tx_complete)(const struct device *dev);

	/** 查询接收 FIFO 中是否已有可读取的字符。 */
	int (*irq_rx_ready)(const struct device *dev);

	/** 启用设备错误中断。 */
	void (*irq_err_enable)(const struct device *dev);

	/** 关闭设备错误中断。 */
	void (*irq_err_disable)(const struct device *dev);

	/** 检查该 UART 是否有待处理的中断源。 */
	int (*irq_is_pending)(const struct device *dev);

	/** 在 ISR 开始时让设备按其清除或锁存语义更新中断状态。 */
	int (*irq_update)(const struct device *dev);

	/** 向底层登记 ISR 将调用的函数及透传的上下文。 */
	void (*irq_callback_set)(const struct device *dev,
				 uart_irq_callback_user_data_t cb,
				 void *user_data);

#endif

#ifdef CONFIG_UART_LINE_CTRL
	/* 设置 ctrl 指定的线路控制项。 */
	int (*line_ctrl_set)(const struct device *dev, uint32_t ctrl,
			     uint32_t val);
	/* 通过 val 返回线路状态。 */
	int (*line_ctrl_get)(const struct device *dev, uint32_t ctrl,
			     uint32_t *val);
#endif

#ifdef CONFIG_UART_DRV_CMD
	/* 交由驱动解释专有命令和参数。 */
	int (*drv_cmd)(const struct device *dev, uint32_t cmd, uint32_t p);
#endif

};


/**
  * @brief  登记异步事件回调，在使用发送或接收接口之前建立事件通路。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  callback：[输入] 用于接收异步事件的处理函数。
  * @param  user_data：[输入] 由调用者持有并在回调期间保持有效的上下文，可为空。
  * @retval 0 为成功，-ENOSYS 表示设备未提供回调入口，-ENOTSUP 表示未启用异步接口；其他错误由驱动返回。
  * @note   回调需在缓冲归还前持续有效。
  */
static inline int uart_callback_set(const struct device *dev,
				    uart_callback_t callback,
				    void *user_data)
{
#ifdef CONFIG_UART_ASYNC_API
	const struct uart_driver_api *api =                    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
			(const struct uart_driver_api *)dev->api;

	if (api->callback_set == NULL) {                       /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}

	return api->callback_set(dev, callback, user_data);    /* 登记异步事件回调，在使用发送或接收接口之前建立事件通路。 */
#else
	return -ENOTSUP;                                       /* 构建未启用这一类 UART API。 */
#endif
}

/**
  * @brief  提交发送缓冲，驱动随即返回，完成或超时通过事件报告。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  buf：[输入] 至少 len 字节的可读发送缓冲。
  * @param  len：[输入] 缓冲区长度，单位字节。
  * @param  timeout：[输入] 流控下的发送超时，单位毫秒；SYS_FOREVER_MS 禁用超时。
  * @retval 0 为已受理，-EBUSY 表示已有传输，-ENOTSUP 表示不支持；其余为驱动错误。
  * @note   收到 UART_TX_DONE 或 UART_TX_ABORTED 前保持发送数据有效。
  */
__syscall int uart_tx(const struct device *dev, const uint8_t *buf,
		      size_t len,
		      int32_t timeout);

/**
  * @brief  提交发送缓冲，驱动随即返回，完成或超时通过事件报告。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  buf：[输入] 至少 len 字节的可读发送缓冲。
  * @param  len：[输入] 缓冲区长度，单位字节。
  * @param  timeout：[输入] 流控下的发送超时，单位毫秒；SYS_FOREVER_MS 禁用超时。
  * @retval 0 为已受理，-EBUSY 表示已有传输，-ENOTSUP 表示不支持；其余为驱动错误。
  * @note   收到 UART_TX_DONE 或 UART_TX_ABORTED 前保持发送数据有效。
  */
static inline int z_impl_uart_tx(const struct device *dev, const uint8_t *buf,
				 size_t len, int32_t timeout)

{
#ifdef CONFIG_UART_ASYNC_API
	const struct uart_driver_api *api =        /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
			(const struct uart_driver_api *)dev->api;

	return api->tx(dev, buf, len, timeout);    /* 提交发送缓冲，驱动随即返回，完成或超时通过事件报告。 */
#else
	return -ENOTSUP;                           /* 构建未启用这一类 UART API。 */
#endif
}

/**
  * @brief  请求终止正在进行的异步发送。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 0 为请求成功，-EFAULT 表示没有活动发送，-ENOTSUP 表示不支持。
  * @note   UART_TX_ABORTED 携带此前实际发送的字节数。
  */
__syscall int uart_tx_abort(const struct device *dev);

/**
  * @brief  请求终止正在进行的异步发送。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 0 为请求成功，-EFAULT 表示没有活动发送，-ENOTSUP 表示不支持。
  * @note   UART_TX_ABORTED 携带此前实际发送的字节数。
  */
static inline int z_impl_uart_tx_abort(const struct device *dev)
{
#ifdef CONFIG_UART_ASYNC_API
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
			(const struct uart_driver_api *)dev->api;

	return api->tx_abort(dev);             /* 请求终止正在进行的异步发送。 */
#else
	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */
#endif
}

/**
  * @brief  把首个缓冲交给异步驱动并开始接收。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  buf：[输出] 至少 len 字节的可写接收缓冲，驱动发出释放通知后可复用。
  * @param  len：[输入] 缓冲区长度，单位字节。
  * @param  timeout：[输入] 从最近一个字节起算的接收空闲时限，单位毫秒；SYS_FOREVER_MS 禁用超时。
  * @retval 0 为已启动，-EBUSY 为已在接收，-ENOTSUP 为不支持；其余为驱动错误。
  * @note   接到 UART_RX_BUF_REQUEST 可提交下一块，实现连续接收。
  */
__syscall int uart_rx_enable(const struct device *dev, uint8_t *buf,
			     size_t len,
			     int32_t timeout);

/**
  * @brief  把首个缓冲交给异步驱动并开始接收。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  buf：[输出] 至少 len 字节的可写接收缓冲，驱动发出释放通知后可复用。
  * @param  len：[输入] 缓冲区长度，单位字节。
  * @param  timeout：[输入] 从最近一个字节起算的接收空闲时限，单位毫秒；SYS_FOREVER_MS 禁用超时。
  * @retval 0 为已启动，-EBUSY 为已在接收，-ENOTSUP 为不支持；其余为驱动错误。
  * @note   接到 UART_RX_BUF_REQUEST 可提交下一块，实现连续接收。
  */
static inline int z_impl_uart_rx_enable(const struct device *dev,
					uint8_t *buf,
					size_t len, int32_t timeout)
{
#ifdef CONFIG_UART_ASYNC_API
	const struct uart_driver_api *api =               /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
				(const struct uart_driver_api *)dev->api;

	return api->rx_enable(dev, buf, len, timeout);    /* 把首个缓冲交给异步驱动并开始接收。 */
#else
	return -ENOTSUP;                                  /* 构建未启用这一类 UART API。 */
#endif
}

/**
  * @brief  响应缓冲请求，提交下一块可写缓冲。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  buf：[输出] 至少 len 字节的可写接收缓冲，驱动发出释放通知后可复用。
  * @param  len：[输入] 缓冲区长度，单位字节。
  * @retval 0 为成功，-EBUSY 为下一块已设置，-EACCES 为接收已关闭，-ENOTSUP 为不支持。
  * @note   只提交尚未由驱动使用的缓冲；UART_RX_BUF_RELEASED 前保持其有效。
  */
static inline int uart_rx_buf_rsp(const struct device *dev, uint8_t *buf,
				  size_t len)
{
#ifdef CONFIG_UART_ASYNC_API
	const struct uart_driver_api *api =       /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
				(const struct uart_driver_api *)dev->api;

	return api->rx_buf_rsp(dev, buf, len);    /* 响应缓冲请求，提交下一块可写缓冲。 */
#else
	return -ENOTSUP;                          /* 构建未启用这一类 UART API。 */
#endif
}

/**
  * @brief  停止异步接收并请求归还各缓冲。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 0 为请求成功，-EFAULT 为没有活动接收，-ENOTSUP 为不支持。
  * @note   剩余新字节先通过 UART_RX_RDY 报告，随后释放缓冲并报告 UART_RX_DISABLED。
  */
__syscall int uart_rx_disable(const struct device *dev);

/**
  * @brief  停止异步接收并请求归还各缓冲。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 0 为请求成功，-EFAULT 为没有活动接收，-ENOTSUP 为不支持。
  * @note   剩余新字节先通过 UART_RX_RDY 报告，随后释放缓冲并报告 UART_RX_DISABLED。
  */
static inline int z_impl_uart_rx_disable(const struct device *dev)
{
#ifdef CONFIG_UART_ASYNC_API
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
			(const struct uart_driver_api *)dev->api;

	return api->rx_disable(dev);           /* 停止异步接收并请求归还各缓冲。 */
#else
	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */
#endif
}

/**
  * @brief  读取接收期间发生的线路错误。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 0 为无错误，正位图由 uart_rx_stop_reason 解释，-ENOSYS 表示接口缺失。
  */
__syscall int uart_err_check(const struct device *dev);

/**
  * @brief  读取接收期间发生的线路错误。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 0 为无错误，正位图由 uart_rx_stop_reason 解释，-ENOSYS 表示接口缺失。
  */
static inline int z_impl_uart_err_check(const struct device *dev)
{
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->err_check == NULL) {          /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}

	return api->err_check(dev);            /* 读取接收期间发生的线路错误。 */
}


/**
  * @brief  轮询读取一个字符。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  p_char：[输出] 可写的单字节地址。
  * @retval 通用约定为 0 读到字符，-1 暂无数据，-EBUSY 异步接收占用，-ENOSYS 设备没有该接口。
  * @note   本项目 uart_acts_poll_in 在 RX FIFO 为空时持续轮询，直到读到字节才返回。
  */
__syscall int uart_poll_in(const struct device *dev, unsigned char *p_char);

/**
  * @brief  轮询读取一个字符。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  p_char：[输出] 可写的单字节地址。
  * @retval 通用约定为 0 读到字符，-1 暂无数据，-EBUSY 异步接收占用，-ENOSYS 设备没有该接口。
  * @note   本项目 uart_acts_poll_in 在 RX FIFO 为空时持续轮询，直到读到字节才返回。
  */
static inline int z_impl_uart_poll_in(const struct device *dev,
				      unsigned char *p_char)
{
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->poll_in == NULL) {            /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}

	return api->poll_in(dev, p_char);      /* 轮询读取一个字符。 */
}

/**
  * @brief  以轮询方式发送一个字符。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  out_char：[输入] 待发送的一个字节。
  * @retval 无。
  * @note   流控开启时受对端 CTS 约束；ATS 实现等待 FIFO 有空间后写入数据寄存器。
  */
__syscall void uart_poll_out(const struct device *dev,
				      unsigned char out_char);

/**
  * @brief  以轮询方式发送一个字符。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  out_char：[输入] 待发送的一个字节。
  * @retval 无。
  * @note   流控开启时受对端 CTS 约束；ATS 实现等待 FIFO 有空间后写入数据寄存器。
  */
static inline void z_impl_uart_poll_out(const struct device *dev,
						unsigned char out_char)
{
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	api->poll_out(dev, out_char);          /* 以轮询方式发送一个字符。 */
}

/**
  * @brief  将给定的线路参数交给设备应用。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cfg：[输入] 非空的线路参数。
  * @retval 0 为成功，-ENOSYS 为无运行时配置入口，其余负值由驱动返回。
  */
__syscall int uart_configure(const struct device *dev,
			     const struct uart_config *cfg);

/**
  * @brief  将给定的线路参数交给设备应用。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cfg：[输入] 非空的线路参数。
  * @retval 0 为成功，-ENOSYS 为无运行时配置入口，其余负值由驱动返回。
  */
static inline int z_impl_uart_configure(const struct device *dev,
					const struct uart_config *cfg)
{
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
				(const struct uart_driver_api *)dev->api;

	if (api->configure == NULL) {          /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->configure(dev, cfg);       /* 将给定的线路参数交给设备应用。 */
}

/**
  * @brief  读出本设备当前的线路配置。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cfg：[输出] 接收当前线路参数的可写结构体。
  * @retval 0 为成功，-ENOSYS 为缺少读回入口，其余负值由驱动返回。
  */
__syscall int uart_config_get(const struct device *dev,
			      struct uart_config *cfg);

/**
  * @brief  读出本设备当前的线路配置。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cfg：[输出] 接收当前线路参数的可写结构体。
  * @retval 0 为成功，-ENOSYS 为缺少读回入口，其余负值由驱动返回。
  */
static inline int z_impl_uart_config_get(const struct device *dev,
					 struct uart_config *cfg)
{
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
				(const struct uart_driver_api *)dev->api;

	if (api->config_get == NULL) {         /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}

	return api->config_get(dev, cfg);      /* 读出本设备当前的线路配置。 */
}

/**
  * @brief  在 UART 发送中断处理期间将待发送的数据写入 FIFO。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  tx_data：[输入] 至少 size 字节可读取的发送区。
  * @param  size：[输入] 本次最多请求的字节数。
  * @retval 非负数为实际写入字节数；-ENOSYS 表示无此操作，-ENOTSUP 表示未开启中断接口。
  * @note   先检查 uart_irq_tx_ready，再按实际写入数推进数据源；无人处理就绪的 TX 可能持续触发中断。
  */
static inline int uart_fifo_fill(const struct device *dev,
				 const uint8_t *tx_data,
				 int size)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =           /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->fifo_fill == NULL) {                 /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                       /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}

	return api->fifo_fill(dev, tx_data, size);    /* 在 UART 发送中断处理期间将待发送的数据写入 FIFO。 */
#endif

	return -ENOTSUP;                              /* 构建未启用这一类 UART API。 */
}

/**
  * @brief  在物理 UART 的 RX 中断内，从硬件 FIFO 搬出已经收到的字节。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  rx_data：[输出] 至少 size 字节可写的接收区。
  * @param  size：[输入] 本次最多请求的字节数。
  * @retval 非负数为实际读出的字节数；-ENOSYS 表示驱动无 FIFO 读入口，-ENOTSUP 表示未开启中断接口。
  * @note   接收就绪可能由边沿或电平触发，应读到实际数量少于请求数量为止。USB CDC ACM 的虚拟接口执行上下文不同。ATS 的 uart_rx_handle 将软件队列的可写区直接传入这里。
  */
static inline int uart_fifo_read(const struct device *dev, uint8_t *rx_data,
				 const int size)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =           /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->fifo_read == NULL) {                 /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                       /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}

	return api->fifo_read(dev, rx_data, size);    /* ATS UART_0 在此进入 uart_acts_fifo_read，把 FIFO 中可用字节读入调用者提供的区间。 */
#endif

	return -ENOTSUP;                              /* 构建未启用这一类 UART API。 */
}

/**
  * @brief  打开 UART 的发送中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   若构建或设备未提供此操作，调用直接结束。
  */
__syscall void uart_irq_tx_enable(const struct device *dev);

/**
  * @brief  打开 UART 的发送中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   若构建或设备未提供此操作，调用直接结束。
  */
static inline void z_impl_uart_irq_tx_enable(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_tx_enable != NULL) {
		api->irq_tx_enable(dev);       /* 打开 UART 的发送中断。 */
	}
#endif
}
/**
  * @brief  关闭 UART 的发送中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   若构建或设备未提供此操作，调用直接结束。
  */
__syscall void uart_irq_tx_disable(const struct device *dev);

/**
  * @brief  关闭 UART 的发送中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   若构建或设备未提供此操作，调用直接结束。
  */
static inline void z_impl_uart_irq_tx_disable(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_tx_disable != NULL) {
		api->irq_tx_disable(dev);      /* 关闭 UART 的发送中断。 */
	}
#endif
}

/**
  * @brief  查询 UART 发送 FIFO 是否至少能写入一个字节。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 非零表示可写，0 表示不可写；-ENOSYS 表示无驱动入口，-ENOTSUP 表示未启用中断接口。
  * @note   在 ISR 入口调用 uart_irq_update 后查询。
  */
static inline int uart_irq_tx_ready(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_tx_ready == NULL) {       /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}

	return api->irq_tx_ready(dev);         /* 查询 UART 发送 FIFO 是否至少能写入一个字节。 */
#endif

	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */
}

/**
  * @brief  打开 UART 的接收中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   ATS 实现先清旧的 RX 挂起位，再打开接收中断使能位。
  */
__syscall void uart_irq_rx_enable(const struct device *dev);

/**
  * @brief  打开 UART 的接收中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   ATS 实现先清旧的 RX 挂起位，再打开接收中断使能位。
  */
static inline void z_impl_uart_irq_rx_enable(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_rx_enable != NULL) {
		api->irq_rx_enable(dev);       /* 打开 UART 的接收中断。 */
	}
#endif
}

/**
  * @brief  关闭 UART 的接收中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   若构建或设备未提供此操作，调用直接结束。
  */
__syscall void uart_irq_rx_disable(const struct device *dev);

/**
  * @brief  关闭 UART 的接收中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   若构建或设备未提供此操作，调用直接结束。
  */
static inline void z_impl_uart_irq_rx_disable(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_rx_disable != NULL) {
		api->irq_rx_disable(dev);      /* 关闭 UART 的接收中断。 */
	}
#endif
}

/**
  * @brief  查询设备发送端的完成状态。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 正值表示完成，0 表示未完成；-ENOSYS 表示驱动缺少入口，-ENOTSUP 表示未启用中断接口。
  * @note   在 ISR 更新状态后调用。ATS 对应该接口测试 TX FIFO 空标志；关闭时钟还须满足芯片的线路空闲条件。
  */
static inline int uart_irq_tx_complete(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_tx_complete == NULL) {    /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->irq_tx_complete(dev);      /* 查询设备发送端的完成状态。 */
#endif
	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */

}


/**
  * @brief  查询 UART 是否有收到的数据可读。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 正值为可读，0 表示无数据；-ENOSYS 表示无驱动入口，-ENOTSUP 表示未启用中断接口。
  * @note   在 ISR 更新状态后查询；就绪时按 uart_fifo_read 的约定排空 FIFO。
  */
static inline int uart_irq_rx_ready(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_rx_ready == NULL) {       /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->irq_rx_ready(dev);         /* 查询 UART 是否有收到的数据可读。 */
#endif

	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */
}
/**
  * @brief  启用可选的 UART 错误中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   仅当驱动提供操作且已启用中断 API 时生效。
  */
__syscall void uart_irq_err_enable(const struct device *dev);

/**
  * @brief  启用可选的 UART 错误中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   仅当驱动提供操作且已启用中断 API 时生效。
  */
static inline void z_impl_uart_irq_err_enable(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_err_enable) {
		api->irq_err_enable(dev);      /* 启用可选的 UART 错误中断。 */
	}
#endif
}

/**
  * @brief  关闭可选的 UART 错误中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   仅当驱动提供操作且已启用中断 API 时生效。
  */
__syscall void uart_irq_err_disable(const struct device *dev);

/**
  * @brief  关闭可选的 UART 错误中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 无。
  * @note   仅当驱动提供操作且已启用中断 API 时生效。
  */
static inline void z_impl_uart_irq_err_disable(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_err_disable) {
		api->irq_err_disable(dev);     /* 关闭可选的 UART 错误中断。 */
	}
#endif
}

/**
  * @brief  检查是否有待处理的 UART 中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 正值为有，0 为无，-ENOSYS 表示缺少入口，-ENOTSUP 表示未启用中断接口。
  */
__syscall int uart_irq_is_pending(const struct device *dev);

/**
  * @brief  检查是否有待处理的 UART 中断。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 正值为有，0 为无，-ENOSYS 表示缺少入口，-ENOTSUP 表示未启用中断接口。
  */
static inline int z_impl_uart_irq_is_pending(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_is_pending == NULL) {     /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->irq_is_pending(dev);       /* 检查是否有待处理的 UART 中断。 */
#endif
	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */
}

/**
  * @brief  在 ISR 开头更新中断状态，供本轮就绪检查使用。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 1 为成功，-ENOSYS 表示驱动缺少入口，-ENOTSUP 表示未启用中断接口。
  * @note   硬件可在读时清状态，也可显式确认或靠收发确认；ATS 的更新槽直接返回 1，FIFO 收发会确认对应挂起位。
  */
__syscall int uart_irq_update(const struct device *dev);

/**
  * @brief  在 ISR 开头更新中断状态，供本轮就绪检查使用。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @retval 1 为成功，-ENOSYS 表示驱动缺少入口，-ENOTSUP 表示未启用中断接口。
  * @note   硬件可在读时清状态，也可显式确认或靠收发确认；ATS 的更新槽直接返回 1，FIFO 收发会确认对应挂起位。
  */
static inline int z_impl_uart_irq_update(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->irq_update == NULL) {         /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->irq_update(dev);           /* 在 ISR 开头更新中断状态，供本轮就绪检查使用。 */
#endif
	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */
}

/**
  * @brief  为 UART IRQ 登记设备回调及自定义上下文。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cb：[输入] 接收 UART IRQ 的处理函数；传 NULL 由具体驱动处理。
  * @param  user_data：[输入] 由调用者持有并在回调期间保持有效的上下文，可为空。
  * @retval 无。
  * @note   触发后在 ISR 中调用 cb；调用者按 uart_irq_update 的顺序检查状态。
  */
static inline void uart_irq_callback_user_data_set(const struct device *dev,
						   uart_irq_callback_user_data_t cb,
						   void *user_data)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_driver_api *api =                   /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if ((api != NULL) && (api->irq_callback_set != NULL)) {
		api->irq_callback_set(dev, cb, user_data);    /* 为 UART IRQ 登记设备回调及自定义上下文。 */
	}
#endif
}

/**
  * @brief  用旧接口登记 UART IRQ 回调，上下文传 NULL。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cb：[输入] 接收 UART IRQ 的处理函数；传 NULL 由具体驱动处理。
  * @retval 无。
  * @note   设备仍通过回调的第一个参数传入。
  */
static inline void uart_irq_callback_set(const struct device *dev,
					 uart_irq_callback_user_data_t cb)
{
	uart_irq_callback_user_data_set(dev, cb, NULL);    /* 复用带上下文的注册接口，把第三个参数置空。 */
}


/**
  * @brief  选择并写入线路控制项。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  ctrl：[输入] 从 uart_line_ctrl 选取的控制项。
  * @param  val：[输入] 对应控制项的值。
  * @retval 0 为成功，-ENOSYS 为无设备入口，-ENOTSUP 为未启用此功能，其余负值来自驱动。
  */
__syscall int uart_line_ctrl_set(const struct device *dev,
				 uint32_t ctrl, uint32_t val);

/**
  * @brief  选择并写入线路控制项。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  ctrl：[输入] 从 uart_line_ctrl 选取的控制项。
  * @param  val：[输入] 对应控制项的值。
  * @retval 0 为成功，-ENOSYS 为无设备入口，-ENOTSUP 为未启用此功能，其余负值来自驱动。
  */
static inline int z_impl_uart_line_ctrl_set(const struct device *dev,
					    uint32_t ctrl, uint32_t val)
{
#ifdef CONFIG_UART_LINE_CTRL
	const struct uart_driver_api *api =           /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->line_ctrl_set == NULL) {             /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                       /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->line_ctrl_set(dev, ctrl, val);    /* 选择并写入线路控制项。 */
#endif

	return -ENOTSUP;                              /* 构建未启用这一类 UART API。 */
}

/**
  * @brief  读取指定线路控制项的当前值。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  ctrl：[输入] 从 uart_line_ctrl 选取的控制项。
  * @param  val：[输出] 接收控制项数值的可写地址。
  * @retval 0 为成功，-ENOSYS 为无设备入口，-ENOTSUP 为未启用此功能，其余负值来自驱动。
  */
__syscall int uart_line_ctrl_get(const struct device *dev, uint32_t ctrl,
				 uint32_t *val);

/**
  * @brief  读取指定线路控制项的当前值。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  ctrl：[输入] 从 uart_line_ctrl 选取的控制项。
  * @param  val：[输出] 接收控制项数值的可写地址。
  * @retval 0 为成功，-ENOSYS 为无设备入口，-ENOTSUP 为未启用此功能，其余负值来自驱动。
  */
static inline int z_impl_uart_line_ctrl_get(const struct device *dev,
					    uint32_t ctrl, uint32_t *val)
{
#ifdef CONFIG_UART_LINE_CTRL
	const struct uart_driver_api *api =           /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->line_ctrl_get == NULL) {             /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                       /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->line_ctrl_get(dev, ctrl, val);    /* 读取指定线路控制项的当前值。 */
#endif

	return -ENOTSUP;                              /* 构建未启用这一类 UART API。 */
}

/**
  * @brief  将厂商专用命令及参数交给设备处理。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cmd：[输入] 驱动识别的命令号。
  * @param  p：[输入] 该命令约定的参数。
  * @retval 0 为成功，-ENOSYS 为无设备入口，-ENOTSUP 为未启用该接口，其他错误由驱动决定。
  * @note   命令编号和参数的含义由对应设备驱动定义。
  */
__syscall int uart_drv_cmd(const struct device *dev, uint32_t cmd, uint32_t p);

/**
  * @brief  将厂商专用命令及参数交给设备处理。
  * @param  dev：[输入] 已初始化、非空的 UART 设备，其 api 指向本实例的操作表。
  * @param  cmd：[输入] 驱动识别的命令号。
  * @param  p：[输入] 该命令约定的参数。
  * @retval 0 为成功，-ENOSYS 为无设备入口，-ENOTSUP 为未启用该接口，其他错误由驱动决定。
  * @note   命令编号和参数的含义由对应设备驱动定义。
  */
static inline int z_impl_uart_drv_cmd(const struct device *dev, uint32_t cmd,
				      uint32_t p)
{
#ifdef CONFIG_UART_DRV_CMD
	const struct uart_driver_api *api =    /* 取得这个设备实例的操作表，后续从对应槽位调用驱动。 */
		(const struct uart_driver_api *)dev->api;

	if (api->drv_cmd == NULL) {            /* 先检查可选操作是否已由设备驱动实现。 */
		return -ENOSYS;                /* 硬件驱动未登记这个操作，向调用者报告接口缺失。 */
	}
	return api->drv_cmd(dev, cmd, p);      /* 将厂商专用命令及参数交给设备处理。 */
#endif

	return -ENOTSUP;                       /* 构建未启用这一类 UART API。 */
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#include <syscalls/uart.h>

#endif    /* ZEPHYR_INCLUDE_DRIVERS_UART_H_ */
