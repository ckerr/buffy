/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#include <libbuffy/block.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

int
bfy_heap_realloc(struct bfy_block* block, size_t requested)
{
    // freeing the memory
    if (block->data != NULL && requested == 0) {
        fprintf(stderr, "free block %p data %p\n", block, block->data);
        free(block->data);
        block->data = NULL;
        return 0;
    }

    // decide on a new capacity
    size_t const Min = 512;
    size_t new_size = Min;
    while (new_size < requested) {
        new_size *= 2u;
    }

    fprintf(stderr, "realloc block %p data %p %zu\n", block, block->data, new_size);
    void* new_data = realloc(block->data, new_size);
    if (new_data == NULL) {
        errno = ENOMEM;
        return -1;
    } else {
        block->data = new_data;
        block->size = new_size;
        return 0;
    }
}

int
bfy_noop_realloc(struct bfy_block* block, size_t size)
{
    if (block->size < size)
    {
        errno = ENOMEM;
        return -1;
    }
    else
    {
        return 0;
    }
}

int
bfy_block_release(struct bfy_block* block)
{
    return block->realloc(block, 0);
}

int
bfy_block_resize(struct bfy_block* block, size_t size)
{
    return block->realloc(block, size);
}

struct bfy_block
bfy_block_init(void)
{
    struct bfy_block block = { 0 };
    block.realloc = bfy_heap_realloc;
    return block;
}

struct bfy_block
bfy_block_init_unowned(void* data, size_t size)
{
    struct bfy_block block = { 0 };
    block.data = data;
    block.size = size;
    block.realloc = bfy_noop_realloc;
    return block;
}

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

const BfyFixedAllocator = {
    .free: free_noop,
    .malloc: malloc_fail,
    .realloc: realloc_fail
};

const BfyHeapAllocator = {
    .free: free,
    .malloc: malloc,
    .realloc: realloc
};

const BfyFixedAllocator static void
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

extern const BfyFixedAllocator;
extern const BfyHeapAllocator;
extern const BfyHeapFirstAllocator;

#endif

