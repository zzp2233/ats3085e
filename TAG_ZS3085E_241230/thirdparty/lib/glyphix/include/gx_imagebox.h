/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_image.h"
#include "gx_svgimage.h"
#include "gx_transformoperations.h"
#include "gx_widget.h"

namespace gx {
class ImageBox : public Widget {
    GX_OBJECT

public:
    explicit ImageBox(Widget *parent = nullptr);
    virtual ~ImageBox();

    GX_NODISCARD Size imageSize() const;
    GX_NODISCARD ObjectFitMode objectFit() const { return m_objectFit; }
    GX_NODISCARD bool isCacheable() const { return m_cacheable; }
    GX_NODISCARD TransformOperations transform() const;
    void setImage(const Image &image);
    void setImage(const SVGImage &image);
    void setImage(const String &uri);
    void setObjectFit(ObjectFitMode fit);
    void setCacheable(bool enabled);
    void setTransform(const TransformOperations &transform);
    GX_NODISCARD virtual Size sizeHint() const;

    GX_PROPERTY(String src, set setImage, uri true)
    GX_PROPERTY(ObjectFitMode objectFit, get objectFit, set setObjectFit)
    GX_PROPERTY(TransformOperations transform, set setTransform, get transform)
    GX_PROPERTY(bool noCache, set setNoCache, get isNoCache)

protected:
    virtual void updateStyle();
    virtual void paintEvent(PaintEvent *event);

private:
    GX_NODISCARD bool isNoCache() const { return !isCacheable(); }
    void setNoCache(bool enabled) { setCacheable(!enabled); }
    void destroy();

private:
    union {
        uint8_t image[sizeof(Image)];
        uint8_t svg[sizeof(SVGImage)];
    } u;
    uint8_t m_type;
    uint8_t m_cacheable;
    TransformOperations *m_transform;
    ObjectFitMode m_objectFit;
};
} // namespace gx
