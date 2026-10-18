@echo off
rem Builds the mod loader and the mod, runs the self-test against the installed game and packs the release into dist\.
setlocal
cd /d "%~dp0"

rem The loader: SMModLoader\ in the source archive, otherwise the project next to this one
set "LOADER=%~dp0SMModLoader"
if not exist "%LOADER%\build.bat" set "LOADER=%~dp0..\Scrap Mechanic Mod Loader"
call "%LOADER%\build.bat"
if errorlevel 1 exit /b 1
call "%~dp0build.bat"
if errorlevel 1 exit /b 1

echo.
echo Running the self-test...
"%~dp0build\SelfTest.exe" %*
if errorlevel 1 (
    echo.
    echo Self-test failed - the release was not packed.
    echo Pass the path to ScrapMechanic.exe if the game is not in the default Steam folder.
    exit /b 1
)

echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1"
if errorlevel 1 exit /b 1
