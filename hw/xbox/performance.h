/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_PERFORMANCE_H
#define XEMU_PERFORMANCE_H

#include "hw/xbox/performance-stats.h"

typedef struct QemuThread QemuThread;

typedef struct XemuPerfAudioFrame {
    unsigned generation;
    int64_t start_us;
    uint64_t phase_us[XEMU_PERF_PHASES];
    bool gp_enabled, ep_enabled;
    bool gp_realtime, ep_realtime;
} XemuPerfAudioFrame;

/* Control/sampling functions require the BQL. */
void xemu_perf_init(void);
void xemu_perf_invalidate(void);
void xemu_perf_set_thread(enum XemuPerfThread id, QemuThread *thread);
void xemu_perf_set_enabled(bool enabled);
XemuPerfResult xemu_perf_sample(void);

/* APU worker only. A generation token rejects work spanning resets/pauses. */
XemuPerfAudioFrame xemu_perf_audio_begin(void);
int64_t xemu_perf_audio_clock(const XemuPerfAudioFrame *frame);
void xemu_perf_audio_end(const XemuPerfAudioFrame *frame, unsigned samples);
void xemu_perf_audio_idle(void);
#endif
