@echo off
REM Build script for winmm proxy DLL
REM Run this from x86 Native Tools Command Prompt for VS

echo Building winmm.dll...
cl /LD /O2 /DNDEBUG ^
   winmm.cpp ^
   /link /OUT:winmm.dll gdi32.lib user32.lib

if %errorlevel% equ 0 (
    echo.
    echo Build successful!
    echo Output: winmm.dll
    echo.
    echo Clean up intermediate files...
    del *.obj >nul 2>&1
    del *.exp >nul 2>&1
    del *.lib >nul 2>&1
    echo Done!
) else (
    echo.
    echo Build failed!
)