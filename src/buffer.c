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

/// page utils

static struct bfy_page const InitPage = { 0 };

static struct bfy_page*
pages_begin(bfy_buffer* buf) {
    return buf->pages == NULL ? &buf->page : buf->pages;
}
static struct bfy_page const*
pages_cbegin(bfy_buffer const* buf) {
    return buf->pages == NULL ? &buf->page : buf->pages;
}
static struct bfy_page const*
pages_cend(bfy_buffer const* buf) {
    return buf->pages == NULL ? &buf->page + 1 : buf->pages + buf->n_pages;
}
static struct bfy_page*
pages_back(bfy_buffer* buf) {
    return buf->pages == NULL ? &buf->page : buf->pages + buf->n_pages - 1;
}
static struct bfy_page const*
pages_cback(bfy_buffer const* buf) {
    return buf->pages == NULL ? &buf->page : buf->pages + buf->n_pages - 1;
}
static bool
page_can_realloc(struct bfy_page const* page) {
    return (page->flags & (BFY_PAGE_FLAGS_READONLY | BFY_PAGE_FLAGS_UNMANAGED)) == 0;
}
static bool
page_is_writable(struct bfy_page const* page) {
    return (page->flags & BFY_PAGE_FLAGS_READONLY) == 0;
}
static bool
page_is_recyclable(struct bfy_page const* page) {
    return page_is_writable(page);
}
static bool
page_has_readable(struct bfy_page const* page) {
    return page->write_pos > page->read_pos;
}

static void*
page_read_begin(struct bfy_page* page) {
    return page->data + page->read_pos;
}
static const void*
page_read_cbegin(struct bfy_page const* page) {
    return page->data + page->read_pos;
}

static size_t
page_get_content_len(struct bfy_page const* page) {
    return page->write_pos - page->read_pos;
}
static size_t
page_get_space_len(struct bfy_page const* page) {
    return page->size - page->write_pos;
}

static bool
page_realloc(struct bfy_page* page, size_t requested) {
    assert(page_can_realloc(page));

    // maybe free the memory
    if (requested == 0) {
        if (page->data != NULL) {
            free(page->data);
            page->data = NULL;
        }
        return true;
    }

    // decide on a new capacity
    size_t const Min = 1024;
    size_t new_size = Min;
    while (new_size < requested) {
        new_size *= 2u;
    }

    if (new_size <= page->size) {
        // FIXME: is there a use case for allowing shrinking?
        return true;
    }

    void* new_data = realloc(page->data, new_size);
    if (new_data == NULL) {
        errno = ENOMEM;
        return false;
    } else {
        page->data = new_data;
        page->size = new_size;
        return true;
    }
}

static bool
page_ensure_space_len(struct bfy_page* page, size_t wanted) {
    size_t const space = page_get_space_len(page);
    if (wanted <= space) {
        return true;
    }
    if (page_can_realloc(page)) {
       return page_realloc(page, page->size + (wanted - space));
    }
    return false;
}

static void
page_release(struct bfy_page* page) {
    if (page->unref_cb != NULL) {
        page->unref_cb(page->data, page->size, page->unref_arg);
    }
    if (page_can_realloc(page)) {
        page_realloc(page, 0);
    }
    *page = InitPage;
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
        .page = {
            .data = data,
            .size = len,
            .flags = BFY_PAGE_FLAGS_UNMANAGED
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
buffer_count_pages(bfy_buffer const* buf) {
    return pages_cend(buf) - pages_cbegin(buf);
}

static void
buffer_release_first_n_pages(bfy_buffer* buf, size_t n) {
    n = size_t_min(n, buffer_count_pages(buf));
    for (struct bfy_page *it=pages_begin(buf), *const end=it+n; it!=end; ++it) {
        page_release(it);
    }
}

void
bfy_buffer_destruct(bfy_buffer* buf) {
    buffer_release_first_n_pages(buf, SIZE_MAX);
    free(buf->pages);
    buf->pages = NULL;
    buf->n_pages = 0;
}

void
bfy_buffer_free(bfy_buffer* buf) {
    bfy_buffer_destruct(buf);
    free(buf);
}

/// some simple getters

static struct bfy_pos
buffer_get_pos(bfy_buffer const* buf, size_t content_pos) {
    struct bfy_pos ret = { 0 };

    struct bfy_page const* const begin = pages_cbegin(buf);
    struct bfy_page const* const end = pages_cend(buf);
    struct bfy_page const* it;
    for (it = begin; it != end; ++it) {
        size_t const page_len = page_get_content_len(it);
        size_t const got = size_t_min(page_len, content_pos);
        if (got == 0) {
            break;
        }
        ret.content_pos += got;
        content_pos -= got;
        if ((content_pos == 0) && (page_len > got)) {
            ret.page_pos = got;
            break;
        }
    }

    ret.page_idx = it - begin;
    return ret;
}

size_t
bfy_buffer_get_content_len(bfy_buffer const* buf) {
    return buffer_get_pos(buf, SIZE_MAX).content_pos;
}

size_t
bfy_buffer_get_space_len(bfy_buffer const* buf) {
    return page_get_space_len(pages_cback(buf));
}

/// readers

size_t
bfy_buffer_peek_all(bfy_buffer const* buf,
                    struct bfy_iovec* vec_out, size_t n_vec) {
    return bfy_buffer_peek(buf, SIZE_MAX, vec_out, n_vec);
}

static struct bfy_iovec
page_peek_content(struct bfy_page const* page, size_t n) {
    struct bfy_iovec vec = {
        .iov_base = (void*) page_read_cbegin(page),
        .iov_len = size_t_min(n, page_get_content_len(page))
    };
    return vec;
}

size_t
bfy_buffer_peek(bfy_buffer const* buf, size_t wanted,
                struct bfy_iovec* vec, size_t n_vec) {
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);
    size_t const needed = pos.page_idx + (pos.page_pos ? 1 : 0);

    struct bfy_iovec const* const vec_end = vec + n_vec;
    struct bfy_page const* it = pages_cbegin(buf);
    struct bfy_page const* end = it + size_t_min(n_vec, pos.page_idx);
    for ( ; it != end; ++it) {
        *vec++ = page_peek_content(it, SIZE_MAX);
    }
    if ((pos.page_pos > 0) && (vec < vec_end)) {
        *vec++ = page_peek_content(it, pos.page_pos);
    }
    return needed;
}

/// adding pages

static bool
buf_has_readable(bfy_buffer const* buf) {
    return page_has_readable(pages_cbegin(buf));
}

static bool
buffer_insert_pages(bfy_buffer* buf, size_t pos,
                     struct bfy_page const* new_pages,
                     size_t new_len) {
    if (new_len > 0) {
        struct bfy_page const* old_pages = pages_cbegin(buf);
        size_t const old_len = buf_has_readable(buf)
                                  ? pages_cend(buf) - old_pages
                                  : 0;
        pos = size_t_min(pos, old_len);
        const size_t pagesize = sizeof(struct bfy_page);
        struct bfy_page* const pages = malloc(pagesize * (old_len + new_len));
        if (pages == NULL) {
            return false;
        }
        memcpy(pages, old_pages, pagesize * pos);
        memcpy(pages + pos, new_pages, pagesize * new_len);
        memcpy(pages + pos + new_len, old_pages + pos, pagesize * (old_len - pos));
        free(buf->pages);
        buf->pages = pages;
        buf->n_pages = old_len + new_len;
    }
    return true;
}

static bool
buffer_append_pages(bfy_buffer* tgt,
                    struct bfy_page const* new_pages,
                    size_t new_len) {
    return buffer_insert_pages(tgt, buffer_count_pages(tgt),
                               new_pages, new_len);
}
static bool
buffer_prepend_pages(bfy_buffer* tgt,
                      struct bfy_page const* new_pages,
                      size_t new_len) {
    return buffer_insert_pages(tgt, 0, new_pages, new_len);
}

/// removing pages

void
bfy_buffer_reset(bfy_buffer* buf) {
    struct bfy_page recycle_me = InitPage;

    struct bfy_page* it = pages_begin(buf);
    struct bfy_page const* const end = pages_cend(buf);
    for ( ; it != end; ++it) {
        if (page_is_recyclable(it) && it->size > recycle_me.size) {
            page_release(&recycle_me);
            recycle_me = *it;
            recycle_me.read_pos = recycle_me.write_pos = 0;
        } else {
            page_release(it);
        }
    }

    free(buf->pages);
    buf->pages = NULL;
    buf->n_pages = 0;

    buf->page = recycle_me;
}

static void
buffer_forget_first_n_pages(bfy_buffer* buf, size_t len) {
    struct bfy_page* const begin = pages_begin(buf);

    for (struct bfy_page* it=begin, *const end=it + len; it != end; ++it) {
        *it = InitPage;
    }

    if (buf->pages != NULL) {
        struct bfy_page const* const end = pages_cend(buf);
        if (begin + len < end) {
            const size_t pagesize = sizeof(struct bfy_page);
            memmove(begin, begin + len, pagesize * (end - begin - len));
            buf->n_pages -= len;
        } else {
            buf->n_pages = 1;
            buf->pages[0] = InitPage;
        }
    }
}

static void
buffer_drain_first_n_pages(bfy_buffer* buf, size_t len) {
    if (len < buffer_count_pages(buf)) {
        buffer_release_first_n_pages(buf, len);
        buffer_forget_first_n_pages(buf, len);
    } else {
        bfy_buffer_reset(buf);
    }
}

///

typedef bool (page_test)(struct bfy_page const* page);

static struct bfy_page*
buffer_get_usable_back(bfy_buffer* buf, page_test* test) {
    struct bfy_page* page = pages_back(buf);
    if (test(page)) {
        return page;
    }
    if (!buffer_append_pages(buf, &InitPage, 1)) {
        return NULL;
    }
    return pages_back(buf);
}

static struct bfy_page*
buffer_get_writable_back(bfy_buffer* buf) {
    return buffer_get_usable_back(buf, page_is_writable);
}

/// space

bool
bfy_buffer_add_pagebreak(struct bfy_buffer* buf) {
    struct bfy_page page = InitPage;
    return buffer_append_pages(buf, &page, 1);
}

static struct bfy_iovec
page_peek_space(struct bfy_page const* page) {
    struct bfy_iovec vec = {
        .iov_base = (void*) (page->data + page->write_pos),
        .iov_len = page_get_space_len(page)
    };
    return vec;
}

struct bfy_iovec
bfy_buffer_peek_space(struct bfy_buffer* buf) {
    return page_peek_space(pages_back(buf));
}

struct bfy_iovec
bfy_buffer_reserve_space(struct bfy_buffer* buf, size_t wanted) {
    bfy_buffer_ensure_space(buf, wanted);
    return bfy_buffer_peek_space(buf);
}

size_t
bfy_buffer_commit_space(struct bfy_buffer* buf, size_t len) {
    size_t n_committed = 0;

    struct bfy_page* page = buffer_get_writable_back(buf);
    if (page != NULL) {
        size_t const n_writable = page_get_space_len(page);
        assert (len <= n_writable);
        len = size_t_min(len, n_writable);
        page->write_pos += len;
        n_committed = len;
    }

    return n_committed;
}

/// adders

bool
bfy_buffer_add_readonly(bfy_buffer* buf,
        const void* data, size_t len) {
    struct bfy_page const page = {
        .data = (void*) data,
        .size = len,
        .read_pos = 0,
        .write_pos = len,
        .flags = BFY_PAGE_FLAGS_READONLY | BFY_PAGE_FLAGS_UNMANAGED
    };
    return buffer_append_pages(buf, &page, 1);
}

bool bfy_buffer_add_reference(bfy_buffer* buf,
        const void* data, size_t len, 
        bfy_unref_cb* cb, void* unref_arg) {
    struct bfy_page const page = {
        .data = (void*) data,
        .size = len,
        .read_pos = 0,
        .write_pos = len,
        .flags = BFY_PAGE_FLAGS_READONLY,
        .unref_cb = cb,
        .unref_arg = unref_arg
    };
    return buffer_append_pages(buf, &page, 1);
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
    struct bfy_page* page = pages_back(buf);
    if (page_get_space_len(page) >= len) {
        return true;
    }
    page = buffer_get_usable_back(buf, page_can_realloc);
    return (page != NULL) && page_ensure_space_len(page, len);
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
page_drain(struct bfy_page* page, size_t n) {
    n = size_t_min(n, page_get_content_len(page));
    page->read_pos += n;
    return n;
}

bool
bfy_buffer_drain(bfy_buffer* buf, size_t wanted) {
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);
    if (pos.page_idx > 0) {
        buffer_drain_first_n_pages(buf, pos.page_idx);
    }
    if (pos.page_pos > 0) {
        page_drain(pages_begin(buf), pos.page_pos);
    }
    return true;
}

///

static size_t
page_copyout(struct bfy_page const* page, void* data, size_t wanted) {
    struct bfy_iovec const vec = page_peek_content(page, wanted);
    memcpy(data, vec.iov_base, vec.iov_len);
    return vec.iov_len;
}

size_t
bfy_buffer_copyout(bfy_buffer const* buf, void* vdata, size_t wanted) {
    int8_t* data = vdata;
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);
    struct bfy_page const* it = pages_cbegin(buf);
    struct bfy_page const* const end = it + pos.page_idx;
    for ( ; it != end; ++it) {
        data += page_copyout(it, data, SIZE_MAX);
    }
    if (pos.page_pos > 0) {
        data += page_copyout(it, data, pos.page_pos);
    }
    assert ((data - (int8_t*)vdata) == pos.content_pos);
    return data - (int8_t*)vdata;
}

static size_t
page_remove(struct bfy_page* page, void* data, size_t wanted) {
    size_t const n_copied = page_copyout(page, data, wanted);
    page->read_pos += n_copied;
    return n_copied;
}

static size_t
buffer_remove(bfy_buffer* buf, void* vdata, struct bfy_pos stop) {
    int8_t* data = vdata;
    struct bfy_page* it = pages_begin(buf);
    for (int i=0; i<stop.page_idx; ++i, ++it) {
        data += page_remove(it, data, SIZE_MAX);
    }
    if (stop.page_pos > 0) {
        data += page_remove(it, data, stop.page_pos);
    }
    buffer_drain_first_n_pages(buf, stop.page_idx);
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

    // if the first page already holds wanted, then we're done
    if ((pos.page_idx == 0) || (pos.page_idx == 1 && pos.page_pos == 0)) {
        return page_read_begin(pages_begin(buf));
    }

    // FIXME: if page->size >= wanted then do the copies there

    // FIXME: copyout instead of remove so we can reuse pages[0]

    // build a contiguous page
    int8_t* data = malloc(pos.content_pos);
    size_t const n_moved = buffer_remove(buf, data, pos);

    // now prepend a new page with the contiguous memory
    struct bfy_page const newpage = {
        .data = data,
        .size = n_moved,
        .write_pos = n_moved
    };
    if (!buffer_prepend_pages(buf, &newpage, 1)) {
        free(data);
        return false;
    }

    return page_read_begin(pages_begin(buf));
}

bool
bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* src) {
    return bfy_buffer_remove_buffer(src, buf, SIZE_MAX);
}

bool
bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* tgt, size_t wanted) {
    struct bfy_pos pos = buffer_get_pos(buf, wanted);

    // are there any pages that get moved over completely?
    if (pos.page_idx > 0) {
        buffer_append_pages(tgt, pages_cbegin(buf), pos.page_idx);
        buffer_forget_first_n_pages(buf, pos.page_idx);
    }

    // are there any remainder bytes to move over in a new page?
    if (pos.page_pos > 0) {
        struct bfy_page* page = pages_begin(buf);
        bfy_buffer_add(tgt, page_read_cbegin(page), pos.page_pos);
        page_drain(page, pos.page_pos);
    }

    return true;
}

/// search

static struct bfy_iovec
buffer_get_content_at_pos(bfy_buffer const* buf, struct bfy_pos pos) {
    struct bfy_page const* page = pages_cbegin(buf) + pos.page_idx;
    struct bfy_iovec io = {
        .iov_base = (void*) page_read_cbegin(page),
        .iov_len = page_get_content_len(page)
    };
    if (pos.page_pos > 0) {
        io.iov_base = (char*)io.iov_base + pos.page_pos;
        io.iov_len -= pos.page_pos;
    }
    return io;
}

static struct bfy_iovec
buffer_get_content_at_content_pos(bfy_buffer const* buf, size_t pos) {
    struct bfy_iovec io = buffer_get_content_at_pos(buf, buffer_get_pos(buf, pos));
    fprintf(stderr, "content_pos %zu -> iov_base %p, iov_len %zu\n", pos, io.iov_base, io.iov_len);
    return io;
}

static bool
buffer_contains_at(bfy_buffer const* buf, size_t at,
                   void const* needle, size_t needle_len) {
    fprintf(stderr, "contains at %zu needle %s needle_len %zu\n", at, (char const*)needle, needle_len);
    struct bfy_iovec const io = buffer_get_content_at_content_pos(buf, at);
    fprintf(stderr, "iov_len is %zu\n", io.iov_len);
    if (io.iov_len == 0) {
        fprintf(stderr, "returning false because iov_len is 0\n");
        return false;
    }
    if (io.iov_len >= needle_len) {
        bool const match = !memcmp(io.iov_base, needle, needle_len);
        fprintf(stderr, "returning %d because memcmp needle_len\n", (int)match);
        return !memcmp(io.iov_base, needle, needle_len);
    }
    if (memcmp(io.iov_base, needle, io.iov_len)) {
        fprintf(stderr, "returning false because memcmp iov_len\n");
        return false;
    }
    return buffer_contains_at(buf, at + io.iov_len,
                              (char const*)needle + io.iov_len,
                              needle_len - io.iov_len);
}

bool
bfy_buffer_search_range(bfy_buffer const* buf,
                        size_t begin, size_t end,
                        void const* needle, size_t needle_len,
                        size_t* match) {
    end = size_t_min(end, bfy_buffer_get_content_len(buf));

    for (size_t walk = begin; walk < end; ++walk) {
        fprintf(stderr, "WALK %zu\n", walk);
        if (end - walk < needle_len) {
            fprintf(stderr, "out of room\n");
            return false;
        }
        if (buffer_contains_at(buf, walk, needle, needle_len)) {
            fprintf(stderr, "found match at %zu\n", walk);
            *match = walk;
            return true;
        }
    }

    return false;
}

bool
bfy_buffer_search(bfy_buffer const* buf,
                  void const* needle, size_t needle_len,
                  size_t* match) {
    return bfy_buffer_search_range(buf, 0, SIZE_MAX, needle, needle_len, match);
}

/// endian
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
