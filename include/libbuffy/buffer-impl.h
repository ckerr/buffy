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

#ifndef INCLUDE_LIBBUFFY_BUFFER_IMPL_H_
#define INCLUDE_LIBBUFFY_BUFFER_IMPL_H_

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* int8_t */

/* Everything below is an implementation detail.
   Best to not rely on these details in your own code! */

enum {
    BFY_PAGE_FLAGS_UNMANAGED = (1<<0),
    BFY_PAGE_FLAGS_READONLY = (1<<1)
};

struct bfy_page {
    int8_t* data;
    size_t size;

    /* Where the next content will be read from.
       An offset into bfy_page.data.
       Should be <= bfy_page.write_pos */
    size_t read_pos;

    /* Where the next content will be written to.
       An offset into bfy_page.data.
       Should be <= bfy_page.size */
    size_t write_pos;

    int flags;

    /* called when bfy is done with `bfy_page.data`.
       @see bfy_buffer_add_reference() */
    bfy_unref_cb* unref_cb;
    void* unref_arg;
};

struct bfy_buffer {
    /* aggregate a page so that simple bufers won't need to
       allocate a pages array */
    struct bfy_page page;

    /* if more pages are needed, this is where they live */
    struct bfy_page* pages;
    size_t n_pages;
    size_t n_pages_alloc;

    /* number of content bytes in the entire buffer across all pages */
    size_t content_len;

    /* buffer-changed callback */
    bfy_changed_cb* changed_cb;

    /* user-specified pointer from bfy_buffer_set_changed_cb() */
    void* changed_data;

    /* accumulates the info that will be passed to changed_cb */
    struct bfy_changed_cb_info changed_info;

    /* When coalescing > 0, all change events will be folded into a single
       change event that fires when coalescing returns to 0. Used so a
       batch of changes can be represented as a single conceptual change */
    int changed_coalescing;

    /* Changes that occur while muted > 0 will not be emitted, regardless
       of the coalescing state. This is used to silence code which changes
       a buffer's internals but not the content itself, e.g.
       bfy_buffer_make_contiguous() */
    int changed_muted;
};

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

void bfy_buffer_mute_change_events(struct bfy_buffer* buf);

void bfy_buffer_unmute_change_events(struct bfy_buffer* buf);


#endif  /* INCLUDE_LIBBUFFY_BUFFER_IMPL_H_ */
