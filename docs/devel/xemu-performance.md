<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Performance OSD

Toggle **Debug > Performance OSD** to draw three compact rows directly over the
video, including in fullscreen. The OSD uses ImGui's foreground draw list:
there is no window, title bar, close button, background, focus or input target.
Text is shadowed for contrast, anchored at the upper right below the menu bar,
and shrinks to fit narrow viewports. It inherits the current UI font size.
The session-only toggle defaults to off; disabling it stops collection.

## Emulation rate versus host utilization

| Row | Rate field | Separate host field |
| --- | --- | --- |
| CPU speed | `N/D`: no calibrated Xbox execution-speed counter | vCPU thread CPU time / wall time |
| NV2A speed | `N/D`: flips do not establish original-hardware speed | PFIFO thread CPU time / wall time |
| APU @48kHz | Completed sample frames / (48,000 × elapsed seconds) | APU thread CPU time / wall time |

The APU field measures **audio pipeline cadence**, not overall game speed,
perceived playback speed or emulation accuracy. It is shown only with a valid,
fresh window, an active pipeline and both GP and EP enabled in realtime mode.
Limited or disabled DSP execution cannot earn a colored "100%". Configurations
that legitimately use GP alone are conservatively `N/D` in this version.

Only the trusted APU rate percentage receives speed colors:

- Red below 95%.
- White from 95% through 105%, inclusive.
- Green above 105%.

Labels, unavailable values, status and host percentages remain neutral gray.
Host CPU percentages indicate consumption of one host core (100%), not Xbox
speed. They are read using `GetThreadTimes` on Windows and a thread CPU clock
on Linux; unsupported hosts/clock failures show `N/D`. NV2A host load is not
physical GPU utilization. CPU work can include synchronous device operations.

Do not infer CPU speed from QEMU virtual time, GPU speed from FPS/60, or global
speed by averaging component percentages. A 30 FPS title is not automatically
running at 50%. No guest execution, DSP budgets, clock or pacing policy is
changed by this OSD. Existing audio budget and VP/GP/EP counters remain intact
but are not displayed in the compact OSD.

## Sampling and lifecycle

The existing collector updates at most every 250 ms, using four intervals
(approximately one second) and actual elapsed time. Pause/resume, reset,
snapshot restore, visibility changes and long UI stalls invalidate the window.
Audio activity/mode transitions invalidate audio comparisons independently.
Paused, collecting, inactive, stale and limited/off states show neutral `N/D`
in place of an untrustworthy rate; host load remains separately available.

Control/sampling run under the BQL. The APU publishes cumulative counters under
a short-lived mutex; the sampler copies them before reading thread clocks or
drawing. Generation tokens reject in-flight work across lifecycle transitions.
Telemetry is not serialized into save states. An active pipeline with no
completions for over one second is stale. If the UI itself blocks, it cannot
refresh the OSD.

## CPU/NV2A diagnostics and validity

The collector now separates `speed_valid[]`/`speed_percent[]` from host
`cpu_valid[]`/`cpu_percent[]`. Only the APU cadence has a reference in this
version; the reducer never marks CPU or NV2A speed valid. Increasing instruction
throughput, host load, PFIFO work or flip rate cannot make those fields valid.
No TCG instrumentation or automatic `icount` activation is introduced. Xbox
TSC and NV2A PTIMER derive from virtual time, so neither supplies a measured
hardware-relative work rate. The supplied `deep-research-report.md` informed
these choices; no hardware timing baseline is assumed.

Two additional neutral-gray OSD lines show:

- **PFIFO Mword/s**: completed method-data words at the existing DMA_DCOUNT
  update, excluding pushbuffer headers, jumps and calls. It is not a byte rate,
  backlog estimate, drain ratio, GPU completion measurement or speed percentage.
- **flips/s**: READ_3D increment events at the existing profiling site. These
  are guest flip notifications, not a count of distinct rendered images.
- **Stalls/s F/N/C/A**: observed entries into flip, NOP, context-switch and
  FIFO-access blocking conditions. Repeated polling of the same active reason
  does not increment its count. This is an event rate, not blocked duration.

The original order and short-circuit evaluation of stall checks are preserved.
Only transitions observed by those checks can be counted; changes between
checks or hidden behind an earlier blocking reason are not reconstructed.
The first observed blocked state in a collection session counts as one entry.
These rates may reflect normal pacing, not necessarily a bottleneck.

Counters live in NV2A state, protected by its **existing** PFIFO lock, which is
already held at all producer sites (including CPU-written flip increments).
No new mutex acquisition, clock read, GPU fence or atomic increment occurs per
method. A session-token read at pusher entry and flip notification keeps
collection aligned with the existing enable/pause/reset generation. Work from
a pusher batch spanning a generation switch is conservatively discarded if a
new-generation producer has already reset the counters.

The 250 ms sampler uses one nonblocking try-lock to copy this tiny snapshot;
it never waits for PFIFO while holding the BQL, and it releases the telemetry
mutex before attempting the PFIFO lock. A busy lock, old generation, missing
endpoint, or counter regression makes diagnostics `N/D`, not stale values or
fabricated zeroes. Newly enabled idle PFIFO can remain `N/D` until a producer
observes the new generation. A valid unchanged counter represents zero observed
work/events. Rates use the same elapsed-time-weighted rolling window as audio.
The source is unregistered before device teardown; counters are not migrated.
No submitted-work/backlog estimate is inferred from GET/PUT subtraction because
pushbuffer control flow and wrapping do not provide a reliable linear queue.

## Tests

The existing `tests/unit/test-xemu-performance.c` covers rates, irregular
intervals, values over 100%, stale/invalid samples and rolling-window resets.
The fourth TAP group checks the color boundaries (including exactly 95/105),
invalid/non-finite rates, pause/warm-up/inactive/stale states, disabled/limited
DSPs and independence from host utilization. Three additional groups check
that uncalibrated speed stays unavailable, NV2A rates use actual durations and
reject invalid snapshots/wraps, and stall counts are independent of poll count
(including disabled collection and cross-generation batches). It can also run
standalone:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  tests/unit/test-xemu-performance.c -o /tmp/test-xemu-performance
/tmp/test-xemu-performance
```

Before promoting the draft, validate the full MSVC/Linux emulator builds and
real gameplay with OpenGL/Vulkan, fullscreen, DPI changes, pause/reset/snapshots
and both DSP modes. Compare performance with the OSD on/off. CPU/NV2A speed
remains unavailable until a trustworthy hardware-relative reference exists.

For functional regression testing, run the Roswell CPU suites (FPU/MMX/SSE/
testfloat) and NV2A M2MF with OSD off/on. Compare both failures and suppressed
failures with the base commit; a green summary alone is insufficient. Use the
same XBE/output checks on real Xbox hardware for any future timing baseline.
Do not gate CI on a hardware-relative speed threshold for variable cloud hosts.

## Hardware benchmark collection pilot

The optional [Xbox benchmark kit](../../tools/xbox-benchmark/README.md) collects
fixed CPU workloads and NV2A clear/submission/completion workloads in one
unattended session. Its `xbox-benchmark` device is instantiated only with an
explicit `-device` option and records monotonic host durations at benchmark
boundaries. It does not instrument gameplay or feed the OSD. The offline
comparator rejects incompatible, incomplete, incorrect, short or unstable data.

Hardware volunteers must first validate the pilot XBE on stock consoles. Even
with those baselines, per-benchmark ratios are not live CPU/NV2A game-speed
percentages. The current N/D fields therefore remain correct; a broader model
would require independent validation and an explicit estimation label.
