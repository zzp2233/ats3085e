/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_logger.h"
#include "gx_mutex.h"
#include <cstdio>

#define LOG_DEBUG_PROMPT "\x1b[90m"
#define LOG_INFO_PROMPT  ""
#define LOG_WARN_PROMPT  "\x1b[33m"
#define LOG_ERROR_PROMPT "\x1b[31m"
#define LOG_END_LINE     "\x1b[0m\n"

class StdoutLoggerStream : public gx::LoggerStream {
public:
    struct LoggerState : State {
        int level;
        gx::String buffer;
        explicit LoggerState(int level) : level(level), buffer(256) {}
    };

    virtual State *start(int level, const char *tag) {
        auto *state = new LoggerState(level);
        const char *prompt;
        switch (level) {
        case GX_LOG_DEBUG: prompt = LOG_DEBUG_PROMPT; break;
        case GX_LOG_INFO: prompt = LOG_INFO_PROMPT; break;
        case GX_LOG_WARN: prompt = LOG_WARN_PROMPT; break;
        case GX_LOG_ERROR:
        case GX_LOG_FATAL:
        default: prompt = LOG_ERROR_PROMPT; break;
        }
        state->buffer += prompt;
        if (tag)
            state->buffer << '[' << tag << "] ";
        return state;
    }
    virtual void puts(State *state, const char *span) {
        static_cast<LoggerState *>(state)->buffer += span;
    }
    virtual State *flush(State *state, bool) {
        static_cast<LoggerState *>(state)->buffer += LOG_END_LINE;
        gx::LockGuard<gx::Mutex> locker(m_mutex);
        std::fputs(static_cast<LoggerState *>(state)->buffer.c_str(), stdout);
        std::fflush(stdout);
        if (static_cast<LoggerState *>(state)->level == GX_LOG_FATAL)
            abort();
        delete static_cast<LoggerState *>(state);
        return nullptr;
    }

private:
    gx::Mutex m_mutex;
};

static StdoutLoggerStream instance;
gx::LoggerStream *gx::LoggerStream::stream = &instance;
