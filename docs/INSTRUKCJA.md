# esp32-stm32-bridge — Pełna Instrukcja

> ESP32 DevKit jako **master** steruje STM32F103C8T6 "Blue Pill" jako
> **universal hardware agent** przez bezpośredni UART + protokół ASCII z CRC16.

**[Read in English](DOCUMENTATION.md)**

---

## Spis treści

1. [Architektura systemu](#1-architektura-systemu)
2. [Wymagania sprzętowe](#2-wymagania-sprzętowe)
3. [Procedura flashowania](#3-procedura-flashowania)
4. [Protokół komunikacji v2](#4-protokół-komunikacji-v2)
5. [Slave — kompletna lista komend](#5-slave--kompletna-lista-komend)
6. [Master — CLI (Serial Monitor)](#6-master--cli-serial-monitor)
7. [Master — programistyczne API (C++)](#7-master--programistyczne-api-c)
8. [WiFi i synchronizacja NTP→RTC](#8-wifi-i-synchronizacja-ntprtc)
9. [CAN bus](#9-can-bus)
10. [Tabela konfliktów pinów](#10-tabela-konfliktów-pinów)
11. [Test harness](#11-test-harness)
12. [Rozwiązywanie problemów](#12-rozwiązywanie-problemów)
13. [Architektura plików](#13-architektura-plików)

---

## 1. Architektura systemu

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
  │  Dostępne peryferium:           │                            │  ├── cmd_spi.h             │
  │  • WiFi 802.11 b/g/n            │                            │  ├── cmd_u2.h / cmd_u3.h  │
  │  • Bluetooth 4.2 + BLE          │                            │  ├── cmd_ee.h  (flash)     │
  │  • 34 GPIO, 18-kanałowy ADC     │                            │  ├── cmd_irq.h             │
  │  • DAC, touch, hall sensor      │                            │  ├── cmd_can.h (bxCAN)     │
  │  • Dual core 240 MHz            │                            │  ├── cmd_rtc.h (LSE)       │
  │  • 520 KB SRAM                  │                            │  ├── cmd_sys.h             │
  └─────────────────────────────────┘                            │  └── cmd_misc.h (CALC)     │
                │                                                │                            │
         USB Serial                                              │  72 MHz ARM Cortex-M3      │
         (konsola/CLI)                                           │  64 KB flash, 20 KB RAM    │
                │                                                └────────────────────────────┘
           PC / Host
```

### Mechanizmy stabilności

| Mechanizm | Szczegóły |
|-----------|-----------|
| CRC16-CCITT | Każda ramka SEND/DONE/ERR niesie 4-cyfrowy CRC nad polem DATA |
| Retry | Do 3 powtórzeń SEND przy braku RECV (timeout 300 ms) |
| Polling | Do 15 zapytań POLL gdy komenda trwa > 2.5 s (co 400 ms) |
| Heartbeat | Master wysyła HEARTBEAT co 5 s; 3 brak odpowiedzi → DISCONNECTED |
| IWDG watchdog | Slave ustawia IWDG; master auto-kickuje co T/2 |
| Auto-kick | Slave kickuje watchdog przy każdej poprawnej ramce |
| FREE/slot | Master wysyła FREE po każdym DONE; slave zwalnia pamięć slotu |
| Line overflow | Slave odrzuca linie >192 znaków i sygnalizuje LINE_OVERFLOW |

---

## 2. Wymagania sprzętowe

### Minimalne

| Element | Uwagi |
|---------|-------|
| ESP32 DevKit v1 | Standardowy layout 38-pin, CH340/CP210 USB-UART |
| STM32F103C8T6 Blue Pill | Klon OK; weryfikuj flash 64 KB |
| 4× przewód Dupont F-F | Maks. 20 cm dla stabilności |
| stm32flash | `scripts\get-stm32flash.ps1` / `scripts/get-stm32flash.sh` |

### Opcjonalne (rozszerzenia)

| Element | Do czego | Piny |
|---------|----------|------|
| Kryształ 32.768 kHz | RTC precyzyjny | PC14/PC15 (często już na płytce) |
| CR2032 + 100 Ω | RTC backup przez reset | VBAT pin Blue Pill |
| TJA1050 lub SN65HVD230 | CAN bus w trybie normalnym | PB8, PB9 |
| Urządzenie I2C | i2c scan/read/write | PB6 (SCL), PB7 (SDA) |
| Urządzenie SPI | spi xfer | PA5/PA6/PA7 + dowolny CS |

### Schemat połączeń

```
ESP32               STM32 Blue Pill
─────               ───────────────
GPIO17 (TX2) ──────► PA10 (USART1 RX)
GPIO16 (RX2) ◄────── PA9  (USART1 TX)
3.3V         ──────── 3.3V
GND          ──────── GND

NIGDY nie podłączaj 5V do żadnego pinu STM32.
GPIO17/GPIO16 to NIE są piny "TX/RX" na silkscreenie ESP32
(te to GPIO1/GPIO3 — zarezerwowane dla konsoli USB).
```

---

## 3. Procedura flashowania

### Krok 0 — Pobierz stm32flash

**Windows:**
```powershell
.\scripts\get-stm32flash.ps1
```
Skrypt: pobiera ZIP z SourceForge, weryfikuje MD5 z oficjalnego MD5SUMS,
wypakowuje .exe, sprawdza nagłówek MZ.

**Linux/macOS:**
```bash
chmod +x scripts/get-stm32flash.sh && ./scripts/get-stm32flash.sh
```

### Krok 1 — Wgraj esp32_flasher na ESP32

Otwórz `esp32_flasher/esp32_flasher.ino` w Arduino IDE.  
Board: **ESP32 Dev Module** → Upload.  
LED GPIO2 miga ~150 ms → most USB↔UART aktywny (tryb **8E1** dla bootloadera STM32).

### Krok 2 — Skompiluj i wyeksportuj stm32_slave

Otwórz `stm32_slave/stm32_slave.ino`.  
Board: **Generic STM32F1 series**, Part Number: **BluePill F103C8**.  
`Sketch → Export Compiled Binary` → skopiuj `.bin` do `stm32_slave/stm32_slave.ino.bin`.

### Krok 3 — BOOT0 = 1 na Blue Pill

Przesuń jumper **BOOT0** z pozycji `0` na `1`.  
Naciśnij **RESET** na Blue Pill.  
LED PC13 **NIE miga** → tryb bootloadera ROM.

### Krok 4 — Wgraj stm32_slave

```powershell
# Windows — wizard z detekcją bootloadera (zalecany):
.\scripts\flash.ps1

# Windows — szybki:
scripts\flash_stm32.bat

# Linux/macOS:
./scripts/flash_stm32.sh /dev/ttyUSB0

# Ręcznie:
stm32flash.exe -b 115200 -w stm32_slave\stm32_slave.ino.bin -v COM3
```

### Krok 5 — BOOT0 = 0, uruchom aplikację

1. Przesuń BOOT0 z powrotem na `0`.
2. Naciśnij RESET na Blue Pill.
3. LED PC13 **miga 3 razy** → slave uruchomiony.

### Krok 6 — Wgraj esp32_master

Otwórz `esp32_master/esp32_master.ino`.  
Board: **ESP32 Dev Module** → Upload.

### Krok 7 — Weryfikacja

Serial Monitor: **115200 baud**, zakończenie linii **Newline**.

```
> ping
--> PING
<-- PONG
[OK] PONG — link alive.
```

---

## 4. Protokół komunikacji v2

### Format ramki

```
Master → Slave:   SEND:NNN:CCCC:DATA\n
Slave  → Master:  RECV:NNN\n
Slave  → Master:  DONE:NNN:CCCC:RESULT\n
Slave  → Master:  ERR:NNN:CCCC:REASON\n
Slave  → Master:  BUSY:NNN\n
Master → Slave:   POLL:NNN\n
Master → Slave:   FREE:NNN\n
Obustronnie:      PING / PONG
Obustronnie:      HEARTBEAT / HEARTBEAT:ACK
Obustronnie:      RESET / RESET:ACK
```

| Pole | Format | Opis |
|------|--------|------|
| `NNN` | `001`–`999` | Numer sekwencji, zawija do 001 |
| `CCCC` | 4-cyfrowy uppercase hex | CRC16-CCITT nad polem DATA/RESULT |
| `DATA` | ASCII, brak `\n` | Komenda i argumenty |
| `RESULT` | ASCII, brak `\n` | Wynik komendy |

Ramki **bez CRC** (brak ładunku): `RECV`, `BUSY`, `FREE`, `POLL`.  
Ramki **bare** (bez SEQ): `PING`, `PONG`, `HEARTBEAT`, `RESET`.

### CRC16-CCITT

```
poly = 0x1021,  init = 0xFFFF
CRC obliczany wyłącznie nad polem DATA (po trzecim dwukropku)
```

### Timingsy mastera

| Parametr | Wartość |
|----------|---------|
| TIMEOUT_RECV_MS | 300 ms |
| TIMEOUT_DONE_MS | 2500 ms |
| TIMEOUT_POLL_MS | 400 ms |
| MAX_RETRY | 3 |
| MAX_POLLS | 15 |
| HEARTBEAT_INTERVAL_MS | 5000 ms |
| HEARTBEAT_TIMEOUT_MS | 1500 ms |
| HB_MAX_MISS | 3 |

### Maszyna stanów mastera

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

## 5. Slave — kompletna lista komend

Składnia: `SEND:NNN:CCCC:CMD:ARG1:ARG2...`  
Token pinu: `A0`–`A15`, `B0`–`B15`, `C13`–`C15`  
Adres I2C: dziesiętny lub `0x`-hex (np. `104` lub `0x68`)

### GPIO

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `GPIO:MODE:PIN:M` | Set pinMode. M = `IN`/`OUT`/`PU`/`PD`/`AN`/`OD` | `GPIO:MODE:A0:IN` |
| `GPIO:WRITE:PIN:V` | digitalWrite. V = 0/1 | `0` lub `1` |
| `GPIO:READ:PIN` | digitalRead | `0` lub `1` |
| `GPIO:TOGGLE:PIN` | Przełącz wyjście | nowy stan `0`/`1` |
| `GPIO:PORT:X` | Odczyt dolnych 8 bitów portu X = A/B/C | `0x3F` |

### ADC (12-bit, 0–4095)

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `ADC:READ:PIN` | Pojedynczy odczyt | `2048` |
| `ADC:AVG:PIN:N` | Średnia N próbek (1–64) | `2050` |
| `ADC:MV:PIN` | Odczyt w mV (zakłada 3.3V) | `1650` |
| `ADC:MULTI:P1,P2,...` | Wiele pinów naraz (max 8) | `2048,1024,3200` |
| `ADC:STREAM:PIN:N:MS` | Burst N próbek co MS ms, max 48 | hex stream `0FFF0ABC...` |
| `ADC:TEMP` | Wewnętrzny czujnik temperatury | `254` (= 25.4 °C) |
| `ADC:VREF` | Szacowane VDDA w mV | `3285` |

### PWM (duty w promilach 0–1000)

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `PWM:SET:PIN:DUTY` | analogWrite, domyślna częstotliwość | `PWM:A8:500` |
| `PWM:FREQ:PIN:HZ:DUTY` | Własna częstotliwość + duty | `PWM:A8:1000Hz:500‰` |
| `PWM:STOP:PIN` | Zatrzymaj PWM | `STOPPED:A8` |
| `PWM:READ:PIN` | Ostatnio ustawiony duty | `500` |

**Piny PWM:** PA0–PA3, PA6–PA11, PB0–PB1, PB6–PB9

### I2C

Domyślne piny I2C1: PB6=SCL, PB7=SDA.  
I2C2: PB10=SCL, PB11=SDA (⚠ współdzielone z USART3).

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `I2C:CFG:SPEED` | Zmień prędkość: 100 lub 400 kHz | `I2C1:SPEED:400kHz` |
| `I2C:SCAN` | Skan 0x08–0x77 | `0x3C,0x68` lub `NONE` |
| `I2C:PING:ADDR` | Sprawdź obecność urządzenia | `ACK` lub `NAK` |
| `I2C:WRITE:ADDR:HEX` | Wyślij bajty | `OK:2B` |
| `I2C:READ:ADDR:N` | Odczytaj N bajtów (max 32) | `FF0102...` |
| `I2C:WREG:ADDR:REG:HEX` | Zapis rejestru | `OK:1B` |
| `I2C:RREG:ADDR:REG:N` | Odczyt rejestru (repeated start) | hex string |
| `I2C2:...` | Jak wyżej, ale magistrala I2C2 | — |

### SPI

Domyślne piny: PA5=SCK, PA6=MISO, PA7=MOSI. CS = dowolny GPIO.

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `SPI:BEGIN:CS:MODE:FREQ_KHZ` | Init (mode 0–3, freq 1–36000 kHz) | `SPI_OK:MODE0:5000kHz` |
| `SPI:XFER:CS:HEX` | Full-duplex transfer | odebrane bajty hex |
| `SPI:WRITE:CS:HEX` | TX only (odrzuć MISO) | `OK:4B` |
| `SPI:READ:CS:N` | RX only (wysyła 0xFF) | hex string |
| `SPI:END` | Zwolnij SPI | `SPI_OFF` |

### USART2 / USART3

USART2: PA2=TX, PA3=RX.  
USART3: PB10=TX, PB11=RX (⚠ współdzielone z I2C2).

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `U2:CFG:BAUD[:BITS:PAR:STOP]` | Konfiguruj (np. `U2:CFG:9600:8:N:1`) | `OK:9600_8N1` |
| `U2:TX:HEX` | Wyślij bajty | `TX:4B` |
| `U2:RX:N[:TIMEOUT_MS]` | Odbierz max N bajtów | hex lub `NONE` |
| `U2:FLUSH` | Wyczyść bufor RX | `OK` |
| `U2:STATUS` | Bajty dostępne w RX | `12` |
| `U2:CLOSE` | Zamknij port | `OK` |
| `U3:...` | Jak wyżej, USART3 | — |

### EEPROM (512 bajtów, emulacja flash)

⚠ Flash STM32F103 ma ~10 000 cykli kasowania. Nie pisz szybciej niż co 100 ms.

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `EE:WRITE:ADDR:BYTE` | Zapis bajtu (0–255) | `OK` |
| `EE:READ:ADDR` | Odczyt bajtu | `171` |
| `EE:WRWORD:ADDR:UINT32` | Zapis 32-bit little-endian | `OK` |
| `EE:RDWORD:ADDR` | Odczyt 32-bit | `12345678` |
| `EE:WRHEX:ADDR:HEX` | Zapis dowolnych bajtów | `OK:4B` |
| `EE:RDHEX:ADDR:N` | Odczyt N bajtów | `DEADBEEF` |
| `EE:FILL:BYTE` | Wypełnij całe 512 B | `OK` |
| `EE:SIZE` | Rozmiar wirtualnego EEPROM | `512` |

### IRQ (do 8 pinów jednocześnie)

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `IRQ:ATTACH:PIN:MODE` | Dołącz przerwanie. MODE = `RISE`/`FALL`/`CHANGE` | `ATTACHED:A0:RISE` |
| `IRQ:DETACH:PIN` lub `IRQ:DETACH:ALL` | Odłącz | `DETACHED:A0` |
| `IRQ:POLL` | Pobierz i wyczyść liczniki zdarzeń | `A0:3,B5:1` lub `NONE` |
| `IRQ:LIST` | Wylistuj dołączone piny i tryby | `A0:RISE,B5:CHANGE` |

### RTC (kryształ LSE 32.768 kHz na PC14/PC15)

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `RTC:INIT` | Inicjalizuj LSE oscillator (czekaj do 2 s) | `RTC:LSE_OK` |
| `RTC:STATUS` | Czy działa? | `RUNNING:LSE:OK` |
| `RTC:GET` | Aktualna data i czas | `2025-06-15 14:30:05 Sun` |
| `RTC:SET:YYYY:MM:DD:HH:MM:SS` | Ustaw czas | `SET:2025-06-15 14:30:00` |
| `RTC:GETTS` | Sekundy od 2000-01-01 | `803649005` |
| `RTC:SETTSS:N` | Ustaw z timestamp epoch-2000 | `SET:2025-06-15 14:30:00` |
| `RTC:EPOCH` | Unix timestamp (od 1970-01-01) | `1750333405` |

### CAN bus (bxCAN, PB8=RX, PB9=TX)

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `CAN:BEGIN:SPEED:MODE` | Init. SPEED=125/250/500/1000 kbps.<br>MODE=`NORMAL`/`LOOPBACK`/`SILENT` | `CAN:OK:250kbps:LOOPBACK` |
| `CAN:NOWARN` | Wyłącz ostrzeżenie o transceiverze | `CAN:WARN_SUPPRESSED` |
| `CAN:WARN` | Przywróć ostrzeżenie | `CAN:WARN_RESTORED` |
| `CAN:TX:ID:HEX` | Wyślij standard frame (11-bit ID, max 8B) | `TX:291:4B` |
| `CAN:TXE:ID:HEX` | Wyślij extended frame (29-bit ID) | `TX:...` |
| `CAN:RX` | Odbierz następną ramkę z bufora | `291:DEADBEEF:0:0` lub `NONE` |
| `CAN:FILTER:ID:MASK` | Filtr akceptacji | `FILTER:0x123:0x7FF` |
| `CAN:FILTER:OFF` | Wyłącz filtr (akceptuj wszystko) | `FILTER:OFF` |
| `CAN:STATUS` | Stan magistrali, liczniki błędów | `STATE:3:ERR:0x0000:TEC:0:REC:0` |
| `CAN:END` | Deinit CAN | `CAN:OFF` |

Format RX: `ID:HEX_DATA:EXT:RTR`  
EXT=0 → standard 11-bit, EXT=1 → extended 29-bit  
RTR=0 → data frame, RTR=1 → remote frame

### System

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `SYS:STATUS` | Wersja, uptime, RAM, reset cause | `v2.0:UP:12345:RAM:8192:...` |
| `SYS:UPTIME` | ms od boot | `12345` |
| `SYS:CHIPID` | 96-bit unique device ID | `A1B2C3...` (24 hex) |
| `SYS:CPUFREQ` | Częstotliwość CPU | `72MHz` |
| `SYS:FWVER` | Wersja firmware | `2.0` |
| `SYS:FREERAM` | Szacowana wolna RAM | `8192` |
| `SYS:ECHO:TEXT` | Test round-trip | `TEXT` |
| `SYS:RESET` | Soft reset (NVIC_SystemReset) | `RESETTING` |
| `SYS:WDOG:EN:MS` | Zbrojenie IWDG (100–26000 ms) | `WDOG_ON:5000ms` |
| `SYS:WDOG:KICK` | Ręczny kick | `KICKED` |
| `SYS:WDOG:DIS` | Wyłączenie forwarding (IWDG HW nie da się zatrzymać) | `NOTE:...` |

### Compute offload (CALC)

| Komenda DATA | Opis | Wynik |
|-------------|------|-------|
| `CALC:MAP:V:IN_MIN:IN_MAX:OUT_MIN:OUT_MAX` | Arduino `map()` | `50` |
| `CALC:CRC16:HEX` | CRC16-CCITT danych | `29B1` |
| `CALC:SQRT:N` | Całkowitoliczbowy sqrt | `12` |
| `CALC:CONSTRAIN:V:LO:HI` | `constrain()` | `100` |
| `CALC:ABS:V` | `abs()` | `42` |

---

## 6. Master — CLI (Serial Monitor)

Ustawienia: **115200 baud**, zakończenie linii **Newline**.  
Komendy są case-insensitive. Wpisz `help` po podłączeniu.

```
# Link
ping
reset
hb                              ręczny test heartbeat

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

# I2C2 (PB10/PB11, nie używać z u3)
i2c2 cfg|scan|ping|write|read|wreg|rreg  (ta sama składnia)

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

# USART3 (PB10/PB11, nie używać z i2c2)
u3 cfg|tx|rx|flush|status|close  (ta sama składnia)

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

## 7. Master — programistyczne API (C++)

Plik: `esp32_master/stm32_api.h`

Pozwala sterować STM32 z poziomu kodu ESP32 bez pisania komend ręcznie.
Wszystkie metody są **blokujące** — czekają na odpowiedź slave (max 6 s).

### Inicjalizacja

```cpp
#include "stm32_api.h"

STM32 stm;

void setup() {
    if (!stm.begin(10000)) {       // czekaj max 10 s na slave
        Serial.println("Slave not responding!");
        while(1) {}
    }
}

void loop() {
    stm.pump();                    // obowiązkowe — obsługuje UART + heartbeat
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

String hex = stm.adc.stream("A0", 32, 1); // 32 próbki co 1 ms

int tempTenths = stm.adc.temperature();   // np. 254 = 25.4 °C
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

// W loop():
String events = stm.irq.poll();   // "A0:5" lub "NONE"
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
String frame = stm.can.rx();      // "291:DEADBEEF:0:0" lub "NONE"
stm.can.end();
```

### System

```cpp
Serial.println(stm.sys.status());
long uptimeMs = stm.sys.uptime();
stm.sys.wdogEnable(10000);        // IWDG 10 s; master auto-kickuje co 5 s
```

### Obsługa błędów

```cpp
int val = stm.adc.read("A0");

if (!stm.ok()) {
    Serial.println("Error: " + stm.error());
}

// Escape hatch — dowolna komenda:
String r = stm.execute("SYS:ECHO:hello");
```

### Wzorzec dla długich operacji (nie blokuj loop)

```cpp
// ŹLE — blokuje loop() na długo:
for (int i = 0; i < 100; i++) {
    stm.ee.write(i, 0xFF);   // 100 × ~10 ms = 1 s blokady
}

// DOBRZE — jedna operacja na iterację:
static int eeIdx = 0;
if (eeIdx < 100) {
    stm.ee.write(eeIdx++, 0xFF);
}
```

---

## 8. WiFi i synchronizacja NTP→RTC

### Połączenie WiFi

```
wifi connect MojaSiec MojeHaslo
wifi status
wifi scan
wifi disconnect
```

Po połączeniu ESP32 automatycznie ponawia próbę co 30 s jeśli zgubi sygnał.

### Synchronizacja czasu

```
ntp sync
```

Sekwencja automatyczna:
1. `configTime(0, 0, "pool.ntp.org")` — ESP32 SNTP (UTC)
2. Czeka max 8 s na pierwszą odpowiedź NTP
3. Oblicza epoch-2000 = unix\_time − 946684800
4. Wysyła `RTC:INIT` → slave uruchamia LSE
5. Wysyła `RTC:SETTSS:<epoch2000>` → slave ustawia czas

```
ntp status              → "Last NTP sync: 2025-06-15 12:00:00 UTC (45 s ago)"
ntp server time.google.com
```

---

## 9. CAN bus

### Bez transceivera — loopback

```
can begin 250 loopback
can tx 123 DEADBEEF
can rx                    → [OK] 291:DEADBEEF:0:0
can end
```

### Z transponderem TJA1050 lub SN65HVD230

```
can nowarn
can begin 250 normal
can tx 123 DEADBEEF
```

Schemat podłączenia TJA1050:
```
Blue Pill PB9 (TX) → TJA1050 TXD
Blue Pill PB8 (RX) ← TJA1050 RXD
TJA1050 VCC  → 5V (lub 3.3V dla SN65HVD230)
TJA1050 GND  → GND
TJA1050 RS   → GND (high-speed mode)
TJA1050 CANH → magistrala CAN+
TJA1050 CANL → magistrala CAN−
```

### Tryby CAN

| Tryb | Transceiver | TX | RX | Zastosowanie |
|------|-------------|----|----|-------------|
| `LOOPBACK` | NIE | wewnętrzny | wewnętrzny | Testowanie kodu |
| `SILENT` | TAK | NIE | TAK | Pasywny sniffer |
| `NORMAL` | TAK | TAK | TAK | Pełna komunikacja |

---

## 10. Tabela konfliktów pinów

| Pin | Funkcja 1 | Funkcja 2 | Konflikt |
|-----|-----------|-----------|----------|
| PA9 | USART1 TX | — | **ZAREZERWOWANY** dla protokołu master |
| PA10 | USART1 RX | — | **ZAREZERWOWANY** dla protokołu master |
| PA5 | SPI1 SCK | ADC IN5 | SPI aktywny → ADC nie działa |
| PA6 | SPI1 MISO | ADC IN6 / TIM3 CH1 | SPI aktywny → ADC nie działa |
| PA7 | SPI1 MOSI | ADC IN7 / TIM3 CH2 | SPI aktywny → ADC nie działa |
| PB6 | I2C1 SCL | TIM4 CH1 | I2C ↔ PWM |
| PB7 | I2C1 SDA | TIM4 CH2 | I2C ↔ PWM |
| PB8 | CAN1 RX | TIM4 CH3 | CAN ↔ PWM |
| PB9 | CAN1 TX | TIM4 CH4 | CAN ↔ PWM |
| PB10 | I2C2 SCL | USART3 TX | **I2C2 ↔ U3 wzajemnie wykluczają się** |
| PB11 | I2C2 SDA | USART3 RX | **I2C2 ↔ U3 wzajemnie wykluczają się** |
| PC14 | LSE OSC32_IN | GPIO | Po `rtc init` → nie używaj jako GPIO |
| PC15 | LSE OSC32_OUT | GPIO | Po `rtc init` → nie używaj jako GPIO |

---

## 11. Test harness

### PowerShell 7 (Windows)

```powershell
# Auto-detekcja portu:
.\scripts\test.ps1

# Konkretny port:
.\scripts\test.ps1 -Port COM3

# Dłuższy timeout:
.\scripts\test.ps1 -Port COM3 -Timeout 10
```

Pokrycie testowe: GPIO, ADC, PWM, EEPROM, I2C scan, IRQ, CALC, CAN loopback,
RTC (soft-fail jeśli brak kryształu), 5× stress PING, reset + re-ping.  
Exit code: 0 = wszystkie testy PASS, 1 = przynajmniej jeden FAIL.

### Python (Linux / macOS / Windows)

```bash
pip install pyserial
python scripts/test.py              # auto-detekcja
python scripts/test.py /dev/ttyUSB0
python scripts/test.py COM3
```

Ta sama matryca testów co PS7.

---

## 12. Rozwiązywanie problemów

**ESP32 resetuje się przy otwarciu portu COM**  
Normalny objaw — linia DTR triggeruje ESP32 EN. Wizard PS (`scripts\flash.ps1`)
wyłącza DTR/RTS automatycznie. W Serial Monitor Arduino IDE odczekaj ~3 s.

**stm32flash: „Failed to init device"**  
Przytrzymaj RESET na Blue Pill, uruchom komendę flash, puść RESET ~3 s po starcie.
Bootloader ROM musi odbierać bajt synchronizacji 0x7F zaraz po starcie.

**Brak PONG po `ping`**  
Sprawdź: 1) esp32_master wgrany (nie flasher), 2) BOOT0=0 na Blue Pill,
3) GPIO17→PA10, GPIO16←PA9, 4) zakończenie linii **Newline** w Serial Monitor.

**`CRC_ERR` w logu slave**  
Zakłócenia na UART — sprawdź długość przewodów (maks. 20 cm bez ekranowania),
jakość kabli, wspólna masa ESP32↔STM32.

**`[ERR] Slave: I2C:NO_DATA`**  
Urządzenie I2C nie odpowiada. Sprawdź adres (`i2c scan`), zasilanie 3.3V,
podciągnięcia SDA/SCL (4.7 kΩ do 3.3V).

**`[!!] Heartbeat miss #1/3`**  
Slave wolno odpowiada (np. długi I2C scan). Zwiększ `HEARTBEAT_INTERVAL_MS`
w `esp32_master.ino` lub skróć czas operacji na slave.

**PWM bez efektu na danym pinie**  
Nie każdy pin Blue Pill obsługuje PWM sprzętowo. Użyj pinów z sekcji "Piny PWM"
w rozdziale 5. Na nieprawidłowym pinie nie pojawi się błąd.

**`ee write` wolne lub reset STM32**  
Każdy zapis commituje stronę flash (~10 ms). Nie pisz szybciej niż co 100 ms.
Po ~10 000 cykli EEPROM przestanie działać poprawnie.

**CAN: `CAN:INIT_FAIL`**  
Sprawdź: 1) AFIO clock aktywny (w kodzie tak jest), 2) PB8/PB9 wolne
(nie używane przez TIM4).

**RTC: `RTC:LSE_FAIL` lub `NOT_SET`**  
Kryształ 32.768 kHz musi być fizycznie wlutowany na PC14/PC15 (mały srebrny element
obok CPU). Jeśli go brakuje, RTC nie startuje.

**API: `stm.begin()` zwraca false**  
Slave nie odpowiada w ciągu `timeoutMs`. Sprawdź to samo co przy braku PONG.
Upewnij się, że `stm.pump()` jest w `loop()` i nic go nie blokuje.

---

## 13. Architektura plików

```
esp32-stm32-bridge/
│
├── esp32_flasher/
│   └── esp32_flasher.ino       USB↔UART bridge 8E1 (do flashowania STM32)
│
├── esp32_master/
│   ├── esp32_master.ino        Główna pętla, state machine, heartbeat, wdog
│   ├── protocol.h              CRC16, splitTokens, parsePin, hexUtils (kopia)
│   ├── parser.h                Parser CLI → protocol DATA string
│   ├── wifi_ntp.h              WiFi connect/scan, NTP sync → RTC:SETTSS
│   └── stm32_api.h             Programistyczne C++ API (klasa STM32)
│
├── stm32_slave/
│   ├── stm32_slave.ino         Setup/loop, router ramek, sendDone/sendErr
│   ├── protocol.h              CRC16, splitTokens, parsePin, hexUtils (oryginał)
│   ├── cmd_gpio.h              GPIO: MODE/WRITE/READ/TOGGLE/PORT
│   ├── cmd_adc.h               ADC: READ/AVG/MV/MULTI/STREAM/TEMP/VREF
│   ├── cmd_pwm.h               PWM: SET/FREQ/STOP/READ
│   ├── cmd_i2c.h               I2C1+I2C2: SCAN/PING/WRITE/READ/WREG/RREG/CFG
│   ├── cmd_spi.h               SPI: BEGIN/XFER/WRITE/READ/END
│   ├── cmd_u2.h                USART2: CFG/TX/RX/FLUSH/STATUS/CLOSE
│   ├── cmd_u3.h                USART3: CFG/TX/RX/FLUSH/STATUS/CLOSE
│   ├── cmd_ee.h                EEPROM: WRITE/READ/WRWORD/RDWORD/WRHEX/RDHEX/FILL
│   ├── cmd_irq.h               IRQ: ATTACH/DETACH/POLL/LIST (8 slotów ISR)
│   ├── cmd_can.h               CAN: BEGIN/TX/TXE/RX/FILTER/STATUS/END + loopback
│   ├── cmd_rtc.h               RTC: INIT/GET/SET/GETTS/SETTSS/EPOCH (LSE)
│   ├── cmd_sys.h               SYS: STATUS/UPTIME/CHIPID/RESET/WDOG/FREERAM/ECHO
│   └── cmd_misc.h              CALC + legacy LED/BLINK
│
├── scripts/
│   ├── Shared.psm1             Wspólny moduł PS7 (kolory, i18n, detekcja portu)
│   ├── flash.ps1               Wizard flashowania STM32 przez ESP32 bridge (PS7)
│   ├── test.ps1                Smoke test (PS7) — 40+ asercji, auto COM detect
│   ├── test.py                 Smoke test (Python) — ta sama matryca
│   ├── get-stm32flash.ps1      Pobieranie + weryfikacja MD5 stm32flash (Windows)
│   ├── get-stm32flash.sh       Pobieranie + weryfikacja MD5 stm32flash (Linux/macOS)
│   ├── get-stm32rtc.ps1        Instalacja biblioteki STM32RTC via arduino-cli
│   ├── flash_stm32.bat         Minimalny wrapper bat bez wizarda (Windows)
│   ├── flash_stm32.sh          Minimalny wrapper bash (Linux/macOS)
│   └── lang/                   Pliki i18n: pl.psd1 / en.psd1
│
├── docs/
│   └── example_api_usage.ino   Przykłady użycia C++ API (nie standalone sketch)
│
├── .github/workflows/build.yml CI: kompilacja ESP32 master/flasher + STM32 slave
├── README.md                   Skrócony przewodnik startowy (English)
├── DOCUMENTATION.md            Pełna dokumentacja (English)
├── INSTRUKCJA.md               Pełna instrukcja (Polski) ← ten plik
└── AGENTS.md                   Przewodnik dla agentów AI / kontrybutorów
```
