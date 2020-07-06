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

#include <stddef.h>  // size_t
#include <stdint.h>  // int8_t

#ifdef __cplusplus
extern "C" {
#endif


enum {
    BFY_PAGE_FLAGS_UNMANAGED = (1<<0),
    BFY_PAGE_FLAGS_READONLY = (1<<1)
};

struct bfy_page {
    int8_t* data;
    size_t size;

    size_t read_pos;
    size_t write_pos;

    int flags;

    void (*unref_cb)(void* data, size_t len, void* user_data);
    void* unref_arg;
};

struct bfy_buffer {
    struct bfy_page page;
    struct bfy_page* pages;
    size_t n_pages;
    size_t content_len;
};

struct bfy_pos {
    size_t page_idx;
    size_t page_pos;
    size_t content_pos;
};

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BUFFER_H_
