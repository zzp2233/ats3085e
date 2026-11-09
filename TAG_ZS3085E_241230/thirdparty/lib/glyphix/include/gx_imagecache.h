/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_image.h"
#include "gx_vector.h"
#include "intrusive/gx_cachemap.h"

namespace gx {
class ImageLoader;

namespace utils {
template<> struct cache_helper<Image::Data> {
    typedef String key_type;
    typedef std::size_t size_type;
    key_type key(const Image::Data &data);
    size_type size(const Image::Data &data);
    size_type release(Image::Data &data, size_type);
};
} // namespace utils

class ImageCache {
public:
    ~ImageCache();

    GX_NODISCARD std::size_t size() const { return m_cache.size(); }
    GX_NODISCARD std::size_t capacity() const { return m_cache.capacity(); }
    void setCapacity(std::size_t capacity);
    std::size_t flush(std::size_t size);
    /**
     * Flush the image cache for the specified URI.
     * @param uri URI of the image to be flushed.
     */
    void flushUri(const String &uri);
    /**
     * Flush the cache of all images where the URIs match the specified pattern.
     * @param pattern URI pattern of the image to be flushed. The \p pattern substring is matched
     * from the front of the image's URI. substring.
     */
    void flushMatch(const String &pattern);
    GX_NODISCARD SharedRef<Image::Data> load(const String &uri) { return load(uri, false); }
    GX_NODISCARD SharedRef<Image::Data> loadQuiet(const String &uri) { return load(uri, true); }
    void installLoader(ImageLoader *loader);

    static ImageCache *instance();

private:
    ImageCache();
    SharedRef<Image::Data> load(const String &uri, bool quiet);

    intrusive::CacheMap<Image::Data> m_cache;
    Vector<ImageLoader *> m_loaders;
    friend class Image::Data;
};
} // namespace gx
