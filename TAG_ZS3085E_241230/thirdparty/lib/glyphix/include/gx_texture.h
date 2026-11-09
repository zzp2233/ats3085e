/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_geometry.h"

namespace gx {
class Texture {
public:
    virtual ~Texture();
    GX_NODISCARD bool isEmpty() const;
    GX_NODISCARD int pixelFormat() const { return data.pixfmt; }
    GX_NODISCARD int pitch() const { return data.pitch; }
    GX_NODISCARD int width() const { return data.width; }
    GX_NODISCARD int height() const { return data.height; }
    GX_NODISCARD uint8_t *pixels() const { return data.pixels; }
    GX_NODISCARD uint8_t *lineSpan(int line) const { return &data.pixels[line * data.pitch]; }
    GX_NODISCARD uint32_t *lut() const { return data.lut; }
    GX_NODISCARD int lutSize() const { return int(data.lutSize); }
    GX_NODISCARD Size size() const { return Size(width(), height()); }
    GX_NODISCARD Rect rect() const { return Rect(0, 0, width(), height()); }

protected:
    Texture();
    void swap(Texture &src);
    void assign(const Texture &src);
    struct Data {
        uint64_t pixfmt : 5;
        uint64_t lutSize : 9;
        uint64_t pitch : 16;
        uint64_t extra : 6; // can be used as flags
        uint64_t width : 14;
        uint64_t height : 14;
        uint8_t *pixels;
        uint32_t *lut;
    } data;
};
} // namespace gx
