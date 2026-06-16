# esp32-stm32-bridge

ESP32 DevKit jako **master protokołu** steruje STM32F103C8T6 "Blue Pill"
jako **universal hardware agent** przez bezpośredni link UART.

> Pełna dokumentacja: **[INSTRUKCJA.md](INSTRUKCJA.md)** · **[Read in English](../README.md)**

---

## Sprzęt

| Element | Uwagi |
|---------|-------|
| ESP32 DevKit v1 | Standardowy layout 38-pin |
| STM32F103C8T6 "Blue Pill" | Klon jest OK |
| 4× przewód Dupont F-F | Maks. 20 cm |
| `stm32flash` | Windows: `scripts\get-stm32flash.ps1` · Linux/macOS: `scripts/get-stm32flash.sh` |

---

## Schemat połączeń

```
ESP32 GPIO17 (TX2) ──────► STM32 PA10 (USART1 RX)
ESP32 GPIO16 (RX2) ◄────── STM32 PA9  (USART1 TX)
ESP32 3.3V         ─────── STM32 3.3V
ESP32 GND          ─────── STM32 GND
```

> **NIGDY nie podłączaj 5V do żadnego pinu STM32.**  
> GPIO17/GPIO16 to NIE są piny „TX/RX" na silkscreenie ESP32
> (te to GPIO1/GPIO3 — zarezerwowane dla konsoli USB).

---

## Obsługiwane peryferium STM32

| Peryferium | Piny | Komendy |
|-----------|------|---------|
| GPIO | PA0–PA15, PB0–PB15, PC13–PC15 | mode, write, read, toggle, port |
| ADC (12-bit) | PA0–PA7, PB0–PB1, temp, vref | read, avg, mv, multi, stream |
| PWM (0–1000 ‰) | Piny TIM1/2/3/4 | set, freq, stop, read |
| I2C1 / I2C2 | PB6/PB7 · PB10/PB11 | scan, ping, write, read, wreg, rreg |
| SPI1 | PA5=SCK, PA6=MISO, PA7=MOSI | begin, xfer, write, read, end |
| USART2 / USART3 | PA2/PA3 · PB10/PB11 | cfg, tx, rx, flush, status, close |
| EEPROM (512 B) | emulacja flash | write, read, wrword, rdword, wrhex, rdhex, fill |
| IRQ (do 8 pinów) | dowolny GPIO | attach, detach, poll, list |
| RTC (LSE) | PC14/PC15 (32.768 kHz) | init, get, set, getts, settss, epoch |
| CAN bus | PB8=RX, PB9=TX | begin, tx, txe, rx, filter, status, end |
| System | — | status, uptime, chipid, reset, wdog |
| Compute offload | — | map, crc16, sqrt, constrain, abs |

---

## Szybki start

### Krok 0 — Pobierz stm32flash

**Windows:**
```powershell
.\scripts\get-stm32flash.ps1
```

**Linux/macOS:**
```bash
chmod +x scripts/get-stm32flash.sh && ./scripts/get-stm32flash.sh
```

### Krok 1 — Wgraj esp32_flasher na ESP32

Otwórz `esp32_flasher/esp32_flasher.ino` w Arduino IDE, wybierz **ESP32 Dev Module**, wgraj.  
LED GPIO2 miga ~150 ms — most USB↔UART aktywny.

### Krok 2 — Ustaw BOOT0 = 1 na Blue Pill

Przesuń jumper **BOOT0** z `0` na `1`, naciśnij **RESET**.  
LED PC13 nie miga — tryb bootloadera ROM.

### Krok 3 — Skompiluj i wyeksportuj stm32_slave

Otwórz `stm32_slave/stm32_slave.ino`. Board: **Generic STM32F1 series**, Part Number: **BluePill F103C8**.  
`Sketch → Export Compiled Binary` → skopiuj `.bin` do `stm32_slave/stm32_slave.ino.bin`.

### Krok 4 — Wgraj stm32_slave

```powershell
# Windows — wizard z detekcją bootloadera (zalecany):
.\scripts\flash.ps1

# Windows — szybki:
scripts\flash_stm32.bat

# Linux/macOS:
./scripts/flash_stm32.sh /dev/ttyUSB0
```

### Krok 5 — BOOT0 = 0, uruchom aplikację

Przesuń BOOT0 z powrotem na `0`, naciśnij RESET.  
LED PC13 miga 3 razy — slave uruchomiony.

### Krok 6 — Wgraj esp32_master na ESP32

Otwórz `esp32_master/esp32_master.ino`, board **ESP32 Dev Module**, wgraj.

### Krok 7 — Weryfikacja

Serial Monitor: **115200 baud**, zakończenie linii **Newline**.

```
> ping
--> PING
<-- PONG
[OK] PONG — link alive.
```

---

## Konfiguracja Arduino IDE

### ESP32

1. File → Preferences → Additional Boards Manager URLs:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Boards Manager → **esp32** by Espressif → Install
3. Board: **ESP32 Dev Module**

### STM32duino

1. Dodaj do Additional Boards Manager URLs:  
   `https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json`
2. Boards Manager → **STM32 MCU based boards** by STMicroelectronics → Install
3. Board: STM32 MCU based boards → **Generic STM32F1 series**
4. Part Number: **BluePill F103C8**
5. Upload method: **STM32CubeProgrammer (Serial)**

---

## Mechanizmy stabilności

| Mechanizm | Opis |
|-----------|------|
| CRC16-CCITT | Każda ramka SEND/DONE/ERR niesie 4-cyfrowy CRC nad polem DATA |
| Heartbeat | Master wysyła `HEARTBEAT` co 5 s; 3 brak odpowiedzi → link DEAD |
| Retry | Do 3 powtórzeń SEND przy braku RECV (timeout 300 ms) |
| Polling | Do 15 zapytań POLL gdy komenda trwa > 2.5 s (co 400 ms) |
| IWDG watchdog | Slave zbroi IWDG przez `sys wdog en <ms>`; master auto-kickuje co T/2 |
| Flow control | RECV ACK przed wykonaniem; FREE po DONE dla zwolnienia slotu |
| Line overflow | Slave odrzuca linie > 192 znaków i sygnalizuje `LINE_OVERFLOW` |

---

## Format protokołu v2

```
SEND:NNN:CCCC:DATA\n       master → slave  (CCCC = CRC16 nad DATA)
RECV:NNN\n                 slave  → master (szybkie ACK)
DONE:NNN:CCCC:RESULT\n     slave  → master
ERR:NNN:CCCC:REASON\n      slave  → master
BUSY:NNN\n                 slave  → master (odpowiedź na POLL)
POLL:NNN\n                 master → slave
FREE:NNN\n                 master → slave  (zwolnienie slotu po DONE)
PING / PONG
HEARTBEAT / HEARTBEAT:ACK
RESET / RESET:ACK
```

`NNN` = 3-cyfrowy numer sekwencji (001–999, zawija do 001)  
`CCCC` = 4-cyfrowy uppercase hex CRC16-CCITT nad DATA/RESULT

---

## Licencja

[MIT](LICENSE)
