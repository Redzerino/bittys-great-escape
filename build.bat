@echo off
setlocal
cd /d "%~dp0"
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
"%BITTY_CC%" src\*.c -o bitty-build.exe -std=c99 -Wall -Wextra -g -static-libgcc -I"%RAYLIB%\raylib\src" -L"%RAYLIB%\raylib\src" -lraylib -lopengl32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo BUILD FAILED - the last working game is unchanged.
    exit /b 1
)
copy /y "bitty-build.exe" "bitty-latest.exe" >nul
if errorlevel 1 (
    echo Build succeeded. Close the running game, then run build.bat again to update it.
    exit /b 1
)
echo Build OK - opening the latest game.
call "%~dp0run.bat"
exit /b %errorlevel%
