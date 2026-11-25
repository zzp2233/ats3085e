/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file GPIO 控制器驱动共用的端口掩码和回调链表操作。
 */

#ifndef ZEPHYR_DRIVERS_GPIO_GPIO_UTILS_H_    /* 防止多个 GPIO 驱动重复引入同一组静态内联函数。 */
#define ZEPHYR_DRIVERS_GPIO_GPIO_UTILS_H_

/* 把端口引脚数换成连续低位掩码，使用 64 位中间值处理 32 引脚端口。 */
#define GPIO_PORT_PIN_MASK_FROM_NGPIOS(ngpios)			\
	((gpio_port_pins_t)(((uint64_t)1 << (ngpios)) - 1U))

/* 按指定设备树节点 ngpios 属性生成该端口的有效掩码。 */
#define GPIO_PORT_PIN_MASK_FROM_DT_NODE(node_id)		\
	GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_PROP(node_id, ngpios))

/* 由当前驱动实例的设备树属性生成同样的掩码。 */
#define GPIO_PORT_PIN_MASK_FROM_DT_INST(inst)			\
	GPIO_PORT_PIN_MASK_FROM_NGPIOS(DT_INST_PROP(inst, ngpios))


/**
  * @brief  把一个回调注册到端口链表，或从该链表注销。
  * @param  callbacks：[输入/输出]有效的链表对象，允许空链表。
  * @param  callback：[输入/输出]包含处理函数和链表节点的非空对象，注册期间须持续有效。
  * @param  set：[输入]真为注册，假为注销。
  * @retval 0 为完成或从空链表注销；在非空链表中找不到待注销节点时返回 -EINVAL。
  * @note   函数不加锁，调用者按驱动约定协调列表修改和中断回调。
  */
static inline int gpio_manage_callback(sys_slist_t *callbacks,
					struct gpio_callback *callback,
					bool set)
{
	__ASSERT(callback, "No callback!");                                      /* 注册对象须有可用的链表节点。 */
	__ASSERT(callback->handler, "No callback handler!");                     /* 回调将从中断上下文调用，函数指针须有效。 */

	/* 注册时先移除可能已有的同一节点，避免在单链表内重复链接。 */
	if (!sys_slist_is_empty(callbacks)) {
		if (!sys_slist_find_and_remove(callbacks, &callback->node)) {    /* 在非空链表中查找并摘除同一内嵌节点。 */
			if (!set) {                                              /* 注册时找不到旧节点是首次注册，注销时则是错误。 */
				return -EINVAL;                                  /* 向注销方报告该节点不存在。 */
			}
		}
	}

	/* 新注册的回调放到链表头，注销路径在上面完成。 */
	if (set) {
		sys_slist_prepend(callbacks, &callback->node);                   /* 链表只保存节点地址，不复制回调对象。 */
	}

	return 0;                                                                /* 链表状态已按请求处理。 */
}


/**
  * @brief  向订阅了本次待决引脚的回调分发中断。
  * @param  list：[输入]非空的端口回调链表，可为空链表。
  * @param  port：[输入]分发方传来的 GPIO 设备引用，原样交给处理函数。
  * @param  pins：[输入]本次待决引脚的端口位图。
  * @retval 无。
  * @note   由 GPIO ISR 调用。遍历前保存下一节点，回调收到订阅掩码与本次待决位的交集。
  */
static inline void gpio_fire_callbacks(sys_slist_t *list,
					const struct device *port,
					uint32_t pins)
{
	struct gpio_callback *cb, *tmp;                                   /* 分别保存当前和后继节点，支持遍历中摘除当前项。 */

	/* 遍历已登记节点，为每个处理函数传递它关心的引脚交集。 */
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(list, cb, tmp, node) {
		if (cb->pin_mask & pins) {                                /* 只有本次触发位与订阅掩码有交集时才通知。 */
			__ASSERT(cb->handler, "No callback handler!");    /* 分发前断言已登记处理函数。 */
			cb->handler(port, cb, cb->pin_mask & pins);       /* 保留实际设备参数，仅向该处理函数交付命中的位。 */
		}
	}
}

#endif    /* ZEPHYR_DRIVERS_GPIO_GPIO_UTILS_H_ */
