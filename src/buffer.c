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
#include <stdlib.h>  // malloc(), realloc(), free()

// block utilities

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
        fprintf(stderr, "free block %p data %p\n", block, block->data);
        if (block->data != NULL) {
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
        size_t const n_this_block = n_readable < n_left ? n_readable : n_left;
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
        ++buf->n_blocks;
        buf->blocks = realloc(buf->blocks, sizeof(struct bfy_block) * buf->n_blocks);
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
bfy_buffer_copyout(bfy_buffer* buf, void* tgt, size_t n_wanted) {
    size_t const max_size = bfy_buffer_get_readable_size(buf);
    if (n_wanted > max_size) {
        n_wanted = max_size;
    }

    int const n_vecs = bfy_buffer_peek(buf, n_wanted, NULL, 0);
    struct bfy_iovec* vecs = calloc(n_vecs, sizeof(struct bfy_iovec));
    bfy_buffer_peek(buf, n_wanted, vecs, n_vecs);

    char* tgt_it = tgt;
    char* tgt_end = tgt_it + n_wanted;
    struct bfy_iovec* iov_it = vecs;
    struct bfy_iovec* iov_end = iov_it + n_vecs;

    while(iov_it != iov_end) {
        memcpy(tgt_it, iov_it->iov_base, iov_it->iov_len);
        tgt_it += iov_it->iov_len;
        ++iov_it;
    }

    assert(tgt_it == tgt_end);
    assert(iov_it == iov_end);

    free(vecs);
    return tgt_it - (char*)tgt;
}

void*
bfy_buffer_make_all_contiguous(bfy_buffer* buf) {
    return bfy_buffer_make_contiguous(buf, SIZE_MAX);
}

static void
drain_first_block(bfy_buffer* buf) {
    if (buf->blocks == NULL) {
        buf->block = InitBlock;
    } else {
        impl_block_release(buf->blocks);
        memmove(buf->blocks, buf->blocks+1, sizeof(struct bfy_block) * --buf->n_blocks);
    }
}

void*
bfy_buffer_make_contiguous(bfy_buffer* buf, size_t n_wanted) { 
    size_t const max_size = bfy_buffer_get_readable_size(buf);
    if (n_wanted > max_size) {
        n_wanted = max_size;
    }

    // if the first block already holds n_wanted, then we're done
    struct bfy_block* block = blocks_begin(buf);
    if (n_wanted <= block_get_readable_size(block)) {
        return block_read_begin(block);
    }

    // FIXME: if block->size >= n_wanted then do the copies there

    int8_t* data = malloc(n_wanted);
    int8_t* data_it = data;

    size_t n_remaining = n_wanted;
    while (n_remaining > 0) {
        block = blocks_begin(buf);
        if (block == blocks_cend(buf)) {
            break;
        }

        int8_t const* read_cbegin = block_read_cbegin(block);
        int8_t const* read_cend = block_read_cend(block);
        size_t const n_readable = read_cend - read_cbegin;
        if (n_readable == 0) {
            break;
        }

        size_t const n_this_pass = n_remaining < n_readable ? n_remaining : n_readable;
        memcpy(data_it, read_cbegin, n_this_pass);
        data_it += n_this_pass;
        block->read_pos += n_this_pass;
        n_remaining -= n_this_pass;

        size_t const readable_left = block_get_readable_size(block);
        if (readable_left > 0) {
            break;
        }

        drain_first_block(buf);
    }

    assert(data + n_wanted == data_it);

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

    assert(bfy_buffer_get_readable_size(buf) == max_size);

    return block_read_begin(blocks_begin(buf));
}

#if 0




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

#endif
