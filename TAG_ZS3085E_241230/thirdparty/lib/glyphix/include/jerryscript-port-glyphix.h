#ifndef JERRY_PORT_GX_H
#define JERRY_PORT_GX_H

#include "jerryscript-types.h"

JERRY_C_API_BEGIN

typedef void *(*jerry_context_allocator_t)(void *ptr, size_t *size);
typedef jerry_char_t *(*jerry_path_normalizer_t)(const jerry_char_t *, jerry_size_t, jerry_size_t);

void jerry_set_context_allocator(jerry_context_allocator_t cb);
void jerry_set_path_normalizer(jerry_path_normalizer_t cb);

JERRY_C_API_END

#endif
