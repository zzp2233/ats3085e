# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(
	.
	include
)
add_subdirectory_ifdef(CONFIG_BT_MAIN_APP libsrc)
add_subdirectory_ifdef(CONFIG_CHARGER_APP charger)
add_subdirectory_ifdef(CONFIG_BT_MAIN_APP main)
add_subdirectory_ifdef(CONFIG_BT_PLAYER bt_player)
add_subdirectory_ifdef(CONFIG_BT_CALL_APP btcall)
add_subdirectory_ifdef(CONFIG_TOOL tool)
add_subdirectory_ifdef(CONFIG_OTA_APP ota)
add_subdirectory_ifdef(CONFIG_OTA_RECOVERY_APP ota_recovery)
add_subdirectory_ifdef(CONFIG_LCMUSIC_APP lcmusic)
add_subdirectory_ifdef(CONFIG_LOCAL_PLAYER local_player)
add_subdirectory_ifdef(CONFIG_ALARM_APP alarm)
add_subdirectory_ifdef(CONFIG_CARD_READER_APP card_reader)
add_subdirectory_ifdef(CONFIG_LAUNCHER_APP launcher)
add_subdirectory_ifdef(CONFIG_BT_TRANSMIT bt_transmit)
add_subdirectory_ifdef(CONFIG_LVGL widgets)
add_subdirectory_ifdef(CONFIG_BT_VENDOR vendor)
add_subdirectory_ifdef(CONFIG_BT_LOG_SERVICE bt_log)
add_subdirectory_ifdef(CONFIG_NSM_APP bt_nsm)

