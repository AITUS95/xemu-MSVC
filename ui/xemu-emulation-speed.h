/*
 * xemu emulation timing / VBlank service-rate estimator.
 *
 * This measures the rate at which the emulator services the console's
 * VBlank heartbeat. It is NOT an accuracy, compatibility or per-game FPS
 * score. The VBlank source is host paced, so this is a useful timing
 * diagnostic, not proof that every emulated device is keeping up.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef XEMU_EMULATION_SPEED_H
#define XEMU_EMULATION_SPEED_H

#include <stdbool.h>
#include <stdint.h>

#define XEMU_SPEED_WINDOW_NS INT64_C(1000000000)
#define XEMU_SPEED_STALE_NS INT64_C(500000000)

typedef struct XemuEmulationSpeed {
    int64_t window_start_ns;
    int64_t last_vblank_ns;
    uint32_t vblanks_since_start;
    float percent;
    bool valid;
} XemuEmulationSpeed;

static inline void xemu_emulation_speed_reset(XemuEmulationSpeed *meter)
{
    meter->window_start_ns = 0;
    meter->last_vblank_ns = 0;
    meter->vblanks_since_start = 0;
    meter->percent = 0.0f;
    meter->valid = false;
}

/*
 * Call on each serviced VBlank, while holding the BQL. Time must be
 * monotonic host time; expected_interval_ns is 1/50 or 1/60 second.
 *
 * Counting serviced VBlanks (rather than submitted/presented GPU frames)
 * means that games internally capped to 30 FPS are not marked 50% speed.
 */
static inline void xemu_emulation_speed_vblank(XemuEmulationSpeed *meter,
                                               int64_t now_ns,
                                               int64_t expected_interval_ns,
                                               bool running)
{
    int64_t elapsed_ns;
    double sample;

    if (!running || now_ns <= 0 || expected_interval_ns <= 0) {
        xemu_emulation_speed_reset(meter);
        return;
    }

    if (meter->window_start_ns == 0 || now_ns <= meter->last_vblank_ns) {
        xemu_emulation_speed_reset(meter);
        meter->window_start_ns = now_ns;
        meter->last_vblank_ns = now_ns;
        return;
    }

    meter->last_vblank_ns = now_ns;
    meter->vblanks_since_start++;
    elapsed_ns = now_ns - meter->window_start_ns;
    if (elapsed_ns < XEMU_SPEED_WINDOW_NS) {
        return;
    }

    sample = (double)meter->vblanks_since_start *
             (double)expected_interval_ns * 100.0 / (double)elapsed_ns;
    /* Ignore implausible bursts from scheduling jitter; preserve slowdowns. */
    if (sample > 200.0) {
        sample = 200.0;
    }

    if (!meter->valid) {
        meter->percent = (float)sample;
    } else {
        meter->percent = 0.60f * meter->percent + 0.40f * (float)sample;
    }
    meter->valid = true;
    meter->window_start_ns = now_ns;
    meter->vblanks_since_start = 0;
}

/* A stalled VBlank source cannot be reported as a stale 100%. */
static inline bool xemu_emulation_speed_get(const XemuEmulationSpeed *meter,
                                            int64_t now_ns, float *percent)
{
    if (!meter->valid || !percent || now_ns < meter->last_vblank_ns ||
        now_ns - meter->last_vblank_ns > XEMU_SPEED_STALE_NS) {
        return false;
    }
    *percent = meter->percent;
    return true;
}

#endif /* XEMU_EMULATION_SPEED_H */
