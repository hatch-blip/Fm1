#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build DUBWISE with fm1-base (base/, a git submodule; see README.md).
#   ./build.sh [--release X.Y] [--host | --web]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain; base/tools/get_toolchain.sh)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || {
    echo "build.sh: no fm1-base in base/. Once:"
    echo "  git submodule add https://github.com/charlesvestal/fm1-base base"
    echo "  git -C base checkout b0976f295ccaeaf77a8053a5a785a9cc21f5345f && git submodule update --init --recursive"
    exit 1
}
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
exec "$PY" base/tools/build.py app "$@"
