@echo off
setlocal

echo ===================================================
echo   Supermikrokontroler -- STM32 flash wrapper
echo ===================================================
echo.

set /p PORT="Enter COM port number (e.g. 3 for COM3): "

if "%PORT%"=="" (
    echo ERROR: No port entered.
    pause
    exit /b 1
)

set BIN=stm32_slave\stm32_slave.ino.bin
if not exist "%BIN%" (
    echo ERROR: Firmware binary not found at %BIN%
    echo Compile stm32_slave in Arduino IDE first.
    pause
    exit /b 1
)

echo.
echo Flashing %BIN% to COM%PORT% at 115200 baud...
echo Make sure BOOT0=1 on the Blue Pill before continuing.
echo.
pause

stm32flash.exe -b 115200 -w "%BIN%" -v COM%PORT%

if %ERRORLEVEL% EQU 0 (
    echo.
    echo SUCCESS! Move BOOT0 back to 0 and press RESET on the Blue Pill.
) else (
    echo.
    echo FAILED. Check BOOT0=1, wiring, and that esp32_flasher is running.
)

echo.
pause
endlocal
