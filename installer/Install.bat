@echo off
rem Installs or updates FastPhysics. Optional argument: the game folder.
if not exist "%~dp0files\install.ps1" (
    echo The "files" folder was not found next to Install.bat.
    echo Extract the whole archive first, then run Install.bat from the extracted folder.
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\install.ps1" %*
pause
