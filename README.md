# esp32-stm32-bridge

![Build](https://github.com/VermiNew/esp32-stm32-bridge/actions/workflows/build.yml/badge.svg)

ESP32 DevKit as **protocol master** controls an STM32F103C8T6 "Blue Pill"
as a **universal hardware agent** over a direct UART link with ASCII + CRC16 protocol.

**[Czytaj po polsku](docs/CZYTAJ.md)**

---

## Documentation

| | English | Polski |
|-|---------|--------|
| Quick start | [README.md](README.md) ← you are here | [docs/CZYTAJ.md](docs/CZYTAJ.md) |
| Full docs | [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md) | [docs/INSTRUKCJA.md](docs/INSTRUKCJA.md) |
| Scripts | [scripts/README.md](scripts/README.md) | [scripts/CZYTAJ.md](scripts/CZYTAJ.md) |
| Contributors / AI agents | [AGENTS.md](AGENTS.md) | — |

---

## Wiring

```
ESP32 GPIO17 (TX2) ──────► STM32 PA10 (USART1 RX)
ESP32 GPIO16 (RX2) ◄────── STM32 PA9  (USART1 TX)
ESP32 3.3V         ─────── STM32 3.3V
ESP32 GND          ─────── STM32 GND
```

> **NEVER connect 5 V to any STM32 pin.**

---

## Quick start

```powershell
# 1. Get stm32flash
.\scripts\get-stm32flash.ps1          # Windows
./scripts/get-stm32flash.sh           # Linux/macOS

# 2. Flash STM32 slave (BOOT0=1 first, then reset Blue Pill)
.\scripts\flash.ps1                   # Windows wizard
./scripts/flash_stm32.sh /dev/ttyUSB0 # Linux/macOS

# 3. Flash esp32_master via Arduino IDE → board: ESP32 Dev Module

# 4. Open Serial Monitor (115200 baud, Newline), type:
ping   # → [OK] PONG — link alive.
```

Full flashing procedure: [docs/DOCUMENTATION.md — Flashing Procedure](docs/DOCUMENTATION.md#3-flashing-procedure)

---

## Hardware

| Part | Notes |
|------|-------|
| ESP32 DevKit v1 | Standard 38-pin layout |
| STM32F103C8T6 "Blue Pill" | Clone is fine |
| 4× Dupont wire (F-F) | Keep under 20 cm |
| `stm32flash` | `scripts\get-stm32flash.ps1` / `scripts/get-stm32flash.sh` |

---

## License

[MIT](LICENSE)
