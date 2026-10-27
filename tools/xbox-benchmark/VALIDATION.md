<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Pilot 2 validation

This is an experimental collection package, not an original-Xbox baseline.
The PR description records the downloadable CI artifact and its final runtime
validation separately; archive that exact artifact, manifest and source.

## M2MF follow-up

The missing class-0x39 implementation was adapted from upstream PR #3052. See
[M2MF.md](M2MF.md) for changes and explicitly unsupported cases.

- Full Linux builds with OpenGL and Vulkan enabled succeeded.
- The **unchanged published PILOT 2 XBE** was tested with the new implementation:
  all 18 M2MF workloads and all 19 original NV2A validation families passed in
  OpenGL/Mesa llvmpipe. No forced native flag or guest memcpy fallback.
- The revised diagnostic XBE also completed on **Vulkan / RTX 5080 Laptop GPU**
  using PRIME offload: 504 guest/host intervals, all base output checks passed,
  all 144 M2MF trials (including warm-ups) passed with native=1, all 19 NV2A
  validation families passed. The initial probe returned zero and copied
  0xA55A1234. The three known CPU failures remain: total **27 pass / 3 fail**.
- An Xvfb run combining Mesa OpenGL presentation with NVIDIA Vulkan failed at
  display interop initialization, before guest intervals; the successful RTX
  run used the real X display with PRIME for both APIs. That failed attempt is
  not counted as Vulkan validation.
- New native M2MF unit checks exercise exact bounds, short buffers, zero work,
  zero increments, 64-bit footprint overflow, asymmetric strides, pitches and
  untouched guard/padding bytes. Passed normally and with ASan/UBSan.
- Cold snapshot smoke test passed: load a version-4 snapshot made by the old
  emulator, save/load a new snapshot containing the M2MF subsection, then reset.
  This does not establish in-flight transfer/context-switch snapshot coverage.
- Existing benchmark/performance C tests passed again (one plus seven groups).
- The 21 existing Python comparison/rejection tests still pass. The downloadable
  artifact's final identity and smoke-test results are recorded on the PR.

## Earlier PILOT 2 checks (before M2MF implementation)

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

## Remaining hardware/coverage limits

No real Xbox boot/timing baseline, all console revisions/video modes, or broad
rendering/shader/texture coverage. Native emulated M2MF is now validated as above;
that is not verification of the new XBE on a physical Xbox.
A small hardware pilot is needed before community-wide distribution. Cases
that remain too short/noisy are diagnostic only. Compiler changes create new
binary identities: never mix packages, even if they have the same suite name.

No per-game CPU/NV2A speed is derived from these benchmarks; OSD remains N/D.
An unattended session improves reuse but cannot cover every future workload.
