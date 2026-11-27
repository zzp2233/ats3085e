# SPDX-License-Identifier: Apache-2.0
# 本文件决定应用的哪些子目录进入本次构建。左边是 Kconfig 合并后的配置变量，右边是相对于 src 的目录。

# 把 src 和 src/include 加入 Zephyr 的头文件搜索路径。
zephyr_include_directories(
	.
	include
)
# 主应用启用时同时引入 libsrc 和下方的 main；充电应用由独立开关决定。
add_subdirectory_ifdef(CONFIG_BT_MAIN_APP libsrc)
add_subdirectory_ifdef(CONFIG_CHARGER_APP charger)
# 命令 app spi2ram 的登记表位于 main/system_app_shell.c；进入 main 后由该目录的清单选择这个 C 文件。
add_subdirectory_ifdef(CONFIG_BT_MAIN_APP main)
# 分别选择蓝牙音乐、电话和 PC 工具模块。
add_subdirectory_ifdef(CONFIG_BT_PLAYER bt_player)
add_subdirectory_ifdef(CONFIG_BT_CALL_APP btcall)
add_subdirectory_ifdef(CONFIG_TOOL tool)
# OTA 升级和恢复应用可以分别选入。
add_subdirectory_ifdef(CONFIG_OTA_APP ota)
add_subdirectory_ifdef(CONFIG_OTA_RECOVERY_APP ota_recovery)
# 按配置选择本地媒体、闹钟、读卡器和启动器等业务。
add_subdirectory_ifdef(CONFIG_LCMUSIC_APP lcmusic)
add_subdirectory_ifdef(CONFIG_LOCAL_PLAYER local_player)
add_subdirectory_ifdef(CONFIG_ALARM_APP alarm)
add_subdirectory_ifdef(CONFIG_CARD_READER_APP card_reader)
add_subdirectory_ifdef(CONFIG_LAUNCHER_APP launcher)
add_subdirectory_ifdef(CONFIG_BT_TRANSMIT bt_transmit)
# 选择 LVGL 控件、厂商服务、蓝牙日志和 NSM 测试子目录。
add_subdirectory_ifdef(CONFIG_LVGL widgets)
add_subdirectory_ifdef(CONFIG_BT_VENDOR vendor)
add_subdirectory_ifdef(CONFIG_BT_LOG_SERVICE bt_log)
add_subdirectory_ifdef(CONFIG_NSM_APP bt_nsm)

