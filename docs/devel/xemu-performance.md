<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Component performance overlay

Open **Debug > Component performance**. Close the window or uncheck the menu
item to stop collecting measurements. The window remains visible when the menu
bar hides and can be moved in windowed or fullscreen mode. This setting is
session-only and defaults to off.

The display updates at most every 250 ms and averages over the last four
sampling intervals (approximately one second). The actual interval length is
used in every ratio. Initial samples, a UI stall longer than one second,
pause/resume, reset, and snapshot load invalidate the previous window.

## What the numbers mean

| Row | Meaning of 100% |
| --- | --- |
| CPU | The vCPU host thread consumed one CPU-second per wall-clock second |
| NV2A worker | The PFIFO host thread consumed one CPU-second per wall-clock second |
| APU worker | The audio host thread consumed one CPU-second per wall-clock second |
| Audio cadence | The APU completed 48,000 sample frames per wall-clock second |
| Audio budget used | Audio frame work took as long as the audio duration produced |
| VP / GP / EP | The phase's measured elapsed time occupied the entire wall-clock interval |

Thread CPU time is read with `GetThreadTimes` on Windows and a POSIX thread CPU
clock on Linux. Unsupported hosts and failed clock reads show `N/A`. The NV2A
row is CPU work in the submission thread, **not** GPU hardware utilization.
Thread totals include driver/kernel work charged to that thread; CPU includes
synchronous device work. They are not exclusive hardware-component profiles.

Audio cadence counts completed APU frames multiplied by
`NUM_SAMPLES_PER_FRAME`, with 48 kHz as the reference. Stereo channels are not
counted twice. VP/GP/EP are timed around their existing execution scopes. EP's
less frequent execution is naturally accumulated over the same window. Phase
elapsed time includes descheduling and any waits inside the scope. Audio frame
work excludes the separate pacing throttle. `Audio budget used` can exceed
100%; it is not clamped.

These figures do not measure Xbox hardware accuracy, completion of emulator
development, or the speed of the whole game. A 30 FPS title is not automatically
running at 50%. Limited DSP mode can maintain the audio cadence without
executing the same DSP work as realtime mode; both modes are displayed.

Do not add nested phase percentages to their parent or sum independent threads
into a supposed overall emulation percentage. Physical GPU time and HDD, DVD,
network, and USB metrics are deliberately `N/A` in this version.

## Sampling and synchronization

All control operations and UI sampling run under the BQL. Device thread
registrations are cleared before thread teardown. The APU publishes cumulative
counters under a separate short-lived mutex; the sampler copies them and
releases that mutex before calling host clock APIs or drawing ImGui. The
telemetry mutex never acquires a device lock or the BQL.

A generation token prevents an in-flight frame from publishing into a new
session after reset, pause, or a visibility change. Audio enable/mode changes
also invalidate audio comparisons, even if the mode changes back between UI
samples. Telemetry is not migrated or serialized into save states.

`Paused`, `Collecting...`, `Inactive`, `Disabled`, and `Stale / no completions`
are distinct states. A previously valid audio percentage is withdrawn if an
active pipeline has not completed work for over one second. This timeout is
checked when the UI can render; a blocked UI cannot refresh an overlay.

## Validation

`tests/unit/test-xemu-performance.c` tests the pure reducer and rolling window:
48/24 kHz equivalents, thread CPU percentages, irregular intervals, budget
values above 100%, zero/backward time, invalid clocks, stale/idle audio,
mode changes, and generation changes. It is registered with the unit suite and
can also be built independently:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  tests/unit/test-xemu-performance.c -o /tmp/test-xemu-performance
/tmp/test-xemu-performance
```

Before promoting the draft, validate MSVC Debug/Release and Linux builds;
exercise both Vulkan and OpenGL, 30/60 FPS software, fullscreen, idle audio,
limited/realtime DSP, pause/resume, reset and snapshot loading. Compare thread
percentages with an external profiler and compare frame times with the overlay
enabled/disabled to quantify measurement overhead. The default-off path must
not change emulated registers, timing policy, DSP execution budgets, or output.
