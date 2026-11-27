/*
 * Copyright (c) 2019 Actions Semiconductor Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file system_app_main.c
 * @brief 手表主线程的启动与消息分派。Zephyr 在内核和板级设备初始化后进入 main()，模拟器使用 bt_watch_main()。
 */
#include <app_manager.h>         /* 声明应用登记、查询前台应用及退出接口。 */
#include <mem_manager.h>         /* 声明启动时使用的内存管理器接口。 */
#include <msg_manager.h>         /* 提供 app_msg、消息收取和异步投递接口。 */
#include <thread_timer.h>        /* 提供当前线程的下一次定时器期限和到期处理入口。 */
#include <sys_event.h>           /* 定义系统事件编号及通知接口。 */
#include "app_switch.h"          /* 声明按目标名称与切换模式选择应用的接口。 */
#include "system_app.h"          /* 声明本应用的启动模式、输入处理和电源管理等入口。 */
#include "app_ui.h"              /* 声明应用界面和界面栈操作。 */
#include <stream.h>              /* 引入框架的流对象接口。 */
#include <property_manager.h>    /* 引入系统属性访问接口。 */
#ifdef CONFIG_PLAYTTS
#include "tts_manager.h"             /* 启用语音提示时提供播放、处理和加锁接口。 */
#endif

#ifdef CONFIG_BLUETOOTH
#include "mem_manager.h"             /* 蓝牙配置下也引用公共内存接口，头文件保护负责重复包含。 */
#include "bt_manager.h"              /* 提供蓝牙管理器的初始化、服务消息及设备恢复接口。 */
#endif

#ifdef CONFIG_TOOL
#include "tool_app.h"                /* 工具模式用它查询调试设备类型。 */
#endif

#ifdef CONFIG_SENSOR_MANAGER
#include <sensor_manager.h>          /* 提供活动识别、抬手等算法的使能接口。 */
#endif

#ifndef CONFIG_SIMULATOR
#include <drivers/power_supply.h>    /* 实板的充电消息使用电源驱动定义的事件码。 */
#endif

#ifdef CONFIG_ACTLOG
#include <logging/act_log.h>         /* 提供炬芯日志子系统初始化入口。 */
#endif

#ifdef CONFIG_BT_LOG_SERVICE
#include <bt_log/log_service.h>      /* 蓝牙引擎就绪后可启动该日志服务。 */
#endif

#ifdef CONFIG_UI_MANAGER
#include "alipay/alipay_ui.h"        /* 支付功能与界面管理同时启用时初始化支付界面。 */
#endif


#ifdef CONFIG_DVFS
#include <dvfs.h>                    /* 动态电压频率调节配置打开时引入调频接口。 */
#endif

/* 前台应用线程共用这块按架构栈指针对齐的存储区；本文件末尾登记的常驻 main 使用另一块 z_main_stack。 */
char __aligned(ARCH_STACK_PTR_ALIGN) share_stack_area[CONFIG_APP_STACKSIZE];

/**
  * @brief  按编译配置输出系统库版本，供启动串口核对库信息。
  * @param  无。
  * @retval 无。
  */
extern void system_library_version_dump(void);

/**
  * @brief  初始化 Actions 跟踪输出。
  * @param  无。
  * @retval 无。
  * @note   工具与跟踪共用 UART0 时，调用方先调整 DMA 输出方式。
  */
extern void trace_init(void);

/**
  * @brief  选择跟踪打印是否使用 DMA。
  * @param  dma_enable：[输入]本文件传 0 以关闭 DMA 打印。
  * @retval 实现返回的整型结果，本入口不依据它分支。
  */
extern int trace_dma_print_set(unsigned int dma_enable);

/**
  * @brief  检查 USB 设备模式并按需运行读卡器应用。
  * @param  无。
  * @retval 0 为普通退出或无需进入，NO_NEED_INIT_BT 表示跳过常规蓝牙初始化，-ENOMEM 表示分配失败。
  * @note   读卡器模式可在本调用中处理消息直到退出。
  */
extern int card_reader_mode_check(void);

/**
  * @brief  查询本次启动的唤醒来源是否为闹钟。
  * @param  无。
  * @retval true 为闹钟唤醒，false 为其他来源。
  */
extern bool alarm_wakeup_source_check(void);

/**
  * @brief  建立 SD 卡 OTA 升级后端。
  * @param  无。
  * @retval 0 表示初始化完成，-ENODEV 表示无法建立后端。
  */
extern int ota_app_init_sdcard(void);

/**
  * @brief  建立蓝牙 OTA 升级后端。
  * @param  无。
  * @retval 0 表示初始化完成，-ENODEV 表示无法建立后端。
  * @note   收到蓝牙引擎就绪事件后调用。
  */
extern int ota_app_init_bluetooth(void);

/**
  * @brief  检查 OTA 完成标志，以决定是否请求播放欢迎音。
  * @param  无。
  * @retval true 为属性标记已完成，false 表示未读到完成标记。
  */
extern bool ota_is_already_done(void);

/**
  * @brief  初始化厂商定制蓝牙应用。
  * @param  无。
  * @retval 0 为成功，-ENOMEM 表示无法分配应用上下文。
  */
extern int vendor_app_init(void);

/**
  * @brief  初始化工具的 SysRq 调试入口。
  * @param  无，本文件以无实参形式调用。
  * @retval 无。
  * @note   此处沿用工程中的旧式声明。
  */
extern void tool_sysrq_init();

/**
  * @brief  检查并按需运行低电量充电流程。
  * @param  无。
  * @retval 正常返回时为 0。
  * @note   在支持内部电池供电驱动的配置中，可能在充电消息循环等待或因供电条件触发关机。
  */
extern int charger_mode_check(void);


#ifdef CONFIG_BT_MANAGER
static bool att_enter_bqb_flag = false;            /* 初始为普通启动；特定 ATT 测试重启原因会置位，待蓝牙就绪时读取。 */
static uint8_t super_enter_bqb_mode = 0;           /* 零表示未指定测试，特定重启原因会选择 LE_TEST 或 DUT_TEST。 */
#endif
static uint8_t boot_sys_event = SYS_EVENT_NONE;    /* 暂存启动阶段投递失败的低电量事件，初始无待发事件。 */

#ifdef CONFIG_SPP_TEST_SUPPORT

/**
  * @brief  初始化可选的蓝牙串口协议（SPP）测试。
  * @param  无。
  * @retval 成功返回 0，初始化失败返回 -ENODEV。
  */
extern int spp_test_app_init(void);
#endif

#ifdef CONFIG_NSM_APP

/**
  * @brief  初始化可选的 NSM 测试应用。
  * @param  无。
  * @retval 成功返回 0，初始化失败返回 -ENODEV。
  */
extern int nsm_test_app_init(void);
#endif

/**
  * @brief  在主线程取一条消息并分派，随后运行本线程到期的定时器。
  * @param  parama1：[输入]兼容线程入口的保留参数，未读取，可为空。
  * @param  parama2：[输入]保留参数，未读取，可为空。
  * @param  parama3：[输入]保留参数，未读取，可为空。
  * @retval 无。
  * @note   接收等待以最近的定时器期限为上限。消息处理及可选回调均在调用线程中进行。
  */
void main_msg_proc(void *parama1, void *parama2, void *parama3)
{
	struct app_msg msg = {0};                                                                    /* 清零单次接收使用的消息对象，待接收接口填入类型、参数及回调。 */
	int result = 0;                                                                              /* 默认向完成回调传 0；低电量转发路径会用异步投递结果覆盖它。 */

	/* 同一次等待兼顾消息和定时器；超时后仍会执行函数末尾的到期处理。 */
	if (receive_msg(&msg, thread_timer_next_timeout())) {

		/* 按消息类型选择处理路径；处理结束后再通知这条消息的发送方。 */
		switch (msg.type) {
#ifndef CONFIG_SIMULATOR
		case MSG_BAT_CHARGE_EVENT:                                                           /* 实板电源驱动发来的充电或电池告警。 */
			if ((msg.cmd == BAT_CHG_EVENT_DC5V_IN)
				|| (msg.cmd == BAT_CHG_EVENT_DC5V_OUT)) {                            /* 5 V 外部供电接入或拔出时，将原消息交给当前应用。 */
				send_async_msg(app_manager_get_current_app(), &msg);                 /* 保留驱动提供的事件类型和子命令异步转发。 */
			} else if (msg.cmd == BAT_CHG_EVENT_BATTERY_TOO_LOW) {                       /* 电池已降到临界值时直接走系统关机路径。 */
				system_power_off();                                                  /* 请求关机以保护电池。 */
            }
			break;
#endif
		case MSG_SYS_EVENT:                                                                  /* 根据系统事件的具体编号选择关机、延迟转发或通用处理。 */
			if (msg.cmd == SYS_EVENT_BATTERY_TOO_LOW) {                                  /* 系统层同样会报告必须关机的临界电量。 */
				system_power_off();                                                  /* 临界低电量立即走关机流程。 */
			}else if (msg.cmd == SYS_EVENT_BATTERY_LOW) {                                /* 一般低电量只通知应用，由它决定提示方式。 */
				result = send_async_msg(app_manager_get_current_app(), &msg);        /* 返回布尔投递结果，供下面的延迟补发判断及消息回调使用。 */
				if (!result) {                                                       /* 当前应用未收到这次异步投递时记下事件。 */
					boot_sys_event = SYS_EVENT_BATTERY_LOW;                      /* 待蓝牙引擎就绪、前台应用启动后再尝试发送。 */
				}
			} else {
				system_bluetooth_event_handle(&msg);                                 /* 把其他系统事件交给蓝牙相关策略检查。 */
				sys_event_process(msg.cmd);                                          /* 继续按事件映射处理同一系统事件号。 */
			}
			break;

		#ifdef CONFIG_INPUT_MANAGER
		case MSG_SR_INPUT:                                                                   /* 输入管理器提供的 SR 输入消息，载荷经指针传入。 */
			system_sr_input_event_handle(msg.ptr);                                       /* SR 处理函数读取指针处的输入值，并在有前台应用时转发。 */
			break;
		#endif

		#ifdef CONFIG_PLAYTTS
		case MSG_TTS_EVENT:                                                                  /* 处理提示音管理器发给主线程的消息。 */
			if (msg.cmd == TTS_EVENT_START_PLAY) {                                       /* 只有启动播放子命令才推进提示音播放处理。 */
				tts_manager_play_process();                                          /* 在主线程处理已请求的语音播放。 */
			}
			break;
		#endif
		#ifdef CONFIG_INPUT_MANAGER
		case MSG_KEY_INPUT:                                                                  /* 处理输入管理器送来的原始按键消息。 */
			/* 按原有事件约定，收到按键输入意味着 ESD 恢复阶段已结束。 */
			system_input_event_handle(msg.value);                                        /* 按 value 中的键码与事件标志处理本次输入。 */
			break;
		#endif

		case MSG_INPUT_EVENT:                                                                /* 接收已经映射为系统动作的按键消息。 */
			system_key_event_handle(&msg);                                               /* 使用消息对象中的动作与参数处理按键事件。 */
			break;

		case MSG_HOTPLUG_EVENT:                                                              /* 处理系统的设备插拔通知。 */
			system_hotplug_event_handle(&msg);                                           /* 热插拔处理器读取消息中的设备类型与插拔状态。 */
			break;

		case MSG_VOLUME_CHANGED_EVENT:                                                       /* 识别音量变化；下方界面显示调用目前被注释。 */
			//system_app_volume_show(&msg);
			break;

		case MSG_POWER_OFF:                                                                  /* 处理关机请求，允许启用的界面先显示告别画面。 */
		#ifdef CONFIG_UI_MANAGER
			app_ui_stack_forward(GOODBYE_UI, APP_UI_LOAD_ANIM_NONE);                     /* 将界面栈切到关机画面，不播放切换动画。 */
		#endif
			os_sleep(500);                                                               /* 当前线程休眠 500 毫秒后继续关机，无 UI 配置也会等待。 */
			system_power_off();                                                          /* 执行系统关机流程。 */
			break;
		case MSG_REBOOT:                                                                     /* 消息中的子命令给出本次重启的原因。 */
			system_power_reboot(msg.cmd);                                                /* 将该原因传给电源管理重启接口。 */
			break;
		case MSG_NO_POWER:                                                                   /* 电量耗尽时发布关机系统事件。 */
			sys_event_notify(SYS_EVENT_POWER_OFF);                                       /* 交给系统事件机制通知关机。 */
			break;

		/* 蓝牙引擎就绪后才启动依赖它的业务，并尝试补投启动期间缓存的低电量事件。 */
		case MSG_BT_ENGINE_READY:
		#ifdef CONFIG_BT_LOG_SERVICE
			bt_log_service_start();                                                      /* 蓝牙可用后启动可选日志服务。 */
		#endif
		#ifdef CONFOG_ALARM_MANAGER                                                          /* 这里按源码中的 CONFOG_ALARM_MANAGER 拼写控制闹钟启动分支。 */
			if (alarm_wakeup_source_check()) {                                           /* 在该分支启用时检查实际唤醒源。 */
				system_app_launch(SYS_INIT_ALARM_MODE);                              /* 以闹钟模式启动应用。 */
			}
			else
		#endif
			{
				system_app_launch(SYS_INIT_NORMAL_MODE);                             /* 常规唤醒或未编入上述闹钟判断时走普通应用启动。 */
			}

		#ifdef CONFIG_OTA_BACKEND_BLUETOOTH
			ota_app_init_bluetooth();                                                    /* 蓝牙可用后建立 OTA 蓝牙升级后端。 */
		#endif
		#ifdef CONFIG_BT_VENDOR
			vendor_app_init();                                                           /* 若编入厂商蓝牙扩展，初始化其应用上下文。 */
		#endif

		#ifdef CONFIG_ALIPAY_LIB
		#ifdef CONFIG_UI_MANAGER
			alipay_ui_init();                                                            /* 初始化支付宝界面。 */
			wxpay_ui_init();                                                             /* 初始化微信支付界面。 */
		#endif
		#endif

		#ifdef CONFIG_SENSOR_MANAGER
			sensor_manager_enable(ALGO_ACTIVITY_OUTPUT, 0);                              /* 向传感器服务请求启用活动算法输出。 */
			sensor_manager_enable(ALGO_HANDUP, 0);                                       /* 启用抬手算法，用于上层识别抬腕动作。 */
		#endif
	#ifdef CONFIG_BT_MANAGER
			if (att_enter_bqb_flag == true) {                                            /* 只在启动时记录了 ATT 测试请求时进入该分支。 */
				SYS_LOG_INF("ATT Goto BQB TEST");                                    /* 记录进入 ATT 蓝牙认证测试的原因。 */
				os_thread_priority_set(os_current_get(), 0);                         /* 将当前主线程的优先级设为 0。 */
			}
	#endif

			/* 当前台应用可用时，把启动阶段缓存的一条系统事件投递给它。 */
			if (boot_sys_event != SYS_EVENT_NONE) {
				msg.type = MSG_SYS_EVENT;                                            /* 当前消息对象改作系统事件载体。 */
				msg.cmd = boot_sys_event;                                            /* 取出先前暂存的低电量事件号。 */
				send_async_msg(app_manager_get_current_app(), &msg);                 /* 向此时的前台应用补投一次。 */
				boot_sys_event = SYS_EVENT_NONE;                                     /* 发出后清除缓存标记，本分支没有再次检查投递结果。 */
			}

			#ifdef CONFIG_SPP_TEST_SUPPORT
			spp_test_app_init();                                                         /* SPP 测试配置启用时启动测试应用。 */
			#endif

			#ifdef CONFIG_NSM_APP
			nsm_test_app_init();                                                         /* NSM 测试配置启用时启动其应用。 */
			#endif
			break;

		case MSG_START_APP:                                                                  /* 处理切换到指定应用的请求。 */
			SYS_LOG_INF("start %s\n", (char *)msg.ptr);                                  /* 输出目标应用名称供串口诊断。 */
			app_switch((char *)msg.ptr, msg.reserve, true);                              /* 目标名来自 ptr，reserve 指定切换模式，true 表示强制切换。 */
			break;

		case MSG_EXIT_APP:
			SYS_LOG_DBG("exit %s\n", (char *)msg.ptr);                                   /* 输出请求退出的应用名。 */
			app_manager_exit_app((char *)msg.ptr, true);                                 /* 按消息中的名称请求应用退出。 */
			break;
#ifdef CONFIG_BT_ANCS_AMS
		case MSG_BLE_ANCS_AMS_SERVICE:                                                       /* 处理蓝牙低功耗通知与媒体服务的消息。 */
	#ifdef CONFIG_BT_BLE
			bt_manager_ble_ancs_ams_handle(msg.cmd,(void *)msg.value);                   /* 将子命令和 value 承载的指针传给 ANCS/AMS 处理入口。 */
	#endif
			break;
#endif

#ifdef CONFIG_BT_ACTIONS_SUPER_SERVICE
		case MSG_APP_BLE_SUPER_BQB:                                                          /* 处理超级服务 BQB 消息，恢复普通应用启动路径。 */
			system_app_launch(SYS_INIT_NORMAL_MODE);                                     /* 按普通模式启动应用。 */
			if (super_enter_bqb_mode) {                                                  /* 若曾记录专用测试模式，清除它并更新线程优先级。 */
				super_enter_bqb_mode = 0;                                            /* 清掉先前的 LE 或 BR 测试模式。 */
				os_thread_priority_set(os_current_get(), 0);                         /* 将当前线程优先级设为 0。 */
			}
			break;
#endif

#ifdef CONFIG_TOOL
		case MSG_APP_TOOL_INIT:                                                              /* 调试工具请求建立 SysRq 功能。 */
			tool_sysrq_init();                                                           /* 安装工具的系统请求入口。 */
			break;
#endif

		case MSG_BT_PAIRING_EVENT:                                                           /* 处理蓝牙配对通知，当前分支仅记录相关信息。 */
			SYS_LOG_INF(" MSG_BT_PAIRING_EVENT: cmd:%d value:%d", msg.cmd,msg.value);    /* 将子命令和参数值写入现有日志。 */
			break;

#ifdef CONFIG_BT_MANAGER
		case MSG_BT_DEVICE_ERROR:                                                            /* 蓝牙管理器报告设备错误。 */
			bt_manager_reset_btdev();                                                    /* 请求蓝牙管理器复位设备。 */
			break;
#endif
		default:                                                                             /* 对未列入分派表的消息留下诊断信息。 */
			SYS_LOG_ERR(" error: %d\n", msg.type);                                       /* 记录无法识别的消息类型。 */
			break;
		}

		/* 若发送方提供了完成回调，在本地消息对象仍有效时传回分派结果。 */
		if (msg.callback)
			msg.callback(&msg, result, NULL);                                            /* 用本地消息、分派结果和空回复指针调用完成回调。 */
	}

	/* 等待超时或处理完一条消息后，均检查当前线程的定时器。 */
	thread_timer_handle_expired();
}

#ifdef CONFIG_SIMULATOR

/**
  * @brief  在模拟器中运行手表应用启动流程。
  * @param  无。
  * @retval 入口声明为 int，正常路径在永久消息循环内运行。
  * @note   模拟器和实板共用下方的初始化与消息循环。
  */
int bt_watch_main(void)
#else

/**
  * @brief  依次启动框架、可选设备服务与应用，最后在主线程持续处理事件。
  * @param  无。
  * @retval 入口声明为 int，正常路径在永久消息循环内运行。
  * @note   由 Zephyr 主线程进入。Shell 的 app 子命令由命令表登记，其处理函数在 Shell 的命令执行上下文中运行。
  */
int main(void)
#endif
{
	bool play_welcome = true;                                                                                             /* 初始允许走欢迎提示策略，闹钟或 ATT 测试启动可清除此标志。 */
	bool init_bt_manager = true;                                                                                          /* 缺省初始化蓝牙管理器，读卡器或专用测试模式可取消。 */
	uint16_t reboot_type = 0;                                                                                             /* 用于接收硬件报告的重启类别。 */
	uint8_t reason = 0;                                                                                                   /* 用于接收上次重启原因，供后续模式选择使用。 */

	/* 读取上次重启的类型和原因；后续据此选择正常、升级或测试启动路径。 */
	system_power_get_reboot_reason(&reboot_type, &reason);

	/* 输出已配置系统库的版本，再初始化后续服务使用的内存和日志设施。 */
	system_library_version_dump();

	mem_manager_init();                                                                                                   /* 初始化供应用和后续服务使用的内存管理器。 */

#ifdef CONFIG_ACTLOG
	act_log_init();                                                                                                       /* 启用 Actions 日志的构建在这里初始化日志系统。 */
#endif

	/* 先接入输入事件，再初始化可能消费这些事件的 UI 服务。 */
	system_input_handle_init();

	system_init();                                                                                                        /* 执行系统管理框架的初始化。 */

#if defined(CONFIG_UI_MANAGER)
	app_ui_init();                                                                                                        /* 若启用 UI，在输入处理准备好后初始化应用界面。 */
#endif

#ifdef CONFIG_CHARGER_APP
	if (charger_mode_check()) {                                                                                           /* 检查低电量充电模式；检查本身可运行充电流程，返回后分支体为空。 */

	}
#endif

#ifdef CONFIG_AUDIO
	system_audio_policy_init();                                                                                           /* 装入本产品的音频策略。 */
#endif

	/* 配置提示音及系统事件映射，使后续业务能按系统事件选择相应处理。 */
	system_tts_policy_init();

	system_event_map_init();                                                                                              /* 注册系统事件与处理动作的映射。 */

#ifdef CONFIG_PLAYTTS

	/* 根据 OTA 完成标志和认证测试开关决定是否提交开机提示音。 */
	if (!ota_is_already_done()) {
#if (CONFIG_BT_FCC_TEST == 1 || CONFIG_BT_SRRC_TEST == 1)                                                                     /* FCC 或 SRRC 测试配置在此跳过欢迎音请求。 */
#else
		tts_manager_play("welcome.act", PLAY_IMMEDIATELY);                                                            /* 非上述测试配置时立即请求播放现有的欢迎提示资源。 */
#endif
	}
#endif

	/* 准备应用切换表，随后结合唤醒来源与重启原因选择启动策略。 */
	system_app_launch_init();

#ifdef CONFIG_ALARM_APP
	if (alarm_wakeup_source_check()) {                                                                                    /* 查询是否因闹钟唤醒。 */
		play_welcome = false;                                                                                         /* 后续按静音启动策略处理。 */
	}
#endif

	if ((reboot_type == REBOOT_TYPE_SF_RESET) && (reason == REBOOT_REASON_GOTO_BQB_ATT)) {                                /* 同时匹配软件复位与 ATT BQB 原因才进入该测试启动路径。 */
		play_welcome = false;                                                                                         /* 让后续启动逻辑进入锁定语音提示的分支。 */
#ifdef CONFIG_BT_MANAGER
		att_enter_bqb_flag = true;                                                                                    /* 记下 ATT 测试意图，供蓝牙引擎就绪消息处理时使用。 */
#endif
	}

	/* 应用闹钟和 ATT 测试模式的提示音策略，普通启动继续检查读卡器模式。 */
	if (!play_welcome) {
	#ifdef CONFIG_PLAYTTS
		tts_manager_lock();                                                                                           /* 锁住提示音管理器，限制后续语音提示的播放。 */
	#endif
	} else if(!(reboot_type == REBOOT_TYPE_SF_RESET && reason == REBOOT_REASON_OTA_FINISHED)) {                           /* 允许欢迎提示且本次并非升级完成软重启时，继续检查特殊启动设备。 */
		bool enter_stub_tool = false;                                                                                 /* 当前实现初始为 false，本分支内没有改写该标志。 */


		if (enter_stub_tool == false) {                                                                               /* 保留工具模式门控；当前初值使流程进入读卡器检查。 */
		#ifdef CONFIG_CARD_READER_APP
			if (usb_hotplug_device_mode()
				&& !(reason == REBOOT_REASON_NORMAL)) {                                                       /* 同时满足 USB 设备模式和非普通重启原因时才检查读卡器。 */
#if defined(CONFIG_OTA_PRODUCT_SUPPORT) || defined(CONFIG_OTA_BLE_MASTER_SUPPORT)
				if ((reason == REBOOT_REASON_HCI_TIMEOUT) || (reason == REBOOT_REASON_SYSTEM_EXCEPTION)) {    /* OTA 产品或 BLE 主机配置对 HCI 超时、系统异常重启另行处理。 */
					SYS_LOG_INF("ota product auto reboot.");                                              /* 输出 OTA 产品自动重启日志。 */
					os_sleep(100);                                                                        /* 等待 100 毫秒，在该条件下跳过下面的读卡器检查。 */
				} else
#endif
				if (card_reader_mode_check() == NO_NEED_INIT_BT) {                                            /* 只有读卡器明确要求跳过蓝牙时才调整常规初始化标志。 */
					init_bt_manager = false;                                                              /* 后续普通启动路径跳过蓝牙管理器初始化。 */
				}
			}
		#endif
		}
	}


	/* 标记公共系统设施就绪，再建立按配置选入的升级与跟踪后端。 */
	system_ready();

#ifdef CONFIG_OTA_BACKEND_SDCARD
	ota_app_init_sdcard();                                                                                                /* SD 卡 OTA 构建在此建立本地升级后端。 */
#endif

#ifdef CONFIG_ACTIONS_TRACE
#ifdef CONFIG_TOOL
	if (tool_get_dev_type() == TOOL_DEV_TYPE_UART0) {                                                                     /* 开启工具支持且工具使用 UART0 时处理串口争用。 */
		/* 工具串口和跟踪输出共用 UART0 时，关闭跟踪输出的 DMA 模式。 */
		trace_dma_print_set(false);                                                                                   /* 在跟踪系统初始化前关闭其 DMA 打印。 */
	}
#endif
	trace_init();                                                                                                         /* 按已选择的串口输出方式初始化跟踪。 */
#endif

#ifdef CONFIG_BT_CONTROLER_RF_FCC
	if ((reboot_type == REBOOT_TYPE_SF_RESET) && (reason == REBOOT_REASON_GOTO_BQB)) {                                    /* 本配置将 BQB 软件重启与常规 FCC 测试入口分开处理。 */
		SYS_LOG_INF("Goto BQB TEST");                                                                                 /* BQB 软件重启分支只在此处记录测试启动。 */
	} else {
		system_enter_FCC_mode();                                                                                      /* 其他情况下转入射频 FCC 测试模式。 */
	}
#endif

#ifdef CONFIG_BT_ACTIONS_SUPER_SERVICE

	/* 将超级服务专用重启原因映射成控制器测试模式。 */
	if ((reason == REBOOT_REASON_SUPER_BR_BQB) || (reason == REBOOT_REASON_SUPER_LE_BQB)) {
		init_bt_manager = false;                                                                                      /* 专用测试启动时禁用普通蓝牙初始化。 */
		if(reason == REBOOT_REASON_SUPER_LE_BQB)                                                                      /* 区分低功耗与经典蓝牙的认证测试。 */
			super_enter_bqb_mode = LE_TEST;                                                                       /* 记录低功耗蓝牙测试模式。 */
		else
			super_enter_bqb_mode = DUT_TEST;                                                                      /* 记录经典蓝牙的被测设备模式。 */
	}
#endif

#ifdef CONFIG_BT_MANAGER

	/* 测试入口与常规蓝牙初始化互斥；常规蓝牙应用在收到引擎就绪消息后启动。 */
	if(super_enter_bqb_mode){
#ifdef CONFIG_BT_ACTIONS_SUPER_SERVICE
		bt_manager_ble_super_enter_bqb(super_enter_bqb_mode);                                                         /* 按先前选择的模式进入超级服务 BQB 测试。 */
#endif
	}
	else{
		if (init_bt_manager) {                                                                                        /* 仅在启动策略允许时初始化普通蓝牙管理器。 */
#if (CONFIG_BT_FCC_TEST == 1 || CONFIG_BT_SRRC_TEST == 1)
			os_sleep(1000);                                                                                       /* FCC 或 SRRC 测试构建额外等待 1000 毫秒。 */
#endif
			bt_manager_init();                                                                                    /* 启动蓝牙管理器，后续由引擎就绪消息继续拉起业务。 */
		}
	}
#else
	system_app_launch(SYS_INIT_NORMAL_MODE);                                                                              /* 未启用蓝牙管理器时直接启动普通应用。 */
#endif

	/* 转入常驻消息循环，每一轮至多取一条消息并处理已到期的线程定时器。 */
	while (1) {
		main_msg_proc(NULL, NULL, NULL);                                                                              /* 处理一轮主线程消息和线程定时器；三个保留参数均传空。 */
	}
}

extern char z_main_stack[CONFIG_MAIN_STACK_SIZE];    /* 引用 Zephyr 已提供的主线程栈，长度单位为字节。 */

/* 在实板的 .app_entry 段登记常驻 main，沿用内核主线程栈；三个参数、独立线程循环和输入回调均为空。 */
APP_DEFINE(main, z_main_stack, CONFIG_MAIN_STACK_SIZE,\
			APP_PRIORITY, RESIDENT_APP,\
			NULL, NULL, NULL,\
			NULL, NULL);

