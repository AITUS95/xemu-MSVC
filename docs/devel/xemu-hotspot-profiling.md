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
