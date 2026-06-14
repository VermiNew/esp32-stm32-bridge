# scripts/

PowerShell 7 scripts for the Supermikrokontroler project.
All scripts require **PowerShell 7+** and import `Shared.psm1` for shared helpers.

## Scripts

| Script | Description |
|--------|-------------|
| `flash.ps1` | Interactive wizard — detects COM port, verifies bootloader, flashes STM32 |
| `test.ps1` | Smoke-test harness — connects to ESP32, runs full command suite, reports PASS/FAIL |
| `get-stm32flash.ps1` | Downloads `stm32flash.exe` from SourceForge, verifies MD5, extracts to `tools\` |
| `get-stm32rtc.ps1` | Downloads and installs the STM32RTC Arduino library via arduino-cli |
| `flash_stm32.bat` | Minimal Windows CMD wrapper around stm32flash (no wizard, no auto-detect) |
| `flash_stm32.sh` | Minimal Linux/macOS wrapper around stm32flash |
| `Shared.psm1` | Shared module — ANSI colors, COM port detection, prerequisite checks |

## Quick start

```powershell
# 1. Download stm32flash (first time only)
.\scripts\get-stm32flash.ps1

# 2. Install STM32RTC library (first time only)
.\scripts\get-stm32rtc.ps1

# 3. Flash STM32 slave firmware
.\scripts\flash.ps1

# 4. Run smoke tests
.\scripts\test.ps1
.\scripts\test.ps1 -Port COM3 -Timeout 10
```

## Prerequisites

- **PowerShell 7+** — `winget install Microsoft.PowerShell`
- **arduino-cli** — `winget install ArduinoSA.ArduinoCLI`
- **stm32flash.exe** — auto-downloaded by `get-stm32flash.ps1` into `tools\`
- **Compiled firmware** — `stm32_slave\stm32_slave.ino.bin` (build in Arduino IDE)

## Directory layout

```
scripts/        ← all scripts live here
tools/          ← stm32flash.exe (auto-created by get-stm32flash.ps1)
stm32_slave/    ← firmware source + compiled .bin goes here
esp32_master/   ← master firmware source
```
