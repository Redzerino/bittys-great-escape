@echo off
REM Build Bitty's Great Escape on Windows (raylib installer in C:\raylib)
REM Compiles every .c file in src\ so new files are picked up automatically.

C:\raylib\w64devkit\bin\gcc.exe src\*.c -o bitty.exe -std=c99 -Wall -Wextra -g -IC:\raylib\raylib\src -LC:\raylib\raylib\src -lraylib -lopengl32 -lgdi32 -lwinmm

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

echo Build OK - running...
bitty.exe
