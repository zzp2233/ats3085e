/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_internal.h"
#include <cstddef>

namespace gx {
namespace os {
size_t interrupt_disable();          // NOLINT(*-identifier-naming)
void interrupt_enable(size_t level); // NOLINT(*-identifier-naming)
} // namespace os
} // namespace gx
