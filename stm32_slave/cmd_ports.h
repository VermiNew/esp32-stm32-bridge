#pragma once
/**
 * cmd_ports.h — pin usage report for stm32_slave
 *
 * Queries the live state of all modules and returns a compact map
 * of which pins are occupied and by what function.
 *
 * Commands:
 *   PORTS:ALL    → every pin with its current function (or FREE)
 *   PORTS:FREE   → only pins that are currently free
 *   PORTS:USED   → only pins that are currently occupied
 *
 * Result format: semicolon-separated entries, each "PIN=FUNCTION"
 *   e.g.  "A9=USART1_TX;A10=USART1_RX;B6=I2C1_SCL;A0=FREE;..."
 *
 * Pin coverage: PA0–PA15, PB0–PB15, PC13–PC15
 * (PA9/PA10 are always USART1_TX/RX — reserved for master protocol)
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

// ---------------------------------------------------------------------------
// Include-order contract:
//   cmd_ports.h MUST be included after all other cmd_*.h headers in the
//   same translation unit (stm32_slave.ino).  It reads static state variables
//   declared in those headers — no extern needed because everything lives in
//   one .ino compilation unit, but the order must be preserved.
//
//   Compile-time guard: the headers below define sentinel macros that we
//   check here to catch accidental wrong-order includes.
// ---------------------------------------------------------------------------
#if !defined(CMD_SPI_H_INCLUDED) || !defined(CMD_I2C_H_INCLUDED) || \
    !defined(CMD_CAN_H_INCLUDED) || !defined(CMD_IRQ_H_INCLUDED) || \
    !defined(CMD_DAC_H_INCLUDED) || !defined(CMD_BUZZER_H_INCLUDED) || \
    !defined(CMD_DEBUG_H_INCLUDED)
  #error "cmd_ports.h must be included after cmd_spi.h, cmd_i2c.h, cmd_can.h, cmd_irq.h, cmd_dac.h, cmd_buzzer.h, cmd_debug.h"
#endif

// ---------------------------------------------------------------------------
// Pin descriptor table — all STM32F103C8 pins exposed by this firmware
// ---------------------------------------------------------------------------
struct PinEntry {
    const char* token;   // "A0".."C15"
    int         flat;    // parsePin() result
};

// Build flat index inline: (port-'A')*16 + n
#define _F(port, n) (((port)-'A')*16+(n))

static const PinEntry ALL_PINS[] = {
    {"A0",  _F('A',0)},  {"A1",  _F('A',1)},  {"A2",  _F('A',2)},  {"A3",  _F('A',3)},
    {"A4",  _F('A',4)},  {"A5",  _F('A',5)},  {"A6",  _F('A',6)},  {"A7",  _F('A',7)},
    {"A8",  _F('A',8)},  {"A9",  _F('A',9)},  {"A10", _F('A',10)}, {"A11", _F('A',11)},
    {"A12", _F('A',12)}, {"A15", _F('A',15)},
    {"B0",  _F('B',0)},  {"B1",  _F('B',1)},  {"B3",  _F('B',3)},  {"B4",  _F('B',4)},
    {"B5",  _F('B',5)},  {"B6",  _F('B',6)},  {"B7",  _F('B',7)},  {"B8",  _F('B',8)},
    {"B9",  _F('B',9)},  {"B10", _F('B',10)}, {"B11", _F('B',11)}, {"B12", _F('B',12)},
    {"B13", _F('B',13)}, {"B14", _F('B',14)}, {"B15", _F('B',15)},
    {"C13", _F('C',13)}, {"C14", _F('C',14)}, {"C15", _F('C',15)},
};
static const int ALL_PINS_COUNT = (int)(sizeof(ALL_PINS) / sizeof(ALL_PINS[0]));

// ---------------------------------------------------------------------------
// Look up current function of a pin — returns "" if free
// ---------------------------------------------------------------------------
static String pinFunction(const char* tok, int flat) {
    // --- Always-reserved ---
    if (flat == _F('A', 9))  return "USART1_TX";   // master protocol
    if (flat == _F('A', 10)) return "USART1_RX";   // master protocol
    if (flat == _F('C', 13)) return "LED_BUILTIN";

    // --- RTC crystal ---
    if (rtcInitialized) {
        if (flat == _F('C', 14)) return "LSE_OSC_IN";
        if (flat == _F('C', 15)) return "LSE_OSC_OUT";
    }

    // --- DAC ---
    if (dac1Active && flat == _F('A', 4)) return "DAC1";
    if (dac2Active && flat == _F('A', 5)) return "DAC2";

    // --- SPI ---
    if (spiActive) {
        if (flat == _F('A', 5)) return "SPI1_SCK";
        if (flat == _F('A', 6)) return "SPI1_MISO";
        if (flat == _F('A', 7)) return "SPI1_MOSI";
        if (spiCsPin >= 0 && flat == spiCsPin) return "SPI1_CS";
    }

    // --- I2C1 ---
    if (i2cInitialized) {
        if (flat == _F('B', 6)) return "I2C1_SCL";
        if (flat == _F('B', 7)) return "I2C1_SDA";
    }

    // --- I2C2 ---
    if (i2c2Initialized) {
        if (flat == _F('B', 10)) return "I2C2_SCL";
        if (flat == _F('B', 11)) return "I2C2_SDA";
    }

    // --- USART2 ---
    if (u2Active) {
        if (flat == _F('A', 2)) return "USART2_TX";
        if (flat == _F('A', 3)) return "USART2_RX";
    }

    // --- CAN ---
    if (canActive) {
        if (flat == _F('B', 8)) return "CAN_RX";
        if (flat == _F('B', 9)) return "CAN_TX";
    }

    // --- IRQ ---
    for (int i = 0; i < MAX_IRQ_SLOTS; i++) {
        if (irqPinNums[i] >= 0 && irqPinNums[i] == flat) {
            String s = "IRQ_";
            switch (irqModes[i]) {
                case RISING:  s += "RISE"; break;
                case FALLING: s += "FALL"; break;
                default:      s += "CHG";  break;
            }
            return s;
        }
    }

    // --- Buzzer ---
    for (int i = 0; i < BUZZER_SLOTS; i++) {
        if (buzzerSlots[i].active && buzzerSlots[i].pin == flat) {
            return "BUZZER_" + String(buzzerSlots[i].hz) + "Hz";
        }
    }

    // --- Debug LEDs ---
    if (dbgAttached) {
        if (flat == dbgRxPin) return "DBG_RX_LED";
        if (flat == dbgTxPin) return "DBG_TX_LED";
    }

    // --- PWM (any pin with non-zero duty) ---
    if (flat >= 0 && flat < 64 && pwmDutyTable[flat] > 0) {
        return "PWM_" + String(pwmDutyTable[flat]) + "pm";
    }

    return "";   // free
}

// ---------------------------------------------------------------------------
// Handler
// ---------------------------------------------------------------------------
static void handlePorts(const String& seq, const String& args) {
    String toks[2];
    int n = splitTokens(args, ':', toks, 2);
    String sub = (n >= 1) ? toks[0] : "ALL";
    sub.toUpperCase();

    bool showFree = (sub == "ALL" || sub == "FREE");
    bool showUsed = (sub == "ALL" || sub == "USED");

    String result;
    result.reserve(256);

    for (int i = 0; i < ALL_PINS_COUNT; i++) {
        String fn = pinFunction(ALL_PINS[i].token, ALL_PINS[i].flat);
        bool used = fn.length() > 0;
        if (used  && !showUsed) continue;
        if (!used && !showFree) continue;

        if (result.length()) result += ';';
        result += ALL_PINS[i].token;
        result += '=';
        result += used ? fn : String("FREE");
    }

    if (!result.length()) result = "NONE";
    sendDone(seq, result);
}
