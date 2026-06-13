#!/usr/bin/env bash
# run_tests.sh  —  Build and run the binja-ghidra test suite (Linux / macOS)
#
# Usage:  ./run_tests.sh [options]
#
#   (no args)   Run both the C++ unit tests and the Java bridge tests
#   --cpp       Run C++ tests only
#   --java      Run Java tests only
#   --no-build  Skip the cmake --build step (use existing test binary)
#   --verbose   Pass --gtest_print_time=1 to C++ runner; show all Gradle output
#
# Prerequisites:
#   C++ tests: CMake build must have been configured (cmake -B plugin/build -S plugin)
#   Java tests: Java 17+ must be on PATH; gradle.properties must set ghidraHome
#
# Exit code:
#   0  All selected test suites passed
#   1  One or more suites failed or could not run

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PLUGIN_BUILD="$SCRIPT_DIR/plugin/build"
BRIDGE_DIR="$SCRIPT_DIR/bridge"

# ---------------------------------------------------------------------------
# Colour helpers (disabled when not writing to a terminal)
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    BOLD='\033[1m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BOLD=''; RESET=''
fi

info()    { echo -e "${BOLD}$*${RESET}"; }
success() { echo -e "${GREEN}$*${RESET}"; }
warn()    { echo -e "${YELLOW}$*${RESET}"; }
fail()    { echo -e "${RED}$*${RESET}"; }

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
RUN_CPP=1
RUN_JAVA=1
DO_BUILD=1
VERBOSE=0

for arg in "$@"; do
    case "$arg" in
        --cpp)      RUN_JAVA=0 ;;
        --java)     RUN_CPP=0  ;;
        --no-build) DO_BUILD=0 ;;
        --verbose)  VERBOSE=1  ;;
        -h|--help)
            sed -n '2,18p' "$0" | sed 's/^# *//'
            exit 0
            ;;
        *)
            warn "Unknown option: $arg  (try --help)"
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Tracking
# ---------------------------------------------------------------------------
CPP_STATUS="skipped"
JAVA_STATUS="skipped"

# ---------------------------------------------------------------------------
# C++ tests
# ---------------------------------------------------------------------------
if [[ $RUN_CPP -eq 1 ]]; then
    echo
    info "====== C++ tests ======"

    # Verify the build has been configured
    if [[ ! -f "$PLUGIN_BUILD/build.ninja" && ! -f "$PLUGIN_BUILD/Makefile" ]]; then
        fail "ERROR: plugin/build has not been configured yet."
        fail "       Run:  cmake -B plugin/build -S plugin   then try again."
        CPP_STATUS="error"
    else
        # (Re-)build the test binary
        if [[ $DO_BUILD -eq 1 ]]; then
            info "Building binja-ghidra-tests..."
            NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
            cmake --build "$PLUGIN_BUILD" --target binja-ghidra-tests -j "$NPROC"
        fi

        TEST_BIN="$PLUGIN_BUILD/binja-ghidra-tests"
        if [[ ! -x "$TEST_BIN" ]]; then
            fail "ERROR: Test binary not found at $TEST_BIN"
            fail "       Build may have failed — run without --no-build to rebuild."
            CPP_STATUS="error"
        else
            info "Running C++ test binary..."
            CPP_ARGS=("--gtest_color=yes")
            [[ $VERBOSE -eq 1 ]] && CPP_ARGS+=("--gtest_print_time=1")

            if "$TEST_BIN" "${CPP_ARGS[@]}"; then
                CPP_STATUS="passed"
            else
                CPP_STATUS="failed"
            fi
        fi
    fi
fi

# ---------------------------------------------------------------------------
# Java tests
# ---------------------------------------------------------------------------
if [[ $RUN_JAVA -eq 1 ]]; then
    echo
    info "====== Java bridge tests ======"

    if ! java -version > /dev/null 2>&1; then
        fail "ERROR: Java not found — cannot run Java tests."
        fail "       Install JDK 17+ and ensure 'java' is on your PATH."
        JAVA_STATUS="error"
    elif [[ ! -f "$BRIDGE_DIR/gradle.properties" ]]; then
        fail "ERROR: bridge/gradle.properties not found."
        fail "       Create it with:  ghidraHome=/path/to/ghidra_12.x_PUBLIC"
        JAVA_STATUS="error"
    else
        GRADLE_ARGS=(test --rerun)
        if [[ $VERBOSE -eq 0 ]]; then
            # Quiet build output; test results are always shown via testLogging
            GRADLE_ARGS+=(--quiet)
        fi

        info "Running Java tests via Gradle..."
        pushd "$BRIDGE_DIR" > /dev/null
        if ./gradlew "${GRADLE_ARGS[@]}"; then
            JAVA_STATUS="passed"
        else
            JAVA_STATUS="failed"
        fi
        popd > /dev/null
    fi
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo
info "======================================================"
info "  Test summary"
info "======================================================"

EXIT_CODE=0

print_status() {
    local suite="$1" status="$2"
    case "$status" in
        passed)  success "  $suite: PASSED" ;;
        failed)  fail    "  $suite: FAILED";  EXIT_CODE=1 ;;
        error)   fail    "  $suite: ERROR";   EXIT_CODE=1 ;;
        skipped) warn    "  $suite: skipped" ;;
    esac
}

print_status "C++ tests (GhidraConnectionState / CheckinPreview / BridgeClientProtocol)" "$CPP_STATUS"
print_status "Java tests (DatabaseImporter / DatabaseRoundTrip)"                          "$JAVA_STATUS"

echo
if [[ $EXIT_CODE -eq 0 ]]; then
    success "All tests passed."
else
    fail "One or more test suites failed."
fi

exit $EXIT_CODE
