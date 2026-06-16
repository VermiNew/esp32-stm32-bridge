# esp32-stm32-bridge — Full Documentation

> ESP32 DevKit as **master** controls an STM32F103C8T6 "Blue Pill" as a
> **universal hardware agent** over a direct UART link with ASCII + CRC16 protocol.

**[Czytaj po polsku](INSTRUKCJA.md)**

---

## Table of Contents

1. [System Architecture](#1-system-architecture)
2. [Hardware Requirements](#2-hardware-requirements)
3. [Flashing Procedure](#3-flashing-procedure)
4. [Communication Protocol v2](#4-communication-protocol-v2)
5. [Slave — Full Command Reference](#5-slave--full-command-reference)
6. [Master — CLI (Serial Monitor)](#6-master--cli-serial-monitor)
7. [Master — Programmatic API (C++)](#7-master--programmatic-api-c)
8. [WiFi and NTP→RTC Sync](#8-wifi-and-ntprtc-sync)
9. [CAN Bus](#9-can-bus)
10. [Pin Conflict Table](#10-pin-conflict-table)
11. [Test Harness](#11-test-harness)
12. [Troubleshooting](#12-troubleshooting)
13. [File Architecture](#13-file-architecture)

---

## 1. System Architecture

```
  ┌─────────────────────────────────┐      UART 115200 8N1      ┌────────────────────────────┐
  │  ESP32 DevKit v1                │ ◄────────────────────────► │  STM32F103C8T6  Blue Pill  │
  │                                 │   GPIO17(TX)→PA10(RX)      │                            │
  │  esp32_master.ino               │   GPIO16(RX)←PA9(TX)       │  stm32_slave.ino           │
  │  ├── protocol.h  (CRC16)        │                            │  ├── protocol.h (CRC16)    │
  │  ├── parser.h    (CLI)          │                            │  ├── cmd_gpio.h            │
  │  ├── wifi_ntp.h  (NTP sync)     │                            │  ├── cmd_adc.h             │
  │  └── stm32_api.h (C++ API)      │                            │  ├── cmd_pwm.h             │
  │                                 │                            │  ├── cmd_i2c.h             │
  │  Available peripherals:         │                            │  ├── cmd_spi.h             │
  │  • WiFi 802.11 b/g/n            │                            │  ├── cmd_u2.h / cmd_u3.h  │
  │  • Bluetooth 4.2 + BLE          │                            │  ├── cmd_ee.h  (flash)     │
  │  • 34 GPIO, 18-ch ADC           │                            │  ├── cmd_irq.h             │
  │  • DAC, touch, hall sensor      │                            │  ├── cmd_can.h (bxCAN)     │
  │  • Dual core 240 MHz            │                            │  ├── cmd_rtc.h (LSE)       │
  │  • 520 KB SRAM                  │                            │  ├── cmd_sys.h             │
  └─────────────────────────────────┘                            │  └── cmd_misc.h (CALC)     │
                │                                                │                            │
         USB Serial                                              │  72 MHz ARM Cortex-M3      │
         (console/CLI)                                           │  64 KB flash, 20 KB RAM    │
                │                                                └────────────────────────────┘
           PC / Host
```

### Stability mechanisms

| Mechanism | Details |
|-----------|---------|
| CRC16-CCITT | Every SEND/DONE/ERR frame carries a 4-digit CRC over the DATA field |
| Retry | Up to 3 SEND retries on missing RECV (300 ms timeout) |
| Polling | Up to 15 POLL queries when a command takes > 2.5 s (400 ms interval) |
| Heartbeat | Master sends HEARTBEAT every 5 s; 3 misses → DISCONNECTED |
| IWDG watchdog | Slave arms IWDG; master auto-kicks at T/2 |
| Auto-kick | Slave kicks watchdog on every valid received frame |
| FREE/slot | Master sends FREE after every DONE; slave releases slot memory |
| Line overflow | Slave rejects lines > 192 chars and signals LINE_OVERFLOW |

---

## 2. Hardware Requirements

### Minimum

| Part | Notes |
|------|-------|
| ESP32 DevKit v1 | Standard 38-pin layout, CH340/CP210 USB-UART |
| STM32F103C8T6 Blue Pill | Clone OK; verify 64 KB flash (not 128 KB) |
| 4× Dupont wire F-F | Max 20 cm for stability |
| stm32flash | `scripts\get-stm32flash.ps1` / `scripts/get-stm32flash.sh` |

### Optional (extensions)

| Part | Purpose | Pins |
|------|---------|------|
| 32.768 kHz crystal | Precise RTC | PC14/PC15 (often already on the board) |
| CR2032 + 100 Ω | RTC backup across reset | VBAT pin on Blue Pill |
| TJA1050 or SN65HVD230 | CAN bus in normal mode | PB8, PB9 |
| I2C device | i2c scan/read/write | PB6 (SCL), PB7 (SDA) |
| SPI device | spi xfer | PA5/PA6/PA7 + any CS GPIO |

### Wiring

```
ESP32               STM32 Blue Pill
─────               ───────────────
GPIO17 (TX2) ──────► PA10 (USART1 RX)
GPIO16 (RX2) ◄────── PA9  (USART1 TX)
3.3V         ──────── 3.3V
GND          ──────── GND

NEVER connect 5 V to any STM32 pin.
GPIO17/GPIO16 are NOT the "TX/RX" pins on the ESP32 silkscreen
(those are GPIO1/GPIO3 — reserved for the USB console).
```

---

## 3. Flashing Procedure

### Step 0 — Get stm32flash

**Windows:**
```powershell
.\scripts\get-stm32flash.ps1
```
Downloads ZIP from SourceForge, verifies MD5 against the official MD5SUMS file,
extracts the .exe, and checks the MZ header.

**Linux/macOS:**
```bash
chmod +x scripts/get-stm32flash.sh && ./scripts/get-stm32flash.sh
```

### Step 1 — Flash esp32_flasher onto the ESP32

Open `esp32_flasher/esp32_flasher.ino` in Arduino IDE.  
Board: **ESP32 Dev Module** → Upload.  
GPIO2 LED blinks ~150 ms → USB↔UART bridge active (**8E1** mode for the STM32 bootloader).

### Step 2 — Compile and export stm32_slave

Open `stm32_slave/stm32_slave.ino`.  
Board: **Generic STM32F1 series**, Part Number: **BluePill F103C8**.  
`Sketch → Export Compiled Binary` → copy the `.bin` to `stm32_slave/stm32_slave.ino.bin`.

### Step 3 — Set BOOT0 = 1 on the Blue Pill

Move the **BOOT0** jumper from `0` to `1`.  
Press **RESET** on the Blue Pill.  
PC13 LED must **NOT blink** → ROM bootloader mode confirmed.

### Step 4 — Flash stm32_slave

```powershell
# Windows — wizard with bootloader detection (recommended):
.\scripts\flash.ps1

# Windows — quick, no wizard:
scripts\flash_stm32.bat

# Linux/macOS:
./scripts/flash_stm32.sh /dev/ttyUSB0

# Manual:
stm32flash.exe -b 115200 -w stm32_slave\stm32_slave.ino.bin -v COM3
```

### Step 5 — Set BOOT0 = 0, run the application

1. Move BOOT0 back to `0`.
2. Press RESET on the Blue Pill.
3. PC13 LED **blinks 3 times** → slave is running.

### Step 6 — Flash esp32_master

Open `esp32_master/esp32_master.ino`.  
Board: **ESP32 Dev Module** → Upload.

### Step 7 — Verify

Serial Monitor: **115200 baud**, line ending **Newline**.

```
> ping
--> PING
<-- PONG
[OK] PONG — link alive.
```

---

## 4. Communication Protocol v2

### Frame format

```
Master → Slave:   SEND:NNN:CCCC:DATA\n
Slave  → Master:  RECV:NNN\n
Slave  → Master:  DONE:NNN:CCCC:RESULT\n
Slave  → Master:  ERR:NNN:CCCC:REASON\n
Slave  → Master:  BUSY:NNN\n
Master → Slave:   POLL:NNN\n
Master → Slave:   FREE:NNN\n
Both:             PING / PONG
Both:             HEARTBEAT / HEARTBEAT:ACK
Both:             RESET / RESET:ACK
```

| Field | Format | Description |
|-------|--------|-------------|
| `NNN` | `001`–`999` | Sequence number, wraps to 001 |
| `CCCC` | 4-digit uppercase hex | CRC16-CCITT over DATA/RESULT field |
| `DATA` | ASCII, no `\n` | Command and arguments |
| `RESULT` | ASCII, no `\n` | Command result |

Frames **without CRC** (no payload): `RECV`, `BUSY`, `FREE`, `POLL`.  
**Bare** frames (no SEQ): `PING`, `PONG`, `HEARTBEAT`, `RESET`.

### CRC16-CCITT

```
poly = 0x1021,  init = 0xFFFF
CRC is computed only over the DATA field (everything after the third colon)
```

### Master timings

| Parameter | Value |
|-----------|-------|
| TIMEOUT_RECV_MS | 300 ms |
| TIMEOUT_DONE_MS | 2500 ms |
| TIMEOUT_POLL_MS | 400 ms |
| MAX_RETRY | 3 |
| MAX_POLLS | 15 |
| HEARTBEAT_INTERVAL_MS | 5000 ms |
| HEARTBEAT_TIMEOUT_MS | 1500 ms |
| HB_MAX_MISS | 3 |

### Master state machine

```
IDLE ──SEND──► WAIT_RECV ──RECV──► WAIT_DONE ──DONE──► IDLE
                  │                    │                  ▲
               timeout×3            timeout               │
                  ▼                    ▼                   │
                IDLE              POLLING ──DONE──────────┘
              (ERR log)              │
                                MAX_POLLS → IDLE (ERR log)
```

---

## 5. Slave — Full Command Reference

Syntax: `SEND:NNN:CCCC:CMD:ARG1:ARG2...`  
Pin tokens: `A0`–`A15`, `B0`–`B15`, `C13`–`C15`  
I2C address: decimal or `0x`-hex (e.g. `104` or `0x68`)

### GPIO

| DATA command | Description | Result |
|-------------|-------------|--------|
| `GPIO:MODE:PIN:M` | Set pinMode. M = `IN`/`OUT`/`PU`/`PD`/`AN`/`OD` | `GPIO:MODE:A0:IN` |
| `GPIO:WRITE:PIN:V` | digitalWrite. V = 0/1 | `0` or `1` |
| `GPIO:READ:PIN` | digitalRead | `0` or `1` |
| `GPIO:TOGGLE:PIN` | Toggle output | new state `0`/`1` |
| `GPIO:PORT:X` | Read lower 8 bits of port X = A/B/C | `0x3F` |

### ADC (12-bit, 0–4095)

| DATA command | Description | Result |
|-------------|-------------|--------|
| `ADC:READ:PIN` | Single read | `2048` |
| `ADC:AVG:PIN:N` | Average N samples (1–64) | `2050` |
| `ADC:MV:PIN` | Read in millivolts (assumes 3.3 V) | `1650` |
| `ADC:MULTI:P1,P2,...` | Multiple pins at once (max 8) | `2048,1024,3200` |
| `ADC:STREAM:PIN:N:MS` | Burst N samples every MS ms, max 48 | hex stream `0FFF0ABC...` |
| `ADC:TEMP` | Internal temperature sensor | `254` (= 25.4 °C) |
| `ADC:VREF` | Estimated VDDA in mV | `3285` |

### PWM (duty in per-mille 0–1000)

| DATA command | Description | Result |
|-------------|-------------|--------|
| `PWM:SET:PIN:DUTY` | analogWrite, default frequency | `PWM:A8:500` |
| `PWM:FREQ:PIN:HZ:DUTY` | Custom frequency + duty | `PWM:A8:1000Hz:500‰` |
| `PWM:STOP:PIN` | Stop PWM | `STOPPED:A8` |
| `PWM:READ:PIN` | Last set duty | `500` |

**PWM-capable pins:** PA0–PA3, PA6–PA11, PB0–PB1, PB6–PB9

### I2C

Default I2C1 pins: PB6=SCL, PB7=SDA.  
I2C2: PB10=SCL, PB11=SDA (⚠ shared with USART3).

| DATA command | Description | Result |
|-------------|-------------|--------|
| `I2C:CFG:SPEED` | Set bus speed: 100 or 400 kHz | `I2C1:SPEED:400kHz` |
| `I2C:SCAN` | Scan 0x08–0x77 | `0x3C,0x68` or `NONE` |
| `I2C:PING:ADDR` | Check device presence | `ACK` or `NAK` |
| `I2C:WRITE:ADDR:HEX` | Send bytes | `OK:2B` |
| `I2C:READ:ADDR:N` | Read N bytes (max 32) | `FF0102...` |
| `I2C:WREG:ADDR:REG:HEX` | Write register | `OK:1B` |
| `I2C:RREG:ADDR:REG:N` | Read register (repeated start) | hex string |
| `I2C2:...` | Same as above, but I2C2 bus | — |

### SPI

Default pins: PA5=SCK, PA6=MISO, PA7=MOSI. CS = any GPIO.

| DATA command | Description | Result |
|-------------|-------------|--------|
| `SPI:BEGIN:CS:MODE:FREQ_KHZ` | Init (mode 0–3, freq 1–36000 kHz) | `SPI_OK:MODE0:5000kHz` |
| `SPI:XFER:CS:HEX` | Full-duplex transfer | received bytes hex |
| `SPI:WRITE:CS:HEX` | TX only (discard MISO) | `OK:4B` |
| `SPI:READ:CS:N` | RX only (sends 0xFF) | hex string |
| `SPI:END` | Release SPI | `SPI_OFF` |

### USART2 / USART3

USART2: PA2=TX, PA3=RX.  
USART3: PB10=TX, PB11=RX (⚠ shared with I2C2).

| DATA command | Description | Result |
|-------------|-------------|--------|
| `U2:CFG:BAUD[:BITS:PAR:STOP]` | Configure (e.g. `U2:CFG:9600:8:N:1`) | `OK:9600_8N1` |
| `U2:TX:HEX` | Send bytes | `TX:4B` |
| `U2:RX:N[:TIMEOUT_MS]` | Receive up to N bytes | hex or `NONE` |
| `U2:FLUSH` | Clear RX buffer | `OK` |
| `U2:STATUS` | Bytes available in RX | `12` |
| `U2:CLOSE` | Close port | `OK` |
| `U3:...` | Same as above, USART3 | — |

### EEPROM (512 bytes, flash emulation)

⚠ STM32F103 flash has ~10,000 erase cycles. Do not write faster than every 100 ms.

| DATA command | Description | Result |
|-------------|-------------|--------|
| `EE:WRITE:ADDR:BYTE` | Write byte (0–255) | `OK` |
| `EE:READ:ADDR` | Read byte | `171` |
| `EE:WRWORD:ADDR:UINT32` | Write 32-bit little-endian | `OK` |
| `EE:RDWORD:ADDR` | Read 32-bit | `12345678` |
| `EE:WRHEX:ADDR:HEX` | Write arbitrary bytes | `OK:4B` |
| `EE:RDHEX:ADDR:N` | Read N bytes | `DEADBEEF` |
| `EE:FILL:BYTE` | Fill all 512 B | `OK` |
| `EE:SIZE` | Virtual EEPROM size | `512` |

### IRQ (up to 8 pins simultaneously)

| DATA command | Description | Result |
|-------------|-------------|--------|
| `IRQ:ATTACH:PIN:MODE` | Attach interrupt. MODE = `RISE`/`FALL`/`CHANGE` | `ATTACHED:A0:RISE` |
| `IRQ:DETACH:PIN` or `IRQ:DETACH:ALL` | Detach | `DETACHED:A0` |
| `IRQ:POLL` | Get and clear event counters | `A0:3,B5:1` or `NONE` |
| `IRQ:LIST` | List attached pins and modes | `A0:RISE,B5:CHANGE` |

### RTC (LSE crystal 32.768 kHz on PC14/PC15)

| DATA command | Description | Result |
|-------------|-------------|--------|
| `RTC:INIT` | Initialize LSE oscillator (wait up to 2 s) | `RTC:LSE_OK` |
| `RTC:STATUS` | Is it running? | `RUNNING:LSE:OK` |
| `RTC:GET` | Current date and time | `2025-06-15 14:30:05 Sun` |
| `RTC:SET:YYYY:MM:DD:HH:MM:SS` | Set time | `SET:2025-06-15 14:30:00` |
| `RTC:GETTS` | Seconds since 2000-01-01 | `803649005` |
| `RTC:SETTSS:N` | Set from epoch-2000 timestamp | `SET:2025-06-15 14:30:00` |
| `RTC:EPOCH` | Unix timestamp (since 1970-01-01) | `1750333405` |

### CAN bus (bxCAN, PB8=RX, PB9=TX)

| DATA command | Description | Result |
|-------------|-------------|--------|
| `CAN:BEGIN:SPEED:MODE` | Init. SPEED=125/250/500/1000 kbps.<br>MODE=`NORMAL`/`LOOPBACK`/`SILENT` | `CAN:OK:250kbps:LOOPBACK` |
| `CAN:NOWARN` | Suppress transceiver warning | `CAN:WARN_SUPPRESSED` |
| `CAN:WARN` | Restore warning | `CAN:WARN_RESTORED` |
| `CAN:TX:ID:HEX` | Send standard frame (11-bit ID, max 8 B) | `TX:291:4B` |
| `CAN:TXE:ID:HEX` | Send extended frame (29-bit ID) | `TX:...` |
| `CAN:RX` | Receive next frame from buffer | `291:DEADBEEF:0:0` or `NONE` |
| `CAN:FILTER:ID:MASK` | Set acceptance filter | `FILTER:0x123:0x7FF` |
| `CAN:FILTER:OFF` | Disable filter (accept all) | `FILTER:OFF` |
| `CAN:STATUS` | Bus state, error counters | `STATE:3:ERR:0x0000:TEC:0:REC:0` |
| `CAN:END` | Deinit CAN | `CAN:OFF` |

RX result format: `ID:HEX_DATA:EXT:RTR`  
EXT=0 → standard 11-bit, EXT=1 → extended 29-bit  
RTR=0 → data frame, RTR=1 → remote frame

### System

| DATA command | Description | Result |
|-------------|-------------|--------|
| `SYS:STATUS` | Version, uptime, RAM, reset cause | `v2.0:UP:12345:RAM:8192:...` |
| `SYS:UPTIME` | ms since boot | `12345` |
| `SYS:CHIPID` | 96-bit unique device ID | `A1B2C3...` (24 hex) |
| `SYS:CPUFREQ` | CPU frequency | `72MHz` |
| `SYS:FWVER` | Firmware version | `2.0` |
| `SYS:FREERAM` | Estimated free RAM | `8192` |
| `SYS:ECHO:TEXT` | Round-trip test | `TEXT` |
| `SYS:RESET` | Soft reset (NVIC_SystemReset) | `RESETTING` |
| `SYS:WDOG:EN:MS` | Arm IWDG (100–26000 ms) | `WDOG_ON:5000ms` |
| `SYS:WDOG:KICK` | Manual kick | `KICKED` |
| `SYS:WDOG:DIS` | Disable forwarding (IWDG HW cannot be stopped) | `NOTE:...` |

### Compute offload (CALC)

| DATA command | Description | Result |
|-------------|-------------|--------|
| `CALC:MAP:V:IN_MIN:IN_MAX:OUT_MIN:OUT_MAX` | Arduino `map()` | `50` |
| `CALC:CRC16:HEX` | CRC16-CCITT of data | `29B1` |
| `CALC:SQRT:N` | Integer square root | `12` |
| `CALC:CONSTRAIN:V:LO:HI` | `constrain()` | `100` |
| `CALC:ABS:V` | `abs()` | `42` |

---

## 6. Master — CLI (Serial Monitor)

Settings: **115200 baud**, line ending **Newline**.  
Commands are case-insensitive. Type `help` after connecting.

```
# Link
ping
reset
hb                              manual heartbeat test

# GPIO
gpio mode  <pin> in|out|pu|pd|an|od
gpio write <pin> 0|1
gpio read  <pin>
gpio toggle <pin>
gpio port  A|B|C

# ADC
adc read  <pin>
adc avg   <pin> <n>
adc mv    <pin>
adc multi A0,A1,B0
adc stream <pin> <n> <interval_ms>
adc temp
adc vref

# PWM
pwm set  <pin> <duty 0-1000>
pwm freq <pin> <hz> <duty>
pwm stop <pin>
pwm read <pin>

# I2C1 (PB6/PB7)
i2c cfg 100|400
i2c scan
i2c ping  <addr>
i2c write <addr> <hexbytes>
i2c read  <addr> <n>
i2c wreg  <addr> <reg> <hexbytes>
i2c rreg  <addr> <reg> <n>

# I2C2 (PB10/PB11, do not use with u3)
i2c2 cfg|scan|ping|write|read|wreg|rreg  (same syntax as i2c)

# SPI
spi begin <cs_pin> <mode 0-3> <freq_khz>
spi xfer  <cs_pin> <hexbytes>
spi write <cs_pin> <hexbytes>
spi read  <cs_pin> <n>
spi end

# USART2 (PA2/PA3)
u2 cfg <baud> [bits parity stop]
u2 tx <hexbytes>
u2 rx <n> [timeout_ms]
u2 flush | u2 status | u2 close

# USART3 (PB10/PB11, do not use with i2c2)
u3 cfg|tx|rx|flush|status|close  (same syntax as u2)

# EEPROM
ee write  <addr> <byte>
ee read   <addr>
ee wrword <addr> <uint32>
ee rdword <addr>
ee wrhex  <addr> <hexbytes>
ee rdhex  <addr> <n>
ee fill   <byte>
ee size

# IRQ
irq attach <pin> rise|fall|change
irq detach <pin>|all
irq poll
irq list

# RTC
rtc init
rtc status
rtc get
rtc set YYYY-MM-DD HH:MM:SS
rtc getts
rtc settss <seconds_since_2000>
rtc epoch

# CAN
can begin 125|250|500|1000 [loopback|silent|normal]
can nowarn | can warn
can tx  <id> <hexbytes>
can txe <id> <hexbytes>
can rx
can filter <id> <mask> | can filter off
can status | can end

# WiFi
wifi connect <ssid> <password>
wifi status | wifi disconnect | wifi scan

# NTP
ntp sync
ntp status
ntp server <hostname>

# System
sys status | sys uptime | sys chipid | sys cpufreq
sys fwver  | sys freeram | sys reset
sys echo <text>
sys wdog en <ms> | sys wdog kick | sys wdog dis

# Compute
calc map <v> <in_min> <in_max> <out_min> <out_max>
calc crc16 <hexbytes>
calc sqrt <n>
calc constrain <v> <lo> <hi>
calc abs <v>
```

---

## 7. Master — Programmatic API (C++)

File: `esp32_master/stm32_api.h`

Lets you control the STM32 from ESP32 code without typing commands manually.
All methods are **blocking** — they wait for the slave response (max 6 s).

### Initialization

```cpp
#include "stm32_api.h"

STM32 stm;

void setup() {
    if (!stm.begin(10000)) {       // wait up to 10 s for slave
        Serial.println("Slave not responding!");
        while(1) {}
    }
}

void loop() {
    stm.pump();                    // mandatory — drives UART + heartbeat
}
```

### GPIO

```cpp
stm.gpio.mode("A0", GPIO_ANALOG);
stm.gpio.write("C13", 0);         // LOW — LED on (active-low)
int v    = stm.gpio.read("A1");
int v2   = stm.gpio.toggle("C13");
int byte = stm.gpio.port('A');
```

### ADC

```cpp
int raw  = stm.adc.read("A0");
int avg  = stm.adc.avg("A0", 16);
int mv   = stm.adc.mv("A0");

int vals[4];
int n = stm.adc.multi("A0,A1,B0,B1", vals, 4);

String hex = stm.adc.stream("A0", 32, 1); // 32 samples every 1 ms

int tempTenths = stm.adc.temperature();   // e.g. 254 = 25.4 °C
int vdda_mv    = stm.adc.vref();
```

### PWM

```cpp
stm.pwm.set("A8", 500);           // 50% duty
stm.pwm.freq("A8", 1000, 750);    // 1 kHz, 75%
stm.pwm.stop("A8");
int duty = stm.pwm.read("A8");
```

### I2C

```cpp
stm.i2c.setSpeed(400);
String addrs = stm.i2c.scan();
bool found   = stm.i2c.ping(0x68);
stm.i2c.writeReg(0x68, 0x6B, "00");
String data  = stm.i2c.readReg(0x68, 0x3B, 6);
stm.i2c2.ping(0x20);              // I2C2
```

### SPI

```cpp
stm.spi.begin("B12", 0, 5000);
String rx = stm.spi.xfer("B12", "FF00");
stm.spi.end();
```

### EEPROM

```cpp
stm.ee.write(0, 0xAB);
int val = stm.ee.read(0);
stm.ee.writeWord(4, 0xDEADBEEF);
String hex = stm.ee.readHex(16, 4);
```

### IRQ

```cpp
stm.irq.attach("A0", "RISE");

// In loop():
String events = stm.irq.poll();   // "A0:5" or "NONE"
stm.irq.detach("A0");
```

### RTC

```cpp
stm.rtc.init();
stm.rtc.set(2025, 6, 15, 14, 30, 0);
Serial.println(stm.rtc.get());
uint32_t ep = stm.rtc.epoch();
```

### CAN

```cpp
stm.can.setNoWarn(true);
stm.can.begin(250, "LOOPBACK");
stm.can.tx(0x123, "DEADBEEF");
String frame = stm.can.rx();      // "291:DEADBEEF:0:0" or "NONE"
stm.can.end();
```

### System

```cpp
Serial.println(stm.sys.status());
long uptimeMs = stm.sys.uptime();
stm.sys.wdogEnable(10000);        // IWDG 10 s; master auto-kicks every 5 s
```

### Error handling

```cpp
int val = stm.adc.read("A0");

if (!stm.ok()) {
    Serial.println("Error: " + stm.error());
}

// Escape hatch — any command without a wrapper:
String r = stm.execute("SYS:ECHO:hello");
```

### Pattern for long operations (don't block loop)

```cpp
// BAD — blocks loop() for a long time:
for (int i = 0; i < 100; i++) {
    stm.ee.write(i, 0xFF);   // 100 × ~10 ms = 1 s blocked
}

// GOOD — one operation per iteration:
static int eeIdx = 0;
if (eeIdx < 100) {
    stm.ee.write(eeIdx++, 0xFF);
}
```

---

## 8. WiFi and NTP→RTC Sync

### WiFi connection

```
wifi connect MyNetwork MyPassword
wifi status
wifi scan
wifi disconnect
```

After connecting, the ESP32 automatically retries every 30 s if the signal is lost.

### Time synchronization

```
ntp sync
```

Automatic sequence:
1. `configTime(0, 0, "pool.ntp.org")` — ESP32 SNTP (UTC)
2. Waits up to 8 s for the first NTP response
3. Calculates epoch-2000 = unix\_time − 946684800
4. Sends `RTC:INIT` → slave starts LSE
5. Sends `RTC:SETTSS:<epoch2000>` → slave sets the time

```
ntp status              → "Last NTP sync: 2025-06-15 12:00:00 UTC (45 s ago)"
ntp server time.google.com
```

---

## 9. CAN Bus

### Without a transceiver — loopback

```
can begin 250 loopback
can tx 123 DEADBEEF
can rx                    → [OK] 291:DEADBEEF:0:0
can end
```

### With TJA1050 or SN65HVD230 transceiver

```
can nowarn
can begin 250 normal
can tx 123 DEADBEEF
```

TJA1050 wiring:
```
Blue Pill PB9 (TX) → TJA1050 TXD
Blue Pill PB8 (RX) ← TJA1050 RXD
TJA1050 VCC  → 5V (or 3.3V for SN65HVD230)
TJA1050 GND  → GND
TJA1050 RS   → GND (high-speed mode)
TJA1050 CANH → CAN bus +
TJA1050 CANL → CAN bus −
```

### CAN modes

| Mode | Transceiver | TX | RX | Use case |
|------|-------------|----|----|---------|
| `LOOPBACK` | No | internal | internal | Code testing |
| `SILENT` | Yes | No | Yes | Passive sniffer |
| `NORMAL` | Yes | Yes | Yes | Full communication |

---

## 10. Pin Conflict Table

| Pin | Function 1 | Function 2 | Conflict |
|-----|-----------|-----------|----------|
| PA9 | USART1 TX | — | **RESERVED** for master protocol |
| PA10 | USART1 RX | — | **RESERVED** for master protocol |
| PA5 | SPI1 SCK | ADC IN5 | SPI active → ADC unavailable |
| PA6 | SPI1 MISO | ADC IN6 / TIM3 CH1 | SPI active → ADC unavailable |
| PA7 | SPI1 MOSI | ADC IN7 / TIM3 CH2 | SPI active → ADC unavailable |
| PB6 | I2C1 SCL | TIM4 CH1 | I2C ↔ PWM |
| PB7 | I2C1 SDA | TIM4 CH2 | I2C ↔ PWM |
| PB8 | CAN1 RX | TIM4 CH3 | CAN ↔ PWM |
| PB9 | CAN1 TX | TIM4 CH4 | CAN ↔ PWM |
| PB10 | I2C2 SCL | USART3 TX | **I2C2 ↔ U3 mutually exclusive** |
| PB11 | I2C2 SDA | USART3 RX | **I2C2 ↔ U3 mutually exclusive** |
| PC14 | LSE OSC32_IN | GPIO | After `rtc init` → do not use as GPIO |
| PC15 | LSE OSC32_OUT | GPIO | After `rtc init` → do not use as GPIO |

---

## 11. Test Harness

### PowerShell 7 (Windows)

```powershell
# Auto-detect port:
.\scripts\test.ps1

# Specific port:
.\scripts\test.ps1 -Port COM3

# Longer timeout:
.\scripts\test.ps1 -Port COM3 -Timeout 10
```

Coverage: GPIO, ADC, PWM, EEPROM, I2C scan, IRQ, CALC, CAN loopback,
RTC (soft-fail if no crystal), 5× stress PING, reset + re-ping.  
Exit code: 0 = all tests PASS, 1 = at least one FAIL.

### Python (Linux / macOS / Windows)

```bash
pip install pyserial
python scripts/test.py              # auto-detect
python scripts/test.py /dev/ttyUSB0
python scripts/test.py COM3
```

Same test matrix as the PS7 harness.

---

## 12. Troubleshooting

**ESP32 resets when the COM port is opened**  
Normal behavior — the DTR line triggers ESP32 EN. The PS7 wizard (`scripts\flash.ps1`)
disables DTR/RTS automatically. In Arduino IDE Serial Monitor, wait ~3 s after connecting.

**stm32flash: "Failed to init device"**  
Hold RESET on the Blue Pill, run the flash command, release RESET ~3 s after the port opens.
The ROM bootloader must receive the autobaud byte 0x7F right after it starts.

**No PONG after `ping`**  
Check: 1) esp32_master is flashed (not flasher), 2) BOOT0=0 on Blue Pill,
3) GPIO17→PA10, GPIO16←PA9, 4) Serial Monitor line ending is **Newline**.

**`CRC_ERR` in slave log**  
UART noise — check wire length (max 20 cm without shielding), wire quality,
shared GND between ESP32 and STM32.

**`[ERR] Slave: I2C:NO_DATA`**  
I2C device not responding. Check address (`i2c scan`), 3.3 V power,
SDA/SCL pull-ups (4.7 kΩ to 3.3 V).

**`[!!] Heartbeat miss #1/3`**  
Slave is responding slowly (e.g. long I2C scan). Increase `HEARTBEAT_INTERVAL_MS`
in `esp32_master.ino` or shorten slave operations.

**PWM has no effect on a given pin**  
Not every Blue Pill pin supports hardware PWM. Use pins from the "PWM-capable" list
in section 5. An invalid pin will not return an error.

**`ee write` is slow or STM32 resets**  
Every write commits a flash page (~10 ms). Do not write faster than every 100 ms.
After ~10,000 cycles the EEPROM page will stop working correctly.

**CAN: `CAN:INIT_FAIL`**  
Check: 1) AFIO clock is active (it is in the code), 2) PB8/PB9 are free
(not used by TIM4).

**RTC: `RTC:LSE_FAIL` or persistent `NOT_SET`**  
The 32.768 kHz crystal must be physically soldered on PC14/PC15 (small silver component
near the CPU). Without it RTC will not start.

**API: `stm.begin()` returns false**  
Slave not responding within `timeoutMs`. Check the same things as for missing PONG.
Ensure `stm.pump()` is in `loop()` and nothing is blocking it.

---

## 13. File Architecture

```
esp32-stm32-bridge/
│
├── esp32_flasher/
│   └── esp32_flasher.ino       USB↔UART bridge 8E1 (for flashing STM32)
│
├── esp32_master/
│   ├── esp32_master.ino        Main loop, state machine, heartbeat, wdog
│   ├── protocol.h              CRC16, splitTokens, parsePin, hexUtils (copy)
│   ├── parser.h                CLI parser → protocol DATA string
│   ├── wifi_ntp.h              WiFi connect/scan, NTP sync → RTC:SETTSS
│   └── stm32_api.h             ★ Programmatic C++ API (STM32 class)
│
├── stm32_slave/
│   ├── stm32_slave.ino         Setup/loop, frame router, sendDone/sendErr
│   ├── protocol.h              CRC16, splitTokens, parsePin, hexUtils (original)
│   ├── cmd_gpio.h              GPIO: MODE/WRITE/READ/TOGGLE/PORT
│   ├── cmd_adc.h               ADC: READ/AVG/MV/MULTI/STREAM/TEMP/VREF
│   ├── cmd_pwm.h               PWM: SET/FREQ/STOP/READ
│   ├── cmd_i2c.h               I2C1+I2C2: SCAN/PING/WRITE/READ/WREG/RREG/CFG
│   ├── cmd_spi.h               SPI: BEGIN/XFER/WRITE/READ/END
│   ├── cmd_u2.h                USART2: CFG/TX/RX/FLUSH/STATUS/CLOSE
│   ├── cmd_u3.h                USART3: CFG/TX/RX/FLUSH/STATUS/CLOSE
│   ├── cmd_ee.h                EEPROM: WRITE/READ/WRWORD/RDWORD/WRHEX/RDHEX/FILL
│   ├── cmd_irq.h               IRQ: ATTACH/DETACH/POLL/LIST (8 ISR slots)
│   ├── cmd_can.h               CAN: BEGIN/TX/TXE/RX/FILTER/STATUS/END + loopback
│   ├── cmd_rtc.h               RTC: INIT/GET/SET/GETTS/SETTSS/EPOCH (LSE)
│   ├── cmd_sys.h               SYS: STATUS/UPTIME/CHIPID/RESET/WDOG/FREERAM/ECHO
│   └── cmd_misc.h              CALC + legacy LED/BLINK
│
├── scripts/
│   ├── Shared.psm1             Shared PS7 module (colors, i18n, COM detection)
│   ├── flash.ps1               STM32 flash wizard via ESP32 bridge (PS7)
│   ├── test.ps1                Smoke test harness (PS7) — 40+ assertions, auto COM detect
│   ├── test.py                 Smoke test harness (Python) — same test matrix
│   ├── get-stm32flash.ps1      Download + MD5 verify stm32flash (Windows)
│   ├── get-stm32flash.sh       Download + MD5 verify stm32flash (Linux/macOS)
│   ├── get-stm32rtc.ps1        Install STM32RTC library via arduino-cli
│   ├── flash_stm32.bat         Minimal bat wrapper, no wizard (Windows)
│   ├── flash_stm32.sh          Minimal bash wrapper (Linux/macOS)
│   └── lang/                   i18n files: pl.psd1 / en.psd1
│
├── docs/
│   └── example_api_usage.ino   C++ API usage examples (not a standalone sketch)
│
├── .github/workflows/build.yml CI: compile ESP32 master/flasher + STM32 slave
├── README.md                   Quick-start guide (English)
├── DOCUMENTATION.md            Full documentation (English) ← this file
├── INSTRUKCJA.md               Full documentation (Polski)
└── AGENTS.md                   AI agent / contributor guide
```
