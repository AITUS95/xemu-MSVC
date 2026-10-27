/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/m2mf.h"

int main(void)
{
    uint64_t span;
    assert(nv2a_m2mf_span(0, UINT32_MAX, UINT32_MAX, 0, 0, &span));
    assert(span == 0);
    assert(nv2a_m2mf_span(1, 0, 4, 1, 4, &span) && span == 4);
    assert(!nv2a_m2mf_span(1, 0, 4, 1, 3, &span));
    assert(!nv2a_m2mf_span(1, 0, 4, 0, 64, &span));
    assert(nv2a_m2mf_span(3, 16, 4, 2, 39, &span) && span == 39);
    assert(!nv2a_m2mf_span(3, 16, 4, 2, 38, &span));
    assert(!nv2a_m2mf_span(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
                           UINT64_MAX, &span));
    uint8_t src[256], dst[256];
    for (unsigned i = 0; i < sizeof(src); i++) {
        src[i] = i;
    }
    for (unsigned in = 1; in <= 4; in++) {
        for (unsigned out = 1; out <= 4; out++) {
            memset(dst, 0xcc, sizeof(dst));
            nv2a_m2mf_copy(dst + 1, src + 3, 3, 7, 32, 40, in, out);
            for (unsigned i = 0; i < sizeof(dst); i++) {
                unsigned relative = i ? i - 1 : UINT32_MAX;
                unsigned y = relative / 40, x = relative % 40;
                uint8_t expected = y < 3 && x % out == 0 && x / out < 7 ?
                                       src[3 + y * 32 + x / out * in] :
                                       0xcc;
                assert(dst[i] == expected);
            }
        }
    }
    puts("TAP version 13\n1..1\n"
         "ok 1 - M2MF bounds, overflow, strides, pitches and guards");
    return 0;
}
