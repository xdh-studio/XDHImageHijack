@echo off
setlocal
cd /d "%~dp0"

:: ±‡“Î◊ ‘¥£®∫¨ manifest£©
windres resource.rc -O coff -o resource.res
if errorlevel 1 (echo windres  ß∞‹ & pause & exit /b 1)

gcc -municode -O2 -mwindows main.c resource.res -o XDHImageHijack.exe ^
    -lshlwapi -ladvapi32 -lole32 -lshell32 -luser32 -lgdi32
if errorlevel 1 (echo ±‡“Î ß∞‹ & pause & exit /b 1)

echo ±‡“Î≥…π¶£∫XDHImageHijack.exe