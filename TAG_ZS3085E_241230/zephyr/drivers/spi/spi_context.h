/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief SPI 驱动私有上下文：串行化调用、维护缓冲游标并管理可选完成通知。
 */

/* 包含保护；本文件供 SPI 驱动使用，应用通过公共 spi.h 调用设备 API。 */
#ifndef ZEPHYR_DRIVERS_SPI_SPI_CONTEXT_H_
#define ZEPHYR_DRIVERS_SPI_SPI_CONTEXT_H_

/* gpio.h 提供软件片选操作；spi.h 提供配置、缓冲类型，并通过设备/内核头引入信号量。 */
#include <drivers/gpio.h>
#include <drivers/spi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 上下文内部运行角色位，与公共 operation 中主机编码为 0 的规则不同，不应直接混用。 */
enum spi_ctx_runtime_op_mode {
	SPI_CTX_RUNTIME_OP_MODE_MASTER = BIT(0),    /* 内部主机运行角色标志。 */
	SPI_CTX_RUNTIME_OP_MODE_SLAVE  = BIT(1),    /* 内部从机运行角色标志。 */
};

/* 每个驱动实例持有一份上下文；配置仅保存指针，缓冲也不复制，调用者负责其生命周期。tx_len/rx_len 是数据字数，指针推进需乘每字节数 dfs。 */
struct spi_context {
	const struct spi_config *config;    /* 最近绑定的配置地址；不是配置对象的副本。 */

	struct k_sem lock;    /* 串行化访问同一控制器的二值信号量。 */
	struct k_sem sync;    /* 同步请求的完成通知信号量。 */
	int sync_status;      /* 完成处理记录的成功或负错误状态。 */

#ifdef CONFIG_SPI_ASYNC
	struct k_poll_signal *signal;        /* 异步构建保存的可选完成信号。 */
	bool asynchronous;                   /* 异步构建保存的本次请求通知方式。 */
#endif                                       /* CONFIG_SPI_ASYNC */
	const struct spi_buf *current_tx;    /* 当前发送块描述的地址。 */
	size_t tx_count;                     /* 包含当前块在内的剩余发送块数。 */
	const struct spi_buf *current_rx;    /* 当前接收块描述的地址。 */
	size_t rx_count;                     /* 包含当前块在内的剩余接收块数。 */

	const uint8_t *tx_buf;    /* 当前块中下一待发送字节的地址，可为空占位。 */
	size_t tx_len;            /* 当前发送块剩余数据字数。 */
	uint8_t *rx_buf;          /* 当前块中下一待写入接收字节的地址，可为空丢弃。 */
	size_t rx_len;            /* 当前接收块剩余数据字数。 */

#ifdef CONFIG_SPI_SLAVE
	int recv_frames;    /* 从机支持构建累计接收的数据字数。 */
#endif                      /* CONFIG_SPI_SLAVE */
};

/* 静态初始化容量为 1、初值为 0 的总线锁；设备初始化末尾通过无条件解锁首次释放。 */
#define SPI_CONTEXT_INIT_LOCK(_data, _ctx_name)				\
	._ctx_name.lock = Z_SEM_INITIALIZER(_data._ctx_name.lock, 0, 1)

/* 静态初始化初值为 0 的完成信号量，避免首次等待误判为已完成。 */
#define SPI_CONTEXT_INIT_SYNC(_data, _ctx_name)				\
	._ctx_name.sync = Z_SEM_INITIALIZER(_data._ctx_name.sync, 0, 1)

/**
  * @brief  用配置对象地址判断是否可复用上次硬件配置。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  config：[输入]待比较的配置地址。
  * @retval true：地址相同；false：地址不同。
  * @note   只比较指针，不比较字段；硬件复位后须清除 ctx->config。
  */
static inline bool spi_context_configured(struct spi_context *ctx,
					  const struct spi_config *config)
{
	return !!(ctx->config == config);    /* 配置地址相同即视为已配置，不比较成员值。 */
}

/**
  * @brief  从已绑定的配置中读取从机角色标志。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval true：从机；false：主机。
  * @note   ctx->config 必须已经绑定，函数不检查空配置。
  */
static inline bool spi_context_is_slave(struct spi_context *ctx)
{
	return (ctx->config->operation & SPI_OP_MODE_SLAVE);    /* 只提取主从角色标志。 */
}

/**
  * @brief  等待独占总线，并保存本次请求的完成通知方式。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  asynchronous：[输入]本次是否采用异步通知；仅异步构建保存。
  * @param  signal：[输入/输出]异步完成信号，可为空；使用期间必须保持有效。
  * @retval 无。
  * @note   K_FOREVER 可能永久阻塞；不能在中断中执行阻塞等待。
  */
static inline void spi_context_lock(struct spi_context *ctx,
				    bool asynchronous,
				    struct k_poll_signal *signal)
{
	k_sem_take(&ctx->lock, K_FOREVER);    /* 持续等待总线锁可用，取得后才修改上下文。 */

#ifdef CONFIG_SPI_ASYNC
	ctx->asynchronous = asynchronous;     /* 保存此次请求的同步或异步方式。 */
	ctx->signal = signal;                 /* 保存异步完成信号地址，不创建新信号对象。 */
#endif                                        /* CONFIG_SPI_ASYNC */
}

/**
  * @brief  按持锁选项、同步方式和失败状态决定是否归还总线锁。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  status：[输入]本次驱动状态，负数表示失败。
  * @retval 无。
  * @note   从机支持构建中，成功且设置 SPI_LOCK_ON 时保留锁；异步成功的解锁通常留给完成处理。
  */
static inline void spi_context_release(struct spi_context *ctx, int status)
{
#ifdef CONFIG_SPI_SLAVE
	if (status >= 0 && (ctx->config->operation & SPI_LOCK_ON)) {    /* 成功且请求持锁时将释放责任交给显式 release。 */
		return;                                                 /* 此路径无需继续修改上下文或片选，保留前面已说明的状态。 */
	}
#endif                                                                  /* CONFIG_SPI_SLAVE */

#ifdef CONFIG_SPI_ASYNC
	if (!ctx->asynchronous || (status < 0)) {                       /* 同步调用或异步提交失败需要在此归还总线。 */
		k_sem_give(&ctx->lock);                                 /* 归还一次总线访问许可。 */
	}
#else
	k_sem_give(&ctx->lock);                                         /* 归还一次总线访问许可。 */
#endif                                                                  /* CONFIG_SPI_ASYNC */
}

/**
  * @brief  等待完成信号量，并将从机成功状态转换为接收数据字数。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval 等待超时返回 -ETIMEDOUT；否则返回 sync_status；从机无错误时返回 recv_frames。
  * @note   等待预算按较长方向字数乘 8 估算毫秒，再加容差；frequency 必须非零。异步请求不等待信号量；当前 Actions CPU 路径并不调用此函数。
  */
static inline int spi_context_wait_for_completion(struct spi_context *ctx)
{
	int status = 0;                                                      /* 缺省完成状态为成功，等待后由上下文状态替换。 */
	uint32_t timeout_ms;                                                 /* 本次完成等待预算，单位毫秒。 */

	timeout_ms = MAX(ctx->tx_len, ctx->rx_len) * 8 * 1000 /              /* 按较长方向的字数和配置频率估算 8 位传输耗时。 */
		     ctx->config->frequency;
	timeout_ms += CONFIG_SPI_COMPLETION_TIMEOUT_TOLERANCE;               /* 加入配置规定的毫秒容差。 */

#ifdef CONFIG_SPI_ASYNC
	if (!ctx->asynchronous) {                                            /* 同步请求等待完成；异步请求走信号通知路径。 */
		if (k_sem_take(&ctx->sync, K_MSEC(timeout_ms))) {            /* 在预算时间内未取得完成通知即进入超时处理。 */
			LOG_ERR("Timeout waiting for transfer complete");    /* 记录等待超时，便于区分硬件错误与未通知完成。 */
			return -ETIMEDOUT;                                   /* 向调用者报告完成等待超过预算。 */
		}
		status = ctx->sync_status;                                   /* 取得完成处理保存的状态。 */
	}
#else
	if (k_sem_take(&ctx->sync, K_MSEC(timeout_ms))) {                    /* 在预算时间内未取得完成通知即进入超时处理。 */
		LOG_ERR("Timeout waiting for transfer complete");            /* 记录等待超时，便于区分硬件错误与未通知完成。 */
		return -ETIMEDOUT;                                           /* 向调用者报告完成等待超过预算。 */
	}
	status = ctx->sync_status;                                           /* 取得完成处理保存的状态。 */
#endif                                                                       /* CONFIG_SPI_ASYNC */

#ifdef CONFIG_SPI_SLAVE
	if (spi_context_is_slave(ctx) && !status) {                          /* 从机且无错误时向上返回接收字数。 */
		return ctx->recv_frames;                                     /* 把从机成功状态转换为累计接收数据字数。 */
	}
#endif                                                                       /* CONFIG_SPI_SLAVE */

	return status;                                                       /* 返回完成处理保存的状态或异步路径的缺省状态。 */
}

/**
  * @brief  记录完成状态，唤醒同步调用者或通知异步信号接收方。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  status：[输入]驱动完成状态，0 为成功、负数为错误。
  * @retval 无。
  * @note   异步分支按 SPI_LOCK_ON 决定是否释放总线；无信号对象时不发送完成信号。
  */
static inline void spi_context_complete(struct spi_context *ctx, int status)
{
#ifdef CONFIG_SPI_ASYNC
	if (!ctx->asynchronous) {                                      /* 同步请求等待完成；异步请求走信号通知路径。 */
		ctx->sync_status = status;                             /* 先保存结果，再发出完成通知，避免等待者读取旧状态。 */
		k_sem_give(&ctx->sync);                                /* 唤醒同步等待完成的调用者。 */
	} else {
		if (ctx->signal) {                                     /* 仅在提供信号对象时发送异步通知。 */
#ifdef CONFIG_SPI_SLAVE
			if (spi_context_is_slave(ctx) && !status) {    /* 从机且无错误时向上返回接收字数。 */
				/* 从机成功时用已接收数据字数替换零状态，供异步信号接收方读取。 */
				status = ctx->recv_frames;             /* 异步从机成功时用实际接收字数作为信号结果。 */
			}
#endif                                                                 /* CONFIG_SPI_SLAVE */
			k_poll_signal_raise(ctx->signal, status);      /* 触发异步完成信号并携带处理结果。 */
		}

		if (!(ctx->config->operation & SPI_LOCK_ON)) {         /* 未要求保持总线锁时才在异步完成处分配访问许可。 */
			k_sem_give(&ctx->lock);                        /* 归还一次总线访问许可。 */
		}
	}
#else
	ctx->sync_status = status;                                     /* 先保存结果，再发出完成通知，避免等待者读取旧状态。 */
	k_sem_give(&ctx->sync);                                        /* 唤醒同步等待完成的调用者。 */
#endif                                                                 /* CONFIG_SPI_ASYNC */
}

/**
  * @brief  把片选有效极性转换为 GPIO 逻辑电平。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval 高有效配置返回 1；默认低有效返回 0。
  * @note   必须先绑定有效配置。
  */
static inline int spi_context_cs_active_value(struct spi_context *ctx)
{
	if (ctx->config->operation & SPI_CS_ACTIVE_HIGH) {    /* 片选极性由 operation 的高有效选项决定。 */
		return 1;                                     /* 返回片选对应状态的GPIO高电平值。 */
	}

	return 0;                                             /* 返回对应分支所需的低电平值。 */
}

/**
  * @brief  把片选无效状态转换为 GPIO 逻辑电平。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval 高有效配置返回 0；默认低有效返回 1。
  * @note   必须先绑定有效配置。
  */
static inline int spi_context_cs_inactive_value(struct spi_context *ctx)
{
	if (ctx->config->operation & SPI_CS_ACTIVE_HIGH) {    /* 片选极性由 operation 的高有效选项决定。 */
		return 0;                                     /* 返回对应分支所需的低电平值。 */
	}

	return 1;                                             /* 返回片选对应状态的GPIO高电平值。 */
}

/**
  * @brief  初始化可选 GPIO 片选并将其置为无效电平。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval 无。
  * @note   配置及 GPIO 设备存在时才操作；未传播 GPIO 配置和写入错误。硬件片选无需此 GPIO 路径。
  */
static inline void spi_context_cs_configure(struct spi_context *ctx)
{
	if (ctx->config->cs && ctx->config->cs->gpio_dev) {          /* 只对配置了有效 GPIO 设备的片选执行初始化。 */
		gpio_pin_configure(ctx->config->cs->gpio_dev,        /* 把指定软件片选引脚配置为输出。 */
				   ctx->config->cs->gpio_pin, GPIO_OUTPUT);
		gpio_pin_set(ctx->config->cs->gpio_dev,              /* 通过 GPIO 驱动写入片选电平，具体极性由末参数给出。 */
			     ctx->config->cs->gpio_pin,
			     spi_context_cs_inactive_value(ctx));
	} else {
		LOG_DBG("CS control inhibited (no GPIO device)");    /* 降为 DBG：从机由主机器件驱动片选，此处每帧都会触发，级别调到 INFO 以下才输出。 */
	}
}

/**
  * @brief  按片选策略拉有效或释放 GPIO 片选，并执行配置的微秒延时。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  on：[输入]true 拉有效，false 请求释放。
  * @param  force_off：[输入]释放时是否忽略 SPI_HOLD_ON_CS。
  * @retval 无。
  * @note   没有配置、片选描述或 GPIO 设备时不操作。忙等延时不主动让出 CPU。
  */
static inline void _spi_context_cs_control(struct spi_context *ctx,
					   bool on, bool force_off)
{
	if (ctx->config && ctx->config->cs && ctx->config->cs->gpio_dev) {    /* 缺少任一片选控制对象时不执行寄存器或 GPIO 操作。 */
		if (on) {                                                     /* 请求选中从设备，先驱动有效电平。 */
			gpio_pin_set(ctx->config->cs->gpio_dev,               /* 通过 GPIO 驱动写入片选电平，具体极性由末参数给出。 */
				     ctx->config->cs->gpio_pin,
				     spi_context_cs_active_value(ctx));
			k_busy_wait(ctx->config->cs->delay);                  /* 忙等配置的微秒间隔，满足片选建立或释放前等待要求。 */
		} else {
			if (!force_off &&                                     /* 普通释放路径允许 SPI_HOLD_ON_CS 保持选中。 */
			    ctx->config->operation & SPI_HOLD_ON_CS) {
				return;                                       /* 此路径无需继续修改上下文或片选，保留前面已说明的状态。 */
			}

			k_busy_wait(ctx->config->cs->delay);                  /* 忙等配置的微秒间隔，满足片选建立或释放前等待要求。 */
			gpio_pin_set(ctx->config->cs->gpio_dev,               /* 通过 GPIO 驱动写入片选电平，具体极性由末参数给出。 */
				     ctx->config->cs->gpio_pin,
				     spi_context_cs_inactive_value(ctx));
		}
	}
}

/**
  * @brief  按配置的保持片选策略控制 GPIO 片选。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  on：[输入]true 拉有效，false 请求释放。
  * @retval 无。
  * @note   不会强制覆盖 SPI_HOLD_ON_CS。
  */
static inline void spi_context_cs_control(struct spi_context *ctx, bool on)
{
	_spi_context_cs_control(ctx, on, false);    /* 使用普通策略，不强制覆盖保持片选选项。 */
}

/**
  * @brief  强制释放 GPIO 片选，并把空的总线信号量恢复为可用。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval 无。
  * @note   设备初始化和显式 release 使用；不是可递归锁，也不检查调用者身份。
  */
static inline void spi_context_unlock_unconditionally(struct spi_context *ctx)
{
	/* 强制释放 GPIO 片选，忽略 SPI_HOLD_ON_CS，避免释放锁后仍选中外设。 */
	_spi_context_cs_control(ctx, false, true);    /* 强制将软件片选置为无效。 */

	if (!k_sem_count_get(&ctx->lock)) {           /* 锁计数为零时才补充许可，避免重复增加计数。 */
		k_sem_give(&ctx->lock);               /* 归还一次总线访问许可。 */
	}
}

/**
  * @brief  将双向缓冲集合的首块映射到当前传输游标，并清零完成状态。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  tx_bufs：[输入]发送集合，可为空；非空时必须至少有一项。
  * @param  rx_bufs：[输入]接收集合，可为空；非空时必须至少有一项。
  * @param  dfs：[输入]每个数据字占用的字节数，必须大于 0；当前链路为 1。
  * @retval 无。
  * @note   长度以 len/dfs 转换，余数截断；本函数不验证 count 或缓冲长度对齐。
  */
static inline
void spi_context_buffers_setup(struct spi_context *ctx,
			       const struct spi_buf_set *tx_bufs,
			       const struct spi_buf_set *rx_bufs,
			       uint8_t dfs)
{
	LOG_DBG("tx_bufs %p - rx_bufs %p - %u", tx_bufs, rx_bufs, dfs);

	/* 映射发送方向；空集合与空数据缓冲是不同概念。 */
	if (tx_bufs) {                                                  /* 发送集合存在时映射首块，调用者须保证至少一项。 */
		ctx->current_tx = tx_bufs->buffers;                     /* 保存发送描述数组首项地址。 */
		ctx->tx_count = tx_bufs->count;                         /* 记录发送集合包含的块数。 */
		ctx->tx_buf = (const uint8_t *)ctx->current_tx->buf;    /* 把当前发送块地址转换为逐字节游标。 */
		ctx->tx_len = ctx->current_tx->len / dfs;               /* 将当前块字节长度换算为剩余数据字数。 */
	} else {
		ctx->current_tx = NULL;                                 /* 没有发送集合，不保留发送描述地址。 */
		ctx->tx_count = 0;                                      /* 标记没有待发送的块。 */
		ctx->tx_buf = NULL;                                     /* 发送集合不存在或已经耗尽，清除数据游标。 */
		ctx->tx_len = 0;                                        /* 标记当前没有待发送数据字。 */
	}

	/* 独立映射接收方向，长度同样从字节换算成数据字。 */
	if (rx_bufs) {                                                  /* 接收集合存在时映射首块，调用者须保证至少一项。 */
		ctx->current_rx = rx_bufs->buffers;                     /* 保存接收描述数组首项地址。 */
		ctx->rx_count = rx_bufs->count;                         /* 记录接收集合包含的块数。 */
		ctx->rx_buf = (uint8_t *)ctx->current_rx->buf;          /* 把当前接收块转换为可写字节游标。 */
		ctx->rx_len = ctx->current_rx->len / dfs;               /* 将当前接收块字节长度换算为数据字数。 */
	} else {
		ctx->current_rx = NULL;                                 /* 没有接收集合，不保留接收描述地址。 */
		ctx->rx_count = 0;                                      /* 标记没有待接收的块。 */
		ctx->rx_buf = NULL;                                     /* 接收集合不存在或已经耗尽，清除数据游标。 */
		ctx->rx_len = 0;                                        /* 标记当前没有待接收数据字。 */
	}

	/* 所有缓冲游标准备后，重置新请求的完成统计。 */
	ctx->sync_status = 0;                                           /* 清除上一请求的完成状态。 */

#ifdef CONFIG_SPI_SLAVE
	ctx->recv_frames = 0;                                           /* 新请求从零累计从机接收字数。 */
#endif                                                                  /* CONFIG_SPI_SLAVE */

	LOG_DBG("current_tx %p (%zu), current_rx %p (%zu),"
		    " tx buf/len %p/%zu, rx buf/len %p/%zu",
		    ctx->current_tx, ctx->tx_count,
		    ctx->current_rx, ctx->rx_count,
		    ctx->tx_buf, ctx->tx_len, ctx->rx_buf, ctx->rx_len);
}

/**
  * @brief  扣除已发送数据字并在当前块耗尽时切换下一块。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  dfs：[输入]每数据字字节数，必须与初始化时一致。
  * @param  len：[输入]本次已发送数据字数，不能超过当前 tx_len。
  * @retval 无；更新越界时记录错误并保留当前游标。
  * @note   调用者负责在硬件传输成功后更新；空数据指针可表示占位发送。
  */
static ALWAYS_INLINE
void spi_context_update_tx(struct spi_context *ctx, uint8_t dfs, uint32_t len)
{
	if (!ctx->tx_len) {                                                     /* 当前发送块为空或刚耗尽时，不从该块继续取字。 */
		return;                                                         /* 此路径无需继续修改上下文或片选，保留前面已说明的状态。 */
	}

	if (len > ctx->tx_len) {                                                /* 拒绝比当前发送块剩余量更大的推进请求。 */
		LOG_ERR("Update exceeds current buffer");                       /* 报告推进长度越过当前缓冲边界。 */
		return;                                                         /* 此路径无需继续修改上下文或片选，保留前面已说明的状态。 */
	}

	ctx->tx_len -= len;                                                     /* 扣除本轮实际发送的数据字数。 */
	if (!ctx->tx_len) {                                                     /* 当前发送块为空或刚耗尽时，不从该块继续取字。 */
		ctx->tx_count--;                                                /* 当前发送块耗尽，从剩余块数中扣除。 */
		if (ctx->tx_count) {                                            /* 还有发送描述时切换到下一块。 */
			ctx->current_tx++;                                      /* 推进一个 spi_buf 描述项。 */
			ctx->tx_buf = (const uint8_t *)ctx->current_tx->buf;    /* 把当前发送块地址转换为逐字节游标。 */
			ctx->tx_len = ctx->current_tx->len / dfs;               /* 将当前块字节长度换算为剩余数据字数。 */
		} else {
			ctx->tx_buf = NULL;                                     /* 发送集合不存在或已经耗尽，清除数据游标。 */
		}
	} else if (ctx->tx_buf) {                                               /* 当前块尚有数据且非占位缓冲时才推进实际地址。 */
		ctx->tx_buf += dfs * len;                                       /* 将发送游标前移本轮已发送字节数。 */
	}

	LOG_DBG("tx buf/len %p/%zu", ctx->tx_buf, ctx->tx_len);
}

/**
  * @brief  检查当前发送块是否还剩数据字。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval true：tx_len 非零；false：当前块没有剩余字。
  * @note   不检查 tx_buf 是否为空。
  */
static ALWAYS_INLINE
bool spi_context_tx_on(struct spi_context *ctx)
{
	return !!(ctx->tx_len);    /* 把发送剩余字数转换成布尔值。 */
}

/**
  * @brief  检查当前发送块是否既有有效数据地址又有剩余数据字。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval true：可从实际发送缓冲取数；false：无实际发送数据。
  * @note   空指针占位发送与有实际数据的发送由此区分。
  */
static ALWAYS_INLINE
bool spi_context_tx_buf_on(struct spi_context *ctx)
{
	return !!(ctx->tx_buf && ctx->tx_len);    /* 同时要求真实发送地址与剩余字数。 */
}

/**
  * @brief  累计从机接收字数并推进接收缓冲游标。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @param  dfs：[输入]每数据字字节数，必须与初始化时一致。
  * @param  len：[输入]本次接收数据字数，不能超过当前 rx_len。
  * @retval 无；更新越界时记录错误并停止缓冲推进。
  * @note   从机 recv_frames 在长度检查前累加；调用者必须保证传入计数正确。
  */
static ALWAYS_INLINE
void spi_context_update_rx(struct spi_context *ctx, uint8_t dfs, uint32_t len)
{
#ifdef CONFIG_SPI_SLAVE
	if (spi_context_is_slave(ctx)) {                                  /* 仅从机请求累计接收字数。 */
		ctx->recv_frames += len;                                  /* 将本轮接收字数加入当前请求总数。 */
	}

#endif                                                                    /* CONFIG_SPI_SLAVE */

	if (!ctx->rx_len) {                                               /* 当前接收块没有剩余空间或刚耗尽。 */
		return;                                                   /* 此路径无需继续修改上下文或片选，保留前面已说明的状态。 */
	}

	if (len > ctx->rx_len) {                                          /* 拒绝越过当前接收块边界的推进请求。 */
		LOG_ERR("Update exceeds current buffer");                 /* 报告推进长度越过当前缓冲边界。 */
		return;                                                   /* 此路径无需继续修改上下文或片选，保留前面已说明的状态。 */
	}

	ctx->rx_len -= len;                                               /* 扣除本轮已接收的数据字数。 */
	if (!ctx->rx_len) {                                               /* 当前接收块没有剩余空间或刚耗尽。 */
		ctx->rx_count--;                                          /* 当前接收块耗尽，从剩余块数中扣除。 */
		if (ctx->rx_count) {                                      /* 还有接收描述时切换下一块。 */
			ctx->current_rx++;                                /* 推进一个接收 spi_buf 描述项。 */
			ctx->rx_buf = (uint8_t *)ctx->current_rx->buf;    /* 把当前接收块转换为可写字节游标。 */
			ctx->rx_len = ctx->current_rx->len / dfs;         /* 将当前接收块字节长度换算为数据字数。 */
		} else {
			ctx->rx_buf = NULL;                               /* 接收集合不存在或已经耗尽，清除数据游标。 */
		}
	} else if (ctx->rx_buf) {                                         /* 非丢弃接收且当前块尚未耗尽时推进实际地址。 */
		ctx->rx_buf += dfs * len;                                 /* 将接收游标前移本轮已写入的字节数。 */
	}

	LOG_DBG("rx buf/len %p/%zu", ctx->rx_buf, ctx->rx_len);
}

/**
  * @brief  检查当前接收块是否还剩待接收数据字。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval true：rx_len 非零；false：当前块没有剩余字。
  * @note   不检查 rx_buf 是否为空。
  */
static ALWAYS_INLINE
bool spi_context_rx_on(struct spi_context *ctx)
{
	return !!(ctx->rx_len);    /* 把接收剩余字数转换为布尔值。 */
}

/**
  * @brief  检查当前接收块是否有可写数据地址和待接收数据字。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval true：接收数据可存入实际缓冲；false：无实际接收空间。
  * @note   空指针可表示丢弃接收数据。
  */
static ALWAYS_INLINE
bool spi_context_rx_buf_on(struct spi_context *ctx)
{
	return !!(ctx->rx_buf && ctx->rx_len);    /* 同时要求真实接收空间及待接收数据字。 */
}

/**
  * @brief  确定本轮可共同推进的数据字数。
  * @param  ctx：[输入/输出]驱动实例的有效上下文，不可为空。
  * @retval 仅单方向有数据时返回其长度；双向都有数据时返回两者较小长度。
  * @note   函数名含 longest，但实现取双向最小非零长度；这保证不越过任一当前块边界。
  */
static inline size_t spi_context_longest_current_buf(struct spi_context *ctx)
{
	if (!ctx->tx_len) {                        /* 当前发送块为空或刚耗尽时，不从该块继续取字。 */
		return ctx->rx_len;                /* 返回接收长度；双向分支中它不大于发送长度。 */
	} else if (!ctx->rx_len) {                 /* 仅发送方向有剩余数据时返回发送长度。 */
		return ctx->tx_len;                /* 返回发送长度；在双向分支中它是较小长度。 */
	} else if (ctx->tx_len < ctx->rx_len) {    /* 双向均有数据时选择较短块，避免越界推进。 */
		return ctx->tx_len;                /* 返回发送长度；在双向分支中它是较小长度。 */
	}

	return ctx->rx_len;                        /* 返回接收长度；双向分支中它不大于发送长度。 */
}

#ifdef __cplusplus
}
#endif

#endif    /* ZEPHYR_DRIVERS_SPI_SPI_CONTEXT_H_ */
