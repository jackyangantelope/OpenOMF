#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
gcc -std=gnu11 -g -fsanitize=address,undefined -fno-pie -no-pie \
    -ffunction-sections -fdata-sections $(pkg-config --cflags sdl2) \
    -Ideps/openomf/src tests/test_omf_input.c deps/openomf/src/controller/joystick.c \
    -Wl,--gc-sections $(pkg-config --libs sdl2) -o build/host/omf-input
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/host/omf-input
