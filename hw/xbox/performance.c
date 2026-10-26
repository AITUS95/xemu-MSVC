/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "hw/core/cpu.h"
#include "hw/xbox/performance.h"
#include "system/runstate.h"

/* Zero-initialized GMutex is valid; no emulator lock is taken while held. */
static GMutex lock;
static unsigned generation;
static unsigned serial;
static bool enabled;
static QemuThread *threads[XEMU_PERF_THREADS];
static XemuPerfCounters counters;
static XemuPerfHistory history;
static XemuPerfResult cached;
static int64_t last_sample_us;
static XemuPerfNv2a *nv2a_source;
static QemuMutex *nv2a_mutex;

void xemu_perf_invalidate(void)
{
    assert(bql_locked());
    g_mutex_lock(&lock);
    memset(&counters, 0, sizeof(counters));
    memset(&history, 0, sizeof(history));
    memset(&cached, 0, sizeof(cached));
    last_sample_us = 0;
    if (++serial == 0) {
        ++serial;
    }
    counters.generation = enabled && runstate_is_running() ? serial : 0;
    qatomic_set(&generation, counters.generation);
    g_mutex_unlock(&lock);
}

static void xemu_perf_vm_change(void *opaque, bool running, RunState state)
{
    xemu_perf_invalidate();
}

void xemu_perf_init(void)
{
    qemu_add_vm_change_state_handler(xemu_perf_vm_change, NULL);
}

void xemu_perf_set_enabled(bool value)
{
    assert(bql_locked());
    if (enabled != value) {
        enabled = value;
        xemu_perf_invalidate();
    }
}

void xemu_perf_set_thread(enum XemuPerfThread id, QemuThread *thread)
{
    assert(bql_locked());
    assert(id < XEMU_PERF_THREADS);
    threads[id] = thread;
    xemu_perf_invalidate();
}

unsigned xemu_perf_generation(void)
{
    return qatomic_read(&generation);
}

void xemu_perf_set_nv2a(XemuPerfNv2a *source, QemuMutex *mutex)
{
    assert(bql_locked());
    nv2a_source = source;
    nv2a_mutex = mutex;
    xemu_perf_invalidate();
}

static int64_t thread_cpu_ns(QemuThread *thread)
{
    if (!thread) {
        return -1;
    }
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER k, u;
    HANDLE handle =
        OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread->tid);
    if (!handle) {
        return -1;
    }
    BOOL ok = GetThreadTimes(handle, &created, &exited, &kernel, &user);
    CloseHandle(handle);
    if (!ok) {
        return -1;
    }
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    uint64_t ticks = k.QuadPart + u.QuadPart;
    return ticks * 100;
#elif defined(__linux__)
    clockid_t clock;
    struct timespec time;
    if (pthread_getcpuclockid(thread->thread, &clock) ||
        clock_gettime(clock, &time)) {
        return -1;
    }
    return (int64_t)time.tv_sec * 1000000000 + time.tv_nsec;
#else
    return -1;
#endif
}

XemuPerfResult xemu_perf_sample(void)
{
    assert(bql_locked());
    int64_t now = g_get_monotonic_time();
    if (last_sample_us && now - last_sample_us < XEMU_PERF_INTERVAL_US) {
        return cached;
    }
    last_sample_us = now;
    g_mutex_lock(&lock);
    XemuPerfCounters sample = counters;
    g_mutex_unlock(&lock);
    sample.time_us = now;
    /* Never wait for the GPU worker while holding the BQL. */
    if (sample.generation && nv2a_source &&
        qemu_mutex_trylock(nv2a_mutex) == 0) {
        sample.nv2a = *nv2a_source;
        sample.nv2a_valid = sample.nv2a.generation == sample.generation;
        qemu_mutex_unlock(nv2a_mutex);
    }
    QemuThread *cpu_thread =
        first_cpu && first_cpu->created ? first_cpu->thread : NULL;
    sample.cpu_ns[XEMU_PERF_CPU] = thread_cpu_ns(cpu_thread);
    for (unsigned i = XEMU_PERF_NV2A; i < XEMU_PERF_THREADS; i++) {
        sample.cpu_ns[i] = thread_cpu_ns(threads[i]);
    }
    cached = xemu_perf_history_push(&history, &sample);
    return cached;
}

XemuPerfAudioFrame xemu_perf_audio_begin(void)
{
    XemuPerfAudioFrame frame = { 0 };
    frame.generation = qatomic_read(&generation);
    if (frame.generation) {
        frame.start_us = g_get_monotonic_time();
        g_mutex_lock(&lock);
        if (frame.generation == counters.generation) {
            if (!counters.audio_active) {
                counters.audio_generation++;
                counters.audio_updated_us = frame.start_us;
            }
            counters.audio_active = true;
            if (!counters.audio_updated_us) {
                counters.audio_updated_us = frame.start_us;
            }
        }
        g_mutex_unlock(&lock);
    }
    return frame;
}

int64_t xemu_perf_audio_clock(const XemuPerfAudioFrame *frame)
{
    return frame->generation ? g_get_monotonic_time() : 0;
}

void xemu_perf_audio_end(const XemuPerfAudioFrame *frame, unsigned samples)
{
    if (!frame->generation) {
        return;
    }
    int64_t now = g_get_monotonic_time();
    g_mutex_lock(&lock);
    if (frame->generation == counters.generation) {
        counters.audio_active = true;
        counters.audio_updated_us = now;
        counters.sample_frames += samples;
        counters.phase_us[0] += now - frame->start_us;
        for (unsigned i = 1; i < XEMU_PERF_PHASES; i++) {
            counters.phase_us[i] += frame->phase_us[i];
        }
        if (counters.gp_enabled != frame->gp_enabled ||
            counters.ep_enabled != frame->ep_enabled ||
            counters.gp_realtime != frame->gp_realtime ||
            counters.ep_realtime != frame->ep_realtime) {
            counters.audio_generation++;
        }
        counters.gp_enabled = frame->gp_enabled;
        counters.ep_enabled = frame->ep_enabled;
        counters.gp_realtime = frame->gp_realtime;
        counters.ep_realtime = frame->ep_realtime;
    }
    g_mutex_unlock(&lock);
}

void xemu_perf_audio_idle(void)
{
    if (!qatomic_read(&generation)) {
        return;
    }
    g_mutex_lock(&lock);
    if (counters.audio_active) {
        counters.audio_generation++;
    }
    counters.audio_active = false;
    g_mutex_unlock(&lock);
}
