#!/bin/sh
# Copyright (C) 2026 Spamoidutout - SPDX-License-Identifier: LGPL-2.1-or-later
# Builds SC4MiddleClickThingy.dll (32-bit, MSVC ABI) with clang + lld, from Linux, macOS or Windows.
# Requirements: clang, lld-link and llvm-dlltool (LLVM 15 or newer). No Visual Studio needed.
#
# Outputs:
#   build/out/SC4MiddleClickThingy.dll   the plugin
#   test/out/SC4MiddleClickThingy.dll    test build (SC4_TEST: input driven by the test program)
#   test/out/harness.exe                 the fake game; run: cd test/out && WINEDEBUG=-all wine harness.exe
set -e
cd "$(dirname "$0")"
OUT=out
TEST=../test/out
mkdir -p "$OUT" "$TEST"

CXXFLAGS="--target=i686-pc-windows-msvc -std=c++17 -O2 -march=pentium4 -mfpmath=sse \
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector \
  -fms-extensions -Wall -Wextra"
LINK="lld-link /Brepro /nodefaultlib /machine:x86 /safeseh:no"

# Import libraries: a few kernel32 and user32 functions, and the SEH handler from the system msvcrt.dll.
llvm-dlltool -m i386 -k -d kernel32.def -l "$OUT/kernel32.lib"
llvm-dlltool -m i386 -k -d user32.def -l "$OUT/user32.lib"
llvm-dlltool -m i386 -d msvcrt.def -l "$OUT/msvcrt.lib"
LIBS="$OUT/kernel32.lib $OUT/user32.lib $OUT/msvcrt.lib"

clang++ $CXXFLAGS -c ../src/Runtime.cpp -o "$OUT/Runtime.obj"
clang++ $CXXFLAGS -c ../src/MiddleClick.cpp -o "$OUT/MiddleClick.obj"

# /safeseh:no is required: the plugin's SEH handler comes from msvcrt.dll.
$LINK /dll /entry:DllEntry /subsystem:windows /out:"$OUT/SC4MiddleClickThingy.dll" \
  "$OUT/Runtime.obj" "$OUT/MiddleClick.obj" $LIBS

# Test build and fake game.
clang++ $CXXFLAGS -DSC4_TEST -c ../src/MiddleClick.cpp -o "$TEST/MiddleClick.obj"
$LINK /dll /entry:DllEntry /subsystem:windows /out:"$TEST/SC4MiddleClickThingy.dll" \
  "$OUT/Runtime.obj" "$TEST/MiddleClick.obj" $LIBS
clang++ $CXXFLAGS -I../src -c ../test/Harness.cpp -o "$TEST/Harness.obj"
$LINK /entry:HarnessMain /subsystem:console /out:"$TEST/harness.exe" \
  "$TEST/Harness.obj" "$OUT/Runtime.obj" $LIBS

echo "Built $OUT/SC4MiddleClickThingy.dll"
