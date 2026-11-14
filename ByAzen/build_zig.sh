#!/bin/sh
# Cross-builds ByAzen.exe on Linux/macOS with Zig (bundled clang + MinGW-w64).
#   pip install ziglang    ->  ZIG="python3 -m ziglang" ./build_zig.sh
# On Windows with MSYS2 MinGW-w64 use build_native.bat instead.
set -eu
ZIG="${ZIG:-zig}"
cd "$(dirname "$0")/src"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

$ZIG rc /c65001 resource.rc "$tmp/resource.res"
$ZIG c++ -target x86_64-windows-gnu -std=c++20 -O2 -Wall -Wextra -Werror \
    -municode -Wl,--subsystem,windows \
    byazen_native.cpp "$tmp/resource.res" \
    -lgdiplus -lgdi32 -luser32 -lshell32 -lwinmm -lole32 -lcomdlg32 -ladvapi32 \
    -o "$tmp/ByAzen.exe"
cp "$tmp/ByAzen.exe" ../ByAzen.exe
echo "Build succeeded: ByAzen.exe"
