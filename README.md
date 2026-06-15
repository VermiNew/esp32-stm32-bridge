# Supermikrokontroler v2

ESP32 DevKit as **protocol master** controls an STM32F103C8T6 "Blue Pill"
as a **universal hardware agent** over a direct UART link.

> Full documentation: **[DOCUMENTATION.md](DOCUMENTATION.md)** (English) · **[INSTRUKCJA.md](INSTRUKCJA.md)** (Polski)

---

## Hardware

| Part | Notes |
|------|-------|
| ESP32 DevKit v1 | Standard 38-pin layout |
| STM32F103C8T6 "Blue Pill" | Clone is fine |
| 4× Dupont wire (F-F) | Keep under 20 cm |
| `stm32flash` | Windows: `scripts\get-stm32flash.ps1` · Linux/macOS: `scripts/get-stm32flash.sh` |

---

## Wiring

```
ESP32 GPIO17 (TX2) ──────► STM32 PA10 (USART1 RX)
ESP32 GPIO16 (RX2) ◄────── STM32 PA9  (USART1 TX)
ESP32 3.3V         ─────── STM32 3.3V
ESP32 GND          ─────── STM32 GND
```

> **NEVER connect 5 V to any STM32 pin.**  
> GPIO17/GPIO16 are NOT the "TX/RX" pins silkscreened on the ESP32 board
> (those are GPIO1/GPIO3 — reserved for the USB console).

---

## Supported STM32 peripherals

| Peripheral | Pins | Commands |
|-----------|------|---------|
| GPIO | PA0–PA15, PB0–PB15, PC13–PC15 | mode, write, read, toggle, port |
| ADC (12-bit) | PA0–PA7, PB0–PB1, temp, vref | read, avg, mv, multi, stream |
| PWM (0–1000 ‰) | TIM1/2/3/4 capable pins | set, freq, stop, read |
| I2C1 / I2C2 | PB6/PB7 · PB10/PB11 | scan, ping, write, read, wreg, rreg |
| SPI1 | PA5=SCK, PA6=MISO, PA7=MOSI | begin, xfer, write, read, end |
| USART2 / USART3 | PA2/PA3 · PB10/PB11 | cfg, tx, rx, flush, status, close |
| EEPROM (512 B) | flash-emulated | write, read, wrword, rdword, wrhex, rdhex, fill |
| IRQ (up to 8) | any GPIO | attach, detach, poll, list |
| RTC (LSE) | PC14/PC15 (32.768 kHz) | init, get, set, getts, settss, epoch |
| CAN bus | PB8=RX, PB9=TX | begin, tx, txe, rx, filter, status, end |
| System | — | status, uptime, chipid, reset, wdog |
| Compute offload | — | map, crc16, sqrt, constrain, abs |

---

## Quick start

### Step 0 — Get stm32flash

**Windows:**
```powershell
.\scripts\get-stm32flash.ps1
```

**Linux/macOS:**
```bash
chmod +x scripts/get-stm32flash.sh && ./scripts/get-stm32flash.sh
```

### Step 1 — Flash esp32_flasher onto the ESP32

Open `esp32_flasher/esp32_flasher.ino` in Arduino IDE, select **ESP32 Dev Module**,
upload. GPIO2 LED blinks ~150 ms — USB↔UART bridge is active.

### Step 2 — Set BOOT0 = 1 on the Blue Pill

Move the **BOOT0** jumper from `0` to `1`, then press **RESET**.  
PC13 LED must NOT blink — this confirms ROM bootloader mode.

### Step 3 — Compile and export stm32_slave

Open `stm32_slave/stm32_slave.ino`. Board: **Generic STM32F1 series**,
Part Number: **BluePill F103C8**.  
`Sketch → Export Compiled Binary` → copy the `.bin` to `stm32_slave/stm32_slave.ino.bin`.

### Step 4 — Flash stm32_slave

**Windows (wizard with bootloader detection — recommended):**
```powershell
.\scripts\flash.ps1
```

**Windows (quick, no wizard):**
```cmd
scripts\flash_stm32.bat
```

**Linux/macOS:**
```bash
./scripts/flash_stm32.sh /dev/ttyUSB0
```

### Step 5 — Set BOOT0 = 0, run the application

Move BOOT0 back to `0`, press RESET.  
PC13 LED blinks 3 times — slave is running.

### Step 6 — Flash esp32_master onto the ESP32

Open `esp32_master/esp32_master.ino`, board **ESP32 Dev Module**, upload.

### Step 7 — Verify

Serial Monitor: **115200 baud**, line ending **Newline**.

```
> ping
--> PING
<-- PONG
[OK] PONG — link alive.
```

---

## Arduino IDE setup

### ESP32

1. File → Preferences → Additional Boards Manager URLs:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Boards Manager → **esp32** by Espressif → Install
3. Board: **ESP32 Dev Module**

### STM32duino

1. Add to Additional Boards Manager URLs:  
   `https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json`
2. Boards Manager → **STM32 MCU based boards** by STMicroelectronics → Install
3. Board: STM32 MCU based boards → **Generic STM32F1 series**
4. Part Number: **BluePill F103C8**
5. Upload method: **STM32CubeProgrammer (Serial)**

---

## Stability features

| Mechanism | Description |
|-----------|-------------|
| CRC16-CCITT | Every SEND/DONE/ERR frame carries a 4-digit CRC over the DATA field |
| Heartbeat | Master sends `HEARTBEAT` every 5 s; 3 misses → link DEAD |
| Retry | Up to 3 SEND retries on missing RECV (300 ms timeout) |
| Polling | Up to 15 POLL queries when a command takes > 2.5 s (400 ms interval) |
| IWDG watchdog | Slave arms IWDG via `sys wdog en <ms>`; master auto-kicks at T/2 |
| Flow control | RECV ACK before execution; FREE after DONE to release the slot |
| Line overflow | Slave rejects lines > 192 chars and signals `LINE_OVERFLOW` |

---

## Protocol frame format (v2)

```
SEND:NNN:CCCC:DATA\n       master → slave  (CCCC = CRC16 over DATA)
RECV:NNN\n                 slave  → master (fast ACK)
DONE:NNN:CCCC:RESULT\n     slave  → master
ERR:NNN:CCCC:REASON\n      slave  → master
BUSY:NNN\n                 slave  → master (reply to POLL)
POLL:NNN\n                 master → slave
FREE:NNN\n                 master → slave  (slot release after DONE)
PING / PONG
HEARTBEAT / HEARTBEAT:ACK
RESET / RESET:ACK
```

`NNN` = 3-digit sequence (001–999, wraps to 001)  
`CCCC` = 4-digit uppercase hex CRC16-CCITT over DATA/RESULT

---

## License

[MIT](LICENSE)
