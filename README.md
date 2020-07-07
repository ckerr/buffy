# libbuffy

libbuffy is an embeddable, MIT-licensed, C-language, zero-dependency
memory buffer class inspired by libevent's `evbuffer`. It consists of
just a few files that can be dropped into your own project as-is.

Some common uses:

* A pipeline between data producers and consumers
* Queueing data that's been received or about to be sent over the network
* Building strings or data buffers without worrying about memory management

Buffy is designed to be as efficient as possible despite being a
general-purpose tool. It avoids unnecessary memory allocations and
copies when possible.

## Concepts: Pages, Content, and Space

bfy buffers are implemented using an array of separate pages, where a
**page** is a chunk of contiguous memory. Page memory that's been written
into via `bfy_buffer_add*()` is the **content**, while the as-yet-unused
memory at the end of the last page is the buffer's free **space**. These
terms are reflected in the API, e.g. `bfy_buffer_get_content_len()`.

Most of the time, no special steps are needed for efficient use.
bfy will use space available when possible and recycle leftover memory
from content that's already been consumed by `bfy_buffer_remove*()` or
`bfy_buffer_drain*()`.

Sometimes, though, you may have data with special constraints:
it may be read-only, or it may be memory-managed by someone else
and bfy shouldn't free it, or you may want to transfer it from a different
buffer. You can add these with the functions `bfy_buffer_add_reference()`,
`bfy_buffer_add_readonly()`, or `bfy_buffer_add_buffer()`. These all create
new pages in the buffer's array of pages so pre-existing content
can be embedded into the buffer without the overhead of cloning it.

## API

### Life Cycle

Buffers can be instantiated on the heap:

```c
bfy_buffer* bfy_buffer_new(void);
void bfy_buffer_free(bfy_buffer*);
```

If you're building bfy inside your own project such that bfy's ABI is
not a concern, you can also instantiate it on the stack or aggregate
it in a struct to avoid another malloc/free pair for the `bfy_buffer`
struct itself:

```c
bfy_buffer bfy_buffer_init(void);
void bfy_buffer_destruct(bfy_buffer*);
```

### Adding Data

```c
size_t bfy_buffer_add(bfy_buffer* buf, void const* addme, size_t len);
size_t bfy_buffer_add_ch(bfy_buffer* buf, char addme);
size_t bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...);
size_t bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args);
size_t bfy_buffer_add_hton_u8 (bfy_buffer* buf, uint8_t  addme);
size_t bfy_buffer_add_hton_u16(bfy_buffer* buf, uint16_t addme);
size_t bfy_buffer_add_hton_u32(bfy_buffer* buf, uint32_t addme);
size_t bfy_buffer_add_hton_u64(bfy_buffer* buf, uint64_t addme);
```

Of these functions, `bfy_buffer_add()` is the key: it copies the specified
bytes into the free space at the end of the last page, marking that space
as content. If not enough space is available, more is allocated. The number
of bytes added is returned.

The rest are convenience wrappers: `add_ch()` adds a single character;
`add_printf() / add_vprintf()` are printf-like functions that add to the
end of the buffer, and `add_hton_u*()` will convert numbers into big-endian
network byte order before ading them to he buffer.. Like `bfy_buffer_add()`,
these all try to append to the end of the current page.

```c
size_t bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* src);
size_t bfy_buffer_add_readonly(bfy_buffer* buf, const void* data, size_t len);
size_t bfy_buffer_add_reference(bfy_buffer* buf, const void* data, size_t len,
                                bfy_unref_cb* cb, void* user_data);
```

These functions add pre-existing content into the buffer by embedding it
rather than duplicating it.

`add_buffer()` transfers the contents of one buffer to another.

`add_reference()` adds a new page that embeds content managed outside of bfy.
When bfy is done with the page, the unref callback passed to `add_reference()`
is called.

`add_readonly()` adds a new page that embeds read-only content.

## Removing Data

```c
size_t bfy_buffer_remove(bfy_buffer* buf, void* setme, size_t len);
char* bfy_buffer_remove_string(bfy_buffer* buf, size_t* len);
int bfy_buffer_remove_ntoh_u8 (bfy_buffer* buf, uint8_t* setme);
int bfy_buffer_remove_ntoh_u16(bfy_buffer* buf, uint16_t* setme);
int bfy_buffer_remove_ntoh_u32(bfy_buffer* buf, uint32_t* setme);
int bfy_buffer_remove_ntoh_u64(bfy_buffer* buf, uint64_t* setme);
```

Of these functions, `bfy_buffer_remove()` is the key: it moves the
next `len` bytes of content from the front of the buffer to the
specified location and returns the number of bytes consumed. Just as
content is added with `bfy_buffer_add*()`, it is consumed with
`bfy_buffer_remove*()`.

The others are convenience wrappers: `bfy_buffer_remove_ntoh*()` read
numbers from big-endian network byte order into the host machine's
byte order, and `bfy_buffer_remove_string()` will remove the buffer
into a newly-allocated string.

```c
int bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* tgt, size_t len);
```

This moves the first `len` bytes of content from the source buffer to
the target buffer. Unnecessary memory copying is avoided as much as
possible.

```c
size_t bfy_buffer_drain(bfy_buffer* buf, size_t len);
size_t bfy_buffer_drain_all(bfy_buffer* buf);
size_t bfy_buffer_copyout(bfy_buffer const* buf, void* vdata, size_t len);
```

`bfy_buffer_drain()` and `bfy_buffer_drain_all()` remove content from
the buffer without copying it first. `bfy_buffer_copyout()` copies data
from the buffer without draining it afterwards.

## Searching

```c
int bfy_buffer_search(bfy_buffer const* buf,
                      void const* needle, size_t needle_len,
                      size_t* match);

int bfy_buffer_search_range(bfy_buffer const* buf,
                            size_t begin, size_t end,
                            void const* needle, size_t needle_len,
                            size_t* match);
```

`bfy_buffer_search_range()` searches for a string inside the [begin..end)
subset of the buffer's content. `bfy_buffer_search()` searches the entire
buffer, equivalent to `bfy_buffer_search_range(buf, 0, SIZE_MAX, ...)`

## Efficient Memory Management

### Preallocating Space

If you know how much space you're going to use, it makes sense to tell bfy
with `bfy_buffer_ensure_space()` so that it can be preallocated once in a
single page before you start filling it with `bfy_buffer_add*()`.

`bfy_buffer_get_space_len()` returns how much space is available.

```c
size_t bfy_buffer_get_space_len(bfy_buffer const* buf);
int bfy_buffer_ensure_space(bfy_buffer* buf, size_t len);
```

### Peek / Reserve / Commit

As an alternative to `bfy_buffer_ensure_space()` + `bfy_buffer_add*()`,
the reserve/commit API lets you work directly a buffer's free space.
`bfy_buffer_reserve_space(len)` preallocates `len` bytes of free space
and returns its address. You can write directly to this memory or pass
it along to a third party data generator, then commit your work to the
buffer with `bfy_buffer_commit_space()`. It's OK to commit fewer bytes
than you reserved, or even to skip committing altogether if you find
that you didn't need the space after all.

`bfy_buffer_peek_space()` similar to `bfy_buffer_reserve_space()` but
only returns the free space that's already allocated. This is not often
needed but can be useful if you want to write as much content as possible
on the current page before adding a new page.
As with `bfy_buffer_reserve_space()`, you can commit some or all of your
work when done.

It is an error to do anything that changes the buffer while you're still
working on uncommitted memory. Changing the buffer can invalidate the
pointers returned by peek/reserve.

```c
struct bfy_iovec bfy_buffer_reserve_space(bfy_buffer* buf, size_t len);
struct bfy_iovec bfy_buffer_peek_space(bfy_buffer* buf);
size_t bfy_buffer_commit_space(bfy_buffer* buf, size_t len);
```

### Contiguous / Non-contiguous Memory

As mentioned above in [Concepts](#concepts-pages-content-and-space),
buffers are made from a series of pages and a page is a contiguous
block of memory.

If you need to view multiple pages' worth of memory as a contiguous
block, `bfy_buffer_make_contiguous(len)` will ensure the first `len` bytes
of content are in a single page. `bfy_buffer_make_all_contiguous()` is a
convenience helper to make the entire buffer a single page.

Instead of the `contiguous()` functions, It is generally more efficient
to walk through content with `bfy_buffer_peek()` or `bfy_buffer_peek_all()`
instead, since that can be done without moving data into a single page.

User code won't often need it, but `bfy_buffer_add_pagebreak()` can be
used to force a pagebreak between calls to `bfy_buffer_add*()`.

```c
void* bfy_buffer_make_contiguous(bfy_buffer* buf, size_t len);
void* bfy_buffer_make_all_contiguous(bfy_buffer* buf);
size_t bfy_buffer_peek(bfy_buffer const* buf, size_t size,
                       struct bfy_iovec* vec_out, size_t vec_len);
size_t bfy_buffer_peek_all(bfy_buffer const* buf,
                           struct bfy_iovec* vec_out, size_t vec_len);
int bfy_buffer_add_pagebreak(bfy_buffer* buf);
```

## Comparison to `evbuffer`

libbuffy is inspired by
[libevent](https://libevent.org/)'s [evbuffer](http://www.wangafu.net/~nickm/libevent-book/Ref7_evbuffer.html),
so it's no surprise that they have similar APIs.
But the software world is full of event loop tools, so if you're using
[glib](https://github.com/GNOME/glib) or
[libuv](https://libuv.org/) or
[libev](http://software.schmorp.de/pkg/libev.html) or
[libhv](https://github.com/ithewei/libhv)
instead -- or none of the above! -- you may find libbuffy to be more accessible.

