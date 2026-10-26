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

## Tests

The existing `tests/unit/test-xemu-performance.c` covers rates, irregular
intervals, values over 100%, stale/invalid samples and rolling-window resets.
Its fourth TAP group checks the color boundaries (including exactly 95/105),
invalid/non-finite rates, pause/warm-up/inactive/stale states, disabled/limited
DSPs and independence from host utilization. It can also run standalone:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  tests/unit/test-xemu-performance.c -o /tmp/test-xemu-performance
/tmp/test-xemu-performance
```

Before promoting the draft, validate the full MSVC/Linux emulator builds and
real gameplay with OpenGL/Vulkan, fullscreen, DPI changes, pause/reset/snapshots
and both DSP modes. Compare performance with the OSD on/off. CPU/NV2A speed
remains unavailable until a trustworthy hardware-relative reference exists.
