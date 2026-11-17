@echo off
setlocal
cd /d "%~dp0"
if not exist "bitty-latest.exe" (
    echo No current build. Run build.bat first.
    exit /b 1
)
start "" /D "%~dp0" "%~dp0bitty-latest.exe"
if errorlevel 1 (
    echo The game exists, but Windows prevented it from opening.
    exit /b 1
)
exit /b 0
