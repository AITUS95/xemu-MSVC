/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_PERFORMANCE_STATS_H
#define XEMU_PERFORMANCE_STATS_H

#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define XEMU_PERF_THREADS 3
#define XEMU_PERF_PHASES 4
#define XEMU_PERF_HISTORY 4
#define XEMU_PERF_INTERVAL_US 250000
#define XEMU_PERF_STALE_US 1000000

enum XemuPerfThread {
    XEMU_PERF_CPU,
    XEMU_PERF_NV2A,
    XEMU_PERF_APU,
};

enum XemuPerfPhase {
    XEMU_PERF_AUDIO_TOTAL,
    XEMU_PERF_VP,
    XEMU_PERF_GP,
    XEMU_PERF_EP,
};

typedef struct XemuPerfCounters {
    int64_t time_us;
    unsigned generation;
    uint64_t audio_generation;
    int64_t cpu_ns[XEMU_PERF_THREADS]; /* -1 if unavailable */
    uint64_t sample_frames;
    uint64_t phase_us[XEMU_PERF_PHASES];
    int64_t audio_updated_us;
    bool audio_active;
    bool gp_enabled, ep_enabled;
    bool gp_realtime, ep_realtime;
} XemuPerfCounters;

typedef struct XemuPerfResult {
    bool ready;
    bool cpu_valid[XEMU_PERF_THREADS];
    double cpu_percent[XEMU_PERF_THREADS];
    bool audio_valid;
    bool audio_stale;
    double audio_percent;
    double audio_budget_percent;
    double phase_percent[XEMU_PERF_PHASES];
    XemuPerfCounters current;
} XemuPerfResult;

typedef struct XemuPerfHistory {
    XemuPerfCounters samples[XEMU_PERF_HISTORY];
    unsigned count, next;
} XemuPerfHistory;

enum XemuPerfRateBand {
    XEMU_PERF_RATE_UNAVAILABLE,
    XEMU_PERF_RATE_SLOW,
    XEMU_PERF_RATE_NORMAL,
    XEMU_PERF_RATE_FAST,
};

/* A completed audio cadence is not a measurement of overall game speed. */
static inline enum XemuPerfRateBand
xemu_perf_audio_rate_band(const XemuPerfResult *stats)
{
    if (!stats->current.generation || !stats->ready || !stats->audio_valid ||
        !stats->current.audio_active || stats->audio_stale ||
        !stats->current.gp_enabled || !stats->current.ep_enabled ||
        !stats->current.gp_realtime || !stats->current.ep_realtime ||
        !(stats->audio_percent >= 0 && stats->audio_percent <= DBL_MAX)) {
        return XEMU_PERF_RATE_UNAVAILABLE;
    }
    if (stats->audio_percent < 95.0) {
        return XEMU_PERF_RATE_SLOW;
    }
    if (stats->audio_percent > 105.0) {
        return XEMU_PERF_RATE_FAST;
    }
    return XEMU_PERF_RATE_NORMAL;
}

/* Pure reducer: shared by the UI sampler and deterministic unit tests. */
static inline XemuPerfResult xemu_perf_compare(const XemuPerfCounters *before,
                                               const XemuPerfCounters *after)
{
    XemuPerfResult result = { 0 };
    int64_t elapsed = after->time_us - before->time_us;
    result.current = *after;
    if (!after->generation || before->generation != after->generation ||
        elapsed <= 0) {
        return result;
    }
    result.ready = true;
    for (unsigned i = 0; i < XEMU_PERF_THREADS; i++) {
        if (before->cpu_ns[i] >= 0 && after->cpu_ns[i] >= before->cpu_ns[i]) {
            result.cpu_valid[i] = true;
            result.cpu_percent[i] =
                (after->cpu_ns[i] - before->cpu_ns[i]) / (10.0 * elapsed);
        }
    }
    result.audio_stale =
        after->audio_active &&
        (after->audio_updated_us <= 0 ||
         after->time_us - after->audio_updated_us > XEMU_PERF_STALE_US);
    if (!after->audio_active || result.audio_stale ||
        before->audio_generation != after->audio_generation ||
        after->sample_frames <= before->sample_frames ||
        before->gp_enabled != after->gp_enabled ||
        before->ep_enabled != after->ep_enabled ||
        before->gp_realtime != after->gp_realtime ||
        before->ep_realtime != after->ep_realtime) {
        return result;
    }
    for (unsigned i = 0; i < XEMU_PERF_PHASES; i++) {
        if (after->phase_us[i] < before->phase_us[i]) {
            return result;
        }
    }
    double audio_us =
        (after->sample_frames - before->sample_frames) * (1000000.0 / 48000);
    result.audio_valid = true;
    result.audio_percent = 100.0 * audio_us / elapsed;
    result.audio_budget_percent =
        100.0 * (after->phase_us[0] - before->phase_us[0]) / audio_us;
    for (unsigned i = 0; i < XEMU_PERF_PHASES; i++) {
        result.phase_percent[i] =
            100.0 * (after->phase_us[i] - before->phase_us[i]) / elapsed;
    }
    return result;
}

static inline XemuPerfResult
xemu_perf_history_push(XemuPerfHistory *history, const XemuPerfCounters *sample)
{
    XemuPerfResult result = { 0 };
    result.current = *sample;
    if (history->count) {
        unsigned last =
            (history->next + XEMU_PERF_HISTORY - 1) % XEMU_PERF_HISTORY;
        const XemuPerfCounters *previous = &history->samples[last];
        if (sample->generation != previous->generation ||
            sample->time_us <= previous->time_us ||
            sample->time_us - previous->time_us > XEMU_PERF_STALE_US) {
            memset(history, 0, sizeof(*history));
        }
    }
    if (history->count == XEMU_PERF_HISTORY) {
        result = xemu_perf_compare(&history->samples[history->next], sample);
    } else {
        history->count++;
    }
    history->samples[history->next] = *sample;
    history->next = (history->next + 1) % XEMU_PERF_HISTORY;
    return result;
}
#endif
