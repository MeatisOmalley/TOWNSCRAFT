@echo off
setlocal
set "ROOT=%~dp0..\.."
set "EMU=%ROOT%\build\emulator\main_cui\Release\Tsugaru_CUI.exe"
set "ROM=%ROOT%\build\play\STUBROM"
set "ISO=%~dp0TOWNSCRAFT_14_timing_stream_budget.ISO"
if not exist "%EMU%" goto missing
if not exist "%ROM%" goto missing
if not exist "%ISO%" goto missing
pushd "%ROOT%"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\run_1989.ps1" -IsoPath "%ISO%" %*
set "EXITCODE=%ERRORLEVEL%"
popd
if not "%EXITCODE%"=="0" pause
exit /b %EXITCODE%
:missing
echo Townscraft launcher could not find a required file.
echo Emulator: "%EMU%"
echo ROM:      "%ROM%"
echo ISO:      "%ISO%"
pause
exit /b 1

