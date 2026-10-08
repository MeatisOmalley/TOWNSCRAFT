@echo off
setlocal
cd /d "%~dp0"
".\build\emulator\main_cui\Release\Tsugaru_CUI.exe" ".\build\play\STUBROM" -CD ".\iso\TOWNSCRAFT.ISO" -TOWNSTYPE MODEL2 -FREQ 16 -MEMSIZE 2 -DIFFMOUSE -DONTAUTOSAVECMOS -YESWAIT -AUTOSCALE -MAXIMIZE

