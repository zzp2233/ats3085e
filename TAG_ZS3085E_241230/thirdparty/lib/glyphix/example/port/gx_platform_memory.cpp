/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#include "gx_memory.h"
#include <cstdlib>

namespace gx {
void *malloc(std::size_t size, int) { return ::malloc(size); }

void *realloc(void *ptr, std::size_t size, int) { return ::realloc(ptr, size); }

void free(void *ptr, int) { ::free(ptr); }

namespace texture_allocator {
uint8_t *alloc(std::size_t size) { return static_cast<uint8_t *>(::malloc(size)); }

Block alloc(int width, int height, int pixfmt) {
    int pitch = width * PixelFormat::depth(pixfmt) / 8;
    std::size_t size = pitch * height;
    uint8_t *ptr = static_cast<uint8_t *>(::malloc(size));
    return Block(ptr, size, pitch);
}

void free(uint8_t *ptr) { ::free(ptr); }
} // namespace texture_allocator
} // namespace gx
