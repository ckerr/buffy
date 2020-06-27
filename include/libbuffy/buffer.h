/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#ifndef INCLUDE_LIBBUFFY_BUFFER_H_
#define INCLUDE_LIBBUFFY_BUFFER_H_

#include <stddef.h>  // size_t
#include <stdarg.h>  // va_list

#include <libbuffy/block.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bfy_buffer {
    struct bfy_block block;
    size_t write_pos;
}
bfy_buffer;

extern const struct bfy_buffer BfyBufferInit;

struct bfy_buffer bfy_buffer_init(void);
struct bfy_buffer bfy_buffer_init_unowned(void* data, size_t size);
struct bfy_buffer bfy_buffer_init_with_block(bfy_block block);

int bfy_buffer_take_string(bfy_buffer* buf, char** str, size_t* strsize);
void bfy_buffer_destruct(bfy_buffer* buf);
void bfy_buffer_clear(bfy_buffer* buf);

size_t bfy_buffer_get_available(bfy_buffer const* buf);
size_t bfy_buffer_get_capacity(bfy_buffer const* buf);
size_t bfy_buffer_get_length(bfy_buffer const* buf);

void* bfy_buffer_begin(bfy_buffer* buf);
void* bfy_buffer_end(bfy_buffer* buf);
const void* bfy_buffer_cbegin(bfy_buffer const* buf);
const void* bfy_buffer_cend(bfy_buffer const* buf);

int bfy_buffer_reserve(bfy_buffer* buf, size_t size);
int bfy_buffer_reserve_available(bfy_buffer* buf, size_t size);

int bfy_buffer_add(bfy_buffer* buf, void const* addme, size_t n);
int bfy_buffer_add_ch(bfy_buffer* buf, char ch);
int bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...);
int bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in);


// convenience wrapper for `bfy_buffer_reserve(buf, bfy_buffer_get_length(buf) + size)`


#define BFY_HEAP_BUFFER(name) \
    bfy_buffer name = bfy_buffer_init();

#define BFY_STACK_BUFFER(name, size) \
    char name##_stack[size]; \
    bfy_buffer name = bfy_buffer_init_unowned(name##_stack, size);

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BUFFER_H_
