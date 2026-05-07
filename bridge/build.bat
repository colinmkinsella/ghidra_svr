@echo off
REM Build the ghidra-bridge shadow JAR.
REM
REM Usage:
REM   build.bat [ghidraHome=C:\path\to\ghidra]
REM
REM If ghidraHome is not passed here, set it in gradle.properties.

setlocal

if not "%~1"=="" (
    set GHIDRA_ARG=-PghidraHome=%~1
) else (
    set GHIDRA_ARG=
)

where gradle >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    gradle %GHIDRA_ARG% shadowJar
) else (
    echo Gradle not found on PATH. Install Gradle 8+ or add it to PATH.
    exit /b 1
)

echo.
echo Build complete. JAR is at: build\libs\ghidra-bridge-0.1.0.jar
