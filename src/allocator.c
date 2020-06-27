/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#include <libbuffy/allocator.h>

#include <errno.h>
#include <stdlib.h>

void* bfy_heap_resize(struct bfy_memory* memory, size_t n) {
    memory->data = realloc(memory->data, n);
    memory->capacity = n;
    return memory->data;
}

void bfy_heap_free(struct bfy_memory* memory) {
    free(memory->data);
}

const struct bfy_allocator BfyHeapAllocator = {
    .free = bfy_heap_free,
    .resize = bfy_heap_resize
};

void* bfy_stack_resize(struct bfy_memory* memory, size_t n) {
    if (memory->capacity < n) {
      errno = ENOMEM;
    }
    return memory->data;
}

void bfy_stack_free(struct bfy_memory* memory) {
}
const struct bfy_allocator BfyStackAllocator = {
    .free = bfy_stack_free,
    .resize = bfy_stack_resize
};

  // extern const struct bfy_allocator BfyStackAllocator;
  // extern const struct bfy_allocator BfyHeapAllocator;
  // extern const struct bfy_allocator BfyHeapFirstAllocator;

#if 0
static void* realloc_fail(void* buf, size_t n)
{
    errno = ENOMEM;
    return NULL;
}

static void* malloc_fail(size_t n)
{
    errno = ENOMEM;
    return NULL;
}

static void free_noop(void* buf)
{
}

/**
***
**/

const BfyStackAllocator = {
    .free: free_noop,
    .malloc: malloc_fail,
    .realloc: realloc_fail
};

const BfyHeapAllocator = {
    .free: free,
    .malloc: malloc,
    .realloc: realloc
};

const BfyStackAllocator static void
#include <stddef.h> // size_t, NULL

typedef void (*bfy_free_func)(struct bfy_allocator*, void*);
typedef void* (*bfy_malloc_func)(struct bfy_allocator*, size_t);
typedef void* (*bfy_realloc_func)(struct bfy_allocator*, void*, size_t);

typedef struct bfy_allocator
{
    bfy_free_func free;
    bfy_malloc_func malloc;
    bfy_realloc_func realloc;
}
bfy_allocator;

extern const BfyStackAllocator;
extern const BfyHeapAllocator;
extern const BfyHeapFirstAllocator;

#endif

