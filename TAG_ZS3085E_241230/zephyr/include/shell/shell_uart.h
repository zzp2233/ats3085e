/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* UART Shell 后端的公共定义。由宏组合常驻配置、可写控制块和软件收发资源，供 Shell 核心通过传输接口回调。 */
#ifndef SHELL_UART_H__
#define SHELL_UART_H__

#include <shell/shell.h>              /* 提供 Shell 实例及通用传输 API 类型。 */
#include <sys/ring_buffer.h>          /* 提供 ring_buf 及生成固定缓冲区的宏。 */
#include <sys/atomic.h>               /* 提供多上下文共享的原子状态类型。 */
#include "mgmt/mcumgr/smp_shell.h"    /* 可选复用路径需要管理协议接收状态与相关接口。 */

/* 对 C++ 调用者使用 C 链接名。 */
#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  观察 UART 后端的一次输出请求。
  * @param  data：[输入]调用期间有效的原始可读数据指针。
  * @param  len：[输入]这次请求传入 write 的字节数。
  * @param  user_data：[输入]登记此函数时关联的用户指针。
  * @retval 无。
  * @note   write 在选定输出路径处理后调用；队列较小时 len 可能大于本次实际入队量。
  */
typedef void (* shell_uart_callback_t)(const uint8_t *data, uint32_t len, void *user_data);

/* 实例运行期间可修改的状态。设备绑定、事件订阅者及输出观察者的上下文分别保存。 */
struct shell_uart_ctrl_blk {
	const struct device *dev;             /* 初始化时绑定的 UART 设备。 */
	shell_transport_handler_t handler;    /* Shell 核心的接收或发送就绪事件入口。 */
	void *context;                        /* 发布事件时传回给 handler 的上层上下文。 */
	atomic_t tx_busy;                     /* TX 环形队列发送是否已有中断服务推进；空闲到忙通过原子交换判定。 */
	bool blocking_tx;                     /* 普通 UART 的 write 据此在排队发送和阻塞输出之间选择。 */
#ifdef CONFIG_MCUMGR_SMP_SHELL
	struct smp_shell_data smp;         /* 管理协议复用串口时独立保存帧接收状态。 */
#endif                                     /* CONFIG_MCUMGR_SMP_SHELL */
	shell_uart_callback_t callback;    /* 可选的输出观察者，在 write 路径中调用。 */
	void *user_data;                   /* 仅传给 callback，区别于带给 Shell 事件处理器的 context。 */
};

#ifdef CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN
/* 以实例名作前缀定义发送队列，其容量由 _size（字节）指定。 */
#define Z_UART_SHELL_TX_RINGBUF_DECLARE(_name, _size) \
	RING_BUF_DECLARE(_name##_tx_ringbuf, _size)

/* 中断到达即触发接收，本配置无需声明周期定时器。 */
#define Z_UART_SHELL_RX_TIMER_DECLARE(_name)                        /* 无须分配定时器。 */
#define Z_UART_SHELL_TX_RINGBUF_PTR(_name) (&_name##_tx_ringbuf)    /* 供传输结构绑定该实例的发送队列。 */

/* 中断实例的描述符保存空定时器指针。 */
#define Z_UART_SHELL_RX_TIMER_PTR(_name) NULL

#else    /* CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN */
/* 轮询模式逐字节直接输出，省去发送队列，并为接收保留一个定时器。 */
#define Z_UART_SHELL_TX_RINGBUF_DECLARE(_name, _size)                               /* 无须分配 TX 队列。 */
#define Z_UART_SHELL_RX_TIMER_DECLARE(_name) static struct k_timer _name##_timer    /* 声明实例专属的定时器，接收轮询在其到期函数内运行。 */
#define Z_UART_SHELL_TX_RINGBUF_PTR(_name) NULL                                     /* 不通过 TX 环形队列，实例保存空指针。 */
#define Z_UART_SHELL_RX_TIMER_PTR(_name) (&_name##_timer)                           /* 取得本实例的接收轮询定时器地址。 */
#endif                                                                              /* CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN */

/* 连接可写控制块、轮询定时器和软件环形队列。未选模式的可选指针按上方条件宏置为 NULL。 */
struct shell_uart {
	struct shell_uart_ctrl_blk *ctrl_blk;    /* 指向本实例可写的设备、回调及状态。 */
	struct k_timer *timer;                   /* 轮询配置使用的定时器；中断配置为 NULL。 */
	struct ring_buf *tx_ringbuf;             /* 中断配置的 TX 软件队列；轮询配置为 NULL。 */
	struct ring_buf *rx_ringbuf;             /* 所有配置都有的 RX 队列，字节在 Shell 调用 read 时出队。 */
};

/**
  * @brief  定义一个静态 UART 后端及其控制块、软件队列和可选轮询定时器。
  * @param  _name：[输入]传输实例标识符，同时作为内部对象的命名前缀。
  * @param  _tx_ringbuf_size：[输入]中断模式发送环形队列容量，单位为字节。
  * @param  _rx_ringbuf_size：[输入]软件接收队列容量，单位为字节。
  * @retval 此宏生成对象定义。
  * @note   RX 队列承接来自 FIFO 或轮询的字节。生成的 shell_transport 以 api 绑定操作表、以 ctx 指向本实例的组织结构。
  */
#define SHELL_UART_DEFINE(_name, _tx_ringbuf_size, _rx_ringbuf_size)	\
	extern const struct shell_transport_api shell_uart_transport_api; \
	static struct shell_uart_ctrl_blk _name##_ctrl_blk;		\
	Z_UART_SHELL_RX_TIMER_DECLARE(_name);				\
	Z_UART_SHELL_TX_RINGBUF_DECLARE(_name, _tx_ringbuf_size);	\
	RING_BUF_DECLARE(_name##_rx_ringbuf, _rx_ringbuf_size);		\
	static const struct shell_uart _name##_shell_uart = {		\
		.ctrl_blk = &_name##_ctrl_blk,				\
		.timer = Z_UART_SHELL_RX_TIMER_PTR(_name),		\
		.tx_ringbuf = Z_UART_SHELL_TX_RINGBUF_PTR(_name),	\
		.rx_ringbuf = &_name##_rx_ringbuf,			\
	};								\
	struct shell_transport _name = {				\
		.api = &shell_uart_transport_api,			\
		.ctx = (struct shell_uart *)&_name##_shell_uart		\
	}

/**
  * @brief  取得串口后端的静态 Shell 实例。
  * @param  无。
  * @retval 返回有效的常驻 Shell 描述符指针。
  * @note   可传给 shell_execute_cmd 等接口，用此实例执行命令。
  */
const struct shell *shell_backend_uart_get_ptr(void);

/**
  * @brief  登记用于观察串口后端输出的回调和用户指针。
  * @param  callback：[输入]输出处理函数，NULL 取消观察。
  * @param  user_data：[输入]回调的自定义上下文，可为 NULL，登记后需在使用期间保持有效。
  * @retval 当前实现返回 0。
  * @note   观察者在每次 write 的输出路径处理之后收到原始请求指针和请求长度。
  */
int shell_uart_register_output_callback(shell_uart_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif    /* SHELL_UART_H__ */
