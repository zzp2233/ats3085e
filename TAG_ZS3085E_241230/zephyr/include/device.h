/* 设备模型公共头：device_get_binding只返回已初始化成功的设备，DEVICE_GET/DEVICE_DT_GET只取静态地址。头文件保护与extern C防止重复展开及C++名称改编。包含init、链接段、电源、MMIO和工具宏，SPI本次通过DEVICE_DEFINE绑定实际驱动API。 */
/*
 * Copyright (c) 2015 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DEVICE_H_
#define ZEPHYR_INCLUDE_DEVICE_H_

/** @brief 设备驱动接口。
 * @defgroup io_interfaces 设备驱动接口
 * @{
 * @}
 */
/** @brief 杂项设备接口。
 * @defgroup misc_interfaces 杂项设备接口
 * @ingroup io_interfaces
 * @{
 * @}
 */
/** @brief 设备模型接口。
 * @defgroup device_model 设备模型接口
 * @{
 */

#include <init.h>
#include <linker/sections.h>
#include <pm/device.h>
#include <sys/device_mmio.h>
#include <sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 设备/服务的16位紧凑句柄。正数映射静态设备数组，0及两个极值有特殊含义；负数可表示系统时钟、SYS_INIT服务等非设备功能。 */
typedef int16_t device_handle_t;

/** 句柄表分组分隔符，取int16_t最小值，不能作为设备索引。 */
#define DEVICE_HANDLE_SEP INT16_MIN

/** 句柄表结束标记，取int16_t最大值。 */
#define DEVICE_HANDLE_ENDS INT16_MAX

/** 未知或空设备句柄0。 */
#define DEVICE_HANDLE_NULL 0

#define Z_DEVICE_MAX_NAME_LEN	48    /* 设备名上限，用户态复制缓冲容量为48字节。 */

/** 将name符号拼为DEVICE_DEFINE生成的完整设备对象符号；用于前置extern声明及DEVICE_GET。name不是运行时设备名字符串。 */
#define DEVICE_NAME_GET(name) _CONCAT(__device_, name)

/** 生成系统服务设备及启动项；drv_name为运行时名，init_fn为初始化回调，pm_control_fn可为空，level/prio指定启动阶段及0～99优先级。宏将data、config、api置NULL，仍传递提供的电源回调；不会分配堆内存。 */
#define SYS_DEVICE_DEFINE(drv_name, init_fn, pm_control_fn, level, prio) \
    DEVICE_DEFINE(Z_SYS_NAME(init_fn), drv_name, init_fn,		\
              pm_control_fn,					\
              NULL, NULL, level, prio, NULL)

/**
 * 创建静态设备及启动项。dev_name为对象符号名，drv_name为运行时检索字符串（受48字节含终止符的名称上限约束）。
 * init_fn为int(const struct device*)初始化函数；pm_control_fn为可选电源回调。
 * data_ptr指向长期有效可变私有状态，cfg_ptr指向长期有效只读配置，api_ptr指向驱动API函数表，可为空。
 * level为SYS_INIT阶段标识，prio为同阶段0～99的十进制优先级字面量或等价宏，值小先执行。
 * 宏生成ROM设备、RAM状态与链接段入口，kernel/device.c才实际执行初始化；实例空闲电源管理启用时，驱动须建立适当初始挂起状态。
 * api非空且对象权限满足时才可能供用户态调用，不能仅凭此宏保证所有设备都可从用户态访问。
 */
#define DEVICE_DEFINE(dev_name, drv_name, init_fn, pm_control_fn,	\
              data_ptr, cfg_ptr, level, prio, api_ptr)		\
    Z_DEVICE_DEFINE(DT_INVALID_NODE, dev_name, drv_name, init_fn,	\
            pm_control_fn,					\
            data_ptr, cfg_ptr, level, prio, api_ptr)

/** 从设备树node_id生成运行时名称：有label属性则使用其值，否则使用完整节点名及单元地址。结果为字符串常量，不生成设备。 */
#define DEVICE_DT_NAME(node_id) \
    DT_PROP_OR(node_id, label, DT_NODE_FULL_NAME(node_id))

/** DEVICE_DEFINE的设备树版本：node_id决定对象符号及运行时名，其余init_fn、pm_control_fn、data_ptr、cfg_ptr、level、prio、api_ptr契约相同。尾部可选参数传给内部依赖处理宏；设备为外部可见，DEVICE_DT_GET取地址后须用device_is_ready检查。 */
#define DEVICE_DT_DEFINE(node_id, init_fn, pm_control_fn,		\
             data_ptr, cfg_ptr, level, prio,		\
             api_ptr, ...)					\
    Z_DEVICE_DEFINE(node_id, Z_DEVICE_DT_DEV_NAME(node_id),		\
            DEVICE_DT_NAME(node_id), init_fn,		\
            pm_control_fn,					\
            data_ptr, cfg_ptr, level, prio,			\
            api_ptr, __VA_ARGS__)

/** 用DT_DRV_COMPAT对应的inst实例展开DEVICE_DT_DEFINE；inst为编译时实例号，其他参数原样传递，不做运行时设备搜索。 */
#define DEVICE_DT_INST_DEFINE(inst, ...) \
    DEVICE_DT_DEFINE(DT_DRV_INST(inst), __VA_ARGS__)

/** 将node_id转换为设备对象符号，供extern声明或DEVICE_DT_GET使用；结果不是设备名称字符串。 */
#define DEVICE_DT_NAME_GET(node_id) DEVICE_NAME_GET(Z_DEVICE_DT_DEV_NAME(node_id))

/** 返回node_id对应静态设备对象地址；该地址本身不保证初始化成功，调用API前检查device_is_ready。 */
#define DEVICE_DT_GET(node_id) (&DEVICE_DT_NAME_GET(node_id))

/** 取得DT_DRV_COMPAT对应inst实例的静态设备地址，仍须检查就绪状态。 */
#define DEVICE_DT_INST_GET(inst) DEVICE_DT_GET(DT_DRV_INST(inst))

/** compat为小写下划线形式的设备树兼容标识。存在启用节点则任选一个对应设备地址，否则为NULL；多个匹配时无固定选择保证，非空也须检查device_is_ready。 */
#define DEVICE_DT_GET_ANY(compat)					    \
    COND_CODE_1(DT_HAS_COMPAT_STATUS_OKAY(compat),			    \
            (DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(compat))), \
            (NULL))

/** 与DEVICE_DT_GET_ANY相同地任选启用的compat设备，但无匹配时触发编译错误；取得地址不等于已初始化。 */
#define DEVICE_DT_GET_ONE(compat)					    \
    COND_CODE_1(DT_HAS_COMPAT_STATUS_OKAY(compat),			    \
            (DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(compat))), \
            (ZERO_OR_COMPILE_ERROR(0)))

/** 返回DEVICE_DEFINE中name符号对应的静态设备对象地址；与按运行时字符串搜索的device_get_binding不同，不自动检查状态。 */
#define DEVICE_GET(name) (&DEVICE_NAME_GET(name))

/** 在文件作用域前置声明静态设备，使初始化函数可先用DEVICE_GET引用，随后再完整DEVICE_DEFINE，解决初始化函数与注册宏互相引用。name须与后续定义一致。 */
#define DEVICE_DECLARE(name) static const struct device DEVICE_NAME_GET(name)

/* 旧电源回调类型：dev为设备、status为操作状态、context/arg为调用方上下文；无返回值。此类型声明本身未规定触发时机、参数空值或所有权，实际电源接口另见pm/device.h。 */
typedef void (*device_pm_cb)(const struct device *dev,
                 int status, void *context, void *arg);

/** 每设备一份RAM动态状态，默认零初始化；设备框架在访问前建立非零配置。ROM中的struct device通过state指向此对象。 */
struct device_state {
    /** 初始化返回值的绝对值，超过8位时饱和为UINT8_MAX；initialized为true且本字段为0才表示成功。 */
    unsigned int init_res : 8;

    /** 初始化回调是否已经执行；true不代表回调一定成功，须同时检查init_res。 */
    bool initialized : 1;

#ifdef CONFIG_PM_DEVICE
    /* 启用CONFIG_PM_DEVICE时保存在RAM中的电源管理状态。 */
    struct pm_device pm;
#endif    /* CONFIG_PM_DEVICE */
};

/** 每设备一份ROM只读设备描述；指向的state/data在RAM中可变，函数表和静态硬件配置通常只读。 */
struct device {
    /** 长期有效的运行时设备名称字符串，用于device_get_binding。 */
    const char *name;
    /** 实例硬件配置地址；SPI驱动转换为acts_spi_config，非应用的spi_config。 */
    const void *config;
    /** 驱动API函数表地址；SPI公共包装转换为spi_driver_api后调用transceive/release。 */
    const void *api;
    /** 指向RAM中设备公共初始化/电源状态。 */
    struct device_state * const state;
    /** 指向驱动私有RAM状态；SPI为acts_spi_data，其中含总线锁与缓冲游标。 */
    void * const data;
    /** 可选分组句柄表，编码设备依赖等关系；device_required_handles_get取第一组。指针及表均长期有效，结束符不可缺少。 */
    const device_handle_t *const handles;
#ifdef CONFIG_PM_DEVICE
    /** 可选设备电源控制回调，由电源管理流程调用。 */
    pm_device_control_callback_t pm_control;
    /** 指向该实例的RAM电源管理对象，不转移所有权。 */
    struct pm_device * const pm;
#endif
};

/**
  * @brief  将连续静态设备表中的对象转换为紧凑句柄。
  * @param  dev：[输入] 静态设备表内的真实对象或NULL；不支持表外任意对象。
  * @retval NULL返回DEVICE_HANDLE_NULL；其他为设备表索引加1。
  * @note   不检查非空指针是否落在表内，调用者须保证来源。
  */
static inline device_handle_t
device_handle_get(const struct device *dev)
{
    device_handle_t ret = DEVICE_HANDLE_NULL;                 /* 默认返回空句柄，NULL对象不进入指针差运算。 */
    extern const struct device __device_start[];              /* 链接器提供的静态设备区起点，本处仅声明。 */

    /* TODO：当前转换只适用于连续静态设备表；未来若支持表外构造的设备，须换用其他句柄方案。 */
    if (dev != NULL) {                                        /* 只有非空设备才按静态数组位置生成句柄。 */
        ret = 1 + (device_handle_t)(dev - __device_start);    /* 索引加1使有效设备句柄从1开始，0保留给NULL。 */
    }

    return ret;                                               /* 返回生成的句柄。 */
}

/**
  * @brief  把正的设备句柄转换为静态设备地址。
  * @param  dev_handle：[输入] 1～设备数的句柄，其他值视为无效。
  * @retval 有效静态设备地址，或NULL；未检查设备初始化状态。
  */
static inline const struct device *
device_from_handle(device_handle_t dev_handle)
{
    extern const struct device __device_start[];                 /* 链接器提供的静态设备区起点，本处仅声明。 */
    extern const struct device __device_end[];                   /* 静态设备区结束边界，不是可用设备。 */
    const struct device *dev = NULL;                             /* 无效句柄默认返回空设备。 */
    size_t numdev = __device_end - __device_start;               /* 通过链接段边界计算静态设备项数。 */

    if ((dev_handle > 0) && ((size_t)dev_handle <= numdev)) {    /* 只接受1～设备数量，排除分隔/结束标记及负服务编号。 */
        dev = &__device_start[dev_handle - 1];                   /* 将1起始句柄换为0起始数组索引。 */
    }

    return dev;                                                  /* 返回对应静态对象或NULL，不保证初始化成功。 */
}

/** 设备遍历回调：访问器框架对每个依赖调用visitor(dev,context)。dev为静态设备，context为原样传入的用户状态，可为空。非负返回继续，负错误停止遍历并传播；对象不转移所有权。 */
typedef int (*device_visitor_callback_t)(const struct device *dev, void *context);

/**
  * @brief  取得设备句柄表中的第一组必需依赖。
  * @param  dev：[输入] 非空真实设备对象，handles表若非空须正确终止。
  * @param  count：[输出] 非空可写数量对象；仅返回表非空时写入，可写0。
  * @retval 指向count个静态句柄的只读表；无表时返回NULL且不写count。
  * @note   扫描到DEVICE_HANDLE_SEP或DEVICE_HANDLE_ENDS停止；不复制表、不检查表容量。
  */
static inline const device_handle_t *
device_required_handles_get(const struct device *dev,
                size_t *count)
{
    const device_handle_t *rv = dev->handles;    /* 取得可选静态句柄表首地址。 */

    if (rv != NULL) {                            /* 无表时保留调用者原count值。 */
        size_t i = 0;                            /* 第一组依赖句柄计数，从0开始扫描。 */

        /* 扫描当前组直到结束标记或分隔标记，依赖构建保证句柄表有终止符。 */
        while ((rv[i] != DEVICE_HANDLE_ENDS)
               && (rv[i] != DEVICE_HANDLE_SEP)) {
            ++i;                                 /* 尚未遇到组分隔或表结束，增加依赖项数。 */
        }
        *count = i;    /* 输出第一组必需依赖设备数量。 */
    }

    return rv;                                   /* 返回静态句柄表视图，不分配内存。 */
}

/** 直接依赖访问可用于检查下层I2C等设备或释放资源；访问顺序无保证，非特权线程不能直接使用。具体声明在后面device_required_foreach处。 */


/**
 * @}
 */

/** @brief 设备电源管理接口。
 * @defgroup device_power_management_api 设备电源管理接口
 * @ingroup power_management_api
 * @{
 */

#if 0


/** 固定禁用的旧电源状态：ACTIVE表示正常工作，全部上下文保留。 */
#define DEVICE_PM_ACTIVE_STATE          1

/** 固定禁用的旧电源状态：低功耗且硬件保留上下文，驱动不必恢复。 */
#define DEVICE_PM_LOW_POWER_STATE       2

/** 固定禁用的旧电源状态：挂起可能丢失大部分硬件上下文，驱动须保存/恢复或重新初始化。 */
#define DEVICE_PM_SUSPEND_STATE         3

/** 固定禁用的旧强制挂起状态：完成正在处理的事务后挂起，不再处理排队及新请求；丢失上下文须由驱动恢复。 */
#define DEVICE_PM_FORCE_SUSPEND_STATE	4

/** 固定禁用的旧断电状态：设备完全失电、上下文丢失，再上电需重新初始化。 */

#define DEVICE_PM_OFF_STATE             5


/** 固定禁用的旧早期挂起状态编码。 */
#define DEVICE_PM_EARLY_SUSPEND_STATE    6

/** 固定禁用的旧晚期恢复状态编码；宏名为LATE_RESUME，勿沿用原早期恢复误述。 */
#define DEVICE_PM_LATE_RESUME_STATE    7



/* 固定禁用的旧电源命令编码：设置或读取状态。 */
#define DEVICE_PM_SET_POWER_STATE       1
#define DEVICE_PM_GET_POWER_STATE       2

/**
  * @brief  保留固定禁用的旧设备电源状态查询占位。
  * @param  dev：[输入] 旧接口设备对象，此实现未使用。
  * @param  device_power_state：[输出] 旧接口状态接收区，此实现不会写入。
  * @retval 固定-ENOTSUP：不支持。
  * @note   位于#if 0，当前不参与构建。
  */
static inline int device_get_power_state(const struct device *dev,
                     uint32_t *device_power_state)
{
    return -ENOTSUP;    /* 旧占位接口不读取硬件或填写输出。 */
}

#endif    /* CONFIG_PM_DEVICE */

/**
  * @brief  对设备的每个直接必需依赖调用访问器。
  * @param  dev：[输入] 非空有效设备对象。
  * @param  visitor_cb：[输入] 非空回调，非负继续，负值停止。
  * @param  context：[输入输出] 原样传给回调的用户状态，回调允许时可为空。
  * @retval 全部成功返回访问项数，否则返回首个负回调错误。
  * @note   仅特权上下文使用，无固定访问顺序，不递归自动遍历间接依赖。
  */
int device_required_foreach(const struct device *dev,
              device_visitor_callback_t visitor_cb,
              void *context);

/**
  * @brief  按运行时名称查找已初始化成功的静态设备。
  * @param  name：[输入] 可读零结尾设备名；NULL或空串返回NULL。
  * @retval 静态设备对象地址，或NULL表示未找到/不可用，不转移所有权。
  * @note   SPI_2由spi_acts.c注册；先比较名称指针，再逐字符比较。用户态名称须满足长度及内存权限约束。
  */
__syscall const struct device *device_get_binding(const char *name);

/**
  * @brief  取得全部静态设备数组视图。
  * @param  devices：[输出] 非空可写指针对象，接收只读静态设备表，不得修改或释放。
  * @retval 静态设备项数，包含未就绪设备。
  */
size_t z_device_get_all_static(const struct device * *devices);

/**
  * @brief  检查设备是否已初始化且返回成功。
  * @param  dev：[输入] 真实设备对象或NULL，不能传任意非法非空指针。
  * @retval true：初始化已执行且init_res为0；false：其他情况。
  */
bool z_device_ready(const struct device *dev);

/**
  * @brief  把设备就绪状态转换为状态码。
  * @param  dev：[输入] 有效设备对象或NULL。
  * @retval 0：可使用；-ENODEV：NULL、未初始化或初始化失败。
  * @note   当前实现只检查初始化状态，不额外检查电源是否挂起。
  */
static inline int z_device_usable_check(const struct device *dev)
{


    return z_device_ready(dev) ? 0 : -ENODEV;    /* 把当前初始化就绪结果转换为0或设备不存在错误。 */
}

/**
  * @brief  通过公共接口检查设备是否可使用。
  * @param  dev：[输入] 有效设备对象或NULL；用户态另受对象权限约束。
  * @retval 0：可使用；当前实现-ENODEV表示NULL/未初始化/初始化失败。
  * @note   接口可扩展其他负错误；当前包装转调z_device_usable_check。
  */
__syscall int device_usable_check(const struct device *dev);

/**
  * @brief  将系统调用内核入口转到就绪检查。
  * @param  dev：[输入] 有效设备对象或NULL。
  * @retval 0：可使用；-ENODEV：当前不可用。
  */
static inline int z_impl_device_usable_check(const struct device *dev)
{


    return z_device_usable_check(dev);    /* 复用同一就绪检查，保持声明和实现一致。 */
}

/**
  * @brief  将设备可用性接口转换为布尔结果。
  * @param  dev：[输入] 真实设备对象或NULL。
  * @retval true：device_usable_check返回0；false：返回错误。
  * @note   适用于DEVICE_DT_GET取地址后的检查；当前实现不代表硬件永远保持供电。
  */
static inline bool device_is_ready(const struct device *dev)
{


    return device_usable_check(dev) == 0;    /* 只有公共检查成功才返回true。 */
}

/**
 * @}
 */

/* 设备树路径可能超过用户态名称长度上限，故用依赖序号合成唯一符号。可通过generated/device_extern.h把序号映射回节点；若改变转换格式，须同步生成器gen_defines。生成头不直接修改。 */
#define Z_DEVICE_DT_DEV_NAME(node_id) _CONCAT(dts_ord_, DT_DEP_ORD(node_id))

/* 根据dev_name合成RAM设备状态对象符号，不分配动态内存。 */
#define Z_DEVICE_STATE_NAME(dev_name) _CONCAT(__devstate_, dev_name)

/** 合成设备句柄/依赖对象符号：存在设备树节点时用node_id，否则用dev_name。 */
#define Z_DEVICE_HANDLE_NAME(node_id, dev_name)				\
    _CONCAT(__devicehdl_,						\
        COND_CODE_1(DT_NODE_EXISTS(node_id),			\
                (node_id),					\
                (dev_name)))

/* 将可选依赖参数展开为逗号分隔序列，空参数不生成项。 */
#define Z_DEVICE_EXTRA_HANDLES(...)				\
    FOR_EACH_NONEMPTY_TERM(IDENTITY, (,), __VA_ARGS__)

#ifdef CONFIG_PM_DEVICE
/* 启用电源管理时初始化RAM状态中的pm字段，关闭时展开为空。 */
#define Z_DEVICE_STATE_PM_INIT(node_id, dev_name)			\
    .pm = Z_PM_DEVICE_INIT(Z_DEVICE_STATE_NAME(dev_name).pm, node_id),
#else
/* 启用电源管理时初始化RAM状态中的pm字段，关闭时展开为空。 */
#define Z_DEVICE_STATE_PM_INIT(node_id, dev_name)
#endif

/** 定义RAM设备状态对象并放入.z_devstate段；node_id用于可选电源初始化，dev_name决定唯一符号，未显式赋值字段按C静态初始化规则为0。 */
#define Z_DEVICE_STATE_DEFINE(node_id, dev_name)			\
    static struct device_state Z_DEVICE_STATE_NAME(dev_name)	\
    __attribute__((__section__(".z_devstate"))) = {			\
        Z_DEVICE_STATE_PM_INIT(node_id, dev_name)		\
    };

/* 启用设备电源管理时为每设备在.z_pm_device_slots段预留一个指针槽，电源子系统用其跟踪系统电源切换时已挂起的设备；关闭时宏展开为空。 */
#if CONFIG_PM_DEVICE
#define Z_DEVICE_DEFINE_PM_SLOT(dev_name)				\
    static const Z_DECL_ALIGN(struct device *)			\
    _CONCAT(__pm_device_slot_, DEVICE_NAME_GET(dev_name)) __used	\
    __attribute__((__section__(".z_pm_device_slots")));
#else
#define Z_DEVICE_DEFINE_PM_SLOT(dev_name)
#endif

/* 构造ROM设备所引用的RAM状态及可选电源管理槽。node_id、dev_name确定对象身份；当前宏没有在此实际生成依赖句柄数据。 */
#define Z_DEVICE_DEFINE_PRE(node_id, dev_name, ...)			\
    Z_DEVICE_STATE_DEFINE(node_id, dev_name)			\
    Z_DEVICE_DEFINE_PM_SLOT(dev_name)

/* CONFIG_DEVICE_HANDLE_PADDING的0～10级手工填充宏。不能内部再用UTIL_LISTIFY，因为外部可能正用UTIL_LISTIFY展开DEVICE_DT_DEFINE，C预处理器不会递归重展开自身。
 * 原调用示例保留：
 * #define DEFINE_FOO_DEVICE(...) DEVICE_DT_DEFINE(...)
 * UTIL_LISTIFY(N, DEFINE_FOO_DEVICE)
 * 当前宏逐层添加DEVICE_HANDLE_ENDS，配置移除时这些辅助宏也应一并移除。 */
#define Z_DEVICE_HANDLE_PADDING \
    Z_DEVICE_HANDLE_PADDING_(CONFIG_DEVICE_HANDLE_PADDING)
#define Z_DEVICE_HANDLE_PADDING_(count) \
    Z_DEVICE_HANDLE_PADDING__(count)
#define Z_DEVICE_HANDLE_PADDING__(count) \
    Z_DEVICE_HANDLE_PADDING_ ## count
#define Z_DEVICE_HANDLE_PADDING_10 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_9
#define Z_DEVICE_HANDLE_PADDING_9 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_8
#define Z_DEVICE_HANDLE_PADDING_8 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_7
#define Z_DEVICE_HANDLE_PADDING_7 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_6
#define Z_DEVICE_HANDLE_PADDING_6 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_5
#define Z_DEVICE_HANDLE_PADDING_5 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_4
#define Z_DEVICE_HANDLE_PADDING_4 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_3
#define Z_DEVICE_HANDLE_PADDING_3 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_2
#define Z_DEVICE_HANDLE_PADDING_2 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_1
#define Z_DEVICE_HANDLE_PADDING_1 \
    DEVICE_HANDLE_ENDS, Z_DEVICE_HANDLE_PADDING_0
#define Z_DEVICE_HANDLE_PADDING_0 EMPTY


#ifdef CONFIG_PM_DEVICE
/* 启用电源管理时在ROM设备中填写电源回调与RAM电源状态指针，关闭时无字段。 */
#define Z_DEVICE_DEFINE_PM_INIT(dev_name, pm_control_fn)		\
    .pm_control = (pm_control_fn),					\
    .pm = &Z_DEVICE_STATE_NAME(dev_name).pm,
#else
/* 启用电源管理时在ROM设备中填写电源回调与RAM电源状态指针，关闭时无字段。 */
#define Z_DEVICE_DEFINE_PM_INIT(dev_name, pm_control_fn)
#endif

/* 内部初始化器扩展点，当前只展开可选电源字段。 */
#define Z_DEVICE_DEFINE_INIT(node_id, dev_name, pm_control_fn)		\
        Z_DEVICE_DEFINE_PM_INIT(dev_name, pm_control_fn)
/* 底层设备定义：同时接收node_id及dev_name，尾部参数为设备树之外的依赖描述。先建状态/电源槽，再生成ROM描述和启动项。当前初始化器未写handles字段，不能仅凭尾部参数声称已建立依赖表。 */
#define Z_DEVICE_DEFINE(node_id, dev_name, drv_name, init_fn, pm_control_fn, \
            data_ptr, cfg_ptr, level, prio, api_ptr, ...)	\
    Z_DEVICE_DEFINE_PRE(node_id, dev_name, __VA_ARGS__)		\
    COND_CODE_1(DT_NODE_EXISTS(node_id), (), (static))		\
        const Z_DECL_ALIGN(struct device)			\
        DEVICE_NAME_GET(dev_name) __used			\
    __attribute__((__section__(".z_device_" #level STRINGIFY(prio)"_"))) = { \
        .name = drv_name,					\
        .config = (cfg_ptr),					\
        .api = (api_ptr),					\
        .state = &Z_DEVICE_STATE_NAME(dev_name),		\
        .data = (data_ptr),					\
        Z_DEVICE_DEFINE_INIT(node_id, dev_name, pm_control_fn)	\
    };								\
    BUILD_ASSERT(sizeof(Z_STRINGIFY(drv_name)) <= Z_DEVICE_MAX_NAME_LEN, \
             Z_STRINGIFY(DEVICE_NAME_GET(drv_name)) " too long"); \
    Z_INIT_ENTRY_DEFINE(DEVICE_NAME_GET(dev_name), init_fn,		\
        (&DEVICE_NAME_GET(dev_name)), level, prio)

#ifdef __cplusplus
}
#endif

/* device_extern.h依据设备树节点生成，提供外部设备声明，不直接手工编辑。 */
#include <device_extern.h>

#include <syscalls/device.h>

#endif    /* ZEPHYR_INCLUDE_DEVICE_H_ */
