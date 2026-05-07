@echo off
REM Configure and build the binja-ghidra plugin.
REM
REM Prerequisites:
REM   1. Clone binaryninja-api at the commit in BinaryNinja/api_REVISION.txt:
REM        git clone https://github.com/Vector35/binaryninja-api.git
REM        git -C binaryninja-api checkout 5accfca2dc53fe5685dbe9e74f519a7f2f657d48
REM   2. Install Qt 6 SDK (matches the Qt version BN ships — Qt6.x).
REM        Options: Qt Online Installer, or vcpkg install qt6-base
REM   3. Visual Studio 2022 (MSVC) or LLVM/clang-cl with CMake 3.24+.
REM
REM Usage:
REM   setup.bat <path-to-binaryninja-api> <path-to-Qt6-cmake-dir>
REM
REM Example:
REM   setup.bat C:\repos\binaryninja-api C:\Qt\6.7.0\msvc2019_64\lib\cmake\Qt6

setlocal

if "%~1"=="" (
    echo Usage: setup.bat ^<BN_API_PATH^> ^<Qt6_DIR^>
    exit /b 1
)
if "%~2"=="" (
    echo Usage: setup.bat ^<BN_API_PATH^> ^<Qt6_DIR^>
    exit /b 1
)

set BN_API=%~1
set QT6_DIR=%~2
set BUILD_DIR=build

cmake -B %BUILD_DIR% -S . ^
    -DBN_API_PATH="%BN_API%" ^
    -DQt6_DIR="%QT6_DIR%" ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

if %ERRORLEVEL% NEQ 0 (
    echo CMake configure failed.
    exit /b %ERRORLEVEL%
)

cmake --build %BUILD_DIR% --config RelWithDebInfo

if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b %ERRORLEVEL%
)

echo.
echo Build succeeded.
echo Install with:  cmake --install %BUILD_DIR% --config RelWithDebInfo
