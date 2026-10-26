#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare a complete pilot-2 campaign, retaining unsuppressed validation."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import zlib

import compare
from compare import require

FIELDS = (
    "seq,case,trial,loops,bytes,layout,phase,tsc_start,tsc_end,tsc,"
    "ticks_ms,ok,failures,native"
).split(",")


def specs(catalog):
    result = {
        c["id"]: (
            16384 if c["id"] == "cpu/TestFpuBasicArithmetic" else 1024,
            0,
            0,
            0,
        )
        for c in catalog
        if c["cpu"]
    }
    result.update(
        {
            f"m2mf/{size}/{layout}/{phase}": (
                67108864 // size,
                size,
                layout,
                phase,
            )
            for size in (4096, 65536, 1048576)
            for layout in range(3)
            for phase in (0, 8192)
        }
    )
    require(len(catalog) == 30 and len(result) == 29, "Invalid catalog")
    return result


def events_failed(events):
    for event in events:
        if event.get("level") in ("fail", "error") or "[SKIP]" in event.get(
            "message", ""
        ):
            return True
        if events_failed(event.get("events", [])):
            return True
    return False


def read_validation(path, header, catalog):
    data = json.loads(Path(path).read_text())
    require(
        data["schema_version"] == 1
        and data["suite"] == "xbox-benchmark-pilot-2-validation|" + header,
        "Validation identity mismatch",
    )
    require(
        [t["name"] for t in data["tests"]] == [c["id"] for c in catalog],
        "Missing/reordered validation cases",
    )
    results = [t["result"] for t in data["tests"]]
    require(
        all(r in ("pass", "fail") for r in results),
        "Unknown validation result",
    )
    require(
        data["summary"]
        == {
            "total": 30,
            "passed": results.count("pass"),
            "failed": results.count("fail"),
        },
        "Validation summary mismatch",
    )
    # Preserve all events; never apply Roswell's failure suppressions.
    data["usable"] = {
        t["name"]: t["result"] == "pass" and not events_failed(t["events"])
        for t in data["tests"]
    }
    if events_failed(data["events"]):
        data["usable"] = dict.fromkeys(data["usable"], False)
    return data


def read_campaign(path, manifest, role):
    path = Path(path)
    session, base = compare.read_run(path, manifest, role)
    header = path.read_text(encoding="ascii").splitlines()[0]
    catalog = manifest["upstream_catalog"]
    expected = specs(catalog)
    lines = (
        path.with_suffix(".extended.csv")
        .read_text(encoding="ascii")
        .splitlines()
    )
    require(
        len(lines) > 2 and lines[0] == header and lines[-1] == "# complete",
        "Incomplete extension or mismatched identity",
    )
    frequency = int(header.rsplit(",", 1)[1])
    reader = csv.DictReader(lines[1:-1])
    require(reader.fieldnames == FIELDS, "Extension schema mismatch")
    rows, seen = [], set()
    for seq, raw in enumerate(reader, 273):
        r = {k: v if k == "case" else int(v) for k, v in raw.items()}
        require(
            all(
                0 <= v < 2**64
                for k, v in r.items()
                if k not in ("case", "trial")
            ),
            "Out-of-range field",
        )
        require(r["seq"] == seq, "Extension sequence mismatch")
        require(
            r["case"] in expected and -1 <= r["trial"] < 7,
            "Unknown case/trial",
        )
        pair = (r["case"], r["trial"])
        require(pair not in seen, "Duplicate case/trial")
        seen.add(pair)
        require(
            tuple(r[k] for k in ("loops", "bytes", "layout", "phase"))
            == expected[r["case"]],
            "Workload mismatch",
        )
        require(r["ok"] in (0, 1) and r["native"] in (0, 1), "Invalid flags")
        require(
            r["tsc_end"] > r["tsc_start"]
            and r["tsc_end"] - r["tsc_start"] == r["tsc"],
            "Invalid TSC interval",
        )
        require(r["ticks_ms"] < 2**32, "Invalid tick interval")
        r["seconds"] = r["tsc"] / frequency
        rows.append(r)
    require(len(seen) == len(expected) * 8, "Missing extension intervals")
    validation = read_validation(
        path.with_suffix(".validation.json"), header, catalog
    )
    return session, base, rows, validation


def summarize(hw, em, host, hw_validation, em_validation, timer_max):
    result = []
    for cid in dict.fromkeys(r["case"] for r in hw):
        hrows = [r for r in hw if r["case"] == cid]
        erows = [r for r in em if r["case"] == cid]
        h = [r["seconds"] for r in hrows if r["trial"] >= 0]
        e = [host[r["seq"]] for r in erows if r["trial"] >= 0]
        reasons = []
        if any(
            not r["ok"] or r["failures"] or not r["native"]
            for r in hrows + erows
        ):
            reasons.append(
                "failed verification, skipped work or CPU fallback "
                "(including warm-up)"
            )
        for validation in (hw_validation, em_validation):
            relevant = [
                v
                for name, v in validation["usable"].items()
                if name == cid
                or (cid.startswith("m2mf/") and name.startswith("nv2a/"))
            ]
            if not relevant or not all(relevant):
                reasons.append("raw upstream validation failed/skipped")
        if min(h) < max(0.01, timer_max[0] * 100) or min(e) < max(
            0.01, timer_max[1] * 100
        ):
            reasons.append("too short or boundary overhead above 1%")
        if (max(h) - min(h)) / statistics.median(h) > 0.1 or (
            max(e) - min(e)
        ) / statistics.median(e) > 0.1:
            reasons.append("observed spread exceeds 10%")
        ticks = sum(r["ticks_ms"] for r in hrows if r["trial"] >= 0) / 1000
        if abs(ticks - sum(h)) > max(0.05 * sum(h), 0.002 * 7):
            reasons.append("hardware TSC/tick clocks disagree")
        result.append(
            {
                "case": cid,
                "scope": (
                    "CPU validation replay including checks"
                    if cid.startswith("cpu/")
                    else "M2MF driver + submission + notifier completion"
                ),
                "hardware_median_ms": 1000 * statistics.median(h),
                "xemu_host_median_ms": 1000 * statistics.median(e),
                "speed_percent": (
                    None
                    if reasons
                    else 100 * statistics.median(h) / statistics.median(e)
                ),
                "observed_ratio_range": (
                    None
                    if reasons
                    else [100 * min(h) / max(e), 100 * max(h) / min(e)]
                ),
                "unavailable_reasons": sorted(set(reasons)),
            }
        )
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("hardware", type=Path)
    p.add_argument("emulator", type=Path)
    p.add_argument("host_timestamps", type=Path)
    p.add_argument("--package", type=Path, required=True)
    p.add_argument("--hardware-info", type=Path, required=True)
    p.add_argument("--xemu-info", type=Path, required=True)
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
        hw_info = json.loads(args.hardware_info.read_text())
        em_info = json.loads(args.xemu_info.read_text())
        compare.validate_metadata(hw_info, em_info)
        _, hw, hx, hv = read_campaign(
            args.hardware, manifest, "hardware-unverified"
        )
        session, em, ex, ev = read_campaign(args.emulator, manifest, "xemu")
        host = compare.read_host(
            args.host_timestamps, session, len(em) + len(ex)
        )
        result = compare.summarize(hw, em, host)
        result["extended_cases"] = summarize(
            hx,
            ex,
            host,
            hv,
            ev,
            [x / 1000 for x in result["timer_control_max_ms"]],
        )
        inputs = [args.host_timestamps, args.hardware_info, args.xemu_info]
        for base in (args.hardware, args.emulator):
            inputs.extend(
                [
                    base,
                    base.with_suffix(".extended.csv"),
                    base.with_suffix(".validation.json"),
                ]
            )
        result.update(
            hardware_provenance=hw_info,
            xemu_provenance=em_info,
            xbe_sha256=manifest["xbe_sha256"],
            upstream_sources=manifest["upstream_sources"],
            validation={"hardware": hv, "xemu": ev},
            inputs_sha256={
                str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in inputs
            },
        )
        print(json.dumps(result, indent=2, allow_nan=False))
    except (ValueError, KeyError, TypeError, OSError, IndexError) as exc:
        p.exit(1, f"No comparison produced: {exc}\n")


if __name__ == "__main__":
    main()
