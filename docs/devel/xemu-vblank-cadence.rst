:orphan:

VBlank cadence diagnostics
==========================

The timing overlay deliberately separates two statements::

    VBlank cadence: 100.0%
    Global speed: unverified

The first value measures serviced VBlank callbacks against the configured
nominal PAL/NTSC reference (50/60 Hz). It is independent of rendered FPS,
NV2A flip count and the host monitor's refresh rate. A game producing 30
frames per second can have normal 60 Hz VBlank servicing.

What is measured
----------------

``process_vblank()`` records one event after ``graphic_hw_update()`` returns.
For N intervals between completed callbacks in a host-monotonic window of
duration dt, the sample is ``100 * N * expected_interval / dt``. Windows
last at least one second. Subsequent samples use the existing exponential
smoothing: 60% previous reading and 40% new sample. Readings above 100% are
not clamped to the target.

This measures callback cadence, not presentation latency, successful guest
interrupt handling or all of the work of a simulated video frame. Uneven
callback spacing can still average to the expected cadence.

The reference is the interval already configured for xemu's VBlank source.
This patch does not change the source, video frequencies, render pacing,
limiters, CPU speed, QEMU clocks or device synchronization. The helper can
accept a fractional interval, but does not change the configured 50/60 Hz
scheduler to a different physical television timing.

Availability and synchronization
-------------------------------

The overlay shows ``VBlank cadence: --`` during initial measurement and
after invalidation, and ``VBlank cadence: paused`` while paused. VM state
changes reset the meter even if no VBlank callback observes a short pause.
Clock regressions, repeated timestamps, reference changes and gaps longer
than 500 ms start a fresh window. A stale reading is unavailable, not 0%.

The VBlank callback, VM state handler and UI getter all access the small
meter under the existing Big QEMU Lock. No atomics, additional locks,
sampling thread, per-event allocation, file I/O or per-instruction callbacks
are introduced. The getter asserts the BQL requirement. The View menu toggle
controls the overlay only and is not persisted.

Why global speed is unverified
------------------------------

xemu's VBlank heartbeat is host paced. CPU execution can fall behind while
that heartbeat continues at its expected cadence. GPU or audio work can
also lag without changing the callback count. A 100% cadence reading
therefore cannot verify overall Xbox temporal progress or hardware accuracy.
Conversely, few flips or an idle CPU can be intentional.

The helper has no numeric global-speed output. The UI always displays
``Global speed: unverified`` while the overlay is visible, including when
cadence is valid, unavailable or paused. It never derives global speed from
FPS or from this percentage.

The CPU-cycle prototype, TCG plugin observer, multi-device CSV telemetry and
NV2A/SDL/Vulkan shutdown fix are outside this patch. In particular, the
experimental observer with a material measured throughput cost is not
loaded, built or sampled by the ordinary timing overlay.

Validation
----------

Run the lightweight deterministic tests with::

    meson test -C build --print-errorlogs test-xemu-vblank-cadence

They cover nominal NTSC/PAL rates, a fractional reference, half/quarter
servicing, pause/resume, stalls, recovery, discontinuities, mode changes,
unavailable values, smoothing, overservice and large monotonic timestamps.
The rendered-frame and synthetic guest-work counterexamples show why
identical VBlank cadence does not imply identical game/CPU progress. Those
tests are mathematical counterexamples, not physical Xbox measurements.

Separate native diagnostic evidence has also demonstrated sharply reduced
guest work while host-paced VBlank remains near 100%. That costly diagnostic
instrumentation is preserved separately and does not run during ordinary
emulation or this unit test. Neither form of evidence calibrates Xbox cycles.

A future global percentage would need a validated guest temporal reference,
correct treatment of idle time and causal completion across CPU, NV2A, APU,
DMA, timers and interrupts. Host-derived timers or an uncalibrated constant
instruction cost are insufficient. That work does not block this bounded,
explicitly labelled cadence diagnostic.
