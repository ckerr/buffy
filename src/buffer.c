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
    bfy_buffer* buf = bfy_buffer_new();
    *buf = bfy_buffer_init_unmanaged(data, size);
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

size_t
bfy_buffer_get_content_len(bfy_buffer const* buf) {
    size_t size = 0;
    for (struct bfy_block const* it = blocks_cbegin(buf), *end = blocks_cend(buf); it != end; ++it) {
        size += block_get_content_len(it);
    }
    return size;
}

size_t
bfy_buffer_get_space_len(bfy_buffer const* buf) {
    return block_get_space_len(blocks_cback(buf));
}

/// readers

size_t
bfy_buffer_peek_all(bfy_buffer const* buf, struct bfy_iovec* vec_out, size_t n_vec) {
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
bfy_buffer_peek(bfy_buffer const* buf, size_t len_wanted, struct bfy_iovec* vec_out, size_t n_vec) {
    size_t n_left = len_wanted;
    struct bfy_iovec const* const vec_end = vec_out + n_vec;

    struct bfy_block const* it = blocks_cbegin(buf);
    struct bfy_block const* const end = blocks_cend(buf);
    while ((it != end) && (n_left > 0)) {
        struct bfy_iovec const vec = block_peek_content(it, n_left);
        if (vec.iov_len == 0) {
            break;
        }
        if (vec_out != vec_end) {
            *vec_out++ = vec;
        }
        n_left -= vec.iov_len;
        ++it;
    }

    return it - blocks_cbegin(buf);
}

/// adding blocks

static bool
buf_has_readable(bfy_buffer const* buf) {
    return block_has_readable(blocks_cbegin(buf));
}

static bool
buffer_insert_blocks(bfy_buffer* buf,
        size_t pos,
        struct bfy_block const* new_blocks, size_t n_new_blocks) {
    if (n_new_blocks > 0) {
        struct bfy_block const* old_blocks = blocks_cbegin(buf);
        size_t const n_old_blocks = buf_has_readable(buf) ? blocks_cend(buf) - old_blocks : 0;
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
buffer_append_blocks(bfy_buffer* tgt, struct bfy_block const* new_blocks, size_t n_new_blocks) {
    return buffer_insert_blocks(tgt, blocks_cend(tgt)-blocks_begin(tgt), new_blocks, n_new_blocks);
}
static bool
buffer_prepend_blocks(bfy_buffer* tgt, struct bfy_block const* new_blocks, size_t n_new_blocks) {
    return buffer_insert_blocks(tgt, 0, new_blocks, n_new_blocks);
}

/// removing blocks

void
bfy_buffer_reset(bfy_buffer* buf) {
    struct bfy_block recycle_me = InitBlock;
    for (struct bfy_block *it=blocks_begin(buf), *end=blocks_end(buf); it != end; ++it) {
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
    // fprintf(stderr, "after reset/recycle, %zu content %zu space\n", bfy_buffer_get_content_len(buf), bfy_buffer_get_space_len(buf));
}

static void
buffer_forget_first_n_blocks(bfy_buffer* buf, size_t n) {
    struct bfy_block* const begin = blocks_begin(buf);

    for (struct bfy_block* it=begin, *const end=it+n; it != end; ++it) {
        *it = InitBlock;
    }

    if (buf->blocks != NULL) {
        struct bfy_block const* const end = blocks_cend(buf);
        if (begin + n < end) {
            const size_t blocksize = sizeof(struct bfy_block);
            memmove(begin, begin + n, blocksize * (end-begin-n));
            buf->n_blocks -= n;
        } else {
            buf->n_blocks = 1;
            buf->blocks[0] = InitBlock;
        }
    }
}

static void
buffer_drain_first_n_blocks(bfy_buffer* buf, size_t n) {
    size_t const total_blocks = buffer_count_blocks(buf);
    n = size_t_min(n, total_blocks);
    if (n == total_blocks) {
        bfy_buffer_reset(buf);
        return;
    }

    buffer_release_first_n_blocks(buf, n);
    buffer_forget_first_n_blocks(buf, n);
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
bfy_buffer_reserve_space(struct bfy_buffer* buf, size_t n_wanted) {
    bfy_buffer_ensure_space(buf, n_wanted);
    return bfy_buffer_peek_space(buf);
}

size_t
bfy_buffer_commit_space(struct bfy_buffer* buf, size_t size) {
    size_t n_committed = 0;

    struct bfy_block* block = buffer_get_writable_back(buf);
    if (block != NULL) {
        size_t const n_writable = block_get_space_len(block);
        assert (size <= n_writable);
        size = size_t_min(size, n_writable);
        block->write_pos += size;
        n_committed = size;
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
bfy_buffer_add(bfy_buffer* buf, const void* data, size_t size) {
    struct bfy_iovec const io = bfy_buffer_reserve_space(buf, size);
    if (data == NULL || io.iov_base == NULL || io.iov_len < size) {
        return false;
    }
    memcpy(io.iov_base, data, size);
    bfy_buffer_commit_space(buf, size);
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
bfy_buffer_ensure_space(bfy_buffer* buf, size_t size) {
    struct bfy_block* block = blocks_back(buf);
    if (block_get_space_len(block) >= size) {
        return true;
    }
    block = buffer_get_usable_back(buf, block_can_realloc);
    return (block != NULL) && block_ensure_space_len(block, size);
}

// evbuffer-like naming
bool
bfy_buffer_expand(bfy_buffer* buf, size_t size) {
    return bfy_buffer_ensure_space(buf, size);
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
    struct bfy_iovec const vec = block_peek_content(block, n);
    block->read_pos += vec.iov_len;
    return vec.iov_len;
}

bool
bfy_buffer_drain(bfy_buffer* buf, size_t len_wanted) {
    size_t n_left = len_wanted;

    struct bfy_block* it = blocks_begin(buf);
    struct bfy_block const* end = blocks_cend(buf);
    while(it != end) {
        n_left -= block_drain(it, n_left);
        if (block_has_readable(it)) {
            break;
        }
        ++it;
    }

    buffer_drain_first_n_blocks(buf, it - blocks_cbegin(buf));
    return true;
}

///

static size_t
block_copyout(struct bfy_block const* block, void* data, size_t n_wanted) {
    struct bfy_iovec const vec = block_peek_content(block, n_wanted);
    memcpy(data, vec.iov_base, vec.iov_len);
    return vec.iov_len;
}

size_t
bfy_buffer_copyout(bfy_buffer const* buf, void* vdata, size_t n_wanted) {
    int8_t* data = vdata;
    size_t n_left = n_wanted;
    struct bfy_block const* const end = blocks_cend(buf);
    for (struct bfy_block const* it = blocks_cbegin(buf); it != end; ++it) {
        size_t const n_this_block = block_copyout(it, data, n_left);
        n_left -= n_this_block;
        data += n_this_block;
        if (block_get_content_len(it) > n_this_block) {
            break;
        }
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

    struct bfy_block* it = blocks_begin(buf);
    struct bfy_block const* const end = blocks_cend(buf);
    for (; it != end; ++it) {
        size_t const n_from_block = block_remove(it, data, n_left);
        n_left -= n_from_block;
        data += n_from_block;
        if (block_has_readable(it)) {
            break;
        }
    }

    buffer_drain_first_n_blocks(buf, it - blocks_cbegin(buf));
    return data - (int8_t*)vdata;
}

char*
bfy_buffer_remove_string(bfy_buffer* buf, size_t* setme_len) {
    size_t const len = bfy_buffer_get_content_len(buf);
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

static size_t
buf_limit_content_len(bfy_buffer const* buf, size_t len) {
    return size_t_min(len, bfy_buffer_get_content_len(buf));
}

void*
bfy_buffer_make_contiguous(bfy_buffer* buf, size_t n_wanted) {
    n_wanted = buf_limit_content_len(buf, n_wanted);

    // if the first block already holds n_wanted, then we're done
    struct bfy_block* block = blocks_begin(buf);
    if (n_wanted <= block_get_content_len(block)) {
        return block_read_begin(block);
    }

    // FIXME: if block->size >= n_wanted then do the copies there

    // build a contiguous block
    int8_t* data = malloc(n_wanted);
    size_t const n_moved = bfy_buffer_remove(buf, data, n_wanted);

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
bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* tgt, size_t n_bytes_wanted)
{
    size_t n_left = n_bytes_wanted;

    // see how many blocks get moved as-is
    // and how many bytes will be left if we need to move half-a-block
    size_t n_blocks_moved = 0;
    for (struct bfy_block const* it = blocks_cbegin(buf), *end = blocks_cend(buf); it != end; ++it) {
        size_t const n_readable = block_get_content_len(it);
        if (n_left < n_readable) {
            break;
        }
        n_left -= n_readable;
        ++n_blocks_moved;
    }

    // are there any blocks that get moved over completely?
    if (n_blocks_moved > 0) {
        buffer_append_blocks(tgt, blocks_cbegin(buf), n_blocks_moved);
        buffer_forget_first_n_blocks(buf, n_blocks_moved);
    }

    // are there any remainder bytes to move over in a new block?
    if ((n_left > 0) && buf_has_readable(buf)) {
        struct bfy_block* block = blocks_begin(buf);
        bfy_buffer_add(tgt, block_read_cbegin(block), n_left);
        block_drain(block, n_left);
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
