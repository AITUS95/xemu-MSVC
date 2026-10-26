<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Pilot 2 validation

This is an experimental collection package, not an original-Xbox baseline.
The PR description records the downloadable CI artifact and its final runtime
validation separately; archive that exact artifact, manifest and source.

## Checks performed

- XBE and hardware/xemu ISO variants built with pinned nxdk and upstream sources.
  Application C/C++ compiles with `-Wall -Wextra -Werror`; upstream SDK/libc++
  and linker warnings remain and are not claimed to be absent.
- 21 Python tests: base checksums/identity/completion and extension completeness,
  host-clock formula, raw nested failures, skips, failed warm-ups, CPU fallback,
  changed work counts/physical phases, sequence/TSC errors, timing noise and
  hardware-clock disagreement. These are synthetic parser tests, not timings.
- All 24 full base CPU workloads verified natively against independent formulas.
- Existing C tests: benchmark interval protocol and seven performance groups.
  No emulator timing, scheduling, hardware emulation or OSD code changed here.
- A development XBE cold-booted with the PR's Linux CI emulator on an isolated
  copy-on-write HDD and copied EEPROM. OpenGL/Mesa llvmpipe under Xvfb, 1x scale.
  Saved FATX results were extracted: **272 base + 232 extension rows matched
  504 host intervals**, with complete CSV/JSON output. All base CPU checksums
  and framebuffer checks passed, including warm-ups.

## Failures are evidence, not suppressed passes

The development run's 11 upstream CPU families produced **8 passes, 3 failures**:
x87 precision control, MMX/x87 aliasing, and SSE reciprocal/rsqrt approximations.
The same three families fail in the original raw Ubuntu Roswell results from
[CI run 40370539499](https://github.com/AITUS95/xemu-MSVC/actions/runs/40370539499).
This checks failure-family parity, not every event or compiler configuration.

The M2MF driver reported **native execution unavailable**. All 18 transfer
workloads therefore produced unavailable rows; the 19 original NV2A validation
families were explicitly recorded as not executed, never passed via `memcpy`.
The original Roswell run reports 15 M2MF passes and 4 failures, but its driver
can fall back to CPU copies. Those pass counts alone cannot establish native
NV2A performance. The new native-state gate is intentionally stricter.

The combined JSON summary counts unavailable cases as failures (8 passed,
22 failed in that development run); their raw messages distinguish the three
CPU failures from the nineteen unavailable M2MF checks. COMPLETE means the
collection finished, not that all checks passed.

## Still unvalidated

No real Xbox boot/timing baseline, all console revisions/video modes, successful
native M2MF execution/guard checks, or broad rendering/shader/texture coverage.
A small hardware pilot is needed before community-wide distribution. Cases
that remain too short/noisy are diagnostic only. Compiler changes create new
binary identities: never mix packages, even if they have the same suite name.

No per-game CPU/NV2A speed is derived from these benchmarks; OSD remains N/D.
An unattended session improves reuse but cannot cover every future workload.
