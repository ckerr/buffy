/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the Lesser GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#ifndef INCLUDE_LIBBUFFY_BUFFER_ENDIAN_H_
#define INCLUDE_LIBBUFFY_BUFFER_ENDIAN_H_

#include <libbuffy/buffer.h>

#include <stdbool.h>
#include <stdint.h>  // uint8_t, uint16_t, uint32_t, uint64_t

#ifdef __cplusplus
extern "C" {
#endif

bool bfy_buffer_add_hton_u8 (struct bfy_buffer* buf, uint8_t  addme);
bool bfy_buffer_add_hton_u16(struct bfy_buffer* buf, uint16_t addme);
bool bfy_buffer_add_hton_u32(struct bfy_buffer* buf, uint32_t addme);
bool bfy_buffer_add_hton_u64(struct bfy_buffer* buf, uint64_t addme);

bool bfy_buffer_remove_ntoh_u8 (struct bfy_buffer* buf, uint8_t* setme);
bool bfy_buffer_remove_ntoh_u16(struct bfy_buffer* buf, uint16_t* setme);
bool bfy_buffer_remove_ntoh_u32(struct bfy_buffer* buf, uint32_t* setme);
bool bfy_buffer_remove_ntoh_u64(struct bfy_buffer* buf, uint64_t* setme);

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_LIBBUFFY_BUFFER_ENDIAN_H_
