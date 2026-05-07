#!/usr/bin/env bash
# Build the ghidra-bridge shadow JAR.
#
# Usage:
#   ./build.sh [/path/to/ghidra]
#
# If the Ghidra home path is not passed here, set it in gradle.properties.

set -euo pipefail

GHIDRA_ARG=""
if [[ $# -gt 0 ]]; then
    GHIDRA_ARG="-PghidraHome=$1"
fi

if command -v gradle &>/dev/null; then
    gradle $GHIDRA_ARG shadowJar
else
    echo "Gradle not found on PATH. Install Gradle 8+ or add it to PATH." >&2
    exit 1
fi

echo ""
echo "Build complete. JAR is at: build/libs/ghidra-bridge-0.1.0.jar"
