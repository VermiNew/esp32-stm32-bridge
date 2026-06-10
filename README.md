# Supermikrokontroler

ESP32 DevKit acts as a **protocol master** that controls an STM32F103C8T6 "Blue Pill"
acting as a **universal hardware agent** over a direct UART link.

---

## Hardware you need

| Item | Notes |
|------|-------|
| ESP32 DevKit v1 | Any board with the standard 38-pin layout |
| STM32F103C8T6 "Blue Pill" | Standard clone is fine |
| 4× Dupont wires (female–female) | |
| stm32flash tool | Windows: .exe from SourceForge; Linux/macOS: package manager |
| USB cable for ESP32 | Micro-USB or USB-C depending on your board |

---

## Wiring

```
ESP32 GPIO17 (TX2)  ──────►  STM32 PA10  (USART1 RX)
ESP32 GPIO16 (RX2)  ◄──────  STM32 PA9   (USART1 TX)
ESP32 3.3V          ──────── STM32 3.3V
ESP32 GND           ──────── STM32 GND
```

> **WARNING — NEVER connect 5V to any STM32 pin. 3.3V only.**

The ESP32 communicates with the STM32 on **UART2 (GPIO16/GPIO17)**,
not on the pins silkscreened "TX"/"RX" — those are GPIO1/GPIO3,
which belong to the USB console.

---

## Arduino IDE setup

### ESP32 board package

1. File → Preferences → "Additional boards manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. Tools → Board → Boards Manager → search **esp32** → install by Espressif Systems.
3. Select board: **ESP32 Dev Module**.

### STM32duino board package

1. Add the following URL to "Additional boards manager URLs":
   ```
   https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json
   ```
2. Boards Manager → search **STM32** → install **STM32 MCU based boards** by STMicroelectronics.
3. Select board: Tools → Board → STM32 MCU based boards → **Generic STM32F1 series**.
4. Select part number: **BluePill F103C8**.

---

## Flashing procedure

### Overview

Because the STM32 Blue Pill has no built-in USB bootloader, you use the ESP32 as a
USB-to-UART bridge. The process involves two separate ESP32 sketches:

1. **esp32_flasher** — transparent bridge, used only during STM32 flashing.
2. **esp32_master** — the actual application firmware, flashed afterward.

### Step 1 — Flash esp32_flasher onto the ESP32

Open `esp32_flasher/esp32_flasher.ino` in Arduino IDE, select your board and port, upload normally.
After upload the onboard LED (GPIO2) blinks rapidly (~150 ms period) — this confirms the bridge is running.

### Step 2 — Set BOOT0 = 1 on the Blue Pill

Locate the small **BOOT0** jumper on the Blue Pill (near the USB connector).
Move it from position **0** to position **1**.
Then press the **RESET** button on the Blue Pill.

> **Tip:** After BOOT0=1 + RESET, the onboard LED (PC13) should **not** blink.
> If it blinks, the application is still running — recheck the BOOT0 jumper.

### Step 3 — Flash stm32_slave

**Option A — PowerShell wizard (recommended on Windows):**

```powershell
.\flash_script.ps1
```

The wizard auto-detects the ESP32 COM port, optionally runs a loopback test,
probes the STM32 bootloader, runs stm32flash, and guides you step-by-step.

**Option B — Batch wrapper (Windows, no wizard):**

```cmd
flash_stm32.bat
```

Enter the COM port number when prompted.

**Option C — Shell wrapper (Linux/macOS):**

```bash
chmod +x flash_stm32.sh
./flash_stm32.sh /dev/ttyUSB0
```

**Option D — Manual:**

```cmd
stm32flash.exe -b 115200 -w stm32_slave\stm32_slave.ino.bin -v COM3
```

Replace `COM3` with your actual port.

### Step 4 — Return to application mode

1. Move the BOOT0 jumper back to position **0**.
2. Press RESET on the Blue Pill.
3. The onboard LED (PC13) should **blink 3 times** to confirm the slave firmware booted.

### Step 5 — Flash esp32_master

Open `esp32_master/esp32_master.ino` in Arduino IDE, select your ESP32 board and port, upload.

### Step 6 — Verify

1. Open the Serial Monitor at **115200 baud**.
2. Set line ending to **Newline** (not CR, not Both).
3. Type `ping` and press Enter.
4. You should see:
   ```
   --> PING
   <-- PONG
   [OK] PONG received — link is alive.
   ```

---

## Command reference

Commands are typed in the Serial Monitor (line ending = Newline, case-insensitive).

| Command | Example | Description |
|---------|---------|-------------|
| `ping` | `ping` | Test the link. Slave replies PONG. |
| `led on` | `led on` | Turn the Blue Pill LED (PC13) on. |
| `led off` | `led off` | Turn the Blue Pill LED off. |
| `led status` | `led status` | Read current LED state (ON/OFF). |
| `blink <ms>` | `blink 500` | Blink LED with given period in ms. `blink 0` stops it. |
| `adc <ch>` | `adc 0` | Read ADC channel (12-bit). ch=0 → PA0, ch=1 → PA1, or use pin token. |
| `pwm <pin> <v>` | `pwm A8 128` | analogWrite to pin (0–255). |
| `gpio set <P> <v>` | `gpio set B5 1` | Set pin P to 0 or 1 (OUTPUT mode). |
| `gpio get <P>` | `gpio get B5` | Read pin P (INPUT mode), returns 0 or 1. |
| `status` | `status` | Slave firmware version and uptime. |
| `reset` | `reset` | Resynchronize the protocol state machine. |
| `help` | `help` | Print this command list. |

**Pin tokens:** `A0`..`A15`, `B0`..`B15`, `C13`..`C15`  
(maps to PA0..PA15, PB0..PB15, PC13..PC15 on the Blue Pill)

---

## Protocol reference

ASCII, line-based. Every frame ends with `\n`. General format: `TYPE:SEQ:DATA\n`

| Frame | Direction | Meaning |
|-------|-----------|---------|
| `SEND:NNN:CMD` | master → slave | Execute command CMD |
| `RECV:NNN` | slave → master | Command received (fast ACK) |
| `DONE:NNN:RESULT` | slave → master | Command finished |
| `BUSY:NNN` | slave → master | Still running (reply to POLL) |
| `POLL:NNN` | master → slave | Are you done with NNN? |
| `FREE:NNN` | master → slave | Slot acknowledged, slave may clear it |
| `ERR:NNN:REASON` | either | Error |
| `PING` | master → slave | Link test |
| `PONG` | slave → master | Link test reply |
| `RESET` | either | Resynchronize state |

---

## Troubleshooting

**ESP32 resets when I open the COM port**  
This is normal — the CH340/CP210 DTR line triggers ESP32 EN. The flash wizard
disables DTR/RTS before opening the port. If your terminal does it — just wait
~3 seconds for the ESP32 to reboot, then try again.

**stm32flash says "Failed to init device"**  
Hold RESET on the Blue Pill, start the flash command, release RESET ~3 seconds
later. The bootloader must be fresh when the sync byte arrives.

**stm32flash prints "Warning: the interface was not closed properly"**  
Harmless on Windows. The firmware was flashed successfully if the exit code is 0.

**Bootloader probe returns 0x00 (all zeros)**  
STM32 RX is held LOW — the STM32 is not transmitting. Check: BOOT0=1, power on the Blue Pill,
correct wiring (PA9/PA10 ↔ GPIO16/GPIO17).

**"led on" does nothing**  
The Blue Pill PC13 LED is **active-low**: LOW = on, HIGH = off. The firmware
handles this correctly. If the LED still doesn't respond, check the UART connection.

**Serial Monitor shows garbage**  
Line ending must be **Newline**. CR or "Both" will break command parsing.

**"Still waiting for previous command"**  
The previous command did not complete. Type `reset` to resynchronize.

**PWM command has no effect**  
Not all Blue Pill pins support PWM in hardware. Known working PWM-capable pins:
PA0, PA1, PA2, PA3, PA6, PA7, PA8, PA9, PA10, PB0, PB1, PB6, PB7, PB8, PB9.

---

## arduino-cli (optional)

```bash
# Compile STM32 slave
arduino-cli compile --fqbn STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8 stm32_slave

# Compile ESP32 sketches
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_master
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_flasher
```
