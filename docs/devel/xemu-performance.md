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

The underlying calibrated APU speed-band thresholds remain red below 95%,
normal between 95% and 105%, and fast above 105%. The whole-row glow treats
both normal and fast as green. CPU/NV2A fallback and NV2A trend colors are
explained below.
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

Additional OSD diagnostics show:

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


## Whole-row glow (experimental)

The OSD illuminates the entire row (both text and a faint pulsating
background). Red signals a warning, green signals a favorable *indicator*, and
unavailable data stays neutral gray. PFIFO and flips have their own rows, so
their state can differ; Stalls/s remains one row for all four entry categories.

- APU uses the validated audio cadence: red below 95%, green at or above 95%
  (including 95–105% nominal). Disabled/stale DSP measurements remain gray.
- CPU and NV2A prioritize calibrated speed if ever available. Until such a
  reference exists, the entire row may glow according to **host-thread
  pressure only**: red at or above 90% of one core, green at or below 60%,
  neutral between. An on-screen note explicitly says this is host load,
  **not** speed. No speed percent is inferred and `N/D` remains unchanged.
- PFIFO and flips show changes relative to their own recent, slowly adapting
  reference, never an absolute hardware target. Once five distinct valid
  samples have arrived, PFIFO dropping below 70% of its reference or flips
  below 75% makes the row red. Stable rates show green. Very low activity
  stays gray rather than being reported as an error.
- Stalls/s glows red only when observed stall *entries* surge over
  1.8× the recent level + 5/s, with at least 12/s. Otherwise it shows
  green after warm-up. High but stable stall counts can be normal.
- Invalid/paused/collecting counters remove illumination. History resets
  on performance generation changes; cached frames do not advance history.
  The 250ms UI sampling cadence, telemetry locks and emulation timing stay
  unchanged.

These colors are **trends and host-load hints, not a diagnostic pass/fail or
a measured Xbox emulation-speed percentage**. Scene transitions, menus, game
speed changes and naturally paced stalls can cause temporary red rows.

## Tests

The existing `tests/unit/test-xemu-performance.c` covers rates, irregular
intervals, values over 100%, stale/invalid samples and rolling-window resets.
The fourth TAP group checks the rate-band boundaries (including exactly 95/105),
invalid/non-finite rates, pause/warm-up/inactive/stale states, disabled/limited
DSPs and independence from host utilization. Three additional groups check
that uncalibrated speed stays unavailable, NV2A rates use actual durations and
reject invalid snapshots/wraps, and stall counts are independent of poll count
(including disabled collection and cross-generation batches). Two more groups
check host-pressure validity and whole-row trend-band behavior. It can also run
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
