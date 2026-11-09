/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_imageloader.h"

namespace gx {
class ImageLoaderHDC : public ImageLoader {
public:
    ImageLoaderHDC() : ImageLoader("HDC") {}
    virtual Error load(Image::Data &data, File &file, bool withPixel);
};
} // namespace gx
