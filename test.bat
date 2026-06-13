@echo off
setlocal enabledelayedexpansion

rem ===========================================================================
rem  run_tests.bat  —  Build and run the binja-ghidra test suite (Windows)
rem ===========================================================================
rem  Usage:  run_tests.bat [options]
rem
rem    (no args)   Run both the C++ unit tests and the Java bridge tests
rem    --cpp       Run C++ tests only
rem    --java      Run Java tests only
rem    --no-build  Skip the cmake --build step (use existing test binary)
rem    --verbose   Pass --gtest_print_time=1 to C++ runner; show all Gradle output
rem
rem  Prerequisites:
rem    C++ tests: CMake build must be configured (cmake -B plugin\build -S plugin)
rem    Java tests: Java 17+ on PATH; bridge\gradle.properties must set ghidraHome
rem
rem  Exit code:
rem    0  All selected suites passed
rem    1  One or more suites failed or could not run
rem ===========================================================================

rem ---------------------------------------------------------------------------
rem  Configuration — mirror build.bat so paths resolve the same way
rem ---------------------------------------------------------------------------
set "CMAKE_EXE=cmake"
rem If cmake is not on PATH, uncomment and set the full path:
rem set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

set "SCRIPT_DIR=%~dp0"
rem Remove trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "PLUGIN_BUILD=%SCRIPT_DIR%\plugin\build"
set "BRIDGE_DIR=%SCRIPT_DIR%\bridge"
set "TEST_EXE=%PLUGIN_BUILD%\binja-ghidra-tests.exe"

rem ---------------------------------------------------------------------------
rem  Parse arguments
rem ---------------------------------------------------------------------------
set RUN_CPP=1
set RUN_JAVA=1
set DO_BUILD=1
set VERBOSE=0

:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="--cpp"      ( set RUN_JAVA=0 & shift & goto :parse_args )
if /i "%~1"=="--java"     ( set RUN_CPP=0  & shift & goto :parse_args )
if /i "%~1"=="--no-build" ( set DO_BUILD=0 & shift & goto :parse_args )
if /i "%~1"=="--verbose"  ( set VERBOSE=1  & shift & goto :parse_args )
if /i "%~1"=="--help"     ( goto :show_help )
if /i "%~1"=="-h"         ( goto :show_help )
echo WARNING: Unknown option: %~1
shift & goto :parse_args
:done_args

rem ---------------------------------------------------------------------------
rem  Tracking (0=not run, 1=passed, 2=failed, 3=error)
rem ---------------------------------------------------------------------------
set CPP_STATUS=0
set JAVA_STATUS=0

rem ---------------------------------------------------------------------------
rem  C++ tests
rem ---------------------------------------------------------------------------
if %RUN_CPP%==0 goto :skip_cpp

echo.
echo ====== C++ tests ======

rem Check the build has been configured
if not exist "%PLUGIN_BUILD%\build.ninja" (
if not exist "%PLUGIN_BUILD%\CMakeCache.txt" (
    echo ERROR: plugin\build has not been configured yet.
    echo        Run:  cmake -B plugin\build -S plugin   then try again.
    set CPP_STATUS=3
    goto :skip_cpp
))

rem Build the test binary
if %DO_BUILD%==1 (
    echo Building binja-ghidra-tests...
    "%CMAKE_EXE%" --build "%PLUGIN_BUILD%" --target binja-ghidra-tests
    if errorlevel 1 (
        echo ERROR: C++ test build failed.
        set CPP_STATUS=3
        goto :skip_cpp
    )
)

if not exist "%TEST_EXE%" (
    echo ERROR: Test binary not found at %TEST_EXE%
    echo        Build may have failed — run without --no-build to rebuild.
    set CPP_STATUS=3
    goto :skip_cpp
)

echo Running C++ test binary...
if %VERBOSE%==1 (
    "%TEST_EXE%" --gtest_color=yes --gtest_print_time=1
) else (
    "%TEST_EXE%" --gtest_color=yes
)

if errorlevel 1 (
    set CPP_STATUS=2
) else (
    set CPP_STATUS=1
)

:skip_cpp

rem ---------------------------------------------------------------------------
rem  Java tests
rem ---------------------------------------------------------------------------
if %RUN_JAVA%==0 goto :skip_java

echo.
echo ====== Java bridge tests ======

where java >nul 2>&1
if errorlevel 1 (
    echo ERROR: Java not found — cannot run Java tests.
    echo        Install JDK 17+ and ensure 'java' is on your PATH.
    set JAVA_STATUS=3
    goto :skip_java
)

if not exist "%BRIDGE_DIR%\gradle.properties" (
    echo ERROR: bridge\gradle.properties not found.
    echo        Create it with:  ghidraHome=C:\path\to\ghidra_12.x_PUBLIC
    set JAVA_STATUS=3
    goto :skip_java
)

echo Running Java tests via Gradle...
pushd "%BRIDGE_DIR%"

if %VERBOSE%==1 (
    call gradlew.bat test --rerun
) else (
    call gradlew.bat test --rerun --quiet
)
set GRADLE_EXIT=%errorlevel%
popd

if %GRADLE_EXIT% neq 0 (
    set JAVA_STATUS=2
) else (
    set JAVA_STATUS=1
)

:skip_java

rem ---------------------------------------------------------------------------
rem  Summary
rem ---------------------------------------------------------------------------
echo.
echo ======================================================
echo   Test summary
echo ======================================================

set EXIT_CODE=0

call :print_status "C++ tests (GhidraConnectionState / CheckinPreview / BridgeClientProtocol)" %CPP_STATUS%
call :print_status "Java tests (DatabaseImporter / DatabaseRoundTrip)"                          %JAVA_STATUS%

echo.
if %EXIT_CODE%==0 (
    echo All tests passed.
) else (
    echo One or more test suites failed.
)
exit /b %EXIT_CODE%

rem ---------------------------------------------------------------------------
rem  Subroutine: print_status  <label> <status-code>
rem    0 = skipped, 1 = passed, 2 = failed, 3 = error
rem ---------------------------------------------------------------------------
:print_status
set "_label=%~1"
set "_code=%~2"
if %_code%==1 ( echo   %_label%: PASSED  & goto :eof )
if %_code%==2 ( echo   %_label%: FAILED  & set EXIT_CODE=1 & goto :eof )
if %_code%==3 ( echo   %_label%: ERROR   & set EXIT_CODE=1 & goto :eof )
echo   %_label%: skipped
goto :eof

rem ---------------------------------------------------------------------------
rem  Help
rem ---------------------------------------------------------------------------
:show_help
echo Usage:  run_tests.bat [--cpp] [--java] [--no-build] [--verbose]
echo.
echo   (no args)   Run both C++ and Java tests
echo   --cpp       Run C++ tests only
echo   --java      Run Java tests only
echo   --no-build  Skip cmake --build (use existing binary)
echo   --verbose   More output from both test runners
echo.
exit /b 0
