/* Host-paced cadence tests; no model of Xbox CPU cycles is used.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "ui/xemu-vblank-cadence.h"

static const int64_t start = INT64_C(1000000000);
static const int64_t ntsc = INT64_C(1000000000) / 60;
static const int64_t pal = INT64_C(1000000000) / 50;

static void drive(XemuVBlankCadence *meter, int64_t from, int64_t actual,
                  int64_t expected, int ticks)
{
    xemu_vblank_cadence_event(meter, from, expected, true);
    for (int i = 1; i <= ticks; i++) {
        xemu_vblank_cadence_event(meter, from + i * actual, expected, true);
    }
}

static float reading(const XemuVBlankCadence *meter, int64_t now)
{
    float percent = -1.0f;
    g_assert_true(xemu_vblank_cadence_get(meter, now, &percent));
    return percent;
}

static void test_ntsc(void)
{
    XemuVBlankCadence meter = { 0 };
    drive(&meter, start, ntsc, ntsc, 61);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 61 * ntsc), 100,
                                   0.001);
}

static void test_fractional_reference(void)
{
    XemuVBlankCadence meter = { 0 };
    int64_t interval = INT64_C(1001000000000) / 60000;
    drive(&meter, start, interval, interval, 61);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 61 * interval), 100,
                                   0.001);
}

static void test_pal(void)
{
    XemuVBlankCadence meter = { 0 };
    drive(&meter, start, pal, pal, 51);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 51 * pal), 100,
                                   0.001);
}

static void test_half_cadence(void)
{
    XemuVBlankCadence meter = { 0 };
    drive(&meter, start, 2 * ntsc, ntsc, 31);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 62 * ntsc), 50,
                                   0.001);
}

static void test_quarter_cadence(void)
{
    XemuVBlankCadence meter = { 0 };
    drive(&meter, start, 4 * ntsc, ntsc, 16);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 64 * ntsc), 25,
                                   0.001);
}

static void test_pause_and_resume(void)
{
    XemuVBlankCadence meter = { 0 };
    float percent = -1;
    drive(&meter, start, pal, pal, 51);
    xemu_vblank_cadence_event(&meter, start + 52 * pal, pal, false);
    g_assert_false(xemu_vblank_cadence_get(&meter, start + 52 * pal, &percent));
    xemu_vblank_cadence_event(&meter, start + 53 * pal, pal, true);
    g_assert_false(xemu_vblank_cadence_get(&meter, start + 53 * pal, &percent));
    drive(&meter, start + 54 * pal, pal, pal, 51);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 105 * pal), 100,
                                   0.001);
}

static void test_stale_boundary(void)
{
    XemuVBlankCadence meter = { 0 };
    float percent = -1;
    drive(&meter, start, pal, pal, 51);
    int64_t last = start + 51 * pal;
    g_assert_true(
        xemu_vblank_cadence_get(&meter, last + XEMU_VBLANK_STALE_NS, &percent));
    g_assert_false(xemu_vblank_cadence_get(
        &meter, last + XEMU_VBLANK_STALE_NS + 1, &percent));
}

static void test_restart_after_stall(void)
{
    XemuVBlankCadence meter = { 0 };
    float percent = -1;
    drive(&meter, start, pal, pal, 51);
    int64_t restart = start + 51 * pal + XEMU_VBLANK_STALE_NS + 1;
    xemu_vblank_cadence_event(&meter, restart, pal, true);
    g_assert_false(xemu_vblank_cadence_get(&meter, restart, &percent));
    for (int i = 1; i <= 26; i++) {
        xemu_vblank_cadence_event(&meter, restart + i * 2 * pal, pal, true);
    }
    g_assert_cmpfloat_with_epsilon(reading(&meter, restart + 52 * pal), 50,
                                   0.001);
}

static void test_mode_change(void)
{
    XemuVBlankCadence meter = { 0 };
    float percent = -1;
    drive(&meter, start, ntsc, ntsc, 61);
    int64_t changed = start + 62 * ntsc;
    xemu_vblank_cadence_event(&meter, changed, pal, true);
    g_assert_false(xemu_vblank_cadence_get(&meter, changed, &percent));
    drive(&meter, changed + pal, pal, pal, 51);
    g_assert_cmpfloat_with_epsilon(reading(&meter, changed + 52 * pal), 100,
                                   0.001);
}

static void test_clock_discontinuity(void)
{
    XemuVBlankCadence meter = { 0 };
    float percent = -1;
    drive(&meter, start, pal, pal, 51);
    g_assert_false(xemu_vblank_cadence_get(&meter, start, &percent));
    xemu_vblank_cadence_event(&meter, start, pal, true);
    g_assert_false(xemu_vblank_cadence_get(&meter, start, &percent));
    drive(&meter, start + pal, pal, pal, 51);
    int64_t last = start + 52 * pal;
    xemu_vblank_cadence_event(&meter, last, pal, true);
    g_assert_false(xemu_vblank_cadence_get(&meter, last, &percent));
}

static void test_unavailable(void)
{
    XemuVBlankCadence meter = { 0 };
    float percent = -123;
    g_assert_false(xemu_vblank_cadence_get(&meter, start, &percent));
    g_assert_cmpfloat(percent, ==, -123);
    drive(&meter, start, pal, pal, 51);
    g_assert_false(xemu_vblank_cadence_get(&meter, start + 51 * pal, NULL));
    xemu_vblank_cadence_event(&meter, 0, pal, true);
    g_assert_false(meter.valid);
    drive(&meter, start, pal, pal, 51);
    xemu_vblank_cadence_event(&meter, start + 52 * pal, 0, true);
    g_assert_false(meter.valid);
    xemu_vblank_cadence_event(&meter, start + 53 * pal, -1, true);
    g_assert_false(meter.valid);
}

static void test_smoothing(void)
{
    XemuVBlankCadence meter = { 0 };
    drive(&meter, start, pal, pal, 50);
    for (int i = 1; i <= 25; i++) {
        xemu_vblank_cadence_event(&meter, start + 50 * pal + i * 2 * pal, pal,
                                  true);
    }
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 100 * pal), 80,
                                   0.001);
}

static void test_rendered_fps_is_not_input(void)
{
    XemuVBlankCadence meter = { 0 };
    int rendered_frames = 0;
    for (int i = 0; i <= 120; i++) {
        xemu_vblank_cadence_event(&meter, start + i * ntsc, ntsc, true);
        rendered_frames += i > 0 && i % 2 == 0;
    }
    g_assert_cmpint(rendered_frames, ==, 60);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 120 * ntsc), 100,
                                   0.001);
}

static void test_guest_work_is_not_global_speed(void)
{
    XemuVBlankCadence normal = { 0 }, slow = { 0 };
    uint64_t normal_work = 0, slow_work = 0;
    /* Fabricated work streams prove what the API cannot observe. Native
     * guest-work contention evidence is preserved separately in the review.
     */
    for (int i = 0; i <= 61; i++) {
        int64_t now = start + i * ntsc;
        xemu_vblank_cadence_event(&normal, now, ntsc, true);
        xemu_vblank_cadence_event(&slow, now, ntsc, true);
        normal_work += 4096;
        slow_work += 512;
    }
    g_assert_cmpuint(slow_work * 8, ==, normal_work);
    g_assert_cmpfloat(reading(&normal, start + 61 * ntsc), ==,
                      reading(&slow, start + 61 * ntsc));
    g_assert_cmpfloat_with_epsilon(reading(&slow, start + 61 * ntsc), 100,
                                   0.001);
    /* There is intentionally no numeric global-speed output in this API. */
}

static void test_burst_is_not_clamped_to_target(void)
{
    XemuVBlankCadence meter = { 0 };
    drive(&meter, start, pal / 4, pal, 201);
    g_assert_cmpfloat_with_epsilon(reading(&meter, start + 201 * (pal / 4)),
                                   400, 0.001);
}

static void test_large_monotonic_time(void)
{
    XemuVBlankCadence meter = { 0 };
    int64_t from = INT64_MAX - INT64_C(4000000000);
    drive(&meter, from, pal, pal, 51);
    g_assert_cmpfloat_with_epsilon(reading(&meter, from + 51 * pal), 100,
                                   0.001);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/cadence/ntsc-60", test_ntsc);
    g_test_add_func("/xemu/cadence/fractional-reference",
                    test_fractional_reference);
    g_test_add_func("/xemu/cadence/pal-50", test_pal);
    g_test_add_func("/xemu/cadence/half", test_half_cadence);
    g_test_add_func("/xemu/cadence/quarter", test_quarter_cadence);
    g_test_add_func("/xemu/cadence/pause-resume", test_pause_and_resume);
    g_test_add_func("/xemu/cadence/stale-boundary", test_stale_boundary);
    g_test_add_func("/xemu/cadence/restart-after-stall",
                    test_restart_after_stall);
    g_test_add_func("/xemu/cadence/mode-change", test_mode_change);
    g_test_add_func("/xemu/cadence/clock-discontinuity",
                    test_clock_discontinuity);
    g_test_add_func("/xemu/cadence/unavailable", test_unavailable);
    g_test_add_func("/xemu/cadence/smoothing", test_smoothing);
    g_test_add_func("/xemu/cadence/independent-of-rendered-fps",
                    test_rendered_fps_is_not_input);
    g_test_add_func("/xemu/cadence/guest-work-counterexample",
                    test_guest_work_is_not_global_speed);
    g_test_add_func("/xemu/cadence/overservice",
                    test_burst_is_not_clamped_to_target);
    g_test_add_func("/xemu/cadence/large-monotonic-time",
                    test_large_monotonic_time);
    return g_test_run();
}
