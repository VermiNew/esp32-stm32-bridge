# scripts/

PowerShell 7 and Python scripts for the Supermikrokontroler project.
All PS1/PSM1 scripts require **PowerShell 7+** and import `Shared.psm1` for shared helpers.

Skrypty PowerShell 7 i Python dla projektu Supermikrokontroler.
Wszystkie skrypty PS1/PSM1 wymagają **PowerShell 7+** i importują `Shared.psm1`.

---

## Scripts / Skrypty

| Script | Description | Opis |
|--------|-------------|------|
| `flash.ps1` | Interactive wizard — detects COM port, verifies bootloader, flashes STM32 | Kreator interaktywny — wykrywa port COM, weryfikuje bootloader, wgrywa STM32 |
| `test.ps1` | Smoke-test harness (PowerShell) — full command suite, reports PASS/FAIL | Test dymny (PowerShell) — pełna seria komend, raport PASS/FAIL |
| `test.py` | Smoke-test harness (Python) — same tests, requires `pip install pyserial` | Test dymny (Python) — te same testy, wymaga `pip install pyserial` |
| `minify.py` | Strip comments from PS1/PSM1/Python scripts → `dist/` | Usuwa komentarze ze skryptów PS1/PSM1/Python → `dist/` |
| `get-stm32flash.ps1` | Downloads `stm32flash.exe`, verifies MD5, extracts to `tools\` (Windows) | Pobiera `stm32flash.exe`, weryfikuje MD5, rozpakowuje do `tools\` (Windows) |
| `get-stm32flash.sh` | Downloads `stm32flash` binary (Linux/macOS) | Pobiera binarkę `stm32flash` (Linux/macOS) |
| `get-stm32rtc.ps1` | Downloads and installs the STM32RTC Arduino library via arduino-cli | Pobiera i instaluje bibliotekę STM32RTC przez arduino-cli |
| `flash_stm32.bat` | Minimal Windows CMD wrapper around stm32flash (no wizard, no auto-detect) | Minimalny wrapper CMD dla stm32flash (bez wizarda, bez auto-detekcji) |
| `flash_stm32.sh` | Minimal Linux/macOS wrapper around stm32flash | Minimalny wrapper bash dla stm32flash (Linux/macOS) |
| `Shared.psm1` | Shared module — ANSI colors, i18n, COM port detection, prerequisite checks | Wspólny moduł — kolory ANSI, i18n, detekcja portu COM, sprawdzanie wymagań |

---

## Quick start / Szybki start

```powershell
# 1. Download stm32flash (first time only) / Pobierz stm32flash (tylko raz)
.\scripts\get-stm32flash.ps1

# 2. Install STM32RTC library (first time only) / Zainstaluj bibliotekę STM32RTC (tylko raz)
.\scripts\get-stm32rtc.ps1

# 3. Flash STM32 slave firmware / Wgraj firmware STM32 slave
.\scripts\flash.ps1

# 4. Run smoke tests / Uruchom testy dymne
.\scripts\test.ps1
.\scripts\test.ps1 -Port COM3 -Timeout 10
```

## Prerequisites / Wymagania

- **PowerShell 7+** — `winget install Microsoft.PowerShell`
- **arduino-cli** — `winget install ArduinoSA.ArduinoCLI`
- **stm32flash.exe** — auto-downloaded by `get-stm32flash.ps1` into `tools\` / auto-pobierany przez `get-stm32flash.ps1` do `tools\`
- **Compiled firmware** — `stm32_slave\stm32_slave.ino.bin` (build in Arduino IDE / skompiluj w Arduino IDE)

---

## Directory layout / Układ katalogów

```
scripts/        ← all scripts / wszystkie skrypty
tools/          ← stm32flash.exe (auto-created / auto-tworzony przez get-stm32flash.ps1)
dist/           ← minified scripts / zminifikowane skrypty (auto-tworzony przez minify.py)
stm32_slave/    ← firmware source + compiled .bin / źródło firmware + skompilowany .bin
esp32_master/   ← master firmware source / źródło firmware mastera
```
