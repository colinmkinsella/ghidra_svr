#!/usr/bin/env bash
# build.sh  —  Build the complete binja-ghidra project (Linux / macOS)
#
# Usage:  ./build.sh [clean] [install] [bridge] [plugin]
#
#   clean    Delete build directories before building
#   install  Copy the plugin into the BN user plugins folder afterwards
#   bridge   Build the Java bridge only  (default: build both)
#   plugin   Build the C++ plugin only   (default: build both)
#
# Examples:
#   ./build.sh                   — incremental build of both components
#   ./build.sh clean install     — clean rebuild + install into BN
#   ./build.sh bridge            — rebuild bridge JAR only
#
# Environment variables (override defaults):
#   Qt6_DIR     Path to Qt6 CMake dir  (default: /usr/lib/cmake/Qt6)
#   BN_INSTALL  Path to BN install dir (default: /opt/Vector35/BinaryNinja)

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# Configuration — adjust paths here if your environment differs
# ---------------------------------------------------------------------------
Qt6_DIR="${Qt6_DIR:-/usr/lib/cmake/Qt6}"
BN_INSTALL="${BN_INSTALL:-/opt/Vector35/BinaryNinja}"

BRIDGE_DIR="$SCRIPT_DIR/bridge"
PLUGIN_DIR="$SCRIPT_DIR/plugin"
PLUGIN_BUILD="$PLUGIN_DIR/build"
BRIDGE_JAR="$BRIDGE_DIR/build/libs/ghidra-bridge-0.1.0.jar"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
DO_CLEAN=0
DO_INSTALL=0
BRIDGE_ONLY=0
PLUGIN_ONLY=0

for arg in "$@"; do
    case "$(echo "$arg" | tr '[:upper:]' '[:lower:]')" in
        clean)   DO_CLEAN=1 ;;
        install) DO_INSTALL=1 ;;
        bridge)  BRIDGE_ONLY=1 ;;
        plugin)  PLUGIN_ONLY=1 ;;
    esac
done

DO_BRIDGE=1
DO_PLUGIN=1
if [[ $BRIDGE_ONLY -eq 1 && $PLUGIN_ONLY -eq 0 ]]; then DO_PLUGIN=0; fi
if [[ $PLUGIN_ONLY -eq 1 && $BRIDGE_ONLY -eq 0 ]]; then DO_BRIDGE=0; fi

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
if [[ $DO_CLEAN -eq 1 ]]; then
    echo
    echo "Cleaning build directories..."
    rm -rf "$PLUGIN_BUILD" "$BRIDGE_DIR/build"
    echo "Done."
fi

# ---------------------------------------------------------------------------
# Build Java bridge
# ---------------------------------------------------------------------------
if [[ $DO_BRIDGE -eq 1 ]]; then
    echo
    echo "====== Building Java bridge ======"
    pushd "$BRIDGE_DIR" > /dev/null
    ./gradlew shadowJar
    popd > /dev/null
    echo "Bridge JAR: $BRIDGE_JAR"
fi

# ---------------------------------------------------------------------------
# Build C++ plugin
# ---------------------------------------------------------------------------
if [[ $DO_PLUGIN -eq 1 ]]; then
    echo
    echo "====== Configuring C++ plugin ======"
    cmake \
        -B "$PLUGIN_BUILD" \
        -S "$PLUGIN_DIR" \
        -G Ninja \
        -DQt6_DIR="$Qt6_DIR" \
        -DBN_INSTALL_DIR="$BN_INSTALL" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
    if [[ $? -ne 0 ]]; then
        echo "ERROR: CMake configure failed."
        exit 1
    fi

    echo
    echo "====== Building C++ plugin ======"
    cmake --build "$PLUGIN_BUILD" --config RelWithDebInfo -j "$NPROC"
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Plugin build failed."
        exit 1
    fi
    echo "Plugin: $PLUGIN_BUILD/libbinja-ghidra.so"
fi

# ---------------------------------------------------------------------------
# Install into Binary Ninja plugins folder
# ---------------------------------------------------------------------------
if [[ $DO_INSTALL -eq 1 ]]; then
    echo
    echo "====== Installing ======"
    cmake --install "$PLUGIN_BUILD" --config RelWithDebInfo
    if [[ $? -ne 0 ]]; then
        echo "ERROR: Install failed."
        exit 1
    fi
    echo "Installed to: ~/.binaryninja/plugins/"
fi

echo
echo "====== Build complete ======"
