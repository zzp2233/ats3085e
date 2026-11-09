/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <os_common_api.h>
#include <memory/mem_cache.h>
#include <lvgl/lvgl_memory.h>
#include <display/display_hal.h>
#include <ui_mem.h>

#ifdef CONFIG_VG_LITE
#  include <vg_lite/vglite_util.h>
#endif

void lvgl_img_buf_clean_cache(const lv_img_dsc_t *dsc)
{
	uint32_t size = dsc->data_size;

	if (size == 0) {
		size = lv_img_buf_get_img_size(dsc->header.w, dsc->header.h, dsc->header.cf);
	}

	mem_dcache_clean(dsc->data, size);
}

void lvgl_img_buf_invalidate_cache(const lv_img_dsc_t *dsc)
{
	uint32_t size = dsc->data_size;

	if (size == 0) {
		size = lv_img_buf_get_img_size(dsc->header.w, dsc->header.h, dsc->header.cf);
	}

	mem_dcache_invalidate(dsc->data, size);
}

bool lvgl_img_buf_is_clean(const void *buf, unsigned int size)
{
	if (mem_is_cacheable(buf) == false) {
		return true;
	}

#ifdef CONFIG_UI_MEMORY_MANAGER
	return ui_mem_is_type(MEM_FB, buf) || ui_mem_is_type(MEM_RES, buf);
#else
	return false;
#endif
}

uint32_t lvgl_img_cf_to_display(lv_img_cf_t cf, uint8_t * bits_per_pixel)
{
	uint32_t format = 0;
	uint8_t bpp = 0;

	switch (cf) {
	case LV_IMG_CF_RGB_565:
		format = HAL_PIXEL_FORMAT_RGB_565;
		bpp = 16;
		break;
	case LV_IMG_CF_ARGB_8565:
		format = HAL_PIXEL_FORMAT_ARGB_8565;
		bpp = 24;
		break;
	case LV_IMG_CF_ARGB_8888:
		format = HAL_PIXEL_FORMAT_ARGB_8888;
		bpp = 32;
		break;
	case LV_IMG_CF_XRGB_8888:
		format = HAL_PIXEL_FORMAT_XRGB_8888;
		bpp = 32;
		break;
	case LV_IMG_CF_RGB_888:
		format = HAL_PIXEL_FORMAT_RGB_888;
		bpp = 24;
		break;
	case LV_IMG_CF_ARGB_6666:
		format = HAL_PIXEL_FORMAT_ARGB_6666;
		bpp = 24;
		break;
	case LV_IMG_CF_ARGB_1555:
		format = HAL_PIXEL_FORMAT_ARGB_1555;
		bpp = 16;
		break;

	case LV_IMG_CF_ALPHA_8BIT:
		format = HAL_PIXEL_FORMAT_A8;
		bpp = 8;
		break;
	case LV_IMG_CF_ALPHA_4BIT:
		format = HAL_PIXEL_FORMAT_A4;
		bpp = 4;
		break;
	case LV_IMG_CF_ALPHA_2BIT:
		format = HAL_PIXEL_FORMAT_A2;
		bpp = 2;
		break;
	case LV_IMG_CF_ALPHA_1BIT:
		format = HAL_PIXEL_FORMAT_A1;
		bpp = 1;
		break;
	case LV_IMG_CF_ALPHA_4BIT_LE:
		format = HAL_PIXEL_FORMAT_A4_LE;
		bpp = 4;
		break;
	case LV_IMG_CF_ALPHA_1BIT_LE:
		format = HAL_PIXEL_FORMAT_A1_LE;
		bpp = 1;
		break;

	case LV_IMG_CF_INDEXED_8BIT:
	case LV_IMG_CF_INDEXED_8BIT_ACTS:
		format = HAL_PIXEL_FORMAT_I8;
		bpp = 8;
		break;
	case LV_IMG_CF_INDEXED_4BIT:
		format = HAL_PIXEL_FORMAT_I4;
		bpp = 4;
		break;
	case LV_IMG_CF_INDEXED_2BIT:
		format = HAL_PIXEL_FORMAT_I2;
		bpp = 2;
		break;
	case LV_IMG_CF_INDEXED_1BIT:
		format = HAL_PIXEL_FORMAT_I1;
		bpp = 1;
		break;

	default:
		break;
	}

	if (bits_per_pixel) {
		*bits_per_pixel = bpp;
	}

	return format;
}

#ifdef CONFIG_VG_LITE

int lvgl_img_cf_to_vglite(lv_img_cf_t cf, uint8_t * bits_per_pixel)
{
	int format = -1;
	uint8_t bpp = 0;

	switch(cf) {
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
	case LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED: /* fall through */
#endif
	case LV_IMG_CF_RGB_565:
		format = VG_LITE_BGR565;
		bpp = 16;
		break;

	case LV_IMG_CF_ARGB_8565:
		format = VG_LITE_BGRA5658;
		bpp = 24;
		break;

	case LV_IMG_CF_ARGB_8888:
		format = VG_LITE_BGRA8888;
		bpp = 32;
		break;

#if LV_COLOR_DEPTH == 32
	case LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED: /* fall through */
#endif
	case LV_IMG_CF_XRGB_8888:
		format = VG_LITE_BGRX8888;
		bpp = 32;
		break;

	case LV_IMG_CF_RGB_888:
		format = VG_LITE_BGR888;
		bpp = 24;
		break;

	case LV_IMG_CF_ARGB_1555:
		format = VG_LITE_BGRA5551;
		bpp = 16;
		break;

	case LV_IMG_CF_ALPHA_8BIT:
		format = VG_LITE_A8;
		bpp = 8;
		break;

	case LV_IMG_CF_INDEXED_8BIT:
	case LV_IMG_CF_INDEXED_8BIT_ACTS:
		format = VG_LITE_INDEX_8;
		bpp = 8;
		break;

	case LV_IMG_CF_ETC2_EAC:
		format = VG_LITE_RGBA8888_ETC2_EAC;
		bpp = 8;
		break;

	default:
		break;
	}

	if (bits_per_pixel) {
		*bits_per_pixel = bpp;
	}

	return format;
}

int lvgl_vglite_map(vg_lite_buffer_t *vgbuf, const void *color_p,
		uint16_t w, uint16_t h, uint16_t stride, lv_img_cf_t cf)
{
	uint8_t bpp = 0;
	int format;

	if (vgbuf == NULL || color_p == NULL)
		return -EINVAL;

	format = lvgl_img_cf_to_vglite(cf, &bpp);
	if (format < 0)
		return -EINVAL;

	const uint8_t * color8 = color_p;
	if (cf == LV_IMG_CF_INDEXED_8BIT) {
		color8 += 1024;
	} else if (cf == LV_IMG_CF_INDEXED_8BIT_ACTS) {
		color8 += *((uint32_t *)color8) * 4 + 4;
	}

	stride = stride * bpp / 8;
	if (format == VG_LITE_RGBA8888_ETC2_EAC)
		stride = (stride + 0x3) & ~0x3;

	return vglite_buf_map(vgbuf, (void *)color8, w, h, stride, format);
}

int lvgl_vglite_map_img(vg_lite_buffer_t *vgbuf, const lv_img_dsc_t *img_dsc)
{
	return lvgl_vglite_map(vgbuf, img_dsc->data, img_dsc->header.w,
			img_dsc->header.h, img_dsc->header.w, img_dsc->header.cf);
}

int lvgl_vglite_unmap(vg_lite_buffer_t *vgbuf)
{
	return vglite_buf_unmap(vgbuf);
}

int lvgl_vglite_set_img_clut(const lv_img_dsc_t *img_dsc)
{
	vg_lite_uint32_t *palette = (vg_lite_uint32_t *)img_dsc->data;

	switch (img_dsc->header.cf) {
	case LV_IMG_CF_INDEXED_8BIT_ACTS:
		palette += 4;
		break;
	case LV_IMG_CF_INDEXED_8BIT:
		break;
	default:
		return -EINVAL;
	}

	return vg_lite_set_CLUT(256, palette);
}

#endif /* CONFIG_VG_LITE */
