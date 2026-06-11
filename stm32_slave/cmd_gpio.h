#pragma once
/**
 * cmd_gpio.h — GPIO commands for stm32_slave
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   GPIO:MODE:PIN:M    set pin mode  M = IN | OUT | PU | PD | AN | OD
 *   GPIO:WRITE:PIN:V   digitalWrite  V = 0 | 1
 *   GPIO:READ:PIN      digitalRead   → 0 | 1
 *   GPIO:TOGGLE:PIN    toggle output pin
 *   GPIO:PORT:A|B|C    read full port byte (lower 8 bits) → decimal
 *
 * PIN token: A0..A15, B0..B15, C13..C15
 */

// Forward declarations (defined in stm32_slave.ino)
void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static void handleGpio(const String& seq, const String& args) {
    // args = "MODE:A0:OUT"  |  "WRITE:A0:1"  |  "READ:A0"  |  "TOGGLE:A0" | "PORT:A"
    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 2) { sendErr(seq, "GPIO:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- MODE -----
    if (sub == "MODE") {
        if (n < 3) { sendErr(seq, "GPIO:MISSING_MODE"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "GPIO:BAD_PIN"); return; }

        String m = toks[2]; m.toUpperCase();
        if      (m == "IN")  { pinMode(pin, INPUT); }
        else if (m == "OUT") { pinMode(pin, OUTPUT); }
        else if (m == "PU")  { pinMode(pin, INPUT_PULLUP); }
        else if (m == "PD")  { pinMode(pin, INPUT_PULLDOWN); }
        else if (m == "AN")  { pinMode(pin, INPUT_ANALOG); }
        else if (m == "OD")  { pinMode(pin, OUTPUT_OPEN_DRAIN); }
        else { sendErr(seq, "GPIO:BAD_MODE"); return; }

        sendDone(seq, "GPIO:MODE:" + toks[1] + ":" + m);
        return;
    }

    // ----- WRITE -----
    if (sub == "WRITE") {
        if (n < 3) { sendErr(seq, "GPIO:MISSING_VAL"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "GPIO:BAD_PIN"); return; }
        int val = toks[2].toInt();
        if (val != 0 && val != 1) { sendErr(seq, "GPIO:BAD_VAL"); return; }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, val ? HIGH : LOW);
        sendDone(seq, String(val));
        return;
    }

    // ----- READ -----
    if (sub == "READ") {
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "GPIO:BAD_PIN"); return; }
        sendDone(seq, String(digitalRead(pin)));
        return;
    }

    // ----- TOGGLE -----
    if (sub == "TOGGLE") {
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "GPIO:BAD_PIN"); return; }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, !digitalRead(pin));
        sendDone(seq, String(digitalRead(pin)));
        return;
    }

    // ----- PORT (read all 8 pins of a port as a byte) -----
    if (sub == "PORT") {
        char port = toupper((unsigned char)toks[1].charAt(0));
        if (port < 'A' || port > 'C') { sendErr(seq, "GPIO:BAD_PORT"); return; }

        uint8_t result = 0;
        int base = (port - 'A') * 16;
        for (int b = 0; b < 8; b++) {
            if (digitalRead(base + b) == HIGH) result |= (1 << b);
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", result);
        sendDone(seq, String(buf));
        return;
    }

    sendErr(seq, "GPIO:UNKNOWN_SUB");
}
