/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NV2A_PGRAPH_M2MF_H
#define NV2A_PGRAPH_M2MF_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Validate the entire footprint before touching memory; never truncate a DMA
 * and then publish successful completion. DMA limits are inclusive. */
static inline bool nv2a_m2mf_span(uint32_t rows, uint32_t pitch, uint32_t bytes,
                                  uint32_t stride, uint64_t available,
                                  uint64_t *span)
{
    *span = 0;
    if (!rows || !bytes) {
        return true;
    }
    if (!stride) {
        return false;
    }
    uint64_t preceding = (uint64_t)(rows - 1) * pitch;
    uint64_t last = (uint64_t)(bytes - 1) * stride + 1;
    if (preceding > available || last > available - preceding) {
        return false;
    }
    *span = preceding + last;
    return true;
}

/* Call only after validating both complete footprints. The pointers already
 * include the source/destination offsets within their DMA objects. */
static inline void nv2a_m2mf_copy(uint8_t *dst, const uint8_t *src,
                                  uint32_t rows, uint32_t bytes,
                                  uint32_t src_pitch, uint32_t dst_pitch,
                                  uint32_t src_stride, uint32_t dst_stride)
{
    for (uint32_t y = 0; y < rows; y++) {
        if (src_stride == 1 && dst_stride == 1) {
            memmove(dst, src, bytes);
        } else {
            for (uint32_t x = 0; x < bytes; x++) {
                dst[(uint64_t)x * dst_stride] = src[(uint64_t)x * src_stride];
            }
        }
        if (y + 1 < rows) {
            src += src_pitch;
            dst += dst_pitch;
        }
    }
}
#endif
