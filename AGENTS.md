# Supermikrokontroler v2 — Contributor / Agent Guide

ESP32 master ↔ STM32 Blue Pill slave przez UART, protokół ASCII z CRC16.

---

## Struktura projektu

```
supermikrokontroler/
├── esp32_flasher/
│   └── esp32_flasher.ino       USB↔UART bridge (8E1) do flashowania STM32
├── esp32_master/
│   ├── esp32_master.ino        główna pętla, state machine, heartbeat, wdog
│   ├── parser.h                parser komend CLI → protocol DATA string
│   └── protocol.h              CRC16, splitTokens, parsePin, hexUtils (kopia)
├── stm32_slave/
│   ├── stm32_slave.ino         setup/loop, router ramek, sendDone/sendErr
│   ├── protocol.h              CRC16, splitTokens, parsePin, hexUtils (oryginał)
│   ├── cmd_gpio.h              GPIO: MODE/WRITE/READ/TOGGLE/PORT
│   ├── cmd_adc.h               ADC: READ/AVG/MV/MULTI/TEMP/VREF
│   ├── cmd_pwm.h               PWM: SET/FREQ/STOP/READ
│   ├── cmd_i2c.h               I2C: SCAN/PING/WRITE/READ/WREG/RREG
│   ├── cmd_spi.h               SPI: BEGIN/XFER/WRITE/READ/END
│   ├── cmd_u2.h                USART2: CFG/TX/RX/FLUSH/STATUS/CLOSE
│   ├── cmd_ee.h                EEPROM: WRITE/READ/WRWORD/RDWORD/WRHEX/RDHEX/FILL
│   ├── cmd_irq.h               IRQ: ATTACH/DETACH/POLL/LIST (8 slotów)
│   ├── cmd_sys.h               SYS: STATUS/UPTIME/CHIPID/RESET/WDOG/...
│   └── cmd_misc.h              CALC (MAP/CRC16/SQRT/...) + legacy LED/BLINK
├── flash_script.ps1            PowerShell 7 wizard flashowania (Windows)
├── flash_stm32.bat             prosty wrapper bat (Windows)
├── flash_stm32.sh              wrapper bash (Linux/macOS)
├── get_stm32flash.ps1          pobieranie + weryfikacja MD5 stm32flash (Windows)
└── get_stm32flash.sh           pobieranie + weryfikacja MD5 stm32flash (Linux/macOS)
```

---

## Iteracyjny workflow (OBOWIĄZKOWY)

Każda zmiana = jeden mały, weryfikowalny krok.

```
Plan → Kod → Przeczytaj swój kod → Kompilacja → Test HW → Commit
  ↑                                                           |
  └───────────────────── następny krok ──────────────────────┘
```

### Zasady

1. **Czytaj przed pisaniem** — przed modyfikacją pliku wywołaj `view`, żeby
   zobaczyć aktualny stan. Nigdy nie edytuj z pamięci.
2. **Jeden plik na raz** — nie zmieniaj jednocześnie slave i master.
3. **Kompiluj po każdej zmianie** — patrz FQBNs poniżej.
4. **Nie commituj nieskompilowanego kodu** — commit to nagroda za działający kod.
5. **Nie używaj `git add .`** — tylko `git add <konkretne pliki>`.
6. **Nie zmieniaj formatu protokołu** bez jednoczesnej zmiany obu stron.

### FQBNs

```bash
# STM32 Blue Pill
arduino-cli compile \
  --fqbn STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8 \
  stm32_slave

# ESP32 master
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_master

# ESP32 flasher
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_flasher
```

---

## Protokół v2 — pełna specyfikacja

### Format ramki

```
SEND:NNN:CCCC:DATA\n       master → slave
RECV:NNN\n                 slave → master (szybkie ACK przed wykonaniem)
DONE:NNN:CCCC:RESULT\n     slave → master
ERR:NNN:CCCC:REASON\n      slave → master
BUSY:NNN\n                 slave → master (odpowiedź na POLL)
POLL:NNN\n                 master → slave
FREE:NNN\n                 master → slave (master odebrał DONE)
PING / PONG
HEARTBEAT / HEARTBEAT:ACK
RESET / RESET:ACK
```

- `NNN` = 3-cyfrowy seq 001–999, wrap→001
- `CCCC` = CRC16-CCITT (poly 0x1021, init 0xFFFF) nad polem DATA/RESULT
- Ramki BEZ ładunku (RECV, BUSY, FREE, POLL) NIE mają pola CRC
- Ramki kontrolne (PING, RESET, HEARTBEAT) są bare — bez SEQ

### CRC16

```cpp
uint16_t proto_crc16(const char* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(uint8_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
    return crc;
}
```

CRC liczony wyłącznie nad polem DATA (wszystko po trzecim dwukropku).

### Timingsy (master)

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
                  │timeout×3           │timeout           │
                  ▼                    ▼                   │
                IDLE              POLLING ──DONE──────────┘
                                     │
                                  MAX_POLLS
                                     │
                                    IDLE (error)
```

---

## Numeracja pinów STM32duino GenF1

Token → flat pin index: `pin = (port - 'A') * 16 + n`

| Token | Flat idx | Funkcja alternatywna |
|-------|---------|---------------------|
| A0–A7 | 0–7 | ADC_IN0–IN7 |
| A8–A11 | 8–11 | TIM1 CH1–CH4 (PWM) |
| A9, A10 | 9, 10 | USART1 TX/RX (RESERVED dla mastera) |
| A2, A3 | 2, 3 | USART2 TX/RX |
| A5–A7 | 5–7 | SPI1 SCK/MISO/MOSI |
| B0, B1 | 16, 17 | ADC_IN8/IN9, TIM3 CH3/CH4 |
| B6, B7 | 22, 23 | I2C1 SCL/SDA, TIM4 CH1/CH2 |
| B8, B9 | 24, 25 | TIM4 CH3/CH4, CAN RX/TX |
| B10, B11 | 26, 27 | USART3 TX/RX, I2C2 SCL/SDA |
| B12–B15 | 28–31 | SPI2 NSS/SCK/MISO/MOSI |
| C13 | 45 | Onboard LED (active-low) |

**USART1 (A9/A10) jest ZAREZERWOWANY** dla komunikacji master-slave.
Nie używaj go w cmd_gpio.h ani nigdzie indziej.

---

## Bezpieczeństwo sprzętowe

- **Nigdy 5V na STM32** — wszystkie sygnały 3.3V
- **USART1 (PA9/PA10) reserved** — tylko dla protokołu master↔slave
- **Bootloader 8E1** — esp32_flasher używa 8E1; aplikacja używa 8N1. Nie mieszaj.
- **BOOT0=1** → bootloader ROM (PC13 nie miga)
- **BOOT0=0** → aplikacja (PC13 miga 3× przy boot)
- **IWDG nie da się wyłączyć** — po `sys wdog en` STM32 będzie się resetował
  bez regularnych kick'ów. Master automatycznie wysyła kick co `ms/2`.
- **Flash wear** — `EE:WRITE` commituje flash. Max ~10 000 cykli na stronę.
  Nie używaj w pętli szybszej niż co 100 ms.
- **SPI + ADC conflict** — PA5/PA6/PA7 to jednocześnie SPI1 i piny ADC.
  Po `spi begin` ADC na tych pinach nie działa.

---

## Konwencja commitów

```
<type>(<scope>): <description>

type:  feat | fix | refactor | docs | chore | test
scope: stm32-slave | esp32-master | esp32-flasher | tooling | docs | protocol
```

Przykłady:
```
feat(stm32-slave): add U3 handler for USART3
fix(protocol): handle CRC miss gracefully without loop reset
docs: update AGENTS.md with USART3 pin mapping
```

---

## Checklist przed commitem

- [ ] Sketch kompiluje się bez błędów dla docelowego FQBN
- [ ] Brak nowych ostrzeżeń kompilatora
- [ ] Brak `delay()` w `loop()` ani w handlerach komend
- [ ] Format ramki protokołu niezmieniony (chyba że to jest commit zmiany protokołu)
- [ ] Obie strony (slave + master) zaktualizowane przy zmianie protokołu
- [ ] `protocol.h` jest identyczny w `stm32_slave/` i `esp32_master/`
- [ ] Bezpieczeństwo sprzętowe zachowane (3.3V, 8E1 vs 8N1, PA9/PA10 zarezerwowane)
- [ ] Nie ma `git add .` w historii

---

## Kod w języku angielskim

Wszystkie identyfikatory, komentarze i wyjścia Serial są po angielsku.
Jedynym wyjątkiem jest live chat z człowiekiem — to po polsku.
