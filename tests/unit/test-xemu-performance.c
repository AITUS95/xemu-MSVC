/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "hw/xbox/performance-stats.h"

static void near(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.000001);
}

static XemuPerfCounters baseline(void)
{
    XemuPerfCounters value = { 0 };
    value.generation = 1;
    value.time_us = 1000000;
    value.audio_active = true;
    value.gp_enabled = value.ep_enabled = true;
    value.gp_realtime = value.ep_realtime = true;
    return value;
}

static void test_rates(void)
{
    XemuPerfCounters a = baseline(), b = a;
    b.time_us += 1000000;
    b.audio_updated_us = b.time_us;
    b.sample_frames += 48000;
    b.cpu_ns[0] += 250000000;
    b.cpu_ns[1] += 750000000;
    b.phase_us[0] += 500000;
    b.phase_us[1] += 100000;
    b.phase_us[2] += 300000;
    b.phase_us[3] += 100000;
    XemuPerfResult r = xemu_perf_compare(&a, &b);
    assert(r.ready && r.audio_valid && r.cpu_valid[0]);
    near(r.audio_percent, 100);
    near(r.audio_budget_percent, 50);
    near(r.cpu_percent[0], 25);
    near(r.cpu_percent[1], 75);
    near(r.phase_percent[2], 30);
    b.sample_frames = 24000;
    r = xemu_perf_compare(&a, &b);
    near(r.audio_percent, 50);
    near(r.audio_budget_percent, 100);
    b.phase_us[0] = 750000;
    r = xemu_perf_compare(&a, &b);
    near(r.audio_budget_percent, 150); /* Must not clamp at 100%. */
    b.time_us = a.time_us + 1500000;
    b.audio_updated_us = b.time_us;
    b.sample_frames = 72000;
    b.cpu_ns[0] = 375000000;
    r = xemu_perf_compare(&a, &b);
    near(r.audio_percent, 100);
    near(r.cpu_percent[0], 25); /* Actual duration, not a nominal second. */
}

static void test_invalid_samples(void)
{
    XemuPerfCounters a = baseline(), b = a;
    assert(!xemu_perf_compare(&a, &b).ready);
    b.time_us--;
    assert(!xemu_perf_compare(&a, &b).ready);
    b.time_us = a.time_us + 1000000;
    b.sample_frames = 48000;
    b.audio_updated_us = b.time_us;
    b.generation++;
    assert(!xemu_perf_compare(&a, &b).ready);
    b.generation = 0;
    assert(!xemu_perf_compare(&a, &b).ready);
    b.generation = a.generation;
    b.cpu_ns[0] = -1;
    assert(!xemu_perf_compare(&a, &b).cpu_valid[0]);
    b.audio_updated_us = b.time_us - XEMU_PERF_STALE_US - 1;
    XemuPerfResult r = xemu_perf_compare(&a, &b);
    assert(r.audio_stale && !r.audio_valid);
    b.audio_active = false;
    r = xemu_perf_compare(&a, &b);
    assert(!r.audio_stale && !r.audio_valid);
    b.audio_active = true;
    b.audio_updated_us = b.time_us;
    b.gp_realtime = false;
    assert(!xemu_perf_compare(&a, &b).audio_valid);
    b.gp_realtime = true;
    b.ep_enabled = false;
    assert(!xemu_perf_compare(&a, &b).audio_valid);
    b.ep_enabled = true;
    b.audio_generation++;
    assert(!xemu_perf_compare(&a, &b).audio_valid);
    b.audio_generation = a.audio_generation;
    b.sample_frames = a.sample_frames;
    assert(!xemu_perf_compare(&a, &b).audio_valid);
    a.cpu_ns[1] = 10;
    b.cpu_ns[1] = 5;
    assert(!xemu_perf_compare(&a, &b).cpu_valid[1]);
}

static void test_window_and_restart(void)
{
    XemuPerfHistory history = { 0 };
    XemuPerfCounters sample = baseline();
    XemuPerfResult result;
    for (unsigned i = 0; i <= XEMU_PERF_HISTORY; i++) {
        sample.time_us = 1000000 + i * XEMU_PERF_INTERVAL_US;
        sample.audio_updated_us = sample.time_us;
        sample.sample_frames = i * 12000;
        result = xemu_perf_history_push(&history, &sample);
        assert(result.ready == (i == XEMU_PERF_HISTORY));
    }
    near(result.audio_percent, 100);
    /* Hide/show, pause/resume, reset, and load all change the generation. */
    sample.generation++;
    sample.time_us += XEMU_PERF_INTERVAL_US;
    sample.sample_frames = 0;
    assert(!xemu_perf_history_push(&history, &sample).ready);
    assert(history.count == 1);
    /* A UI stall must not reuse an old average. */
    sample.time_us += XEMU_PERF_STALE_US + 1;
    assert(!xemu_perf_history_push(&history, &sample).ready);
    assert(history.count == 1);
}

static void test_audio_rate_colors(void)
{
    XemuPerfCounters a = baseline(), b = a;
    b.time_us += 1000000;
    b.audio_updated_us = b.time_us;
    b.sample_frames = 48000;
    XemuPerfResult good = xemu_perf_compare(&a, &b);
    const double values[] = { 0, 94.999, 95, 100, 105, 105.001, 150 };
    const enum XemuPerfRateBand expected[] = {
        XEMU_PERF_RATE_SLOW, XEMU_PERF_RATE_SLOW,
        XEMU_PERF_RATE_NORMAL, XEMU_PERF_RATE_NORMAL, XEMU_PERF_RATE_NORMAL,
        XEMU_PERF_RATE_FAST, XEMU_PERF_RATE_FAST,
    };
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        good.speed_percent[XEMU_PERF_APU] = values[i];
        assert(xemu_perf_audio_rate_band(&good) == expected[i]);
    }
    const double invalid[] = { -1, NAN, INFINITY };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        good.speed_percent[XEMU_PERF_APU] = invalid[i];
        assert(xemu_perf_audio_rate_band(&good) == XEMU_PERF_RATE_UNAVAILABLE);
    }
    good.speed_percent[XEMU_PERF_APU] = 100;
    for (unsigned i = 0; i < 10; i++) {
        XemuPerfResult bad = good;
        switch (i) {
        case 0:
            bad.current.generation = 0;
            break;
        case 1:
            bad.ready = false;
            break;
        case 2:
            bad.audio_valid = false;
            break;
        case 3:
            bad.current.audio_active = false;
            break;
        case 4:
            bad.audio_stale = true;
            break;
        case 5:
            bad.current.gp_enabled = false;
            break;
        case 6:
            bad.current.ep_enabled = false;
            break;
        case 7:
            bad.current.gp_realtime = false;
            break;
        case 8:
            bad.current.ep_realtime = false;
            break;
        case 9:
            bad.speed_valid[XEMU_PERF_APU] = false;
            break;
        }
        assert(xemu_perf_audio_rate_band(&bad) == XEMU_PERF_RATE_UNAVAILABLE);
    }
    /* Host utilization must never affect the audio rate classification. */
    good.cpu_percent[0] = 10;
    good.cpu_percent[1] = 150;
    good.cpu_percent[2] = 0;
    assert(xemu_perf_audio_rate_band(&good) == XEMU_PERF_RATE_NORMAL);
}

static void test_uncalibrated_speed(void)
{
    XemuPerfCounters a = baseline(), b = a;
    b.time_us += 1000000;
    b.sample_frames = 48000;
    b.audio_updated_us = b.time_us;
    b.cpu_ns[0] = b.cpu_ns[1] = 1000000000;
    a.nv2a_valid = b.nv2a_valid = true;
    a.nv2a.generation = b.nv2a.generation = a.generation;
    b.nv2a.words = 100000000;
    b.nv2a.flips = 120;
    XemuPerfResult r = xemu_perf_compare(&a, &b);
    assert(r.speed_valid[XEMU_PERF_APU]);
    near(r.speed_percent[XEMU_PERF_APU], 100);
    for (unsigned i = XEMU_PERF_CPU; i <= XEMU_PERF_NV2A; i++) {
        assert(!r.speed_valid[i]);
        near(r.speed_percent[i], 0);
        assert(xemu_perf_speed_band(&r, (enum XemuPerfThread)i) ==
               XEMU_PERF_RATE_UNAVAILABLE);
    }
    assert(xemu_perf_speed_band(&r, (enum XemuPerfThread)-1) ==
           XEMU_PERF_RATE_UNAVAILABLE);
}

static void test_nv2a_rates(void)
{
    XemuPerfCounters a = baseline(), b = a;
    a.nv2a_valid = b.nv2a_valid = true;
    a.nv2a.generation = b.nv2a.generation = a.generation;
    b.time_us += 1500000;
    b.nv2a.words = 3600000;
    b.nv2a.flips = 45;
    b.nv2a.stalls[XEMU_PERF_STALL_FLIP] = 3;
    XemuPerfResult r = xemu_perf_compare(&a, &b);
    assert(r.nv2a_valid);
    near(r.nv2a_words_per_second, 2400000);
    near(r.nv2a_flips_per_second, 30);
    near(r.nv2a_stalls_per_second[XEMU_PERF_STALL_FLIP], 2);
    near(r.nv2a_stalls_per_second[XEMU_PERF_STALL_CONTEXT], 0);
    /* No GPU work is valid zero throughput, not a fabricated speed. */
    b.nv2a = a.nv2a;
    r = xemu_perf_compare(&a, &b);
    assert(r.nv2a_valid && !r.speed_valid[XEMU_PERF_NV2A]);
    near(r.nv2a_words_per_second, 0);
    b.nv2a_valid = false; /* Nonblocking snapshot could not get the lock. */
    assert(!xemu_perf_compare(&a, &b).nv2a_valid);
    b.nv2a_valid = true;
    b.nv2a.generation++;
    assert(!xemu_perf_compare(&a, &b).nv2a_valid);
    b.nv2a.generation = a.nv2a.generation;
    a.nv2a.words = UINT64_MAX;
    assert(!xemu_perf_compare(&a, &b).nv2a_valid);
    a.nv2a.words = 0;
    a.nv2a.flips = 1;
    assert(!xemu_perf_compare(&a, &b).nv2a_valid);
    a.nv2a.flips = 0;
    a.nv2a.stalls[XEMU_PERF_STALL_NOP] = 1;
    assert(!xemu_perf_compare(&a, &b).nv2a_valid);
    b.time_us = a.time_us;
    assert(!xemu_perf_compare(&a, &b).nv2a_valid);
}

static void test_nv2a_stall_transitions(void)
{
    XemuPerfNv2a p = { 0 };
    xemu_perf_nv2a_words(&p, 0, 123);
    xemu_perf_nv2a_stall(&p, XEMU_PERF_STALL_FLIP, true);
    assert(p.words == 0 && p.stalls[XEMU_PERF_STALL_FLIP] == 0);
    xemu_perf_nv2a_prepare(&p, 1);
    xemu_perf_nv2a_words(&p, 1, 128);
    xemu_perf_nv2a_words(&p, 1, 64);
    assert(p.words == 192);
    for (unsigned reason = 0; reason < XEMU_PERF_STALL_COUNT; reason++) {
        for (unsigned poll = 0; poll < 1000; poll++) {
            xemu_perf_nv2a_stall(&p, (enum XemuPerfStall)reason, true);
        }
        assert(p.stalls[reason] == 1);
        xemu_perf_nv2a_stall(&p, (enum XemuPerfStall)reason, false);
        xemu_perf_nv2a_stall(&p, (enum XemuPerfStall)reason, true);
        assert(p.stalls[reason] == 2);
    }
    xemu_perf_nv2a_prepare(&p, 1);
    assert(p.words == 192); /* Same session preserves cumulative counts. */
    xemu_perf_nv2a_prepare(&p, 2);
    xemu_perf_nv2a_words(&p, 1, 999); /* Batch spanning a session change. */
    assert(p.words == 0 && p.stalled == 0 && p.flips == 0);
    xemu_perf_nv2a_prepare(&p, 0);
    xemu_perf_nv2a_words(&p, 2, 999);
    assert(p.words == 0);
}


static void test_host_pressure_colors(void)
{
    XemuPerfResult r = { 0 };
    r.current.generation = 1;
    r.ready = true;
    r.cpu_valid[XEMU_PERF_CPU] = true;
    r.cpu_percent[XEMU_PERF_CPU] = 96.8;
    assert(xemu_perf_host_pressure_band(&r, XEMU_PERF_CPU) ==
           XEMU_PERF_RATE_SLOW);
    r.cpu_percent[XEMU_PERF_CPU] = 8.0;
    assert(xemu_perf_host_pressure_band(&r, XEMU_PERF_CPU) ==
           XEMU_PERF_RATE_FAST);
    r.cpu_percent[XEMU_PERF_CPU] = 75.0;
    assert(xemu_perf_host_pressure_band(&r, XEMU_PERF_CPU) ==
           XEMU_PERF_RATE_NORMAL);
    r.cpu_percent[XEMU_PERF_CPU] = NAN;
    assert(xemu_perf_host_pressure_band(&r, XEMU_PERF_CPU) ==
           XEMU_PERF_RATE_UNAVAILABLE);
    r.cpu_percent[XEMU_PERF_CPU] = 96.8;
    r.cpu_valid[XEMU_PERF_CPU] = false;
    assert(xemu_perf_host_pressure_band(&r, XEMU_PERF_CPU) ==
           XEMU_PERF_RATE_UNAVAILABLE);
    r.cpu_valid[XEMU_PERF_CPU] = true;
    r.ready = false;
    assert(xemu_perf_host_pressure_band(&r, XEMU_PERF_CPU) ==
           XEMU_PERF_RATE_UNAVAILABLE);
    r.ready = true;
    /* CPU speed stays unknown regardless of this *host* pressure indicator. */
    assert(xemu_perf_speed_band(&r, XEMU_PERF_CPU) ==
           XEMU_PERF_RATE_UNAVAILABLE);
}

static void test_trend_row_colors(void)
{
    XemuPerfTrend pfifo = { 0 }, flips = { 0 }, stalls = { 0 };
    for (unsigned i = 0; i < 5; i++) {
        enum XemuPerfRateBand a =
            xemu_perf_trend_band(&pfifo, XEMU_PERF_TREND_PFIFO, true, 2200000);
        enum XemuPerfRateBand b =
            xemu_perf_trend_band(&flips, XEMU_PERF_TREND_FLIPS, true, 25);
        enum XemuPerfRateBand c =
            xemu_perf_trend_band(&stalls, XEMU_PERF_TREND_STALLS, true, 75);
        if (i < 4) {
            assert(a == XEMU_PERF_RATE_UNAVAILABLE);
            assert(b == XEMU_PERF_RATE_UNAVAILABLE);
            assert(c == XEMU_PERF_RATE_UNAVAILABLE);
        } else {
            assert(a == XEMU_PERF_RATE_FAST);
            assert(b == XEMU_PERF_RATE_FAST);
            assert(c == XEMU_PERF_RATE_FAST);
        }
    }
    assert(xemu_perf_trend_band(&pfifo, XEMU_PERF_TREND_PFIFO,
                                true, 800000) == XEMU_PERF_RATE_SLOW);
    assert(xemu_perf_trend_band(&flips, XEMU_PERF_TREND_FLIPS,
                                true, 10) == XEMU_PERF_RATE_SLOW);
    assert(xemu_perf_trend_band(&stalls, XEMU_PERF_TREND_STALLS,
                                true, 150) == XEMU_PERF_RATE_SLOW);
    double original = pfifo.reference;
    assert(xemu_perf_trend_band(&pfifo, XEMU_PERF_TREND_PFIFO,
                                false, 0) == XEMU_PERF_RATE_UNAVAILABLE);
    assert(xemu_perf_trend_band(&pfifo, XEMU_PERF_TREND_PFIFO,
                                true, NAN) == XEMU_PERF_RATE_UNAVAILABLE);
    near(pfifo.reference, original);
    XemuPerfTrend idle = { 0 };
    for (unsigned i = 0; i < 6; ++i) {
        assert(xemu_perf_trend_band(&idle, XEMU_PERF_TREND_FLIPS, true, 0)
               == XEMU_PERF_RATE_UNAVAILABLE);
    }
    /* Zero stalls is green after warm-up, not an alert. */
    XemuPerfTrend no_stalls = { 0 };
    for (unsigned i = 0; i < 4; i++) {
        assert(xemu_perf_trend_band(&no_stalls, XEMU_PERF_TREND_STALLS,
                                    true, 0) == XEMU_PERF_RATE_UNAVAILABLE);
    }
    assert(xemu_perf_trend_band(&no_stalls, XEMU_PERF_TREND_STALLS,
                                true, 0) == XEMU_PERF_RATE_FAST);
    /* A burst from zero is flagged only when substantial. */
    assert(xemu_perf_trend_band(&no_stalls, XEMU_PERF_TREND_STALLS,
                                true, 18) == XEMU_PERF_RATE_SLOW);
}

int main(void)
{
    puts("TAP version 13\n1..9");
    test_rates();
    puts("ok 1 - rates and irregular intervals");
    test_invalid_samples();
    puts("ok 2 - invalid and stale samples");
    test_window_and_restart();
    puts("ok 3 - rolling window and restart");
    test_audio_rate_colors();
    puts("ok 4 - trusted audio rate colors and unavailable states");
    test_uncalibrated_speed();
    puts("ok 5 - uncalibrated CPU/NV2A speed remains unavailable");
    test_nv2a_rates();
    puts("ok 6 - NV2A throughput, snapshot validity and discontinuities");
    test_nv2a_stall_transitions();
    puts("ok 7 - stall transitions and collection generations");
    test_host_pressure_colors();
    puts("ok 8 - host load indication never fabricates CPU speed");
    test_trend_row_colors();
    puts("ok 9 - PFIFO, flips and stall row trend colors");
    return 0;
}
