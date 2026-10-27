<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Xbox benchmark pilot 2

This is an **experimental collection kit**, not a calibrated CPU/GPU speed meter.
Use a small pilot group first. A successful build is not proof that the XBE works
on every original Xbox revision. Keep failed/partial runs too.

The current revision implements the missing NV2A class-0x39 transfer path in
xemu and adds probe/timeout/storage diagnostics to the XBE. See [M2MF.md](M2MF.md)
for the cause, upstream attribution, supported behavior and hardware-pilot limits.
An older xemu build still cannot execute native M2MF, even with the new ISO.

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
5. Condividi i due CSV e il JSON di validazione (stesso nome di sessione), più `VOLUNTEER-NOTES.txt` compilato sulla PR #8. In caso di
   blocco/FAILED, conserva tutti i file parziali e indica l'ultima riga visibile.
   Non condividere EEPROM, chiavi, seriali, BIOS o immagini del disco.

**COMPLETE indica che la raccolta è terminata, non che tutti i test siano passati.**
Se non termina entro 15 minuti, annota l’ultima schermata e conserva i file
parziali; non è necessario ripetere continuamente i test.

Vengono scritti soltanto risultati in `E:\\XBench`; file con lo stesso nome non
vengono sovrascritti. Il benchmark non aggiorna firmware e non cambia clock.

## Volunteers (English)

Use an original Xbox already able to run homebrew, preferably stock clocks and
64 MB RAM. Copy `default.xbe` to a new application folder and launch it from your
dashboard. Do **not** add `XEMU.TXT` on real hardware. Leave it running until
COMPLETE; allow a provisional 15-minute slot (hardware duration is not yet known).
COMPLETE means collection finished, not that all checks passed.
Retrieve both CSV files and the validation JSON (preserve their matching names) from `E:\\XBench` using your usual dashboard/FTP workflow.
Submit them with the filled-in `VOLUNTEER-NOTES.txt` to
[PR #8](https://github.com/AITUS95/xemu-MSVC/pull/8). No network service or telemetry
is started by the application. Do not upload EEPROM/keys/serials/BIOS/disk images.
If it stops or fails, retain the partial file and report the last screen message.

## What a session captures

The original base section has 34 cases, each with one full warm-up and seven
measured repetitions (272 rows). The extension adds 29 cases (232 rows) and
30 validation families, described below.
Each base round uses a fixed shuffled order to spread thermal/order effects. Work
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
`xbox-benchmark-xemu.iso` when present. Retrieve both guest CSVs and the validation JSON from the test HDD.
Do not compare emulator guest TSC values to Xbox TSC as a speed measurement.

## Offline comparison

Python 3.10+ only, no additional packages. Fill in the two metadata templates.
Stock-clock 64 MB consoles are the initial accepted baseline population; archive
modified-console submissions separately rather than blending their results.

```
python3 compare_campaign.py xbox.csv xemu.csv host.csv --package . \
  --hardware-info hardware-info.json --xemu-info xemu-info.json > comparison.json
```

The tool verifies the local XBE against the manifest, identical workload and
executable identifiers, all 504 intervals, seven distinct measured trials plus a warm-up per case, independent
CPU expected checksums, successful framebuffer checks, matching host session and
complete positive timestamps. Keep `<session>.csv`, `<session>.extended.csv` and
`<session>.validation.json` together without editing/renaming them. Raw upstream
failures/skips are never suppressed; any failed M2MF validation blocks all M2MF
ratios conservatively. CPU replay failures block the corresponding replay ratio.
The original independent kernels retain their own output checks. It reports **100 × Xbox median time / xemu host
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
texture/shader/vertex/fill combinations, readbacks and CPU/GPU contention.
Use held-out workloads and original-Xbox game traces for error validation; do
not train and validate on the same cases. A finite one-session kit cannot supply
all future measurements. This first package establishes a reusable collection
and rejection protocol; it must not be advertised as a complete calibration.

## Reuse of existing nxdk tests

Pilot 2 adds **11 CPU validation replays** and **18 M2MF transfer workloads**
to the original 34 cases: **63 cases, 504 timed intervals** in one automatic run.
The original **30 validation families** execute once with detailed JSON output.
Warm-ups and failed results are retained, not filtered out.

* CPU source: [nxdk_low_level_cpu_tests at 702fe1b](https://github.com/abaire/nxdk_low_level_cpu_tests/tree/702fe1b6a61692104bdbb153034c266e2769c347),
  the revision selected by the inspected Roswell CI release: 2 FPU, 5 MMX,
  4 SSE families. Each timed sample replays a whole test 1,024 times (16,384 for basic FPU arithmetic) with
  output disabled but checks and logging-argument construction intact. These
  numbers describe **validation replay cost**, not isolated instruction latency
  or CPU IPC. Original verbose `duration_ms` is diagnostic only.
* GPU source: [nxdk_low_level_nv2a_tests at d80bff6](https://github.com/abaire/nxdk_low_level_nv2a_tests/tree/d80bff6d99cadda227739ee07f826c0803b6a4da),
  the inspected CI M2MF revision. All 19 families retain original assertions:
  alignment/odd sizes/phases, batching/causal chains, formats, framebuffer,
  pitch/packing, line counts, minimum/zero transfers, notifier records/switching,
  and system/video-memory transfers. Adversarial cases execute last because
  they can rebind channel/notifier state. No Roswell FailureFilter is applied.
* [pbkitplusplus at c3aa587](https://github.com/abaire/pbkitplusplus/tree/c3aa5879f1dd96743bf81128468780d8b59d42a7)
  supplies the existing M2MF driver, submission helpers and notifier completion.
  Timed workloads cross 4 KiB/64 KiB/1 MiB with linear/pitched/stride-2 layouts
  and physical source/destination phases 0/8192 modulo 16 KiB. Each sample
  copies 64 MiB total (16,384 / 1,024 / 64 completed transfers respectively). Original pattern/guard helpers validate source,
  destination, padding and guards outside the interval. WB cache flushes also
  stay outside it. Driver work, CPU submission, polling and memory contention
  are included: this is **not pure NV2A shader throughput**.
* The driver can silently fall back to CPU `memcpy`. A read-only accessor in the
  generated driver exposes its actual native/fallback state; fallback results
  are marked unavailable, never presented as GPU speed. A completion timeout
  stops collection without freeing buffers that DMA might still own.
* [nxdk](https://github.com/XboxDev/nxdk/tree/03acd1415f7ccc3127bcfb629224aacdd95d783c)
  provides the open-source compiler support, pbkit and FATX APIs. Hardware TSC
  is cross-checked with coarse ticks; xemu speed uses the separate host timer.

`prepare_upstream.py` pins all sources, including the original printf library.
It generates a catalog from the original runners and an adapted-source hash
inventory, packaged with sources/licenses. Small build-only adapters unify the
logger, add quiet replay/failure counting, expose native M2MF state, replace the
fork's private framebuffer globals with public `pb_back_buffer()`, and extract
only the existing M2MF declarations/helper from the large NV2A state header.
The source checkouts and emulator algorithms are not modified.

Other useful projects have different roles. The CPU project's Berkeley
TestFloat/SoftFloat tests remain correctness oracles; timing the oracle together
with the emulated instruction would not isolate CPU work. They are **not included**
in this collection. [nxdk_pgraph_tests](https://github.com/abaire/nxdk_pgraph_tests)
provides extensive rendering/golden-image cases, but needs separate image
validation and carefully defined submission/completion intervals before reuse
as timing workloads. Its rendering/texture/shader coverage is **not claimed** by
this M2MF extension. PTIMER clock-manipulation tests are likewise not speed
references. One run collects a reusable matrix, not every future GPU behavior.

## Building and tests

See `build.sh`. nxdk and upstream test sources are pinned; its submodules are fetched at the exact recorded
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
