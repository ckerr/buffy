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

#ifndef INCLUDE_LIBBUFFY_BUFFER_H_
#define INCLUDE_LIBBUFFY_BUFFER_H_

#include <stdarg.h>  // va_list
#include <stdbool.h>  // bool
#include <stddef.h>  // size_t

#include <libbuffy/buffer-impl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bfy_iovec {
    void* iov_base;
    size_t iov_len;
};

typedef struct bfy_buffer bfy_buffer;

struct bfy_buffer bfy_buffer_init(void);
struct bfy_buffer* bfy_buffer_new(void);
struct bfy_buffer bfy_buffer_init_unmanaged(void* data, size_t size);
struct bfy_buffer* bfy_buffer_new_unmanaged(void* data, size_t size);
void bfy_buffer_destruct(bfy_buffer* buf);
void bfy_buffer_free(bfy_buffer*);

size_t bfy_buffer_get_content_len(bfy_buffer const* buf);
size_t bfy_buffer_peek(bfy_buffer const* buf, size_t size, struct bfy_iovec* vec_out, size_t n_vec);
size_t bfy_buffer_peek_all(bfy_buffer const* buf, struct bfy_iovec* vec_out, size_t n_vec);

bool bfy_buffer_add(bfy_buffer* buf, void const* addme, size_t n);
bool bfy_buffer_add_ch(bfy_buffer* buf, char ch);
bool bfy_buffer_add_readonly(bfy_buffer* buf, const void* data, size_t size);
bool bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...);
bool bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in);
bool bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* src);
bool bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* tgt, size_t len);

void bfy_buffer_reset(bfy_buffer* buf);
bool bfy_buffer_drain(bfy_buffer* buf, size_t len);

void* bfy_buffer_make_contiguous(bfy_buffer* buf, size_t size);
void* bfy_buffer_make_all_contiguous(bfy_buffer* buf);

char* bfy_buffer_remove_string(bfy_buffer* buf, size_t* len);
size_t bfy_buffer_remove(bfy_buffer* buf, void* data, size_t n_wanted);
size_t bfy_buffer_copyout(bfy_buffer const* buf, void* vdata, size_t n_wanted);

struct bfy_iovec bfy_buffer_peek_space(struct bfy_buffer* buf);
struct bfy_iovec bfy_buffer_reserve_space(struct bfy_buffer* buf, size_t size);
bool bfy_buffer_commit_space(struct bfy_buffer* buf, size_t size);

size_t bfy_buffer_get_space_len(bfy_buffer const* buf);
bool bfy_buffer_ensure_space(bfy_buffer* buf, size_t size);
bool bfy_buffer_expand(bfy_buffer* buf, size_t size);

void bfy_buffer_add_chain(struct bfy_buffer* buf);

#if 0
int bfy_buffer_take_string(bfy_buffer* buf, char** str, size_t* strsize);
void bfy_buffer_clear(bfy_buffer* buf);
int bfy_buffer_reserve(bfy_buffer* buf, size_t size);
int bfy_buffer_reserve_available(bfy_buffer* buf, size_t size);


#define BFY_HEAP_BUFFER(name) \
    bfy_buffer name = bfy_buffer_init();

#define BFY_STACK_BUFFER(name, size) \
    char name##_stack[size]; \
    bfy_buffer name = bfy_buffer_init_unmanaged(name##_stack, size);
#endif

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BUFFER_H_
