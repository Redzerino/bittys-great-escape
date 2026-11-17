@echo off
setlocal
cd /d "%~dp0"
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
"%BITTY_CC%" tests\vm_test.c src\vm.c src\compiler.c src\parser.c src\ast.c src\lexer.c -Isrc -o vm_test.exe -std=c99 -Wall -Wextra -g -static-libgcc
if errorlevel 1 exit /b 1
vm_test.exe
exit /b %errorlevel%
