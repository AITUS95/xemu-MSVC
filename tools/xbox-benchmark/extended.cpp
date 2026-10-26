/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "extended.h"
#include <hal/debug.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "generated/cpu/test_log.h"
#include "generated/nv2a/m2mf_tests/m2mf_test_common.h"

extern "C" void bench_log_mode(bool quiet);
extern "C" unsigned bench_log_failures();
extern "C" bool bench_native_m2mf();

extern "C" void _putchar(char c)
{
    putchar(c);
}

static void flush_cache()
{
    __asm__ volatile("wbinvd" ::: "memory");
}

static bool record(FILE *out, unsigned seq, const char *id, int trial,
                   unsigned loops, unsigned bytes, unsigned layout,
                   unsigned phase, uint64_t start, uint64_t end, DWORD ticks,
                   bool ok, unsigned failures, bool native)
{
    fprintf(out, "%u,%s,%d,%u,%u,%u,%u,%llu,%llu,%llu,%lu,%u,%u,%u\n", seq, id,
            trial, loops, bytes, layout, phase, (unsigned long long)start,
            (unsigned long long)end, (unsigned long long)(end - start),
            (unsigned long)ticks, ok && end > start, failures, native);
    return fflush(out) == 0 && !ferror(out);
}

static bool validate(const UpstreamCase &c, bool available)
{
    bench_log_mode(false);
    TestLog::BeginTest(c.id, c.source);
    bool ok = false;
    if (available) {
        ok = c.run();
    } else {
        TEST_ERROR("Native M2MF unavailable; test not executed");
    }
    ok = ok && !bench_log_failures();
    TestLog::EndTest(ok);
    return ok;
}

static bool cpu_replay(FILE *out, const UpstreamCase &c, bool preflight)
{
    /*
     * These are validation replays, including checks/argument construction;
     * Suppress only output, never assertions or a failing iteration.
     */
    unsigned loops =
        strcmp(c.id, "cpu/TestFpuBasicArithmetic") == 0 ? 16384 : 1024;
    for (int trial = -1; trial < 7; trial++) {
        bench_log_mode(true);
        bool ok = preflight;
        unsigned seq;
        DWORD ticks = GetTickCount();
        uint64_t start = bench_begin(&seq);
        for (unsigned n = 0; n < loops; n++) {
            bool passed = c.run();
            ok = passed && ok;
        }
        uint64_t end = bench_end(seq);
        DWORD elapsed = GetTickCount();
        elapsed -= ticks;
        unsigned failures = bench_log_failures();
        bench_log_mode(false);
        if (!record(out, seq, c.id, trial, loops, 0, 0, 0, start, end, elapsed,
                    ok && !failures, failures, true)) {
            return false;
        }
    }
    return true;
}

static bool m2mf_case(FILE *out, unsigned bytes, unsigned layout,
                      unsigned phase, bool native)
{
    const unsigned guard = 256;
    unsigned width = layout ? 256 : bytes;
    unsigned lines = layout ? bytes / width : 1;
    unsigned stride = layout == 2 ? 2 : 1;
    unsigned pitch = layout ? width * stride + 64 : width;
    unsigned span = pitch * lines;
    unsigned offset = (span + 2 * guard + 16383) & ~16383u;
    offset += phase;
    unsigned size = offset + span + 2 * guard;
    auto *allocation = static_cast<uint8_t *>(
        MmAllocateContiguousMemoryEx(size, 0, kMaxRam, 16384, PAGE_READWRITE));
    if (!allocation) {
        return false;
    }
    uint8_t *src = allocation + guard;
    uint8_t *dst = src + offset;
    if (gpum_pair_phase(dst, src) != phase) {
        MmFreeContiguousMemory(allocation);
        return false;
    }
    unsigned loops = 67108864 / bytes;
    char id[64];
    snprintf(id, sizeof(id), "m2mf/%u/%u/%u", bytes, layout, phase);
    for (int trial = -1; trial < 7; trial++) {
        memset(allocation, 0x55, size);
        M2MFFillPattern(src, span);
        memset(dst, 0xcc, span);
        flush_cache();
        unsigned seq;
        bool ok = native;
        DWORD ticks = GetTickCount();
        uint64_t start = bench_begin(&seq);
        for (unsigned n = 0; native && n < loops; n++) {
            if (layout) {
                gpum_start_strided(dst, src, width, lines, pitch, pitch, stride,
                                   stride);
            } else {
                gpum_start(dst, src, bytes);
            }
            if (gpum_wait() != 0) {
                /* DMA may still own the allocation. Stop without freeing it. */
                bench_end(seq);
                return false;
            }
        }
        uint64_t end = bench_end(seq);
        DWORD elapsed = GetTickCount();
        elapsed -= ticks;
        flush_cache();
        ok = ok && bench_native_m2mf() && M2MFVerifyPattern(src, span) &&
             M2MFVerifyCanaries(src - guard, guard) &&
             M2MFVerifyCanaries(src + span, guard) &&
             M2MFVerifyCanaries(dst - guard, guard) &&
             M2MFVerifyCanaries(dst + span, guard);
        for (unsigned y = 0; y < lines; y++) {
            for (unsigned x = 0; x < pitch; x++) {
                uint8_t expected = x < width * stride && x % stride == 0 ?
                                       src[y * pitch + x] :
                                       0xcc;
                if (dst[y * pitch + x] != expected) {
                    ok = false;
                }
            }
        }
        if (!record(out, seq, id, trial, loops, bytes, layout, phase, start,
                    end, elapsed, ok, 0, native && bench_native_m2mf())) {
            MmFreeContiguousMemory(allocation);
            return false;
        }
    }
    MmFreeContiguousMemory(allocation);
    return true;
}

bool bench_extended_run(const char *base_path, const char *header)
{
    std::string stem(base_path);
    stem.resize(stem.size() - 4);
    std::string csv = stem + ".extended.csv";
    std::string json = stem + ".validation.json";
    if (GetFileAttributesA(csv.c_str()) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA(json.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    FILE *out = fopen(csv.c_str(), "w");
    if (!out) {
        return false;
    }
    if (!TestLog::Initialize(json, true)) {
        fclose(out);
        return false;
    }
    fputs(header, out);
    fputs("seq,case,trial,loops,bytes,layout,phase,tsc_start,tsc_end,tsc,"
          "ticks_ms,ok,failures,native\n",
          out);
    std::string identity(header);
    identity.pop_back();
    TestLog::BeginSuite("xbox-benchmark-pilot-2-validation|" + identity);
    unsigned passed = 0;
    bool complete = true;
    for (unsigned i = 0; i < 11 && complete; i++) {
        debugPrint("CPU validation/replay %u/11\n", i + 1);
        bool ok = validate(upstream_cases[i], true);
        passed += ok;
        complete = cpu_replay(out, upstream_cases[i], ok);
    }
    bool native = M2MFInit() && bench_native_m2mf();
    const unsigned sizes[] = { 4096, 65536, 1048576 };
    const unsigned phases[] = { 0, 8192 };
    for (unsigned bytes : sizes) {
        for (unsigned layout = 0; layout < 3 && complete; layout++) {
            for (unsigned phase : phases) {
                debugPrint("M2MF %u bytes, layout %u, phase %u\n", bytes,
                           layout, phase);
                complete = m2mf_case(out, bytes, layout, phase, native);
                if (!complete) {
                    break;
                }
            }
        }
    }
    /* Adversarial cases can rebind notifier contexts; run them last. */
    for (unsigned i = 11; i < 30 && complete; i++) {
        debugPrint("M2MF validation %u/19\n", i - 10);
        passed += validate(upstream_cases[i], native);
    }
    if (complete) {
        TestLog::EndSuite(passed, 30 - passed);
        fputs("# complete\n", out);
    }
    bool saved = !ferror(out);
    saved = fclose(out) == 0 && saved;
    return complete && saved;
}
