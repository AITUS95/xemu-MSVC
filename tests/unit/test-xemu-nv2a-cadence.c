/* Existing GPU profiler rates, without a global-speed estimate.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "ui/xemu-nv2a-cadence.h"

static const int64_t start = INT64_C(1000000000);

static void drive(XemuNV2ACadence *meter, int64_t from, uint32_t count,
                  unsigned int rate, unsigned int fps)
{
    for (int i = 0; i <= 4; i++) {
        int64_t now = from + i * XEMU_NV2A_POLL_NS;
        xemu_nv2a_cadence_sample(meter, now, count + i * rate / 4, fps,
                                 now / 1000);
    }
}

static void test_nominal_rates(void)
{
    const unsigned int rates[] = { 30, 60 };
    for (size_t i = 0; i < G_N_ELEMENTS(rates); i++) {
        XemuNV2ACadence meter = { 0 };
        float flips = -1;
        unsigned int fps = 0;
        bool fps_available = false;
        g_assert_false(
            xemu_nv2a_cadence_get(&meter, start, &flips, &fps, &fps_available));
        g_assert_cmpfloat(flips, ==, -1);
        drive(&meter, start, 100, rates[i], rates[i]);
        g_assert_true(xemu_nv2a_cadence_get(&meter, start + XEMU_NV2A_WINDOW_NS,
                                            &flips, &fps, &fps_available));
        g_assert_cmpfloat_with_epsilon(flips, rates[i], 0.001);
        g_assert_cmpuint(fps, ==, rates[i]);
        g_assert_true(fps_available);
    }
}

static void test_independent_rates(void)
{
    XemuNV2ACadence meter = { 0 };
    float flips = 0;
    unsigned int fps = 0;
    bool available = false;
    drive(&meter, start, 0, 30, 60);
    g_assert_true(xemu_nv2a_cadence_get(&meter, start + XEMU_NV2A_WINDOW_NS,
                                        &flips, &fps, &available));
    g_assert_cmpfloat_with_epsilon(flips, 30, 0.001);
    g_assert_cmpuint(fps, ==, 60);
    g_assert_true(available);
    /* No nominal video frequency or global percentage enters this API. */
}

static void test_zero_flips_stale_fps(void)
{
    XemuNV2ACadence meter = { 0 };
    float flips = -1;
    unsigned int fps = 0;
    bool available = true;
    for (int i = 0; i <= 4; i++) {
        xemu_nv2a_cadence_sample(&meter, start + i * XEMU_NV2A_POLL_NS, 100, 60,
                                 0);
    }
    g_assert_true(xemu_nv2a_cadence_get(&meter, start + XEMU_NV2A_WINDOW_NS,
                                        &flips, &fps, &available));
    g_assert_cmpfloat(flips, ==, 0);
    g_assert_false(available);
}

static void test_pause_reset(void)
{
    XemuNV2ACadence meter = { 0 };
    float flips = -1;
    unsigned int fps = 0;
    bool available = false;
    drive(&meter, start, 0, 60, 60);
    xemu_nv2a_cadence_reset(&meter);
    g_assert_false(xemu_nv2a_cadence_get(&meter, start + XEMU_NV2A_WINDOW_NS,
                                         &flips, &fps, &available));
    /* Ignore both elapsed paused time and counts accumulated before reset. */
    int64_t resumed = start + INT64_C(10000000000);
    drive(&meter, resumed, 10000, 30, 30);
    g_assert_true(xemu_nv2a_cadence_get(&meter, resumed + XEMU_NV2A_WINDOW_NS,
                                        &flips, &fps, &available));
    g_assert_cmpfloat_with_epsilon(flips, 30, 0.001);
}

static void test_counter_decrease_and_wrap(void)
{
    XemuNV2ACadence meter = { 0 };
    drive(&meter, start, 100, 30, 30);
    g_assert_true(meter.valid);
    xemu_nv2a_cadence_sample(&meter, start + 5 * XEMU_NV2A_POLL_NS, 1, 30, 0);
    g_assert_false(meter.valid);
    xemu_nv2a_cadence_reset(&meter);
    xemu_nv2a_cadence_sample(&meter, start, UINT32_MAX - 1, 60, 0);
    xemu_nv2a_cadence_sample(&meter, start + XEMU_NV2A_POLL_NS, 0, 60, 0);
    g_assert_false(meter.valid);
    g_assert_cmpuint(meter.window_count, ==, 0);
}

static void test_sample_availability(void)
{
    XemuNV2ACadence meter = { 0 };
    float flips = 0;
    unsigned int fps = 0;
    bool available = false;
    drive(&meter, start, 0, 60, 60);
    int64_t last = start + XEMU_NV2A_WINDOW_NS;
    g_assert_true(xemu_nv2a_cadence_get(&meter, last + XEMU_NV2A_STALE_NS,
                                        &flips, &fps, &available));
    g_assert_false(xemu_nv2a_cadence_get(&meter, last + XEMU_NV2A_STALE_NS + 1,
                                         &flips, &fps, &available));
    g_assert_false(xemu_nv2a_cadence_get(&meter, last, NULL, &fps, &available));
    g_assert_false(
        xemu_nv2a_cadence_get(&meter, last, &flips, NULL, &available));
    g_assert_false(xemu_nv2a_cadence_get(&meter, last, &flips, &fps, NULL));
    xemu_nv2a_cadence_sample(&meter, last + XEMU_NV2A_STALE_NS + 1, 100, 60, 0);
    g_assert_false(meter.valid);
    xemu_nv2a_cadence_sample(&meter, start, 100, 60, 0);
    g_assert_false(meter.valid);
    g_assert_cmpint(meter.window_start_ns, ==, start);
    xemu_nv2a_cadence_sample(&meter, start, 100, 60, 0);
    g_assert_false(meter.valid);
    xemu_nv2a_cadence_sample(&meter, 0, 100, 60, 0);
    g_assert_cmpint(meter.sample_ns, ==, 0);
}

static void test_fps_freshness(void)
{
    XemuNV2ACadence meter = { 0 };
    float flips = 0;
    unsigned int fps = 0;
    bool available = false;
    drive(&meter, start, 0, 60, 60);
    int64_t now = start + XEMU_NV2A_WINDOW_NS + XEMU_NV2A_POLL_NS;
    xemu_nv2a_cadence_sample(&meter, now, 75, 60,
                             (now - XEMU_NV2A_STALE_NS) / 1000 - 1);
    g_assert_true(xemu_nv2a_cadence_get(&meter, now, &flips, &fps, &available));
    g_assert_false(available);
    now += XEMU_NV2A_POLL_NS;
    xemu_nv2a_cadence_sample(&meter, now, 90, 60, now / 1000 + 1);
    g_assert_true(xemu_nv2a_cadence_get(&meter, now, &flips, &fps, &available));
    g_assert_false(available);
    now += XEMU_NV2A_POLL_NS;
    xemu_nv2a_cadence_sample(&meter, now, 105, 60, now / 1000);
    g_assert_true(xemu_nv2a_cadence_get(&meter, now, &flips, &fps, &available));
    g_assert_true(available);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/nv2a-cadence/30-and-60", test_nominal_rates);
    g_test_add_func("/xemu/nv2a-cadence/independent-rates",
                    test_independent_rates);
    g_test_add_func("/xemu/nv2a-cadence/zero-flips", test_zero_flips_stale_fps);
    g_test_add_func("/xemu/nv2a-cadence/pause-reset", test_pause_reset);
    g_test_add_func("/xemu/nv2a-cadence/counter-rollback",
                    test_counter_decrease_and_wrap);
    g_test_add_func("/xemu/nv2a-cadence/availability",
                    test_sample_availability);
    g_test_add_func("/xemu/nv2a-cadence/fps-freshness", test_fps_freshness);
    return g_test_run();
}
