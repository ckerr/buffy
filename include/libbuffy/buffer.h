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
#include <stdint.h>  // uint8_t, uint16_t, uint32_t, uint64_t

#include <libbuffy/buffer-impl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (bfy_unref_cb)(void* data, size_t len, void* user_data);

struct bfy_iovec {
    void* iov_base;
    size_t iov_len;
};

typedef struct bfy_buffer bfy_buffer;

bfy_buffer bfy_buffer_init(void);
bfy_buffer* bfy_buffer_new(void);
bfy_buffer bfy_buffer_init_unmanaged(void* space, size_t len);
bfy_buffer* bfy_buffer_new_unmanaged(void* space, size_t len);
void bfy_buffer_destruct(bfy_buffer* buf);
void bfy_buffer_free(bfy_buffer*);

size_t bfy_buffer_get_content_len(bfy_buffer const* buf);
size_t bfy_buffer_peek(bfy_buffer const* buf, size_t len,
                       struct bfy_iovec* vec_out, size_t vec_len);
size_t bfy_buffer_peek_all(bfy_buffer const* buf,
                           struct bfy_iovec* vec_out, size_t vec_len);

bool bfy_buffer_add(bfy_buffer* buf, void const* addme, size_t len);
bool bfy_buffer_add_ch(bfy_buffer* buf, char addme);
bool bfy_buffer_add_readonly(bfy_buffer* buf, const void* data, size_t len);
bool bfy_buffer_add_reference(bfy_buffer* buf, const void* data, size_t len,
                              bfy_unref_cb* cb, void* cb_data);
bool bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...);
bool bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args);
bool bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* addme);
bool bfy_buffer_add_pagebreak(bfy_buffer* buf);
bool bfy_buffer_add_hton_u8(bfy_buffer* buf, uint8_t addme);
bool bfy_buffer_add_hton_u16(bfy_buffer* buf, uint16_t addme);
bool bfy_buffer_add_hton_u32(bfy_buffer* buf, uint32_t addme);
bool bfy_buffer_add_hton_u64(bfy_buffer* buf, uint64_t addme);

bool bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* setme, size_t len);
char* bfy_buffer_remove_string(bfy_buffer* buf, size_t* len);
size_t bfy_buffer_remove(bfy_buffer* buf, void* data, size_t len);
bool bfy_buffer_remove_ntoh_u8(bfy_buffer* buf, uint8_t* setme);
bool bfy_buffer_remove_ntoh_u16(bfy_buffer* buf, uint16_t* setme);
bool bfy_buffer_remove_ntoh_u32(bfy_buffer* buf, uint32_t* setme);
bool bfy_buffer_remove_ntoh_u64(bfy_buffer* buf, uint64_t* setme);
size_t bfy_buffer_copyout(bfy_buffer const* buf, void* vdata, size_t len);

size_t bfy_buffer_drain(bfy_buffer* buf, size_t len);
size_t bfy_buffer_drain_all(bfy_buffer* buf);

void* bfy_buffer_make_contiguous(bfy_buffer* buf, size_t len);
void* bfy_buffer_make_all_contiguous(bfy_buffer* buf);

struct bfy_iovec bfy_buffer_peek_space(bfy_buffer* buf);
struct bfy_iovec bfy_buffer_reserve_space(bfy_buffer* buf, size_t len);
size_t bfy_buffer_commit_space(bfy_buffer* buf, size_t len);

size_t bfy_buffer_get_space_len(bfy_buffer const* buf);
bool bfy_buffer_ensure_space(bfy_buffer* buf, size_t len);
bool bfy_buffer_expand(bfy_buffer* buf, size_t len);

bool bfy_buffer_search(bfy_buffer const* buf,
                       void const* needle, size_t needle_len,
                       size_t* match);

bool bfy_buffer_search_range(bfy_buffer const* buf,
                             size_t begin, size_t end,
                             void const* needle, size_t needle_len,
                             size_t* match);

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BUFFER_H_
