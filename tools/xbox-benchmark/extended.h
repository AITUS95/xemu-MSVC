/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XBOX_BENCHMARK_EXTENDED_H
#define XBOX_BENCHMARK_EXTENDED_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
struct UpstreamCase {
    const char *id;
    const char *source;
    bool (*run)();
    bool cpu;
};
extern const UpstreamCase upstream_cases[30];
extern "C" {
#endif
bool bench_extended_run(const char *base_path, const char *header);
uint64_t bench_begin(unsigned *seq);
uint64_t bench_end(unsigned seq);
#ifdef __cplusplus
}
#endif
#endif
