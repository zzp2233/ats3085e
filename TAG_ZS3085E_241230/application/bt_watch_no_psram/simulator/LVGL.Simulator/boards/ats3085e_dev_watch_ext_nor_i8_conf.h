/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ATS3085E_DVB_WATCH_EXT_NOR_I8_CONF_H_
#define ATS3085E_DVB_WATCH_EXT_NOR_I8_CONF_H_

 /* panel config */
#define CONFIG_PANEL_HOR_RES 466
#define CONFIG_PANEL_VER_RES 466
#define CONFIG_PANEL_TIMING_REFRESH_RATE_HZ 50
#define CONFIG_PANEL_ROUND_SHAPE 1
/* lvgl config */
#define CONFIG_LV_VDB_NUM 2
#define CONFIG_LV_VDB_SIZE 36348
/* ui mem config */
#define CONFIG_UI_GUI_MEM_POOL_SIZE 327680
#define CONFIG_UI_RES_MEM_POOL_SIZE 0
/* ui decoder config */
#define CONFIG_TILE_CACHE_NUM 2
#define CONFIG_TILE_MAX_H 78
#define CONFIG_TILE_MAX_W 78
#define CONFIG_TILE_BYTES_PER_PIXELS 3
#define CONFIG_LVGL_DECODE_CACHE_BUF_SIZE 90870
/* font cache config */
#define CONFIG_BITMAP_PER_FONT_CACHE_SIZE 0
#define CONFIG_BITMAP_FONT_CACHE_POOL_SIZE 0
#define CONFIG_FREETYPE_PER_FONT_CACHE_SIZE 24576
/* resource path config */
#define CONFIG_APP_UI_DISK "../../boards/ats3085e_dev_watch_ext_nor_i8/res/"
#define CONFIG_APP_FONT_DISK "../../boards/ats3085e_dev_watch_ext_nor_i8/fonts/"
#define CONFIG_WELCOME_UI_DISK "../../boards/ats3085e_dev_watch_ext_nor_i8/sdfs_k/"

#endif /* ATS3085E_DVB_WATCH_EXT_NOR_I8_CONF_H_ */
