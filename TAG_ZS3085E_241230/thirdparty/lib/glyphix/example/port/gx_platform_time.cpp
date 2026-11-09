/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "platform/gx_os.h"
#include <chrono>
#include <thread>

namespace gx {
namespace os {
static auto start_time = std::chrono::high_resolution_clock::now();

uint32_t clock_ms() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now - start_time;
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
}

uint64_t clock_us() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now - start_time;
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

void delay_ms(int time) { std::this_thread::sleep_for(std::chrono::milliseconds(time)); }
} // namespace os
} // namespace gx
