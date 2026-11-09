/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_deviceinfo.h"

namespace gx {
JsValue device_info_get_brand() { return "glyphix"; }

JsValue device_info_get_manufacturer() { return "manufacturer"; }

JsValue device_info_get_model() { return "model"; }

JsValue device_info_get_product() { return "product"; }

JsValue device_info_get_device_id() { return "456"; }

JsValue device_info_get_serial_number() { return "sn123"; }

JsValue device_info_get_mac() { return "789"; }

JsValue device_info_get_user_id() { return "234"; };

JsValue device_info_get_advertis_id() { return "345"; };

JsValue device_info_get_os_type() { return "rtos"; }

JsValue device_info_get_os_version_name() { return "rtos"; }

int device_info_get_totalstorage() { return 64 * 1024 * 1024; }

int device_info_get_availablestorage() { return 32 * 1024 * 1024; }
} // namespace gx
