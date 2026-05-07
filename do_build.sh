#!/usr/bin/env bash
# do_build.sh  —  Quick incremental rebuild + install (no CMake configure step)
#
# Use this after an initial ./build.sh run when you just want to recompile
# and reinstall without re-running CMake configure.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_DIR="$SCRIPT_DIR/plugin/build"
BUILD_LOG="$SCRIPT_DIR/build_log.txt"
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

{
    echo "START"
    cmake --build "$BUILD_DIR" --config RelWithDebInfo -j "$NPROC"
    echo "BUILD_DONE errorlevel=$?"
    cmake --install "$BUILD_DIR" --config RelWithDebInfo
    echo "INSTALL_DONE errorlevel=$?"
} 2>&1 | tee "$BUILD_LOG"
