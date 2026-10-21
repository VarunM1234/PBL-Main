@echo off
REM PURPOSE: Build only (no server): the C++ equivalent of "npm run build". Forwards to the Makefile.
REM WHY THIS NAME: paired with run-web.cmd; used by package.json's "build" script.
setlocal
cd /d "%~dp0"
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
where g++ >nul 2>nul
if errorlevel 1 echo g++ was not found. Install MSYS2 and run: pacman -S mingw-w64-ucrt-x86_64-gcc make   (README section 1) & pause & exit /b 1
make all
if errorlevel 1 echo The build failed. Copy the messages above and send them to me. & pause & exit /b 1
echo Build OK: bin\smartroute.exe
