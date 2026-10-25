/*
 * Host-paced VBlank callback cadence, independent of rendered/guest FPS.
 * This is neither a global Xbox speed nor a hardware accuracy measurement.
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_VBLANK_CADENCE_H
#define XEMU_VBLANK_CADENCE_H

#include <stdbool.h>
#include <stdint.h>

#define XEMU_VBLANK_WINDOW_NS INT64_C(1000000000)
#define XEMU_VBLANK_STALE_NS INT64_C(500000000)

typedef struct XemuVBlankCadence {
    int64_t window_start_ns;
    int64_t last_vblank_ns;
    int64_t expected_interval_ns;
    uint32_t intervals;
    float percent;
    bool valid;
} XemuVBlankCadence;

static inline void xemu_vblank_cadence_reset(XemuVBlankCadence *meter)
{
    meter->window_start_ns = 0;
    meter->last_vblank_ns = 0;
    meter->expected_interval_ns = 0;
    meter->intervals = 0;
    meter->percent = 0.0f;
    meter->valid = false;
}

/*
 * One call per completed VBlank callback under the BQL. Use monotonic host
 * time and the configured nominal PAL/NTSC interval, not presented frames.
 */
static inline void xemu_vblank_cadence_event(XemuVBlankCadence *meter,
                                             int64_t now_ns,
                                             int64_t expected_interval_ns,
                                             bool running)
{
    if (!running || now_ns <= 0 || expected_interval_ns <= 0) {
        xemu_vblank_cadence_reset(meter);
        return;
    }

    if (!meter->window_start_ns ||
        expected_interval_ns != meter->expected_interval_ns ||
        now_ns <= meter->last_vblank_ns ||
        now_ns - meter->last_vblank_ns > XEMU_VBLANK_STALE_NS) {
        xemu_vblank_cadence_reset(meter);
        meter->window_start_ns = now_ns;
        meter->last_vblank_ns = now_ns;
        meter->expected_interval_ns = expected_interval_ns;
        return;
    }

    meter->last_vblank_ns = now_ns;
    meter->intervals++;
    int64_t elapsed_ns = now_ns - meter->window_start_ns;
    if (elapsed_ns < XEMU_VBLANK_WINDOW_NS) {
        return;
    }

    double sample = (double)meter->intervals * expected_interval_ns * 100.0 /
                    (double)elapsed_ns;
    meter->percent = meter->valid ?
                         0.60f * meter->percent + 0.40f * (float)sample :
                         (float)sample;
    meter->valid = true;
    meter->window_start_ns = now_ns;
    meter->intervals = 0;
}

/* Unavailable during warmup or after a stalled/discontinuous source. */
static inline bool xemu_vblank_cadence_get(const XemuVBlankCadence *meter,
                                           int64_t now_ns, float *percent)
{
    if (!meter->valid || !percent || now_ns < meter->last_vblank_ns ||
        now_ns - meter->last_vblank_ns > XEMU_VBLANK_STALE_NS) {
        return false;
    }
    *percent = meter->percent;
    return true;
}

#endif /* XEMU_VBLANK_CADENCE_H */
