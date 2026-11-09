/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONFIG_SIMULATOR
#include <soc.h>
#endif
#include <os_common_api.h>
#include <ui_manager.h>
#include <input_manager.h>
#include <property_manager.h>
#include <view_stack.h>
#include <sys_wakelock.h>
#include <app_ui.h>
#include <alipay.h>
#include <vendor_ble.h>
#include <vendor_ble_alipay.h>
#include "alipay_ui.h"

static bool is_bind_inited = false;
static bool is_barcode_mode = false;
static bool is_screen_locked = false;

static void _alipay_start_adv(void)
{
	vendor_ble_alipay_start_adv();
}

static void _alipay_stop_adv(void)
{
	vendor_ble_alipay_stop_adv();
}

static void _alipay_bind_init(void)
{
	if (!is_bind_inited) {
		alipay_bind_env_init();
		is_bind_inited = true;
	}
}

static void _alipay_bind_deinit(void)
{
	if (is_bind_inited) {
		alipay_bind_env_destroy();
		is_bind_inited = false;
	}
}

static void _alipay_do_unbind(void)
{
	alipay_unbinding();
}

static int _alipay_get_binding_status(void)
{
	int status = STATE_BINDING_OK;
	int binding_status = alipay_get_binding_status();

	switch (binding_status) {
		case STATUS_UNKNOWN:
		case STATUS_UNBINDED:
			status = STATE_UNBINDED;
			break;

		case STATUS_START_BINDING:
			status = STATE_WAIT_SCAN;
			break;

		case STATUS_GETTING_PROFILE:	//进度10%，进度>0时说明BLE连接已经OK，可以关闭绑定码，显示进度
		case STATUS_SAVING_DATA:		//进度30%
		case STATUS_SAVING_DATA_OK:		//进度70%
		case STATUS_FINISH_BINDING:		//进度90%
		case STATUS_FINISH_BINDING_OK:
			status = STATE_BINDING;
			break;

		case STATUS_BINDING_FAIL:
			status = STATE_BINDING_FAIL;
			break;

		case STATUS_BINDING_OK:
			status = STATE_BINDING_OK;
			break;
	}
	SYS_LOG_INF("ui binding status: %d", status);

	return status;
}

static int _alipay_get_binding_string(uint8_t *buf, uint32_t *out_len)
{
	int ret = alipay_get_binding_string(buf, out_len);

	buf[*out_len] = '\0';
	return ret;
}

static int _alipay_get_paycode_string(uint8_t *buf, uint32_t *out_len)
{
	int ret = alipay_get_paycode(buf, out_len);

	buf[*out_len] = '\0';
	return ret;
}

static int _alipay_get_userinfo(uint8_t *name, uint32_t *name_len, uint8_t *id, uint32_t *id_len)
{
	int ret = 0;
	
	ret |= alipay_get_nick_name(name, name_len);
	ret |= alipay_get_logon_ID(id, id_len);
	name[*name_len] = '\0';
	id[*id_len] = '\0';

	return ret;
}

static bool _alipay_is_barcode_on(void)
{
	return is_barcode_mode;
}

static void _alipay_toggle_barcode(void)
{
	is_barcode_mode = !is_barcode_mode;
}

static void _alipay_lock_screen(void)
{
	if (!is_screen_locked) {
#ifdef CONFIG_SYS_WAKELOCK
		sys_wake_lock(FULL_WAKE_LOCK);
#endif
		is_screen_locked = true;
	}
}

static void _alipay_unlock_screen(void)
{
	if (is_screen_locked) {
#ifdef CONFIG_SYS_WAKELOCK
		sys_wake_unlock(FULL_WAKE_LOCK);
#endif
		is_screen_locked = false;
	}
}

static void _alipay_init_cb(const uint8_t *data, uint16_t len)
{
	int status;

	alipay_exec_env_init();

	status = _alipay_get_binding_status();
	if (status != STATE_BINDING_OK) {
		_alipay_bind_init();
	}
}

static void _alipay_init(void)
{
	uint8_t pay_type = 0;

	// init ble
	vendor_ble_alipay_init();

	// run pay_lib_init in ui_srv to avoid stack overflow
	paylib_ble_recv_cb(&pay_type, sizeof(pay_type), _alipay_init_cb);
}

static void _alipay_sync_time(void)
{
}

const alipay_view_presenter_t g_alipay_view_presenter = {
	.init = _alipay_init,
	.start_adv = _alipay_start_adv,
	.stop_adv = _alipay_stop_adv,
	.bind_init = _alipay_bind_init,
	.bind_deinit = _alipay_bind_deinit,
	.do_unbind = _alipay_do_unbind,
	.sync_time = _alipay_sync_time,
	.get_binding_status = _alipay_get_binding_status,
	.get_binding_string = _alipay_get_binding_string,
	.get_paycode_string = _alipay_get_paycode_string,
	.get_userinfo = _alipay_get_userinfo,
	.is_barcode_on = _alipay_is_barcode_on,
	.toggle_barcode = _alipay_toggle_barcode,
	.lock_screen = _alipay_lock_screen,
	.unlock_screen = _alipay_unlock_screen,
};

