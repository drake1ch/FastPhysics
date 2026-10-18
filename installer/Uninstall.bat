@echo off
rem Removes FastPhysics (and the mod loader, if no other mods use it). Optional argument: the game folder.
if not exist "%~dp0files\uninstall.ps1" (
    echo The "files" folder was not found next to Uninstall.bat.
    echo Extract the whole archive first, then run Uninstall.bat from the extracted folder.
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\uninstall.ps1" %*
pause
