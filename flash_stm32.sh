#!/usr/bin/env bash
# flash_stm32.sh — minimal wrapper around stm32flash for Linux/macOS
#
# Usage:
#   ./flash_stm32.sh [/dev/ttyUSB0]
#
# Requirements:
#   - stm32flash installed (apt install stm32flash  OR  brew install stm32flash)
#   - stm32_slave/stm32_slave.ino.bin compiled from Arduino IDE
#   - ESP32 running esp32_flasher firmware (LED blinks ~150 ms)
#   - STM32 Blue Pill with BOOT0 = 1 before running this script

set -euo pipefail

DEVICE="${1:-/dev/ttyUSB0}"
BIN="stm32_slave/stm32_slave.ino.bin"

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
    echo "Compile stm32_slave in Arduino IDE first, then copy the .bin here."
    exit 1
fi

echo "Device : $DEVICE"
echo "Binary : $BIN"
echo ""
echo "Make sure:"
echo "  - BOOT0 is set to 1 on the Blue Pill"
echo "  - ESP32 is running esp32_flasher (LED blinking fast)"
echo "  - Wiring: GPIO17->PA10, GPIO16<-PA9, 3.3V, GND"
echo ""
read -rp "Press ENTER to start flashing..."
echo ""

stm32flash -b 115200 -w "$BIN" -v "$DEVICE"

echo ""
echo "Done! Now:"
echo "  1. Move BOOT0 jumper back to 0"
echo "  2. Press RESET on the Blue Pill"
echo "  3. Flash esp32_master.ino onto the ESP32"
echo "  4. Open Serial Monitor at 115200, line ending = Newline"
echo "  5. Type 'ping' -- you should see PONG and [OK]"
