/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_internal.h"
#include <stdint.h>

namespace gx {
namespace os {
uint32_t clock_ms();     // NOLINT(readability-*)
uint64_t clock_us();     // NOLINT(readability-*)
void delay_ms(int time); // NOLINT(readability-*)
uint64_t gmtime();
uint64_t localtime(uint64_t utcMs);
} // namespace os
} // namespace gx
