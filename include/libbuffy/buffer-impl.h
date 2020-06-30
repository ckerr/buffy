/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#ifndef INCLUDE_LIBBUFFY_BUFFER_IMPL_H_
#define INCLUDE_LIBBUFFY_BUFFER_IMPL_H_

#include <stddef.h>  // size_t
#include <stdint.h>  // int8_t

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

enum {
    BFY_BLOCK_FLAGS_UNMANAGED = (1<<0),
    BFY_BLOCK_FLAGS_READONLY = (1<<1)
};

struct bfy_block {
    int8_t* data;
    size_t size;

    size_t read_pos;
    size_t write_pos;

    int flags;
};

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BUFFER_H_
