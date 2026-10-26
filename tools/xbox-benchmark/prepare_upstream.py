#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build-only adapters for immutable hardware validation sources."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
SOURCES = {
    "cpu": (
        "https://github.com/abaire/nxdk_low_level_cpu_tests.git",
        "702fe1b6a61692104bdbb153034c266e2769c347",
    ),
    "nv2a": (
        "https://github.com/abaire/nxdk_low_level_nv2a_tests.git",
        "d80bff6d99cadda227739ee07f826c0803b6a4da",
    ),
    "pbkitplusplus": (
        "https://github.com/abaire/pbkitplusplus.git",
        "c3aa5879f1dd96743bf81128468780d8b59d42a7",
    ),
    "printf": (
        "https://github.com/mpaland/printf.git",
        "d3b984684bb8a8bdc48cc7a1abecb93ce59bbe3e",
    ),
}


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Adapter anchor changed: {old}")
    return text.replace(old, new)


def prepare():
    cache = Path(os.environ.get("XBENCH_CACHE", ROOT / ".upstream"))
    generated = ROOT / "generated"
    if generated.exists():
        shutil.rmtree(generated)
    generated.mkdir()
    for name, (url, revision) in SOURCES.items():
        checkout = cache / name
        if not checkout.exists():
            subprocess.run(
                ["git", "clone", "--no-checkout", url, str(checkout)],
                check=True,
            )
        subprocess.run(
            ["git", "-C", str(checkout), "checkout", "--detach", revision],
            check=True,
        )
        # Reject local modifications to sources used for the build.
        if subprocess.check_output(
            [
                "git",
                "-C",
                str(checkout),
                "status",
                "--porcelain",
                "--untracked-files=no",
            ]
        ):
            raise ValueError(f"Modified upstream checkout: {checkout}")
        target = generated / name
        shutil.copytree(
            checkout / "src" if name != "printf" else checkout,
            target,
            ignore=shutil.ignore_patterns(".git"),
        )
        for license in checkout.glob("LICENSE*"):
            if license.is_file():
                shutil.copy2(license, target / license.name)
    cpu, nv, pb = (generated / n for n in ("cpu", "nv2a", "pbkitplusplus"))
    shutil.copy2(cpu / "test_log.h", nv / "test_log.h")
    (cpu / "configure.h").write_text(
        "/* Build identity is recorded in the campaign header. */\n"
    )
    log = (cpu / "test_log.cpp").read_text()
    log = replace_once(
        log,
        "namespace {",
        "namespace {\nstatic bool bench_quiet;\nstatic unsigned bench_failures;",
    )
    for anchor in (
        "void Event(Level level, const char* file, int line, const LogFields& fields,\n           const char* fmt, ...) {\n  char buf[kMaxMessageLength];",
        "size_t len) {\n  const auto* p",
        "void BeginScope(const char* file, int line, const char* name) {",
        "void EndScope() {",
        "void Print(const char* fmt, ...) {",
    ):
        if anchor.startswith("void Event"):
            new = anchor.replace(
                "\n  char",
                "\n  if (level == Level::kFail || level == Level::kError) ++bench_failures;\n  if (bench_quiet) return;\n  char",
            )
        elif "const auto*" in anchor:
            new = anchor.replace(
                "\n  const", "\n  if (bench_quiet) return;\n  const"
            )
        else:
            new = anchor + "\n  if (bench_quiet) return;"
        log = replace_once(log, anchor, new)
    log += (
        '\nextern "C" void bench_log_mode(bool quiet) { '
        "bench_quiet = quiet; bench_failures = 0; }\n"
        'extern "C" unsigned bench_log_failures() { return bench_failures; }\n'
    )
    (cpu / "test_log.cpp").write_text(log)
    # Keep the original M2MF declarations/helper without SDL/math code.
    state = (pb / "nv2astate.h").read_text()
    (pb / "nv2astate.original.h").write_text(state)
    constants = re.findall(
        r"^constexpr (?:uint32_t|int32_t) kM2M\w+ = [^\n]+;", state, re.M
    )
    if len(constants) != 5:
        raise ValueError("M2MF declarations changed")
    busy = re.search(
        r"  static void PBKitBusyWait\(\) \{.*?\n  \}", state, re.S
    ).group()
    (pb / "nv2astate.h").write_text(
        "#pragma once\n#include <pbkit/pbkit.h>\n#include <cstdint>\n"
        '#include "third_party/gpu_m2m.h"\nnamespace PBKitPlusPlus {\n'
        + "\n".join(constants)
        + "\nclass NV2AState { public:\n"
        "static int InitializeM2M() { return gpum_init(); }\n"
        + busy
        + "\n};\n}\n"
    )
    driver = pb / "third_party/gpu_m2m.cpp"
    driver.write_text(
        driver.read_text() + '\nextern "C" bool bench_native_m2mf() { '
        "return g_notif && !g_fallback_to_memcpy; }\n"
    )
    fb = nv / "m2mf_tests/test_m2mf_framebuffer.cpp"
    text = (
        fb.read_text()
        .replace("extern DWORD pb_FBAddr[3];\n", "")
        .replace("extern int pb_back_index;\n", "")
    )
    if text.count("pb_FBAddr[pb_back_index]") != 2:
        raise ValueError("Framebuffer adapter changed")
    fb.write_text(text.replace("pb_FBAddr[pb_back_index]", "pb_back_buffer()"))
    catalog, includes, entries = [], [], []
    for project, directory, groups in (
        ("cpu", cpu, ("fpu", "mmx", "sse")),
        ("nv2a", nv, ("m2mf",)),
    ):
        for group in groups:
            runner = (directory / f"{group}_main.cpp").read_text()
            for header in re.findall(r'#include "([^\"]+)"', runner):
                if header.startswith(group + "_tests/"):
                    includes.append(f'#include "{project}/{header}"')
            for cls in re.findall(r"TestCase::From<([^>]+)>", runner):
                cid = f"{project}/{cls}"
                catalog.append({"id": cid, "cpu": project == "cpu"})
                entries.append(
                    f'{{"{cid}", {cls}::Source(), &{cls}::Test, '
                    f'{str(project == "cpu").lower()}}}'
                )
    if len(catalog) != 30 or sum(c["cpu"] for c in catalog) != 11:
        raise ValueError("Upstream catalog changed")
    (generated / "catalog.cpp").write_text(
        '#include "../extended.h"\n'
        + "\n".join(includes)
        + "\nconst UpstreamCase upstream_cases[30] = {\n"
        + ",\n".join(entries)
        + "\n};\n"
    )
    (generated / "catalog.json").write_text(
        json.dumps(catalog, indent=2) + "\n"
    )
    sources = [
        generated / "catalog.cpp",
        cpu / "test_log.cpp",
        generated / "printf/printf.c",
        pb / "pushbuffer.cpp",
        pb / "pbkpp_assert.cpp",
        driver,
    ]
    for directory, groups in ((cpu, ("fpu", "mmx", "sse")), (nv, ("m2mf",))):
        for group in groups:
            sources.extend(
                sorted((directory / f"{group}_tests").glob("*.cpp"))
            )
    make = (
        "NXDK_CXX = y\nSRCS += $(CURDIR)/extended.cpp "
        + " ".join(map(str, sources))
        + "\n"
    )
    make += (
        "CXXFLAGS += -O2 -std=c++17 -fno-strict-aliasing "
        + " ".join(f"-I{p}" for p in (generated, cpu, nv, pb))
        + "\n"
    )
    make += f"CFLAGS += -I{generated}\n"
    for src in sources:
        if (
            src.parent.name in ("mmx_tests", "sse_tests")
            or src.name == "test_fpu_x87_precision_control.cpp"
        ):
            make += (
                f"{src.with_suffix('.obj')}: "
                "CXXFLAGS += -masm=intel -mmmx -msse\n"
            )
    (ROOT / "generated.mk").write_text(make)
    inventory = {
        str(p.relative_to(generated)): hashlib.sha256(
            p.read_bytes()
        ).hexdigest()
        for p in sorted(generated.rglob("*"))
        if p.is_file()
    }
    (generated / "sources.json").write_text(
        json.dumps({"pins": SOURCES, "adapted_sha256": inventory}, indent=2)
        + "\n"
    )


if __name__ == "__main__":
    prepare()
