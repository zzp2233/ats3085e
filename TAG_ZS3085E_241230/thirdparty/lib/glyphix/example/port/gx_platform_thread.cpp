/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "platform/gx_thread.h"
#include <pthread.h>

namespace gx {
namespace os {
void *this_thread() { return reinterpret_cast<void *>(pthread_self()); }
} // namespace os
} // namespace gx
