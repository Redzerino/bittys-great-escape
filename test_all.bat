@echo off
setlocal
cd /d "%~dp0"
set "BITTY_FAILURES=0"
call test_lexer.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
call test_parser.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
call test_vm.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
call test_levels.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
call test_endless.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
call test_escape.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
call test_vm_visuals.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
call test_save.bat
if errorlevel 1 set /a BITTY_FAILURES+=1
echo Failed or blocked suites: %BITTY_FAILURES%
if not "%BITTY_FAILURES%"=="0" exit /b 1
exit /b 0
