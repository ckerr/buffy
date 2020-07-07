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

#include <stdarg.h>  /* va_list */
#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint8_t, uint16_t, uint32_t, uint64_t */

#ifdef __cplusplus
extern "C" {
#endif

struct bfy_buffer;

struct bfy_changed_cb_info {
    /* The number of bytes in this buffer when cb was last invoked. */
    size_t orig_size;

    /** The number of bytes added since cb was last invoked. */
    size_t n_added;

    /** The number of bytes removed or drained since cb was last invoked. */
    size_t n_deleted;
};

typedef void (bfy_changed_cb)(struct bfy_buffer*,
                              struct bfy_changed_cb_info const*,
                              void* cb_data);

typedef void (bfy_unref_cb)(void* data, size_t len, void* user_data);

#include <libbuffy/buffer-impl.h>

struct bfy_iovec {
    void* iov_base;
    size_t iov_len;
};

typedef struct bfy_buffer bfy_buffer;

/* LIFE CYCLE */

/**
 * Allocate a new heap-allocated buffer and initialize it.
 * 
 * @return a pointer to the new buffer, or NULL if an error occurred
 * @see bfy_buffer_free()
 */
bfy_buffer* bfy_buffer_new(void);

/**
 * Destructs a buffer and frees its memory.
 *
 *  @see bfy_buffer_new()
 */
void bfy_buffer_free(bfy_buffer*);

/**
 * Initialize an empty buffer and return it by value.
 *
 * If you are embedding bfy and ABI is not an issue, you can use this
 * to "placement new" a buffer on memory that is already allocated,
 * e.g. on the stack or aggregated in another struct or class.
 *
 * @see bfy_buffer_destruct()
 * @return an initialized buffer
 */
bfy_buffer bfy_buffer_init(void);

/**
 * Destroys a buffer created with bfy_buffer_init().
 *
 * @see bfy_buffer_init()
 */
void bfy_buffer_destruct(bfy_buffer* buf);

/**
 * Convenience function to create a new heap-allocated buffer and
 * give it an externally-managed block of space to use. bfy will
 * not attempt to resize or free this space.
 *
 * @param space an externally-managed block of space
 * @param len number of bytes in `space` block
 * @return a pointer to the new buffer, or NULL if an error occurred
 */
bfy_buffer* bfy_buffer_new_unmanaged(void* space, size_t len);

/**
 * Convenience function to initialize a new buffer with an
 * externally-managed block of space to use. bfy will not attempt
 * to resize or free this space.
 *
 * @param space an externally-managed block of space
 * @param len number of bytes in `space` block
 * @return an initialized buffer
 */
bfy_buffer bfy_buffer_init_unmanaged(void* space, size_t len);

/* ADDING CONTENT */

/**
 * Copy content to the end of a buffer.
 *
 * If the buffer's current page has enough free space or can be resized,
 * the new content will be appended there in contiguous memory. Otherwise,
 * a new page will be allocated for it. @see bfy_buffer_add_pagebreak().
 *
 * @param buf the buffer to be used
 * @param content pointer to the the content to be copied
 * @param len the number of bytes to be copied
 * @return 0 on success, -1 on failure.
 */
int bfy_buffer_add(bfy_buffer* buf, void const* content, size_t len);

/**
 * Copy a character to the end of a buffer.
 *
 * Equivalent to `bfy_buffer_add(buf, &ch, 1)`
 *
 * @param buf the buffer to be used
 * @param content the character to be copied
 * @return 0 on success, -1 on failure.
 */
int bfy_buffer_add_ch(bfy_buffer* buf, char addme);

/**
 * Add a read-only page of content to a buffer.
 *
 * Buffy will not memory-manage this buffer, nor will it attempt to
 * reuse the space to store new content once the current content has
 * been removed or drained.
 *
 * @param buf the buffer to which the content will be added
 * @param content pointer to the read-only content
 * @param len number of bytes in `content`
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_readonly(bfy_buffer* buf, void const* data, size_t len);

/**
 * Add an externally-managed page of memory to a buffer.
 *
 * Buffy will not memory-manage this buffer. Instead, when done with the
 * memory it will call the unref callback provided. This can be used to
 * work with data from external tools with their own memory management.
 *
 * @param buf the buffer to which the content will be added
 * @param content pointer to the read-only content
 * @param len number of bytes in `content`
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_reference(bfy_buffer* buf, void const* data, size_t len,
                             bfy_unref_cb* cb, void* cb_data);

/**
 * Add content to a buffer via a printf-style command.
 *
 * @see bfy_buffer_add()
 * @see bfy_buffer_add_vprintf()
 * @param buf the buffer to which the content will be added
 * @param fmt the printf-like format string
 * @param ... the varargs to be processed by `fmt`
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_printf(bfy_buffer* buf, char const* fmt, ...);

/**
 * Add content to a buffer via a vprintf-style command.
 *
 * @see bfy_buffer_add()
 * @see bfy_buffer_add_printf()
 * @param buf the buffer to which the content will be added
 * @param fmt the printf-like format string
 * @param args the varargs to be processed by `fmt`
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_vprintf(bfy_buffer* buf, char const* fmt, va_list args);

/**
 * Move content from one buffer to another.
 *
 * When possible this operation will move pages from one buffer
 * to the other, avoiding unnecessary memory allocation and copying.
 *
 * @see bfy_buffer_remove_buffer()
 * @param buf the buffer that will receive content
 * @param addme the buffer whose content will be moved to `buf`
 * @return 0 on success, or -1 on failure
 */
int bfy_buffer_add_buffer(bfy_buffer* buf, bfy_buffer* addme);

/**
 * Adds a network-endian number to the buffer.
 *
 * Convenience utility to convert a number into network-endian
 * format before adding it to the buffer. This function is
 * provided for consistency with the other endian functions,
 * although endian order is moot on a uint8_t.
 *
 * @see bfy_buffer_remove_ntoh_u8()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_hton_u8(bfy_buffer* buf, uint8_t addme);

/**
 * Adds a network-endian number to the buffer.
 *
 * Convenience utility to conver at number into network-endian
 * format before adding it to the buffer.
 *
 * @see bfy_buffer_remove_ntoh_u16()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_hton_u16(bfy_buffer* buf, uint16_t addme);

/**
 * Adds a network-endian number to the buffer.
 *
 * Convenience utility to conver at number into network-endian
 * format before adding it to the buffer.
 *
 * @see bfy_buffer_remove_ntoh_u32()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_hton_u32(bfy_buffer* buf, uint32_t addme);

/**
 * Adds a network-endian number to the buffer.
 *
 * Convenience utility to conver at number into network-endian
 * format before adding it to the buffer.
 *
 * @see bfy_buffer_remove_ntoh_u64()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_hton_u64(bfy_buffer* buf, uint64_t addme);

/**
 * Adds a page break to the buffer's internal bookkeeping.
 *
 * bfy buffers store content in a series of pages, where each page is
 * a * contiguous chunk of memory. Buffy prefers to keep memory contiguous
 * when possible -- for example, `bfy_buffer_add()` will try to resize the
 * current page instead of adding new pages. This function inserts a page
 * break such that subsequent content will be added to the next page.
 *
 * This is generally an implementation function, e.g. it is used by
 * `bfy_buffer_add_unmanaged()`, but it is included as public API for
 * users who want more fine-grained control of memory.
 *
 * @see bfy_buffer_add()
 * @param buf the buffer to which the page break will be added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_add_pagebreak(bfy_buffer* buf);

/* LOOKING AT CONTENT */

/**
 * Returns the number of bytes of content in the buffer.
 *
 * @param buf the buffer to inspect
 * @return the number of bytes of content in the buffer.
 */
size_t bfy_buffer_get_content_len(bfy_buffer const* buf);

/**
 * Peeks at the content inside a buffer without removing or draining it.
 *
 * The `vec_out` structures will be filled with pointers to one or
 * more pages inside the buffer.
 *
 * The total content in `vec_out` may exceed your request if there was
 * more content than you asked for in the last page. It may also be less
 * if the buffer had less than you requested or if there weren't enough
 * vecs to hold all of it.
 *
 * @param buf the buffer to inspect.
 * @param len the number of bytes of content to try to peek.
 * @param out an array of `n_out` bfy_iovecs.
 * @param n_vec the number of items in the `out` array. If `n_vec`
 *   is 0, we only count how many iovecs would be needed to point
 *   to the amount of requested data.
 * @return The number of iovecs needed. This may be less or more than
 *   `n_vec` if fewer or more iovecs were needed for the requested data.
 */
size_t bfy_buffer_peek(bfy_buffer const* buf, size_t len,
                       struct bfy_iovec* vec_out, size_t vec_len);

/**
 * Peeks at all the content inside a buffer.
 *
 * This is equivalent to `bfy_buffer_peek(buf, SIZE_MAX, vec_out, vec_len)`
 *
 * @see bfy_buffer_peek()
 * @param buf the buffer to inspect.
 * @param out an array of `n_out` bfy_iovecs.
 * @param n_vec the number of items in the `out` array. If `n_vec`
 *   is 0, we only count how many iovecs would be needed to point
 *   to the amount of requested data.
 * @return The number of iovecs needed. This may be less or more than
 *   `n_vec` if fewer or more iovecs were needed for the requested data.
 */
size_t bfy_buffer_peek_all(bfy_buffer const* buf,
                           struct bfy_iovec* vec_out, size_t vec_len);

/**
 * Returns a buffer-managed string of the buffer's contents.
 *
 * This can be a lower-overhead alternative to `bfy_buffer_remove_string()`
 * when you don't need ownership of the returned string.
 *
 * This function is not `const` because the buffer's contents may need
 * to be made contiguous before it can be returned as an unbroken string
 * and to ensure there is space to zero-terminate the string.
 *
 * @see bfy_buffer_remove_string()
 * @see bfy_buffer_add_printf()
 * @see bfy_buffer_add_vprintf()
 *
 * @param buf the buffer whose contents should be returned as a string
 * @param len pointer to a size_t which, if not NULL, is set with the strlen
 * @return a const pointer to a buffer-managed string
 */
char const* bfy_buffer_peek_string(bfy_buffer* buf, size_t* len);

/**
 * Copy content out from the buffer without removing it.
 *
 * Similar to `bfy_buffer_remove()` in that it lets you access the
 * buffer's contents; but unlike that function, this does not remove
 * content from the buffer.
 *
 * If a local is not needed, the `peek` functions may be more efficient
 * since they do not involve copying the content.
 *
 *
 * @see bfy_buffer_peek()
 * @see bfy_buffer_remove()
 * @see bfy_buffer_drain()
 * @param buf the buffer whose contents will be copied
 * @param begin offset into the buffer's contents to begin copying from
 * @param setme address where the content will be copied to
 * @param len number of bytes to copy from `buf` to `setme`
 * @return the number of bytes copied
 */
size_t bfy_buffer_copyout(bfy_buffer const* buf, size_t begin,
                          void* setme, size_t len);

/**
 * Searches for a string within a buffer.
 *
 * This is equivalent to
 * `bfy_buffer_search_range(buf, 0, SIZE_MAX, needle_needle_len, match)`
 *
 * @param buf the buffer to search
 * @param needle the string to search for
 * @param needle_len len the length of `needle`
 * @param match pointer to size_t offset that, if non-NULL, will be set
 *   to the location within the buffer if the search finds a match.
 * @return 0 if a match was found, -1 on failure.
 */
int bfy_buffer_search(bfy_buffer const* buf,
                      void const* needle, size_t needle_len,
                      size_t* match);

/**
 * Searches for a string within a buffer.
 *
 * @param buf the buffer to search
 * @param begin offset inside `buf` where the search should begin
 * @param end offset inside `buf` where the search should stop
 * @param needle the string to search for
 * @param needle_len len the length of `needle`
 * @param match pointer to size_t offset that, if non-NULL and a match
 *   is found, will be set to the offset in `buf` of the match.
 * @return 0 if a match was found, -1 on failure.
 */
int bfy_buffer_search_range(bfy_buffer const* buf,
                            size_t begin, size_t end,
                            void const* needle, size_t needle_len,
                            size_t* match);

/* CONSUMING CONTENT */

/**
 * Copy and remove the first `len` bytes of content from `buf`.
 *
 * @param buf the buffer to remove content from
 * @param setme where to copy the content to
 * @param len how many bytes of content to move from `buf` to `setme`
 * @return the number of bytes moved
 */
size_t bfy_buffer_remove(bfy_buffer* buf, void* setme, size_t len);

/**
 * Move data from one buffer to another.
 *
 * When possible this operation will move pages from one buffer
 * to the other, avoiding unnecessary memory allocation and copying.
 *
 * @see bfy_buffer_add_buffer()
 * @param buf the buffer to remove content from
 * @param setme the buffer to receive the content
 * @param len how many bytes of content to move from `buf` to `setme`
 * @return the number of bytes moved
 */
size_t bfy_buffer_remove_buffer(bfy_buffer* buf, bfy_buffer* setme, size_t len);

/**
 * Removes a network-endian number from the buffer.
 *
 * Convenience utility to convert a number from network-endian
 * into host-endian after removing it from the buffer.
 * This uint8_t function is provided for consistency with the other
 * endian functions, although endian order is moot on a uint8_t.
 *
 * @see bfy_buffer_add_hton_u8()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_remove_ntoh_u8(bfy_buffer* buf, uint8_t* setme);

/**
 * Removes a network-endian number from the buffer.
 *
 * Convenience utility to convert a number from network-endian
 * into host-endian after removing it from the buffer.
 *
 * @see bfy_buffer_add_hton_u16()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_remove_ntoh_u16(bfy_buffer* buf, uint16_t* setme);

/**
 * Removes a network-endian number from the buffer.
 *
 * Convenience utility to convert a number from network-endian
 * into host-endian after removing it from the buffer.
 *
 * @see bfy_buffer_add_hton_u32()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_remove_ntoh_u32(bfy_buffer* buf, uint32_t* setme);

/**
 * Removes a network-endian number from the buffer.
 *
 * Convenience utility to convert a number from network-endian
 * into host-endian after removing it from the buffer.
 *
 * @see bfy_buffer_add_hton_u64()
 * @param buf the buffer to which the content will be added
 * @param value host-endian number to be converted and added
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_remove_ntoh_u64(bfy_buffer* buf, uint64_t* setme);

/**
 * Removes the entire buffer as a newly-allocated string.
 *
 * This operation also drains the entire buffer.
 *
 * @see bfy_buffer_peek_string()
 * @see bfy_buffer_add_printf()
 * @see bfy_buffer_add_vprintf()
 *
 * @param buf the buffer to drain into a newly-allocated string
 * @param len pointer to a size_t which, if not NULL, is set with the strlen
 * @return pointer to a newly-allocated string
 */
char* bfy_buffer_remove_string(bfy_buffer* buf, size_t* len);

/* DRAINING CONTENT */

/**
 * Remove `len` bytes of content from the beginning of the buffer.
 *
 * @see bfy_buffer_remove()
 * @see bfy_buffer_copyout()
 * @param buf the buffer to remove content from
 * @param len how much content to remove, in bytes
 * @return how much content was removed, in bytes
 */
size_t bfy_buffer_drain(bfy_buffer* buf, size_t len);

/**
 * Drain all content from the buffer.
 *
 * Equivalent to `bfy_buffer_drain(buf, SIZE_MAX)`
 */
size_t bfy_buffer_drain_all(bfy_buffer* buf);

/* CHANGE NOTIFICATIONS */

/**
 * Set a callback to be invoked whenever the buffer's contents change.
 *
 * @param buf the buffer to watch
 * @param cb the callback to be invoked when the buffer's contents change
 * @param cb_data argument to be passed to the `cb` callback when called
 */
void bfy_buffer_set_changed_cb(bfy_buffer* buf, bfy_changed_cb* cb, void* cb_data);

/**
 * Begin coalescing change events.
 *
 * All content changes made while coalescing is enabled will be folded
 * into a single change event that fires as coalescing is disabled.
 * This can be useful when making a batch of changes that should be
 * seen as one single change.
 *
 * @see bfy_buffer_set_changed_cb()
 * @see bfy_buffer_end_coalescing_change_events()
 * @param buf the buffer whose change events were coalesced
 */
void bfy_buffer_begin_coalescing_change_events(bfy_buffer* buf);

/**
 * End coalescing change events.
 *
 * If content changes were made while coalescing was enabled, they will
 * all be folded into a single change event notification that is made
 * when coalescing ends.
 *
 * @see bfy_buffer_set_changed_cb()
 * @see bfy_buffer_begin_coalescing_change_events()
 * @param buf the buffer whose change events were coalesced
 */
void bfy_buffer_end_coalescing_change_events(bfy_buffer* buf);


/* MEMORY MANAGEMENT */

/**
 * Makes the content at the beginning of a buffer contiguous.
 *
 * @param buf the buffer to make contiguous
 * @param len number of bytes to make contiguous
 * @return a pointer to the contiguous memory
 */
void* bfy_buffer_make_contiguous(bfy_buffer* buf, size_t len);

/**
 * Ensures the entire buffer's content is in contiguous memory.
 *
 * Equivalent to `bfy_buffer_make_contiguous(buf, SIZE_MAX)`
 *
 * @param buf the buffer to make contiguous
 * @return a pointer to the contiguous memory
 */
void* bfy_buffer_make_all_contiguous(bfy_buffer* buf);

/**
 * Ensures a buffer has free space available.
 *
 * Ensures that at least `len` bytes of free space are available
 * for writing at the end of the buffer.
 *
 * @param buf the buffer that will receive the free space
 * @param len size of the space wanted, in bytes
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_ensure_space(bfy_buffer* buf, size_t len);

/**
 * Returns how much free space is available in the buffer.
 *
 * @see bfy_buffer_get_content_len()
 * @param buf the buffer whose space should be returned
 * @return the size of the buffer's free space, in bytes
 */
size_t bfy_buffer_get_space_len(bfy_buffer const* buf);

/**
 * Reserves space at the end of the buffer.
 *
 * This function ensures that at least `len` bytes of free space are
 * available at the end of the buffer and returns an iovec to its address
 * so you can write to it directly or pass it to a data generator,
 * then commit your work to the buffer with `bfy_buffer_commit_space()`.
 *
 * It is an error to do anything that changes the buffer while you're
 * still working on uncommitted memory. Changing the buffer can invalidate
 * the pointers returned by peek/reserve.
 *
 * @see bfy_buffer_commit_space()
 * @see bfy_buffer_peek_space()
 * @param buf the buffer to be appended to
 * @param len the desired free space, in bytes
 * @return an iovec of the free space
 */
struct bfy_iovec bfy_buffer_reserve_space(bfy_buffer* buf, size_t len);

/**
 * Commits previously-reserved space.
 *
 * Use this to commit your work after writing to memory returned by
 * `bfy_buffer_reserve_space()` or `bfy_buffer_peek_space()`.
 * It's OK to commit fewer bytes than you reserved, or even to skip
 * committing altogether if you find that you didn't need the space
 * after all.
 *
 * @see bfy_buffer_reserve_space()
 * @see bfy_buffer_peek_space()
 * @param buf the buffer whose space is to be committed
 * @param len length of space to commit, in bytes
 * @return 0 on success, -1 on failure
 */
int bfy_buffer_commit_space(bfy_buffer* buf, size_t len);

/**
 * Reserves pre-existing free space at the end of the buffer.
 *
 * Simlar to `bfy_buffer_reserve_space()` but only returns the
 * free space that's already allocated. This function is not often
 * needed but can be useful if you want to write as much content as
 * possible on the current page before adding a new page.
 *
 * As with `bfy_buffer_reserve_space()`, you can commit some or all
 * of your work when done.
 *
 * It is an error to do anything that changes the buffer while you're
 * still working on uncommitted memory. Changing the buffer can invalidate
 * the pointers returned by peek/reserve.
 *
 * @see bfy_buffer_commit_space()
 * @see bfy_buffer_reserve_space()
 * @param buf the buffer to be appended to
 * @return an iovec of the pre-existing free space (if any).
 */
struct bfy_iovec bfy_buffer_peek_space(bfy_buffer* buf);

#ifdef __cplusplus
}
#endif

#endif  /* INCLUDE_LIBBUFFY_BUFFER_H_ */
