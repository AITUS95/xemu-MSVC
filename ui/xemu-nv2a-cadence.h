/* Rates from existing NV2A profiling counters, not global Xbox speed.
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_NV2A_CADENCE_H
#define XEMU_NV2A_CADENCE_H

#include <stdbool.h>
#include <stdint.h>

#define XEMU_NV2A_POLL_NS INT64_C(250000000)
#define XEMU_NV2A_WINDOW_NS INT64_C(1000000000)
#define XEMU_NV2A_STALE_NS INT64_C(500000000)

typedef struct XemuNV2ACadence {
    int64_t window_start_ns;
    int64_t sample_ns;
    int64_t last_increment_us;
    uint32_t window_count;
    uint32_t last_count;
    unsigned int fps;
    float flip_stalls_per_second;
    bool valid;
} XemuNV2ACadence;

static inline void xemu_nv2a_cadence_reset(XemuNV2ACadence *meter)
{
    *meter = (XemuNV2ACadence){ 0 };
}

static inline void xemu_nv2a_cadence_sample(XemuNV2ACadence *meter,
                                            int64_t now_ns, uint32_t count,
                                            unsigned int fps,
                                            int64_t last_increment_us)
{
    if (now_ns <= 0) {
        xemu_nv2a_cadence_reset(meter);
        return;
    }
    if (!meter->sample_ns || now_ns <= meter->sample_ns ||
        now_ns - meter->sample_ns > XEMU_NV2A_STALE_NS ||
        count < meter->last_count) {
        /* Counter rollback, including wrap, is conservatively unavailable. */
        xemu_nv2a_cadence_reset(meter);
        meter->window_start_ns = now_ns;
        meter->window_count = count;
    }
    meter->sample_ns = now_ns;
    meter->last_count = count;
    meter->last_increment_us = last_increment_us;
    meter->fps = fps;
    int64_t elapsed_ns = now_ns - meter->window_start_ns;
    if (elapsed_ns >= XEMU_NV2A_WINDOW_NS) {
        meter->flip_stalls_per_second =
            (float)((double)(count - meter->window_count) * 1e9 / elapsed_ns);
        meter->valid = true;
        meter->window_start_ns = now_ns;
        meter->window_count = count;
    }
}

static inline bool xemu_nv2a_cadence_get(const XemuNV2ACadence *meter,
                                         int64_t now_ns, float *flip_stalls,
                                         unsigned int *fps, bool *fps_available)
{
    if (!meter->valid || !flip_stalls || !fps || !fps_available ||
        now_ns < meter->sample_ns ||
        now_ns - meter->sample_ns > XEMU_NV2A_STALE_NS) {
        return false;
    }
    *flip_stalls = meter->flip_stalls_per_second;
    *fps = meter->fps;
    int64_t now_us = now_ns / 1000;
    *fps_available =
        meter->last_increment_us > 0 && meter->last_increment_us <= now_us &&
        now_us - meter->last_increment_us <= XEMU_NV2A_STALE_NS / 1000;
    return true;
}

#endif /* XEMU_NV2A_CADENCE_H */
