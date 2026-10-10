@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-DOScraft.ps1" -IsoPath "%~dp0DOSCRAFT.ISO" %*
if errorlevel 1 pause
