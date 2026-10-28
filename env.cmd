@echo off
rem runs a command with the VS 2022 x64 toolchain, clang, cmake and ninja on PATH
set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
cd /d "%~dp0"
%*
