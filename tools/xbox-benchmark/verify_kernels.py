#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify full CPU workloads natively against independent formulas."""

import os
from pathlib import Path
import subprocess
import tempfile
import compare

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    harness = Path(tmp) / "verify.c"
    lines = [
        "#include <stdio.h>",
        "#include <stdlib.h>",
        '#include "kernels.h"',
        "int main(void) { uint32_t *m = malloc(4*1024*1024); if (!m) return 2;",
    ]
    spec = [c for c in compare.cases() if c[0] < 9]
    for k, n, w, _, _ in spec:
        lines.append(
            f'bench_prepare(m, {w}); printf("%u\\n", '
            f"bench_cpu({k}, {n}, m, {w}));"
        )
    lines.append("free(m); return 0; }")
    harness.write_text("\n".join(lines))
    binary = Path(tmp) / "verify"
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root),
            str(harness),
            str(root / "kernels.c"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    output = subprocess.check_output([str(binary)], text=True, timeout=120)
    actual = list(map(int, output.splitlines()))
    expected = [compare.expected(c) for c in spec]
    if actual != expected:
        raise SystemExit(f"CPU output mismatch: {actual} != {expected}")
    print(
        f"{len(spec)} full CPU workloads match independent expected checksums "
        "(native, not Xbox)."
    )
