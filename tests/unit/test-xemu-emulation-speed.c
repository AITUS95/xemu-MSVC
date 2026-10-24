/*
 * Unit tests for the host-paced VBlank service-rate estimator.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "ui/xemu-emulation-speed.h"

static void drive(XemuEmulationSpeed *meter, int64_t start_ns,
                  int64_t actual_interval_ns, int64_t target_interval_ns,
                  int ticks)
{
    int i;

    xemu_emulation_speed_vblank(meter, start_ns,
                               target_interval_ns, true);
    for (i = 1; i <= ticks; ++i) {
        xemu_emulation_speed_vblank(meter,
                                   start_ns + i * actual_interval_ns,
                                   target_interval_ns, true);
    }
}

static void test_ntsc(void)
{
    XemuEmulationSpeed meter = {0};
    float percent = 0.0f;
    const int64_t frame = INT64_C(1000000000) / 60;
    const int64_t start = INT64_C(1000000000);

    drive(&meter, start, frame, frame, 61);
    g_assert_true(xemu_emulation_speed_get(&meter, start + 61 * frame,
                                           &percent));
    g_assert_cmpfloat(percent, >, 98.0f);
    g_assert_cmpfloat(percent, <, 102.0f);
}

static void test_pal(void)
{
    XemuEmulationSpeed meter = {0};
    float percent = 0.0f;
    const int64_t frame = INT64_C(1000000000) / 50;
    const int64_t start = INT64_C(1000000000);

    drive(&meter, start, frame, frame, 51);
    g_assert_true(xemu_emulation_speed_get(&meter, start + 51 * frame,
                                           &percent));
    g_assert_cmpfloat(percent, >, 99.0f);
    g_assert_cmpfloat(percent, <, 101.0f);
}

static void test_slow_vblank_service(void)
{
    XemuEmulationSpeed meter = {0};
    float percent = 0.0f;
    const int64_t frame = INT64_C(1000000000) / 60;
    const int64_t start = INT64_C(1000000000);

    drive(&meter, start, 2 * frame, frame, 31);
    g_assert_true(xemu_emulation_speed_get(&meter, start + 62 * frame,
                                           &percent));
    g_assert_cmpfloat(percent, >, 48.0f);
    g_assert_cmpfloat(percent, <, 52.0f);
}

static void test_pause_and_stall(void)
{
    XemuEmulationSpeed meter = {0};
    float percent = 0.0f;
    const int64_t frame = INT64_C(1000000000) / 50;
    const int64_t start = INT64_C(1000000000);

    drive(&meter, start, frame, frame, 51);
    g_assert_false(xemu_emulation_speed_get(
        &meter, start + 51 * frame + XEMU_SPEED_STALE_NS + 1, &percent));
    xemu_emulation_speed_vblank(&meter, start + 52 * frame,
                               frame, false);
    g_assert_false(xemu_emulation_speed_get(
        &meter, start + 52 * frame, &percent));
    xemu_emulation_speed_vblank(&meter, start + 53 * frame,
                               frame, true);
    g_assert_false(xemu_emulation_speed_get(
        &meter, start + 53 * frame, &percent));
}

static void test_clock_regression(void)
{
    XemuEmulationSpeed meter = {0};
    float percent = 0.0f;
    const int64_t frame = INT64_C(1000000000) / 50;
    const int64_t start = INT64_C(1000000000);

    drive(&meter, start, frame, frame, 51);
    xemu_emulation_speed_vblank(&meter, start, frame, true);
    g_assert_false(xemu_emulation_speed_get(&meter, start, &percent));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/speed/ntsc", test_ntsc);
    g_test_add_func("/xemu/speed/pal", test_pal);
    g_test_add_func("/xemu/speed/slow-vblank", test_slow_vblank_service);
    g_test_add_func("/xemu/speed/pause-and-stall", test_pause_and_stall);
    g_test_add_func("/xemu/speed/clock-regression", test_clock_regression);
    return g_test_run();
}
