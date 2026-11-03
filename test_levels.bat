@echo off
REM Check every level file: it loads, its solution passes all its tests,
REM and its starter code compiles but doesn't already pass.

C:\raylib\w64devkit\bin\gcc.exe tests\levels_test.c src\levels.c src\repair.c src\vm.c src\compiler.c src\parser.c src\ast.c src\lexer.c -o levels_test.exe -std=c99 -Wall -Wextra -g

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

levels_test.exe
