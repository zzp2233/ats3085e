/*
 * Copyright (c) 2020 Actions Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <os_common_api.h>
#include <app_ui.h>
#include "alipay_ui.h"

void alipay_ui_sync_time(void)
{
	g_alipay_view_presenter.sync_time();
}

void alipay_ui_init(void)
{
	g_alipay_view_presenter.init();
}

void alipay_ui_enter(int anim_type)
{
	int status = g_alipay_view_presenter.get_binding_status();

	switch(status) {
		case STATE_BINDING_OK:
			app_ui_stack_forward(ALIPAY_PAY_UI, anim_type);
			break;

		default:
			app_ui_stack_forward(ALIPAY_BIND_UI, anim_type);
			break;
	}
}

void alipay_ui_update(void)
{
	int status = g_alipay_view_presenter.get_binding_status();

	switch(status) {
		case STATE_BINDING_OK:
			app_ui_stack_update(ALIPAY_PAY_UI, APP_UI_LOAD_ANIM_NONE);
			break;

		default:
			app_ui_stack_update(ALIPAY_BIND_UI, APP_UI_LOAD_ANIM_NONE);
			break;
	}
}

void wxpay_ui_init(void)
{
	int status;

	g_wxpay_view_presenter.init();

	status = g_wxpay_view_presenter.get_binding_status();
	if (status != STATE_BINDING_OK) {
		g_wxpay_view_presenter.bind_init();
	}
}

void wxpay_ui_enter(int anim_type)
{
	int status = g_wxpay_view_presenter.get_binding_status();

	switch(status) {
		case STATE_BINDING_OK:
			app_ui_stack_forward(WXPAY_PAY_UI, anim_type);
			break;

		default:
			app_ui_stack_forward(WXPAY_BIND_UI, anim_type);
			break;
	}
}

void wxpay_ui_update(void)
{
	int status = g_wxpay_view_presenter.get_binding_status();

	switch(status) {
		case STATE_BINDING_OK:
			app_ui_stack_update(WXPAY_PAY_UI, APP_UI_LOAD_ANIM_NONE);
			break;

		default:
			app_ui_stack_update(WXPAY_BIND_UI, APP_UI_LOAD_ANIM_NONE);
			break;
	}
}
