@echo off
setlocal
cd /d "%~dp0"
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
"%BITTY_CC%" tests\parser_test.c src\parser.c src\ast.c src\lexer.c -Isrc -o parser_test.exe -std=c99 -Wall -Wextra -g -static-libgcc
if errorlevel 1 exit /b 1
parser_test.exe
exit /b %errorlevel%
