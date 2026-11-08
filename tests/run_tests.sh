#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# DUBWISE's tests:
#   engine   the sound engine alone (tests/dsp/render.c): a scripted session, checked, into build/dubwise-demo.wav
#   ui       app.c and the engine against a fake panel (tests/stub/ui_test.c): the controls, MIDI clock in and out,
#            saving (no fm1-base needed)
#   host     with fm1-base in base/: the whole firmware in its simulator, app/tests/basic.txt
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FAIL=0
mkdir -p build
run() {
    name="$1"; shift
    if "$@" > "build/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see build/$name.log)"
        tail -15 "build/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run engine sh -c "$CC -O2 -std=c99 -Wall -Wextra -Werror -ffp-contract=off -DDUB_TEST -Iapp -o build/render \
    tests/dsp/render.c app/dub.c -lm && build/render build/dubwise-demo.wav"
run app $CC -fsyntax-only -std=c99 -Wall -Wextra -Werror -Wno-unused-function -DPLAT_HOST -Itests/stub -Iapp \
    tests/stub/check_app.c
run ui sh -c "$CC -std=c99 -Wall -Wextra -Werror -Wno-unused-function -DPLAT_HOST -DDUB_TEST -Itests/stub -Iapp \
    -o build/ui_test tests/stub/ui_test.c app/dub.c -lm && build/ui_test"
if [ -f base/tools/build.py ]; then
    run host-build sh build.sh --host
    mkdir -p app/build/host
    run host-basic app/build/host/dubwise_host app/tests/basic.txt app/build/host
else
    echo "  skip host (no fm1-base in base/: see README.md)"
fi
exit $FAIL
