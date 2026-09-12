# Profiling TCG execution

The Xbox TCG path provides optional aggregate trace events. With the default
log trace backend, append these arguments when launching the emulator:

```text
-trace "xemu_tcg_*" -D tcg-profile.log
```

Counters are disabled unless either statistics event is enabled. They are
thread-local and cumulative since tracing was enabled, and reset when tracing
is disabled and re-enabled across CPU execution calls. Reports are emitted at
the next completed `cpu_exec` call after at least one second since the last
report. A halted CPU or a long execution call can delay a report. The final
partial interval is not automatically emitted on shutdown.

`xemu_tcg_exec_stats` reports:

- `elapsed_ns`: host real-time elapsed since collection started.
- `entries`: calls into generated code, not the number of guest instructions
  or translated blocks executed through direct chaining.
- `exit0`, `exit1`: normal returns from generated code through each exit slot.
- `requested`: returns marked `TB_EXIT_REQUESTED`.
- `other`: returns with the remaining exit-mask value.

An entry that leaves generated code through a longjmp does not increment a
normal return counter. The difference between entries and returns is therefore
not a count of interrupt requests or a specific exception type.

`xemu_tcg_lookup_stats` reports:

- `lookups`: calls to the indirect-branch lookup helper.
- `jump_misses`: helper lookups that miss the per-CPU jump cache.
- `table_misses`: those misses that also fail the translation-block table lookup.
- `generated`: successful block generations from the main execution loop.
- `chain_attempts`: calls to `tb_add_jump` from that loop, not confirmed new
  links and not the number of direct jumps executed by generated code.

Use differences between consecutive snapshots to calculate rates. For example,
`delta(jump_misses) / delta(lookups)` measures the helper jump-cache miss ratio;
only calculate it when the denominator is nonzero. Events must be enabled
together to correlate their counts. Do not enable per-block `exec_tb*` events
for this measurement, since their log volume can dominate the workload.

Compare the same game sequence, duration, renderer settings and warm-up period.
Collect frame times separately, and repeat with tracing disabled to assess
instrumentation overhead. Aggregate counts identify where to investigate;
they do not measure time spent in a function.

## Optional chaining diagnostics

The wildcard command above also enables `xemu_tcg_chain_stats` and
`xemu_tcg_hotspot`. For the original aggregate-only measurement, use
`-trace xemu_tcg_exec_stats -trace xemu_tcg_lookup_stats -D tcg-profile.log`.
Repeat the same scene with details disabled to assess their extra overhead.

`xemu_tcg_chain_stats` contains cumulative counts since detail collection was
enabled:

- `linked`: successful jump-slot claims and patches.
- `invalid`: link attempts rejected because the destination is invalid.
- `occupied`: link attempts rejected because the destination slot is nonzero
  (including a slot marked during invalidation).
- `two_pages`: a predecessor discarded because the destination spans two pages.
- `no_previous`: iterations reaching the linking step without a predecessor.
  This includes `two_pages`, initial iterations and cleared predecessors; these
  two counters are not disjoint.
- `no_tb`: normal returns without an identified final TB. These cannot supply
  a predecessor for direct linking.
- `samples`, `dropped`: selected return samples and samples omitted because
  the bounded address table was full.

The sampler selects every 4096th normal return (exit slot 0 or 1), excluding
requested exits and longjmp paths. Each reporting interval retains at most
256 distinct combinations of PC, return PC, CS base, cflags, exit slot and
address kind.
Existing combinations continue accumulating when the table is full. Up to
eight combinations with the largest retained counts are logged as
`xemu_tcg_hotspot`, then the table is cleared. Hotspot counts are per interval;
they are not cumulative and do not necessarily sum to all selected samples.
Periodic sampling can alias with guest loops, and table saturation biases
results toward earlier addresses. Treat this as candidate identification,
not an exact ranking or a measurement of CPU time.

`entry_pc=0` identifies the final returned non-PC-relative TB. `entry_pc=1`
means no final TB was supplied, or it uses `CF_PCREL` and therefore does not
store its runtime address. In either case, `pc` is the runtime entry address
passed by the execution loop, and flags and CS base describe the entry TB.
A chain may have run before the return. `resume_pc` is the CPU-reported PC
sampled after a normal return, not the start of the final TB or the address
of the instruction responsible for the exit. Pair it with the entry PC to
identify candidate guest paths. It is read only for selected samples.
Earlier logs without `resume_pc` used `tb->pc` even with `CF_PCREL`; their
zero addresses cannot identify hotspots and must not be compared as addresses.

`patchable=1` means the final TB has a generated
jump for that exit slot; it does not assert that the jump is currently linked
or safe to link. Inspect `cflags` using `include/exec/translation-block.h`,
especially `CF_NO_GOTO_TB` and `CF_NO_GOTO_PTR`. No TB pointers are retained
between samples, and collection does not change linking decisions.

## Vulkan texture counters

Open the video debug display and expand `Advanced`. The existing per-frame
counter graph includes these Vulkan counters (without the `NV2A_PROF_` prefix):

| Counter | Meaning |
| --- | --- |
| `TEX_BIND_CHECK` | Calls to texture binding preparation across all slots. |
| `TEX_BIND_CLEAN` | Calls that reuse all slots without preparing textures. |
| `TEX_BIND_REUSE` | Individual slots reusing their current image after checking its state; a sampler update may still occur. |
| `TEX_DIRTY_PAGE_TEST` | Dirty-bitmap queries over a texture or palette range, not a count of individual pages. |
| `TEX_DIRTY_PAGE_HIT` | Queries reporting at least one dirty page. |
| `TEX_SURFACE_SYNC_CHECK` | Requests to download dirty overlapping surfaces; a request may transfer nothing. |
| `TEX_HASH` | Content-hash passes, including palette data for indexed textures. |
| `TEX_HASH_UNCHANGED` | Hash passes confirming an existing image is unchanged, avoiding an upload. |
| `TEX_CACHE_MISS` | Texture preparations that need a new image after the cache lookup. |

Correlate these with the existing `TEX_UPLOAD`, `SURF_DOWNLOAD`, and
`SURF_TO_TEX` counters. Surface counters also include work outside texture
binding. These counts are operations, not bytes or elapsed times.

A high dirty-query count with few hits suggests redundant checking. Many hash
passes with unchanged content suggest overly broad invalidations. Many surface
sync checks with few downloads suggest redundant overlap queries. Confirm the
actual CPU cost before changing these paths; none of these ratios alone proves
that a transfer or invalidation can safely be removed.
