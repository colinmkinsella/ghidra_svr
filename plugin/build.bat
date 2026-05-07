@echo off
REM Build the binja-ghidra plugin using Ninja from within the VS Developer environment.
REM
REM Usage (from a normal cmd or PowerShell):
REM   build.bat [clean]
REM
REM Prerequisites already satisfied on this machine:
REM   BN API:    C:\Projects\binaryninja-api  (at commit 5accfca)
REM   Qt6:       C:\qt\v6.7.2
REM   Ghidra:    C:\ghidra_11.0.1_PUBLIC
REM   Ninja:     C:\ninja\ninja.exe
REM   VS 2022/18:C:\Program Files\Microsoft Visual Studio\18\Professional

setlocal

set VSDEVCMD="C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"
set BN_API=C:\Projects\binaryninja-api
set Qt6_DIR=C:\qt\v6.7.2\lib\cmake\Qt6
set BN_INSTALL=C:\Program Files\Vector35\BinaryNinja
set CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA_EXE=C:\ninja\ninja.exe
set BUILD_DIR=%~dp0build

if /I "%~1"=="clean" (
    echo Removing build directory...
    rmdir /s /q "%BUILD_DIR%" 2>nul
)

REM Run everything inside the VS x64 developer environment.
cmd /c "%VSDEVCMD%" -startdir=none -arch=x64 -host_arch=x64 ^
    && "%CMAKE_EXE%" ^
        -B "%BUILD_DIR%" ^
        -S "%~dp0." ^
        -G Ninja ^
        -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
        -DBN_API_PATH="%BN_API%" ^
        -DQt6_DIR="%Qt6_DIR%" ^
        -DBN_INSTALL_DIR="%BN_INSTALL%" ^
        -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    && "%CMAKE_EXE%" --build "%BUILD_DIR%" --config RelWithDebInfo

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED.
    exit /b %ERRORLEVEL%
)

echo.
echo Build succeeded.
echo To install: cmake --install "%BUILD_DIR%" --config RelWithDebInfo
