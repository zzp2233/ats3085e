/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_abstractstream.h"
#include "gx_atomic.h"
#include "gx_semaphore.h"
#include "gx_vector.h"
#include "httpclient.h"

namespace gx {
class HttpClientStream : public AbstractStream {
public:
    HttpClientStream();
    ~HttpClientStream() override;

    int streamOpenByOffset(const String &uri, HttpFetch::Method method, const String &body,
                           HttpFetch::ContentType contentType, size_t offset);

    int open(const String &uri, HttpFetch::Method method);
    int open(const String &uri) override { return open(uri, HttpFetch::Method::Get); };

    off_t size() override;
    off_t seekByWhence(off_t pos, int whence) override;
    off_t seek(off_t pos) override;
    off_t tell() override;
    size_t read(void *data, size_t count) override;

private:
    struct AsyncReadTask;
    String m_uri;
    httpclient_session *m_session;
    off_t m_pos;
    off_t m_streamSize;
    int m_bufferPos;
    int m_bufferSize;
    off_t m_remainBytes;
    Vector<uint8_t> m_buffer;
    Semaphore m_readSem;
    AtomicInt m_breakRead;
    AtomicInt m_readError;
    Mutex m_mutex;
    AsyncReadTask *m_readTask;
};
} // namespace gx
