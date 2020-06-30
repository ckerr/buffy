/*
 * This file Copyright (C) 2009-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <libbuffy/buffer.h>
#include <libbuffy/buffer-endian.h>

#include <inttypes.h>
#include <stdio.h>

#include "portable-endian.h"

bool
bfy_buffer_add_hton_u8(struct bfy_buffer* buf, uint8_t addme) {
    return bfy_buffer_add(buf, &addme, 1);
}

bool
bfy_buffer_add_hton_u16(struct bfy_buffer* buf, uint16_t addme) {
    uint16_t const be = htobe16(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

bool
bfy_buffer_add_hton_u32(struct bfy_buffer* buf, uint32_t addme) {
    uint32_t const be = htobe32(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

bool
bfy_buffer_add_hton_u64(struct bfy_buffer* buf, uint64_t addme) {
    uint64_t const be = htobe64(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

bool
bfy_buffer_remove_ntoh_u8(struct bfy_buffer* buf, uint8_t* setme) {
    return bfy_buffer_remove(buf, setme, sizeof(uint8_t));
}

#define REMOVE_NTOH_N(size) \
bool \
bfy_buffer_remove_ntoh_u##size(struct bfy_buffer* buf, uint##size##_t* setme) { \
    uint##size##_t tmp; \
    bool success; \
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) { \
        *setme = be##size##toh(tmp); \
    } \
    return success; \
}

REMOVE_NTOH_N(16)
REMOVE_NTOH_N(32)
REMOVE_NTOH_N(64)

#if 0
bool
bfy_buffer_remove_ntoh_16(struct bfy_buffer* buf, uint16_t* setme) {
    uint16_t tmp;
    bool success;
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) {
        *setme = be16toh(tmp);
    }
    return success;
}

bool
bfy_buffer_remove_ntoh_32(struct bfy_buffer* buf, uint32_t* setme) {
    uint32_t tmp;
    bool success;
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) {
        *setme = be32toh(tmp);
    }
    return success;
}

bool
bfy_buffer_remove_ntoh_64(struct bfy_buffer* buf, uint64_t* setme) {
    uint64_t tmp;
    bool success;
    if ((success = bfy_buffer_remove(buf, &tmp, sizeof(tmp)))) {
        *setme = be64toh(tmp);
    }
    return success;
}
#endif
