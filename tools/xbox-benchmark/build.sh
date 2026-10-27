#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
suite_dir=$(cd -- "$(dirname -- "$0")" && pwd)
sdk_revision=03acd1415f7ccc3127bcfb629224aacdd95d783c
: "${NXDK_DIR:?Set NXDK_DIR to an isolated nxdk checkout directory}"
if [[ ! -d "$NXDK_DIR/.git" ]]; then
    git clone https://github.com/XboxDev/nxdk.git "$NXDK_DIR"
fi
git -C "$NXDK_DIR" checkout --detach "$sdk_revision"
git -C "$NXDK_DIR" submodule update --init --depth 1 \
    lib/libcxx lib/pdclib lib/net/lwip lib/usb/libusbohci tools/extract-xiso \
    lib/zlib lib/libpng/libpng lib/libjpeg/libjpeg-turbo
export PATH="$NXDK_DIR/bin:$PATH"
python3 "$suite_dir/prepare_upstream.py"
make -C "$suite_dir" -j"${JOBS:-4}"
python3 "$suite_dir/package.py" --nxdk "$NXDK_DIR"
