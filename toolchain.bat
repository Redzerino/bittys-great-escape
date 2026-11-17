@echo off
if not defined RAYLIB set "RAYLIB=C:\raylib"
if not defined BITTY_CC (
    if exist "C:\msys64\ucrt64\bin\gcc.exe" (
        set "BITTY_CC=C:\msys64\ucrt64\bin\gcc.exe"
    ) else (
        set "BITTY_CC=%RAYLIB%\w64devkit\bin\gcc.exe"
    )
)
if not exist "%BITTY_CC%" (
    echo No supported GCC compiler found. Set BITTY_CC to your installed gcc.exe.
    exit /b 1
)
for %%I in ("%BITTY_CC%") do set "PATH=%%~dpI;%PATH%"
exit /b 0
