/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*********************
 *      INCLUDES
 *********************/
#include <os_common_api.h>
#include <app_ui.h>
#include <ui_service.h>
#include <msgbox_cache.h>

#ifdef CONFIG_LVGL
#  include <lvgl/lvgl_view.h>
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  GLOBAL PROTOTYPES
 **********************/
#ifdef CONFIG_ALARM_APP
extern int alarm_msgbox_popup_cb(uint16_t msgbox_id, uint8_t msg_id, void * msg_data, void * hwnd);
#endif
#ifdef CONFIG_OTA_BACKGROUND
extern int ota_msgbox_popup_cb(uint16_t msgbox_id, uint8_t msg_id, void * msg_data, void * hwnd);
#endif
#ifdef CONFIG_BT_CALL_APP
extern int btcall_msgbox_popup_cb(uint16_t msgbox_id, uint8_t msg_id, void * msg_data, void * hwnd);
#endif

extern int lpower_msgbox_popup_cb(uint16_t msgbox_id, uint8_t msg_id, void * msg_data, void * hwnd);

/**********************
 *  STATIC VARIABLES
 **********************/

static const msgbox_dsc_t msgbox_dsc[] = {
#ifdef CONFIG_ALARM_APP
	{ .id = ALARM_MSGBOX_ID, .order = 0, .handler = alarm_msgbox_popup_cb, },
#endif

#ifdef CONFIG_OTA_BACKGROUND
	{ .id = OTA_MSGBOX_ID, .order = 0, .handler = ota_msgbox_popup_cb, },
#endif

#ifdef CONFIG_BT_CALL_APP
	{ .id = BTCALL_MSGBOX_ID, .order = 0, .handler = btcall_msgbox_popup_cb, },
#endif

	{ .id = LPOWER_MSGBOX_ID, .order = 0, .handler = lpower_msgbox_popup_cb, },
};

#ifdef CONFIG_LVGL_USE_BITMAP_FONT
static const bitmap_font_cache_preset_t font_cache_preset[] = {
	{DEF_FONT28_FILE,64*1024},
	{DEF_FONT32_FILE,84*1024},
#ifndef CONFIG_EMOJI_FONT_USE_MMAP
#if CONFIG_PSRAM_SIZE > 4096
	{DEF_FONT28_EMOJI,72*1024},
#else
	{DEF_FONT28_EMOJI,36*1024},
#endif
#else
	{DEF_FONT28_EMOJI, 4*1024},
#endif
};
#endif /* CONFIG_LVGL_USE_BITMAP_FONT */

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int app_ui_init(void)
{
#ifdef CONFIG_LVGL
	lvgl_view_system_init();
#endif

	ui_service_register_gesture_default_callback();
	msgbox_cache_init(msgbox_dsc, ARRAY_SIZE(msgbox_dsc));

#ifdef CONFIG_LVGL_USE_RES_MANAGER
	lvgl_res_loader_init(DEF_UI_WIDTH, DEF_UI_HEIGHT);
#endif

#ifdef CONFIG_LVGL_USE_BITMAP_FONT
	lvgl_bitmap_font_init(NULL);
	lvgl_bitmap_font_cache_preset((void *)font_cache_preset, ARRAY_SIZE(font_cache_preset));
#endif

#ifdef CONFIG_LVGL_USE_FREETYPE_FONT
	lvgl_freetype_font_init();
//	lvgl_freetype_font_cache_preset((void *)ft_font_cache_preset, ARRAY_SIZE(ft_font_cache_preset));
#endif

	app_ui_stack_init(APP_UI_INVALID);

	return 0;
}
