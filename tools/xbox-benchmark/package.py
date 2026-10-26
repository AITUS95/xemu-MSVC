#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package identical Xbox/xemu executables and the optional timing mode."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
import zlib

p = argparse.ArgumentParser()
p.add_argument("--nxdk", type=Path, required=True)
args = p.parse_args()
root = Path(__file__).resolve().parent
repo = root.parent.parent
sdk_revision = subprocess.check_output(
    ["git", "-C", str(args.nxdk), "rev-parse", "HEAD"], text=True
).strip()
if sdk_revision != "03acd1415f7ccc3127bcfb629224aacdd95d783c":
    p.error("Unpinned nxdk revision")
package = root / "package"
if package.exists():
    shutil.rmtree(package)
package.mkdir()
for name in (
    "README.md",
    "VALIDATION.md",
    "VOLUNTEER-NOTES.txt",
    "compare.py",
):
    shutil.copy2(root / name, package / name)

metadata_templates = {
    "hardware-info.example.json": {
        "platform": "original-xbox",
        "stock_clocks": None,
        "ram_mb": None,
        "revision": "UNKNOWN",
        "bios": "UNKNOWN",
        "video_region": "UNKNOWN",
        "collector_alias": "UNKNOWN",
    },
    "xemu-info.example.json": {
        "render_scale": 1,
        "icount": False,
        "cold_boot": True,
        "commit": "UNKNOWN",
        "renderer": "UNKNOWN",
        "host_cpu": "UNKNOWN",
        "host_gpu": "UNKNOWN",
        "os": "UNKNOWN",
        "settings": "UNKNOWN",
    },
}
for name, template in metadata_templates.items():
    (package / name).write_text(json.dumps(template, indent=2) + "\n")
shutil.copy2(root / "bin/default.xbe", package / "default.xbe")
shutil.copy2(root / "xbox-benchmark.iso", package / "xbox-benchmark.iso")
with tempfile.TemporaryDirectory() as tmp:
    contents = Path(tmp) / "disc"
    contents.mkdir()
    shutil.copy2(package / "default.xbe", contents / "default.xbe")
    (contents / "XEMU.TXT").write_text("xemu benchmark device required\n")
    subprocess.run(
        [
            str(args.nxdk / "tools/extract-xiso/build/extract-xiso"),
            "-c",
            str(contents),
            str(package / "xbox-benchmark-xemu.iso"),
        ],
        check=True,
    )
source_hash = hashlib.sha256()
for relative in (
    "tools/xbox-benchmark",
    "hw/xbox/benchmark-clock.h",
    "hw/xbox/benchmark.c",
    "tests/unit/test-xemu-benchmark.c",
):
    path = repo / relative
    files = sorted(path.iterdir()) if path.is_dir() else [path]
    for item in files:
        if not item.is_file() or item.suffix in (
            ".obj",
            ".d",
            ".exe",
            ".iso",
            ".zip",
        ):
            continue
        rel = item.relative_to(repo)
        target = package / "source" / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, target)
        source_hash.update(str(rel).encode() + b"\0" + item.read_bytes())
shutil.copy2(repo / "COPYING", package / "COPYING")
shutil.copytree(args.nxdk / "LICENSES", package / "nxdk-licenses")
binary = (package / "default.xbe").read_bytes()
manifest = {
    "suite": "xbox-benchmark-pilot-1",
    "status": "experimental-pilot-no-hardware-baseline",
    "xbe_sha256": hashlib.sha256(binary).hexdigest(),
    "xbe_crc32": f"{zlib.crc32(binary):08x}",
    "xbe_bytes": len(binary),
    "nxdk_revision": sdk_revision,
    "nxdk_submodules": subprocess.check_output(
        ["git", "-C", str(args.nxdk), "submodule", "status"], text=True
    ).splitlines(),
    "source_commit": subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True
    ).strip(),
    "source_dirty": bool(
        subprocess.check_output(
            ["git", "-C", str(repo), "status", "--porcelain"], text=True
        )
    ),
    "included_source_sha256": source_hash.hexdigest(),
    "compiler": subprocess.check_output(
        ["clang", "--version"], text=True
    ).splitlines()[0],
    "flags": (
        "-O2 -Wall -Wextra -Werror -fno-vectorize "
        "-fno-slp-vectorize; nxdk pentium3 target"
    ),
}
(package / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
files = sorted(f for f in package.rglob("*") if f.is_file())
(package / "SHA256SUMS").write_text(
    "".join(
        f"{hashlib.sha256(f.read_bytes()).hexdigest()}  "
        f"{f.relative_to(package).as_posix()}\n"
        for f in files
    )
)
with zipfile.ZipFile(
    root / "xbox-benchmark-pilot.zip", "w", zipfile.ZIP_DEFLATED
) as archive:
    for f in sorted(package.rglob("*")):
        if f.is_file():
            archive.write(f, f.relative_to(package))
print(f"Pilot package: {root / 'xbox-benchmark-pilot.zip'}")
