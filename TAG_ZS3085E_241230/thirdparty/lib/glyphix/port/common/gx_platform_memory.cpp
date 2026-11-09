/*
 * This file is part of PersimUI
 * Copyright (c) 2006-2023, RT-Thread Development Team
 */
#include "gx_memory.h"
#include "kernel.h"
#include <cstdlib>

namespace gx {
void *alloc_align(size_t size, size_t align) {
    void *align_ptr = NULL;
    int uintptr_size = sizeof(void*);
    uintptr_size -= 1;
    
    align = ((align + uintptr_size) & ~uintptr_size);
    size_t align_size = ((size + uintptr_size) & ~uintptr_size) + align;
    void *ptr = ::malloc(align_size);
    if (ptr != NULL) {
        if (((unsigned long)ptr & (align - 1)) == 0) {
            align_ptr = (void *)((unsigned long)ptr + align);
        } else {
            align_ptr = (void *)(((unsigned long)ptr + (align - 1)) & ~(align - 1));
        }
        *((unsigned long*)((unsigned long)align_ptr - sizeof(void*))) = (unsigned long)ptr;
        ptr = align_ptr;
    }
    return ptr;
}

void free_align(void *ptr) {
    if (ptr == NULL) return;
    void *real_ptr = (void*) * (unsigned long*)((unsigned long)ptr - sizeof(void*));
    ::free(real_ptr);
}

void *malloc(std::size_t size, int) { return ::malloc(size); }

void *realloc(void *ptr, std::size_t size, int) { return ::realloc(ptr, size); }

void free(void *ptr, int) { ::free(ptr); }

namespace texture_allocator {
uint8_t *alloc(std::size_t size) { return static_cast<uint8_t *>(alloc_align(size, 64)); }

Block alloc(int width, int height, int pixfmt) {
    int pitch = width * PixelFormat::depth(pixfmt) / 8;
    std::size_t size = pitch * height;
    uint8_t *ptr = static_cast<uint8_t *>(alloc_align(size, 64));
    return Block(ptr, size, pitch);
}

void free(uint8_t *ptr) { free_align(ptr); }
} // namespace texture_allocator
} // namespace gx
