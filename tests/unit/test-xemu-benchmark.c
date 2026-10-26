/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/benchmark-clock.h"

int main(void)
{
    XemuBenchmarkClock s = { 0 };
    assert(xemu_benchmark_end(&s, 1, 200) == -1);
    xemu_benchmark_begin(&s, 1, 100);
    assert(xemu_benchmark_end(&s, 1, 200) == 100);
    assert(xemu_benchmark_end(&s, 1, 300) == -1);
    xemu_benchmark_begin(&s, 2, 100);
    assert(xemu_benchmark_end(&s, 3, 200) == -1);
    xemu_benchmark_begin(&s, 3, 100);
    xemu_benchmark_begin(&s, 4, 150);
    assert(xemu_benchmark_end(&s, 4, 200) == -1);
    xemu_benchmark_begin(&s, 5, 100);
    s.valid = false; /* Pause, reset or session mutation. */
    assert(xemu_benchmark_end(&s, 5, 200) == -1);
    xemu_benchmark_begin(&s, 6, 100);
    assert(xemu_benchmark_end(&s, 6, 99) == -1);
    xemu_benchmark_begin(&s, 7, 100);
    assert(xemu_benchmark_end(&s, 7, 100) == -1);
    xemu_benchmark_begin(&s, 0, 100);
    assert(xemu_benchmark_end(&s, 0, 200) == -1);
    xemu_benchmark_begin(&s, 8, 100);
    assert(xemu_benchmark_end(&s, 8, 250) == 150);
    puts("TAP version 13\n1..1\nok 1 - benchmark interval validity");
    return 0;
}
