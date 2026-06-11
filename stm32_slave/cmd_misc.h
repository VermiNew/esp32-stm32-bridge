#pragma once
/**
 * cmd_misc.h — compute offload (CALC) + legacy LED/BLINK commands
 *
 * CALC commands offload simple math to the STM32, saving ESP32 cycles
 * or allowing MCU-side computation without round-trip data transfer.
 *
 * Commands:
 *   CALC:MAP:V:IN_MIN:IN_MAX:OUT_MIN:OUT_MAX  Arduino map()
 *   CALC:CRC16:HEX                            CRC16-CCITT of given bytes
 *   CALC:SQRT:N                               integer square root
 *   CALC:CONSTRAIN:V:LO:HI                    constrain()
 *   CALC:ABS:V                                |V|
 *
 * Legacy commands (backward-compatible with v1 protocol):
 *   LED:ON | LED:OFF | LED:STATUS
 *   BLINK:MS  (0 = stop)
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

// Non-blocking blink engine state (also used by BLINK command)
static unsigned long blinkPeriodMs = 0;
static unsigned long lastBlinkMs   = 0;
static bool          blinkLedState = false;

static void blinkTick() {
    if (blinkPeriodMs == 0) return;
    unsigned long now = millis();
    if (now - lastBlinkMs >= blinkPeriodMs / 2) {
        lastBlinkMs = now;
        blinkLedState = !blinkLedState;
        // PC13 is active-low
        digitalWrite(PC13, blinkLedState ? LOW : HIGH);
    }
}

// ---- CALC ----
static void handleCalc(const String& seq, const String& args) {
    String toks[7];
    int n = splitTokens(args, ':', toks, 7);
    if (n < 2) { sendErr(seq, "CALC:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // MAP
    if (sub == "MAP") {
        if (n < 6) { sendErr(seq, "CALC:MISSING_ARGS"); return; }
        long v      = toks[1].toInt();
        long in_min = toks[2].toInt();
        long in_max = toks[3].toInt();
        long ou_min = toks[4].toInt();
        long ou_max = toks[5].toInt();
        if (in_max == in_min) { sendErr(seq, "CALC:DIV_ZERO"); return; }
        long result = map(v, in_min, in_max, ou_min, ou_max);
        sendDone(seq, String(result));
        return;
    }

    // CRC16
    if (sub == "CRC16") {
        uint8_t buf[64];
        int len = hexToBytes(toks[1], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "CALC:BAD_HEX"); return; }
        uint16_t crc = proto_crc16((const char*)buf, len);
        char out[5]; snprintf(out, sizeof(out), "%04X", crc);
        sendDone(seq, String(out));
        return;
    }

    // SQRT
    if (sub == "SQRT") {
        long v = toks[1].toInt();
        if (v < 0) { sendErr(seq, "CALC:NEGATIVE"); return; }
        sendDone(seq, String((long)sqrt((float)v)));
        return;
    }

    // CONSTRAIN
    if (sub == "CONSTRAIN") {
        if (n < 4) { sendErr(seq, "CALC:MISSING_ARGS"); return; }
        long v  = toks[1].toInt();
        long lo = toks[2].toInt();
        long hi = toks[3].toInt();
        sendDone(seq, String(constrain(v, lo, hi)));
        return;
    }

    // ABS
    if (sub == "ABS") {
        long v = toks[1].toInt();
        sendDone(seq, String(abs(v)));
        return;
    }

    sendErr(seq, "CALC:UNKNOWN_SUB");
}

// ---- Legacy LED ----
static void handleLed(const String& seq, const String& arg) {
    String a = arg; a.toUpperCase();
    if (a == "ON") {
        blinkPeriodMs = 0;
        digitalWrite(PC13, LOW);   // active-low
        sendDone(seq, "ON");
    } else if (a == "OFF") {
        blinkPeriodMs = 0;
        digitalWrite(PC13, HIGH);
        sendDone(seq, "OFF");
    } else if (a == "STATUS") {
        bool on = (digitalRead(PC13) == LOW);
        sendDone(seq, on ? "ON" : "OFF");
    } else {
        sendErr(seq, "LED:BAD_ARG");
    }
}

// ---- Legacy BLINK ----
static void handleBlink(const String& seq, const String& arg) {
    unsigned long ms = (unsigned long)arg.toInt();
    if (ms == 0) {
        blinkPeriodMs = 0;
        digitalWrite(PC13, HIGH);  // off
        sendDone(seq, "BLINK_OFF");
    } else if (ms < 20) {
        sendErr(seq, "BLINK:TOO_FAST");
    } else {
        blinkPeriodMs = ms;
        lastBlinkMs   = millis();
        blinkLedState = false;
        digitalWrite(PC13, HIGH);
        sendDone(seq, "BLINK_ON:" + String(ms) + "ms");
    }
}
