#!/usr/bin/env bash
# flash_stm32.sh — minimal wrapper around stm32flash for Linux/macOS
#
# Usage: ./scripts/flash_stm32.sh [/dev/ttyUSB0]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
DEVICE="${1:-/dev/ttyUSB0}"
BIN="$ROOT/stm32_slave/stm32_slave.ino.bin"

echo "==================================================="
echo "  Supermikrokontroler -- STM32 flash wrapper"
echo "==================================================="
echo ""

if ! command -v stm32flash &>/dev/null; then
    echo "ERROR: stm32flash not found."
    echo "  Linux:  sudo apt install stm32flash"
    echo "  macOS:  brew install stm32flash"
    exit 1
fi

if [[ ! -f "$BIN" ]]; then
    echo "ERROR: Firmware binary not found at $BIN"
    echo "Compile stm32_slave in Arduino IDE first."
    exit 1
fi

echo "Device : $DEVICE"
echo "Binary : $BIN"
echo ""
echo "Make sure BOOT0=1, ESP32 running esp32_flasher (LED ~150ms blink)."
echo "Wiring: GPIO17->PA10, GPIO16<-PA9, 3.3V, GND."
echo ""
read -rp "Press ENTER to start flashing..."
echo ""

stm32flash -b 115200 -w "$BIN" -v "$DEVICE"

echo ""
echo "Done! Move BOOT0 to 0, press RESET, flash esp32_master, open Serial Monitor."
