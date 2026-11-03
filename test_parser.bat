@echo off
REM Build and run the parser tests (no raylib needed)

C:\raylib\w64devkit\bin\gcc.exe tests\parser_test.c src\parser.c src\ast.c src\lexer.c -o parser_test.exe -std=c99 -Wall -Wextra -g

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

parser_test.exe
