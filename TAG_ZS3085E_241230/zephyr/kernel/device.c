/* 设备模型实现：链接器汇集静态设备和启动项，按阶段调用初始化函数，记录成功状态后device_get_binding才返回可用设备。SPI_2由spi_acts.c注册；名称匹配只在已初始化成功的对象中搜索。用户态验证分支仅在CONFIG_USERSPACE使能时存在。 */
/*
 * Copyright (c) 2015-2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <device.h>
#include <sys/atomic.h>
#include <syscall_handler.h>

extern const struct init_entry __init_start[];                 /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
extern const struct init_entry __init_PRE_KERNEL_1_start[];    /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
extern const struct init_entry __init_PRE_KERNEL_2_start[];    /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
extern const struct init_entry __init_POST_KERNEL_start[];     /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
extern const struct init_entry __init_APPLICATION_start[];     /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
extern const struct init_entry __init_POWER_MAX_start[];       /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
extern const struct init_entry __init_end[];                   /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */

#ifdef CONFIG_SMP
extern const struct init_entry __init_SMP_start[];    /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
#endif

extern const struct device __device_start[];    /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */
extern const struct device __device_end[];      /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */

extern uint32_t __device_init_status_start[];    /* 链接器提供的静态区边界或状态区地址声明，本文件不定义其存储。 */

/**
  * @brief  登记全部静态设备对象的内核对象状态。
  * @param  无。
  * @retval 无。
  * @note   state对象原本零初始化，这里补充用户态对象跟踪等初始化。
  */
void z_device_state_init(void)
{
    const struct device *dev = __device_start;    /* 从链接器收集的第一个静态设备开始。 */

    while (dev < __device_end) {                  /* 遍历直到设备区尾，不越过结束边界。 */
        z_object_init(dev);                       /* 登记内核对象的初始化状态。 */
        ++dev;                                    /* 推进到下一个连续静态设备对象。 */
    }
}

/**
  * @brief  按链接器给定边界执行指定阶段的启动项。
  * @param  level：[输入] 有效_SYS_INIT_LEVEL_*索引，调用方保证范围。
  * @retval 无。
  * @note   按阶段表顺序执行，设备初始化错误取绝对值并饱和到UINT8_MAX后保存；服务项dev可为空。
  */
void z_sys_init_run_level(int32_t level)
{
    static const struct init_entry *levels[] = {                               /* 阶段索引到链接段起点的固定映射；末尾额外保存结束边界。 */
        __init_PRE_KERNEL_1_start,
        __init_PRE_KERNEL_2_start,
        __init_POST_KERNEL_start,
        __init_APPLICATION_start,
#ifdef CONFIG_SMP
        __init_SMP_start,
#endif
		__init_POWER_MAX_start,
        /* 最后阶段的结束边界，不是可执行启动项。 */
        __init_end,
    };
    const struct init_entry *entry;                                            /* 保存当前启动项地址。 */

    for (entry = levels[level]; entry < levels[level+1]; entry++) {            /* 只遍历本阶段起点到下一阶段起点之间的启动项。 */
        const struct device *dev = entry->dev;                                 /* 设备项传入对应对象，服务项传NULL。 */

#ifdef CONFIG_DEVICE_DEBUG
        if(dev){                                                               /* 设备调试输出包含设备名，服务项走地址输出。 */
            printk("z_sys_init dev(%s) func:%p\n", dev->name, entry->init);    /* 调试构建打印设备名及初始化函数地址。 */
        }else{
            printk("z_sys_init entry(%p) func:%p\n", entry, entry->init);      /* 调试构建打印无设备服务项地址和函数地址。 */
        }
#endif
        int rc = entry->init(dev);                                             /* 经回调函数指针执行驱动或服务初始化。 */

        if (dev != NULL) {                                                     /* 只有设备项有state可以记录结果。 */
            /* 回调结束即标记已初始化；失败时另存非零错误，使ready检查拒绝此设备。 */
            if (rc != 0) {                                                     /* 非0初始化返回值视为设备失败。 */
                if (rc < 0) {                                                  /* 回调负值请求停止后续访问。 */
                    rc = -rc;                                                  /* 取错误绝对值，便于压缩存入8位状态。 */
                }
                if (rc > UINT8_MAX) {                                          /* 超出8位记录范围时饱和，保持非零失败语义。 */
                    rc = UINT8_MAX;                                            /* 使用最大可表示错误编码255。 */
                }
                dev->state->init_res = rc;                                     /* 保存初始化错误，后续ready据此拒绝设备。 */
            }
            dev->state->initialized = true;                                    /* 标记回调已执行，成功失败都到达此状态。 */
        }
    }
}

/**
  * @brief  按名称查找已经初始化成功的静态设备。
  * @param  name：[输入] 零结尾只读名称；允许NULL或空串，两者返回NULL。
  * @retval 匹配设备地址：静态只读对象，不转移所有权；NULL：名称无效或没有就绪匹配。
  * @note   先地址后字符串内容比较，返回对象的api才绑定到实际驱动函数表。
  */
const struct device *z_impl_device_get_binding(const char *name)
{
    const struct device *dev;                                           /* 保存设备名称检索游标。 */

    /* 空指针及空字符串均不指向任何设备。 */
    if ((name == NULL) || (name[0] == '\0')) {                          /* 拒绝空名称，避免字符串读取或误绑定。 */
        return NULL;                                                    /* 没有可安全返回的匹配设备。 */
    }

    /* 常见CONFIG名称在ROM中共享地址，先比较指针可避免逐字符比较；未命中后再用strcmp按内容搜索。 */
    for (dev = __device_start; dev != __device_end; dev++) {            /* 遍历全部静态设备，不搜索动态分配对象。 */
        if (z_device_ready(dev) && (dev->name == name)) {               /* 同名常量地址且设备就绪时快速命中。 */
            return dev;                                                 /* 返回静态设备对象，应用通过其API表调用驱动。 */
        }
    }

    for (dev = __device_start; dev != __device_end; dev++) {            /* 遍历全部静态设备，不搜索动态分配对象。 */
        if (z_device_ready(dev) && (strcmp(name, dev->name) == 0)) {    /* 地址不同但内容相同且设备就绪时匹配。 */
            return dev;                                                 /* 返回静态设备对象，应用通过其API表调用驱动。 */
        }
    }

#ifdef CONFIG_DEVICE_DEBUG
	printk("!!!ERR: dev %s not found\n", name);                     /* 仅设备调试构建输出未绑定的名称。 */
#endif

    return NULL;                                                        /* 没有可安全返回的匹配设备。 */
}

#ifdef CONFIG_USERSPACE

/**
  * @brief  复制并验证用户态设备名后执行内核查找。
  * @param  name：[输入] 用户态可读零结尾字符串，长度须小于内部名称上限。
  * @retval 设备地址或NULL；复制失败返回NULL。
  * @note   仅CONFIG_USERSPACE分支编译，避免直接信任用户内存。
  */
static inline const struct device *z_vrfy_device_get_binding(const char *name)
{
    char name_copy[Z_DEVICE_MAX_NAME_LEN];          /* 内核栈副本，限制用户字符串扫描范围。 */

    /* 受限复制用户态名称，失败时不把用户指针交给设备查找。 */
    if (z_user_string_copy(name_copy, (char *)name, sizeof(name_copy))
        != 0) {
        return NULL;                                /* 没有可安全返回的匹配设备。 */
    }

    return z_impl_device_get_binding(name_copy);    /* 用验证后的内核副本查找设备。 */
}
#include <syscalls/device_get_binding_mrsh.c>

/**
  * @brief  验证用户设备对象后转调设备可用性检查。
  * @param  dev：[输入] 已初始化且用户可访问的设备对象。
  * @retval 实现层可用性检查返回值；对象验证失败触发系统调用异常。
  * @note   仅CONFIG_USERSPACE分支编译。
  */
static inline int z_vrfy_device_usable_check(const struct device *dev)
{
    Z_OOPS(Z_SYSCALL_OBJ_INIT(dev, K_OBJ_ANY));    /* 要求设备对象已初始化且当前用户线程有访问权限，失败触发异常。 */

    return z_impl_device_usable_check(dev);        /* 返回实际可用性检查状态。 */
}
#include <syscalls/device_usable_check_mrsh.c>
#endif    /* CONFIG_USERSPACE */

/**
  * @brief  返回静态设备数组的起点和元素数量。
  * @param  devices：[输出] 非空可写指针对象，接收静态设备表首地址，不得释放。
  * @retval 静态设备元素数量，单位项，包括未就绪对象。
  */
size_t z_device_get_all_static(struct device const **devices)
{
    *devices = __device_start;    /* 输出静态设备区首地址，不分配新数组。 */
    return __device_end - __device_start;    /* 按指针差返回设备项数，而非字节数。 */
}

/**
  * @brief  检查设备是否完成初始化且没有初始化错误。
  * @param  dev：[输入] 真实静态设备对象或NULL；任意非法非空地址不受保护。
  * @retval true：已初始化且init_res为0；false：NULL、未初始化或初始化失败。
  */
bool z_device_ready(const struct device *dev)
{
    /* NULL设备报告未就绪；非空指针仍须指向真实device对象，不验证任意地址是否合法。 */
    if (dev == NULL) {                                                 /* NULL明确视为不可用设备。 */
        return false;                                                  /* 空设备不允许进入驱动调用。 */
    }

    return dev->state->initialized && (dev->state->init_res == 0U);    /* 同时要求初始化已执行且结果为成功。 */
}

/**
  * @brief  对设备的每个静态必需依赖执行访问回调。
  * @param  dev：[输入] 有效设备对象，其句柄表须已由构建/链接建立。
  * @param  visitor_cb：[输入] 非空回调，接收依赖设备和context；负返回值要求提前停止。
  * @param  context：[输入输出] 原样传给回调的用户上下文，可为空，实际约定由回调决定。
  * @retval 非负：访问的句柄数量；负值：首个回调错误，后续项未访问。
  * @note   不动态分配设备，回调中的对象仍归设备模型持有。
  */
int device_required_foreach(const struct device *dev,
              device_visitor_callback_t visitor_cb,
              void *context)
{
    size_t handle_count = 0;                                   /* 保存必需依赖设备句柄数量。 */
    const device_handle_t *handles =                           /* 取得依赖句柄表及数量；该指针指向静态依赖数据。 */
        device_required_handles_get(dev, &handle_count);

    /* 逐项访问静态必需设备句柄，回调负值时立即停止。 */
    for (size_t i = 0; i < handle_count; ++i) {                /* 按句柄表顺序访问所有必需依赖。 */
        device_handle_t dh = handles[i];                       /* 取当前依赖的紧凑句柄编号。 */
        const struct device *rdev = device_from_handle(dh);    /* 将句柄转换为设备数组中的对象地址。 */
        int rc = visitor_cb(rdev, context);                    /* 以原用户上下文调用访问回调。 */

        if (rc < 0) {                                          /* 回调负值请求停止后续访问。 */
            return rc;                                         /* 传播访问回调错误。 */
        }
    }

    return handle_count;                                       /* 全部访问完成后返回依赖设备项数。 */
}
