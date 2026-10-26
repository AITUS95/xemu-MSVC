<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Xbox benchmark pilot 1

This is an **experimental collection kit**, not a calibrated CPU/GPU speed meter.
Use a small pilot group first. A successful build is not proof that the XBE works
on every original Xbox revision. Keep failed/partial runs too.

## Volontari: istruzioni semplici

1. Serve una **Xbox originale capace di avviare homebrew**, preferibilmente con
   CPU/GPU e RAM originali (64 MB). Non occorre modificare BIOS o impostazioni.
2. Estrai il pacchetto e copia `default.xbe` in una cartella nuova della console,
   per esempio `E:\\Apps\\XBench`. **Non creare `XEMU.TXT` sulla console reale.**
3. Avvia `default.xbe` dal dashboard. La sessione parte automaticamente: lascia
   lavorare la console fino a **COMPLETE**, senza riavviarla. La durata effettiva
   deve ancora essere caratterizzata su hardware; riserva circa 15 minuti.
4. Torna al dashboard e recupera tutti i file in `E:\\XBench` tramite il normale
   metodo di trasferimento file/FTP della tua console. Il test non invia dati.
5. Condividi il CSV e `VOLUNTEER-NOTES.txt` compilato sulla PR #8. In caso di
   blocco/FAILED, conserva il CSV parziale e indica l'ultima riga visibile.
   Non condividere EEPROM, chiavi, seriali, BIOS o immagini del disco.

Vengono scritti soltanto risultati in `E:\\XBench`; file con lo stesso nome non
vengono sovrascritti. Il benchmark non aggiorna firmware e non cambia clock.

## Volunteers (English)

Use an original Xbox already able to run homebrew, preferably stock clocks and
64 MB RAM. Copy `default.xbe` to a new application folder and launch it from your
dashboard. Do **not** add `XEMU.TXT` on real hardware. Leave it running until
COMPLETE; allow a provisional 15-minute slot (hardware duration is not yet known).
Retrieve the CSV files from `E:\\XBench` using your usual dashboard/FTP workflow.
Submit them with the filled-in `VOLUNTEER-NOTES.txt` to
[PR #8](https://github.com/AITUS95/xemu-MSVC/pull/8). No network service or telemetry
is started by the application. Do not upload EEPROM/keys/serials/BIOS/disk images.
If it stops or fails, retain the partial file and report the last screen message.

## What a session captures

34 cases, each with one full warm-up and seven measured repetitions (272 rows).
Each round uses a fixed shuffled order to spread thermal/order effects. Work
counts are fixed across machines, not adapted to whichever machine runs first.
All raw rows, including warm-ups and outliers, are retained; each row is flushed
outside the measurement so interruption leaves useful evidence.

* 24 CPU cases: dependent integer multiply/add, independent integer chains,
  data-dependent branches, sequential loads/stores, dependent fixed-stride
  pointer traversal, SSE packed addition, MMX packed addition, x87 addition.
  Register workloads use two work counts; memory workloads span 4 KiB, 16 KiB,
  128 KiB and 4 MiB. Explicit assembly/volatile memory prevents dead-code removal.
  These are warm workloads, **not** a complete cache/branch/pipeline model.
* 9 NV2A cases: 16x16, 128x128 and 640x480 color clear regions, with
  64/1024/16384 changing-color commands. 640x480, 32-bit, 60 Hz video setup.
  The measured interval includes CPU submission and `WAIT_FOR_IDLE` + `pb_busy`
  completion; it does not wait for vblank or swap a visible frame. Outside the
  timer, every framebuffer pixel is checked through pbkit's linear AGP alias.
  This is **end-to-end clear/submission/completion latency**, not isolated GPU
  execution time. In xemu, WAIT_FOR_IDLE also downloads surfaces; that cost is
  deliberately included. It cannot represent shaders, textures, draws or M2MF.
* One empty-work timer/control case, repeated with the same boundaries.
* Raw serialized TSC endpoints/delta, nominal TSC frequency from nxdk, coarse
  tick delta, workload parameters, checksum/status, session ID, executable size
  and CRC32. The package manifest also records the executable's SHA-256.

Result headers deliberately say `hardware-unverified`: absence of the emulator
flag is not proof of real hardware. Console provenance must be checked separately.
CRC32 is a convenient accidental-mismatch check, not security/remote attestation;
keep the package SHA-256 and the original binary alongside every submission.

## Run the identical XBE in xemu

Use the updated PR build, a separate test HDD/EEPROM copy, cold boot, normal clocks,
1x render scale, and record renderer, host specifications, exact xemu commit and
settings. Do not use snapshots, pause, icount, fast-forward or artificial pacing
changes. Keep the OSD off for the reference run; repeat on/off separately to
measure overhead. Host background load affects elapsed time and must be reported.

Place an empty `XEMU.TXT` beside **the same `default.xbe`** on the guest. Launch
xemu with these additional command-line options:

```
-chardev file,id=bench,path=host.csv -device xbox-benchmark,chardev=bench
```

The device exists only when explicitly requested. The XBE refuses emulator mode
if the device's protocol signature is absent. Do not enable this mode on a real
console. Port 0x500 is a **synthetic lab interface**, not an Xbox register.

`host.csv` records the session ID, sequence and elapsed **monotonic host ns**.
Only two markers per interval enter the device; no instruction/TCG/PFIFO hot path
is instrumented. BEGIN performs no output; END writes after taking the timestamp.
Pause/resume, reset, nested/mismatched markers and nonpositive time invalidate an
interval. This device is intentionally unmigratable. The host interval includes
a small boundary cost around guest timestamp reads; the empty test exposes it.
Neither clock is overhead-subtracted or silently corrected.

The included `xbox-benchmark.iso` is for hardware mode. To make an emulator ISO,
put the same XBE plus an empty `XEMU.TXT` in a temporary folder and run nxdk's
`extract-xiso -c folder emulator-benchmark.iso`, or use the provided
`xbox-benchmark-xemu.iso` when present. Retrieve the guest CSV from the test HDD.
Do not compare emulator guest TSC values to Xbox TSC as a speed measurement.

## Offline comparison

Python 3.10+ only, no additional packages. Fill in the two metadata templates.
Stock-clock 64 MB consoles are the initial accepted baseline population; archive
modified-console submissions separately rather than blending their results.

```
python3 compare.py xbox.csv xemu.csv host.csv --package . \
  --hardware-info hardware-info.json --xemu-info xemu-info.json > comparison.json
```

The tool verifies the local XBE against the manifest, identical workload and
executable identifiers, all 272 rows, seven distinct trials/case, independent
CPU expected checksums, successful framebuffer checks, matching host session and
complete positive timestamps. It reports **100 × Xbox median time / xemu host
median time per case** only. No global or per-component average is produced.

Cases shorter than 10 ms, smaller than 100 times the maximum empty-boundary cost,
with >10% observed timing spread, or inconsistent Xbox TSC/coarse tick timing
remain unavailable. All raw evidence stays available. Reported ratio ranges use
sample minima/maxima and are **not confidence intervals**. A single console is
one observation, not a population baseline. Do not discard slow consoles or
failed cases to improve the score. Compare multiple stock consoles/revisions,
retain per-console medians, and investigate systematic differences before
publishing a reference. Provenance and clock declarations are not attestation.

## Why gameplay OSD still says N/D

This kit measures the same bounded work on both machines. A game does not run
these fixed workloads: microbenchmarks cannot tell us how long its current CPU
instruction stream or NV2A command stream would take on a real Xbox. Applying a
constant benchmark multiplier to host utilization, FPS, PFIFO words or virtual
clocks would still be misleading. The OSD remains unchanged and uncalibrated.

For a future *estimated* workload model, collect broader independent suites:
integer divide/latency/throughput, mixed scalar/SIMD, cache conflict/miss patterns,
texture/shader/vertex/fill combinations, M2MF, readbacks and CPU/GPU contention.
Use held-out workloads and original-Xbox game traces for error validation; do
not train and validate on the same cases. A finite one-session kit cannot supply
all future measurements. This first package establishes a reusable collection
and rejection protocol; it must not be advertised as a complete calibration.

## Existing tools and technical references

The supplied research report remains the architectural reference. Implementation
was checked against primary source, without repeating the general research:

* [nxdk, pinned revision](https://github.com/XboxDev/nxdk/tree/03acd1415f7ccc3127bcfb629224aacdd95d783c):
  open-source XBE toolchain, pbkit, FATX file APIs. `lib/winapi/profiling.c` uses
  RDTSC for QueryPerformanceCounter, hence the separate host stopwatch.
* [pbkit completion and submission](https://github.com/XboxDev/nxdk/blob/03acd1415f7ccc3127bcfb629224aacdd95d783c/lib/pbkit/pbkit.c):
  `pb_busy` checks DMA GET against PUT and PGRAPH status. `pb_agp_access` supplies
  the linear framebuffer view. No guessed register offsets are introduced.
* Local `hw/xbox/nv2a/pgraph/pgraph.c`: WAIT_FOR_IDLE invokes surface download;
  it is not merely a PFIFO submission counter.
* [Roswell/xemu-test](https://github.com/xemu-project/xemu-test) and its CPU/M2MF
  suites remain the functional regression tools; durations/suppressed failures
  are not automatically performance references.
* [nxdk_pgraph_tests](https://github.com/abaire/nxdk_pgraph_tests): reuse its
  functional golden-image coverage when expanding GPU benchmarks. It does not
  supply timing baselines for this new executable.

## Building and tests

See `build.sh`. nxdk is pinned; its submodules are fetched at the exact recorded
revisions. Build in an isolated nxdk checkout. Required tools: clang/LLVM/lld,
make, cmake, flex, bison, Python 3. No proprietary SDK or console assets are shipped.
Changing source/compiler/flags changes the executable identity; never silently
merge those runs with an older package. Archive the package and manifest forever.
The Actions artifact is a pilot distribution, with limited retention: download
and archive it before collecting hardware results.

```
python3 -m unittest discover -s tools/xbox-benchmark -v
python3 tools/xbox-benchmark/verify_kernels.py
cc -std=c11 -Wall -Wextra -Werror -pedantic -I. \
  tests/unit/test-xemu-benchmark.c -o /tmp/test-xemu-benchmark
/tmp/test-xemu-benchmark
```

Native kernel verification is not an Xbox execution test. Real hardware boot,
all video encoders/revisions, thermal behavior and GPU correctness still need a
pilot. Hardware timing baselines do not exist yet. Preserve the draft PR.
