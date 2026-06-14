@echo off
setlocal

echo ===================================================
echo   Supermikrokontroler -- STM32 flash wrapper
echo ===================================================
echo.

set /p PORT="Enter COM port number (e.g. 3 for COM3): "
if "%PORT%"=="" ( echo ERROR: No port entered. & pause & exit /b 1 )

set ROOT=%~dp0..
set BIN=%ROOT%\stm32_slave\stm32_slave.ino.bin
set EXE=%ROOT%\tools\stm32flash.exe

if not exist "%BIN%" (
    echo ERROR: Firmware binary not found at %BIN%
    echo Compile stm32_slave in Arduino IDE first.
    pause & exit /b 1
)
if not exist "%EXE%" (
    echo ERROR: stm32flash.exe not found at %EXE%
    echo Run scripts\get-stm32flash.ps1 first.
    pause & exit /b 1
)

echo.
echo Flashing %BIN% to COM%PORT% at 115200 baud...
echo Make sure BOOT0=1 on the Blue Pill before continuing.
echo.
pause

"%EXE%" -b 115200 -w "%BIN%" -v COM%PORT%

if %ERRORLEVEL% EQU 0 (
    echo. & echo SUCCESS! Move BOOT0 back to 0 and press RESET.
) else (
    echo. & echo FAILED. Check BOOT0=1, wiring, and esp32_flasher firmware.
)

echo. & pause
endlocal
