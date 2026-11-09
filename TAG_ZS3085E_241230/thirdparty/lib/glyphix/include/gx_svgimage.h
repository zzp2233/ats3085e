/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_geometry.h"
#include "gx_sharedref.h"
#include "gx_string.h"

namespace gx {
class SVGImage {
public:
    explicit SVGImage(const String &uri);
    GX_NODISCARD Size size() const;

private:
    class Data;
    SharedRef<Data> d;
    friend class SVGRender;
};
} // namespace gx
