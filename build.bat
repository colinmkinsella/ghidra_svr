@echo off
setlocal enabledelayedexpansion

rem ===========================================================================
rem  build.bat  —  Build the complete binja-ghidra project
rem ===========================================================================
rem  Usage:  build.bat [clean] [install] [bridge] [plugin]
rem
rem    clean    Delete build directories before building
rem    install  Copy the DLL + JAR into the BN user plugins folder afterwards
rem    bridge   Build the Java bridge only  (default: build both)
rem    plugin   Build the C++ plugin only   (default: build both)
rem
rem  Examples:
rem    build.bat                   — incremental build of both components
rem    build.bat clean install     — clean rebuild + install into BN
rem    build.bat bridge            — rebuild bridge JAR only
rem ===========================================================================

rem ---------------------------------------------------------------------------
rem  Configuration  — adjust paths here if your environment differs
rem ---------------------------------------------------------------------------
set "JAVA_HOME=C:\Program Files\Java\jdk-25.0.3+9"
set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"
set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "Qt6_DIR=C:\qt\v6.7.2\lib\cmake\Qt6"
set "QMAKE_BIN=C:\qt\v6.7.2\bin"
set "BN_INSTALL=C:\Program Files\Vector35\BinaryNinja"

set "BRIDGE_DIR=%~dp0bridge"
set "PLUGIN_DIR=%~dp0plugin"
set "PLUGIN_BUILD=%PLUGIN_DIR%\build"
set "BRIDGE_JAR=%BRIDGE_DIR%\build\libs\ghidra-bridge-0.1.0.jar"

rem ---------------------------------------------------------------------------
rem  Parse arguments
rem ---------------------------------------------------------------------------
set DO_CLEAN=0
set DO_INSTALL=0
set DO_BRIDGE=1
set DO_PLUGIN=1
set BRIDGE_ONLY=0
set PLUGIN_ONLY=0

for %%A in (%*) do (
    if /I "%%A"=="clean"   set DO_CLEAN=1
    if /I "%%A"=="install" set DO_INSTALL=1
    if /I "%%A"=="bridge"  set BRIDGE_ONLY=1
    if /I "%%A"=="plugin"  set PLUGIN_ONLY=1
)

if %BRIDGE_ONLY%==1 if %PLUGIN_ONLY%==0 set DO_PLUGIN=0
if %PLUGIN_ONLY%==1 if %BRIDGE_ONLY%==0 set DO_BRIDGE=0

rem ---------------------------------------------------------------------------
rem  Set up VS developer environment (required for C++ build; harmless otherwise)
rem ---------------------------------------------------------------------------
call "%VSDEVCMD%" -startdir=none -arch=x64 -host_arch=x64
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to initialise VS developer environment.
    exit /b 1
)

rem Add qmake to PATH so FindBinaryNinjaUI.cmake can locate Qt automatically.
set "PATH=%QMAKE_BIN%;%PATH%"

rem ---------------------------------------------------------------------------
rem  Clean
rem ---------------------------------------------------------------------------
if %DO_CLEAN%==1 (
    echo.
    echo Cleaning build directories...
    if exist "%PLUGIN_BUILD%"    rmdir /s /q "%PLUGIN_BUILD%"
    if exist "%BRIDGE_DIR%\build" rmdir /s /q "%BRIDGE_DIR%\build"
    echo Done.
)

rem ---------------------------------------------------------------------------
rem  Build Java bridge
rem ---------------------------------------------------------------------------
if %DO_BRIDGE%==1 (
    echo.
    echo ====== Building Java bridge ======
    pushd "%BRIDGE_DIR%"
    call gradlew.bat shadowJar
    set BUILD_ERR=!ERRORLEVEL!
    popd
    if !BUILD_ERR! NEQ 0 (
        echo ERROR: Bridge build failed.
        exit /b !BUILD_ERR!
    )
    echo Bridge JAR: %BRIDGE_JAR%
)

rem ---------------------------------------------------------------------------
rem  Build C++ plugin
rem ---------------------------------------------------------------------------
if %DO_PLUGIN%==1 (
    echo.
    echo ====== Configuring C++ plugin ======
    "%CMAKE_EXE%" ^
        -B "%PLUGIN_BUILD%" ^
        -S "%PLUGIN_DIR%" ^
        -G Ninja ^
        -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
        -DQt6_DIR="%Qt6_DIR%" ^
        -DBN_INSTALL_DIR="%BN_INSTALL%" ^
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: CMake configure failed.
        exit /b %ERRORLEVEL%
    )

    echo.
    echo ====== Building C++ plugin ======
    "%CMAKE_EXE%" --build "%PLUGIN_BUILD%" --config RelWithDebInfo -j 8
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Plugin build failed.
        exit /b %ERRORLEVEL%
    )
    echo Plugin DLL: %PLUGIN_BUILD%\binja-ghidra.dll
)

rem ---------------------------------------------------------------------------
rem  Install into Binary Ninja plugins folder
rem ---------------------------------------------------------------------------
if %DO_INSTALL%==1 (
    echo.
    echo ====== Installing ======

    "%CMAKE_EXE%" --install "%PLUGIN_BUILD%" --config RelWithDebInfo
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Install failed.
        exit /b %ERRORLEVEL%
    )
    echo Installed to: %APPDATA%\Binary Ninja\plugins\
)

echo.
echo ====== Build complete ======
