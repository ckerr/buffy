/*
 * This file Copyright (C) 2009-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <libbuffy/buffer-endian.h>

#include "portable-endian.h"

int
bfy_buffer_add_hton_8(struct bfy_buffer* buf, uint8_t addme) {
    return bfy_buffer_add(buf, &addme, 1);
}

int
bfy_buffer_add_hton_16(struct bfy_buffer* buf, uint16_t addme) {
    uint16_t const be = htobe16(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

int
bfy_buffer_add_hton_32(struct bfy_buffer* buf, uint32_t addme) {
    uint32_t const be = htobe32(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

int
bfy_buffer_add_hton_64(struct bfy_buffer* buf, uint64_t addme) {
    uint64_t const be = htobe64(addme);
    return bfy_buffer_add(buf, &be, sizeof(be));
}

