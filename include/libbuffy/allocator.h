/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#ifndef LIBBUFFY_ALLOCATOR_H
#define LIBBUFFY_ALLOCATOR_H

#include <stddef.h> // size_t, NULL

#ifdef __cplusplus
extern "C" {
#endif

struct bfy_allocator;

typedef struct bfy_memory
{
    void* data;
    size_t capacity;
    void* user_data;
}
bfy_memory;

typedef void (*bfy_free_func)(struct bfy_memory*);
typedef void* (*bfy_resize_func)(struct bfy_memory*, size_t);

typedef struct bfy_allocator
{
    bfy_free_func free;
    bfy_resize_func resize;
}
bfy_allocator;

extern const struct bfy_allocator BfyStackAllocator;
extern const struct bfy_allocator BfyHeapAllocator;
extern const struct bfy_allocator BfyHeapFirstAllocator;

extern void bfy_heap_free(struct bfy_memory* memory);
extern void* bfy_heap_resize(struct bfy_memory* memory, size_t size);
extern void bfy_stack_free(struct bfy_memory* memory);
extern void* bfy_stack_resize(struct bfy_memory* memory, size_t size);

#ifdef __cplusplus
}
#endif

#endif // #ifndef LIBBUFFY_ALLOCATOR_H
