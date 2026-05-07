@echo off
echo START > C:\Projects\ghidra_svr\build_log.txt
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -startdir=none -arch=x64 -host_arch=x64 >> C:\Projects\ghidra_svr\build_log.txt 2>&1
echo VSDEVCMD_DONE >> C:\Projects\ghidra_svr\build_log.txt
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set BUILD_DIR=C:\Projects\ghidra_svr\plugin\build
set "PATH=C:\qt\v6.7.2\bin;%PATH%"
%CMAKE% --build "%BUILD_DIR%" --config RelWithDebInfo -j 8 >> C:\Projects\ghidra_svr\build_log.txt 2>&1
echo BUILD_DONE errorlevel=%ERRORLEVEL% >> C:\Projects\ghidra_svr\build_log.txt
%CMAKE% --install "%BUILD_DIR%" --config RelWithDebInfo >> C:\Projects\ghidra_svr\build_log.txt 2>&1
echo INSTALL_DONE errorlevel=%ERRORLEVEL% >> C:\Projects\ghidra_svr\build_log.txt
