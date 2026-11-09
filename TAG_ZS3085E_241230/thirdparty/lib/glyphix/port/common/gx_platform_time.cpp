/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "kernel.h"
#include "platform/gx_os.h"
#include <ctime>
#include <sys/time.h>
#include <logging/log.h>

namespace gx {
namespace os {
uint32_t clock_ms() {
    return sys_clock_tick_get();
}

uint64_t clock_us() {
    return (uint64_t)(sys_clock_tick_get() * 1000U);
}

void delay_ms(int time) { 
    k_sleep(K_MSEC(time));
}
uint64_t gmtime() {
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return ((uint64_t)tv.tv_sec) * 1000 + ((uint64_t)tv.tv_usec) / 1000;
}

uint64_t localtime(uint64_t utc_ms) {
    auto time = std::time_t(utc_ms / 1000);
    std::tm gmt_tm{};
    std::tm local_tm{};

    gmtime_r(&time, &gmt_tm);
    localtime_r(&time, &local_tm);

    std::time_t gmt = mktime(&gmt_tm);

    /* mktime removes the daylight saving time from the result time value, however we want to keep
     * it */
    local_tm.tm_isdst = 0;
    std::time_t local = mktime(&local_tm);

    return (uint64_t)difftime(local, gmt) * 1000;
}

} // namespace os
} // namespace gx
