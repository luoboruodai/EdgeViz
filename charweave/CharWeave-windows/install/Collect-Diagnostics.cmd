@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-Diagnostics.ps1" -OutputPath "%~dp0CharWeave-diagnostics.txt"
pause
