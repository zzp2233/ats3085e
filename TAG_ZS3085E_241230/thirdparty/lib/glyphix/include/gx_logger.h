/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_string.h"
#include <utility>

namespace gx {
// log level filters & log tags
#ifndef GX_LOGGING_LEVEL
#define GX_LOGGING_LEVEL GX_LOG_INFO
#endif
#ifndef GX_LOG_LEVEL
#define GX_LOG_LEVEL GX_LOGGING_LEVEL
#endif
#ifndef GX_LOG_TAG
#define GX_LOG_TAG nullptr
#endif

#define GX_LOG_DEBUG 0
#define GX_LOG_INFO  1
#define GX_LOG_WARN  2
#define GX_LOG_ERROR 3
#define GX_LOG_FATAL 4

class LoggerStream {
public:
    struct State {};
    LoggerStream() GX_DEFAULT;
    virtual ~LoggerStream() GX_DEFAULT;
    virtual State *start(int level, const char *tag) = 0;
    virtual void puts(State *state, const char *span) = 0;
    virtual State *flush(State *state, bool end) = 0;

    static LoggerStream *stream;
};

namespace logger {
enum FmtFlags {
    hex = 1 << 0,   // NOLINT(readability-*)
    fill0 = 1 << 1, // NOLINT(readability-*)
    left = 1 << 2,  // NOLINT(readability-*)
};

class fmt { // NOLINT(readability-*)
public:
    explicit fmt(int value, int width = 0, int flags = 0);
    explicit fmt(float value, int width = 0, int flags = 0);
    explicit fmt(double value, int width = 0, int flags = 0);
    explicit fmt(long value, int width = 0, int flags = 0);
    explicit fmt(long long value, int width = 0, int flags = 0);
    explicit fmt(unsigned int value, int width = 0, int flags = 0);
    explicit fmt(unsigned long value, int width = 0, int flags = 0);
    explicit fmt(unsigned long long value, int width = 0, int flags = 0);

private:
    char m_buffer[32];
    friend class gx::Logger;
};
} // namespace logger

class Logger {
public:
    struct endl_t {};    // NOLINT(readability-*)
    struct nospace_t {}; // NOLINT(readability-*)
    struct space_t {};   // NOLINT(readability-*)
    class holdspace {    // NOLINT(readability-*)
    public:
        explicit holdspace(const Logger &logger) : m_logger(logger) { logger << nospace_t(); }
        ~holdspace() { m_logger << space_t(); }

    private:
        const Logger &m_logger;
    };

    explicit Logger(int level, const char *tag);              // use LoggerStream::stream
    Logger(LoggerStream *stream, int level, const char *tag); // specified stream
    ~Logger();
    const Logger &operator<<(nospace_t) const;
    const Logger &operator<<(space_t) const;
    const Logger &operator<<(endl_t) const;
    const Logger &operator<<(int value) const;
    const Logger &operator<<(float value) const;
    const Logger &operator<<(double value) const;
    const Logger &operator<<(long value) const;
    const Logger &operator<<(long long value) const;
    const Logger &operator<<(unsigned int value) const;
    const Logger &operator<<(unsigned long value) const;
    const Logger &operator<<(unsigned long long value) const;
    const Logger &operator<<(const char *text) const;
    const Logger &operator<<(const String &text) const;
    const Logger &operator<<(bool value) const;
    const Logger &operator<<(const void *ptr) const;
    const Logger &operator<<(const logger::fmt &f) const;

    static const endl_t endl;       // NOLINT(readability-*)
    static const nospace_t nospace; // NOLINT(readability-*)
    static const space_t space;     // NOLINT(readability-*)

protected:
    char spaceChar() const;
    const Logger &puts(const char *span) const;

private:
    const char *m_tag;
    const uint16_t m_level;
    mutable uint16_t m_noSpaceNested;
    mutable LoggerStream::State *m_state;
    LoggerStream *const m_stream;
};

template<class T1, class T2>
const Logger &operator<<(const Logger &logger, const std::pair<T1, T2> &pair) {
    Logger::holdspace spc(logger);
    return logger << "std::pair(" << pair.first << ", " << pair.second << ")";
}

inline const Logger &operator<<(const Logger &logger, const uint8_t *bytes) {
    return logger << (const char *)bytes;
}

namespace internal {
struct LoggerFake {
    explicit LoggerFake(int, const char *) {}
    LoggerFake(LoggerStream *, int, const char *) {}
    template<class T> const LoggerFake &operator<<(const T &) const { return *this; }
};

template<bool> struct LoggerSelector {};
template<> struct LoggerSelector<true> {
    typedef Logger logger_type;
};
template<> struct LoggerSelector<false> {
    typedef LoggerFake logger_type;
};

template<int filter, int level> struct LoggerLevel : LoggerSelector<filter <= level>::logger_type {
    typedef typename LoggerSelector<filter <= level>::logger_type logger_type;
    explicit LoggerLevel(const char *tag = GX_LOG_TAG) : logger_type(level, tag) {}
    explicit LoggerLevel(LoggerStream *stream, const char *tag = GX_LOG_TAG)
        : logger_type(stream, level, tag) {}
};
} // namespace internal

typedef internal::LoggerLevel<GX_LOG_LEVEL, GX_LOG_DEBUG> LogDebug;
typedef internal::LoggerLevel<GX_LOG_LEVEL, GX_LOG_INFO> LogInfo;
typedef internal::LoggerLevel<GX_LOG_LEVEL, GX_LOG_WARN> LogWarn;
typedef internal::LoggerLevel<GX_LOG_LEVEL, GX_LOG_ERROR> LogError;
typedef internal::LoggerLevel<GX_LOG_LEVEL, GX_LOG_FATAL> LogFatal;
} // namespace gx
