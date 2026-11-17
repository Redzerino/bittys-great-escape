@echo off
setlocal
cd /d "%~dp0"
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
"%BITTY_CC%" tests\lexer_test.c src\lexer.c -Isrc -o lexer_test.exe -std=c99 -Wall -Wextra -g -static-libgcc
if errorlevel 1 exit /b 1
lexer_test.exe
exit /b %errorlevel%
