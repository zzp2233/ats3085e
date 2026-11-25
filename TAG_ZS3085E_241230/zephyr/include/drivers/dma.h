/**
 * @file
 *
 * @brief DMA 控制器公共接口，SPI2 的物理通道操作经设备函数表分派。
 */

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DMA_H_
#define ZEPHYR_INCLUDE_DRIVERS_DMA_H_

#include <kernel.h>
#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief DMA 公共接口
 * @defgroup dma_interface DMA 公共接口
 * @ingroup io_interfaces
 * @{
 */

enum dma_channel_direction {
	MEMORY_TO_MEMORY = 0x0,     /* 内存到内存。 */
	MEMORY_TO_PERIPHERAL,       /* 内存到外设，SPI 用于 TX。 */
	PERIPHERAL_TO_MEMORY,       /* 外设到内存，SPI 用于 RX。 */
	PERIPHERAL_TO_PERIPHERAL    /* 外设到外设，原接口说明限 NXP EDMA。 */
};

/** 源端或目的端地址的步进方式。 */
enum dma_addr_adj {
	DMA_ADDR_ADJ_INCREMENT,    /* 每个元素结束后地址递增。 */
	DMA_ADDR_ADJ_DECREMENT,    /* 地址递减。 */
	DMA_ADDR_ADJ_NO_CHANGE,    /* 地址保持固定，适合外设数据寄存器。 */
};

/* 通用分配器可使用的通道筛选条件。 */
enum dma_channel_filter {
	DMA_CHANNEL_NORMAL,      /* 普通搬运通道。 */
	DMA_CHANNEL_PERIODIC,    /* 可由周期性源触发的通道。 */
};

/** @brief 单块搬运描述。两端地址、块长和可选重载描述一次连续传输；具体字段是否生效由控制器决定。 */
struct dma_block_config {
#ifdef CONFIG_DMA_64BIT
	uint64_t source_address;    /* 64 位源总线地址。 */
	uint64_t dest_address;      /* 64 位目的总线地址。 */
#else
	uint32_t source_address;    /* 32 位源总线地址。 */
	uint32_t dest_address;      /* 32 位目的总线地址。 */
#endif
	uint32_t source_gather_interval;        /* 到达聚集边界后对源地址作的调整。 */
	uint32_t dest_scatter_interval;         /* 到达分散边界后对目的地址作的调整。 */
	uint16_t dest_scatter_count;            /* 相邻分散边界之间连续传输的数量。 */
	uint16_t source_gather_count;           /* 相邻聚集边界之间连续传输的数量。 */
	uint32_t block_size;                    /* 本块实际传输字节数。 */
	struct dma_block_config *next_block;    /* 后续块，可为空，须由硬件驱动支持。 */
	uint16_t  source_gather_en :  1;        /* 启用源端聚集。 */
	uint16_t  dest_scatter_en :   1;        /* 启用目的端分散。 */
	uint16_t  source_addr_adj :   2;        /* 源地址每元素步进方式。 */
	uint16_t  dest_addr_adj :     2;        /* 目的地址每元素步进方式。 */
	uint16_t  source_reload_en :  1;        /* 块结束时重载源地址。 */
	uint16_t  dest_reload_en :    1;        /* 块结束时重载目的地址，SPI2 用此字段保持持续接收。 */
	uint16_t  fifo_mode_control : 4;        /* 启动传输所需的 FIFO 填充程度，编码取决于硬件。 */
	uint16_t  flow_control_mode : 1;        /* 选择源端就绪或等待目的端请求的流控方式。 */
	uint16_t  reserved :          3;        /* 保留位，初始化清零。 */
};

/** @brief 驱动在完成或错误时通知调用者。dev 和 channel 标识硬件，user_data 由配置传入并需保持有效。Actions PDMA ISR 用 status=1 表示半程、0 表示完成，无返回值。 */
typedef void (*dma_callback_t)(const struct device *dev, void *user_data,
			       uint32_t channel, int status);

/** @brief 通道配置。请求源与传输方向共同确定哪一侧连接外设，从 head_block 取得块描述。Actions PDMA 在 dma_config 中立即写入寄存器。 */
struct dma_config {
	uint32_t  dma_slot :             7;     /* 硬件 DMA 请求槽号。 */
	uint32_t  channel_direction :    3;     /* 选择内存与外设之间的数据流方向。 */
	uint32_t  complete_callback_en : 1;     /* 完成回调策略，具体中断映射见目标驱动。 */
	uint32_t  error_callback_en :    1;     /* 错误回调配置位，实际通知依控制器支持。 */
	uint32_t  source_handshake :     1;     /* 源端握手，0 为硬件，1 为软件。 */
	uint32_t  dest_handshake :       1;     /* 目的端握手，0 为硬件，1 为软件。 */
	uint32_t  channel_priority :     4;     /* 硬件通道优先级。 */
	uint32_t  source_chaining_en :   1;     /* 源端是否启用通道链接。 */
	uint32_t  dest_chaining_en :     1;     /* 目的端是否启用通道链接。 */
	uint32_t  linked_channel   :     7;     /* 计数耗尽时可触发的下一通道。 */
	uint32_t  reserved :             5;     /* 平台扩展保留位，Actions 音频路径读取此字段。 */
	uint32_t  source_data_size :    16;     /* 源元素字节数。 */
	uint32_t  dest_data_size :      16;     /* 目的元素字节数。 */
	uint32_t  source_burst_length : 16;     /* 每次突发包含的源元素数。 */
	uint32_t  dest_burst_length :   16;     /* 每次突发包含的目的元素数。 */
	uint32_t block_count;                   /* 链中块的个数，受驱动支持能力限制。 */
	struct dma_block_config *head_block;    /* 首块描述的地址，PDMA 配置时同步读取。 */
	void *user_data;                        /* 回调上下文，由应用管理生命周期。 */
	dma_callback_t dma_callback;            /* 完成或异常时驱动可调用的通知函数。 */
};

/** @brief 当前通道状态。通道预留和正在搬运是不同状态，pending_length 的可见性由具体实现决定。 */
struct dma_status {
	bool busy;                         /* 驱动观察到的运行状态。 */
	enum dma_channel_direction dir;    /* 已配置的传输方向。 */
	uint32_t pending_length;           /* 驱动报告的剩余长度，字节数由目标实现提供。 */
};

/** @brief 通用原子位图分配器的设备上下文，适用驱动需将它放在设备 data 首部。Actions 物理通道走设备自己的 request/free。 */
struct dma_context {
	int32_t magic;       /* 校验 data 布局是否符合通用分配器约定。 */
	int dma_channels;    /* 通用分配器中可索引的通道数。 */
	atomic_t *atomic;    /* 原子位图，置位表示已预留。 */
};

/* 用魔数检查设备 data 首部是否为公共位图上下文。 */
#define DMA_MAGIC 0x47494749

/**
 * @cond INTERNAL_HIDDEN
 *
 * 以下函数指针构成驱动内部操作表，外部通常通过包装接口访问。
 */
/**
  * @brief  为指定通道提交请求源、方向及块描述。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  config：[输入]非空配置，首块必须有效；回调使用的数据须延续至停机。
  * @retval 0 表示配置成功，负数由驱动返回。
  * @note   不隐式启动通道，PDMA 仅使用首块。
  */
typedef int (*dma_api_config)(const struct device *dev, uint32_t channel,
			      struct dma_config *config);

#ifdef CONFIG_DMA_64BIT
/**
  * @brief  更新已分配通道的源、目的和块长度。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  src：[输入]源总线地址。
  * @param  dst：[输入]目的总线地址。
  * @param  size：[输入]新的传输字节数。
  * @retval 0 为成功，包装层在缺少 reload 时返回 -ENOSYS，其他错误来自驱动。
  * @note   CONFIG_DMA_64BIT 决定地址类型，调用方遵守驱动对重配时机的要求。
  */
typedef int (*dma_api_reload)(const struct device *dev, uint32_t channel,
			      uint64_t src, uint64_t dst, size_t size);
#else
/**
  * @brief  更新已分配通道的源、目的和块长度。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  src：[输入]源总线地址。
  * @param  dst：[输入]目的总线地址。
  * @param  size：[输入]新的传输字节数。
  * @retval 0 为成功，包装层在缺少 reload 时返回 -ENOSYS，其他错误来自驱动。
  * @note   CONFIG_DMA_64BIT 决定地址类型，调用方遵守驱动对重配时机的要求。
  */
typedef int (*dma_api_reload)(const struct device *dev, uint32_t channel,
			      uint32_t src, uint32_t dst, size_t size);
#endif

/**
  * @brief  使配置好的通道可以响应搬运请求。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 0 为启动成功，负数来自驱动。
  * @note   外设请求开关和线路完成状态须由外设调用方协调。
  */
typedef int (*dma_api_start)(const struct device *dev, uint32_t channel);

/**
  * @brief  请求控制器停止指定通道。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 0 为驱动接受停止，负数为错误。
  * @note   不负责释放通道；Actions PDMA 的实现清 START，SPI2 专用入口另行检查回读。
  */
typedef int (*dma_api_stop)(const struct device *dev, uint32_t channel);

/**
  * @brief  获取驱动可观察到的搬运状态。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  status：[输出]非空可写的状态对象。
  * @retval 0 表示成功，未实现返回 -ENOSYS，其他负值来自驱动。
  * @note   Actions 在 START 清零时令 pending_length 为零，独立帧完整性判断还需要硬件 RC。
  */
typedef int (*dma_api_get_status)(const struct device *dev, uint32_t channel,
				  struct dma_status *status);
/**
  * @brief  通过设备分配接口预留物理通道。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 成功返回通道编号，负数为驱动错误；Actions PDMA 以 -EINVAL 报告无可用通道。
  * @note   该驱动接受 0xff 自动选择，SPI2 分别申请 RX 与 TX。
  */
typedef int (*dma_api_request)(const struct device *dev, uint32_t channel);
/**
  * @brief  将物理通道归还给分配器。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 无。
  * @note   调用方先停止通道并确认不再访问缓冲。
  */
typedef void (*dma_api_free)(const struct device *dev, uint32_t channel);


/** @brief 可选驱动筛选器。dev 和 channel 为候选设备与通道，filter_param 原样来自请求方；返回真表示符合筛选。 */
typedef bool (*dma_api_chan_filter)(const struct device *dev,
				int channel, void *filter_param);

__subsystem struct dma_driver_api {
	dma_api_config config;              /* 通道配置入口，由具体控制器注册。 */
	dma_api_reload reload;              /* 源、目的地址与块长重装入口，由具体控制器注册。 */
	dma_api_start start;                /* 启动入口，由具体控制器注册。 */
	dma_api_stop stop;                  /* 停机入口，由具体控制器注册。 */
	dma_api_get_status get_status;      /* 状态查询入口，由具体控制器注册。 */
	dma_api_request request;            /* 预留物理通道入口，由具体控制器注册。 */
	dma_api_free free;                  /* 归还物理通道入口，由具体控制器注册。 */
	dma_api_chan_filter chan_filter;    /* 可选通道筛选入口，由具体控制器注册。 */
};


/**
  * @brief  通过设备分配接口预留物理通道。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 成功返回通道编号，负数为驱动错误；Actions PDMA 以 -EINVAL 报告无可用通道。
  * @note   该驱动接受 0xff 自动选择，SPI2 分别申请 RX 与 TX。
  */
static inline int dma_request(const struct device *dev, uint32_t channel)
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;    /* 取得绑定设备的 DMA 操作表。 */
	return api->request(dev, channel);                  /* 直接委派设备所提供的分配器，获得实际通道号。 */
}

/**
  * @brief  将物理通道归还给分配器。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 无。
  * @note   调用方先停止通道并确认不再访问缓冲。
  */
static inline void dma_free(const struct device *dev, uint32_t channel)
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;    /* 取得绑定设备的 DMA 操作表。 */
	api->free(dev, channel);                            /* 将所有权交回与申请配对的设备接口。 */
}

/**
 * @endcond
 */


/**
  * @brief  为指定通道提交请求源、方向及块描述。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  config：[输入]非空配置，首块必须有效；回调使用的数据须延续至停机。
  * @retval 0 表示配置成功，负数由驱动返回。
  * @note   不隐式启动通道，PDMA 仅使用首块。
  */
static inline int dma_config(const struct device *dev, uint32_t channel,
			     struct dma_config *config)
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;    /* 取得绑定设备的 DMA 操作表。 */

	return api->config(dev, channel, config);           /* 调用对应驱动配置函数，同步返回配置错误。 */
}


#ifdef CONFIG_DMA_64BIT
/**
  * @brief  更新已分配通道的源、目的和块长度。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  src：[输入]源总线地址。
  * @param  dst：[输入]目的总线地址。
  * @param  size：[输入]新的传输字节数。
  * @retval 0 为成功，包装层在缺少 reload 时返回 -ENOSYS，其他错误来自驱动。
  * @note   CONFIG_DMA_64BIT 决定地址类型，调用方遵守驱动对重配时机的要求。
  */
static inline int dma_reload(const struct device *dev, uint32_t channel,
			     uint64_t src, uint64_t dst, size_t size)
#else
/**
  * @brief  更新已分配通道的源、目的和块长度。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  src：[输入]源总线地址。
  * @param  dst：[输入]目的总线地址。
  * @param  size：[输入]新的传输字节数。
  * @retval 0 为成功，包装层在缺少 reload 时返回 -ENOSYS，其他错误来自驱动。
  * @note   CONFIG_DMA_64BIT 决定地址类型，调用方遵守驱动对重配时机的要求。
  */
static inline int dma_reload(const struct device *dev, uint32_t channel,
		uint32_t src, uint32_t dst, size_t size)
#endif
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;             /* 取得绑定设备的 DMA 操作表。 */

	if (api->reload) {                                           /* 检查可选重装接口是否存在。 */
		return api->reload(dev, channel, src, dst, size);    /* 按当前地址宽度传递源、目的地址和新字节数。 */
	}

	return -ENOSYS;                                              /* 驱动未提供此可选操作。 */
}


/**
  * @brief  使配置好的通道可以响应搬运请求。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 0 为启动成功，负数来自驱动。
  * @note   外设请求开关和线路完成状态须由外设调用方协调。
  */
__syscall int dma_start(const struct device *dev, uint32_t channel);

/**
  * @brief  使配置好的通道可以响应搬运请求。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 0 为启动成功，负数来自驱动。
  * @note   外设请求开关和线路完成状态须由外设调用方协调。
  */
static inline int z_impl_dma_start(const struct device *dev, uint32_t channel)
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;    /* 取得绑定设备的 DMA 操作表。 */

	return api->start(dev, channel);                    /* 由当前控制器实际写入通道启动配置。 */
}


/**
  * @brief  请求控制器停止指定通道。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 0 为驱动接受停止，负数为错误。
  * @note   不负责释放通道；Actions PDMA 的实现清 START，SPI2 专用入口另行检查回读。
  */
__syscall int dma_stop(const struct device *dev, uint32_t channel);

/**
  * @brief  请求控制器停止指定通道。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 0 为驱动接受停止，负数为错误。
  * @note   不负责释放通道；Actions PDMA 的实现清 START，SPI2 专用入口另行检查回读。
  */
static inline int z_impl_dma_stop(const struct device *dev, uint32_t channel)
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;    /* 取得绑定设备的 DMA 操作表。 */

	return api->stop(dev, channel);                     /* 向设备提交停止请求并透传状态。 */
}


/**
  * @brief  尝试从通用位图中原子取得符合筛选条件的通道。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  filter_param：[输入]可选的驱动筛选参数，其格式由驱动约定。
  * @retval 未取到候选或上下文无效时初值为 -EINVAL；返回最后记录的候选号。
  * @note   若最后候选被筛选拒绝，位图会释放该位，但局部 channel 仍保留此前编号。SPI2 使用私有物理通道分配入口。
  */
__syscall int dma_request_channel(const struct device *dev,
				  void *filter_param);

/**
  * @brief  尝试从通用位图中原子取得符合筛选条件的通道。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  filter_param：[输入]可选的驱动筛选参数，其格式由驱动约定。
  * @retval 未取到候选或上下文无效时初值为 -EINVAL；返回最后记录的候选号。
  * @note   若最后候选被筛选拒绝，位图会释放该位，但局部 channel 仍保留此前编号。SPI2 使用私有物理通道分配入口。
  */
static inline int z_impl_dma_request_channel(const struct device *dev,
					     void *filter_param)
{
	int i = 0;                                                              /* 从通道零开始扫描。 */
	int channel = -EINVAL;                                                  /* 暂未获得候选，初值用于无分配结果。 */
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;                        /* 取得绑定设备的 DMA 操作表。 */
	/* 通用分配器仅接受把 dma_context 放在 data 首部的驱动。 */
	struct dma_context *dma_ctx = (struct dma_context *)dev->data;

	if (dma_ctx->magic != DMA_MAGIC) {
		return channel;                                                 /* 设备未提供通用布局，返回初始错误。 */
	}

	for (i = 0; i < dma_ctx->dma_channels; i++) {                           /* 遍历位图内的所有有效编号。 */
		if (!atomic_test_and_set_bit(dma_ctx->atomic, i)) {             /* 原子尝试置位，旧值为零时当前调用取得该候选。 */
			channel = i;                                            /* 先记下候选编号，再调用可能拒绝它的筛选器。 */
			if (api->chan_filter &&
			    !api->chan_filter(dev, channel, filter_param)) {    /* 仅当有筛选器且拒绝此候选时撤销预留。 */
				atomic_clear_bit(dma_ctx->atomic, channel);     /* 释放不符合条件的候选。 */
				continue;                                       /* 继续检查余下通道，局部 channel 未在此复位。 */
			}
			break;                                                  /* 符合条件的候选已占用，结束扫描。 */
		}
	}

	return channel;                                                         /* 返回最后记下的编号或初始错误。 */
}


/**
  * @brief  清除通用位图的通道预留位。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 无。
  * @note   魔数不匹配或通道超界时直接返回，不操作硬件。
  */
__syscall void dma_release_channel(const struct device *dev,
				   uint32_t channel);

/**
  * @brief  清除通用位图的通道预留位。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @retval 无。
  * @note   魔数不匹配或通道超界时直接返回，不操作硬件。
  */
static inline void z_impl_dma_release_channel(const struct device *dev,
					      uint32_t channel)
{
	struct dma_context *dma_ctx = (struct dma_context *)dev->data;

	if (dma_ctx->magic != DMA_MAGIC) {
		return;                                        /* 布局不匹配时不更改位图。 */
	}

	if (channel < dma_ctx->dma_channels) {
		atomic_clear_bit(dma_ctx->atomic, channel);    /* 在合法范围内原子归还通道。 */
	}

}


/**
  * @brief  调用驱动筛选器检查通道是否符合参数要求。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  filter_param：[输入]可空的不透明过滤条件，具体含义由驱动决定。
  * @retval 有实现时返回其布尔结果（0 或 1），未提供时返回 -ENOSYS。
  * @note   不申请也不释放通道。
  */
__syscall int dma_chan_filter(const struct device *dev,
				   int channel, void *filter_param);

/**
  * @brief  调用驱动筛选器检查通道是否符合参数要求。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  filter_param：[输入]可空的不透明过滤条件，具体含义由驱动决定。
  * @retval 有实现时返回其布尔结果（0 或 1），未提供时返回 -ENOSYS。
  * @note   不申请也不释放通道。
  */
static inline int z_impl_dma_chan_filter(const struct device *dev,
					      int channel, void *filter_param)
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;                /* 取得绑定设备的 DMA 操作表。 */

	if (api->chan_filter) {
		return api->chan_filter(dev, channel, filter_param);    /* 返回驱动对候选通道的布尔判定。 */
	}

	return -ENOSYS;                                                 /* 该控制器未提供筛选实现。 */
}


/**
  * @brief  获取驱动可观察到的搬运状态。
  * @param  dev：[输入]有效 DMA 设备，实例的 api 指向对应驱动。
  * @param  channel：[输入]物理或逻辑通道编号，由所选驱动约定。
  * @param  stat：[输出]非空可写的状态对象。
  * @retval 0 表示成功，未实现返回 -ENOSYS，其他负值来自驱动。
  * @note   Actions 在 START 清零时令 pending_length 为零，独立帧完整性判断还需要硬件 RC。
  */
static inline int dma_get_status(const struct device *dev, uint32_t channel,
				 struct dma_status *stat)
{
	const struct dma_driver_api *api =
		(const struct dma_driver_api *)dev->api;       /* 取得绑定设备的 DMA 操作表。 */

	if (api->get_status) {
		return api->get_status(dev, channel, stat);    /* 通过设备函数表填入调用者提供的状态对象。 */
	}

	return -ENOSYS;                                        /* 可选状态查询未实现。 */
}


/**
  * @brief  把以字节表示的位宽映射到通用索引。
  * @param  size：[输入]1～32 字节，须为二的幂。
  * @retval 无效时为 0，否则返回最高置位的位置。
  * @note   使用前须确认本控制器期待此编码。
  */
static inline uint32_t dma_width_index(uint32_t size)
{
	/* 宽度允许范围为 1～32 字节。 */
	if (size < 1 || size > 32) {
		return 0;             /* 宽度越界时返回默认索引。 */
	}

	/* 只有二的幂能映射为单一宽度编码。 */
	if (!is_power_of_two(size)) {
		return 0;             /* 不能用单一指数编码非二次幂宽度。 */
	}

	/* 返回最高置位的位置，调用者须核对特定控制器的位编码。 */
	return find_msb_set(size);    /* 返回以 1 起算的最高置位位置。 */
}


/**
  * @brief  按当前通用辅助函数将突发长度映射为索引。
  * @param  burst：[输入]1～256 的突发长度。
  * @retval 范围外或二的幂返回 0，范围内其他值返回最高置位的位置。
  * @note   实际条件对二的幂走默认分支，使用者须核对目标控制器编码。
  */
static inline uint32_t dma_burst_index(uint32_t burst)
{
	/* 拒绝超出 1～256 的突发长度。 */
	if (burst < 1 || burst > 256) {
		return 0;              /* 范围外返回默认索引。 */
	}

	/* 当前条件对二的幂返回默认索引；其他范围内的长度才进入位位置换算。 */
	if (!(burst & (burst - 1))) {
		return 0;              /* 实际判定为二的幂，直接返回默认索引。 */
	}

	/* 返回该数最高置位的位置。 */
	return find_msb_set(burst);    /* 对其余输入返回最高置位位置。 */
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#include <syscalls/dma.h>

#endif    /* ZEPHYR_INCLUDE_DRIVERS_DMA_H_ */
