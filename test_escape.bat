@echo off
REM Plays every ESCAPE level's [solution] and checks Bitty reaches the EXIT.
REM Needs raylib (it uses the real game world), so it's built like the game.
if "%RAYLIB%"=="" set RAYLIB=C:\raylib

%RAYLIB%\w64devkit\bin\gcc.exe tests\escape_test.c src\world.c src\customisation.c src\save.c src\levels.c src\lexer.c src\parser.c src\ast.c src\compiler.c src\vm.c -o escape_test.exe -std=c99 -Wall -Wextra -Isrc -I%RAYLIB%\raylib\src -L%RAYLIB%\raylib\src -lraylib -lopengl32 -lgdi32 -lwinmm
if %errorlevel% neq 0 (
    echo BUILD FAILED
    exit /b 1
)
escape_test.exe
