#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline per-workload comparison; never a gameplay estimate."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import zlib

SUITE = "xbox-benchmark-pilot-1"
TRIALS = 7
FIELDS = (
    "seq,case,trial,kind,iterations,words,width,height,"
    "tsc_start,tsc_end,tsc,ticks_ms,checksum,ok"
).split(",")


def cases():
    result = [(9, 0, 0, 0, 0)]
    for kind in range(9):
        if kind in (3, 4, 5):
            result.extend(
                (kind, 33554432, w, 0, 0) for w in (1024, 4096, 32768, 1048576)
            )
        else:
            counts = (2097152, 8388608) if kind == 6 else (8388608, 33554432)
            result.extend((kind, n, 1024, 0, 0) for n in counts)
    result.extend(
        (10, n, 0, w, h)
        for w, h in ((16, 16), (128, 128), (640, 480))
        for n in (64, 1024, 16384)
    )
    return result


def lcg(n):
    value, a, b = 1, 1664525, 1013904223
    while n:
        if n & 1:
            value = (a * value + b) & 0xFFFFFFFF
        b = (a * b + b) & 0xFFFFFFFF
        a = (a * a) & 0xFFFFFFFF
        n >>= 1
    return value


def expected(case):
    kind, n, w, _, _ = case
    if kind == 0:
        return lcg(n)
    if kind == 1:
        return (
            (1 + 3 * n) ^ (3 + 5 * n) ^ (5 + 7 * n) ^ (7 + 11 * n)
        ) & 0xFFFFFFFF
    if kind == 2:
        # These counts are exact multiples of the low-17-bit LCG period.
        assert n % 131072 == 0
        return (lcg(n) ^ (3 + 5 * n)) & 0xFFFFFFFF
    if kind == 3:
        return (1 + (n // w) * w * (w - 1) // 2) & 0xFFFFFFFF
    if kind == 4:
        return (17 * n) & (w - 1)
    if kind == 5:
        # XOR of an aligned power-of-two block of >=4 consecutive integers.
        return 1
    return {6: 16 * n, 7: 2 * n, 8: n, 9: 1, 10: n}[kind]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_run(path, manifest, role):
    lines = Path(path).read_text(encoding="ascii").splitlines()
    require(len(lines) > 2 and lines[-1] == "# complete", "Incomplete run")
    header = lines[0].removeprefix("# ").split(",")
    require(len(header) == 6, "Invalid run header")
    suite, session, mode, crc, size, freq = header
    require(suite == SUITE and manifest["suite"] == SUITE, "Suite mismatch")
    require(mode == role, "Wrong hardware/emulator role")
    require(len(session) == 16 and int(session, 16) > 0, "Invalid session")
    require(
        crc == manifest["xbe_crc32"] and int(size) == manifest["xbe_bytes"],
        "Executable does not match package manifest",
    )
    frequency = int(freq)
    require(
        725000000 < frequency < 742000000, "Non-stock/unknown TSC frequency"
    )
    reader = csv.DictReader(lines[1:-1])
    require(reader.fieldnames == FIELDS, "Schema mismatch")
    rows = []
    seen = set()
    spec = cases()
    for index, raw in enumerate(reader, 1):
        r = {key: int(value) for key, value in raw.items()}
        require(all(-1 <= x < 2**64 for x in r.values()), "Out-of-range field")
        require(r["seq"] == index, "Missing, reordered or duplicate sequence")
        cid, trial = r["case"], r["trial"]
        require(
            0 <= cid < len(spec) and -1 <= trial < TRIALS, "Unknown case/trial"
        )
        require((cid, trial) not in seen, "Duplicate case/trial")
        seen.add((cid, trial))
        require(
            tuple(
                r[k]
                for k in ("kind", "iterations", "words", "width", "height")
            )
            == spec[cid],
            "Workload mismatch",
        )
        require(
            r["ok"] == 1 and r["checksum"] == expected(spec[cid]),
            "Work/output verification failed",
        )
        require(
            r["tsc_end"] > r["tsc_start"]
            and r["tsc_end"] - r["tsc_start"] == r["tsc"],
            "Invalid TSC interval",
        )
        require(0 <= r["ticks_ms"] < 2**32, "Invalid tick interval")
        r["seconds"] = r["tsc"] / frequency
        rows.append(r)
    require(len(seen) == len(spec) * (TRIALS + 1), "Missing cases/repetitions")
    return session, rows


def read_host(path, session, count):
    result = {}
    for row in csv.reader(Path(path).read_text(encoding="ascii").splitlines()):
        require(
            len(row) == 3 and row[0] == session, "Wrong host session/schema"
        )
        seq, ns = map(int, row[1:])
        require(
            seq not in result and 0 < seq <= count and 0 < ns < 2**63,
            "Invalid/paused/reset/duplicate host interval",
        )
        result[seq] = ns / 1e9
    require(len(result) == count, "Missing host timestamps")
    return result


def summarize(hardware, emulator, host):
    def durations(rows, cid, host_times=None):
        return [
            host_times[r["seq"]] if host_times is not None else r["seconds"]
            for r in rows
            if r["case"] == cid and r["trial"] >= 0
        ]

    empty_hw = max(durations(hardware, 0))
    empty_em = max(durations(emulator, 0, host))
    output = []
    for cid, spec in enumerate(cases()[1:], 1):
        hw = durations(hardware, cid)
        em = durations(emulator, cid, host)
        h, e = statistics.median(hw), statistics.median(em)
        reasons = []
        if min(hw) < max(0.01, 100 * empty_hw) or min(em) < max(
            0.01, 100 * empty_em
        ):
            reasons.append("too short or boundary overhead above 1%")
        if (max(hw) - min(hw)) / h > 0.10 or (max(em) - min(em)) / e > 0.10:
            reasons.append(
                "observed spread exceeds 10%; rerun emulator / review hardware"
            )
        hardware_rows = [
            r for r in hardware if r["case"] == cid and r["trial"] >= 0
        ]
        ticks = sum(r["ticks_ms"] for r in hardware_rows) / 1000
        # Coarse ticks have ~1 ms endpoint quantization.
        if abs(ticks - sum(hw)) > max(0.05 * sum(hw), 0.002 * TRIALS):
            reasons.append("hardware TSC/tick clocks disagree")
        output.append(
            {
                "case": cid,
                "workload": spec,
                "scope": (
                    "CPU benchmark"
                    if spec[0] < 9
                    else "NV2A clear + submission + completion"
                ),
                "hardware_median_ms": h * 1000,
                "xemu_host_median_ms": e * 1000,
                "speed_percent": None if reasons else 100 * h / e,
                "observed_ratio_range": (
                    None
                    if reasons
                    else [100 * min(hw) / max(em), 100 * max(hw) / min(em)]
                ),
                "unavailable_reasons": reasons,
            }
        )
    return {
        "suite": SUITE,
        "gameplay_speed": None,
        "note": (
            "Per-workload comparisons only; ranges are observed extrema, "
            "not confidence intervals. No aggregate score."
        ),
        "timer_control_max_ms": [empty_hw * 1000, empty_em * 1000],
        "cases": output,
    }


def validate_metadata(hardware, emulator):
    require(
        hardware.get("platform") == "original-xbox"
        and hardware.get("stock_clocks") is True
        and hardware.get("ram_mb") == 64,
        "Hardware must declare a stock-clock 64 MB original Xbox",
    )
    for key in ("revision", "bios", "video_region", "collector_alias"):
        require(
            isinstance(hardware.get(key), str)
            and bool(hardware[key].strip())
            and hardware[key] != "UNKNOWN",
            f"Missing hardware provenance: {key}",
        )
    require(
        emulator.get("render_scale") == 1
        and emulator.get("icount") is False
        and emulator.get("cold_boot") is True,
        "Unsupported emulator configuration",
    )
    for key in (
        "commit",
        "renderer",
        "host_cpu",
        "host_gpu",
        "os",
        "settings",
    ):
        require(
            isinstance(emulator.get(key), str)
            and bool(emulator[key].strip())
            and emulator[key] != "UNKNOWN",
            f"Missing emulator provenance: {key}",
        )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("hardware")
    p.add_argument("emulator")
    p.add_argument("host_timestamps")
    p.add_argument(
        "--package",
        required=True,
        type=Path,
        help="Directory containing manifest.json and default.xbe",
    )
    p.add_argument("--hardware-info", required=True)
    p.add_argument("--xemu-info", required=True)
    args = p.parse_args()
    try:
        manifest = json.loads((args.package / "manifest.json").read_text())
        binary = (args.package / "default.xbe").read_bytes()
        require(
            hashlib.sha256(binary).hexdigest() == manifest["xbe_sha256"]
            and f"{zlib.crc32(binary):08x}" == manifest["xbe_crc32"]
            and len(binary) == manifest["xbe_bytes"],
            "Package hash mismatch",
        )
        hw_info = json.loads(Path(args.hardware_info).read_text())
        em_info = json.loads(Path(args.xemu_info).read_text())
        validate_metadata(hw_info, em_info)
        _, hw = read_run(args.hardware, manifest, "hardware-unverified")
        session, em = read_run(args.emulator, manifest, "xemu")
        host = read_host(args.host_timestamps, session, len(em))
        result = summarize(hw, em, host)
        result.update(
            {
                "hardware_provenance": hw_info,
                "xemu_provenance": em_info,
                "xbe_sha256": manifest["xbe_sha256"],
                "inputs_sha256": {
                    str(path): hashlib.sha256(
                        Path(path).read_bytes()
                    ).hexdigest()
                    for path in (
                        args.hardware,
                        args.emulator,
                        args.host_timestamps,
                    )
                },
            }
        )
        print(json.dumps(result, indent=2, allow_nan=False))
    except (ValueError, KeyError, TypeError, OSError) as exc:
        p.exit(1, f"No comparison produced: {exc}\n")


if __name__ == "__main__":
    main()
