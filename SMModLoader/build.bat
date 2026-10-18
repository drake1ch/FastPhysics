@echo off
rem Builds bin\SMModLoader.dll and the tests: build\LoaderTest.exe, build\TestMod.dll.
rem Requires Visual Studio with the "Desktop development with C++" workload.
setlocal
cd /d "%~dp0"

set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "VSDIR="
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with C++ tools was not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist bin mkdir bin
if not exist build mkdir build

rem /MT links the CRT statically: the DLL must not import vcruntime140_1.dll, which it replaces.
rem /Brepro (compiler and linker) makes the build reproducible: every mod's release.bat rebuilds the
rem loader, and the same source must give the same file in every mod package.
set "CLFLAGS=/nologo /O2 /MT /utf-8 /W4 /permissive- /std:c++17 /EHs-c- /D_HAS_EXCEPTIONS=0 /Brepro /Fobuild\"

rc /nologo /fo build\SMModLoader.res src\SMModLoader.rc
if errorlevel 1 exit /b 1

cl %CLFLAGS% src\SMModLoader.cpp build\SMModLoader.res /LD /link /Brepro /DEF:src\SMModLoader.def /OUT:bin\SMModLoader.dll /IMPLIB:build\SMModLoader.lib
if errorlevel 1 exit /b 1

cl %CLFLAGS% tests\TestMod.cpp /LD /link /OUT:build\TestMod.dll /NOIMPLIB
if errorlevel 1 exit /b 1

cl %CLFLAGS% tests\LoaderTest.cpp /link /OUT:build\LoaderTest.exe
if errorlevel 1 exit /b 1

echo.
echo Built: bin\SMModLoader.dll
echo Tests: build\LoaderTest.exe bin\SMModLoader.dll build\TestMod.dll
echo        powershell -ExecutionPolicy Bypass -File tests\InstallTest.ps1   (after release.bat of the mods)
