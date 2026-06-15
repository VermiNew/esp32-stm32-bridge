# scripts/ — Instrukcja

Skrypty PowerShell 7 i Python dla projektu Supermikrokontroler.
Wszystkie skrypty PS1/PSM1 wymagają **PowerShell 7+** i importują `Shared.psm1`.

---

## Skrypty

| Skrypt | Opis |
|--------|------|
| `flash.ps1` | Kreator interaktywny — wykrywa port COM, weryfikuje bootloader, wgrywa firmware STM32 |
| `test.ps1` | Test dymny (PowerShell) — pełna seria komend, raport PASS/FAIL |
| `test.py` | Test dymny (Python) — te same testy, wymaga `pip install pyserial` |
| `minify.py` | Usuwa komentarze ze skryptów PS1/PSM1/Python → katalog `dist/` |
| `get-stm32flash.ps1` | Pobiera `stm32flash.exe`, weryfikuje MD5, rozpakowuje do `tools\` (Windows) |
| `get-stm32flash.sh` | Pobiera binarkę `stm32flash` (Linux/macOS) |
| `get-stm32rtc.ps1` | Pobiera i instaluje bibliotekę STM32RTC przez arduino-cli |
| `flash_stm32.bat` | Minimalny wrapper CMD dla stm32flash — bez wizarda, bez auto-detekcji (Windows) |
| `flash_stm32.sh` | Minimalny wrapper bash dla stm32flash (Linux/macOS) |
| `Shared.psm1` | Wspólny moduł — kolory ANSI, i18n, detekcja portu COM, sprawdzanie wymagań |

---

## Szybki start

```powershell
# 1. Pobierz stm32flash (tylko przy pierwszym użyciu)
.\scripts\get-stm32flash.ps1

# 2. Zainstaluj bibliotekę STM32RTC (tylko przy pierwszym użyciu)
.\scripts\get-stm32rtc.ps1

# 3. Wgraj firmware STM32 slave
.\scripts\flash.ps1

# 4. Uruchom testy dymne
.\scripts\test.ps1
.\scripts\test.ps1 -Port COM3 -Timeout 10
```

## Wymagania

- **PowerShell 7+** — `winget install Microsoft.PowerShell`
- **arduino-cli** — `winget install ArduinoSA.ArduinoCLI`
- **stm32flash.exe** — pobierany automatycznie przez `get-stm32flash.ps1` do `tools\`
- **Skompilowany firmware** — `stm32_slave\stm32_slave.ino.bin` (skompiluj w Arduino IDE)

---

## Układ katalogów

```
scripts/        ← wszystkie skrypty
tools/          ← stm32flash.exe (auto-tworzony przez get-stm32flash.ps1)
dist/           ← zminifikowane skrypty (auto-tworzony przez minify.py)
stm32_slave/    ← źródło firmware + skompilowany .bin
esp32_master/   ← źródło firmware mastera
```
