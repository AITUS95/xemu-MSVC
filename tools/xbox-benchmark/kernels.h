/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XBOX_BENCHMARK_KERNELS_H
#define XBOX_BENCHMARK_KERNELS_H
#include <stdint.h>
/* Fixed work counts; all unsigned wraparound is intentional. */
uint32_t bench_cpu(unsigned kind, uint32_t iterations, uint32_t *memory,
                   uint32_t words);
void bench_prepare(uint32_t *memory, uint32_t words);
#endif
