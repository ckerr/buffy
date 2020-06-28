/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#ifndef INCLUDE_LIBBUFFY_BLOCK_H_
#define INCLUDE_LIBBUFFY_BLOCK_H_

#include <stddef.h>  // size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bfy_block
{
    void* data;
    size_t size;
    int (*realloc)(struct bfy_block*, size_t n);
}
bfy_block;

// convenience wrapper for block->realloc(block, 0)
int bfy_block_release(bfy_block* block);

// convenience wrapper for block->realloc(block, size)
int bfy_block_resize(bfy_block* block, size_t size);

struct bfy_block bfy_block_init(void);
struct bfy_block bfy_block_init_unowned(void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BLOCK_H_
