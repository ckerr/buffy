/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#ifndef INCLUDE_LIBBUFFY_BUFFER_H_
#define INCLUDE_LIBBUFFY_BUFFER_H_

#include <stddef.h>  // size_t
#include <stdarg.h>  // va_list

#include <libbuffy/allocator.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bfy_buffer {
    size_t length;
    struct bfy_memory memory;
    struct bfy_allocator allocator;
}
bfy_buffer;

extern const struct bfy_buffer BfyBufferInit;

void  bfy_buffer_construct(bfy_buffer* buf);
void  bfy_buffer_set_allocator(bfy_buffer* buf, bfy_allocator allocator);
void  bfy_buffer_set_memory(bfy_buffer* buf, bfy_memory memory);

void  bfy_buffer_destruct(bfy_buffer* buf);

void* bfy_buffer_begin(bfy_buffer* buf);
void* bfy_buffer_end(bfy_buffer* buf);

const void* bfy_buffer_cbegin(bfy_buffer const* buf);
const void* bfy_buffer_cend(bfy_buffer const* buf);

void  bfy_buffer_clear(bfy_buffer* buf);

char* bfy_buffer_destruct_to_string(bfy_buffer* buf, size_t* length);

void  bfy_buffer_reserve(bfy_buffer* buf, size_t n);

void  bfy_buffer_reserve_available(bfy_buffer* buf, size_t n);

void  bfy_buffer_add(bfy_buffer* buf, void const* addme, size_t n);

void  bfy_buffer_add_ch(bfy_buffer* buf, char ch);

void  bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...);

void  bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in);

#define BFY_HEAP_BUFFER(name) \
    bfy_buffer name = { \
        .memory.data = NULL, \
        .memory.capacity = 0, \
        .allocator.resize = bfy_heap_resize, \
        .allocator.free = bfy_heap_free, \
        .length = 0 \
    };

#define BFY_STACK_BUFFER(name, size) \
    char name##_stack[size]; \
    bfy_buffer name = { \
        .memory.data = name##_stack, \
        .memory.capacity = size, \
        .allocator.resize = bfy_stack_resize, \
        .allocator.free = bfy_stack_free, \
        .length = 0 \
    };

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BUFFER_H_
