@echo off
REM PURPOSE: Windows one-click start: build (if needed), start the SmartRoute web server, open the browser.
REM WHY THIS NAME: "run-web" starts the web UI; .cmd is a Windows command file. Double-click it or run:  run-web.cmd [PORT]
REM CONNECTS TO: Makefile (build), bin\smartroute.exe --serve --open. Needs MSYS2 UCRT64 at C:\msys64 (see README section 1).
setlocal
cd /d "%~dp0"
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"
set "PORT=%~1"
if "%PORT%"=="" set "PORT=8080"
where g++ >nul 2>nul
if errorlevel 1 echo g++ was not found. Install MSYS2 and run: pacman -S mingw-w64-ucrt-x86_64-gcc make   (README section 1) & pause & exit /b 1
where make >nul 2>nul
if errorlevel 1 echo make was not found. Install MSYS2 and run: pacman -S mingw-w64-ucrt-x86_64-gcc make   (README section 1) & pause & exit /b 1
make all
if errorlevel 1 goto buildfail
goto serve

:buildfail
REM The link step fails with "Permission denied" when another SmartRoute window is already running
REM from this same exe. That must not stop the dev flow: fall back to the existing build.
if not exist bin\smartroute.exe echo The build failed and no previous bin\smartroute.exe exists. Copy the messages above and send them to me. & pause & exit /b 1
echo.
echo NOTE: could not rebuild bin\smartroute.exe - another SmartRoute window is using it.
echo Starting the existing build instead. For the freshest code, close the other
echo SmartRoute window (check ports 8080-8143) and run this again.

:serve
echo.
echo Starting SmartRoute on http://localhost:%PORT%
echo Keep this window open while you use the page. Close it (or press Ctrl+C) to stop.
bin\smartroute.exe --serve %PORT% --open
pause
