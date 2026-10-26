<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Pilot validation

Before publication, the final local XBE was cold-booted in the modified xemu
Linux build using a separate copy-on-write HDD and copied EEPROM. The session
finished automatically and its saved FATX CSV was extracted and passed through
`read_run` and `read_host`: all **272 guest rows and 272 host intervals** matched,
all **24 CPU workload checksums** matched independent formulas in every trial,
and all **9 GPU framebuffer checks** passed in every trial, including warm-ups.

This was OpenGL with Mesa llvmpipe under Xvfb, 1x scale, software rendering. It
checks execution/completion and data collection, not representative host speed.
The local executable SHA-256 was:

```
e90782f4c0e3ad7e0f2681c8dbd9e8753df21083c7db7c9cc904805d28c4a4e6
```

CI builds may use a different compiler and have a different executable hash.
Use each artifact's own manifest and the identical XBE for both hardware and
xemu runs; do not combine packages. The above runtime test is not a claim that
all future rebuilt binaries have been boot-tested.

Other checks performed:

- Full Linux emulator build, including the opt-in benchmark device.
- Meson `test-xemu-performance` (seven TAP groups) and
  `test-xemu-benchmark` (interval protocol invariants).
- Both C tests with AddressSanitizer/UndefinedBehaviorSanitizer; leak detection
  disabled for the ptrace environment.
- Twelve Python comparison/rejection tests.
- All 24 full CPU workloads run natively and checked against independent
  expected results (in addition to the guest execution above).
- nxdk XBE and both ISO variants built; application sources compiled with
  `-Wall -Wextra -Werror`. Existing SDK/linker and emulator third-party warnings
  are not claimed to be absent.
- `git diff --check`; checkpatch: zero errors, one generic new-files/MAINTAINERS
  reminder.

Not yet validated: original Xbox hardware or timing baselines, all console/video
revisions, Vulkan execution of this suite, MSVC/macOS integration, Roswell
CPU/M2MF failure/suppression parity, or gameplay OSD on/off overhead. No global
CPU/NV2A percentage or hardware-relative performance claim is enabled.
