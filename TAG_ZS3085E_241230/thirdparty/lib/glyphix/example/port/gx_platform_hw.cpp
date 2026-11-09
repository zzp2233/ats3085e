/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_mutex.h"
#include "platform/gx_hw.h"

namespace gx {
namespace os {
Mutex interruptMutex;

size_t interrupt_disable() {
    interruptMutex.lock();
    return 0;
}

void interrupt_enable(size_t) { interruptMutex.unlock(); }
} // namespace os
} // namespace gx
