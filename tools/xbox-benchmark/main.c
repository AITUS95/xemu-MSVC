/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <hal/debug.h>
#include <hal/video.h>
#include <nxdk/mount.h>
#include <pbkit/pbkit.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernels.h"
#include "../../hw/xbox/benchmark-clock.h"

#define TRIALS 7
#define MAX_CASES 40
#define RESULT_DIR "E:\\XBench"
#define SUITE "xbox-benchmark-pilot-1"

typedef struct BenchCase {
    unsigned kind;
    uint32_t iterations, words, width, height;
} BenchCase;

static BenchCase cases[MAX_CASES];
static unsigned case_count, sequence;
static bool host_mode;
static uint32_t *memory;
static FILE *output;

static void out32(unsigned port, uint32_t value)
{
    __asm__ volatile("outl %0,%w1" : : "a"(value), "Nd"(port) : "memory");
}

static uint32_t in32(unsigned port)
{
    uint32_t value;
    __asm__ volatile("inl %w1,%0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static uint64_t stamp(void)
{
    uint32_t lo, hi;
    /* Pentium III has no RDTSCP/LFENCE. Serialize with CPUID on both ends. */
    __asm__ volatile("xorl %%eax,%%eax\n\tcpuid\n\trdtsc"
                     : "=a"(lo), "=d"(hi)
                     :
                     : "ebx", "ecx", "memory");
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t crc32(uint32_t crc, const void *data, size_t length)
{
    const unsigned char *p = data;
    while (length--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return crc;
}

static bool executable_id(uint32_t *crc, unsigned *size)
{
    FILE *f = fopen("D:\\default.xbe", "rb");
    unsigned char buffer[4096];
    size_t n;
    *crc = UINT32_MAX;
    *size = 0;
    if (!f) {
        return false;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), f))) {
        *crc = crc32(*crc, buffer, n);
        *size += n;
    }
    bool ok = !ferror(f);
    fclose(f);
    *crc ^= UINT32_MAX;
    return ok && *size != 0;
}

static bool gpu_idle(void)
{
    DWORD start = GetTickCount();
    while (pb_busy()) {
        DWORD now = GetTickCount();
        if ((uint32_t)(now - start) > 10000) {
            return false;
        }
    }
    return true;
}

static void completion(void)
{
    uint32_t *p = pb_begin();
    p = pb_push1(p, NV097_WAIT_FOR_IDLE, 0);
    pb_end(p);
}

static bool prepare(const BenchCase *c)
{
    if (c->kind < 9) {
        bench_prepare(memory, c->words);
        return true;
    }
    if (c->kind == 9) { /* Timer/control baseline. */
        return true;
    }
    if (!gpu_idle()) {
        return false;
    }
    pb_reset();
    pb_target_back_buffer();
    pb_fill(0, 0, 640, 480, 0xff000000);
    completion();
    return gpu_idle();
}

static uint32_t workload(const BenchCase *c, bool *ok)
{
    if (c->kind < 9) {
        return bench_cpu(c->kind, c->iterations, memory, c->words);
    }
    if (c->kind == 9) {
        return 1;
    }
    /* Vary colors so repeated clears cannot all be treated as redundant. */
    for (uint32_t i = 0; i < c->iterations; i++) {
        pb_fill(0, 0, c->width, c->height, 0xff000000 | (i + 1));
    }
    completion();
    *ok = gpu_idle();
    return c->iterations;
}

static bool verify_gpu(const BenchCase *c)
{
    /* AGP alias gives a linear CPU view of the tiled framebuffer. */
    volatile uint32_t *pixels = pb_agp_access(pb_back_buffer());
    unsigned pitch = pb_back_buffer_pitch() / 4;
    for (unsigned y = 0; y < 480; y++) {
        for (unsigned x = 0; x < 640; x++) {
            uint32_t expected = 0xff000000;
            if (x < c->width && y < c->height) {
                expected |= c->iterations;
            }
            if (pixels[y * pitch + x] != expected) {
                return false;
            }
        }
    }
    return true;
}

static bool trial(unsigned id, int repetition)
{
    const BenchCase *c = &cases[id];
    if (!prepare(c)) {
        return false;
    }
    bool ok = true;
    unsigned seq = ++sequence;
    DWORD tick_start = GetTickCount();
    if (host_mode) {
        out32(XEMU_BENCHMARK_PORT, XEMU_BENCHMARK_BEGIN | seq);
    }
    uint64_t start = stamp();
    uint32_t checksum = workload(c, &ok);
    uint64_t end = stamp();
    if (host_mode) {
        out32(XEMU_BENCHMARK_PORT, seq);
    }
    DWORD ticks = GetTickCount();
    ticks -= tick_start;
    if (c->kind == 10 && ok) {
        ok = verify_gpu(c);
    }
    fprintf(output, "%u,%u,%d,%u,%u,%u,%u,%u,%llu,%llu,%llu,%lu,%u,%u\n", seq,
            id, repetition, c->kind, c->iterations, c->words, c->width,
            c->height, (unsigned long long)start, (unsigned long long)end,
            (unsigned long long)(end - start), (unsigned long)ticks, checksum,
            ok && end > start);
    fflush(
        output); /* Outside both measured intervals; preserve partial runs. */
    return ok && !ferror(output);
}

static void add(unsigned kind, uint32_t n, uint32_t words, unsigned width,
                unsigned height)
{
    cases[case_count++] = (BenchCase){ kind, n, words, width, height };
}

static void finish(const char *message)
{
    debugPrint("%s\nResults: E:\\XBench\\\nYou may return to the dashboard.\n",
               message);
    if (output) {
        fclose(output);
    }
    for (;;) {
        Sleep(1000);
    }
}

int main(void)
{
    XVideoSetMode(640, 480, 32, 60);
    debugPrint("Xbox Benchmark PILOT 1\nAutomatic run. Do not pause.\n");
    FILE *mode = fopen("D:\\XEMU.TXT", "r");
    host_mode = mode != NULL;
    if (mode) {
        fclose(mode);
        if (in32(XEMU_BENCHMARK_PORT) != XEMU_BENCHMARK_MAGIC) {
            finish("Missing xemu benchmark device; no results collected.");
        }
    }
    uint32_t binary_crc;
    unsigned binary_size;
    if (!executable_id(&binary_crc, &binary_size)) {
        finish("Cannot identify D:\\default.xbe; no results collected.");
    }
    if (!nxIsDriveMounted('E') &&
        !nxMountDrive('E', "\\Device\\Harddisk0\\Partition1")) {
        finish("Cannot mount the existing E: data partition.");
    }
    if (!CreateDirectoryA(RESULT_DIR, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        finish("Cannot create E:\\XBench. Check free space/filesystem.");
    }
    LARGE_INTEGER time, frequency;
    KeQuerySystemTime(&time);
    QueryPerformanceFrequency(&frequency);
    char name[96];
    snprintf(name, sizeof(name), RESULT_DIR "\\%08x%08x.csv",
             (unsigned)time.HighPart, (unsigned)time.LowPart);
    if (GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES) {
        finish(
            "Result filename already exists. Correct the console clock first.");
    }
    output = fopen(name, "w");
    if (!output) {
        finish(
            "Cannot create a NEW result file on E:. Free space/check clock.");
    }
    if (host_mode) {
        out32(XEMU_BENCHMARK_PORT + 4, time.LowPart);
        out32(XEMU_BENCHMARK_PORT + 8, time.HighPart);
    }
    fprintf(output, "# %s,%08x%08x,%s,%08x,%u,%llu\n", SUITE,
            (unsigned)time.HighPart, (unsigned)time.LowPart,
            host_mode ? "xemu" : "hardware-unverified", binary_crc, binary_size,
            (unsigned long long)frequency.QuadPart);
    fputs("seq,case,trial,kind,iterations,words,width,height,tsc_start,tsc_end,"
          "tsc,ticks_ms,checksum,ok\n",
          output);
    fflush(output);
    memory = malloc(4 * 1024 * 1024);
    if (!memory) {
        finish("Out of memory. Partial run is not a baseline.");
    }
    add(9, 0, 0, 0, 0);
    for (unsigned kind = 0; kind < 9; kind++) {
        if (kind >= 3 && kind <= 5) {
            const uint32_t sizes[] = { 1024, 4096, 32768, 1048576 };
            for (unsigned i = 0; i < 4; i++) {
                add(kind, 33554432, sizes[i], 0, 0);
            }
        } else if (kind == 6) {
            /* Keep float sums exact, below the 2^24 unit-step limit. */
            add(kind, 2097152, 1024, 0, 0);
            add(kind, 8388608, 1024, 0, 0);
        } else {
            add(kind, 8388608, 1024, 0, 0);
            add(kind, 33554432, 1024, 0, 0);
        }
    }
    if (pb_init()) {
        finish("pbkit initialization failed; partial run is not a baseline.");
    }
    pb_show_debug_screen();
    if (pb_back_buffer_width() != 640 || pb_back_buffer_height() != 480) {
        finish("Unsupported video geometry; no comparable results.");
    }
    const unsigned widths[] = { 16, 128, 640 };
    const unsigned heights[] = { 16, 128, 480 };
    const unsigned batches[] = { 64, 1024, 16384 };
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 3; j++) {
            add(10, batches[j], 0, widths[i], heights[i]);
        }
    }
    /* One full warm-up/case, then interleaved deterministic shuffled rounds. */
    uint32_t rng = 0x58424d31;
    for (int round = -1; round < TRIALS; round++) {
        unsigned order[MAX_CASES];
        for (unsigned i = 0; i < case_count; i++) {
            order[i] = i;
        }
        for (unsigned i = case_count - 1; i > 0; i--) {
            rng = rng * 1664525 + 1013904223;
            unsigned j = rng % (i + 1), tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
        for (unsigned i = 0; i < case_count; i++) {
            debugPrint("Round %d/7, case %u/%u\n", round + 1, i + 1,
                       case_count);
            if (!trial(order[i], round)) {
                finish("FAILED: timeout/output mismatch/write error. Keep "
                       "partial file.");
            }
        }
    }
    fputs("# complete\n", output);
    fflush(output);
    finish(ferror(output) ?
               "Write failed." :
               "COMPLETE. Please share the CSV and console details.");
    return 0;
}
