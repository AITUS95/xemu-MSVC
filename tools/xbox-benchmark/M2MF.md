<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# M2MF implementation and collection limits

## Root cause

The previous fork only defined the name of class `0x39`; PGRAPH did not dispatch
its methods. The driver's four-byte support probe therefore did not complete.
Its memcpy fallback made some original CI copy tests pass without executing an
emulated M2MF transfer. The collection gate correctly rejected those timings.
Changing the host GPU or forcing the native flag cannot fix missing commands.

## Implementation reused

This branch adapts Erik Abair's
[xemu PR #3052](https://github.com/xemu-project/xemu/pull/3052), reviewed at head
`158c5a133eed1b69f0688c751af035697957da8f`. That PR remains upstream work in
progress, not an upstream release guarantee. Its original tests are the pinned
nxdk low-level NV2A tests already included here.

The dispatcher stores class-0x39 DMA bindings, offsets, pitches, line count and
byte increments. BUFFER_NOTIFY executes the transfer, then publishes the
PTIMER/error/status completion record to the currently bound notifier. Renderer
callbacks download dirty source/destination surfaces and invalidate affected
surfaces/textures and vertex memory in both OpenGL and Vulkan.

Additional changes in this adaptation:

- Validate the entire source/destination footprint and notifier bounds before
  copying. No clipped transfer can receive a successful completion.
- Reject unsupported zero increments, upper format bits, line counts above the
  tested 2047-line limit, or nonzero BUFFER_NOTIFY modes. They remain pending
  and are logged; hardware exception/interrupt behavior is not fully modeled.
- Use QEMU RAM map/unmap write accounting and physical writes for completion,
  so code translations and dirty tracking are updated for shared system RAM.
- Reset M2MF state on reset. Save it in a named VMState subsection and clear it
  before loading older snapshots; the existing parent version-4 layout stays
  unchanged. Benchmark stopwatch devices remain intentionally unmigratable.

The transfer uses host software memory copies as an implementation of the
emulated NV2A engine. This is **not the guest driver's CPU memcpy fallback**:
commands pass through PFIFO/PGRAPH, move guest data and complete a real emulated
notifier. It does not mean the host RTX executes a hardware DMA copy. Timings
include the actual implementation's dispatch, memory and synchronization costs.

## Volunteer readiness

The package is suitable for a **small hardware pilot**, not yet a promise of one
successful run on every Xbox revision. One unattended run retains seven trials
plus warm-up for each of 63 workloads and 30 validation families. It cannot
cover all shaders, texture formats, instruction mixes or future regressions.

The revised package records the initial native probe's result and copied value
in validation JSON, checks logger write/flush failures before announcing
completion, checks probe allocation failure, and preserves a failed timed row
and wait error before aborting on a DMA timeout. It does not free buffers that
may still belong to the timed DMA operation, or falsely mark work complete.
Original adversarial upstream tests can still hang on unexpected hardware;
retain partial files and the last screen message. There is no proven safe
universal GPU reset/recovery routine for continuing after a hardware hang.

No original-Xbox timing baseline has been collected. All console/video revisions,
power-loss persistence, arbitrary DMA faults, overlapping/context-switch edge
cases and broad game regressions remain unvalidated. Send the pilot to a few
stock 64 MB consoles first, archive every raw outcome and only then decide
whether to ask a larger volunteer group for their one session. Do not mix
executables: this revision keeps the PILOT 2 schema but has a new XBE identity.

Untrusted cases remain unavailable. Even with a hardware baseline, per-workload
ratios do not establish live per-component speed in arbitrary games; OSD remains
N/D for CPU/NV2A.
