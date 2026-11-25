/*
 * Copyright (c) 2018 Google LLC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Actions 物理 DMA。request 按通道预留，config 立即将一个块写入寄存器，start 开放搬运，stop 与 free 分别负责停机和归还所有权。 */
#include <device.h>
#include <soc.h>
#include <drivers/dma.h>

#include <board_cfg.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(dma_acts, CONFIG_DMA_LOG_LEVEL);

#define DMA_INVALID_CHAN       	(0xff)    /* 动态申请通道的哨兵。 */


#define DMA_ID_MEM				0                                                  /* 控制字中的内存请求源编码。 */
#define MAX_DMA_CH  			CONFIG_DMA_0_PCHAN_NUM                                     /* 实例的物理通道数来自板级配置。 */
#define DMA_CHAN(base, ch)		((struct acts_dma_chan_reg*)((base) +  (ch+1) * 0x100))    /* 将编号映射为基地址后第 ch+1 个通道窗口。 */
/* 单次允许写入块长度的上限，单位为字节。 */
#define DMA_ACTS_MAX_DATA_ITEMS		0x7ffff    /* 单块字节数的驱动限制。 */

/* 通道控制字包含请求槽、地址模式、数据宽度、突发模式与硬件重载。 */
#define DMA_CTL_SRC_TYPE_SHIFT			0                                  /* 源请求槽在控制字中的最低位。 */
#define DMA_CTL_SRC_TYPE(x)			((x) << DMA_CTL_SRC_TYPE_SHIFT)    /* 源请求槽的硬件编码。 */
#define DMA_CTL_SRC_TYPE_MASK			DMA_CTL_SRC_TYPE(0x1f)             /* 源请求槽的字段掩码。 */
#define DMA_CTL_SAM_CONSTANT			(0x1 << 7)                         /* 源地址固定，对应启用。 */
#define DMA_CTL_DST_TYPE_SHIFT			8                                  /* 目的请求槽在控制字中的最低位。 */
#define DMA_CTL_DST_TYPE(x)			((x) << DMA_CTL_DST_TYPE_SHIFT)    /* 目的请求槽的硬件编码。 */
#define DMA_CTL_DST_TYPE_MASK			DMA_CTL_DST_TYPE(0x1f)             /* 目的请求槽的字段掩码。 */
#define DMA_CTL_DAM_CONSTANT			(0x1 << 15)                        /* 目的地址固定，对应启用。 */

#define DMA_CTL_ADUDIO_TYPE_SHIFT			16                    /* 音频地址模式在控制字中的最低位。 */
#define DMA_CTL_ADUDIO_TYPE(x)		((x) << DMA_CTL_ADUDIO_TYPE_SHIFT)    /* 音频地址模式的硬件编码。 */
#define DMA_CTL_ADUDIO_TYPE_MASK	DMA_CTL_ADUDIO_TYPE(0x1)              /* 音频地址模式的字段掩码。 */
#define DMA_CTL_ADUDIO_TYPE_INTER	DMA_CTL_ADUDIO_TYPE(0)                /* 音频地址模式，对应交错。 */
#define DMA_CTL_ADUDIO_TYPE_SEP		DMA_CTL_ADUDIO_TYPE(1)                /* 音频地址模式，对应分离。 */

#define DMA_CTL_TRM_SHIFT			17                            /* 突发方式在控制字中的最低位。 */
#define DMA_CTL_TRM(x)				((x) << DMA_CTL_TRM_SHIFT)    /* 突发方式的硬件编码。 */
#define DMA_CTL_TRM_MASK			DMA_CTL_TRM(0x1)              /* 突发方式的字段掩码。 */
#define DMA_CTL_TRM_BURST8			DMA_CTL_TRM(0)                /* 突发方式，对应八拍突发。 */
#define DMA_CTL_TRM_SINGLE			DMA_CTL_TRM(1)                /* 突发方式，对应单次。 */

#define DMA_CTL_RELOAD				(0x1 << 18)    /* 硬件循环通道使能位。 */

#define DMA_CTL_TWS_SHIFT			20                            /* 搬运元素位宽在控制字中的最低位。 */
#define DMA_CTL_TWS(x)				((x) << DMA_CTL_TWS_SHIFT)    /* 搬运元素位宽的硬件编码。 */
#define DMA_CTL_TWS_MASK			DMA_CTL_TWS(0x3)              /* 搬运元素位宽的字段掩码。 */
#define DMA_CTL_TWS_8BIT			DMA_CTL_TWS(2)                /* 搬运元素位宽，对应八位。 */
#define DMA_CTL_TWS_16BIT			DMA_CTL_TWS(1)                /* 搬运元素位宽，对应十六位。 */
#define DMA_CTL_TWS_32BIT			DMA_CTL_TWS(0)                /* 搬运元素位宽，对应三十二位。 */

/* 全局待决寄存器分别锁存整块与半块事件，对相应位写一清除。 */
#define DMA_PD_TCIP(ch)				(1 << ch)         /* 整块传输完成待决位。 */
#define DMA_PD_HFIP(ch)				(1 << (ch+16))    /* 传输达到半程的待决位。 */

/* 中断使能位决定是否通知 CPU，轮询 DMAIP 可独立观察已锁存事件。 */
#define DMA_IE_TCIP(ch)				(1 << ch)         /* 整块完成中断使能。 */
#define DMA_IE_HFIP(ch)				(1 << (ch+16))    /* 半程中断使能。 */


#define DMA_START_START				(0x1 << 0)    /* 硬件通道运行控制位。 */



/* 单个通道窗口的寄存器顺序，volatile 保证发出访问；计数与运行标志需分别采样。 */
struct acts_dma_chan_reg {
	volatile uint32_t ctl;       /* 控制字，包括请求源、地址递增、位宽与重载。 */
	volatile uint32_t start;     /* 启动寄存器，写入控制运行、读回用于确认。 */
	volatile uint32_t saddr0;    /* 主源地址。 */
	volatile uint32_t saddr1;    /* 分离模式的附加源地址。 */
	volatile uint32_t daddr0;    /* 主目的地址。 */
	volatile uint32_t daddr1;    /* 分离模式的附加目的地址。 */
	volatile uint32_t bc;        /* 编程的块字节计数。 */
	volatile uint32_t rc;        /* 硬件实时剩余计数，位于通道偏移 0x1c。 */
};
struct acts_dma_reg {
	volatile uint32_t dma_ip;    /* 全局半传输与完成待决位。 */
	volatile uint32_t dma_ie;    /* 对应的全局中断使能位。 */
};


struct dma_acts_channel {
	dma_callback_t cb;                                       /* 上层注册的通知函数。 */
	void   *cb_arg;                                          /* 提交配置时保存并在回调时还给调用者的上下文。 */
    uint16_t reload_count;                                       /* 中断服务中观察到重载模式的次数。 */
	uint16_t  complete_callback_en	: 1;                     /* 是否允许调用完成回调。 */
	uint16_t  hcom_callback_en		: 1;             /* 重载模式是否可开启半程中断。 */
	uint16_t  channel_direction	    : 3;                 /* 已配置的传输方向，供状态查询。 */
	uint16_t  busy					: 1;     /* 驱动的分配占用位，独立于通道 START。 */
	uint16_t  reserved				: 10;    /* 软件状态预留位。 */

};

struct dma_acts_data {
	uint32_t base;                                   /* 控制器寄存器基地址。 */
	int chan_num;                                    /* 当前设备支持的物理通道总数。 */
	struct dma_acts_channel channels[MAX_DMA_CH];    /* 按通道号索引所有权与回调状态。 */
};

/* 由已注册的设备对象取得控制器状态，通道数组为静态存储。 */
#define DEV_DATA(dev) \
	((struct dma_acts_data *const)(dev)->data)


static struct dma_acts_data dmac_data;    /* 供唯一物理控制器实例使用的静态状态。 */

DEVICE_DECLARE(dma_acts_0);    /* 允许 ISR 取得在文件末尾定义的设备对象。 */


#if defined(CONFIG_DMA_DBG_DUMP)

/**
  * @brief  打印一个物理通道的配置与当前计数。
  * @param  ddev：[输入]非空控制器状态。
  * @param  id：[输入]合法的物理通道编号。
  * @retval 无。
  * @note   仅调试构建读取活动寄存器。
  */
static void dma_acts_dump_reg(struct dma_acts_data *ddev, uint32_t id)
{
	struct acts_dma_chan_reg *cregs = DMA_CHAN(ddev->base, id);    /* 按通道号定位专用寄存器窗口。 */
	LOG_INF("Using channel: %d", id);
	LOG_INF("  DMA_CTL:      0x%x", cregs->ctl);                   /* 输出该寄存器当前值，供对照编程状态与搬运进度。 */
	LOG_INF("  DMA_SADDR0:    0x%x", cregs->saddr0);               /* 输出该寄存器当前值，供对照编程状态与搬运进度。 */
	LOG_INF("  DMA_SADDR1:    0x%x", cregs->saddr1);               /* 输出该寄存器当前值，供对照编程状态与搬运进度。 */
	LOG_INF("  DMA_DADDR0:    0x%x", cregs->daddr0);               /* 输出该寄存器当前值，供对照编程状态与搬运进度。 */
	LOG_INF("  DMA_DADDR1:    0x%x", cregs->daddr1);               /* 输出该寄存器当前值，供对照编程状态与搬运进度。 */
	LOG_INF("  DMA_LEN:       0x%x", cregs->bc);                   /* 输出该寄存器当前值，供对照编程状态与搬运进度。 */
	LOG_INF("  DMA_RMAIN_LEN: 0x%x", cregs->rc);                   /* 输出该寄存器当前值，供对照编程状态与搬运进度。 */
}
/**
  * @brief  遍历所有物理通道并打印预留及硬件状态。
  * @param  无。
  * @retval 无。
  * @note   仅调试构建可用，多次寄存器读取并非同一时刻的快照。
  */
void dma_dump_info(void)
{
	int i;
	struct dma_acts_channel *pchan;
	struct dma_acts_data *ddev = &dmac_data;    /* 取得控制器通道池和总数。 */

	LOG_INF("----pchan= %d stauts:--------\n", ddev->chan_num);
	for(i= 0; i < ddev->chan_num; i++){
		pchan = &ddev->channels[i];
		printk("chan%d: isbusy=%d\n",	i, pchan->busy);
		dma_acts_dump_reg(ddev, i);
	}

}
#endif


/* 各物理通道中断携带自身通道号，共用一个服务例程。 */
/**
  * @brief  分发一个物理通道的半传输和整块完成事件。
  * @param  arg：[输入]注册 IRQ 时编码为指针值的通道编号，不解引用。
  * @retval 无。
  * @note   中断上下文调用已注册回调，先清待决再通知；关闭 IRQ 的 SPI2 循环入口自行确认事件。
  */
static void dma_acts_isr(void *arg)
{
	uint32_t id = (uint32_t) arg;                                      /* IRQ_CONNECT 的参数值即物理通道号。 */
	const struct device *dev = DEVICE_GET(dma_acts_0);                 /* 传给回调的控制器实例。 */
	struct dma_acts_data *ddev = &dmac_data;                           /* 取得控制器通道池和总数。 */
	struct acts_dma_chan_reg *cregs = DMA_CHAN(ddev->base, id);        /* 按通道号定位专用寄存器窗口。 */
	struct acts_dma_reg *gregs = (struct acts_dma_reg *)ddev->base;    /* 取得所有通道共用的待决与使能寄存器。 */
	struct dma_acts_channel *chan = &ddev->channels[id];               /* 当前通道回调描述和计数。 */
	uint32_t hf_pending, tc_pending;                                   /* 分别记录半块与整块事件。 */

	if (id >= ddev->chan_num)
		return;                                                    /* 不处理不属于控制器的 IRQ。 */

	/* 用待决位与使能位的交集区分本通道两种事件。 */
	hf_pending = DMA_PD_HFIP(id) &
		  gregs->dma_ip & gregs->dma_ie;                           /* 只有该通道已使能的半程事件才进入回调路径。 */

	tc_pending = DMA_PD_TCIP(id) &
		  gregs->dma_ip & gregs->dma_ie;                           /* 只有该通道已使能的完成事件才进入回调路径。 */

	/* 只确认该通道已经使能且处于待决状态的事件。 */
	gregs->dma_ip = tc_pending | hf_pending;                           /* 对本通道待决位写一清除，其他通道保持原状态。 */

	if((tc_pending|hf_pending) == 0)
		return;                                                    /* 如果没有可处理的事件就退出。 */
	/* 若同时有半程和完成事件，按半程、完成顺序通知原调用者。 */
	if ( chan->complete_callback_en && chan->cb) {
		//chan->cb(dev, chan->cb_arg, id, !!hf_pending);
		if(hf_pending)
			chan->cb(dev, chan->cb_arg, id, 1);                /* 以状态 1 报告半程事件。 */
		if(tc_pending)
			chan->cb(dev, chan->cb_arg, id, 0);                /* 以状态 0 报告整块完成。 */

	}
	if(cregs->ctl & DMA_CTL_RELOAD)
		chan->reload_count++;                                      /* 每次在重载模式进入有效事件处理时累计一次。 */
}


/* 来自公共 DMA 配置接口的单块参数同步写入硬件。 */
/**
  * @brief  根据方向、地址和块属性生成 PDMA 控制字。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @param  channel：[输入]已申请的物理通道编号，应小于实际通道数。
  * @param  config：[输入]非空描述，head_block 须非空。硬件配置在返回前完成，回调及 user_data 在使用期间须持续有效。
  * @retval 0 为配置完成，-EINVAL 为通道越界或块超过 0x7ffff 字节。
  * @note   只消费首块。两端位宽都非零时目的端设置优先，源或目的任一 reload 标志均打开同一硬件重载位。
  */
static int dma_acts_config(const struct device *dev, uint32_t channel,
			   struct dma_config *config)
{
	struct dma_acts_data *ddev = DEV_DATA(dev);                           /* 取得控制器通道池和总数。 */
	struct dma_acts_channel *chan  = &ddev->channels[channel];            /* 取得该通道的软件配置和回调状态。 */
	struct acts_dma_chan_reg *cregs = DMA_CHAN(ddev->base, channel);      /* 按通道号定位专用寄存器窗口。 */
	struct dma_block_config *head_block = config->head_block;             /* 从配置中读取首块；本驱动未遍历后续块。 */
	uint32_t ctl;
	int data_width = 0;

	if (channel >= ddev->chan_num) {                                      /* 拒绝超出本实例通道数组的编号。 */
		LOG_ERR("DMA error:ch=%d > dma max chan=%d\n",channel,
		       ddev->chan_num);

		return -EINVAL;                                               /* 向调用者报告通道或描述参数无效。 */
	}

	if (head_block->block_size > DMA_ACTS_MAX_DATA_ITEMS) {               /* 超过驱动最大块长时不会编程通道。 */
		LOG_ERR("DMA error: Data size too big: %d",
		       head_block->block_size);
		return -EINVAL;                                               /* 向调用者报告通道或描述参数无效。 */
	}

	/* 提取可跨调用保留的回调和上下文。 */
	if (config->complete_callback_en || config->error_callback_en) {
		chan->cb = config->dma_callback;                              /* 保存通知函数；ISR 仍需检查完成回调开关。 */
		chan->cb_arg = config->user_data;                             /* 保存用户上下文，完成时与物理通道号一起传回。 */
		chan->complete_callback_en = config->complete_callback_en;    /* 记录是否需要启用完成中断。 */
	} else {
		chan->cb = NULL;                                              /* 无回调模式清除先前应用的回调。 */
		chan->complete_callback_en = 0;                               /* 禁止在下次 start 时开放完成 IRQ。 */
	}
	chan->hcom_callback_en = 0;                                           /* 新配置先禁用半程，可由下面的重载条件开启。 */

	/* 先编程地址与块长，再按方向生成控制字。 */
	cregs->saddr0 = (uint32_t)head_block->source_address;                 /* 编程源地址，TX 通常为内存，RX 通常为外设。 */
	cregs->daddr0 = (uint32_t)head_block->dest_address;                   /* 编程目的地址，与传输方向配对。 */
	cregs->bc = (uint32_t)head_block->block_size;                         /* 把本块的字节数装入 BC。 */

	chan->channel_direction = config->channel_direction;                  /* 保存查询接口要返回的方向。 */
	chan->reload_count = 0;                                               /* 新配置重新开始累计服务次数。 */

	if (config->channel_direction == MEMORY_TO_PERIPHERAL) {
		ctl = DMA_CTL_SRC_TYPE(DMA_ID_MEM) |
		      DMA_CTL_DST_TYPE(config->dma_slot) |
		      DMA_CTL_DAM_CONSTANT;                                   /* TX 选择固定的外设目的地址和板级请求源。 */
	} else if (config->channel_direction == PERIPHERAL_TO_MEMORY)  {
		ctl = DMA_CTL_SRC_TYPE(config->dma_slot) |
		      DMA_CTL_SAM_CONSTANT |
		      DMA_CTL_DST_TYPE(DMA_ID_MEM);                           /* RX 选择固定的外设源地址及递增内存目的。 */
	} else {
		ctl = DMA_CTL_SRC_TYPE(DMA_ID_MEM) |
		      DMA_CTL_DST_TYPE(DMA_ID_MEM);                           /* 缺省分支把两端都配置为内存。 */
	}
	/* Actions 音频扩展使用 reserved 字段请求分离地址模式。 */
	if (config->reserved == 1 && config->channel_direction == MEMORY_TO_PERIPHERAL) {
		ctl |= DMA_CTL_ADUDIO_TYPE_SEP;
	}else if(config->reserved == 1 && config->channel_direction == PERIPHERAL_TO_MEMORY) {
		ctl |= DMA_CTL_ADUDIO_TYPE_SEP;
	}

	/* 由两端传输要求选择突发、宽度和重载。 */
	if (config->source_burst_length == 1 || config->dest_burst_length == 1) {
		ctl |= DMA_CTL_TRM_SINGLE;                                    /* 任何一端要求单元素传输时选择 SINGLE。 */
	}

	if (config->source_data_size) {
		data_width = config->source_data_size;                        /* 先按源端给出的宽度选取。 */
	}

	if (config->dest_data_size) {
		data_width = config->dest_data_size;                          /* 若目的宽度非零则覆盖源宽度选择。 */
	}

	if (head_block->source_reload_en || head_block->dest_reload_en) {
		ctl |= DMA_CTL_RELOAD;                                        /* 在控制字中设置硬件重载。 */
		chan->hcom_callback_en = 1;                                   /* 允许 start 在注册回调的前提下开放半程 IRQ。 */
	}

	/* 将 SDK 字节数编码成控制器宽度字段。 */
	switch (data_width) {
	case 2:
		ctl |= DMA_CTL_TWS_16BIT;                                     /* 两字节配置编码为十六位。 */
		break;
	case 4:
		ctl |= DMA_CTL_TWS_32BIT;                                     /* 四字节配置编码为三十二位。 */
		break;
	case 1:
	default:
		ctl |= DMA_CTL_TWS_8BIT;                                      /* 单字节及未识别的宽度均使用八位。 */
		break;
	}
	cregs->ctl = ctl;                                                     /* 将完整控制字一次写入目标通道。 */
	return 0;                                                             /* 通道已配置，启动由独立接口完成。 */

}

/**
  * @brief  清旧事件、选择通知方式并启动通道。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @param  channel：[输入]已申请的物理通道编号，应小于实际通道数。
  * @retval 0 为已置启动位，-EINVAL 为通道越界。
  * @note   处理中断使能和启动寄存器期间关闭本地中断。
  */
static int dma_acts_start(const struct device *dev, uint32_t channel)
{

	struct dma_acts_data *ddev = DEV_DATA(dev);                          /* 取得控制器通道池和总数。 */
	struct dma_acts_channel *chan  = &ddev->channels[channel];           /* 取得该通道的软件配置和回调状态。 */
	struct acts_dma_chan_reg *cregs = DMA_CHAN(ddev->base, channel);     /* 按通道号定位专用寄存器窗口。 */
	struct acts_dma_reg *gregs = (struct acts_dma_reg *)ddev->base;      /* 取得所有通道共用的待决与使能寄存器。 */

	uint32_t key;                                                        /* 保存本次临界区的中断状态。 */

	if (channel >= ddev->chan_num) {                                     /* 拒绝超出本实例通道数组的编号。 */
		return -EINVAL;                                              /* 向调用者报告通道或描述参数无效。 */
	}

	/* 中断使能和启动共享全局寄存器，在短临界区成组更新。 */
	key = irq_lock();                                                    /* 保存并关闭本地中断，保护共享寄存器或通道所有权。 */
	/* 清除该通道可能遗留的上一次传输事件。 */
	gregs->dma_ip = DMA_PD_TCIP(channel) | DMA_PD_HFIP(channel);         /* 写一确认该通道上次传输的两个事件。 */

	gregs->dma_ie &= ~( DMA_IE_TCIP(channel) | DMA_IE_HFIP(channel));    /* 屏蔽此通道原来的中断设置。 */

	/* 仅配置完成回调时打开中断，重载块还可收到半程通知。 */
	if (chan->complete_callback_en) {
		gregs->dma_ie |= DMA_IE_TCIP(channel) ;                      /* 开放整块完成中断。 */
			/* 重载模式的半程中断与完成中断使用不同位。 */
		if(chan->hcom_callback_en)
			gregs->dma_ie |= DMA_IE_HFIP(channel) ;              /* 已允许半程回调时开放半块事件中断。 */
	}

	/* 配置音频分离模式所需的第二个地址寄存器。 */
	if (cregs->ctl & DMA_CTL_ADUDIO_TYPE_SEP) {
		if ((cregs->ctl & DMA_CTL_DST_TYPE_MASK) == 0) {
			/* 外设向内存搬运。 */
			cregs->daddr1 = cregs->saddr0;                       /* 目的为内存时按此音频模式补充第二地址。 */
		} else {
			/* 内存向外设搬运。 */
			cregs->saddr1 = cregs->daddr0;                       /* 目的为外设时补充第二源地址。 */
		}
	}

	/* 写 START 后通道可以响应外设请求。 */
	cregs->start |= DMA_START_START;                                     /* 置 START；SPI 外设请求是否已打开由上层独立控制。 */
	irq_unlock(key);                                                     /* 恢复进入临界区前的中断状态。 */

	return 0;                                                            /* 启动写入已完成，不代表数据已经到达目的地。 */
}

/**
  * @brief  屏蔽通道中断，关重载并请求停机。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @param  channel：[输入]已申请的物理通道编号，应小于实际通道数。
  * @retval 0 为已发出停止写入，-EINVAL 为通道越界。
  * @note   本实现不等待 START 回读和 RC 清零，也不归还通道。SPI2 负责独立回读。
  */
static int dma_acts_stop(const struct device *dev, uint32_t channel)
{
	struct dma_acts_data *ddev = DEV_DATA(dev);                          /* 取得控制器通道池和总数。 */
	struct acts_dma_chan_reg *cregs = DMA_CHAN(ddev->base, channel);     /* 按通道号定位专用寄存器窗口。 */
	struct acts_dma_reg *gregs = (struct acts_dma_reg *)ddev->base;      /* 取得所有通道共用的待决与使能寄存器。 */
	uint32_t key;                                                        /* 保存本次临界区的中断状态。 */

	if (channel >= ddev->chan_num) {                                     /* 拒绝超出本实例通道数组的编号。 */
		return -EINVAL;                                              /* 向调用者报告通道或描述参数无效。 */
	}

	/* 停机时屏蔽通知并阻止自动重装。 */
	key = irq_lock();                                                    /* 保存并关闭本地中断，保护共享寄存器或通道所有权。 */
	gregs->dma_ie &= ~( DMA_IE_TCIP(channel) | DMA_IE_HFIP(channel));    /* 禁止已停止通道继续向 CPU 发出通知。 */
	/* 丢弃停止通道上未消费的旧事件。 */
	gregs->dma_ip = DMA_PD_TCIP(channel) | DMA_PD_HFIP(channel);         /* 清除此通道的旧事件。 */

	/* 先关闭自动重载，避免清 START 后又被硬件重启。 */
	cregs->ctl &= ~DMA_CTL_RELOAD;                                       /* 阻止通道在块尾自动装载下一轮。 */
	cregs->start &= ~DMA_START_START;                                    /* 要求通道停止，并未在这里等待硬件回读。 */
	irq_unlock(key);                                                     /* 恢复进入临界区前的中断状态。 */

	return 0;                                                            /* 返回表示已提交停止动作。 */

}

/**
  * @brief  在临界区更新传输地址和字节计数。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @param  channel：[输入]已申请的物理通道编号，应小于实际通道数。
  * @param  src：[输入]源总线地址。
  * @param  dst：[输入]目的总线地址。
  * @param  size：[输入]新块长度，单位字节。
  * @retval 0 为寄存器已写入，-EINVAL 为通道越界。
  * @note   此入口不会置 START，调用者须保证重配时机。
  */
static int dma_acts_reload(const struct device *dev, uint32_t channel,
			   uint32_t src, uint32_t dst, size_t size)
{

	struct dma_acts_data *ddev = DEV_DATA(dev);                         /* 取得控制器通道池和总数。 */
	struct acts_dma_chan_reg *cregs = DMA_CHAN(ddev->base, channel);    /* 按通道号定位专用寄存器窗口。 */
	uint32_t key;                                                       /* 保存本次临界区的中断状态。 */

	if (channel >= ddev->chan_num) {                                    /* 拒绝超出本实例通道数组的编号。 */
		return -EINVAL;                                             /* 向调用者报告通道或描述参数无效。 */
	}
	key = irq_lock();                                                   /* 保存并关闭本地中断，保护共享寄存器或通道所有权。 */
	cregs->saddr0 = src;                                                /* 写入新的源总线地址。 */
	cregs->daddr0 = dst;                                                /* 写入新的目的总线地址。 */
	cregs->bc = size;                                                   /* 设置新的整块长度。 */
	irq_unlock(key);                                                    /* 恢复进入临界区前的中断状态。 */
	return 0;                                                           /* 地址和长度寄存器均已写入。 */

}

/**
  * @brief  查询通道运行标志、方向和可见的剩余计数。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @param  channel：[输入]已申请的物理通道编号，应小于实际通道数。
  * @param  stat：[输出]非空、可写的状态对象。
  * @retval 0 为状态已写入，-EINVAL 为通道越界或输出为空。
  * @note   已停止时 pending_length 被写为 0，此结果不足以判断是正常完成还是提前停止。
  */
static int dma_acts_get_status(const struct device *dev, uint32_t channel,
			       struct dma_status *stat)
{
	struct dma_acts_data *ddev = DEV_DATA(dev);                         /* 取得控制器通道池和总数。 */
	struct acts_dma_chan_reg *cregs = DMA_CHAN(ddev->base, channel);    /* 按通道号定位专用寄存器窗口。 */
	struct dma_acts_channel *chan  = &ddev->channels[channel];          /* 取得该通道的软件配置和回调状态。 */

	if (channel >= ddev->chan_num || stat == NULL) {
		return -EINVAL;                                             /* 向调用者报告通道或描述参数无效。 */
	}

	/* 通道已经停止时 SDK 将残量报告为零，SPI2 需要直接回读 RC。 */
	if (cregs->start) {
		stat->busy = true;                                          /* 运行标志由硬件 START 得出。 */
		stat->pending_length = cregs->rc;                           /* 运行中返回所采样的真实 RC。 */
	} else {
		stat->busy = false;                                         /* START 已清时向上报告停止。 */
		stat->pending_length = 0;                                   /* 此值由软件赋零，不能证明硬件残量为零。 */
	}
	stat->dir = chan->channel_direction;                                /* 方向来自提交配置时的软件记录。 */
	return 0;                                                           /* 状态结构已填好。 */
}

/**
  * @brief  在临界区预留指定物理通道或从高编号开始寻找空闲通道。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @param  channel：[输入]所需通道号，0xff 代表动态分配。
  * @retval 返回非负通道号；编号无效、已占用或池耗尽时返回 -EINVAL。
  * @note   预留位仅记录所有权，硬件运行状态须另查 START。
  */
static int dma_acts_request(const struct device *dev, uint32_t channel)
{
	struct dma_acts_data *ddev = DEV_DATA(dev);          /* 取得控制器通道池和总数。 */
	int i;
	uint32_t key;                                        /* 保存本次临界区的中断状态。 */
	int ret = -EINVAL;                                   /* 初始视为分配失败，只有找到可用编号才覆盖。 */
	//printk("-------requset:dma chan%d \n", channel);

	/* 指定编号和自动申请共用 busy 预留位，由临界区保护。 */
	if (channel != DMA_INVALID_CHAN) {
		if(channel >= ddev->chan_num){
			printk("request chan=%d max err\n", channel);
			return -EINVAL;                      /* 向调用者报告通道或描述参数无效。 */
		}
		key = irq_lock();                            /* 保存并关闭本地中断，保护共享寄存器或通道所有权。 */
		if(ddev->channels[channel].busy){            /* 已被其他客户端或板级设备预留。 */
			printk("request chan id%d already used\n", channel);
			ret = -EINVAL;
		}else{
			ret = channel;                       /* 成功时返回调用者指定的编号。 */
			ddev->channels[channel].busy = 1;    /* 在恢复中断之前标为占用。 */
		}
		irq_unlock(key);                             /* 恢复进入临界区前的中断状态。 */
	}else{
		key = irq_lock();                            /* 保存并关闭本地中断，保护共享寄存器或通道所有权。 */
		for(i = ddev->chan_num-1; i >= 0; i--){      /* 从最高编号向下扫描所有物理通道。 */
			if(!ddev->channels[i].busy)
				break;                       /* 找到第一个未占用通道即停止搜索。 */
		}
		if(i >= 0){
			ret = i;                             /* 把选中的物理编号返回给调用者。 */
			ddev->channels[i].busy = 1;          /* 在同一临界区登记占用，防止重复分配。 */
		}
		irq_unlock(key);                             /* 恢复进入临界区前的中断状态。 */
	}
	//printk("--- alloc dma chan%d \n", ret);

	return ret;                                          /* 返回申请到的编号或负错误码。 */
}
/**
  * @brief  归还此前预留的物理通道号。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @param  channel：[输入]已申请的物理通道编号，应小于实际通道数。
  * @retval 无。
  * @note   本入口不检查通道范围或停止硬件，调用者须先确认停机。
  */
static void dma_acts_free(const struct device *dev, uint32_t channel)
{
	struct dma_acts_data *ddev = DEV_DATA(dev);    /* 取得控制器通道池和总数。 */
	uint32_t key;                                  /* 保存本次临界区的中断状态。 */

	key = irq_lock();                              /* 保存并关闭本地中断，保护共享寄存器或通道所有权。 */
	if(!ddev->channels[channel].busy){             /* 释放未预留通道时仅记录诊断。 */
		printk("err:dma chan%d is free\n", channel);
	}else{
		ddev->channels[channel].busy = 0;      /* 清除软件所有权，使其他设备可申请。 */
	}
	irq_unlock(key);                               /* 恢复进入临界区前的中断状态。 */
}




/* 每个通道连接独立 IRQ，回调参数携带物理编号。 */
#define DMA_ACTS_IRQ_CONNECT(n)						 \
	do {								 \
		IRQ_CONNECT((IRQ_ID_DMA0+n),		 \
			    CONFIG_DMA_IRQ_PRI,		 \
			    dma_acts_isr, n, 0);	 \
		irq_enable((IRQ_ID_DMA0+n));		 \
	} while (0)

/* 编译时排除 LCD 和两种 SPI 闪存专用通道的 IRQ 注册。 */
#define DMA_NOT_RESERVE(chan) ((CONFIG_DMA_LCD_RESEVER_CHAN!=chan) \
								&& (CONFIG_DMA_SPINAND_RESEVER_CHAN!=chan) \
								&& (CONFIG_DMA_SPINOR_RESEVER_CHAN!=chan))

/**
  * @brief  上电并复位 PDMA，注册各通道 IRQ，标记板级专用通道。
  * @param  dev：[输入]设备模型持有的非空物理 DMA 控制器。
  * @retval 成功完成初始化返回 0。
  * @note   设备在 POST_KERNEL 阶段注册，LCD、SPI NAND 和 SPI NOR 的保留编号不能被动态申请。
  */
static int dma_acts_init(const struct device *dev)
{
	struct dma_acts_data *data = DEV_DATA(dev);

	data->base = DMA_REG_BASE;                                   /* 记录 MMIO 基地址供运行期定位通道。 */
	acts_clock_peripheral_enable(CLOCK_ID_DMA);                  /* 开启 PDMA 的外设时钟。 */
	acts_reset_peripheral(RESET_ID_DMA);                         /* 复位控制器，再安装通道中断。 */
	data->chan_num = MAX_DMA_CH;                                 /* 采用板级指定通道数。 */

#if MAX_DMA_CH > 0
#if	DMA_NOT_RESERVE(0)
	DMA_ACTS_IRQ_CONNECT(0);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 1
#if	DMA_NOT_RESERVE(1)
	DMA_ACTS_IRQ_CONNECT(1);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 2
#if	DMA_NOT_RESERVE(2)
	DMA_ACTS_IRQ_CONNECT(2);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 3
#if	DMA_NOT_RESERVE(3)
	DMA_ACTS_IRQ_CONNECT(3);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 4
#if	DMA_NOT_RESERVE(4)
	DMA_ACTS_IRQ_CONNECT(4);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 5
#if	DMA_NOT_RESERVE(5)
	DMA_ACTS_IRQ_CONNECT(5);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 6
#if	DMA_NOT_RESERVE(6)
	DMA_ACTS_IRQ_CONNECT(6);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 7
#if	DMA_NOT_RESERVE(7)
	DMA_ACTS_IRQ_CONNECT(7);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif

#if MAX_DMA_CH > 8
#if	DMA_NOT_RESERVE(8)
	DMA_ACTS_IRQ_CONNECT(8);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif


#if MAX_DMA_CH > 9
#if	DMA_NOT_RESERVE(9)
	DMA_ACTS_IRQ_CONNECT(9);                                     /* 为该物理编号连接并打开通道中断。 */
#endif
#endif
	printk("dma-num=%d\n", data->chan_num);

/* 初始化时在公共分配池中预留板级专用通道。 */
#if CONFIG_DMA_LCD_RESEVER_CHAN < MAX_DMA_CH
	data->channels[CONFIG_DMA_LCD_RESEVER_CHAN].busy = 1;        /* 在分配池内预留给LCD。 */
#endif
#if CONFIG_DMA_SPINAND_RESEVER_CHAN < MAX_DMA_CH
	data->channels[CONFIG_DMA_SPINAND_RESEVER_CHAN].busy = 1;    /* 在分配池内预留给SPI NAND。 */
#endif

#if CONFIG_DMA_SPINOR_RESEVER_CHAN < MAX_DMA_CH
	data->channels[CONFIG_DMA_SPINOR_RESEVER_CHAN].busy = 1;     /* 在分配池内预留给SPI NOR。 */
#endif

	return 0;                                                    /* 初始化完成，设备可按名称取得。 */
}


/* 公共 DMA API 从这张函数表进入实际的物理控制器。 */
static const struct dma_driver_api dma_acts_api = {
	.config = dma_acts_config,
	.start = dma_acts_start,
	.stop = dma_acts_stop,
	.reload = dma_acts_reload,
	.get_status = dma_acts_get_status,
	.request = dma_acts_request,
	.free = dma_acts_free,

};

/* 按板级设备名注册实例，优先级 1 的内核初始化完成后可绑定。 */
DEVICE_DEFINE(dma_acts_0, CONFIG_DMA_0_NAME, &dma_acts_init, NULL,
		    &dmac_data, NULL, POST_KERNEL,
		    1, &dma_acts_api);
