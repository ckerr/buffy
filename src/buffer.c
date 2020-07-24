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

#include <buffy/buffer.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>  // vsprintf()
#include <string.h>  // memcpy()
#include <stdlib.h>  // malloc(), realloc(), free()

#include "endianness.h"

static struct bfy_allocator allocator = {
    .malloc = malloc,
    .free = free,
    .calloc = calloc,
    .realloc = realloc
};

void bfy_set_allocator(struct bfy_allocator* alloc) {
    allocator = *alloc;
}

static size_t
size_t_min(size_t a, size_t b) {
    return a < b ? a : b;
}

struct bfy_pos {
    /* Which page this position is in. */
    size_t page_idx;

    /* Offset into the page.
       This is relative to page.read_pos, so the memory location
       would be bfy_page.data + bfy_page.read_pos + pos.page_pos */
    size_t page_pos;

    /* The offset inside the buffer's content as if it were
       all contiguous, [0..bfy_buffer.contents_len).
       When used as an iterator, can also equal bfy_buffer.contents_len
       to indicate end-of-buffer. */
    size_t content_pos;
};

/// page utils

static struct bfy_page const InitPage = { 0 };

static struct bfy_page*
pages_begin(bfy_buffer* const buf) {
    return buf->pages == NULL ? &buf->page : buf->pages;
}
static struct bfy_page const*
pages_cbegin(bfy_buffer const* const buf) {
    return buf->pages == NULL ? &buf->page : buf->pages;
}
static struct bfy_page const*
pages_cend(bfy_buffer const* const buf) {
    return buf->pages == NULL ? &buf->page + 1 : buf->pages + buf->n_pages;
}
static size_t
buffer_count_pages(bfy_buffer const* buf) {
    return pages_cend(buf) - pages_cbegin(buf);
}
static struct bfy_page*
pages_back(bfy_buffer* const buf) {
    return buf->pages == NULL ? &buf->page : buf->pages + buf->n_pages - 1;
}
static struct bfy_page const*
pages_cback(bfy_buffer const* const buf) {
    return buf->pages == NULL ? &buf->page : buf->pages + buf->n_pages - 1;
}
static bool
page_can_realloc(struct bfy_page const* const page) {
    return (page->flags & (BFY_PAGE_FLAGS_READONLY | BFY_PAGE_FLAGS_UNMANAGED)) == 0;
}
static bool
page_is_writable(struct bfy_page const* const page) {
    return (page->flags & BFY_PAGE_FLAGS_READONLY) == 0;
}
static bool
page_is_recyclable(struct bfy_page const* const page) {
    return page_is_writable(page);
}

static void*
page_read_begin(struct bfy_page const* const page) {
    return page->data + page->read_pos;
}
static const void*
page_read_cbegin(struct bfy_page const* const page) {
    return page->data + page->read_pos;
}

static void*
page_write_cbegin(struct bfy_page const* const page) {
    return page->data + page->write_pos;
}

static size_t
page_get_content_len(struct bfy_page const* const page) {
    return page->write_pos - page->read_pos;
}
static size_t
page_get_space_len(struct bfy_page const* const page) {
    return page->size - page->write_pos;
}

/// iov

static struct bfy_iovec
page_peek_content(struct bfy_page const* const page) {
    struct bfy_iovec const io = {
        .iov_base = (void*) page_read_cbegin(page),
        .iov_len = page_get_content_len(page)
    };
    return io;
}

static struct bfy_iovec
iov_drain(struct bfy_iovec io, size_t len) {
    len = size_t_min(len, io.iov_len);
    io.iov_base = (char*)io.iov_base + len;
    io.iov_len -= len;
    return io;
}

/// content iterator

struct bfy_iter {
    struct bfy_buffer* buf;
    struct bfy_pos cur;
    struct bfy_iovec io;
    struct bfy_pos end;
};

static struct bfy_iovec page_peek_content(struct bfy_page const* const page);

static void
iter_impl_set_io(struct bfy_iter* const iter) {
    struct bfy_page const* page = pages_cbegin(iter->buf) + iter->cur.page_idx;
    iter->io = iov_drain(page_peek_content(page), iter->cur.page_pos);
    iter->io.iov_len = size_t_min(iter->io.iov_len, iter->end.content_pos - iter->cur.content_pos);
}

static bool
iter_begin(struct bfy_iter* const iter,
           struct bfy_buffer const* const buf,
           struct bfy_pos begin,
           struct bfy_pos end) {
    struct bfy_iter init = {
        .buf = (void*) buf,
        .cur = begin,
        .end = end
    };
    if (init.cur.content_pos >= init.end.content_pos) {
        return false;
    }

    *iter = init;
    iter_impl_set_io(iter);
    return true;
}

static bool
iter_next_page(struct bfy_iter* const iter) {
    struct bfy_pos next = {
       .page_idx = iter->cur.page_idx + 1,
       .page_pos = 0,
       .content_pos = iter->cur.content_pos + iter->io.iov_len
    };
    if (next.page_idx >= buffer_count_pages(iter->buf)) {
        iter->cur = iter->end;
        return false;
    }
    if (next.content_pos >= iter->end.content_pos) {
        iter->cur = iter->end;
        return false;
    }

    iter->cur = next;
    iter_impl_set_io(iter);
    return true;
}

static bool
iter_advance_n_bytes(struct bfy_iter* const iter, size_t n) {
    while (iter->io.iov_len < n) { // make sure we're on the right page
        n -= iter->io.iov_len;
        if (!iter_next_page(iter)) {
            return false;
        }
    }
    iter->io = iov_drain(iter->io, n);
    iter->cur.page_pos += n;
    iter->cur.content_pos += n;
    return true;
}

///  change notifications

static void
buffer_reset_changed_info(bfy_buffer* buf) {
    struct bfy_changed_cb_info const info = {
        .orig_size = bfy_buffer_get_content_len(buf)
    };
    buf->changed_info = info;
}

void
bfy_buffer_set_changed_cb(bfy_buffer* buf, bfy_changed_cb* cb, void* cb_data) {
    buf->changed_cb = cb;
    buf->changed_data = cb_data;
    buffer_reset_changed_info(buf);
}

static void
buffer_check_changed_cb(bfy_buffer* buf) {
    if (buf->changed_cb == NULL) {
        return;
    }
    if (buf->changed_muted != 0) {
        return;
    }
    if (buf->changed_coalescing != 0) {
        return;
    }
    if (buf->changed_info.n_added == 0 && buf->changed_info.n_deleted == 0) {
        return;
    }

    buf->changed_cb(buf, &buf->changed_info, buf->changed_data);
    buffer_reset_changed_info(buf);
}

static void
buffer_record_content_added(bfy_buffer* buf, size_t n) {
    buf->content_len += n;

    if (buf->changed_muted == 0) {
        buf->changed_info.n_added += n;
        buffer_check_changed_cb(buf);
    }
}

static void
buffer_record_content_removed(bfy_buffer* buf, size_t n) {
    buf->content_len -= n;

    if (buf->changed_muted == 0) {
        buf->changed_info.n_deleted += n;
        buffer_check_changed_cb(buf);
    }
}

void
bfy_buffer_mute_change_events(bfy_buffer* buf) {
    ++buf->changed_muted;
}

void
bfy_buffer_unmute_change_events(bfy_buffer* buf) {
    if (--buf->changed_muted == 0) {
        buffer_check_changed_cb(buf);
    }
}

void
bfy_buffer_begin_coalescing_change_events(bfy_buffer* buf) {
    ++buf->changed_coalescing;
}

void
bfy_buffer_end_coalescing_change_events(bfy_buffer* buf) {
    if(--buf->changed_coalescing == 0) {
        buffer_check_changed_cb(buf);
    }
}

/// page memory management

static size_t
pick_capacity(size_t min, size_t requested) {
    size_t capacity = min;
    while (capacity < requested) {
        capacity *= 2u;
    }
    return capacity;
}

static int
page_realloc(struct bfy_page* page, size_t requested) {
    assert(page_can_realloc(page));

    // maybe free the memory
    if (requested == 0) {
        if (page->data != NULL) {
            allocator.free(page->data);
            page->data = NULL;
        }
        return 0;
    }

    // decide on a new capacity
    size_t const Min = 1024;
    size_t const new_size = pick_capacity(Min, requested);
    if (new_size <= page->size) {
        return 0;
    }

    void* new_data = allocator.realloc(page->data, new_size);
    if (new_data != NULL) {
        page->data = new_data;
        page->size = new_size;
        return 0;
    }

    errno = ENOMEM;
    return -1;
}

static int
page_ensure_space_len(struct bfy_page* page, size_t wanted) {
    size_t const space = page_get_space_len(page);
    if (wanted <= space) {
        return 0;
    }
    if (page_can_realloc(page)) {
       return page_realloc(page, page->size + (wanted - space));
    }
    return -1;
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

/// some simple getters

static struct bfy_pos
buffer_get_pos(bfy_buffer const* buf, size_t content_pos) {
    if (content_pos == 0) {
        struct bfy_pos const begin = { 0 };
        return begin;
    }
    if (content_pos >= buf->content_len) {
        struct bfy_pos const end = {
            .page_idx = buffer_count_pages(buf),
            .page_pos = 0,
            .content_pos = buf->content_len
        };
        return end;
    }

    struct bfy_pos ret = { 0 };
    struct bfy_page const* const begin = pages_cbegin(buf);
    struct bfy_page const* const end = pages_cend(buf);
    struct bfy_page const* it;
    for (it = begin; it != end; ++it) {
        size_t const page_len = page_get_content_len(it);
        size_t const got = size_t_min(page_len, content_pos);
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
    assert(buf->content_len == buffer_get_pos(buf, SIZE_MAX).content_pos);
    return buf->content_len;
}

size_t
bfy_buffer_get_space_len(bfy_buffer const* buf) {
    return page_get_space_len(pages_cback(buf));
}

/// peek

size_t
bfy_buffer_peek_range(bfy_buffer const* buf,
                      size_t begin_at, size_t end_at,
                      struct bfy_iovec* vec, size_t n_vec) {
    size_t needed = 0;
    struct bfy_iovec const* const vec_end = vec + n_vec;

    struct bfy_iter iter;
    if (iter_begin(&iter, buf, buffer_get_pos(buf, begin_at), buffer_get_pos(buf, end_at))) do {
        ++needed;
        if (vec < vec_end) {
            *vec++ = iter.io;
        }
    } while (iter_next_page(&iter));

    return needed;
}

size_t
bfy_buffer_peek(bfy_buffer const* buf, size_t len,
                struct bfy_iovec* vec_out, size_t n_vec) {
    return bfy_buffer_peek_range(buf, 0, len, vec_out, n_vec);
}

size_t
bfy_buffer_peek_all(bfy_buffer const* buf,
                    struct bfy_iovec* vec_out, size_t n_vec) {
    return bfy_buffer_peek(buf, SIZE_MAX, vec_out, n_vec);
}

/// adding pages

static int
buffer_insert_pages(bfy_buffer* buf, size_t pos,
                    struct bfy_page const* new_pages,
                    size_t new_len) {
    if (new_len == 0 || new_pages == NULL) {
        return 0;
    }

    // if we have nothing, use buf->page
    if (new_len == 1 && buf->pages == NULL && buf->page.data == NULL) {
        buf->page = *new_pages;
        buffer_record_content_added(buf, page_get_content_len(new_pages));
        return 0;
    }

    // ensure we have enough space in buf->pages
    size_t n_pages_alloc = buffer_count_pages(buf) + new_len;
    size_t const pagesize = sizeof(struct bfy_page);
    if (n_pages_alloc > buf->n_pages_alloc) {
        n_pages_alloc = pick_capacity(16, n_pages_alloc);
        void* pages = allocator.realloc(buf->pages, pagesize * n_pages_alloc);
        if (pages != NULL) {
            buf->pages = pages;
            buf->n_pages_alloc = n_pages_alloc;
        }
    }
    if (buf->pages == NULL) {
        return -1;
    }

    // if we had 1 page and are inserting more, handle the special case
    // of moving the single buf->page into the multipage array
    if (buf->n_pages == 0 && buf->page.data != NULL) {
        buf->n_pages = 1;
        *buf->pages = buf->page;
        buf->page = InitPage;
    }

    // insert new_pages into buf->pages
    pos = size_t_min(pos, buf->n_pages);
    if (buf->n_pages > pos) {
        memmove(buf->pages + pos + new_len, buf->pages + pos, pagesize * (buf->n_pages - pos));
    }
    memcpy(buf->pages + pos, new_pages, pagesize * new_len);
    buf->n_pages += new_len;

    // record the new content
    size_t new_content_len = 0;
    for (size_t i = 0; i < new_len; ++i) {
        new_content_len += page_get_content_len(new_pages + i);
    }
    buffer_record_content_added(buf, new_content_len);
    return 0;
}

static int
buffer_append_pages(bfy_buffer* tgt,
                    struct bfy_page const* new_pages,
                    size_t new_len) {
    return buffer_insert_pages(tgt, buffer_count_pages(tgt),
                               new_pages, new_len);
}
static int
buffer_prepend_pages(bfy_buffer* tgt,
                      struct bfy_page const* new_pages,
                      size_t new_len) {
    return buffer_insert_pages(tgt, 0, new_pages, new_len);
}

///

typedef bool (page_test)(struct bfy_page const* page);

static struct bfy_page*
buffer_get_usable_back(bfy_buffer* buf, page_test* test) {
    // if the existing last page is suitable, return it
    struct bfy_page* page = pages_back(buf);
    if (test(page)) {
        return page;
    }

    // if a new page would be suitable, append one and return it
    if (test(&InitPage)) {
        buffer_append_pages(buf, &InitPage, 1);
        return pages_back(buf);
    }

    return NULL;
}

static struct bfy_page*
buffer_get_writable_back(bfy_buffer* buf) {
    return buffer_get_usable_back(buf, page_is_writable);
}

/// space

static struct bfy_iovec
page_peek_space(struct bfy_page const* page) {
    struct bfy_iovec vec = {
        .iov_base = (void*) page_write_cbegin(page),
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
    struct bfy_iovec io = bfy_buffer_peek_space(buf);
    io.iov_len = size_t_min(io.iov_len, wanted);
    return io;
}

int
bfy_buffer_commit_space(struct bfy_buffer* buf, size_t len) {
    size_t n_committed = 0;

    struct bfy_page* page = buffer_get_writable_back(buf);
    if (page != NULL) {
        size_t const n_writable = page_get_space_len(page);
        assert (len <= n_writable);
        len = size_t_min(len, n_writable);
        page->write_pos += len;
        n_committed = len;
        buffer_record_content_added(buf, len);
    }

    return n_committed == len ? 0 : -1;
}

// move all the content to the beginning of the page
static void
page_make_space_contiguous(struct bfy_page* page) {
    if (page->read_pos != 0) {
        size_t const content_len = page_get_content_len(page);
        memmove(page->data, page_read_cbegin(page), content_len);
        page->read_pos = 0;
        page->write_pos = content_len;
    }
}

int
bfy_buffer_ensure_space(bfy_buffer* buf, size_t len) {
    struct bfy_page* page = pages_back(buf);
    if (page_is_writable(page)) {
        size_t const space_len = page_get_space_len(page);
        if (len <= space_len) {
            // there's enough free space at the end of the page,
            // no extra work necessary
            return 0;
        }
        if (len <= space_len + page->read_pos) {
            // page has enough space but it's not contiguous
            page_make_space_contiguous(page);
            return 0;
        }
    }

    if ((page = buffer_get_usable_back(buf, page_can_realloc))) {
        return page_ensure_space_len(page, len);
    }

    return -1;
}

/// add

int
bfy_buffer_add_readonly(bfy_buffer* buf, const void* data, size_t len) {
    struct bfy_page const page = {
        .data = (void*) data,
        .size = len,
        .read_pos = 0,
        .write_pos = len,
        .flags = BFY_PAGE_FLAGS_READONLY | BFY_PAGE_FLAGS_UNMANAGED
    };
    return buffer_append_pages(buf, &page, 1);
}

int
bfy_buffer_add_reference(bfy_buffer* buf,
                         const void* data, size_t len,
                         bfy_unref_cb* cb, void* unref_arg) {
    struct bfy_page const page = {
        .data = (void*) data,
        .size = len,
        .read_pos = 0,
        .write_pos = len,
        .flags = BFY_PAGE_FLAGS_UNMANAGED,
        .unref_cb = cb,
        .unref_arg = unref_arg
    };
    return buffer_append_pages(buf, &page, 1);
}

int
bfy_buffer_add(bfy_buffer* buf, const void* data, size_t len) {
    struct bfy_iovec const io = bfy_buffer_reserve_space(buf, len);
    if (data == NULL || io.iov_base == NULL || io.iov_len < len) {
        return -1;
    }
    memcpy(io.iov_base, data, len);
    return bfy_buffer_commit_space(buf, len);
}

int
bfy_buffer_add_ch(bfy_buffer* buf, char addme) {
    return bfy_buffer_add(buf, &addme, 1);
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
    // see if we can print it into already-available space
    struct bfy_iovec space = bfy_buffer_peek_space(buf);
    va_list args;
    va_copy(args, args_in);
    size_t n = vsnprintf(space.iov_base, space.iov_len, fmt, args);
    va_end(args);

    // if not, make some more free space and try again
    size_t space_wanted = n;
    if (space_wanted < SIZE_MAX) {
        // add +1 for the the nul space ('\0').
        // NOTE: we *must* have enough space for the nul so that vsnprintf
        // does not truncate the string, but note that the nul space isn't
        // included in commit_space() because embedding nuls into the buffer
        // defeats the purpose of using bfy as a stringbuilder -- if someone
        // calls add_printf() in a loop then remove_string(), the result
        // should be one long uninterrupted string.
        ++space_wanted;
    }
    if (space_wanted > space.iov_len) {
        space = bfy_buffer_reserve_space(buf, space_wanted);
        va_copy(args, args_in);
        n = vsnprintf(space.iov_base, space.iov_len, fmt, args);
        va_end(args);
    }

    // if we succeeded, commit our work
    if (n < space.iov_len) {
        return bfy_buffer_commit_space(buf, n);
    }

    return -1;
}

int
bfy_buffer_add_hton_u8(struct bfy_buffer* buf, uint8_t addme) {
    return bfy_buffer_add(buf, &addme, 1);
}

int
bfy_buffer_add_hton_u16(struct bfy_buffer* buf, uint16_t addme) {
    uint16_t const be = hton16(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

int
bfy_buffer_add_hton_u32(struct bfy_buffer* buf, uint32_t addme) {
    uint32_t const be = hton32(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

int
bfy_buffer_add_hton_u64(struct bfy_buffer* buf, uint64_t addme) {
    uint64_t const be = hton64(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

int
bfy_buffer_add_pagebreak(struct bfy_buffer* buf) {
    struct bfy_page page = InitPage;
    return buffer_append_pages(buf, &page, 1);
}

int
bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* src) {
    size_t const new_content_len = bfy_buffer_get_content_len(src);
    return new_content_len == bfy_buffer_remove_buffer(src, new_content_len, buf) ? 0 : -1;
}

/// drain

enum {
    // pages will usually release their memory when draining,
    // but we need to skip that if the page is being given
    // to another buffer
    DRAIN_FLAG_NORELEASE = (1<<0),

    // buffer usually recycles a page for subsequent use,
    // but we need to skip that if the buffer is being destroyed
    DRAIN_FLAG_NORECYCLE = (1<<1)
};

static size_t
buffer_drain_range(bfy_buffer* const buf,
                   struct bfy_pos begin,
                   struct bfy_pos end,
                   int flags) {
    bool const do_release = (flags & DRAIN_FLAG_NORELEASE) == 0;
    bool const do_recycle = (flags & DRAIN_FLAG_NORECYCLE) == 0;
    size_t n_drained = 0;

    struct bfy_iter iter;
    if (iter_begin(&iter, buf, begin, end)) do {
        struct bfy_page* const page = pages_begin(buf) + iter.cur.page_idx;
        size_t const content_len = page_get_content_len(page);
        char const* iov_end = (char const*)iter.io.iov_base + iter.io.iov_len;
        if (iter.io.iov_len >= content_len) {
            // drain the whole page
            n_drained += content_len;
            if (do_recycle && page_is_recyclable(page)) {
                page->read_pos = page->write_pos = 0;
            } else if (do_release) {
                page_release(page);
            } else {
                *page = InitPage;
            }
        } else if (iter.io.iov_base == page_read_cbegin(page)) {
            // drain from the front of the page
            page->read_pos += iter.io.iov_len;
            n_drained += iter.io.iov_len;
        } else if (iov_end == page_write_cbegin(page)) {
            // drain from the end of the page
            page->write_pos -= iter.io.iov_len;
            n_drained += iter.io.iov_len;
        } else {
            // drain from the middle of the page
            if (page_is_writable(page)) {
                size_t const n_bytes = (char const*)page_write_cbegin(page) - iov_end;
                memmove(iter.io.iov_base, iov_end, n_bytes);
                page->write_pos -= n_bytes;
            } else {
                // FIXME
                abort();
            }
        }
    } while (iter_next_page(&iter));

    // remove dead pages
    // maybe recycle a page
    if (buffer_count_pages(buf) > 0) {
        struct bfy_page* keep = pages_begin(buf);
        struct bfy_page const* const end = pages_cend(buf);
        struct bfy_page recycle = InitPage;
        for (struct bfy_page* walk = keep; walk != end; ++walk) {
            if (page_get_content_len(walk) > 0) {
                *keep++ = *walk;
            } else if (do_recycle && page_is_recyclable(walk) && walk->size > recycle.size) {
                if (do_release) {
                    page_release(&recycle);
                }
                recycle = *walk;
            } else if (do_release) {
                page_release(walk);
            }
        }
        if (recycle.size > 0) {
            *keep++ = recycle;
        }
        buf->n_pages = keep - pages_cbegin(buf);
    }

    // if we've drained everything, remove the page containers
    if (buf->n_pages == 0 && buf->pages != NULL) {
        allocator.free(buf->pages);
        buf->pages = NULL;
        buf->n_pages_alloc = 0;
    }

    assert(n_drained == (end.content_pos - begin.content_pos));
    buffer_record_content_removed(buf, n_drained);
    return n_drained;
}

size_t
bfy_buffer_drain_range(bfy_buffer* buf, size_t begin, size_t end) {
    return buffer_drain_range(buf,
                              buffer_get_pos(buf, begin),
                              buffer_get_pos(buf, end),
                              0);
}

size_t
bfy_buffer_drain(bfy_buffer* buf, size_t len) {
    return bfy_buffer_drain_range(buf, 0, len);
}

static size_t
buffer_drain_all(bfy_buffer* const buf, int flags) {
    return buffer_drain_range(buf,
                              buffer_get_pos(buf, 0),
                              buffer_get_pos(buf, SIZE_MAX),
                              flags);
}

size_t
bfy_buffer_drain_all(bfy_buffer* buf) {
    return bfy_buffer_drain_range(buf, 0, SIZE_MAX);
}

/// copyout

static size_t
buffer_copyout(bfy_buffer const* buf,
               struct bfy_pos begin,
               struct bfy_pos end,
               void* setme) {
    char* tgt = setme;

    struct bfy_iter iter;
    if (iter_begin(&iter, buf, begin, end)) do {
        memcpy(tgt, iter.io.iov_base, iter.io.iov_len);
        tgt += iter.io.iov_len;
    } while (iter_next_page(&iter));

    size_t const copied_len = tgt - (const char*)setme;
    assert(copied_len == (end.content_pos - begin.content_pos));
    return copied_len;
}

size_t
bfy_buffer_copyout_range(bfy_buffer const* buf,
                         size_t begin, size_t end,
                         void* setme) {
    return buffer_copyout(buf,
                          buffer_get_pos(buf, begin),
                          buffer_get_pos(buf, end),
                          setme);
}
size_t
bfy_buffer_copyout(bfy_buffer const* buf, size_t len, void* setme) {
    return bfy_buffer_copyout_range(buf, 0, len, setme);
}

/// remove

static size_t
buffer_remove(bfy_buffer* buf, struct bfy_pos begin, struct bfy_pos end, void* data) {
    size_t const n_copied = buffer_copyout(buf, begin, end, data);
    buffer_drain_range(buf, begin, end, 0);
    return n_copied;
}

size_t
bfy_buffer_remove_range(bfy_buffer* buf, size_t begin, size_t end, void* setme) {
    return buffer_remove(buf,
                         buffer_get_pos(buf, begin),
                         buffer_get_pos(buf, end),
                         setme);
}

size_t
bfy_buffer_remove(bfy_buffer* buf, size_t len, void* setme) {
    return bfy_buffer_remove_range(buf, 0, len, setme);
}

static void*
buffer_read_begin(bfy_buffer* buf) {
    return page_read_begin(pages_begin(buf));
}

char const*
bfy_buffer_peek_string(bfy_buffer* buf, size_t* setme_len) {
    // ensure the string we return is zero-terminated,
    // but don't commit the nul because the user may
    // keep building a string and we don't want embedded nuls
    bfy_buffer_mute_change_events(buf);
    char const nul = '\0';
    bfy_buffer_add_ch(buf, nul);
    bfy_buffer_make_all_contiguous(buf);
    struct bfy_page* page = pages_begin(buf);
    page->write_pos -= sizeof(nul);
    buffer_record_content_removed(buf, sizeof(nul));
    bfy_buffer_unmute_change_events(buf);

    if (setme_len != NULL) {
        *setme_len = page_get_content_len(page);
    }

    return buffer_read_begin(buf);
}

char*
bfy_buffer_remove_string(bfy_buffer* buf, size_t* setme_len) {
    if (setme_len != NULL) {
        *setme_len = bfy_buffer_get_content_len(buf);
    }

    bfy_buffer_begin_coalescing_change_events(buf);
    bfy_buffer_add_ch(buf, '\0');
    char* ret = NULL;

    // Plan A: if the whole buffer is in one contiguous malloc'ed
    // block, transfer ownership of that block to the caller
    bfy_buffer_make_all_contiguous(buf);
    if (buffer_count_pages(buf) == 1) {
        struct bfy_page* const page = pages_begin(buf);
        if (page_can_realloc(page)) {
            page_make_space_contiguous(page);
            ret = page_read_begin(page);
            buffer_drain_all(buf, DRAIN_FLAG_NORELEASE | DRAIN_FLAG_NORECYCLE);
        }
    }

    if (ret == NULL) {
        // Plan B: build a new string
        struct bfy_pos const begin = buffer_get_pos(buf, 0);
        struct bfy_pos const end = buffer_get_pos(buf, SIZE_MAX);
        size_t const wanted = end.content_pos - begin.content_pos;
        size_t moved_len = 0;
        ret = allocator.malloc(wanted);
        if (ret != NULL) {
            moved_len = buffer_remove(buf, begin, end, ret);
            assert(moved_len == wanted);
        }
    }

    bfy_buffer_end_coalescing_change_events(buf);
    return ret;
}

uint8_t
bfy_buffer_remove_ntoh_u8(struct bfy_buffer* buf) {
    uint8_t val = 0;
    size_t const len = bfy_buffer_remove(buf, sizeof(val), &val);
    if (len != sizeof(val)) {
        errno = ENOMSG;
    }
    return val;
}

uint16_t
bfy_buffer_remove_ntoh_u16(struct bfy_buffer* buf) {
    uint16_t val = 0;
    size_t const len = bfy_buffer_remove(buf, sizeof(val), &val);
    if (len == sizeof(val)) {
        val = ntoh16(val);
    } else {
        errno = ENOMSG;
    }
    return val;
}

uint32_t
bfy_buffer_remove_ntoh_u32(struct bfy_buffer* buf) {
    uint32_t val = 0;
    size_t const len = bfy_buffer_remove(buf, sizeof(val),  &val);
    if (len == sizeof(val)) {
        val = ntoh32(val);
    } else {
        errno = ENOMSG;
    }
    return val;
}

uint64_t
bfy_buffer_remove_ntoh_u64(struct bfy_buffer* buf) {
    uint64_t val = 0;
    size_t const len = bfy_buffer_remove(buf, sizeof(val), &val);
    if (len == sizeof(val)) {
        val = ntoh64(val);
    } else {
        errno = ENOMSG;
    }
    return val;
}

size_t
bfy_buffer_remove_buffer(bfy_buffer* buf, size_t wanted, bfy_buffer* tgt) {
    struct bfy_pos end = buffer_get_pos(buf, wanted);

    if (end.page_idx > 0 && end.content_pos > 0) {
        buffer_append_pages(tgt, pages_cbegin(buf), end.page_idx);
    }
    if (end.page_pos > 0) {
        struct bfy_page const* page = pages_cbegin(buf) + end.page_idx;
        bfy_buffer_add_pagebreak(tgt);
        bfy_buffer_add(tgt, page_read_cbegin(page), end.page_pos);
    }

    return buffer_drain_range(buf, buffer_get_pos(buf, 0), end, DRAIN_FLAG_NORECYCLE | DRAIN_FLAG_NORELEASE);
}

// make_contiguous

void*
bfy_buffer_make_contiguous(bfy_buffer* buf, size_t wanted) {
    struct bfy_pos const pos = buffer_get_pos(buf, wanted);

    // if the first page already holds wanted, then we're done
    if ((pos.page_idx == 0) || (pos.page_idx == 1 && pos.page_pos == 0)) {
        return buffer_read_begin(buf);
    }

    bfy_buffer_mute_change_events(buf);

    // if we have enough space, use it
    struct bfy_iovec space = bfy_buffer_peek_space(buf);
    if (space.iov_len >= pos.content_pos) {
        size_t const n_copied = buffer_copyout(buf, buffer_get_pos(buf, 0), pos, space.iov_base);
        bfy_buffer_commit_space(buf, n_copied);
        bfy_buffer_drain(buf, n_copied);
    } else {
        // make some new free space, use it, and prepend it
        int8_t* data = allocator.malloc(pos.content_pos);
        size_t const n_moved = buffer_remove(buf, buffer_get_pos(buf, 0), pos, data);
        struct bfy_page const newpage = {
            .data = data,
            .size = n_moved,
            .write_pos = n_moved
        };
        buffer_prepend_pages(buf, &newpage, 1);
    }

    bfy_buffer_unmute_change_events(buf);
    return buffer_read_begin(buf);
}

void*
bfy_buffer_make_all_contiguous(bfy_buffer* buf) {
    return bfy_buffer_make_contiguous(buf, SIZE_MAX);
}

/// search

static bool
buffer_contains_at(bfy_buffer const* buf, struct bfy_iter it,
                   void const* needle, size_t needle_len) {
    if (it.io.iov_len == 0) {
        return false;
    }
    if (it.io.iov_len >= needle_len) {
        return memcmp(it.io.iov_base, needle, needle_len) == 0;
    }
    if (memcmp(it.io.iov_base, needle, it.io.iov_len) != 0) {
        return false;
    }

    needle = (char const*)needle + it.io.iov_len;
    needle_len -= it.io.iov_len;
    if (!iter_next_page(&it)) {
        return false;
    }
    // FIXME: recursive function could be smashed
    return buffer_contains_at(buf, it, needle, needle_len);
}

static size_t
buffer_search_iovec(struct bfy_iovec const io,
                    void const* needle, size_t needle_len) {
    char const* const begin = io.iov_base;
    char const* const end = begin + io.iov_len;
    char const* walk = begin;

    while ((walk = memchr(walk, *(char const*)needle, end - walk))) {
        size_t const len = size_t_min(needle_len, end - walk);
        if (memcmp(walk, needle, len) == 0) {
            return walk - begin;
        }
        ++walk;
    }
    return io.iov_len;
}

static int
buffer_search_range(bfy_buffer const* buf,
                    struct bfy_pos begin, struct bfy_pos end,
                    void const* needle, size_t needle_len,
                    size_t* setme) {
    struct bfy_iter iter;

    if (!iter_begin(&iter, buf, begin, end)) {
        return -1;
    }
    while (iter.cur.content_pos < end.content_pos) {
      size_t const hit = buffer_search_iovec(iter.io, needle, needle_len);
      if (hit == iter.io.iov_len) {
          if (iter_next_page(&iter)) {
              continue;
          }
          break;
      }
      if (!iter_advance_n_bytes(&iter, hit)) {
          break;
      }
      if (buffer_contains_at(buf, iter, needle, needle_len)) {
          *setme = iter.cur.content_pos;
          return 0;
      }
      if (!iter_advance_n_bytes(&iter, 1)) {
          break;
      }
    }

    return -1;
}

int
bfy_buffer_search_range(bfy_buffer const* buf,
                        size_t begin, size_t end,
                        void const* needle, size_t needle_len,
                        size_t* setme_match) {
    return buffer_search_range(buf,
                               buffer_get_pos(buf, begin),
                               buffer_get_pos(buf, end),
                               needle, needle_len, setme_match);
}

int
bfy_buffer_search(bfy_buffer const* buf, size_t len,
                  void const* needle, size_t needle_len,
                  size_t* setme_match) {
    return bfy_buffer_search_range(buf, 0, len,
                                   needle, needle_len, setme_match);
}

int
bfy_buffer_search_all(bfy_buffer const* buf,
                      void const* needle, size_t needle_len,
                      size_t* setme_match) {
    return bfy_buffer_search_range(buf, 0, SIZE_MAX,
                                   needle, needle_len, setme_match);
}

/// life cycle

bfy_buffer
bfy_buffer_init(void) {
    bfy_buffer const buf = {
       .page = { 0 }
    };
    return buf;
}

bfy_buffer*
bfy_buffer_new(void) {
    bfy_buffer* buf = allocator.malloc(sizeof(bfy_buffer));
    if (buf != NULL) {
        *buf = bfy_buffer_init();
    }
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
bfy_buffer_new_unmanaged(void* space, size_t len) {
    bfy_buffer* buf = allocator.malloc(sizeof(bfy_buffer));
    if (buf != NULL) {
        *buf = bfy_buffer_init_unmanaged(space, len);
    }
    return buf;
}

void
bfy_buffer_destruct(bfy_buffer* buf) {
    buffer_drain_all(buf, DRAIN_FLAG_NORECYCLE);
}

void
bfy_buffer_free(bfy_buffer* buf) {
    bfy_buffer_destruct(buf);
    allocator.free(buf);
}
