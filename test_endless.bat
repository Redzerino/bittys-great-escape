@echo off
setlocal
cd /d "%~dp0"
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
"%BITTY_CC%" tests\endless_test.c src\endless.c src\levels.c src\repair.c src\vm.c src\compiler.c src\parser.c src\ast.c src\lexer.c -Isrc -o endless_test.exe -std=c99 -Wall -Wextra -g -static-libgcc
if errorlevel 1 exit /b 1
endless_test.exe 40
exit /b %errorlevel%
