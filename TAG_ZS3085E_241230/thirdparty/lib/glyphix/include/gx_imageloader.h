/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_file.h"
#include "gx_image.h"

namespace gx {
class ImageLoader {
public:
    enum Error { Success, NotExist, FormatError, DecodeError, AllocFailed };
    ImageLoader(const char *type);
    virtual ~ImageLoader();
    virtual Error load(Image::Data &data, File &file, bool withPixel) = 0;

protected:
    enum BlockMode {
        AutoFreePixels = 0,
        DontFreePixels = 1,
    };
    void setSize(Image::Data &data, int width, int height);
    void setPixelsData(Image::Data &data, uint8_t *pixels, int pitch,
                       BlockMode mode = AutoFreePixels);
    void setLutData(Image::Data &data, uint32_t *lut, int size);
    void setPixelFormat(Image::Data &data, int pixfmt);
};
} // namespace gx
