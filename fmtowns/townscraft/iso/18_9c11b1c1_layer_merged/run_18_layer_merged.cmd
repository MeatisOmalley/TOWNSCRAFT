@echo off
setlocal
set "ROOT=%~dp0..\.."
set "EMU=%ROOT%\build\emulator\main_cui\Release\Tsugaru_CUI.exe"
set "ROM=%ROOT%\build\play\STUBROM"
set "ISO=%~dp0TOWNSCRAFT_18_layer_merged.ISO"
if not exist "%EMU%" goto missing
if not exist "%ROM%" goto missing
if not exist "%ISO%" goto missing
pushd "%ROOT%"
if errorlevel 1 goto missing
"%EMU%" "%ROM%" -CD "%ISO%" -TOWNSTYPE MODEL2 -FREQ 16 -MEMSIZE 2 -DIFFMOUSE -DONTAUTOSAVECMOS -YESWAIT -AUTOSCALE -MAXIMIZE
set "EXITCODE=%ERRORLEVEL%"
popd
if not "%EXITCODE%"=="0" pause
exit /b %EXITCODE%
:missing
echo Townscraft launcher could not find a required file or open its working directory.
echo Emulator: "%EMU%"
echo ROM:      "%ROM%"
echo ISO:      "%ISO%"
pause
exit /b 1
