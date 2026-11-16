/*
 * Copyright (c) 2018 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Actions SoC 休眠唤醒类型、回调合同与补偿计时接口。
 */

/* 包含保护避免重复声明；stdarg.h 提供 va_list，uint32_t/int64_t 等类型由调用方的 SDK 公共类型头先行引入，保留现有包含关系。 */
#ifndef	_ACTIONS_SOC_SLEEP_H_
#define	_ACTIONS_SOC_SLEEP_H_

#include <stdarg.h>

/* 逻辑唤醒源编号按连续值排列，既是软件掩码位号也是回调链表索引；实际 NVIC 中断号由 soc_sleep.c 的 wk_msg 映射，二者不能混用。 */
enum S_WK_SRC_TYPE {
	SLEEP_WK_SRC_PMU= 0x00,    /* PMU 电源/按键唤醒；值 0 也被现有实现用于未找到来源。 */
	SLEEP_WK_SRC_RTC,          /* RTC 定时唤醒，AOD 模式会设置该允许位。 */
	SLEEP_WK_SRC_BT ,          /* 蓝牙子系统唤醒。 */
	SLEEP_WK_SRC_GPIO,         /* GPIO 外部信号唤醒。 */
	SLEEP_WK_SRC_T0,           /* TIMER0 唤醒。 */
	SLEEP_WK_SRC_T1,           /* TIMER1 唤醒，也用作初始来源记录。 */
	SLEEP_WK_SRC_T2,           /* TIMER2 唤醒；T2 同时承担休眠周期计数。 */
	SLEEP_WK_SRC_T3,           /* TIMER3 唤醒。 */
	SLEEP_WK_SRC_T4,           /* TIMER4 唤醒。 */
	SLEEP_WK_SRC_T5,           /* TIMER5 唤醒。 */
	SLEEP_WK_SRC_TWS,          /* TWS 同步相关唤醒。 */
	SLEEP_WK_SRC_SPI0MT,       /* SPI0MT 存储控制器源，非本次应用 SPI2。 */
	SLEEP_WK_SRC_SPI1MT,       /* SPI1MT 存储控制器源。 */
	SLEEP_WK_SRC_IIC0MT,       /* IICMT0 传感器接口源。 */
	SLEEP_WK_SRC_IIC1MT,       /* IICMT1 传感器接口源，强制功耗测试按其唤醒次数退出。 */
};

/* prepare 回调报告处理函数所需内存环境；分派器据此决定立即执行或延后。 */
enum WK_RUN_TYPE {
	WK_RUN_IN_SRAM = 0x00,    /* 回调只能依赖保留 SRAM 中的代码与数据。 */
	WK_RUN_IN_NOR,            /* 可访问 SRAM 和已恢复的 NOR，不能假定 PSRAM 可用。 */
	WK_RUN_IN_SYTEM,          /* 需要 SRAM、NOR、PSRAM 等完整系统环境，延后执行；保留原枚举拼写 SYTEM。 */
	WK_RUN_MAX,               /* 运行环境枚举上界，不是有效可执行环境。 */
};

/* 处理回调的决策结果，不是操作系统错误码。 */
enum WK_CB_RC{
	WK_CB_SLEEP_AGAIN = 0x00,    /* 此回调已处理事件，可清对应挂起并再次睡眠。 */
	WK_CB_RUN_SYSTEM,            /* 要求恢复完整系统环境继续处理。 */
	WK_CB_CARELESS,              /* 回调不处理此源；现有分派对末次结果作系统恢复判定。 */
};


/* 唤醒处理回调：由休眠分派器以 wk_src [输入]逻辑源编号调用，无指针输出；返回 WK_CB_RC 决定继续睡眠或完整恢复。回调须遵守 prepare 声明的内存可用范围，不能假定普通线程调度及所有外设均可使用。 */
typedef enum WK_CB_RC (*sleep_wk_callback_t)(enum S_WK_SRC_TYPE wk_src);
/* 唤醒环境查询回调：由分派器传入 wk_src [输入]逻辑源编号，返回 WK_RUN_TYPE 指示处理函数所需环境；查询本身须可在受限的保留代码/数据环境运行。 */
typedef enum WK_RUN_TYPE (*sleep_wk_prepare_t)(enum S_WK_SRC_TYPE wk_src);

/* 由调用者持有的持久回调节点，注册只头插此节点，不复制或分配内存；重复插入同一节点可能形成链表环。 */
struct sleep_wk_fun_data {
	sleep_wk_callback_t wk_cb;          /* 有效唤醒处理回调，注册生效期间不可为空。 */
	sleep_wk_prepare_t	wk_prep;    /* 可选环境准备查询；为空时处理回调延后到系统恢复。 */
	struct sleep_wk_fun_data *next;     /* 注册时改写的同源链表后继；尾节点为空，不应由调用者并发修改。 */
};

/**
  * @brief  将初始唤醒原因记录设为 TIMER1。
  * @param  无。
  * @retval 无。
  * @note   仅设置结果字段，不使能 TIMER1 唤醒。
  */
void sys_s3_wksrc_init(void);

/**
  * @brief  取得已记录的最近唤醒源。
  * @param  无。
  * @retval sleep_context 中保存的唤醒源枚举。
  * @note   无匹配时实现写 0，与 PMU 枚举值相同，不能据此区分“无来源”和 PMU。
  */
enum S_WK_SRC_TYPE sys_s3_wksrc_get(void);

/**
  * @brief  禁止指定休眠唤醒源。
  * @param  src：[输入]有效唤醒源枚举值，作为 16 位掩码的位索引。
  * @retval 无。
  * @note   只修改软件掩码，实际硬件配置在休眠准备阶段应用；调用者负责并发串行化。
  */
void sys_s3_wksrc_clr(enum S_WK_SRC_TYPE src);

/**
  * @brief  允许指定休眠唤醒源。
  * @param  src：[输入]有效唤醒源枚举值，作为 16 位掩码的位索引。
  * @retval 无。
  * @note   只修改软件掩码，实际硬件配置在休眠准备阶段应用；调用者负责并发串行化。
  */
void sys_s3_wksrc_set(enum S_WK_SRC_TYPE src);

/**
  * @brief  将调用者的唤醒处理节点插入对应源的链表头。
  * @param  wk_src：[输入]有效唤醒源编号，0 至 SLEEP_WKSRC_NUM-1。
  * @param  fn_data：[输入/输出]持久可写节点，不可为空；wk_cb 必须有效，wk_prep 可为空，next 被修改。
  * @retval 0 表示已插入，-1 表示 fn_data 为空。
  * @note   不分配或复制节点，不校验源编号，不加锁、不去重；节点及回调在注册生效期间必须有效，避免重复注册形成环。
  */
int sleep_register_wk_callback(enum S_WK_SRC_TYPE wk_src, struct sleep_wk_fun_data *fn_data);

/**
  * @brief  配置休眠传感器代码区域。
  * @param  code_addr：[输入]传感器代码区域地址，代码长度非零时须有效；内存所有权及对齐要求本头未公开。
  * @param  code_len：[输入]代码区域长度，单位字节。
  * @retval 状态值；当前 Zephyr 应用源码中未找到匹配实现，具体错误码未知。
  * @note   当前应用仅见声明；bootloader 目录存在同名实现，但属于不同构建目标，不能当作此接口绑定。复制行为、可空性、时序及保存寿命均待核实。
  */
int sleep_sensor_code_set(void *code_addr, uint32_t code_len);

/**
  * @brief  读取休眠期间累计的 HOSC 等效周期补偿。
  * @param  无。
  * @retval 按 32 位无符号数累计的补偿周期，会回绕。
  * @note   不是当前 T2 原始值，也不是毫秒。soc_timer_exit_sleep 按 soc_rc32K_mutiple_hosc 乘 RC32K 计数差累加；系统定时器用它修正周期读数。
  */
unsigned int soc_sleep_cycle(void);

/**
  * @brief  读取包含深睡眠补偿的系统运行时间。
  * @param  无。
  * @retval 累计运行毫秒数。
  * @note   休眠阶段用预存 uptime 加累计休眠时间及当前 RC32K 差值；正常阶段用 k_uptime_get 加补偿。g_sleep_st 为 0 用作非休眠标志，计数恰为 0 的边界沿用原实现；等待 T2 同步没有超时。
  */
int64_t soc_sys_uptime_get(void);

/**
  * @brief  以可变参数形式调用休眠期打印接口。
  * @param  fmt：[输入]有效零结尾格式串，不可为空。
  * @param  ...：[输入]与格式串匹配的参数。
  * @retval 固定返回 0，不返回字符数。
  * @note   是否实际输出取决于 sl_vprintk 的调试与 panic 条件。
  */
int sl_printk(const char *fmt, ...);

/**
  * @brief  在允许调试或 panic 输出时使用 BROM 格式化器打印。
  * @param  fmt：[输入]有效零结尾格式串，不可为空。
  * @param  ap：[输入/输出]已由 va_start 初始化并与格式串匹配的参数列表。
  * @retval 固定返回 0，包括未输出的情况。
  * @note   普通休眠配置可完全跳过打印；panic 路径按需恢复 HOSC 和 UART，输出等待无超时。
  */
int sl_vprintk(const char *fmt, va_list ap);

/**
  * @brief  设置常亮显示模式并同步 RTC 唤醒允许状态。
  * @param  is_aod：[输入]非零启用 AOD，0 关闭。
  * @retval 无。
  * @note   AOD 用 0/1 保存；关闭时也清除 RTC 唤醒位，调用方须协调其他 RTC 用途。
  */
void soc_set_aod_mode(int is_aod);

/**
  * @brief  读取常亮显示模式标志。
  * @param  无。
  * @retval 1 表示启用，0 表示关闭。
  * @note   返回软件状态，不读取显示硬件。
  */
int soc_get_aod_mode(void);

/**
  * @brief  选择休眠策略并按需暂停唤醒锁管理。
  * @param  mode：[输入]模式 0～3，超出时钳制为 FORCE_SLEEP。
  * @retval 无。
  * @note   非普通模式禁用唤醒锁，恢复动作在特殊模式退出流程，不在简单设置 NORMAL 时立即执行。
  */
void soc_set_sleep_mode(uint8_t mode);

/**
  * @brief  按 TIMER2 休眠起点标志判断是否处于补偿计时阶段。
  * @param  无。
  * @retval g_sleep_st 大于 0 时返回 1，否则返回 0。
  * @note   不是 sleep_mode 策略枚举查询；原实现以 0 作标志，保留计数为 0 的边界限制。
  */
int soc_in_sleep_mode(void);


#endif    /* _ACTIONS_SOC_SLEEP_H_	*/
