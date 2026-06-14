# Supermikrokontroler v2

ESP32 DevKit jako **protokół master** kontroluje STM32F103C8T6 "Blue Pill"
jako **uniwersalny agent sprzętowy** przez bezpośredni link UART.

---

## Hardware

| Element | Uwagi |
|---------|-------|
| ESP32 DevKit v1 | Standardowy layout 38-pin |
| STM32F103C8T6 "Blue Pill" | Klon jest OK |
| 4× przewody Dupont (F-F) | |
| `stm32flash` | Windows: `get-stm32flash.ps1`; Linux/macOS: `get-stm32flash.sh` |

---

## Schemat połączeń

```
ESP32 GPIO17 (TX2) ──────► STM32 PA10 (USART1 RX)
ESP32 GPIO16 (RX2) ◄────── STM32 PA9  (USART1 TX)
ESP32 3.3V         ─────── STM32 3.3V
ESP32 GND          ─────── STM32 GND
```

> **⚠ NIGDY nie podłączaj 5V do pinu STM32. Tylko 3.3V.**

Master używa **UART2 (GPIO16/GPIO17)** — NIE pinów z oznaczeniem „TX/RX"
na płytce (to GPIO1/GPIO3, reserved dla konsoli USB).

---

## Obsługiwane peryferia STM32

| Peryferium | Zakres | Komendy |
|-----------|--------|---------|
| GPIO | PA0–PA15, PB0–PB15, PC13–PC15 | mode, write, read, toggle, port |
| ADC | PA0–PA7, PB0–PB1 (12-bit) + temp/vref | read, avg, mv, multi, temp, vref |
| PWM | TIM1/2/3/4 capable pins | set, freq, stop, read |
| I2C1 | PB6=SCL, PB7=SDA | scan, ping, write, read, wreg, rreg |
| SPI1 | PA5=SCK, PA6=MISO, PA7=MOSI | begin, xfer, write, read, end |
| USART2 | PA2=TX, PA3=RX | cfg, tx, rx, flush, status, close |
| EEPROM | 512B flash-emulated | write, read, wrword, rdword, wrhex, rdhex, fill |
| IRQ | do 8 pinów jednocześnie | attach, detach, poll, list |
| System | chip ID, watchdog, reset | status, uptime, chipid, reset, wdog |
| Compute | offload do STM32 | map, crc16, sqrt, constrain, abs |

---

## Funkcje stabilności

| Mechanizm | Opis |
|-----------|------|
| **CRC16-CCITT** | Każda ramka SEND/DONE/ERR niesie 4-cyfrowy CRC nad polem DATA |
| **Heartbeat** | Master wysyła `HEARTBEAT` co 5 s; 3 brak odpowiedzi → link DEAD |
| **Retry** | Do 3 powtórzeń SEND przy braku RECV (300 ms timeout) |
| **Polling** | Do 15 zapytań POLL gdy komenda trwa > 2.5 s (400 ms interwał) |
| **IWDG watchdog** | Slave zbroi IWDG na `sys wdog en <ms>`; master auto-kickuje co T/2 |
| **Auto-kick** | Slave kickuje watchdog przy każdej poprawnie odebranej ramce |
| **Flow control** | RECV potwierdzenie przed wykonaniem; FREE po DONE dla czyszczenia slotu |
| **Line overflow** | Slave odrzuca linie >192 znaków i sygnalizuje `LINE_OVERFLOW` |

---

## Procedura flashowania

### Krok 0 — Pobierz stm32flash

**Windows:**
```powershell
.\scripts\get-stm32flash.ps1
```

**Linux/macOS:**
```bash
chmod +x scripts/get-stm32flash.sh && ./scripts/get-stm32flash.sh
```

Skrypt pobiera zip z SourceForge, weryfikuje MD5 z oficjalnego `MD5SUMS`,
wypakowuje binarny plik i sprawdza nagłówek.

### Krok 1 — Wgraj esp32_flasher na ESP32

Otwórz `esp32_flasher/esp32_flasher.ino` w Arduino IDE, wybierz płytkę
**ESP32 Dev Module** i port COM, wgraj normalnie.  
Po wgraniu dioda GPIO2 miga szybko (~150 ms) — most USB↔UART jest aktywny.

### Krok 2 — BOOT0 = 1 na Blue Pill

Przesuń jumper **BOOT0** z pozycji `0` na `1`.  
Naciśnij **RESET** na Blue Pill.  
Dioda PC13 NIE powinna migać — to potwierdza tryb bootloadera.

### Krok 3 — Wgraj stm32_slave

Najpierw skompiluj i wyeksportuj binarny plik w Arduino IDE:
`Sketch → Export Compiled Binary` → skopiuj `.bin` do `stm32_slave/stm32_slave.ino.bin`.

**Windows (wizard z detekcją bootloadera):**
```powershell
.\scripts\flash.ps1
```

**Windows (szybki, bez wizarda):**
```cmd
scripts\flash_stm32.bat
```

**Linux/macOS:**
```bash
./scripts/flash_stm32.sh /dev/ttyUSB0
```

**Ręcznie:**
```bash
stm32flash -b 115200 -w stm32_slave/stm32_slave.ino.bin -v COM3
```

### Krok 4 — BOOT0 = 0, powrót do aplikacji

1. Przesuń BOOT0 z powrotem na `0`.
2. Naciśnij RESET na Blue Pill.
3. Dioda PC13 powinna **mignąć 3 razy** — slave uruchomiony.

### Krok 5 — Wgraj esp32_master na ESP32

Otwórz `esp32_master/esp32_master.ino` w Arduino IDE, wgraj na ESP32.

### Krok 6 — Test

1. Otwórz Serial Monitor, **115200 baud**, zakończenie linii **Newline**.
2. Wpisz `ping` → oczekiwany wynik:
```
--> PING
<-- PONG
[OK] PONG — link alive.
```

---

## Dokumentacja komend

Pełna lista komend dostępna po wpisaniu `help` w Serial Monitorze.

### Składnia ogólna

Komendy są case-insensitive. Tokeny pinów: `A0`..`A15`, `B0`..`B15`, `C13`..`C15`.

### GPIO

```
gpio mode  <pin> in|out|pu|pd|an|od   set pinMode
gpio write <pin> 0|1                   digitalWrite
gpio read  <pin>                       digitalRead → 0|1
gpio toggle <pin>                      flip output
gpio port  A|B|C                       read lower 8 pins as hex byte
```

### ADC (12-bit, 0–4095)

```
adc read  <pin>                        single read
adc avg   <pin> <n_samples>            averaged read (max 64)
adc mv    <pin>                        read in millivolts
adc multi A0,A1,B0                     multiple pins at once → CSV
adc temp                               internal temp (tenths °C, e.g. 254=25.4°C)
adc vref                               estimated VDDA in mV
```

### PWM (duty w promilach, 0–1000)

```
pwm set  <pin> <duty>                  default timer frequency
pwm freq <pin> <hz> <duty>            custom frequency
pwm stop <pin>
pwm read <pin>                         last set duty value
```

**PWM-capable pins:** PA0–PA3, PA6–PA11, PB0–PB1, PB6–PB9

### I2C

```
i2c scan                               scan 0x08–0x77 → list or NONE
i2c ping  <addr>                       → ACK | NAK
i2c write <addr> <hexbytes>
i2c read  <addr> <n_bytes>             → hex string
i2c wreg  <addr> <reg> <hexbytes>      write register
i2c rreg  <addr> <reg> <n_bytes>       read register → hex
```

Adres jako decimal lub `0x`-hex, np. `104` lub `0x68` (MPU6050).  
Max 32 bajty na transakcję. Timeout 25 ms (chroni przed zawiśnięciem przy braku urządzenia).

### SPI

```
spi begin <cs_pin> <mode 0-3> <freq_khz>   init (PA5/PA6/PA7 auto)
spi xfer  <cs_pin> <hexbytes>               full-duplex → received hex
spi write <cs_pin> <hexbytes>               TX only
spi read  <cs_pin> <n_bytes>                RX only (sends 0xFF)
spi end
```

### USART2 (PA2=TX, PA3=RX)

```
u2 cfg <baud> [bits parity stop]     e.g. u2 cfg 9600 8 N 1
u2 tx <hexbytes>                     transmit
u2 rx <n_bytes> [timeout_ms]         receive → hex or NONE
u2 flush | u2 status | u2 close
```

### EEPROM (512 bajtów, emulacja flash)

```
ee write  <addr> <byte 0-255>
ee read   <addr>                      → decimal
ee wrword <addr> <uint32>             32-bit little-endian
ee rdword <addr>                      → decimal (unsigned)
ee wrhex  <addr> <hexbytes>
ee rdhex  <addr> <n_bytes>            → hex
ee fill   <byte>                      fill entire 512B
ee size                               → 512
```

⚠ Flash STM32F103 ma ~10 000 cykli kasowania. Nie wywołuj `ee write` w pętli.

### IRQ (do 8 pinów)

```
irq attach <pin> rise|fall|change
irq detach <pin>|all
irq poll                              get+clear event counts → "A0:3,B5:1" or NONE
irq list                              show attached pins
```

### System

```
sys status                            wersja, uptime, wolna RAM, przyczyna resetu
sys uptime                            ms od boot
sys chipid                            96-bit unique ID (24-char hex)
sys cpufreq                           MHz (72 na Blue Pill)
sys fwver                             wersja firmware
sys freeram                           szacowana wolna RAM w bajtach
sys echo <text>                       test latencji round-trip
sys reset                             soft reset (NVIC_SystemReset)
sys wdog en <ms>                      zbrojenie IWDG (100–26000 ms)
sys wdog kick                         ręczne kicnięcie
sys wdog dis                          wyłączenie forwarding (IWDG HW nie da się zatrzymać)
```

Po `sys wdog en <ms>` master automatycznie wysyła `SYS:WDOG:KICK` co `ms/2`.

### Compute offload

```
calc map <v> <in_min> <in_max> <out_min> <out_max>   Arduino map()
calc crc16 <hexbytes>                                 CRC16-CCITT → 4-char hex
calc sqrt <n>                                         integer sqrt
calc constrain <v> <lo> <hi>
calc abs <v>
```

---

## Format protokołu v2

```
SEND:NNN:CCCC:DATA\n       master → slave  (CCCC = CRC16 nad DATA)
RECV:NNN\n                 slave → master  (szybkie ACK)
DONE:NNN:CCCC:RESULT\n     slave → master  (CCCC = CRC16 nad RESULT)
ERR:NNN:CCCC:REASON\n      slave → master
BUSY:NNN\n                 slave → master  (odpowiedź na POLL)
POLL:NNN\n                 master → slave
FREE:NNN\n                 master → slave  (zwolnienie slotu)
PING / PONG\n              link test
HEARTBEAT / HEARTBEAT:ACK  auto co 5 s
RESET / RESET:ACK
```

`NNN` = 3-cyfrowy seq (001–999, wrap→001)  
`CCCC` = 4-cyfrowy uppercase hex CRC16-CCITT nad polem DATA/RESULT

---

## Konfiguracja Arduino IDE

### ESP32

1. File → Preferences → Additional boards URLs:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Boards Manager → **esp32** by Espressif → install
3. Board: **ESP32 Dev Module**

### STM32duino

1. Additional boards URLs (dodaj):
   `https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json`
2. Boards Manager → **STM32 MCU based boards** by STMicroelectronics → install
3. Board: STM32 MCU based boards → **Generic STM32F1 series**
4. Part Number: **BluePill F103C8**
5. Upload method: **STM32CubeProgrammer (Serial)** lub Arduino jako bridge

### arduino-cli (opcjonalne)

```bash
# Kompilacja slave
arduino-cli compile \
  --fqbn STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8 \
  stm32_slave

# Kompilacja master
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_master
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_flasher
```

---

## Rozwiązywanie problemów

**ESP32 resetuje się przy otwarciu portu COM**  
Normalny objaw — DTR linia triggeruje EN. Wizard PS (`flash_script.ps1`)
wyłącza DTR/RTS przed otwarciem portu. W Serial Monitorze Arduino IDE
odczekaj ~3 s po podłączeniu.

**stm32flash: „Failed to init device"**  
Przytrzymaj RESET na Blue Pill, uruchom komendę, puść RESET po ~3 s.
Bootloader musi być świeży gdy dotrze bajt synchronizacji 0x7F.

**Brak PONG po `ping`**  
Sprawdź: 1) esp32_master wgrany (nie flasher), 2) BOOT0=0 na Blue Pill,
3) kable GPIO17↔PA10, GPIO16↔PA9, 4) zakończenie linii = **Newline**.

**`CRC_ERR` w logu**  
Zakłócenia na UART — sprawdź długość i jakość przewodów, masy.
Typowe przy bardzo długich kablach (>30 cm bez ekranowania).

**`[ERR] Slave: I2C:NO_DATA`**  
Urządzenie I2C nie odpowiada. Sprawdź adres (`i2c scan`), zasilanie,
podciągnięcia SDA/SCL (4.7 kΩ do 3.3V).

**`[!!] Heartbeat miss #1/3`**  
Slave wolno odpowiada lub jest zajęty. Zwiększ `HEARTBEAT_INTERVAL_MS`
w `esp32_master.ino` jeśli slave wykonuje długie operacje.

**PWM nie działa na danym pinie**  
Nie każdy pin Blue Pill obsługuje PWM w hardware. Sprawdź listę
w sekcji PWM powyżej. Złe piny nie zwrócą błędu — po prostu
`analogWrite` ustawi stan cyfrowy.

**`ee write` powolne / reset STM32**  
Każdy zapis commituje flash — normalne spowolnienie ~10 ms.
Nie wywołuj w pętli częściej niż co 100 ms.
