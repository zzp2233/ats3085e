/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once
#include <stdarg.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int gx_snprintf(char *buf, size_t size, const char *fmt, ...);

int gx_printf(const char *fmt, ...);

int gx_vsnprintf(char *buf, size_t size, const char *fmt, va_list arg);

#ifdef __cplusplus
}
#endif