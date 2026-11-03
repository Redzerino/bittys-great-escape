@echo off
REM Build and run the VM tests (no raylib needed)

C:\raylib\w64devkit\bin\gcc.exe tests\vm_test.c src\vm.c src\compiler.c src\parser.c src\ast.c src\lexer.c -o vm_test.exe -std=c99 -Wall -Wextra -g

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

vm_test.exe
