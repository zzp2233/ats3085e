/*
 * This file is part of Glyphix
 * Copyright (c) 2023-2024, Glyphix Development Team
 */

#include "gx_abstractstream.h"

#include <gx_httpstream.h>

namespace gx {
class MediaStream {
public:
    MediaStream();
    ~MediaStream();
    int open(const String &uri);
    off_t size();
    off_t tell();
    off_t seekByWhence(off_t pos, int whence);
    off_t seek(off_t pos);
    size_t read(void *data, size_t count);

private:
    AbstractStream *m_mediaStream = nullptr;
};
} // namespace gx