/*
 * Copyright 2020 Mnemosyne LLC
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <libbuffy/buffer.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>  // vsprintf()
#include <string.h>  // memcpy()
#include <stdlib.h>  // malloc(), realloc(), free()

static size_t
size_t_min(size_t a, size_t b) {
    return a < b ? a : b;
}

/// block utils

static struct bfy_block InitBlock = { 0 };

static struct bfy_block*
blocks_begin(bfy_buffer* buf) {
    return buf->blocks == NULL ? &buf->block : buf->blocks;
}
static struct bfy_block const*
blocks_cbegin(bfy_buffer const* buf) {
    return buf->blocks == NULL ? &buf->block : buf->blocks;
}
static struct bfy_block*
blocks_end(bfy_buffer* buf) {
    return buf->blocks == NULL ? &buf->block + 1 : buf->blocks + buf->n_blocks;
}
static struct bfy_block const*
blocks_cend(bfy_buffer const* buf) {
    return buf->blocks == NULL ? &buf->block + 1 : buf->blocks + buf->n_blocks;
}
static struct bfy_block*
blocks_back(bfy_buffer* buf) {
    return buf->blocks == NULL ? &buf->block : buf->blocks + buf->n_blocks - 1;
}
static struct bfy_block const*
blocks_cback(bfy_buffer const* buf) {
    return buf->blocks == NULL ? &buf->block : buf->blocks + buf->n_blocks - 1;
}
static bool
block_can_realloc(struct bfy_block const* block) {
    return (block->flags & (BFY_BLOCK_FLAGS_READONLY | BFY_BLOCK_FLAGS_UNMANAGED)) == 0;
}
static bool
block_is_writable(struct bfy_block const* block) {
    return (block->flags & BFY_BLOCK_FLAGS_READONLY) == 0;
}

static void*
block_read_begin(struct bfy_block* block) {
    return block->data + block->read_pos;
}
static void*
block_read_end(struct bfy_block* block) {
    return block->data + block->write_pos;
}
static void*
block_write_begin(struct bfy_block* block) {
    return block->data + block->write_pos;
}
static void*
block_write_end(struct bfy_block* block) {
    return block->data + block->size;
}
static const void*
block_read_cbegin(struct bfy_block const* block) {
    return block->data + block->read_pos;
}
static const void*
block_read_cend(struct bfy_block const* block) {
    return block->data + block->write_pos;
}
static const void*
block_write_cbegin(struct bfy_block const* block) {
    return block->data + block->write_pos;
}
static const void*
block_write_cend(struct bfy_block const* block) {
    return block->data + block->size;
}

static size_t
block_get_readable_size(struct bfy_block const* block) {
    return block_read_cend(block) - block_read_cbegin(block);
}
static size_t
block_get_writable_size(struct bfy_block const* block) {
    return block_write_cend(block) - block_write_cbegin(block);
}

static bool
block_realloc(struct bfy_block* block, size_t requested) {
    assert(block_can_realloc(block));

    // maybe free the memory
    if (requested == 0) {
        if (block->data != NULL) {
            fprintf(stderr, "free block %p data %p\n", block, block->data);
            free(block->data);
            block->data = NULL;
        }
        return true;
    }

    // decide on a new capacity
    size_t const Min = 1024;
    size_t new_size = Min;
    while (new_size < requested) {
        new_size *= 2u;
    }

    if (new_size <= block->size) {
        // FIXME: is there a use case for allowing shrinking?
        return true;
    }

    void* old_data = block->data;
    void* new_data = realloc(block->data, new_size);
    fprintf(stderr, "realloc %p -> %p %zu\n", old_data, new_data, new_size);
    if (new_data == NULL) {
        errno = ENOMEM;
        return false;
    } else {
        block->data = new_data;
        block->size = new_size;
        return true;
    }
}

static bool
block_ensure_writable_size(struct bfy_block* block, size_t desired) {
    size_t const cur = block_get_writable_size(block);
    return desired <= cur ? true : block_realloc(block, block->size + (desired - cur));
}

static void
impl_block_release(struct bfy_block* block) {
    if (block_can_realloc(block)) {
        block_realloc(block, 0);
    }
    *block = InitBlock;
}

/// bfy_buffer life cycle

bfy_buffer
bfy_buffer_init(void) {
    bfy_buffer buf = { 0 };
    return buf;
}

bfy_buffer*
bfy_buffer_new(void) {
    bfy_buffer* buf = malloc(sizeof(bfy_buffer));
    *buf = bfy_buffer_init();
    return buf;
}

bfy_buffer
bfy_buffer_init_unmanaged(void* data, size_t size) {
    bfy_buffer buf = {
        .block = {
            .data = data,
            .size = size,
            .flags = BFY_BLOCK_FLAGS_UNMANAGED
        }
    };
    return buf;
}

bfy_buffer*
bfy_buffer_new_unmanaged(void* data, size_t size) {
    bfy_buffer* buf = malloc(sizeof(bfy_buffer));
    *buf = bfy_buffer_init_unmanaged(data, size);
    return buf;
}


void
bfy_buffer_destruct(bfy_buffer* buf) {
    struct bfy_block* begin = blocks_begin(buf);
    struct bfy_block* end = blocks_end(buf);
    for (struct bfy_block* it=begin; it!=end; ++it) {
        impl_block_release(it);
    }
    if (buf->blocks != NULL) {
        free(buf->blocks);
    }
}

void
bfy_buffer_free(bfy_buffer* buf) {
    bfy_buffer_destruct(buf);
    free(buf);
}

/// some simple getters

size_t
bfy_buffer_get_readable_size(bfy_buffer const* buf) {
    size_t size = 0;

    struct bfy_block const* begin = blocks_cbegin(buf);
    struct bfy_block const* end = blocks_cend(buf);
    for (struct bfy_block const* it=begin; it!=end; ++it) {
        size += block_get_readable_size(it);
    }

    return size;
}

size_t
bfy_buffer_get_writable_size(bfy_buffer const* buf) {
    return block_get_writable_size(blocks_cback(buf));
}

/// readers

size_t
bfy_buffer_peek_all(bfy_buffer* buf, struct bfy_iovec* vec_out, size_t n_vec) {
    return bfy_buffer_peek(buf, SIZE_MAX, vec_out, n_vec);
}

size_t
bfy_buffer_peek(bfy_buffer* buf, size_t len_wanted, struct bfy_iovec* vec_out, size_t n_vec) {
    size_t vecs_needed = 0;

    size_t n_left = len_wanted;
    struct bfy_iovec* vec_end = vec_out + n_vec;

    struct bfy_block* begin = blocks_begin(buf);
    struct bfy_block* end = blocks_end(buf);
    for (struct bfy_block* it=begin; it!=end; ++it) {
        size_t const n_readable = block_get_readable_size(it);
        size_t const n_this_block = size_t_min(n_readable, n_left);
        if (n_this_block > 0) {
            if (vec_out != vec_end) {
                vec_out->iov_base = block_read_begin(it);
                vec_out->iov_len = n_this_block;
                ++vec_out;
            }
            ++vecs_needed;
            n_left -= n_this_block;
            if (n_left == 0) {
                break;
            }
        }
    }

    return vecs_needed;
}

/// adder helpers

static struct bfy_block*
impl_append_block(bfy_buffer* buf) {
    if (buf->blocks != NULL) {
        buf->blocks = realloc(buf->blocks, sizeof(struct bfy_block) * ++buf->n_blocks);
    } else if (buf->block.data != NULL) {
        buf->n_blocks = 2;
        buf->blocks = malloc(sizeof(struct bfy_block) * buf->n_blocks);
        buf->blocks[0] = buf->block;
        buf->block = InitBlock;
    }

    struct bfy_block* back = blocks_back(buf);
    *back = InitBlock;
    return back;
}

static struct bfy_block*
impl_get_writable_back(bfy_buffer* buf) {
    struct bfy_block* back = blocks_back(buf);
    return block_is_writable(back) ? back : impl_append_block(buf);
}

/// adders

bool
bfy_buffer_add_readonly(bfy_buffer* buf, const void* data, size_t size) {
    struct bfy_block* block = impl_append_block(buf);
    block->data = (void*)data;
    block->read_pos = 0;
    block->size = size;
    block->write_pos = size;
    block->flags |= BFY_BLOCK_FLAGS_READONLY;
    block->flags |= BFY_BLOCK_FLAGS_UNMANAGED;
    return true;
}

bool
bfy_buffer_add(bfy_buffer* buf, const void* data, size_t size) {
    struct bfy_block* back = impl_get_writable_back(buf);

    if (!block_ensure_writable_size(back, size)) {
        return false;
    }

    memcpy(block_write_begin(back), data, size);
    back->write_pos += size;
    return true;
}

bool
bfy_buffer_add_ch(bfy_buffer* buf, char ch) {
    return bfy_buffer_add(buf, &ch, 1);
}

bool
bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    bool const ret = bfy_buffer_add_vprintf(buf, fmt, args);
    va_end(args);
    return ret;
}

bool
bfy_buffer_ensure_writable_size(bfy_buffer* buf, size_t size) {
    struct bfy_block* block = blocks_back(buf);
    if (block_get_writable_size(block) >= size) {
        return true;
    }
    if (!block_can_realloc(block)) {
        block = impl_append_block(buf);
    }
    return block_ensure_writable_size(block, size);
}

// evbuffer-like naming
bool
bfy_buffer_expand(bfy_buffer* buf, size_t size) {
    return bfy_buffer_ensure_writable_size(buf, size);
}

bool
bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in) {
    struct bfy_block* back = impl_get_writable_back(buf);
    for(;;) {
        void* const begin = block_write_begin(back);
        void* const end = block_write_end(back);
        size_t const available = end - begin;

        va_list args;
        va_copy(args, args_in);
        int const n = vsnprintf(begin, available, fmt, args);
        va_end(args);

        if (n < available) {
            back->write_pos += n;
            return true;
        }

        if (!block_ensure_writable_size(back, n+1)) { // +1 for trailing '\0'
            return false;
        }
    }
}

///

static size_t
limit_readable_size(bfy_buffer const* buf, size_t len) {
    return size_t_min(len, bfy_buffer_get_readable_size(buf));
}

static void
drain_first_block(bfy_buffer* buf) {
    if (buf->blocks == NULL) {
        buf->block = InitBlock;
    } else {
        impl_block_release(buf->blocks);
        if (buf->n_blocks > 1) {
            memmove(buf->blocks, buf->blocks+1, sizeof(struct bfy_block) * --buf->n_blocks);
        }
    }
}

bool
bfy_buffer_drain(bfy_buffer* buf, size_t len) {
    size_t n_left = limit_readable_size(buf, len);

    for (;;) {
        struct bfy_block* block = blocks_begin(buf);
        size_t const n_this_block = block_get_readable_size(block);
        if (n_this_block == 0) {
            break;
        }
        if (n_left < n_this_block) {
            block->read_pos += n_left;
            break;
        }
        drain_first_block(buf);
        n_left -= n_this_block;
    }

    return true;
}

///

static size_t
block_copyout(struct bfy_block const* block, void* data, size_t n_wanted) {
    size_t const n_copied = size_t_min(n_wanted, block_get_readable_size(block));
    memcpy(data, block_read_cbegin(block), n_copied);
    return n_copied;
}

size_t
bfy_buffer_copyout(bfy_buffer const* buf, void* vdata, size_t n_wanted) {
    int8_t* data = vdata;
    size_t n_left = n_wanted;
    struct bfy_block const* const begin = blocks_cbegin(buf);
    struct bfy_block const* const end = blocks_cend(buf);
    for (struct bfy_block const* it = begin; it != end; ++it) {
        size_t const n_this_block = block_copyout(it, data, n_left);
        if (n_this_block == 0) {
            break;
        }
        data += n_this_block;
        n_left -= n_this_block;
    }
    return data - (int8_t*)vdata;
}

static size_t
block_remove(struct bfy_block* block, void* data, size_t n_wanted) {
    size_t const n_copied = block_copyout(block, data, n_wanted);
    block->read_pos += n_copied;
    return n_copied;
}

size_t
bfy_buffer_remove(bfy_buffer* buf, void* vdata, size_t n_wanted) {
    int8_t* data = vdata;
    size_t n_left = n_wanted;
    while (n_left > 0) {
        struct bfy_block* block = blocks_begin(buf);
        size_t const n_this_block = block_remove(block, data, n_left);
        data += n_this_block;
        n_left -= n_this_block;

        if (n_this_block == 0) {
            break;
        }
        if (block_get_readable_size(block) > 0) {
            break;
        }

        drain_first_block(buf);
    }
    return data - (int8_t*)vdata;
}

char*
bfy_buffer_remove_string(bfy_buffer* buf, size_t* setme_len) {
    size_t const len = bfy_buffer_get_readable_size(buf);
    char* ret = malloc(len + 1);  // +1 for '\0'
    size_t const moved_len = bfy_buffer_remove(buf, ret, len);
    assert(moved_len == len);
    ret[moved_len] = '\0';
    if (setme_len != NULL) {
        *setme_len = moved_len;
    }
    return ret;
}

void*
bfy_buffer_make_all_contiguous(bfy_buffer* buf) {
    return bfy_buffer_make_contiguous(buf, SIZE_MAX);
}

void*
bfy_buffer_make_contiguous(bfy_buffer* buf, size_t n_wanted) {
    n_wanted = limit_readable_size(buf, n_wanted);

    // if the first block already holds n_wanted, then we're done
    struct bfy_block* block = blocks_begin(buf);
    if (n_wanted <= block_get_readable_size(block)) {
        return block_read_begin(block);
    }

    // FIXME: if block->size >= n_wanted then do the copies there

    // build a contiguous block
    int8_t* data = malloc(n_wanted);
    size_t const n_moved = bfy_buffer_remove(buf, data, n_wanted);

    // now prepend a new block with the contiguous memory
    buf->blocks = realloc(buf->blocks, sizeof(struct bfy_block) * (buf->n_blocks + 1));
    memmove(buf->blocks + 1, buf->blocks, sizeof(struct bfy_block) * buf->n_blocks);
    struct bfy_block const newblock = {
        .data = data,
        .size = n_wanted,
        .write_pos = n_wanted
    };
    buf->blocks[0] = newblock;
    ++buf->n_blocks;

    return block_read_begin(blocks_begin(buf));
}
