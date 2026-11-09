/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_sharedref.h"
#include <utility>

namespace gx {
class String;

class ByteArray {
public:
    typedef const uint8_t *const_iterator;
    explicit ByteArray(std::size_t size = 0);
    ByteArray(const uint8_t *data, std::size_t size);
    ByteArray(const ByteArray &array);
    ByteArray &operator=(const ByteArray &array);
    GX_NODISCARD bool isEmpty() const;
    GX_NODISCARD bool isStatic() const;
    GX_NODISCARD std::size_t size() const;
    GX_NODISCARD const_iterator begin() const { return constData(); }
    GX_NODISCARD const_iterator end() const { return constData() + size(); }
    void clear();
    void resize(std::size_t size);
    void push_back(std::uint8_t byte); // NOLINT(readability-*)
    void append(const_iterator first, const_iterator last);
    GX_NODISCARD uint8_t *data();
    GX_NODISCARD const uint8_t *constData() const;
    GX_NODISCARD const char *charData() const {
        return reinterpret_cast<const char *>(constData());
    }
    uint8_t operator[](std::size_t index) const { return constData()[index]; }
    GX_NODISCARD uint8_t *detach();
    GX_NODISCARD uint8_t *detachUnique();
    /**
     * Converts a ByteArray to a String object.
     */
    GX_NODISCARD String toString() const;
    /**
     * Converts a ByteArray to a String object.。Equivalent to function toString() const.
     */
    explicit operator String() const;

    static void free(void *data);

private:
    class Data;
    SharedRef<Data> d;
};
} // namespace gx
