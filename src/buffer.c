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

static struct bfy_block const InitBlock = { 0 };

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
static bool
block_is_recyclable(struct bfy_block const* block) {
    return block_is_writable(block);
}
static bool
block_has_readable(struct bfy_block const* block) {
    return block->write_pos > block->read_pos;
}

static void*
block_read_begin(struct bfy_block* block) {
    return block->data + block->read_pos;
}
static const void*
block_read_cbegin(struct bfy_block const* block) {
    return block->data + block->read_pos;
}

static size_t
block_get_content_len(struct bfy_block const* block) {
    return block->write_pos - block->read_pos;
}
static size_t
block_get_space_len(struct bfy_block const* block) {
    return block->size - block->write_pos;
}

static bool
block_realloc(struct bfy_block* block, size_t requested) {
    assert(block_can_realloc(block));

    // maybe free the memory
    if (requested == 0) {
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

    void* new_data = realloc(block->data, new_size);
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
block_ensure_space_len(struct bfy_block* block, size_t wanted) {
    size_t const space = block_get_space_len(block);
    return (wanted <= space) || (block_can_realloc(block) && block_realloc(block, block->size + (wanted - space)));
}

static void
block_release(struct bfy_block* block) {
    if (block->unref_cb != NULL) {
        block->unref_cb(block->data, block->size, block->unref_arg);
    }
    if (block_can_realloc(block)) {
        block_realloc(block, 0);
    }
    *block = InitBlock;
}

/// bfy_buffer life cycle

bfy_buffer
bfy_buffer_init(void) {
    bfy_buffer const buf = { 0 };
    return buf;
}

bfy_buffer*
bfy_buffer_new(void) {
    bfy_buffer* buf = malloc(sizeof(bfy_buffer));
    *buf = bfy_buffer_init();
    return buf;
}

bfy_buffer
bfy_buffer_init_unmanaged(void* data, size_t len) {
    bfy_buffer const buf = {
        .block = {
            .data = data,
            .size = len,
            .flags = BFY_BLOCK_FLAGS_UNMANAGED
        }
    };
    return buf;
}

bfy_buffer*
bfy_buffer_new_unmanaged(void* data, size_t len) {
    bfy_buffer* buf = bfy_buffer_new();
    *buf = bfy_buffer_init_unmanaged(data, len);
    return buf;
}

static size_t
buffer_count_blocks(bfy_buffer const* buf) {
    return blocks_cend(buf) - blocks_cbegin(buf);
}

static void
buffer_release_first_n_blocks(bfy_buffer* buf, size_t n) {
    n = size_t_min(n, buffer_count_blocks(buf));
    for (struct bfy_block *it=blocks_begin(buf), *const end=it+n; it!=end; ++it) {
        block_release(it);
    }
}

void
bfy_buffer_destruct(bfy_buffer* buf) {
    buffer_release_first_n_blocks(buf, SIZE_MAX);
    free(buf->blocks);
    buf->blocks = NULL;
    buf->n_blocks = 0;
}

void
bfy_buffer_free(bfy_buffer* buf) {
    bfy_buffer_destruct(buf);
    free(buf);
}

/// some simple getters

static struct bfy_pos
buffer_get_pos(bfy_buffer const* buf, size_t wanted) {
    struct bfy_pos ret = { 0 };

    struct bfy_block const* const begin = blocks_cbegin(buf);
    struct bfy_block const* const end = blocks_cend(buf);
    struct bfy_block const* it;
    for (it = begin; it != end; ++it) {
        size_t const block_len = block_get_content_len(it);
        size_t const got = size_t_min(block_len, wanted);
        if (got == 0) {
            break;
        }
        ret.content_pos += got;
        wanted -= got;
        if ((wanted == 0) && (block_len > got)) {
            ret.block_pos = got;
            break;
        }
    }

    ret.block_idx = it - begin;
    return ret;
}

size_t
bfy_buffer_get_content_len(bfy_buffer const* buf) {
    return buffer_get_pos(buf, SIZE_MAX).content_pos;
}

size_t
bfy_buffer_get_space_len(bfy_buffer const* buf) {
    return block_get_space_len(blocks_cback(buf));
}

/// readers

size_t
bfy_buffer_peek_all(bfy_buffer const* buf,
                    struct bfy_iovec* vec_out, size_t n_vec) {
    return bfy_buffer_peek(buf, SIZE_MAX, vec_out, n_vec);
}

static struct bfy_iovec
block_peek_content(struct bfy_block const* block, size_t n) {
    struct bfy_iovec vec = {
        .iov_base = (void*) block_read_cbegin(block),
        .iov_len = size_t_min(n, block_get_content_len(block))
    };
    return vec;
}

size_t
bfy_buffer_peek(bfy_buffer const* buf, size_t wanted,
                struct bfy_iovec* vec, size_t n_vec) {
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);
    size_t const needed = pos.block_idx + (pos.block_pos ? 1 : 0);

    struct bfy_iovec const* const vec_end = vec + n_vec;
    struct bfy_block const* it = blocks_cbegin(buf);
    struct bfy_block const* end = it + size_t_min(n_vec, pos.block_idx);
    for ( ; it != end; ++it) {
        *vec++ = block_peek_content(it, SIZE_MAX);
    }
    if ((pos.block_pos > 0) && (vec < vec_end)) {
        *vec++ = block_peek_content(it, pos.block_pos);
    }
    return needed;
}

/// adding blocks

static bool
buf_has_readable(bfy_buffer const* buf) {
    return block_has_readable(blocks_cbegin(buf));
}

static bool
buffer_insert_blocks(bfy_buffer* buf, size_t pos,
                     struct bfy_block const* new_blocks,
                     size_t n_new_blocks) {
    if (n_new_blocks > 0) {
        struct bfy_block const* old_blocks = blocks_cbegin(buf);
        size_t const n_old_blocks = buf_has_readable(buf)
                                  ? blocks_cend(buf) - old_blocks
                                  : 0;
        pos = size_t_min(pos, n_old_blocks);
        const size_t blocksize = sizeof(struct bfy_block);
        struct bfy_block* const blocks = malloc(blocksize * (n_old_blocks + n_new_blocks));
        if (blocks == NULL) {
            return false;
        }
        memcpy(blocks, old_blocks, blocksize * pos);
        memcpy(blocks + pos, new_blocks, blocksize * n_new_blocks);
        memcpy(blocks + pos + n_new_blocks, old_blocks + pos, blocksize * (n_old_blocks - pos));
        free(buf->blocks);
        buf->blocks = blocks;
        buf->n_blocks = n_old_blocks + n_new_blocks;
    }
    return true;
}

static bool
buffer_append_blocks(bfy_buffer* tgt,
                     struct bfy_block const* new_blocks,
                     size_t n_new_blocks) {
    return buffer_insert_blocks(tgt,
            blocks_cend(tgt)-blocks_begin(tgt),
            new_blocks, n_new_blocks);
}
static bool
buffer_prepend_blocks(bfy_buffer* tgt,
                      struct bfy_block const* new_blocks,
                      size_t n_new_blocks) {
    return buffer_insert_blocks(tgt, 0, new_blocks, n_new_blocks);
}

/// removing blocks

void
bfy_buffer_reset(bfy_buffer* buf) {
    struct bfy_block recycle_me = InitBlock;

    struct bfy_block* it = blocks_begin(buf);
    struct bfy_block const* const end = blocks_cend(buf);
    for ( ; it != end; ++it) {
        if (block_is_recyclable(it) && it->size > recycle_me.size) {
            block_release(&recycle_me);
            recycle_me = *it;
            recycle_me.read_pos = recycle_me.write_pos = 0;
        } else {
            block_release(it);
        }
    }

    free(buf->blocks);
    buf->blocks = NULL;
    buf->n_blocks = 0;

    buf->block = recycle_me;
}

static void
buffer_forget_first_n_blocks(bfy_buffer* buf, size_t len) {
    struct bfy_block* const begin = blocks_begin(buf);

    for (struct bfy_block* it=begin, *const end=it + len; it != end; ++it) {
        *it = InitBlock;
    }

    if (buf->blocks != NULL) {
        struct bfy_block const* const end = blocks_cend(buf);
        if (begin + len < end) {
            const size_t blocksize = sizeof(struct bfy_block);
            memmove(begin, begin + len, blocksize * (end - begin - len));
            buf->n_blocks -= len;
        } else {
            buf->n_blocks = 1;
            buf->blocks[0] = InitBlock;
        }
    }
}

static void
buffer_drain_first_n_blocks(bfy_buffer* buf, size_t len) {
    size_t const total_blocks = buffer_count_blocks(buf);
    len = size_t_min(len, total_blocks);
    if (len == total_blocks) {
        bfy_buffer_reset(buf);
        return;
    }

    buffer_release_first_n_blocks(buf, len);
    buffer_forget_first_n_blocks(buf, len);
}

///

typedef bool (block_test)(struct bfy_block const* block);

static struct bfy_block*
buffer_get_usable_back(bfy_buffer* buf, block_test* test) {
    struct bfy_block* block = blocks_back(buf);
    if (test(block)) {
        return block;
    }
    if (!buffer_append_blocks(buf, &InitBlock, 1)) {
        return NULL;
    }
    return blocks_back(buf);
}

static struct bfy_block*
buffer_get_writable_back(bfy_buffer* buf) {
    return buffer_get_usable_back(buf, block_is_writable);
}

/// space

bool
bfy_buffer_add_chain(struct bfy_buffer* buf) {
    struct bfy_block block = InitBlock;
    return buffer_append_blocks(buf, &block, 1);
}

static struct bfy_iovec
block_peek_space(struct bfy_block const* block) {
    struct bfy_iovec vec = {
        .iov_base = (void*) (block->data + block->write_pos),
        .iov_len = block_get_space_len(block)
    };
    return vec;
}

struct bfy_iovec
bfy_buffer_peek_space(struct bfy_buffer* buf) {
    return block_peek_space(blocks_back(buf));
}

struct bfy_iovec
bfy_buffer_reserve_space(struct bfy_buffer* buf, size_t wanted) {
    bfy_buffer_ensure_space(buf, wanted);
    return bfy_buffer_peek_space(buf);
}

size_t
bfy_buffer_commit_space(struct bfy_buffer* buf, size_t len) {
    size_t n_committed = 0;

    struct bfy_block* block = buffer_get_writable_back(buf);
    if (block != NULL) {
        size_t const n_writable = block_get_space_len(block);
        assert (len <= n_writable);
        len = size_t_min(len, n_writable);
        block->write_pos += len;
        n_committed = len;
    }

    return n_committed;
}

/// adders

bool
bfy_buffer_add_readonly(bfy_buffer* buf,
        const void* data, size_t len) {
    struct bfy_block const block = {
        .data = (void*) data,
        .size = len,
        .read_pos = 0,
        .write_pos = len,
        .flags = BFY_BLOCK_FLAGS_READONLY | BFY_BLOCK_FLAGS_UNMANAGED
    };
    return buffer_append_blocks(buf, &block, 1);
}

bool bfy_buffer_add_reference(bfy_buffer* buf,
        const void* data, size_t len, 
        bfy_unref_cb* cb, void* unref_arg) {
    struct bfy_block const block = {
        .data = (void*) data,
        .size = len,
        .read_pos = 0,
        .write_pos = len,
        .flags = BFY_BLOCK_FLAGS_READONLY,
        .unref_cb = cb,
        .unref_arg = unref_arg
    };
    return buffer_append_blocks(buf, &block, 1);
}

bool
bfy_buffer_add(bfy_buffer* buf, const void* data, size_t len) {
    struct bfy_iovec const io = bfy_buffer_reserve_space(buf, len);
    if (data == NULL || io.iov_base == NULL || io.iov_len < len) {
        return false;
    }
    memcpy(io.iov_base, data, len);
    bfy_buffer_commit_space(buf, len);
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
bfy_buffer_ensure_space(bfy_buffer* buf, size_t len) {
    struct bfy_block* block = blocks_back(buf);
    if (block_get_space_len(block) >= len) {
        return true;
    }
    block = buffer_get_usable_back(buf, block_can_realloc);
    return (block != NULL) && block_ensure_space_len(block, len);
}

bool
bfy_buffer_expand(bfy_buffer* buf, size_t len) {
    return bfy_buffer_ensure_space(buf, len);
}

bool
bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in) {
    // see if we can print it into the free space
    struct bfy_iovec io = bfy_buffer_peek_space(buf);
    va_list args;
    va_copy(args, args_in);
    size_t n = vsnprintf(io.iov_base, io.iov_len, fmt, args);
    size_t const space_wanted = n + 1; // +1 for trailing '\0'
    va_end(args);

    // if not, make some more free space and try again
    if (space_wanted > io.iov_len) {
        io = bfy_buffer_reserve_space(buf, space_wanted);
        va_copy(args, args_in);
        n = vsnprintf(io.iov_base, io.iov_len, fmt, args);
        va_end(args);
    }

    if (n < io.iov_len) {
        bfy_buffer_commit_space(buf, n);
    }
    return n < io.iov_len;
}

///

static size_t
block_drain(struct bfy_block* block, size_t n) {
    n = size_t_min(n, block_get_content_len(block));
    block->read_pos += n;
    return n;
}

bool
bfy_buffer_drain(bfy_buffer* buf, size_t wanted) {
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);
    if (pos.block_idx > 0) {
        buffer_drain_first_n_blocks(buf, pos.block_idx);
    }
    if (pos.block_pos > 0) {
        block_drain(blocks_begin(buf), pos.block_pos);
    }
    return true;
}

///

static size_t
block_copyout(struct bfy_block const* block, void* data, size_t wanted) {
    struct bfy_iovec const vec = block_peek_content(block, wanted);
    memcpy(data, vec.iov_base, vec.iov_len);
    return vec.iov_len;
}

size_t
bfy_buffer_copyout(bfy_buffer const* buf, void* vdata, size_t wanted) {
    int8_t* data = vdata;
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);
    struct bfy_block const* it = blocks_cbegin(buf);
    struct bfy_block const* const end = it + pos.block_idx;
    for ( ; it != end; ++it) {
        data += block_copyout(it, data, SIZE_MAX);
    }
    if (pos.block_pos > 0) {
        data += block_copyout(it, data, pos.block_pos);
    }
    assert ((data - (int8_t*)vdata) == pos.content_pos);
    return data - (int8_t*)vdata;
}

static size_t
block_remove(struct bfy_block* block, void* data, size_t wanted) {
    size_t const n_copied = block_copyout(block, data, wanted);
    block->read_pos += n_copied;
    return n_copied;
}

static size_t
buffer_remove(bfy_buffer* buf, void* vdata, struct bfy_pos stop) {
    int8_t* data = vdata;
    struct bfy_block* it = blocks_begin(buf);
    for (int i=0; i<stop.block_idx; ++i, ++it) {
        data += block_remove(it, data, SIZE_MAX);
    }
    if (stop.block_pos > 0) {
        data += block_remove(it, data, stop.block_pos);
    }
    buffer_drain_first_n_blocks(buf, stop.block_idx);
    return data - (int8_t*)vdata;
}

size_t
bfy_buffer_remove(bfy_buffer* buf, void* data, size_t wanted) {
    return buffer_remove(buf, data, buffer_get_pos(buf, wanted));
}

char*
bfy_buffer_remove_string(bfy_buffer* buf, size_t* setme_len) {
    struct bfy_pos const pos = buffer_get_pos(buf, SIZE_MAX);
    char* ret = malloc(pos.content_pos + 1);  // +1 for '\0'
    size_t const moved_len = buffer_remove(buf, ret, pos);
    assert(moved_len == pos.content_pos);
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
bfy_buffer_make_contiguous(bfy_buffer* buf, size_t wanted) {
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);

    // if the first block already holds wanted, then we're done
    if ((pos.block_idx == 0) || (pos.block_idx == 1 && pos.block_pos == 0)) {
        return block_read_begin(blocks_begin(buf));
    }

    // FIXME: if block->size >= wanted then do the copies there

    // build a contiguous block
    int8_t* data = malloc(pos.content_pos);
    size_t const n_moved = buffer_remove(buf, data, pos);

    // now prepend a new block with the contiguous memory
    struct bfy_block const newblock = {
        .data = data,
        .size = n_moved,
        .write_pos = n_moved
    };
    if (!buffer_prepend_blocks(buf, &newblock, 1)) {
        free(data);
        return false;
    }

    return block_read_begin(blocks_begin(buf));
}

bool
bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* src) {
    return bfy_buffer_remove_buffer(src, buf, SIZE_MAX);
}

bool
bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* tgt, size_t wanted) {
    // see how many blocks get moved as-is
    // and how many bytes will be left if we need to move half-a-block
    struct bfy_pos pos = buffer_get_pos(buf, wanted);

    // are there any blocks that get moved over completely?
    if (pos.block_idx > 0) {
        buffer_append_blocks(tgt, blocks_cbegin(buf), pos.block_idx);
        buffer_forget_first_n_blocks(buf, pos.block_idx);
    }

    // are there any remainder bytes to move over in a new block?
    if (pos.block_pos > 0) {
        struct bfy_block* block = blocks_begin(buf);
        bfy_buffer_add(tgt, block_read_cbegin(block), pos.block_pos);
        block_drain(block, pos.block_pos);
    }

    return true;
}

#include "portable-endian.h"

bool
bfy_buffer_add_hton_u8(struct bfy_buffer* buf, uint8_t addme) {
    return bfy_buffer_add(buf, &addme, 1);
}

bool
bfy_buffer_add_hton_u16(struct bfy_buffer* buf, uint16_t addme) {
    uint16_t const be = htobe16(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

bool
bfy_buffer_add_hton_u32(struct bfy_buffer* buf, uint32_t addme) {
    uint32_t const be = htobe32(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

bool
bfy_buffer_add_hton_u64(struct bfy_buffer* buf, uint64_t addme) {
    uint64_t const be = htobe64(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

bool
bfy_buffer_remove_ntoh_u8(struct bfy_buffer* buf, uint8_t* setme) {
    return bfy_buffer_remove(buf, setme, sizeof(uint8_t));
}

#define REMOVE_NTOH_N(size) \
bool \
bfy_buffer_remove_ntoh_u##size(struct bfy_buffer* buf, uint##size##_t* setme) { \
    uint##size##_t tmp; \
    bool success; \
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) { \
        *setme = be##size##toh(tmp); \
    } \
    return success; \
}

REMOVE_NTOH_N(16)
REMOVE_NTOH_N(32)
REMOVE_NTOH_N(64)

#if 0
bool
bfy_buffer_remove_ntoh_16(struct bfy_buffer* buf, uint16_t* setme) {
    uint16_t tmp;
    bool success;
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) {
        *setme = be16toh(tmp);
    }
    return success;
}

bool
bfy_buffer_remove_ntoh_32(struct bfy_buffer* buf, uint32_t* setme) {
    uint32_t tmp;
    bool success;
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) {
        *setme = be32toh(tmp);
    }
    return success;
}

bool
bfy_buffer_remove_ntoh_64(struct bfy_buffer* buf, uint64_t* setme) {
    uint64_t tmp;
    bool success;
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) {
        *setme = be64toh(tmp);
    }
    return success;
}
#endif
