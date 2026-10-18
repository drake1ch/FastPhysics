@echo off
rem Builds bin\FastPhysics.dll and build\SelfTest.exe.
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

rem /MT links the CRT statically, so the DLL imports only KERNEL32. /arch:AVX2 is required by the solver kernels
rem (the DLL checks the CPU at start-up and stays inactive without AVX2/FMA).
set "CLFLAGS=/nologo /O2 /MT /utf-8 /W3 /permissive- /std:c++17 /EHs-c- /D_HAS_EXCEPTIONS=0 /arch:AVX2 /I src /Fobuild\"

rc /nologo /fo build\FastPhysics.res src\FastPhysics.rc
if errorlevel 1 exit /b 1

cl %CLFLAGS% src\FastPhysics.cpp src\fastlcp.cpp build\FastPhysics.res /LD /link /OUT:bin\FastPhysics.dll /IMPLIB:build\FastPhysics.lib
if errorlevel 1 exit /b 1

cl %CLFLAGS% tests\SelfTest.cpp src\fastlcp.cpp /link /OUT:build\SelfTest.exe
if errorlevel 1 exit /b 1

echo.
echo Built: bin\FastPhysics.dll
echo Self-test: build\SelfTest.exe
