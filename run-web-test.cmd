@echo off
REM PURPOSE: Run both test suites (core data structures + ops API), the C++ equivalent of "npm test".
REM WHY THIS NAME: paired with run-web.cmd; used by package.json's "test" script.
setlocal
cd /d "%~dp0"
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
where g++ >nul 2>nul
if errorlevel 1 echo g++ was not found. Install MSYS2 and run: pacman -S mingw-w64-ucrt-x86_64-gcc make   (README section 1) & pause & exit /b 1
make test
if errorlevel 1 exit /b 1
make test-ops
if errorlevel 1 exit /b 1
echo All SmartRoute tests passed.
