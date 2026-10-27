/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_BENCHMARK_CLOCK_H
#define XEMU_BENCHMARK_CLOCK_H
#include <stdbool.h>
#include <stdint.h>

/* An opt-in lab instrument, never a source for gameplay speed percentages. */
#define XEMU_BENCHMARK_MAGIC 0x58424d31u
#define XEMU_BENCHMARK_BEGIN 0x80000000u
#define XEMU_BENCHMARK_PORT 0x500

typedef struct XemuBenchmarkClock {
    uint32_t sequence;
    int64_t start_ns;
    bool active;
    bool valid;
} XemuBenchmarkClock;

static inline void xemu_benchmark_begin(XemuBenchmarkClock *s, uint32_t seq,
                                        int64_t now)
{
    bool nested = s->active;
    s->sequence = seq;
    s->start_ns = now;
    s->active = true;
    s->valid = !nested && seq != 0;
}

static inline int64_t xemu_benchmark_end(XemuBenchmarkClock *s, uint32_t seq,
                                         int64_t now)
{
    int64_t elapsed = -1;
    if (s->active && s->valid && seq == s->sequence && now > s->start_ns) {
        elapsed = now - s->start_ns;
    }
    s->active = false;
    s->valid = false;
    return elapsed;
}
#endif
