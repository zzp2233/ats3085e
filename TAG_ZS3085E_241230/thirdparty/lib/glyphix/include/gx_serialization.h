/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_bytearray.h"
#include "gx_string.h"

namespace gx {
template<typename T = const uint8_t *> class BinaryReader {
public:
    BinaryReader(const T &first, const T &last) : m_first(first), m_pointer(first), m_last(last) {}
    GX_NODISCARD bool isEnd() const { return m_pointer == m_last; }
    GX_NODISCARD uint8_t readU8() {
        if (!isEnd())
            return *m_pointer++;
        return 0;
    }
    GX_NODISCARD uint16_t readU16() {
        if (!isEnd()) {
            uint16_t x = *m_pointer++;
            if (!isEnd())
                return x | uint16_t(*m_pointer++) << 8;
        }
        return 0;
    }
    GX_NODISCARD uint32_t readU32() {
        uint32_t x = 0;
        for (int i = 0; i < 4; ++i) {
            if (isEnd())
                return 0;
            x |= uint32_t(*m_pointer++) << (i * 8);
        }
        return x;
    }
    GX_NODISCARD uint64_t readU64() {
        uint64_t x = 0;
        for (int i = 0; i < 8; ++i) {
            if (isEnd())
                return 0;
            x |= uint64_t(*m_pointer++) << (i * 8);
        }
        return x;
    }
    GX_NODISCARD float readFloat() {
        union {
            int i;
            float f;
        } x = {0};
        for (int i = 0; i < 4; ++i) {
            if (isEnd())
                return 0;
            x.i |= uint32_t(*m_pointer++) << (i * 8);
        }
        return x.f;
    }
    GX_NODISCARD double readDouble() {
        union {
            uint64_t i;
            double f;
        } x = {};
        for (int i = 0; i < 8; ++i) {
            if (isEnd())
                return 0;
            x.i |= uint64_t(*m_pointer++) << (i * 8);
        }
        return x.f;
    }
    GX_NODISCARD String readString() {
        std::size_t size = readU16();
        if (!size)
            return String();
        String s(size);
        uint8_t *data = reinterpret_cast<uint8_t *>(s.data());
        for (std::size_t i = size; i; i--) {
            if (isEnd())
                return String();
            *data++ = *m_pointer++;
        }
        s.resize(size);
        return s;
    }
    void seek(int offset) { m_pointer = bound(m_first, m_pointer + offset, m_last); }

private:
    T m_first, m_pointer, m_last;
};

class BinaryWriter {
public:
    explicit BinaryWriter(ByteArray &data) : m_data(data) {}
    void writeU8(int x) { m_data.push_back(uint8_t(x)); }
    void writeU16(int x) {
        m_data.push_back(uint8_t(x));
        m_data.push_back(uint8_t(x >> 8));
    }
    void writeU32(uint32_t x) {
        uint8_t buf[4];
        for (int i = 0; i < 32; i += 8)
            buf[i / 8] = uint8_t(x >> i);
        m_data.append(buf, buf + sizeof(buf));
    }
    void writeU64(uint64_t x) {
        uint8_t buf[8];
        for (int i = 0; i < 64; i += 8)
            buf[i / 8] = uint8_t(x >> i);
        m_data.append(buf, buf + sizeof(buf));
    }
    void writeDouble(double x) {
        union {
            double d;
            uint8_t buf[8];
        } u;
        u.d = x;
        m_data.append(u.buf, u.buf + sizeof(u.buf));
    }
    void writeString(const char *s, std::size_t size) {
        const uint8_t *first = reinterpret_cast<const uint8_t *>(s);
        writeU16(int(size));
        m_data.append(first, first + size);
    }
    void writeString(const String &string) { writeString(string.c_str(), string.size()); }
    void writeString(const char *s) { writeString(s, std::strlen(s)); }

private:
    ByteArray &m_data;
};
} // namespace gx
