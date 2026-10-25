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

int main(void)
{
    puts("TAP version 13\n1..3");
    test_rates();
    puts("ok 1 - rates and irregular intervals");
    test_invalid_samples();
    puts("ok 2 - invalid and stale samples");
    test_window_and_restart();
    puts("ok 3 - rolling window and restart");
    return 0;
}
