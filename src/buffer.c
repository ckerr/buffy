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
static const void*
block_read_cbegin(struct bfy_block const* block) {
    return block->data + block->read_pos;
}

static size_t
block_get_readable_size(struct bfy_block const* block) {
    return block->write_pos - block->read_pos;
}
static size_t
block_get_writable_size(struct bfy_block const* block) {
    return block->size - block->write_pos;
}

static bool
block_realloc(struct bfy_block* block, size_t requested) {
    assert(block_can_realloc(block));

    // maybe free the memory
    if (requested == 0) {
        if (block->data != NULL) {
            fprintf(stderr, "free block %p data %p\n", (void*)block, (void*)block->data);
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
block_release(struct bfy_block* block) {
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
        block_release(it);
    }
    free(buf->blocks);
    buf->blocks = NULL;
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
        size += block_get_readable_size(it);
    }
    return size;
}

size_t
bfy_buffer_get_space_len(bfy_buffer const* buf) {
    return block_get_writable_size(blocks_cback(buf));
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
        .iov_len = size_t_min(n, block_get_readable_size(block))
    };
    return vec;
}

static struct bfy_iovec
block_peek_space(struct bfy_block const* block) {
    struct bfy_iovec vec = {
        .iov_base = (void*) (block->data + block->write_pos),
        .iov_len = block_get_writable_size(block)
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

/// adder helpers

static struct bfy_block*
buffer_append_block(bfy_buffer* buf) {
    size_t const blocksize = sizeof(struct bfy_block);
    if (buf->blocks != NULL) {
        buf->blocks = realloc(buf->blocks, blocksize * ++buf->n_blocks);
    } else if (buf->block.data != NULL) {
        buf->n_blocks = 2;
        buf->blocks = malloc(blocksize * buf->n_blocks);
        buf->blocks[0] = buf->block;
        buf->block = InitBlock;
    }

    struct bfy_block* back = blocks_back(buf);
    *back = InitBlock;
    return back;
}

static struct bfy_block*
buffer_get_writable_back(bfy_buffer* buf) {
    struct bfy_block* back = blocks_back(buf);
    return block_is_writable(back) ? back : buffer_append_block(buf);
}

/// adders

bool
bfy_buffer_add_readonly(bfy_buffer* buf, const void* data, size_t size) {
    struct bfy_block* block = buffer_append_block(buf);
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
    struct bfy_iovec const io = bfy_buffer_reserve_space(buf, size);
    if (io.iov_len < size) {
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
    if (block_get_writable_size(block) >= size) {
        return true;
    }
    if (!block_can_realloc(block)) {
        block = buffer_append_block(buf);
    }
    return block_ensure_writable_size(block, size);
}

// evbuffer-like naming
bool
bfy_buffer_expand(bfy_buffer* buf, size_t size) {
    return bfy_buffer_ensure_space(buf, size);
}

bool
bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in) {
    struct bfy_iovec io = bfy_buffer_peek_space(buf);
    for(;;) {
        va_list args;
        va_copy(args, args_in);
        size_t const n = vsnprintf(io.iov_base, io.iov_len, fmt, args);
        va_end(args);

        if (n < io.iov_len) {
            bfy_buffer_commit_space(buf, n);
            return true;
        }

        io = bfy_buffer_reserve_space(buf, n+1); // +1 for trailing '\0'
        if (io.iov_len < n + 1) {
            return false;
        }
    }
}

///

static void
buffer_drain_first_n_blocks(bfy_buffer* buf, size_t n) {
    block_release(&buf->block);
    buf->block = InitBlock;

    if (buf->blocks != NULL) {
        assert(n <= buf->n_blocks);
        for (size_t i = 0; i < n; ++i) {
            block_release(buf->blocks + i);
        }
        const size_t blocksize = sizeof(struct bfy_block);
        memmove(buf->blocks, buf->blocks + n, blocksize * (buf->n_blocks -= n));
        if (buf->n_blocks == 0) {
            buf->n_blocks = 1;
        }
    }
}

static size_t
block_drain(struct bfy_block* block, size_t n) {
    struct bfy_iovec const vec = block_peek_content(block, n);
    block->read_pos += vec.iov_len;
    return vec.iov_len;
}

static bool
block_has_readable(struct bfy_block const* block) {
    return block->write_pos > block->read_pos;
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
        if (block_get_readable_size(it) > n_this_block) {
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
buf_limit_readable_size(bfy_buffer const* buf, size_t len) {
    return size_t_min(len, bfy_buffer_get_content_len(buf));
}

static struct bfy_block*
buffer_prepend_block(bfy_buffer* buf) {
    const size_t blocksize = sizeof(struct bfy_block);
    buf->blocks = realloc(buf->blocks, blocksize * (buf->n_blocks + 1));
    memmove(buf->blocks + 1, buf->blocks, blocksize * buf->n_blocks);
    buf->blocks[0] = InitBlock;
    ++buf->n_blocks;
    return buf->blocks;
}

void*
bfy_buffer_make_contiguous(bfy_buffer* buf, size_t n_wanted) {
    n_wanted = buf_limit_readable_size(buf, n_wanted);

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
    struct bfy_block const newblock = {
        .data = data,
        .size = n_moved,
        .write_pos = n_moved
    };
    block = buffer_prepend_block(buf);
    *block = newblock;

    return block_read_begin(block);
}

static bool
buf_has_readable(bfy_buffer const* buf) {
    return block_has_readable(blocks_cbegin(buf));
}

static void
buffer_append_blocks(bfy_buffer* tgt, struct bfy_block const* new_blocks, size_t n_new_blocks) {
    if (n_new_blocks > 0) {
        struct bfy_block const* old_blocks = blocks_cbegin(tgt);
        size_t const n_old_blocks = buf_has_readable(tgt) ? blocks_cend(tgt) - old_blocks : 0;
        const size_t blocksize = sizeof(struct bfy_block);
        struct bfy_block* const blocks = malloc(blocksize * (n_old_blocks + n_new_blocks));
        memcpy(blocks, old_blocks, blocksize * n_old_blocks);
        memcpy(blocks + n_old_blocks, new_blocks, blocksize * n_new_blocks);
        free(tgt->blocks);
        tgt->blocks = blocks;
        tgt->n_blocks = n_old_blocks + n_new_blocks;
    }
}

bool
bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* src) {
    return bfy_buffer_remove_buffer(src, buf, SIZE_MAX);
}

bool
bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* tgt, size_t n_bytes_wanted)
{
    size_t n_left = n_bytes_wanted;

    // see how many blocks get removed as-is
    // and how many bytes will be left if we need to move half-a-block
    size_t n_blocks_removed = 0;
    for (struct bfy_block const* it = blocks_cbegin(buf), *end = blocks_cend(buf); it != end; ++it) {
        size_t const n_readable = block_get_readable_size(it);
        if (n_left < n_readable) {
            break;
        }
        n_left -= n_readable;
        ++n_blocks_removed;
    }

    // are there any blocks that get moved over completely?
    if (n_blocks_removed > 0) {
        buffer_append_blocks(tgt, blocks_cbegin(buf), n_blocks_removed);
        buffer_drain_first_n_blocks(buf, n_blocks_removed);
    }

    // are there any remainder bytes to move over in a new block?
    if ((n_left > 0) && buf_has_readable(buf)) {
        struct bfy_block* block = blocks_begin(buf);
        bfy_buffer_add(tgt, block_read_cbegin(block), n_left);
        block_drain(block, n_left);
    }

    return true;
}

void
bfy_buffer_add_chain(struct bfy_buffer* buf) {
    struct bfy_block* block = buffer_get_writable_back(buf);
    if (block_has_readable(block)) {
        buffer_append_block(buf);
    }
}

struct bfy_iovec
bfy_buffer_peek_space(struct bfy_buffer* buf) {
    return block_peek_space(blocks_back(buf));
}

struct bfy_iovec
bfy_buffer_reserve_space(struct bfy_buffer* buf, size_t n_wanted) {
    struct bfy_block* block = buffer_get_writable_back(buf);
    block_ensure_writable_size(block, n_wanted);
    return block_peek_space(block);
}

bool
bfy_buffer_commit_space(struct bfy_buffer* buf, size_t size) {
    struct bfy_block* block = buffer_get_writable_back(buf);
    size_t const n_writable = block_get_writable_size(block);
    assert (size <= n_writable);
    size = size_t_min(size, n_writable);
    block->write_pos += size;
    return true;
}
