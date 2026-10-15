@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-DOScraft-386.ps1"
if errorlevel 1 pause
