/*
 * Copyright (c) 2017 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 炬芯 UART 驱动。每个端口把寄存器、时钟和 IRQ 绑定到 Zephyr 设备。UART_0 中断先进入 uart_acts_isr，再由已注册的 Shell 回调读取接收 FIFO。 */


#include <kernel.h>              /* 提供内核和中断注册所用接口。 */
#include <device.h>              /* 提供设备实例注册及驱动表访问接口。 */
#include <string.h>              /* 提供配置复制与结构体清零函数。 */
#include <drivers/uart.h>        /* 提供 uart_driver_api、线路配置和回调类型。 */
#include <soc.h>                 /* 提供 SoC 时钟、复位和中断号。 */
#include <drivers/uart_dma.h>    /* 引入厂商 UART DMA 扩展接口。 */
#include <board_cfg.h>           /* 给出各端口的板级设备名和配置。 */
#ifdef CONFIG_CFG_DRV
#include <config.h>
#include <drivers/cfg_drv/driver_config.h>
#endif

#define UART_DMA_INVALID_CHAN       	(0xff)    /* 设备状态中尚未申请 DMA 通道的哨兵值。 */


/* 每个端口的只读板级配置，保存硬件资源和启动速率。 */
struct acts_uart_config {
	uint32_t base;                              /* 本端口寄存器组的基地址。 */
	const char *dma_dev_name;                   /* 从设备模型中查找 DMA 控制器时使用的名称。 */
	uint8_t txdma_id;                           /* 该端口 TX 的 DMA 外设请求编号。 */
	uint8_t txdma_chan;                         /* 板级指定的 TX DMA 通道申请参数。 */
	uint8_t rxdma_id;                           /* 该端口 RX 的 DMA 外设请求编号。 */
	uint8_t rxdma_chan;                         /* 板级指定的 RX DMA 通道申请参数。 */
	uint8_t clock_id;                           /* 初始化时打开的 UART 时钟门控编号。 */
	uint8_t reset_id;                           /* 初始化时解除的外设复位编号。 */
	uint8_t flag_use_txdma:1;                   /* 此实例是否允许使用发送 DMA。 */
	uint8_t flag_use_rxdma:1;                   /* 此实例是否允许使用接收 DMA。 */
	uint32_t baud_rate;                         /* 板级初始波特率，单位 bit/s。 */
	uart_irq_config_func_t	irq_config_func;    /* 本实例的 IRQ 连接函数。 */

};

/* 每个端口独享的运行状态，保存回调、DMA 和电源管理快照。 */
struct acts_uart_data {
	/* 设备保存的线路配置副本。 */
	struct uart_config cfg;    /* 保存当前配置以供 config_get 读回。 */

	uint32_t baud_rate;    /* 设备私有状态预留的波特率字段；本实现使用 cfg.baudrate 编程分频。 */
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	/* 由 IRQ 回调注册槽设置，ISR 使用回调和上下文分派事件。 */
	uart_irq_callback_user_data_t cb;    /* 当前登记的 IRQ 回调。 */
	void *user_data;                     /* 注册时保存的上下文，由 ISR 原样传回。 */
#endif
#ifdef CONFIG_UART_DMA_DRIVEN
    struct device *dma_dev;    /* 绑定后的 DMA 设备，收发公用。 */
	uint8_t txdma_chan;    /* 已申请的 TX 通道号。 */
	uint8_t rxdma_chan;    /* 已申请的 RX 通道号。 */
#endif
	uint32_t reg_br_bak;     /* 进入挂起前保存的波特率分频寄存器。 */
	uint32_t reg_ctl_bak;    /* 进入挂起前保存的控制寄存器。 */
};

/* 分别取本设备的只读配置、私有状态和寄存器块。 */
#define DEV_CFG(dev) \
	((const struct acts_uart_config * const)(dev)->config)
#define DEV_DATA(dev) \
	((struct acts_uart_data * const)(dev)->data)
#define UART_STRUCT(dev) \
	((struct acts_uart_controller *)(DEV_CFG(dev))->base)

/* 按寄存器顺序映射端口，volatile 保留对硬件的真实读写。 */
typedef struct acts_uart_controller {
    volatile uint32_t ctrl;    /* 偏移 0x00，线路格式、FIFO、IRQ 及 DMA 请求使能。 */

    volatile uint32_t rxdat;    /* 偏移 0x04，读取接收 FIFO 中的下一个字节。 */

    volatile uint32_t txdat;    /* 偏移 0x08，写入发送 FIFO 以便硬件移出。 */

    volatile uint32_t stat;    /* 偏移 0x0C，读取实时状态，按定义写位确认挂起或错误。 */

    volatile uint32_t br;    /* 偏移 0x10，用相同分频值设置高、低两个半字。 */
} uart_register_t;

/* CTRL、STAT 及波特率字段，寄存器位按功能分组。 */
#define UART_CTL_RX_EN              BIT(31)    /* 开启接收器。 */
#define UART_CTL_TX_EN              BIT(30)    /* 开启发送器。 */

#define UART_CTL_TX_FIFO_EN         BIT(29)    /* 开启硬件发送 FIFO。 */
#define UART_CTL_RX_FIFO_EN         BIT(28)    /* 开启硬件接收 FIFO。 */

#define UART_CTL_TX_DST_SHIFT       26                                /* 发送 FIFO 路由字段的起始位。 */
#define UART_FIFO_CPU         0                                       /* 将 FIFO 交由 CPU 读写。 */
#define UART_FIFO_DMA         1                                       /* 将 FIFO 交由 DMA 搬运。 */
#define UART_FIFO_DSP         2                                       /* DSP 路由编号，当前 FIFO 切换入口仅接受 CPU 或 DMA。 */
#define UART_CTL_TX_DST(x)          ((x) << UART_CTL_TX_DST_SHIFT)    /* 将原始编码移入发送 FIFO 路由所在位段。 */
#define UART_CTL_TX_DST_MASK        UART_CTL_TX_DST(0x3)              /* 用于屏蔽或提取发送 FIFO 路由字段。 */

#define UART_CTL_RX_SRC_SHIFT       24                                /* 接收 FIFO 路由字段的起始位。 */
#define UART_CTL_RX_SRC(x)          ((x) << UART_CTL_RX_SRC_SHIFT)    /* 将原始编码移入接收 FIFO 路由所在位段。 */
#define UART_CTL_RX_SRC_MASK        UART_CTL_RX_SRC(0x3)              /* 用于屏蔽或提取接收 FIFO 路由字段。 */


//#define UART_CTL_TX_DMA             BIT(21)
#define UART_CTL_LB_EN              BIT(20)                                             /* 串口内部环回使能位。 */
#define UART_CTL_TX_IE              BIT(19)                                             /* 发送中断使能位。 */
#define UART_CTL_RX_IE              BIT(18)                                             /* 接收中断使能位。 */
#define UART_CTL_TX_DE              BIT(17)                                             /* 发送 DMA 请求使能位。 */
#define UART_CTL_RX_DE              BIT(16)                                             /* 接收 DMA 请求使能位。 */
#define UART_CTL_EN             BIT(15)                                                 /* 控制串口整体使能。 */
//#define UART_CTL_RX_DMA             BIT(14)
#define UART_CTL_RTS_EN             BIT(13)                                             /* RTS 使能位。 */
#define UART_CTL_AF_EN              BIT(12)                                             /* 自动流控使能位。 */
#define UART_CTL_RX_FIFO_THREHOLD_SHIFT     10                                          /* 接收 FIFO 门限字段的起始位。 */
#define UART_CTL_RX_FIFO_THREHOLD(x)        ((x) << UART_CTL_RX_FIFO_THREHOLD_SHIFT)    /* 将原始编码移入接收 FIFO 门限所在位段。 */
#define UART_CTL_RX_FIFO_THREHOLD_MASK      UART_CTL_RX_FIFO_THREHOLD(0x3)              /* 用于屏蔽或提取接收 FIFO 门限字段。 */
#define     UART_CTL_RX_FIFO_THREHOLD_1BYTE     UART_CTL_RX_FIFO_THREHOLD(0x0)          /* 选择1 字节的接收 FIFO 门限设置。 */
#define     UART_CTL_RX_FIFO_THREHOLD_4BYTES    UART_CTL_RX_FIFO_THREHOLD(0x1)          /* 选择4 字节的接收 FIFO 门限设置。 */
#define     UART_CTL_RX_FIFO_THREHOLD_8BYTES    UART_CTL_RX_FIFO_THREHOLD(0x2)          /* 选择8 字节的接收 FIFO 门限设置。 */
#define     UART_CTL_RX_FIFO_THREHOLD_12BYTES   UART_CTL_RX_FIFO_THREHOLD(0x3)          /* 选择12 字节的接收 FIFO 门限设置。 */
#define UART_CTL_TX_FIFO_THREHOLD_SHIFT     8                                           /* 发送 FIFO 门限字段的起始位。 */
#define UART_CTL_TX_FIFO_THREHOLD(x)        ((x) << UART_CTL_TX_FIFO_THREHOLD_SHIFT)    /* 将原始编码移入发送 FIFO 门限所在位段。 */
#define UART_CTL_TX_FIFO_THREHOLD_MASK      UART_CTL_TX_FIFO_THREHOLD(0x3)              /* 用于屏蔽或提取发送 FIFO 门限字段。 */
#define     UART_CTL_TX_FIFO_THREHOLD_1BYTE     UART_CTL_TX_FIFO_THREHOLD(0x0)          /* 选择1 字节的发送 FIFO 门限设置。 */
#define     UART_CTL_TX_FIFO_THREHOLD_4BYTES    UART_CTL_TX_FIFO_THREHOLD(0x1)          /* 选择4 字节的发送 FIFO 门限设置。 */
#define     UART_CTL_TX_FIFO_THREHOLD_8BYTES    UART_CTL_TX_FIFO_THREHOLD(0x2)          /* 选择8 字节的发送 FIFO 门限设置。 */
#define     UART_CTL_TX_FIFO_THREHOLD_12BYTES   UART_CTL_TX_FIFO_THREHOLD(0x3)          /* 选择12 字节的发送 FIFO 门限设置。 */
//#define UART_CTL_CTS_EN             BIT(7)
#define UART_CTL_PARITY_SHIFT           4                                               /* 校验方式字段的起始位。 */
#define UART_CTL_PARITY(x)          ((x) << UART_CTL_PARITY_SHIFT)                      /* 将原始编码移入校验方式所在位段。 */
#define UART_CTL_PARITY_MASK            UART_CTL_PARITY(0x7)                            /* 用于屏蔽或提取校验方式字段。 */
#define     UART_CTL_PARITY_NONE            UART_CTL_PARITY(0x0)                        /* 关闭校验位。 */
#define     UART_CTL_PARITY_ODD         UART_CTL_PARITY(0x4)                            /* 使用奇校验。 */
#define     UART_CTL_PARITY_LOGIC_1         UART_CTL_PARITY(0x5)                        /* 校验位固定高。 */
#define     UART_CTL_PARITY_EVEN            UART_CTL_PARITY(0x6)                        /* 使用偶校验。 */
#define     UART_CTL_PARITY_LOGIC_0         UART_CTL_PARITY(0x7)                        /* 校验位固定低。 */
#define UART_CTL_STOP_SHIFT         2                                                   /* 停止位字段的起始位。 */
#define UART_CTL_STOP(x)            ((x) << UART_CTL_STOP_SHIFT)                        /* 将原始编码移入停止位所在位段。 */
#define UART_CTL_STOP_MASK          UART_CTL_STOP(0x1)                                  /* 用于屏蔽或提取停止位字段。 */
#define     UART_CTL_STOP_1BIT          UART_CTL_STOP(0x0)                              /* 选择1 位的停止位设置。 */
#define     UART_CTL_STOP_2BIT          UART_CTL_STOP(0x1)                              /* 选择2 位的停止位设置。 */
#define UART_CTL_DATA_WIDTH_SHIFT       0                                               /* 有效数据位宽字段的起始位。 */
#define UART_CTL_DATA_WIDTH(x)          ((x) << UART_CTL_DATA_WIDTH_SHIFT)              /* 将原始编码移入有效数据位宽所在位段。 */
#define UART_CTL_DATA_WIDTH_MASK        UART_CTL_DATA_WIDTH(0x3)                        /* 用于屏蔽或提取有效数据位宽字段。 */
#define     UART_CTL_DATA_WIDTH_5BIT        UART_CTL_DATA_WIDTH(0x0)                    /* 选择5 位的有效数据位宽设置。 */
#define     UART_CTL_DATA_WIDTH_6BIT        UART_CTL_DATA_WIDTH(0x1)                    /* 选择6 位的有效数据位宽设置。 */
#define     UART_CTL_DATA_WIDTH_7BIT        UART_CTL_DATA_WIDTH(0x2)                    /* 选择7 位的有效数据位宽设置。 */
#define     UART_CTL_DATA_WIDTH_8BIT        UART_CTL_DATA_WIDTH(0x3)                    /* 选择8 位的有效数据位宽设置。 */

#define UART_STA_PAER               BIT(23)                          /* 奇偶校验错误状态位。 */
#define UART_STA_STER               BIT(22)                          /* 映射到 UART_ERROR_FRAMING 的停止位错误。 */
#define UART_STA_UTBB                   BIT(21)                      /* 初始化时据此等待发送硬件不再忙。 */
#define UART_STA_TXFL_SHIFT         16                               /* 发送 FIFO 占用量字段的起始位。 */
#define UART_STA_TXFL_MASK          (0x1f << UART_STA_TXFL_SHIFT)    /* 用于屏蔽或提取发送 FIFO 占用量字段。 */
#define UART_STA_RXFL_SHIFT         11                               /* 接收 FIFO 占用量字段的起始位。 */
#define UART_STA_RXFL_MASK          (0x1f << UART_STA_RXFL_SHIFT)    /* 用于屏蔽或提取接收 FIFO 占用量字段。 */
#define UART_STA_TFES                   BIT(10)                      /* TX FIFO 为空。 */
#define UART_STA_RFFS               BIT(9)                           /* 接收 FIFO 满标志。 */
#define UART_STA_RTSS               BIT(8)                           /* RTS 线路状态。 */
#define UART_STA_CTSS               BIT(7)                           /* CTS 线路状态。 */
#define UART_STA_TFFU               BIT(6)                           /* TX FIFO 已满，不能继续填字节。 */
#define UART_STA_RFEM               BIT(5)                           /* RX FIFO 为空，不能继续取字节。 */
#define UART_STA_RXST               BIT(4)                           /* 接收状态标志，本驱动将其纳入批量清除的掩码。 */
#define UART_STA_TFER               BIT(3)                           /* 本驱动映射为溢出错误的状态位。 */
#define UART_STA_RXER               BIT(2)                           /* 归入接收帧错误的状态位。 */
#define UART_STA_TIP                BIT(1)                           /* 发送中断挂起，向该位写一确认。 */
#define UART_STA_RIP                BIT(0)                           /* 接收中断挂起，向该位写一确认。 */

#define UART_STA_ERRS               (UART_STA_RXER | UART_STA_TFER | \
                         UART_STA_RXST | UART_STA_PAER | \
                         UART_STA_STER)

/* 按四舍五入求 UART 分频，保持接收和发送两个半字使用同一速率。 */
#define UART_BAUD_DIV(freq, baud_rate)  (((freq) / (baud_rate))) + ((((freq) % (baud_rate)) + ((baud_rate) >> 1)) / baud_rate)    /* 用输入时钟频率和目标波特率求整数分频，余数达到半个分母时向上取整。 */



/**
  * @brief  将缓存的线路配置写入控制器，并清除旧的错误及接收数据。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 无。
  * @note   波特率必须非零；本实现只把数据位编码低两位写入相应字段，停止位只区分两位与一位。
  */
static void uart_acts_init_bycfg(const struct device *dev)
{
	struct acts_uart_data * dev_data = DEV_DATA(dev);                                  /* 取得当前实例私有的运行数据。 */
	struct acts_uart_controller *uart = UART_STRUCT(dev);                              /* 通过设备实例的基址访问这个端口的寄存器。 */
	struct uart_config *cfg = &dev_data->cfg;                                          /* 定位设备缓存的线路参数。 */
	unsigned int div, ctl, ctl_mask;                                                   /* 分频值、当前需要的格式位和对应掩码。 */
	unsigned int rx_data;                                                              /* 接收旧字节的丢弃槽，读取 rxdat 本身会取走数据。 */
	int try_cnt = 0;                                                                   /* 限制清空 RX FIFO 时读取寄存器的次数。 */


	ctl = UART_CTL_DATA_WIDTH((cfg->data_bits&0x3));                                   /* 将数据位枚举的低两位移入 CTRL 格式字段。 */

	if(cfg->stop_bits== UART_CFG_STOP_BITS_2)                                          /* 该驱动只把值为“两位”的配置映射为双停止位，其余使用一位。 */
		ctl |= UART_CTL_STOP_2BIT;                                                 /* 选中该分支对应的帧格式硬件编码。 */
	else
		ctl |= UART_CTL_STOP_1BIT;                                                 /* 选中该分支对应的帧格式硬件编码。 */

	switch(cfg->parity){                                                               /* 将 SDK 的校验枚举逐项映射为硬件 CTRL 编码。 */
		case UART_CFG_PARITY_NONE:
			ctl |= UART_CTL_PARITY_NONE;                                       /* 选中该分支对应的帧格式硬件编码。 */
			break;
		case UART_CFG_PARITY_ODD:
			ctl |= UART_CTL_PARITY_ODD;                                        /* 选中该分支对应的帧格式硬件编码。 */
			break;
		case UART_CFG_PARITY_EVEN:
			ctl |= UART_CTL_PARITY_EVEN;                                       /* 选中该分支对应的帧格式硬件编码。 */
			break;
		case UART_CFG_PARITY_MARK:
			ctl |= UART_CTL_PARITY_LOGIC_1;                                    /* 选中该分支对应的帧格式硬件编码。 */
			break;
		case UART_CFG_PARITY_SPACE:
			ctl |= UART_CTL_PARITY_LOGIC_0;                                    /* 选中该分支对应的帧格式硬件编码。 */
			break;
	}
	ctl_mask = UART_CTL_PARITY_MASK | UART_CTL_STOP_MASK |UART_CTL_DATA_WIDTH_MASK;    /* 合并本次允许改动的线路格式位。 */

	uart->ctrl = (uart->ctrl&(~ctl_mask)) | ctl;                                       /* 只替换格式字段，保留原有 FIFO 及中断开关。 */

	uart->ctrl &= ~UART_CTL_EN;                                                        /* 先关闭 UART 再写分频，保留原代码对 2 Mbit/s 设置的处理顺序。 */
	div = UART_BAUD_DIV(CONFIG_HOSC_CLK_MHZ * 1000000,cfg->baudrate);                  /* 将高速晶振 MHz 换成 Hz 后计算最接近的分频。 */
	uart->br = (div | (div << 16));                                                    /* 把同一分频装入 BR 的两个半字。 */
	uart->ctrl |= UART_CTL_EN;                                                         /* 新分频写入后重新启用 UART。 */

	/* 向状态寄存器写错误位组，清除已锁存的错误。 */
	uart->stat = UART_STA_ERRS;                                                        /* 按错误组掩码写一确认已有错误记录。 */

    /* 有界排空旧格式下已收到的字节，避免混入新配置的后续输入。 */
	while (!(uart->stat & UART_STA_RFEM) && (try_cnt++ < 16)) {                        /* FIFO 为空或已达到 16 次读取上限时退出。 */
		rx_data = uart->rxdat;                                                     /* 读取即消耗旧数据，随后不再使用该值。 */
	}
}


/**
  * @brief  复制新的线路配置并立即重新编程 UART。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  cfg：[输入] 非空的波特率及帧格式配置。
  * @retval 固定返回 0。
  * @note   重写分频时会短暂关闭端口并排空旧 RX 数据，运行中修改前应协调并发使用者。
  */
static int uart_acts_set_config(const struct device *dev, const struct uart_config *cfg)
{
	struct acts_uart_data * dev_data = DEV_DATA(dev);    /* 取得当前实例私有的运行数据。 */
	struct uart_config *dcfg = &dev_data->cfg;           /* 定位设备缓存的线路参数。 */
	memcpy(dcfg, cfg, sizeof(struct uart_config));       /* 更新 RAM 中供读回的软件配置。 */
	uart_acts_init_bycfg(dev);                           /* 把新线路格式与分频写到寄存器。 */
	return 0;
}

/**
  * @brief  读回设备保存的软件线路配置。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  cfg：[输出] 可写的 uart_config，接收设备保存的线路配置。
  * @retval 固定返回 0。
  * @note   返回最近一次保存的配置，未逐项回读寄存器。
  */
static int  uart_acts_get_config(const struct device *dev, struct uart_config *cfg)
{
	struct acts_uart_data * dev_data = DEV_DATA(dev);    /* 取得当前实例私有的运行数据。 */
	struct uart_config *dcfg = &dev_data->cfg;           /* 定位设备缓存的线路参数。 */
	memcpy(cfg , dcfg, sizeof(struct uart_config));      /* 把完整配置拷出到调用者提供的存储。 */
	return 0;
}



/**
  * @brief  忙等接收 FIFO 有数据后取走一个字节。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  c：[输出] 非空、可写的单字节接收位置。
  * @retval 读到后返回 0。
  * @note   本实现没有超时。Shell 的中断接收路径使用 fifo_read。
  */
static int uart_acts_poll_in(const struct device *dev, unsigned char *c)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	/* 等待接收 FIFO 出现字符。 */
	while (uart->stat & UART_STA_RFEM)                       /* RX FIFO 尚空时持续等待，本实现没有空队列立即返回的路径。 */
		;

	/* RX FIFO 已有字节，从数据端口读取后交给调用者。 */
	*c = (unsigned char)uart->rxdat;    /* 取走一个 FIFO 元素，只交付低八位。 */

	return 0;
}

/**
  * @brief  等待发送 FIFO 出现空间后写入一个字节。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  c：[输入] 要写入发送 FIFO 的字节。
  * @retval 无。
  * @note   无超时；写入后即可返回，最后一位仍可在发送线上移出。
  */
static void uart_acts_poll_out(const struct device *dev,
					     unsigned char c)
{

	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	/* 等待发送 FIFO 出现可写空间。 */
	while (uart->stat & UART_STA_TFFU);                      /* TX FIFO 满时忙等，此路径未设置超时。 */

	/* 将字符写入发送数据端口，后续由硬件移出。 */
	uart->txdat = (uint32_t)c;                               /* 送入一个字节后返回，上层不在这里等待线路发送结束。 */



}

/* 启用中断驱动接口时，以下函数通过 FIFO 出入字节，并由上层 IRQ 回调分派。 */
#ifdef CONFIG_UART_INTERRUPT_DRIVEN

/**
  * @brief  将状态寄存器中检测到的线路错误合成为公共错误位图。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 0 表示没有映射到错误位；正值为 UART_ERROR_* 的组合。
  * @note   读取后写错误位组清除锁存；溢出分支原表达式的两个操作数均为 TFER。
  */
static int uart_acts_err_check(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);        /* 通过设备实例的基址访问这个端口的寄存器。 */
	uint32_t error = 0;                                          /* 从全零位图开始汇总本次发现的错误。 */

	if (uart->stat & UART_STA_ERRS) {
		if (uart->stat & (UART_STA_TFER | UART_STA_TFER))    /* 两个掩码操作数都是 TFER，本表达式实际只测试这一位。 */
			error |= UART_ERROR_OVERRUN;                 /* 把相应错误加入返回给调用者的状态位图。 */

		if (uart->stat & (UART_STA_STER | UART_STA_RXER))
			error |= UART_ERROR_FRAMING;                 /* 把相应错误加入返回给调用者的状态位图。 */

		if (uart->stat & UART_STA_PAER)
			error |= UART_ERROR_PARITY;                  /* 把相应错误加入返回给调用者的状态位图。 */

		/* 向状态寄存器写错误位组，清除已锁存的错误。 */
		uart->stat = UART_STA_ERRS;                          /* 按错误组掩码写一确认已有错误记录。 */
	}

	return error;                                                /* 返回公共 UART 错误位的组合。 */
}

/**
  * @brief  把调用者的发送区尽可能多地装入硬件 TX FIFO。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  tx_data：[输入] 至少 len 字节的可读发送区。
  * @param  len：[输入] 要搬运的字节数。
  * @retval 实际写入的字节数，FIFO 已满时可为 0。
  * @note   IRQ 路径在 FIFO 已满或数据全部写入时停止，随后确认 TX 挂起位。
  */
static int uart_acts_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);            /* 通过设备实例的基址访问这个端口的寄存器。 */
	uint8_t num_tx = 0;                                              /* 本次成功装入 FIFO 的字节数。 */

	while ((len - num_tx > 0) && !(uart->stat & UART_STA_TFFU)) {    /* 遇到请求长度已满足或 FIFO 满即停止。 */
		/* 取下一个待发字节写入硬件 TX FIFO。 */
		uart->txdat = (uint8_t)tx_data[num_tx++];
	}

	/* 确认本方向已处理的中断挂起位。 */
	uart->stat = UART_STA_TIP;                                       /* 向状态位写 TIP 确认发送中断。 */

	return (int)num_tx;                                              /* 向上层返回实际消耗的发送字节数。 */
}

/**
  * @brief  将 RX FIFO 中已有的数据搬到调用者缓冲，随后确认 RX 中断。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  rx_data：[输出] 至少 size 字节的可写接收区。
  * @param  size：[输入] 此次接收缓冲的字节容量。
  * @retval 实际读取的字节数，FIFO 已空时可为 0。
  * @note   UART_0 的 Shell 中断先预留 RX 软件环的一段空间，把地址通过 uart_fifo_read 传到这里，再按返回数量发布给线程。
  */
static int uart_acts_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);             /* 通过设备实例的基址访问这个端口的寄存器。 */
	uint8_t num_rx = 0;                                               /* 本次实际从硬件取出的字节数。 */

	while ((size - num_rx > 0) && !(uart->stat & UART_STA_RFEM)) {    /* 按实际 FIFO 状态读到空或达到目的缓冲的容量为止。 */
		/* 从接收寄存器取走一个字节并推进目标区下标。 */
		rx_data[num_rx++] = (uint8_t)uart->rxdat;                 /* 从 RXDAT 取走低八位，写到软件队列已预留的位置。 */
	}

	/* 确认本方向已处理的中断挂起位。 */
	uart->stat = UART_STA_RIP;                                        /* 本轮搬运后向 RIP 写一确认接收中断。 */

	return num_rx;                                                    /* 让上层按实际长度发布软件队列中的新字节。 */
}

/**
  * @brief  使能 TX FIFO 中断源。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 无。
  */
static void uart_acts_irq_tx_enable(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	uart->ctrl |= UART_CTL_TX_IE;                            /* 置位 TX 中断使能。 */
}

/**
  * @brief  关闭 TX FIFO 中断源。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 无。
  */
static void uart_acts_irq_tx_disable(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	uart->ctrl &= ~UART_CTL_TX_IE;                           /* 清除 TX 中断使能。 */
}

/**
  * @brief  判断 TX FIFO 是否还有空间接受字节。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 1 表示可写，0 表示 FIFO 已满。
  */
static int uart_acts_irq_tx_ready(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	return !(uart->stat & UART_STA_TFFU);                    /* FIFO 未满即可接受更多数据。 */
}

/**
  * @brief  清除旧 RX 挂起状态，再允许接收中断。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 无。
  */
static void uart_acts_irq_rx_enable(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	/* 确认本方向已处理的中断挂起位。 */
	uart->stat = UART_STA_RIP;                               /* 打开接收中断前先确认旧挂起位。 */
	uart->ctrl |= UART_CTL_RX_IE;                            /* 允许下次接收事件触发 CPU 中断。 */
}

/**
  * @brief  屏蔽 UART 接收中断。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 无。
  */
static void uart_acts_irq_rx_disable(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	uart->ctrl &= ~UART_CTL_RX_IE;                           /* 关闭 RX 通知，保留其他控制位。 */
}

/**
  * @brief  读取 TX FIFO 空状态供通用接口查询。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 置位的 TFES 掩码表示 TX FIFO 空，0 表示非空。
  * @note   本函数仅检查 TFES；初始化等待旧线路停稳时还会查看 UTBB。
  */
static int uart_acts_irq_tx_complete(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	return (uart->stat & UART_STA_TFES);                     /* 原样返回 TX FIFO 空位的掩码值。 */
}

/**
  * @brief  判断硬件 RX FIFO 中有无字符可读。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 1 表示可读，0 表示为空。
  */
static int uart_acts_irq_rx_ready(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

	return !(uart->stat & UART_STA_RFEM);                    /* RX FIFO 非空时返回真。 */
}

/**
  * @brief  结合收发中断使能和挂起标志判断本端口是否待处理。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 1 表示至少一路中断待处理，0 表示均无。
  */
static int uart_acts_irq_is_pending(const struct device *dev)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);                         /* 通过设备实例的基址访问这个端口的寄存器。 */
	int tx_pending, rx_pending;                                                   /* 分别保存经使能位筛选后的发送、接收挂起状态。 */

	tx_pending = (uart->ctrl & UART_CTL_TX_IE) && (uart->stat & UART_STA_TIP);    /* 发送中断已使能且 TIP 挂起才算 TX 待处理。 */
	rx_pending = (uart->ctrl & UART_CTL_RX_IE) && (uart->stat & UART_STA_RIP);    /* 接收中断已使能且 RIP 挂起才算 RX 待处理。 */

	return (tx_pending || rx_pending);                                            /* 任一方向有待处理事件即提示上层继续检查。 */
}

/**
  * @brief  实现通用 IRQ 入口的状态更新钩子。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 固定返回 1。
  * @note   本设备从寄存器实时查询状态，无须在此缓存。
  */
static int uart_acts_irq_update(const struct device *dev)
{
	return 1;    /* 本控制器的 IRQ 状态可直接读取，通用更新钩子报告就绪。 */
}

/**
  * @brief  在设备私有数据中保存或替换 IRQ 回调及上下文。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  cb：[输入] IRQ 要调用的处理函数，NULL 表示不再转调。
  * @param  user_data：[输入] 供 cb 使用的调用者上下文，可为空。
  * @retval 无。
  * @note   回调在硬件 ISR 中执行，已登记的上下文须一直有效到回调解除。
  */
static void uart_acts_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb, void *user_data)
{
	struct acts_uart_data * const dev_data = DEV_DATA(dev);    /* 取得当前实例私有的运行数据。 */

	dev_data->cb = cb;                                         /* 保存本实例将执行的用户函数。 */
	dev_data->user_data = user_data;                           /* 与回调一起保存原上下文地址。 */
}

/**
  * @brief  响应指定端口的硬件中断，清错误锁存后分派到已登记的上层回调。
  * @param  arg：[输入] IRQ_CONNECT 登记的端口设备指针。
  * @retval 无。
  * @note   Shell 的 uart_callback 在此上下文内读取 FIFO；后续命令解析由 Shell 线程进行。
  */
void uart_acts_isr(void *arg)
{
	struct device *dev = arg;                                  /* 还原 IRQ_CONNECT 注册的具体设备。 */
	struct acts_uart_data * const dev_data = DEV_DATA(dev);    /* 取得当前实例私有的运行数据。 */
	struct acts_uart_controller *uart = UART_STRUCT(dev);      /* 通过设备实例的基址访问这个端口的寄存器。 */

	/* 向状态寄存器写错误位组，清除已锁存的错误。 */
	uart->stat = UART_STA_ERRS;                                /* 按错误组掩码写一确认已有错误记录。 */

	if (dev_data->cb) {                                        /* 尚未注册回调时只执行前面的错误确认。 */
		dev_data->cb(dev, dev_data->user_data);            /* 在中断上下文分派；UART_0 的串口后端从此进入 uart_callback。 */
	}
}
#endif    /* CONFIG_UART_INTERRUPT_DRIVEN */

/* 启用厂商 DMA 扩展时，下列函数提供 UART 的单独 DMA 通路。 */
#ifdef CONFIG_UART_DMA_DRIVEN

#include <drivers/dma.h>

/**
  * @brief  在 CPU 和 DMA 之间切换指定方向的 FIFO 访问方。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  is_tx：[输入] 非零选择发送，零选择接收。
  * @param  fifo_type：[输入] UART_FIFO_CPU 或 UART_FIFO_DMA。
  * @retval 0 为切换成功，-EINVAL 表示 fifo_type 超出支持的范围。
  * @note   切换 TX 前忙等发送完成；配置该方向阈值和 DMA 请求时先关闭原中断源。
  */
int uart_acts_fifo_switch(struct device *dev, uint32_t is_tx, uint32_t fifo_type)
{
    int tmp;                                                                                                    /* 先在临时控制字中构造完整设置，最后一次写回。 */

    struct acts_uart_controller *uart = UART_STRUCT(dev);                                                       /* 通过设备实例的基址访问这个端口的寄存器。 */

    tmp = uart->ctrl;                                                                                           /* 保存其他控制位的现值。 */

    if(is_tx){                                                                                                  /* 选择 TX 和 RX 各自的 FIFO 路由字段。 */
        if(fifo_type > UART_FIFO_DMA){
            return -EINVAL;                                                                                     /* 指定的路由超过该入口支持的范围。 */
        }
		while(!uart_irq_tx_complete(dev)){}                                                             /* 切换发送路由前等待 TX FIFO 空。 */
        /* 切换所有者前先清除旧的发送使能和阈值。 */
        tmp &= (~(UART_CTL_TX_DST_MASK | UART_CTL_TX_FIFO_THREHOLD_MASK | UART_CTL_TX_DE | UART_CTL_TX_IE));    /* 移除旧的 TX 路由、阈值、DMA 请求和 TX 中断使能。 */

        tmp |=  UART_CTL_TX_DST(fifo_type);                                                                     /* 选择由 CPU 或 DMA 给发送 FIFO 填数。 */

        if(fifo_type == UART_FIFO_DMA){
            tmp |= UART_CTL_TX_FIFO_THREHOLD_8BYTES | UART_CTL_TX_DE;                                           /* DMA 写 TX 时选择八字节门限并开放外设请求。 */
        }else{
            tmp |= UART_CTL_TX_FIFO_THREHOLD_1BYTE;                                                             /* CPU 写 TX 时使用一字节门限。 */
        }
    }else{
        if(fifo_type > UART_FIFO_DMA){
            return -EINVAL;                                                                                     /* 指定的路由超过该入口支持的范围。 */
        }

        /* 先解除旧的接收请求。 */
        tmp &= (~(UART_CTL_RX_SRC_MASK | UART_CTL_RX_FIFO_THREHOLD_MASK | UART_CTL_RX_DE | UART_CTL_RX_IE));    /* 移除旧的 RX 路由、门限及 DMA 和中断使能。 */

        tmp |=  UART_CTL_RX_SRC(fifo_type);                                                                     /* 将 RX FIFO 连接到所选读取端。 */

        if(fifo_type == UART_FIFO_DMA){
            /* DMA 模式要同时允许外设发出请求。 */
            tmp |= UART_CTL_RX_FIFO_THREHOLD_1BYTE | UART_CTL_RX_DE;                                            /* RX DMA 使用一字节阈值并开放接收请求。 */
        }else{
            tmp |= UART_CTL_RX_FIFO_THREHOLD_1BYTE;                                                             /* CPU 收数时保留一字节门限。 */
        }
    }

    uart->ctrl = tmp;                                                                                           /* 将已经组合好的路由设置一次写入 CTRL。 */

    return 0;
}

/**
  * @brief  查找 DMA 设备，申请发送通道并提交内存到 UART 的初始配置。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  callback：[输入] DMA 完成回调，可为空。
  * @param  arg：[输入] 随 DMA 完成回调传回的调用者上下文，可为空。
  * @retval 未启用板级 TX DMA 或绑定 DMA 设备失败返回 -1，走到配置结束返回 0。
  * @note   实际发送地址及长度在 send 时通过 dma_reload 设置，本层未传回 dma_config 结果。
  */
int uart_acts_dma_send_init(struct device *dev, dma_callback_t callback, void *arg)
{
    struct dma_config dma_cfg;                                                                       /* 暂存外设请求号、方向和回调参数。 */
    struct dma_block_config head_block;                                                              /* 准备通道的首个块配置。 */
    struct acts_uart_data *uart_data = DEV_DATA(dev);                                                /* 取得当前实例私有的运行数据。 */
    struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);                  /* 定位端口的板级资源配置。 */

	if(!uart_config->flag_use_txdma){                                                            /* 先确认本端口配置允许使用所选方向的 DMA。 */
		printk("err:dma not config by dts\n");
		return -1;                                                                           /* 缺少本方向需要的 DMA 配置、设备或通道。 */
	}


	if(!uart_data->dma_dev){                                                                     /* 检查 DMA 控制器是否已绑定。 */
	    uart_data->dma_dev = (struct device *)device_get_binding(uart_config->dma_dev_name);     /* 按板级配置的设备名查找 DMA 控制器。 */
		if (!uart_data->dma_dev){                                                            /* 检查 DMA 控制器是否已绑定。 */
			printk("err:get dma(%s) dev fail\n", uart_config->dma_dev_name);
			return -1;                                                                   /* 缺少本方向需要的 DMA 配置、设备或通道。 */
		}
	}
	if(uart_data->txdma_chan == UART_DMA_INVALID_CHAN){                                          /* 检查此方向是否已保存合法通道号。 */
		uart_data->txdma_chan = dma_request(uart_data->dma_dev, uart_config->txdma_chan);    /* 按板级指定参数申请并保存此方向的通道。 */
		printk("uart use txdma chan=%d\n", uart_data->txdma_chan);
	}


    memset(&dma_cfg, 0, sizeof(dma_cfg));                                                            /* 清零此描述，以确定未显式设置的字段。 */
    memset(&head_block, 0, sizeof(head_block));                                                      /* 清零此描述，以确定未显式设置的字段。 */

	if (callback) {                                                                              /* 只在调用者需要完成通知时配置回调。 */
		dma_cfg.dma_callback = callback;                                                     /* 保存 DMA 完成后要调用的处理函数。 */
		dma_cfg.user_data = arg;                                                             /* 将调用者上下文和回调成对传递。 */
		dma_cfg.complete_callback_en = 1;                                                    /* 允许完成事件通知调用者。 */
	}

	head_block.source_address = (unsigned int)NULL;                                              /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */
	head_block.dest_address = (unsigned int)NULL;                                                /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */
	head_block.block_size = 0;                                                                   /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */
	head_block.source_reload_en = 0;                                                             /* 禁止块结束后重新装入源地址。 */

	dma_cfg.block_count = 1;                                                                     /* 发送以一个块建立初始 DMA 配置。 */
	dma_cfg.head_block = &head_block;                                                            /* 把首块描述交给通道配置入口。 */
	head_block.block_size = 0;                                                                   /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */

	dma_cfg.dma_slot = uart_config->txdma_id;                                                    /* 选择本 UART 对应方向的 DMA 外设请求。 */
	dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;                                            /* 明确由内存向 UART 搬运。 */
	dma_cfg.dest_data_size = 1;                                                                  /* 目标 UART 数据端口按字节单位配置。 */
    dma_config(uart_data->dma_dev, uart_data->txdma_chan, &dma_cfg);                                 /* 提交通道初始配置，此处没有检查底层返回状态。 */

    return 0;
}
/**
  * @brief  释放已申请的发送 DMA 通道并恢复无效标志。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 未初始化或通道无效时返回 -1，执行释放后返回 0。
  */
int uart_acts_dma_send_exit(struct device *dev)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);          /* 取得当前实例私有的运行数据。 */

	if(!uart_data->dma_dev){                               /* 检查 DMA 控制器是否已绑定。 */
		printk("err:no uart dma inited\n");
		return -1;                                     /* 缺少本方向需要的 DMA 配置、设备或通道。 */
	}

	if(uart_data->txdma_chan == UART_DMA_INVALID_CHAN){    /* 检查此方向是否已保存合法通道号。 */
		printk("err:tx no uart dma channel\n");
		return -1;                                     /* 缺少本方向需要的 DMA 配置、设备或通道。 */
	}
    dma_free(uart_data->dma_dev, uart_data->txdma_chan);       /* 把不再使用的通道归还给 DMA 驱动。 */
    printk("free uart txdma chan=%d\n", uart_data->txdma_chan);
	uart_data->txdma_chan = UART_DMA_INVALID_CHAN;         /* 保存尚未持有通道的哨兵，供下次初始化检查。 */
    return 0;
}


/**
  * @brief  把内存数据区和 UART 发送端口交给 DMA，并启动传输。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  s：[输入] 至少 len 字节且搬运期间可读的发送区。
  * @param  len：[输入] 要搬运的字节数。
  * @retval 固定返回 0，未转交 dma_reload、dma_start 的返回码。
  * @note   要求已初始化发送通道，DMA 使用源缓冲期间调用者须保持数据有效。
  */
int uart_acts_dma_send(struct device *dev, char *s, int len)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);                                                              /* 取得当前实例私有的运行数据。 */
   // struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);
	struct acts_uart_controller *uart = UART_STRUCT(dev);                                                      /* 通过设备实例的基址访问这个端口的寄存器。 */
    __ASSERT(uart_data->dma_dev, "uart dma dev not config\n");                                                     /* 断言 DMA 设备已经绑定。 */

    dma_reload(uart_data->dma_dev, uart_data->txdma_chan, (unsigned int) s, (unsigned int)(&uart->txdat), len);    /* 把 s 作为源、TXDAT 作为目的端口，同时装入本次字节数。 */
    dma_start(uart_data->dma_dev, uart_data->txdma_chan);                                                          /* 启动 DMA，从缓冲向 TX FIFO 搬运。 */

    return 0;
}

/**
  * @brief  从发送 DMA 通道状态查询剩余长度。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 剩余长度为 0 返回 1，否则返回 0。
  * @note   读取的是 DMA 进度；线路发完还取决于 UART 自身。
  */
int uart_acts_dma_send_complete(struct device *dev)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);                        /* 取得当前实例私有的运行数据。 */
    //struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);
	struct dma_status stat;                                              /* 接收通道的运行状态与剩余长度快照。 */

    __ASSERT(uart_data->dma_dev, "uart dma dev not config\n");               /* 断言 DMA 设备已经绑定。 */
	dma_get_status(uart_data->dma_dev, uart_data->txdma_chan, &stat);    /* 采样此方向的 DMA 状态，后续使用其中的剩余字节数。 */
	if(stat.pending_length==0)                                           /* 剩余量为零表示该通道报告已搬完。 */
		return 1;
    return 0 ;
}

/**
  * @brief  向 DMA 驱动发出停止发送请求。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 固定返回 0，未转交停止结果。
  */
int uart_acts_dma_send_stop(struct device *dev)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);       /* 取得当前实例私有的运行数据。 */
   // struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);
    dma_stop(uart_data->dma_dev, uart_data->txdma_chan);    /* 向 DMA 控制器发出停止发送通道的请求。 */
    return 0;
}

/**
  * @brief  查找 DMA 设备并准备从 UART 向内存搬运的接收通道。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  callback：[输入] DMA 完成回调，可为空。
  * @param  arg：[输入] 随 DMA 完成回调传回的调用者上下文，可为空。
  * @retval 板级未启用 RX DMA 或 DMA 设备绑定失败返回 -1，执行配置后返回 0。
  * @note   这里传递的原始 block_count 和 dest_data_size 都为 200，具体由 DMA 后端解释。
  */
int uart_acts_dma_receive_init(struct device *dev, dma_callback_t callback, void *arg)
{
    struct dma_config dma_cfg;                                                                      /* 暂存外设请求号、方向和回调参数。 */
    struct dma_block_config head_block;                                                             /* 准备通道的首个块配置。 */
    struct acts_uart_data *uart_data = DEV_DATA(dev);                                               /* 取得当前实例私有的运行数据。 */
    struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);                 /* 定位端口的板级资源配置。 */

	if(!uart_config->flag_use_rxdma){                                                           /* 先确认本端口配置允许使用所选方向的 DMA。 */
		printk("err:dma rx not config by dts\n");
		return -1;                                                                          /* 缺少本方向需要的 DMA 配置、设备或通道。 */
	}

	if(!uart_data->dma_dev){                                                                    /* 检查 DMA 控制器是否已绑定。 */
	    uart_data->dma_dev = (struct device *)device_get_binding(uart_config->dma_dev_name);    /* 按板级配置的设备名查找 DMA 控制器。 */
		if (!uart_data->dma_dev){                                                           /* 检查 DMA 控制器是否已绑定。 */
			printk("err:get dma(%s) dev fail\n", uart_config->dma_dev_name);
			return -1;                                                                  /* 缺少本方向需要的 DMA 配置、设备或通道。 */
		}
	}
	if(uart_data->rxdma_chan == UART_DMA_INVALID_CHAN)                                          /* 检查此方向是否已保存合法通道号。 */
        /* 按板级指定参数申请并保存此方向的通道。 */
    	uart_data->rxdma_chan = dma_request(uart_data->dma_dev, uart_config->rxdma_chan);
    printk("uart use rxdma chan=%d\n", uart_data->rxdma_chan);

    memset(&dma_cfg, 0, sizeof(dma_cfg));                                                           /* 清零此描述，以确定未显式设置的字段。 */
    memset(&head_block, 0, sizeof(head_block));                                                     /* 清零此描述，以确定未显式设置的字段。 */

	if (callback) {                                                                             /* 只在调用者需要完成通知时配置回调。 */
		dma_cfg.dma_callback = callback;                                                    /* 保存 DMA 完成后要调用的处理函数。 */
		dma_cfg.user_data = arg;                                                            /* 将调用者上下文和回调成对传递。 */
		dma_cfg.complete_callback_en = 1;                                                   /* 允许完成事件通知调用者。 */
	}

	head_block.source_address = (unsigned int)NULL;                                             /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */
	head_block.dest_address = (unsigned int)NULL;                                               /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */
	head_block.block_size = 0;                                                                  /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */
	head_block.source_reload_en = 0;                                                            /* 禁止块结束后重新装入源地址。 */

	dma_cfg.block_count = 200;                                                                  /* 接收配置此处写入的块数为 200。 */
	dma_cfg.head_block = &head_block;                                                           /* 把首块描述交给通道配置入口。 */
	head_block.block_size = 0;                                                                  /* 初始化时置零，实际缓冲和字节数在提交传输时装入。 */

	dma_cfg.dma_slot = uart_config->rxdma_id;                                                   /* 选择本 UART 对应方向的 DMA 外设请求。 */
	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;                                           /* 明确由 UART 向内存搬运。 */
	dma_cfg.dest_data_size = 200;                                                               /* 接收配置将目的数据大小字段设为 200，后端按自身实现解释。 */
	dma_cfg.source_burst_length = 1;                                                            /* 接收源突发长度写为一个单位。 */
    dma_config(uart_data->dma_dev, uart_data->rxdma_chan, &dma_cfg);                                /* 提交通道初始配置，此处没有检查底层返回状态。 */

    return 0;
}

/**
  * @brief  释放接收 DMA 通道并清除设备保存的通道号。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 未初始化或通道无效返回 -1，执行释放后返回 0。
  */
int uart_acts_dma_receive_exit(struct device *dev)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);          /* 取得当前实例私有的运行数据。 */

	if(!uart_data->dma_dev){                               /* 检查 DMA 控制器是否已绑定。 */
		printk("err:no uart dma inited\n");
		return -1;                                     /* 缺少本方向需要的 DMA 配置、设备或通道。 */
	}

	if(uart_data->rxdma_chan == UART_DMA_INVALID_CHAN){    /* 检查此方向是否已保存合法通道号。 */
		printk("err:no uart dma channel\n");
		return -1;                                     /* 缺少本方向需要的 DMA 配置、设备或通道。 */
	}

    dma_free(uart_data->dma_dev, uart_data->rxdma_chan);       /* 把不再使用的通道归还给 DMA 驱动。 */
    printk("free uart rxdma chan=%d\n", uart_data->rxdma_chan);

	uart_data->rxdma_chan = UART_DMA_INVALID_CHAN;         /* 保存尚未持有通道的哨兵，供下次初始化检查。 */
    return 0;
}

/**
  * @brief  设置接收寄存器到目标内存的 DMA 并提交启动。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  d：[输出] 至少 len 字节且搬运期间保持有效的接收区。
  * @param  len：[输入] 要搬运的字节数。
  * @retval 固定返回 0，未转交 DMA 装载和启动结果。
  * @note   调用者在传输结束或停止前保持缓冲有效。
  */
int uart_acts_dma_receive(struct device *dev, char *d, int len)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);                                                             /* 取得当前实例私有的运行数据。 */
   // struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);
	struct acts_uart_controller *uart = UART_STRUCT(dev);                                                     /* 通过设备实例的基址访问这个端口的寄存器。 */
    __ASSERT(uart_data->dma_dev, "uart dma dev not config\n");                                                    /* 断言 DMA 设备已经绑定。 */

    dma_reload(uart_data->dma_dev, uart_data->rxdma_chan, (unsigned int)(&uart->rxdat), (unsigned int)d, len);    /* 把 RXDAT 作为源、调用者缓冲 d 作为目的地址。 */
    dma_start(uart_data->dma_dev, uart_data->rxdma_chan);                                                         /* 启动 RX DMA，后续由 UART 的接收请求推动搬运。 */

    return 0;
}

/**
  * @brief  从 RX DMA 状态判断预定长度是否已经搬完。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 剩余长度为零返回 1，否则返回 0。
  */
int uart_acts_dma_receive_complete(struct device *dev)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);                        /* 取得当前实例私有的运行数据。 */
    //struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);
	struct dma_status stat;                                              /* 接收通道的运行状态与剩余长度快照。 */

    __ASSERT(uart_data->dma_dev, "uart dma dev not config\n");               /* 断言 DMA 设备已经绑定。 */
	dma_get_status(uart_data->dma_dev, uart_data->rxdma_chan, &stat);    /* 采样此方向的 DMA 状态，后续使用其中的剩余字节数。 */
	if(stat.pending_length==0)                                           /* 剩余量为零表示该通道报告已搬完。 */
		return 1;
    return 0 ;
}

/**
  * @brief  读取接收 DMA 通道尚待搬运的字节数。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 返回状态快照中的 pending_length。
  */
int uart_acts_dma_receive_remain(struct device *dev)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);                        /* 取得当前实例私有的运行数据。 */
    //struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);
	struct dma_status stat;                                              /* 接收通道的运行状态与剩余长度快照。 */

    __ASSERT(uart_data->dma_dev, "uart dma dev not config\n");               /* 断言 DMA 设备已经绑定。 */
	dma_get_status(uart_data->dma_dev, uart_data->rxdma_chan, &stat);    /* 采样此方向的 DMA 状态，后续使用其中的剩余字节数。 */
	return stat.pending_length;                                          /* 向调用者返回这个时刻采样的剩余长度。 */
}


/**
  * @brief  先读取 RX DMA 进度，再请求停止该通道。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 返回请求停止之前的剩余字节数。
  * @note   统计快照和真正停止时刻可以不同。
  */
int uart_acts_dma_receive_stop(struct device *dev)
{
    struct acts_uart_data *uart_data = DEV_DATA(dev);                        /* 取得当前实例私有的运行数据。 */
	struct dma_status stat;                                              /* 接收通道的运行状态与剩余长度快照。 */

   // struct acts_uart_config *uart_config = (struct acts_uart_config *)DEV_CFG(dev);
	dma_get_status(uart_data->dma_dev, uart_data->rxdma_chan, &stat);    /* 采样此方向的 DMA 状态，后续使用其中的剩余字节数。 */
    dma_stop(uart_data->dma_dev, uart_data->rxdma_chan);                     /* 取过剩余量以后再请求停止通道。 */

	return stat.pending_length;                                          /* 交付停通道之前的进度快照。 */
}

/**
  * @brief  控制 UART 的 RX DMA 请求输出。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  drq_enable：[输入] true 打开外设请求，false 关闭。
  * @retval 固定返回 0。
  * @note   只改变外设请求使能位，不重新装载 DMA 地址和计数。
  */
int uart_acts_dma_receive_drq_switch(struct device *dev, bool drq_enable)
{
	struct acts_uart_controller *uart = UART_STRUCT(dev);    /* 通过设备实例的基址访问这个端口的寄存器。 */

    if (drq_enable)
    {
	    uart->ctrl |= UART_CTL_RX_DE;                        /* 打开 UART 到 DMA 的接收请求。 */
    }
    else
    {
	    uart->ctrl &= ~UART_CTL_RX_DE;                       /* 关闭 UART 到 DMA 的接收请求。 */
    }

    return 0;
}
#endif


/* 设备注册时将这张操作表交给 dev->api；通用 UART 接口通过这里绑定到炬芯驱动。 */
__unused static const struct uart_driver_api uart_acts_driver_api = {
	.poll_in          = uart_acts_poll_in,               /* 将 poll_in 接口绑定到本端口的对应实现。 */
	.poll_out         = uart_acts_poll_out,              /* 将 poll_out 接口绑定到本端口的对应实现。 */
	.configure		  = uart_acts_set_config,    /* 将 configure 接口绑定到本端口的对应实现。 */
	.config_get	  	  = uart_acts_get_config,    /* 将 config_get 接口绑定到本端口的对应实现。 */
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.err_check        = uart_acts_err_check,           /* 将 err_check 接口绑定到本端口的对应实现。 */
	.fifo_fill        = uart_acts_fifo_fill,           /* 将 fifo_fill 接口绑定到本端口的对应实现。 */
	.fifo_read        = uart_acts_fifo_read,           /* 将公开的 uart_fifo_read 分派到本设备从 RXDAT 读数的函数。 */
	.irq_tx_enable    = uart_acts_irq_tx_enable,       /* 将 irq_tx_enable 接口绑定到本端口的对应实现。 */
	.irq_tx_disable   = uart_acts_irq_tx_disable,      /* 将 irq_tx_disable 接口绑定到本端口的对应实现。 */
	.irq_tx_ready     = uart_acts_irq_tx_ready,        /* 将 irq_tx_ready 接口绑定到本端口的对应实现。 */
	.irq_rx_enable    = uart_acts_irq_rx_enable,       /* 将 irq_rx_enable 接口绑定到本端口的对应实现。 */
	.irq_rx_disable   = uart_acts_irq_rx_disable,      /* 将 irq_rx_disable 接口绑定到本端口的对应实现。 */
	.irq_tx_complete  = uart_acts_irq_tx_complete,     /* 将 irq_tx_complete 接口绑定到本端口的对应实现。 */
	.irq_rx_ready     = uart_acts_irq_rx_ready,        /* 将 irq_rx_ready 接口绑定到本端口的对应实现。 */
	.irq_is_pending   = uart_acts_irq_is_pending,      /* 将 irq_is_pending 接口绑定到本端口的对应实现。 */
	.irq_update       = uart_acts_irq_update,          /* 将 irq_update 接口绑定到本端口的对应实现。 */
	.irq_callback_set = uart_acts_irq_callback_set,    /* 将 irq_callback_set 接口绑定到本端口的对应实现。 */
#endif

};

/* 动态引脚配置只在 CONFIG_CFG_DRV 下编译。 */
#ifdef CONFIG_CFG_DRV
/**
  * @brief  取动态配置给控制台 TX 和 RX 设置引脚号及复用功能。
  * @param  无。
  * @retval 无。
  * @note   在 CONFIG_CFG_DRV 下定义；本文件初始化中的调用位于停用的条件分支。
  */
static void acts_pinmux_setup_pins_dynamic_uart(void)
{
    struct acts_pin_config pinconf[2];
    unsigned short pin_val;

#if (CONFIG_UART_CONSOLE_ON_DEV_NAME == CONFIG_UART_0_NAME)
    pinconf[0] = UART0_MFP_CFG;
    pinconf[1] = UART0_MFP_CFG;
#endif

#if (CONFIG_UART_CONSOLE_ON_DEV_NAME == CONFIG_UART_1_NAME)
    pinconf[0] = UART1_MFP_CFG;
    pinconf[1] = UART1_MFP_CFG;
#endif

    pin_val = GPIO_NONE;                                                                                    /* 用无引脚值开始，允许缺省 TX 配置保留原引脚。 */
    if(cfg_get_by_key(ITEM_UART_TX_PIN, &pin_val, 2) && (pin_val != GPIO_NONE)){
        pinconf[0].pin_num = pin_val&0xff;                                                                  /* 配置字低八位保存 GPIO 编号。 */
        pinconf[0].mode = (pinconf[0].mode & (~GPIO_CTL_MFP_MASK)) | ((pin_val>>8) & GPIO_CTL_MFP_MASK);    /* 只替换功能复用选择字段。 */
    }
    pin_val = GPIO_NONE;                                                                                    /* 重新置空，再单独读取 RX 的动态配置。 */
    if(cfg_get_by_key(ITEM_UART_RX_PIN, &pin_val, 2) && (pin_val != GPIO_NONE) ){
        pinconf[1].pin_num = pin_val&0xff;                                                                  /* 取 RX 的 GPIO 编号。 */
        pinconf[1].mode = (pinconf[0].mode & (~GPIO_CTL_MFP_MASK)) | ((pin_val>>8)&GPIO_CTL_MFP_MASK);      /* 此表达式参考 TX 条目的非复用位，再组合 RX 的复用值。 */
    }
    acts_pinmux_setup_pins(pinconf, 2);                                                                     /* 一次将发送和接收两个引脚的配置写入板级硬件。 */
}

#endif

/**
  * @brief  在设备初始化阶段打开 UART 时钟、解除复位并安装初始配置和 IRQ。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @retval 完成本函数固定返回 0。
  * @note   注册宏在 PRE_KERNEL_1 接入此函数。中断后端稍后登记自己的 uart_callback。
  */
__unused static int uart_acts_init(const struct device *dev)
{
	const struct acts_uart_config *dev_cfg = DEV_CFG(dev);    /* 定位端口的板级资源配置。 */
	struct acts_uart_data * dev_data = DEV_DATA(dev);         /* 取得当前实例私有的运行数据。 */
	struct acts_uart_controller *uart = UART_STRUCT(dev);     /* 通过设备实例的基址访问这个端口的寄存器。 */
	struct uart_config *cfg = &dev_data->cfg;                 /* 定位设备缓存的线路参数。 */
#ifdef CONFIG_CFG_DRV
	struct acts_pin_config pinconf[2];
	unsigned short pin_val;
#endif
	/* 先打开端口时钟，随后才能访问复位后的寄存器。 */
	acts_clock_peripheral_enable(dev_cfg->clock_id);          /* 打开此 UART 对应的时钟门控。 */
	/* 解除对应外设复位。 */
	acts_reset_peripheral_deassert(dev_cfg->reset_id);        /* 撤销外设复位，寄存器进入可访问状态。 */

	/* 若端口先前已使能，重新配置前等待发送忙标志解除。 */
	if (uart->ctrl & UART_CTL_EN) {
		while (uart->stat & UART_STA_UTBB);               /* 旧线路仍在发送时等待 UTBB 解除。 */
	}


	/* 同时启用收发器与两侧 FIFO，初值为 8 数据位、1 停止位、无奇偶校验。 */
	uart->ctrl = UART_CTL_EN |
		UART_CTL_RX_EN | UART_CTL_RX_FIFO_EN | UART_CTL_RX_FIFO_THREHOLD_1BYTE |
		UART_CTL_TX_EN | UART_CTL_TX_FIFO_EN | UART_CTL_TX_FIFO_THREHOLD_1BYTE |
		UART_CTL_DATA_WIDTH_8BIT | UART_CTL_STOP_1BIT | UART_CTL_PARITY_NONE;
/* 保留的动态引脚及波特率读取示例处于停用分支。 */
#if 0
//#ifdef CONFIG_CFG_DRV
		acts_pinmux_setup_pins_dynamic_uart();
		if(!cfg_get_by_key(ITEM_UART_BAUDRATE,	&cfg->baudrate, 4)){
			cfg->baudrate = dev_cfg->baud_rate;
		}

		printk("drv cfg: console,baud=%d\n", dev_cfg->baud_rate);
//#else
#endif
	cfg->baudrate = dev_cfg->baud_rate;                       /* 从所选 UART 的板级配置取初始速率。 */
//#endif
	cfg->parity = UART_CFG_PARITY_NONE;                       /* 默认使用无奇偶校验。 */
	cfg->stop_bits = UART_CFG_STOP_BITS_1;                    /* 默认使用一个停止位。 */
	cfg->data_bits = UART_CFG_DATA_BITS_8;                    /* 每个字符包含八个有效数据位。 */
	cfg->flow_ctrl = UART_CFG_FLOW_CTRL_NONE;                 /* 默认关闭硬件流控。 */
	uart_acts_init_bycfg(dev);                                /* 把默认线路参数应用到寄存器并清理接收状态。 */

	/* 向状态寄存器写错误位组，清除已锁存的错误。 */
	//uart->stat = UART_STA_ERRS;

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
			DEV_CFG(dev)->irq_config_func(dev);       /* 按设备实例连接 uart_acts_isr 并开放对应中断。 */
#endif
#ifdef CONFIG_UART_DMA_DRIVEN
	dev_data->rxdma_chan = UART_DMA_INVALID_CHAN;             /* 保存尚未持有通道的哨兵，供下次初始化检查。 */
	dev_data->txdma_chan = UART_DMA_INVALID_CHAN;             /* 保存尚未持有通道的哨兵，供下次初始化检查。 */
#endif
	return 0;
}

/* 设备电源管理打开时注册挂起与恢复回调。 */
#ifdef CONFIG_PM_DEVICE
/**
  * @brief  在挂起与恢复时保存或还原非 UART0 实例的控制和分频寄存器。
  * @param  dev：[输入] 当前 UART 设备对象，非空，从中取得板级配置及私有运行状态。
  * @param  action：[输入] 电源管理动作，处理 SUSPEND 和 RESUME。
  * @retval 固定返回 0。
  * @note   UART0 不进入这个寄存器备份分支。
  */
int uart_acts_pm_control(const struct device *dev, enum pm_device_action action)
{
	struct acts_uart_data * dev_data = DEV_DATA(dev);         /* 取得当前实例私有的运行数据。 */
	struct acts_uart_controller *uart = UART_STRUCT(dev);     /* 通过设备实例的基址访问这个端口的寄存器。 */
	const struct acts_uart_config *dev_cfg = DEV_CFG(dev);    /* 定位端口的板级资源配置。 */
	//printk("uart-clkid=%d \n",dev_cfg->clock_id);
	if(dev_cfg->clock_id != CLOCK_ID_UART0){                  /* 本电源管理分支只备份和恢复非 UART0 的端口。 */
		if(action == PM_DEVICE_ACTION_SUSPEND){
			dev_data->reg_br_bak = uart->br;          /* 先保存分频值。 */
			dev_data->reg_ctl_bak = uart->ctrl;       /* 再保存串口使能、线路、FIFO 和 IRQ 等控制位。 */
		}
		if(action == PM_DEVICE_ACTION_RESUME){
			uart->br = dev_data->reg_br_bak;          /* 恢复分频设置。 */
			uart->ctrl = dev_data->reg_ctl_bak;       /* 恢复此前的控制配置。 */
		}
	}
	return 0;
}
#else
#define uart_acts_pm_control 	NULL    /* 未启用设备电源管理时注册空的管理回调。 */
#endif


/*
.rxdma_id =DT_INST_DMAS_CELL_BY_NAME(n, rx, devid),\
.rxdma_chan = DT_INST_DMAS_CELL_BY_NAME(n, rx, channel),\

*/

/* 若板级允许 TX DMA，以下宏同步填入两个方向的请求号及通道配置。 */
#define  dma_use(n)  	(\
		.dma_dev_name =  CONFIG_DMA_0_NAME, \
		.txdma_id = CONFIG_UART_##n##_TX_DMA_ID,\
		.txdma_chan = CONFIG_UART_##n##_TX_DMA_CHAN,\
		.rxdma_id = CONFIG_UART_##n##_RX_DMA_ID,\
        .rxdma_chan = CONFIG_UART_##n##_RX_DMA_CHAN,\
		.flag_use_txdma = 1,					\
		.flag_use_rxdma = 1,					\
		)

/* 关闭板级 DMA 选择时，把两个方向的使用标志清零。 */
#define dma_not(n)	(\
		.flag_use_txdma = 0, \
		.flag_use_rxdma = 0, \
		 )



/* 按端口号生成只读配置和专属 IRQ 连接函数。IRQ_CONNECT 将该设备对象传给统一的 uart_acts_isr。 */
#define UART_ACTS_DEFINE_CONFIG(n)					\
	static const struct device DEVICE_NAME_GET(uart_acts_##n);		\
									\
	static void uart##n##_acts_irq_config(const struct device *port)			\
	{								\
		IRQ_CONNECT(IRQ_ID_UART##n, CONFIG_UART_##n##_IRQ_PRI,	\
			    uart_acts_isr,				\
			    DEVICE_GET(uart_acts_##n), 0);		\
		irq_enable(IRQ_ID_UART##n);				\
	}												\
	static const struct acts_uart_config uart_acts_config_##n = {    \
		   .base = UART##n##_REG_BASE,\
		   .clock_id = CLOCK_ID_UART##n,\
		   .reset_id = RESET_ID_UART##n,\
		   .baud_rate = CONFIG_UART_##n##_SPEED, \
		    COND_CODE_1(CONFIG_UART_##n##_USE_TX_DMA,dma_use(n), dma_not(n))\
		   .irq_config_func = uart##n##_acts_irq_config,	\
	}

/* 为每个端口分配独立运行数据，以 PRE_KERNEL_1 初始化并绑定 uart_acts_driver_api。 */
#define UART_ACTS_DEVICE_INIT(n)						\
	UART_ACTS_DEFINE_CONFIG(n);					\
	static struct acts_uart_data uart_acts_dev_data_##n ;\
	DEVICE_DEFINE(uart_acts_##n,				\
			    CONFIG_UART_##n##_NAME,				\
			    &uart_acts_init, uart_acts_pm_control, &uart_acts_dev_data_##n,	\
			    &uart_acts_config_##n, PRE_KERNEL_1,		\
			    0, &uart_acts_driver_api);



/* 只有已启用的实例才注册到设备模型，Shell 根据配置名查找 UART_0。 */
#if IS_ENABLED(CONFIG_UART_0)
	UART_ACTS_DEVICE_INIT(0)
#endif
#if IS_ENABLED(CONFIG_UART_1)
	UART_ACTS_DEVICE_INIT(1)
#endif
#if IS_ENABLED(CONFIG_UART_2)
	UART_ACTS_DEVICE_INIT(2)
#endif
#if IS_ENABLED(CONFIG_UART_3)
	UART_ACTS_DEVICE_INIT(3)
#endif
#if IS_ENABLED(CONFIG_UART_4)
	UART_ACTS_DEVICE_INIT(4)
#endif


