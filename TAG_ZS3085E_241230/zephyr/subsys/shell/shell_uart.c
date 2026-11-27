/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* UART Shell 传输后端。当前 ATS3085E 板型使用 UART_0 的中断接收：字节先进入软件环形队列，RX_RDY 再唤醒等待输入的 Shell 线程。 */
#include <shell/shell_uart.h>    /* 声明后端实例、控制块及生成宏。 */
#include <drivers/uart.h>        /* 提供 FIFO、轮询、中断和线路控制接口。 */
#include <init.h>                /* 提供系统启动阶段的 SYS_INIT 注册机制。 */
#include <logging/log.h>         /* 提供日志模块的注册与等级定义。 */
#include <net/buf.h>             /* 提供可选管理协议使用的网络缓冲池宏。 */

#ifdef CONFIG_USB_UART_CONSOLE
#include <drivers/console/uart_usb.h>    /* 仅 USB 虚拟串口构建使用专用发送与完成通知接口。 */
#endif

#define LOG_MODULE_NAME shell_uart    /* 指定下面串口后端的日志模块名。 */
LOG_MODULE_REGISTER(shell_uart);      /* 登记该模块的日志源。 */

/* 轮询后端使用配置的毫秒周期；没有定义该值时保留无等待形式，当前板型选择了中断接收。 */
#ifdef CONFIG_SHELL_BACKEND_SERIAL_RX_POLL_PERIOD
#define RX_POLL_PERIOD K_MSEC(CONFIG_SHELL_BACKEND_SERIAL_RX_POLL_PERIOD)
#else
#define RX_POLL_PERIOD K_NO_WAIT
#endif

#ifdef CONFIG_MCUMGR_SMP_SHELL
/* 管理协议与交互式 Shell 复用串口时，为分离出的 SMP 帧建立固定缓冲池。 */
NET_BUF_POOL_DEFINE(smp_shell_rx_pool, CONFIG_MCUMGR_SMP_SHELL_RX_BUF_COUNT,
		    SMP_SHELL_RX_BUF_SIZE, 0, NULL);
#endif    /* CONFIG_MCUMGR_SMP_SHELL */

/* 建立串口传输实例及软件收发资源。当前板型给 RX 队列分配 64 字节，Shell 可分多次读出一行。 */
SHELL_UART_DEFINE(shell_transport_uart,
		  CONFIG_SHELL_BACKEND_SERIAL_TX_RING_BUFFER_SIZE,
		  CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE);
/* 绑定提示符、传输接口、日志队列及换行转换标志，生成供框架运行的 Shell 实例。 */
SHELL_DEFINE(shell_uart, CONFIG_SHELL_PROMPT_UART, &shell_transport_uart,
	     CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_SIZE,
	     CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_TIMEOUT,
	     SHELL_FLAG_OLF_CRLF);

#ifdef CONFIG_MAGIC_SYSRQ
/**
  * @brief  将一个已收到的字符交给 UART 系统请求键识别器。
  * @param  port：[输入]产生该字符的 UART 设备。
  * @param  c：[输入]本次收到的一个字符。
  * @retval 无。
  * @note   由 debug/sysrq.c 提供实现，配置 CONFIG_MAGIC_SYSRQ 时使用。
  */
void uart_handle_sysrq_char(const struct device * port, char c);
/**
  * @brief  按接收顺序将一批字符交给系统请求键识别器。
  * @param  port：[输入]有效 UART 设备。
  * @param  buf：[输入]至少 len 字节的接收区域，此函数只读。
  * @param  len：[输入]本次读到的字节数。
  * @retval 无。
  * @note   调用者仍按原路径处理这些字节。
  */
void uart_handle_sysrq_str(const struct device * port, uint8_t *buf, int len)
{
	int i;                                           /* i 为本批输入中待检查的字符下标。 */

	/* 依次送检刚收到的所有字符，允许识别跨字节的系统请求键序列。 */
	for(i = 0; i < len ; i++)
		uart_handle_sysrq_char(port, buf[i]);    /* 让系统请求键处理器观察该字节。 */
}
#endif

#ifdef CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN
/**
  * @brief  在串口中断回调内从硬件接收 FIFO 取字节，提交至软件接收环形缓冲区并报告接收事件。
  * @param  dev：[输入]触发接收的有效 UART 设备。
  * @param  sh_uart：[输入]该后端实例，含 RX 队列、事件回调及其上下文。
  * @retval 无。
  * @note   FIFO 保存硬件刚收到的字节，rx_ringbuf 保留尚未由 Shell 线程取走的字节。RX_RDY 只通知有输入到达，整行编辑、分词和处理函数调用由后续线程完成。
  */
static void uart_rx_handle(const struct device *dev,
			   const struct shell_uart *sh_uart)
{
	uint8_t *data;                                                       /* 指向本轮认领的连续可写区。 */
	uint32_t len;                                                        /* 此次可认领的连续空间大小，单位为字节。 */
	uint32_t rd_len;                                                     /* 保存 FIFO 读入量；分离 SMP 字节后表示留给 Shell 的长度。 */
	bool new_data = false;                                               /* 本次回调可能连续取多段，最后据此决定是否发布输入事件。 */

	#ifdef CONFIG_MCUMGR_SMP_SHELL
		struct smp_shell_data *const smp = &sh_uart->ctrl_blk->smp;          /* 使用此 UART 自己的管理协议接收状态。 */
	#endif

	/* 每轮先认领队列尾部一段连续可写空间，使硬件 FIFO 能直接写入预留区域。 */
	do {
		len = ring_buf_put_claim(sh_uart->rx_ringbuf, &data,sh_uart->rx_ringbuf->size);         /* 申请至多整个队列容量，实际连续长度由接口返回。 */

		if (len > 0) {                                               /* 有空间时直接把 FIFO 数据写进认领区。 */
			rd_len = uart_fifo_read(dev, data, len);             /* 返回实际搬出的字节数，可能少于预留容量。 */

			/* 有字节到达才标记本轮活动；即使部分字节随后交给 SMP，Shell 也可能需要处理事件。 */
			if (rd_len > 0) {
				new_data = true;                             /* 记录设备本轮确实读出了数据。 */
				#ifdef CONFIG_MAGIC_SYSRQ
				uart_handle_sysrq_str(dev, data, rd_len);    /* 启用系统请求键时检查这批原始字节。 */
				#endif
			}
#ifdef CONFIG_MCUMGR_SMP_SHELL
			/* 管理协议先取走属于 SMP 帧的前缀，再把其余普通命令字节挪到认领区开头。 */
			size_t i = smp_shell_rx_bytes(smp, data, rd_len);    /* i 是被管理协议消耗的前缀字节数。 */

			rd_len -= i;                                         /* 剩余长度对应普通 Shell 输入。 */

			if (rd_len) {                                        /* 有剩余字节才需要前移。 */
				for (uint32_t j = 0; j < rd_len; j++) {      /* 遍历保留给 Shell 的字节。 */
					data[j] = data[i + j];               /* 从未被管理协议消费的位置复制到认领区开头。 */
				}
			}
#endif                                                                       /* CONFIG_MCUMGR_SMP_SHELL */

			/* 提交实际留给 Shell 的字节数。提交之前，读取方不能把这段认领区域当作新数据。 */
			int err = ring_buf_put_finish(sh_uart->rx_ringbuf,
						      rd_len);               /* 仅提交留给 Shell 的字节；可以为零。 */
			(void)err;                                           /* 断言关闭时避免产生未使用变量警告。 */
			__ASSERT_NO_MSG(err == 0);                           /* 提交量应不超过此前认领长度。 */
		} else {
			uint8_t dummy;                                       /* 暂存无处入队的单个字节。 */

			//LOG_WRN("RX ring buffer full.");

			/* 队列已满时仍从 FIFO 取走一个字节；它无法进入 Shell 输入队列，可选的 SysRq 或 SMP 仍有机会使用它。 */
			rd_len = uart_fifo_read(dev, &dummy, 1);             /* 尝试清除 FIFO 中的一个字节。 */
			#ifdef CONFIG_MAGIC_SYSRQ
			if(rd_len == 1)                                      /* 只有读到有效数据才交给系统请求键。 */
				uart_handle_sysrq_char(dev, dummy);          /* 即使 Shell 队列已满仍能识别系统请求键。 */
			#endif
#ifdef CONFIG_MCUMGR_SMP_SHELL
			if ((rd_len != 0) &&
			    (smp_shell_rx_bytes(smp, &dummy, 1) == 1)) {     /* 仅成功取得并由管理协议接收时报告新活动。 */
				new_data = true;                             /* 管理协议随后也需要由线程处理。 */
			}
#endif                                                                       /* CONFIG_MCUMGR_SMP_SHELL */
		}
	} while (rd_len && (rd_len == len));                                 /* 非空且本轮提交长度恰好用足认领长度时再尝试一次。 */

	/* 将这批输入合并为一次接收就绪通知。回调携带 Shell 上下文，输入字节保存在 RX 队列中。 */
	if (new_data) {
		sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_RX_RDY,       /* 通知上层有输入活动，事件本身不传递任何字节。 */
					   sh_uart->ctrl_blk->context);
	}
}
#ifndef CONFIG_USB_UART_CONSOLE
/**
  * @brief  按配置等待串口 DTR（数据终端就绪）信号。
  * @param  dev：[输入]发送使用的 UART 设备。
  * @retval 无。
  * @note   未启用检查时立即返回。若启用且 DTR 一直为低，会反复调用 k_sleep；中断回调上下文不能睡眠。
  */
static void uart_dtr_wait(const struct device *dev)
{
	if (IS_ENABLED(CONFIG_SHELL_BACKEND_SERIAL_CHECK_DTR)) {
		int dtr, err;                                                       /* 保存线路电平和控制查询结果。 */

		/* 只在配置要求时轮询终端状态；DTR 未拉高就让出处理器 100 毫秒后再查。 */
		while (true) {
			err = uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);    /* 查询终端就绪引脚。 */
			if (err == -ENOSYS || err == -ENOTSUP) {                    /* 驱动不提供 DTR 查询时停止等待。 */
				break;                                              /* 继续后面的发送路径。 */
			}
			if (dtr) {                                                  /* 已拉高 DTR，可开始写 FIFO。 */
				break;                                              /* 无需额外等待。 */
			}
			k_sleep(K_MSEC(100));                                       /* 暂停后再检查 DTR。 */
		}
	}
}

/**
  * @brief  从发送环形队列向硬件发送 FIFO 搬运本轮可接收的字节。
  * @param  dev：[输入]报告发送就绪的 UART 设备。
  * @param  sh_uart：[输入]该串口后端实例。
  * @retval 无。
  * @note   用于普通 UART 的中断发送服务。FIFO 接收字节后即可释放相应的软件队列空间，线路上最后一位可能仍在发送。
  */
static void uart_tx_handle(const struct device *dev,
			   const struct shell_uart *sh_uart)
{
	uint32_t len;                                                   /* 本轮可读长度，随后改为 FIFO 接纳的字节数。 */
	int err;                                                        /* 保存队列读完成操作的结果。 */
	const uint8_t *data;                                            /* 指向仍由 TX 软件队列持有的只读区段。 */

	/* 在不移动读指针的情况下取得连续的已入队区段，再按 FIFO 实际接受的字节数归还空间。 */
	len = ring_buf_get_claim(sh_uart->tx_ringbuf, (uint8_t **)&data,
				 sh_uart->tx_ringbuf->size);            /* 申请最多整个 TX 队列容量，实际长度受连续区限制。 */
	if (len) {                                                      /* 仅队列非空时写硬件。 */
		uart_dtr_wait(dev);                                     /* 如需握手，先确认终端就绪。 */
		len = uart_fifo_fill(dev, data, len);                   /* 硬件只能接受部分数据时保留其余数据在软件队列。 */
		err = ring_buf_get_finish(sh_uart->tx_ringbuf, len);    /* 按真实写出量更新队列读指针。 */
		__ASSERT_NO_MSG(err == 0);                              /* 断言完成量与认领区相容。 */
	} else {
		uart_irq_tx_disable(dev);                               /* 队列暂时无数据，禁止空闲发送中断。 */
		sh_uart->ctrl_blk->tx_busy = 0;                         /* 标为空闲，使下一次入队能重新启动中断。 */
	}

	/* 向 Shell 报告发送侧可继续工作，供正在等待输出进度的上层处理。 */
	sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_TX_RDY,
				   sh_uart->ctrl_blk->context);         /* 把初始化时保存的上层上下文原样传回。 */
}
#endif

#ifdef CONFIG_USB_UART_CONSOLE
/**
  * @brief  服务 USB 虚拟串口的完成通知，并在接收就绪时转交接收处理。
  * @param  dev：[输入]回调所属设备。
  * @param  user_data：[输入]注册回调时附带的有效 shell_uart 实例指针。
  * @retval 无。
  * @note   用于启用 CONFIG_USB_UART_CONSOLE 的中断后端。
  */
static void uart_callback(const struct device *dev, void *user_data)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)user_data;    /* 找回 IRQ 注册时保存的后端实例。 */

	/* 先更新 USB 发送完成状态，再检查虚拟串口是否有输入。 */
	uart_usb_update_tx_done();                                            /* 处理 USB 控制台实现的发送完成状态。 */

	if (uart_irq_rx_ready(dev)) {                                         /* 接收准备好时再读 FIFO。 */
		uart_rx_handle(dev, sh_uart);                                 /* 把收到的字节交给相同的软件队列。 */
	}
}
#else
/**
  * @brief  更新本次 UART 中断状态，并分别服务就绪的接收和发送源。
  * @param  dev：[输入]触发中断回调的 UART 设备。
  * @param  user_data：[输入]注册时传入的有效 shell_uart 实例。
  * @retval 无。
  * @note   接收事件交给此实例的 handler；命令匹配随后在 Shell 线程处理。
  */
static void uart_callback(const struct device *dev, void *user_data)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)user_data;    /* 确定事件属于哪个传输实例。 */

	/* 先刷新 UART 中断状态，再逐项检查 RX、TX 是否就绪；同次回调可以处理两个方向。 */
	uart_irq_update(dev);                                                 /* 使随后的就绪检查使用本次中断状态。 */

#ifdef CONFIG_USB_UART_CONSOLE
	uart_usb_update_tx_done();                                            /* 这里还受外层非 USB 分支限制，普通 UART 构建不会执行这行。 */
#endif

	if (uart_irq_rx_ready(dev)) {                                         /* 先处理本次中断报告的接收就绪。 */
		uart_rx_handle(dev, sh_uart);                                 /* 从 FIFO 入队并报告 RX_RDY。 */
	}

	if (uart_irq_tx_ready(dev)) {                                         /* 发送就绪与接收就绪分别判断。 */
		uart_tx_handle(dev, sh_uart);                                 /* 从 TX 队列为硬件补充数据。 */
	}
}
#endif    /* CONFIG_USB_UART_CONSOLE */
#endif    /* CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN */

/**
  * @brief  初始化中断模式所用的环形队列和设备回调。
  * @param  sh_uart：[输入]有效后端，控制块已经保存绑定的 UART 设备。
  * @retval 无。
  * @note   先建立回调与实例的关联，再允许设备产生接收通知；仅中断配置有初始化语句。
  */
static void uart_irq_init(const struct shell_uart *sh_uart)
{
#ifdef CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN
	const struct device *dev = sh_uart->ctrl_blk->dev;                       /* 使用初始化时绑定的设备。 */

	/* 清空两个软件环形队列后关联设备回调，最后开启接收中断。 */
	ring_buf_reset(sh_uart->tx_ringbuf);                                     /* 重置发送队列的读写位置。 */
	ring_buf_reset(sh_uart->rx_ringbuf);                                     /* 重置接收队列的读写位置。 */
	sh_uart->ctrl_blk->tx_busy = 0;                                          /* 将发送侧初态设为空闲。 */
	uart_irq_callback_user_data_set(dev, uart_callback, (void *)sh_uart);    /* 把 UART 中断回调与 sh_uart 实例一起注册。 */
	uart_irq_rx_enable(dev);                                                 /* 允许 RX 事件触发该回调。 */
#endif
}

/**
  * @brief  轮询 UART，把当前可读的字节依次送入接收环形队列。
  * @param  timer：[输入]已关联 shell_uart 实例的轮询定时器。
  * @retval 无。
  * @note   由轮询模式的定时器触发。队列满时丢弃该字节，本实现仍发送 RX_RDY 事件。
  */
static void timer_handler(struct k_timer *timer)
{
	uint8_t c;                                                          /* 暂存轮询读出的单个字节。 */
	const struct shell_uart *sh_uart = k_timer_user_data_get(timer);    /* 由定时器的用户数据找回后端。 */

	/* 反复取走所有可读字节；轮询接口首次报告未取到字节即结束本轮。 */
	while (uart_poll_in(sh_uart->ctrl_blk->dev, &c) == 0) {             /* UART 有输入时逐个读出字符。 */
#ifdef CONFIG_MAGIC_SYSRQ
		uart_handle_sysrq_char(sh_uart->ctrl_blk->dev, c);          /* 对收到的字符同时进行系统请求键检查。 */
#endif
		if (ring_buf_put(sh_uart->rx_ringbuf, &c, 1) == 0U) {       /* 返回零表示软件队列已满，该字节未入队。 */
			//LOG_WRN("RX ring buffer full.");
		}
		sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_RX_RDY,      /* 每读到一个硬件字节都会发送事件，取到的有效数据量由 read 返回。 */
					   sh_uart->ctrl_blk->context);
	}
}

/**
  * @brief  保存 Shell 的传输层回调，并为当前配置启用 UART 接收。
  * @param  transport：[输入]有效传输描述符，其 ctx 指向本后端。
  * @param  config：[输入]由 shell_init 传入的有效 UART 设备指针。
  * @param  evt_handler：[输入]上层事件函数，接收或发送就绪时调用。
  * @param  context：[输入]上层给出的不透明指针，触发事件时原样传回。
  * @retval 本实现返回 0。
  * @note   设备和上下文由外部持有，后端在运行期间保留其地址。
  */
static int init(const struct shell_transport *transport,
		const void *config,
		shell_transport_handler_t evt_handler,
		void *context)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;    /* 由传输描述符取得串口专用实例。 */

	/* 保存设备及上层回调，使后续硬件事件能送回创建此后端的同一 Shell 实例。 */
	sh_uart->ctrl_blk->dev = (const struct device *)config;                    /* 记录本后端使用的 UART 设备。 */
	sh_uart->ctrl_blk->handler = evt_handler;                                  /* 保存用于发布 RX_RDY、TX_RDY 的回调。 */
	sh_uart->ctrl_blk->context = context;                                      /* 保存回调需要的上层上下文。 */

#ifdef CONFIG_MCUMGR_SMP_SHELL
	sh_uart->ctrl_blk->smp.buf_pool = &smp_shell_rx_pool;                      /* 给此串口绑定 SMP 缓冲池。 */
	k_fifo_init(&sh_uart->ctrl_blk->smp.buf_ready);                            /* 初始化待处理管理帧队列。 */
#endif

	/* 中断模式注册设备回调，轮询模式把定时器与当前实例关联后周期触发。 */
	if (IS_ENABLED(CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN)) {
		uart_irq_init(sh_uart);                                            /* 注册设备中断并允许接收。 */
	} else {
		k_timer_init(sh_uart->timer, timer_handler, NULL);                 /* 为轮询模式安装到期函数。 */
		k_timer_user_data_set(sh_uart->timer, (void *)sh_uart);            /* 让到期函数能找到具体后端实例。 */
		k_timer_start(sh_uart->timer, RX_POLL_PERIOD, RX_POLL_PERIOD);     /* 首次到期和后续周期使用同一时间参数。 */
	}

	return 0;                                                                  /* 正常完成后向 Shell 报告成功。 */
}

/**
  * @brief  关闭中断收发事件或停止轮询定时器。
  * @param  transport：[输入]已初始化的串口传输实例。
  * @retval 本实现返回 0。
  * @note   不释放静态控制块或环形缓冲区。
  */
static int uninit(const struct shell_transport *transport)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;    /* 找到需要停止的后端实例。 */

	/* 按启用时采用的接收方式停止事件来源。 */
	if (IS_ENABLED(CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN)) {
		const struct device *dev = sh_uart->ctrl_blk->dev;                 /* 取得该实例当前绑定的 UART。 */

		uart_irq_tx_disable(dev);                                          /* 关闭发送中断。 */
		uart_irq_rx_disable(dev);                                          /* 关闭接收中断。 */
	} else {
		k_timer_stop(sh_uart->timer);                                      /* 停止周期轮询。 */
	}

	return 0;                                                                  /* 向上层报告停用完成。 */
}

/**
  * @brief  设置传输后端的发送模式，必要时关闭 UART 发送中断。
  * @param  transport：[输入]有效的串口传输实例。
  * @param  blocking_tx：[输入]为真时要求采用阻塞式输出。
  * @retval 本实现返回 0。
  * @note   在普通中断式 UART 的 write 分支中，该标志决定是否绕过发送队列；平台专用输出路径有自己的实现。
  */
static int enable(const struct shell_transport *transport, bool blocking_tx)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;    /* 找回可写控制块所在的实例。 */

	sh_uart->ctrl_blk->blocking_tx = blocking_tx;                              /* 保存输出方式供 write 使用。 */

	/* 切换到阻塞输出时，普通 UART 不需要 TX 空闲中断推进队列。 */
	if (blocking_tx) {
#ifdef CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN
		uart_irq_tx_disable(sh_uart->ctrl_blk->dev);                       /* 停止中断驱动的异步排队发送通知。 */
#endif
	}

	return 0;                                                                  /* 返回设置完成。 */
}
#ifdef CONFIG_ACTIONS_PRINTK_DMA
/**
  * @brief  向炬芯的 printk 输出路径提交缓冲区。
  * @param  buf：[输入]至少 len 字节的可读数据。
  * @param  len：[输入]提交的字节数。
  * @retval 返回输出路径报告的字节数。
  * @note   实现在 subsys/trace/printk_dma.c；按其初始化状态选择直接输出或将数据送入输出缓冲并请求 DMA。
  */
extern int uart_dma_send_buf(const uint8_t *buf, int len);
#elif !defined(CONFIG_USB_UART_CONSOLE)
/**
  * @brief  将待发数据排入 TX 软件队列，并在首次由闲转忙时开启发送中断。
  * @param  sh_uart：[输入]普通串口后端及其已初始化的 TX 队列。
  * @param  data：[输入]至少 length 字节的可读区域。
  * @param  length：[输入]请求写出的字节数。
  * @param  cnt：[输出]有效指针，记录本次实际入队的字节数。
  * @retval 无。
  * @note   队列剩余空间可能小于 length；中断服务随后把已入队的数据送进 FIFO。
  */
static void irq_write(const struct shell_uart *sh_uart, const void *data,
		     size_t length, size_t *cnt)
{
	/* 先按可用空间入队，再通过原子交换识别空闲到忙的转换。 */
	*cnt = ring_buf_put(sh_uart->tx_ringbuf, data, length);    /* 以实际入队量反馈本轮消耗的数据长度。 */

	if (atomic_set(&sh_uart->ctrl_blk->tx_busy, 1) == 0) {     /* 只有此前为空闲才需要重新开启 TX 就绪通知。 */
#ifdef CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN
		uart_irq_tx_enable(sh_uart->ctrl_blk->dev);        /* 发送 FIFO 就绪时由中断服务进一步推进队列。 */
#endif
	}
}
#endif
/**
  * @brief  按平台和传输模式输出一段 Shell 数据，并反馈本轮接收的字节数。
  * @param  transport：[输入]有效串口传输描述符。
  * @param  data：[输入]至少 length 字节，调用期间有效。
  * @param  length：[输入]请求输出的字节数。
  * @param  cnt：[输出]有效指针，记录所选路径报告的处理字节数。
  * @retval 本实现返回 0。
  * @note   可选的观察回调拿到原始指针及请求长度 length；普通中断模式实际入队长度可能更短。TX_RDY 表示可继续提交。
  */
static int write(const struct shell_transport *transport,
		 const void *data, size_t length, size_t *cnt)
{
	const struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;              /* 找到传输层绑定的串口实例。 */
	const uint8_t *data8 = (const uint8_t *)data;                                        /* 按字节访问调用方的原始数据。 */

	/* 当前板型定义 CONFIG_ACTIONS_PRINTK_DMA，发送使用炬芯输出路径；接收仍由上方 UART 中断独立处理。 */
	#ifdef CONFIG_ACTIONS_PRINTK_DMA
	*cnt = uart_dma_send_buf(data8, length);                                             /* 记录平台输出路径接受的字节数。 */
	sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_TX_RDY,                               /* 告知上层发送侧可继续处理输出请求。 */
				   sh_uart->ctrl_blk->context);
	#elif CONFIG_USB_UART_CONSOLE
	uart_usb_send(data8, length);                                                        /* 通过 USB 虚拟串口提交整个区段。 */
	*cnt = length;                                                                       /* 该分支将请求长度作为处理量报告。 */
	sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_TX_RDY,                               /* 发布 USB 路径的发送就绪事件。 */
				   sh_uart->ctrl_blk->context);
	#else
	/* 普通 UART 有软件排队和逐字节轮询两种输出方式，取决于后端配置和 blocking_tx。 */
	if (IS_ENABLED(CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN) &&
		!sh_uart->ctrl_blk->blocking_tx) {
		irq_write(sh_uart, data, length, cnt);                                       /* 将能容纳的字节入队并按需触发 TX 中断。 */
	} else {
		for (size_t i = 0; i < length; i++) {                                        /* 无队列或要求阻塞时，逐字节执行轮询输出。 */
			uart_poll_out(sh_uart->ctrl_blk->dev, data8[i]);                     /* 把当前字节交给 UART 轮询接口。 */
		}

		*cnt = length;                                                               /* 整段逐字节输出后报告请求长度。 */

		sh_uart->ctrl_blk->handler(SHELL_TRANSPORT_EVT_TX_RDY,                       /* 通知上层本次轮询写入已结束。 */
					   sh_uart->ctrl_blk->context);
	}
	#endif

	/* 向可选观察者传递本次完整输出请求。 */
	if (sh_uart->ctrl_blk->callback) {                                                   /* 检查应用是否登记了输出观察者。 */
		sh_uart->ctrl_blk->callback(data8, length, sh_uart->ctrl_blk->user_data);    /* 观察者收到原始请求指针、请求长度和自己的上下文。 */
	}

	return 0;                                                                            /* 接口状态返回成功，处理量通过 cnt 反馈。 */
}

/**
  * @brief  从 UART 后端的软件 RX 队列取出已经入队的数据。
  * @param  transport：[输入]有效串口传输描述符。
  * @param  data：[输出]至少可写 length 字节的缓冲区。
  * @param  length：[输入]本次最多取出的字节数。
  * @param  cnt：[输出]有效指针，实际读取的字节数，队列为空时为零。
  * @retval 本实现返回 0。
  * @note   Shell 的处理循环通过接口表调用此函数；读取后，相应软件队列空间可由接收端复用。
  */
static int read(const struct shell_transport *transport,
		void *data, size_t length, size_t *cnt)
{
	struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;    /* 得到持有 RX 软件队列的后端。 */

	*cnt = ring_buf_get(sh_uart->rx_ringbuf, data, length);              /* 拷出至多 length 字节，也可能读到零字节。 */

	return 0;                                                            /* 读操作完成，即使当前无数据也返回成功状态。 */
}

#ifdef CONFIG_MCUMGR_SMP_SHELL
/**
  * @brief  在线程处理阶段把就绪的管理协议输入交给 SMP 服务。
  * @param  transport：[输入]配置 SMP 的有效 UART 后端。
  * @retval 无。
  * @note   仅编译于 CONFIG_MCUMGR_SMP_SHELL。
  */
static void update(const struct shell_transport *transport)
{
	struct shell_uart *sh_uart = (struct shell_uart *)transport->ctx;    /* 通过当前实例访问它的协议接收队列。 */

	/* 管理帧从自己的缓冲队列交给 SMP，其处理与普通命令的逐字读取分开。 */
	smp_shell_process(&sh_uart->ctrl_blk->smp);                          /* 分派队列中的管理消息。 */
}
#endif    /* CONFIG_MCUMGR_SMP_SHELL */

/* 接口表把 shell_transport 的通用读写等操作绑定到本文件实现；Shell 核心通过这些函数指针访问串口。 */
const struct shell_transport_api shell_uart_transport_api = {
	.init = init,        /* 初始化并选择中断或轮询方式。 */
	.uninit = uninit,    /* 停止此后端的 I/O 事件源。 */
	.enable = enable,    /* 设置发送模式。 */
	.write = write,      /* 发送 Shell 输出。 */
	.read = read,        /* 供 Shell 消费 UART 输入队列。 */
#ifdef CONFIG_MCUMGR_SMP_SHELL
	.update = update,    /* 在线程上下文继续处理管理帧。 */
#endif                       /* CONFIG_MCUMGR_SMP_SHELL */
};

/**
  * @brief  在内核就绪后按配置绑定 UART 并初始化为串口 Shell。
  * @param  arg：[输入]系统初始化框架传入的设备参数，此入口不读取。
  * @retval UART 尚未就绪时返回 -ENODEV；调用 shell_init 后固定返回 0。
  * @note   配置的设备名决定绑定对象。此处未将 shell_init 的返回值继续上传。
  */
static int enable_shell_uart(const struct device *arg)
{
	ARG_UNUSED(arg);                                                         /* SYS_INIT 传入的设备参数不参与 UART 选择。 */
	const struct device *dev =
			device_get_binding(CONFIG_UART_SHELL_ON_DEV_NAME);       /* 用配置名从设备模型取得目标 UART。 */
	bool log_backend = CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > 0;            /* 日志级别非零时启用串口 Shell 日志后端。 */
	uint32_t level =
		(CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL > LOG_LEVEL_DBG) ?
		CONFIG_LOG_MAX_LEVEL : CONFIG_SHELL_BACKEND_SERIAL_LOG_LEVEL;    /* 超出调试级的配置取系统最大日志级别，否则沿用串口配置值。 */

	/* 没有可用 UART 时直接向初始化框架报告设备缺失。 */
	if (!device_is_ready(dev)) {
		return -ENODEV;                                                  /* 向初始化框架返回设备不可用。 */
	}

	if (IS_ENABLED(CONFIG_MCUMGR_SMP_SHELL)) {                               /* 开启复用的 SMP 管理协议时才初始化其服务。 */
		smp_shell_init();                                                /* 初始化 Shell 上的管理协议。 */
	}

	/* 把配置设备和日志等级交给框架，框架后续用 transport->api 调用本后端。 */
	shell_init(&shell_uart, dev, true, log_backend, level);                  /* 将 Shell 实例与实际传输设备一起交给框架。 */

	return 0;                                                                /* 该入口不传播 shell_init 的返回值。 */
}
/* 以配置的顺序在 POST_KERNEL 阶段登记串口 Shell 初始化入口。 */
SYS_INIT(enable_shell_uart, POST_KERNEL,
	 CONFIG_SHELL_BACKEND_SERIAL_INIT_PRIORITY);

/**
  * @brief  取得本文件静态定义的串口 Shell 实例。
  * @param  无。
  * @retval 返回指向 shell_uart 的常驻只读描述符。
  * @note   可作为 shell_execute_cmd 等接口的实例参数。
  */
const struct shell *shell_backend_uart_get_ptr(void)
{
	return &shell_uart;    /* 返回由 SHELL_DEFINE 生成的静态实例地址。 */
}

/**
  * @brief  登记用于观察串口后端输出的回调与用户指针。
  * @param  callback：[输入]输出观察函数。传 NULL 可取消回调。
  * @param  user_data：[输入]每次调用 callback 时原样传回，可为 NULL。
  * @retval 本实现返回 0。
  * @note   后端持有两个值；如果与 write 并发，调用方需考虑更新时序。
  */
int shell_uart_register_output_callback(shell_uart_callback_t callback, void *user_data)
{
	const struct shell_uart *sh_uart = shell_uart.iface->ctx;    /* 通过静态 Shell 实例的传输描述符找到对应控制块。 */

	/* 替换观察者及配套上下文；下一次 write 检查回调时可使用它们。 */
	sh_uart->ctrl_blk->callback = callback;                      /* 更新观察函数，允许传入 NULL 取消。 */
	sh_uart->ctrl_blk->user_data = user_data;                    /* 保存观察函数的用户指针。 */

	return 0;                                                    /* 登记完成后返回零。 */
}

