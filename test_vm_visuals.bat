@echo off
setlocal
cd /d "%~dp0"
set "BITTY_CC=C:\msys64\ucrt64\bin\gcc.exe"
if not exist "%BITTY_CC%" set "BITTY_CC=C:\raylib\w64devkit\bin\gcc.exe"
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
"%BITTY_CC%" tests\vm_visual_test.c src\vm.c src\compiler.c src\parser.c src\ast.c src\lexer.c -Isrc -o vm_visual_test.exe -std=c99 -Wall -Wextra -g -static-libgcc
if errorlevel 1 exit /b 1
vm_visual_test.exe
exit /b %errorlevel%
