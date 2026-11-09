/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_coreapplication.h"
#include "gx_signals.h"

namespace gx {
class Font;
class Widget;
class EventLoop;
class PaintDevice;
class WidgetManager;
class StyleEngine;
class FontFaceMap;
struct PaintDeviceMetric;

class Application : public CoreApplication {
public:
    enum CacheTypes {
        ImageTextureCache,
        WidgetSnapshotCache,
        FontGlyphCache,
    };

    explicit Application(PlatformAdapter *adapter = nullptr);
    virtual ~Application();

    GX_NODISCARD PaintDevice *screenDevice() const;
    GX_NODISCARD const PaintDeviceMetric &screenMetric() const;
    void updateScreenMetric();
    GX_NODISCARD Widget *window() const;
    void setWindow(Widget *window);
    GX_NODISCARD StyleEngine *styleEngine() const { return m_styleEngine; }
    void setStyleEngine(StyleEngine *styleEngine);
    /**
     * Get the global default font.
     */
    GX_NODISCARD const Font &font() const;
    /**
     * Set the global default font.
     */
    void setFont(const Font &font);
    /**
     * Set the default pixel format. This format will be used as the default when creating Pixmap
     * objects. The default pixel format with and without alpha channels can be set separately to
     * optimize performance.
     * @param format Pixel format, either one of PixelFormat::Format or a value supported by the
     * specific platform.
     * @param transparent This argument allows to set the default transparent / non-transparent
     * pixel format respectively. When both cases need to be set, just call setDefaultPixelFormat()
     * twice and specify different transparent.
     * @see defaultPixelFormat()
     * @see Pixmap::Pixmap(int, int, int)
     */
    void setDefaultPixelFormat(int format, bool transparent = false);
    /**
     * Get the default pixel format.
     * @param transparent Get the default pixel format with transparent channel if transparent is
     * true, otherwise get the default pixel format without transparent channel.
     * @return The enumeration value of the default pixel format.
     * @see setDefaultPixelFormat()
     */
    GX_NODISCARD int defaultPixelFormat(bool transparent = false) const;
    GX_NODISCARD std::size_t cacheSize(CacheTypes category) const;
    GX_NODISCARD std::size_t cacheCapacity(CacheTypes category) const;
    void setCacheCapacity(CacheTypes category, std::size_t capacity);
    std::size_t flushCache(std::size_t size);
    GX_NODISCARD WidgetManager *widgetManager() const;

    Signal<uint32_t> preTick;
    Signal<uint32_t> postTick;

    struct Private;

protected:
    virtual void event(EventLoop::EventFilter filter);

private:
    class EventDispatcher;
    StyleEngine *m_styleEngine;
};

inline Application *App() { // NOLINT(readability-*)
    return static_cast<Application *>(CoreApp());
}
} // namespace gx