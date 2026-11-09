/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_brightness.h"

namespace gx {
int screen_brightness_get() { return 100; }

int screen_brightness_set(int brightness) {
    (void)brightness;
    return 0;
}

int screen_brightness_mode() { return 0; }

int screen_brightness_set_mode(int mode) {
    (void)mode;
    return 0;
}

int screen_keep_screen_on() { return 0; }

int screen_off() { return 0; }

int screen_on() { return 0; }

int screen_off_time_set(unsigned int time) {
    (void)time;
    return 0;
}

void screen_always_bright(int isAlways) { (void)isAlways; }
} // namespace gx
