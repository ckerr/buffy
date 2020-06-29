/*
 * This file Copyright (C) 2009-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <libbuffy/buffer.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>  // vsprintf()
#include <string.h>  // memcpy()
#include <stdlib.h>  // malloc(), free()

/// Life cycle -- bfy_buffer owned by caller

struct bfy_buffer
bfy_buffer_init_with_block(bfy_block block) {
    struct bfy_buffer buf = { 0 };
    buf.block = block;
    return buf;
}

struct bfy_buffer bfy_buffer_init(void) {
    return bfy_buffer_init_with_block(bfy_block_init());
}

struct bfy_buffer
bfy_buffer_init_unowned(void* data, size_t size) {
    return bfy_buffer_init_with_block(bfy_block_init_unowned(data, size));
}

void
bfy_buffer_destruct(bfy_buffer* buf) {
    bfy_block_release(&buf->block);
}

/// Life cycle -- bfy_buffer is heap-allocated

struct bfy_buffer*
bfy_buffer_new_with_block(bfy_block block) {
    struct bfy_buffer* buf = malloc(sizeof(struct bfy_buffer));
    *buf = bfy_buffer_init_with_block(block);
    return buf;
}

struct bfy_buffer*
bfy_buffer_new(void) {
    return bfy_buffer_new_with_block(bfy_block_init());
}

struct bfy_buffer*
bfy_buffer_new_unowned(void* data, size_t size) {
    return bfy_buffer_new_with_block(bfy_block_init_unowned(data, size));
}

void
bfy_buffer_free(bfy_buffer* buf) {
    bfy_buffer_destruct(buf);
    free(buf);
}

///

void
bfy_buffer_clear(bfy_buffer* buf) {
    buf->write_pos = 0;
}

bfy_block
bfy_buffer_take(bfy_buffer* buf) {
    bfy_block ret = buf->block;
    *buf = bfy_buffer_init();
    return ret;
}

int
bfy_buffer_take_string(bfy_buffer* buf, char** str, size_t* strsize) {
    int ret = 0;

    if (strsize != NULL) {
        *strsize = bfy_buffer_get_readable_size(buf);
    }

    if ((bfy_buffer_get_writable_size(buf) < 1) || *((char*)bfy_buffer_end(buf)) != '\0') {
        if (bfy_buffer_add_ch(buf, '\0')) {
            errno = ENOMEM;
            ret = -1;
        }
    }

    *str = bfy_buffer_begin(buf);
    *buf = bfy_buffer_init();
    return ret;
}


size_t
bfy_buffer_get_writable_size(bfy_buffer const* buf) {
    return buf->block.size - bfy_buffer_get_readable_size(buf);
}
size_t
bfy_buffer_get_readable_size(bfy_buffer const* buf) {
    return buf->write_pos;
}


const void*
bfy_buffer_cbegin(bfy_buffer const* buf) {
    return buf->block.data;
}
const void*
bfy_buffer_cend(bfy_buffer const* buf) {
    return bfy_buffer_cbegin(buf) + bfy_buffer_get_readable_size(buf);
}


void*
bfy_buffer_begin(bfy_buffer * buf) {
    return buf->block.data;
}
void*
bfy_buffer_end(bfy_buffer* buf) {
    return bfy_buffer_begin(buf) + bfy_buffer_get_readable_size(buf);
}


int
bfy_buffer_reserve(bfy_buffer* buf, size_t size) {
    bool const have_enough = buf->block.size >= size;
    return have_enough ? 0 : bfy_block_resize(&buf->block, size);
}

int
bfy_buffer_reserve_available(bfy_buffer* buf, size_t size) {
    return bfy_buffer_reserve(buf, bfy_buffer_get_readable_size(buf) + size);
}

int
bfy_buffer_add(bfy_buffer* buf, void const* addme, size_t size) {
    if (bfy_buffer_reserve_available(buf, size) == -1) {
        return -1;
    }

    memcpy(bfy_buffer_end(buf), addme, size);
    buf->write_pos += size;
    assert(buf->write_pos <= buf->block.size);
    return 0;
}

int
bfy_buffer_add_ch(bfy_buffer* buf, char ch) {
    return bfy_buffer_add(buf, &ch, 1);
}

int
bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int const ret = bfy_buffer_add_vprintf(buf, fmt, args);
    va_end(args);
    return ret;
}

int
bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in) {
    for(;;) {
        va_list args;
        va_copy(args, args_in);
        int const available = bfy_buffer_get_writable_size(buf);
        int const n = vsnprintf(bfy_buffer_end(buf), available, fmt, args);
        va_end(args);

        if (n < available) {
            buf->write_pos += n;
            return 0;
        }

        if (bfy_buffer_reserve_available(buf, n+1) == -1) { // +1 for trailing '\0'
            return -1;
        }
    }
}
