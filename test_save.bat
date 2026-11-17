@echo off
setlocal
cd /d "%~dp0"
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
"%BITTY_CC%" tests\save_test.c src\save.c -Isrc -o save_test.exe -std=c99 -Wall -Wextra -g -static-libgcc
if errorlevel 1 exit /b 1
save_test.exe
exit /b %errorlevel%
