@echo off
setlocal
cd /d "%~dp0"
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
"%BITTY_CC%" tests\escape_test.c src\world.c src\room.c src\customisation.c src\save.c src\levels.c src\vm.c src\compiler.c src\parser.c src\ast.c src\lexer.c -Isrc -o escape_test.exe -std=c99 -Wall -Wextra -g -static-libgcc -I"%RAYLIB%\raylib\src" -L"%RAYLIB%\raylib\src" -lraylib -lopengl32 -lgdi32 -lwinmm
if errorlevel 1 exit /b 1
escape_test.exe
exit /b %errorlevel%
