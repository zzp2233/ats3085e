/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_networkmanager.h"
#include "gx_string.h"

namespace gx {
class AbstractStream {
public:
    virtual ~AbstractStream() GX_DEFAULT;
    virtual int open(const String &uri) = 0;
    virtual off_t size() = 0;
    virtual off_t tell() = 0;
    virtual off_t seek(off_t pos) = 0;
    virtual off_t seekByWhence(off_t pos, int whence) = 0;
    virtual size_t read(void *data, size_t count) = 0;
};
} // namespace gx
