@echo off
setlocal
cd /d "%~dp0"
REM Makes a ready-to-play copy of Bitty's Great Escape for people without raylib.
REM Output: BittysGreatEscape.zip  (unzip it, double-click bitty.exe)

call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1

echo Building release version...
REM -static: no extra DLLs needed   -mwindows: no black console window   -O2: faster
"%BITTY_CC%" src\*.c -o bitty_release.exe -std=c99 -O2 -static -mwindows -I"%RAYLIB%\raylib\src" -L"%RAYLIB%\raylib\src" -lraylib -lopengl32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

REM Fresh folder with just what the game needs: the exe and the levels
if exist release rmdir /s /q release
mkdir release\BittysGreatEscape
move /y bitty_release.exe release\BittysGreatEscape\bitty.exe >nul
xcopy levels release\BittysGreatEscape\levels\ /e /i /q >nul
REM Cloud saves: the database address goes with the game
if exist bitty_cloud.txt copy /y bitty_cloud.txt release\BittysGreatEscape\ >nul

REM Zip it up
if exist BittysGreatEscape.zip del BittysGreatEscape.zip
powershell -NoProfile -Command "Compress-Archive -Path 'release\BittysGreatEscape' -DestinationPath 'BittysGreatEscape.zip'"

echo.
echo Done! Send BittysGreatEscape.zip
echo They unzip it and double-click bitty.exe - nothing else to install.
endlocal
