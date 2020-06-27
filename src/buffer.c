/*
 * This file Copyright (C) 2009-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <libbuffy/buffer.h>

#include <assert.h>
#include <string.h>  // memcpy()

const struct bfy_buffer BfyBufferInit = {
    .memory.data = NULL,
    .memory.capacity = 0,
    .allocator.resize = bfy_heap_resize,
    .allocator.free = bfy_heap_free,
    .length = 0
};

void bfy_buffer_construct(bfy_buffer* buf)
{
    *buf = BfyBufferInit;
}

void bfy_buffer_destruct(bfy_buffer* buf)
{
    buf->allocator.free(&buf->memory);
}

void bfy_buffer_reserve(bfy_buffer* buf, size_t n)
{
    buf->allocator.resize(&buf->memory, n);
}

void bfy_buffer_reserve_available(bfy_buffer* buf, size_t n)
{
    size_t const available = buf->memory.capacity - buf->length;
    if (available < n)
    {
        bfy_buffer_reserve(buf, buf->length + n);
    }
}

void bfy_buffer_add(bfy_buffer* buf, void const* addme, size_t n)
{
    bfy_buffer_reserve_available(buf, n);
    assert(buf->length + n <= buf->memory.capacity);
    memcpy(bfy_buffer_end(buf), addme, n);
    buf->length += n;
    // fprintf(stderr, "[%*.*s] -> [%*.*s]\n", (int)n, (int)n, addme, (int)buf->length, (int)buf->length, buf->data);
    assert(buf->length <= buf->memory.capacity);
}

void bfy_buffer_add_ch(bfy_buffer* buf, char ch)
{
    bfy_buffer_reserve_available(buf, 1);
    ((char*)buf->memory.data)[buf->length++] = ch;
}

const void* bfy_buffer_cbegin(bfy_buffer const* buf)
{
    return buf->memory.data;
}

const void* bfy_buffer_cend(bfy_buffer const* buf)
{
    return bfy_buffer_cbegin(buf) + buf->length;
}

void* bfy_buffer_begin(bfy_buffer * buf)
{
    return buf->memory.data;
}

void* bfy_buffer_end(bfy_buffer* buf)
{
    return bfy_buffer_begin(buf) + buf->length;
}

char* bfy_buffer_destruct_to_string(bfy_buffer* buf, size_t* length)
{
    if (length != NULL)
    {
        *length = buf->length;
    }

    bfy_buffer_add_ch(buf, '\0');
    void* const ret = buf->memory.data;
    buf->length = 0;
    buf->memory.capacity = 0;
    buf->memory.data = NULL;
    return ret;
}

#if 0

    = NULL;

void bfy_buffer_reset(bfy_buffer* buf)
{
    ibfy_free(buf->data);
    *buf = TrBufferInit;
}

void bfy_buffer_reserve(bfy_buffer* buf, size_t n)
{
    if (buf->capacity < n)
    {
        size_t const MinCapacity = 64;
        // size_t const old_capacity = buf->capacity;
        size_t new_capacity = MAX(MinCapacity, buf->capacity);

        while (new_capacity < n)
        {
            new_capacity *= 4u;
        }

        // fprintf(stderr, "realloc buf %p from %zu to %zu\n", buf, old_capacity, new_capacity);
        buf->data = bfy_realloc(buf->data, new_capacity);
        buf->capacity = new_capacity;
    }
}

void bfy_buffer_reserve_available(bfy_buffer* buf, size_t n)
{
    bfy_buffer_reserve(buf, buf->length + n);
}

void bfy_buffer_add_ch(bfy_buffer* buf, char ch)
{
    bfy_buffer_reserve_available(buf, 1);
    ((char*)buf->data)[buf->length++] = ch;
    TR_ASSERT(buf->length <= buf->capacity);
    // fprintf(stderr, "[%c] -> [%*.*s]\n", ch, (int)buf->length, (int)buf->length, buf->data);
}
void bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    bfy_buffer_add_vprintf(buf, fmt, args);
    va_end(args);
}

void bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args_in)
{
    // make sure that at least some space is available...
    bfy_buffer_reserve_available(buf, 64);

    for(;;)
    {
        va_list args;
        va_copy(args, args_in);
        int const available = buf->capacity - buf->length;
        int const n = vsnprintf(bfy_buffer_end(buf), available, fmt, args);
        va_end(args);

        if (n < available)
        {
            buf->length += n;
            break;
        }

        bfy_buffer_reserve_available(buf, n+1); // +1 for trailing '\0'
    }
    TR_ASSERT(buf->length < buf->capacity);
}
#endif
