@echo off
setlocal
cd /d "%~dp0"
REM Prefer MSYS2 locally; retain the raylib compiler for teammates.
set "BITTY_CC=C:\msys64\ucrt64\bin\gcc.exe"
if not exist "%BITTY_CC%" set "BITTY_CC=C:\raylib\w64devkit\bin\gcc.exe"
if not exist "%BITTY_CC%" (
    echo No GCC compiler found at the standard MSYS2 or raylib paths
    exit /b 1
)
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
"%BITTY_CC%" src\*.c -o bitty-latest.exe -std=c99 -Wall -Wextra -g -static-libgcc -IC:\raylib\raylib\src -LC:\raylib\raylib\src -lraylib -lopengl32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo BUILD FAILED - existing game has not been replaced.
    exit /b 1
)
echo Build OK - running latest local code...
start "" /D "%~dp0" "%~dp0bitty-latest.exe"
endlocal
