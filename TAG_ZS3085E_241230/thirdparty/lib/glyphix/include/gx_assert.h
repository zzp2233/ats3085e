/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#ifndef GX_ENABLE_SOURCE
#include "gx_config_export.h"
#endif

#ifdef GX_PLATFORM_CONFIG
#include "gx_platform_config.h"
#endif

/* -------- start of assert macros ---------- */
#ifndef GX_ASSERT
#define GX_ASSERT(expr) ((void)0)
#endif

#ifdef GX_NO_DEBUG
#undef GX_ASSERT
#define GX_ASSERT(expr) ((void)0)
#endif

#ifdef GX_ENABLE_CRT_CHECK
#define GX_CRT_ASSERT(expr) GX_ASSERT(expr)
#else
#define GX_CRT_ASSERT(expr) ((void)0)
#endif
/* --------- end of assert macros ----------- */
