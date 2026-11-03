@echo off
REM Build and run the lexer tests (no raylib needed)

C:\raylib\w64devkit\bin\gcc.exe tests\lexer_test.c src\lexer.c -o lexer_test.exe -std=c99 -Wall -Wextra -g

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

lexer_test.exe
