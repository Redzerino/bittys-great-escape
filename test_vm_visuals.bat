@echo off
C:\raylib\w64devkit\bin\gcc.exe tests\vm_visual_test.c src\vm.c src\compiler.c src\parser.c src\ast.c src\lexer.c -Isrc -o vm_visual_test.exe -std=c99 -Wall -Wextra -g
if errorlevel 1 exit /b 1
vm_visual_test.exe
exit /b %errorlevel%
