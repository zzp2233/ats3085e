/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_font.h"
#include "gx_geometry.h"

namespace gx {
class FontMetrics {
public:
    explicit FontMetrics(const Font &font);
    GX_NODISCARD Rect boundingRect(const String &text, int width, int height, int flags = 0,
                                   int maxLines = 1, float lineHeight = 0);
    GX_NODISCARD int width(const String &text, int length = -1) const;
    GX_NODISCARD Size size(const String &text, int maxLines = 0, float lineHeight = 0) const;
    GX_NODISCARD String elidedText(const String &text, int width) const;
    GX_NODISCARD String elidedText(const String &text, const Rect &rect, int flags = 0,
                                   float lineHeight = 0) const;
    /**
     * Get the character count of the text in the layout area, any overflowed text that will be
     * truncated.
     * @param text The text to be layout.
     * @param rect The layout area.
     * @param flags The text flags, see AlignmentFlag and TextOverflowMode.
     * @param maxLines The maximum layout text lines.
     * @param lineHeight The line height, use default row height if less than or equal to 0.
     * @return The character count in layout area if the text is truncated, return otherwise -1.
     */
    GX_NODISCARD int truncate(const String &text, const Rect &rect, int flags = 0, int maxLines = 1,
                              float lineHeight = 0);

    GX_NODISCARD static float pointToPixel(float point);
    GX_NODISCARD static float pixelToPoint(float pixel);

private:
    Font m_font;
};
} // namespace gx
