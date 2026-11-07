#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# VoiceBar - build script (works on Linux, macOS and Windows/MSYS2)
#
# Uses zig as a C compiler so a real Windows x64 .exe can be produced from any
# host:   zig cc -target x86_64-windows-gnu
#
#   ./build.sh            build ../VoiceBar.exe (release, stripped)
#   ./build.sh test       build + run the native unit tests of the core logic
#   ./build.sh debug      build with symbols, console subsystem (shows printf)
#
# Windows users can use build.bat instead.
# ---------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")"

ZIG="${ZIG:-zig}"
MODE="${1:-release}"

SRC=(
  src/core.c
  src/util.c
  src/config.c
  src/audio.c
  src/stt_server.c
  src/stt_cmd.c
  src/stt_local.c
  src/ui.c
  src/main.c
)

LIBS=(
  -lole32 -loleaut32 -luser32 -lgdi32 -lshell32
  -lwinmm -lwinhttp -lcomctl32 -ladvapi32
)

WARN=(-Wall -Wextra -Wno-unused-parameter -Wno-unused-function)

mkdir -p build

case "$MODE" in
  test)
    echo "== unit tests (native compiler) =="
    cc -O2 "${WARN[@]}" tests/test_core.c src/core.c -o build/test_core -lm
    ./build/test_core
    ;;

  debug)
    echo "== debug build =="
    "$ZIG" cc -target x86_64-windows-gnu -g -O0 "${WARN[@]}" "${SRC[@]}" \
      -o build/VoiceBar-debug.exe "${LIBS[@]}" -Wl,--subsystem,console
    echo "built build/VoiceBar-debug.exe"
    ;;

  *)
    echo "== release build =="
    "$ZIG" cc -target x86_64-windows-gnu -O2 "${WARN[@]}" "${SRC[@]}" \
      -o build/VoiceBar.exe "${LIBS[@]}" -Wl,--subsystem,windows -Wl,-s
    cp build/VoiceBar.exe VoiceBar.exe
    echo "built build/VoiceBar.exe -> copied to VoiceBar.exe"
    ;;
esac
