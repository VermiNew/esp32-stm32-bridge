#pragma once
/**
 * cmd_debug.h — activity debug LEDs for stm32_slave
 *
 * Two LEDs show protocol activity on the slave side:
 *   RX LED — blinks briefly on every valid SEND frame received from master
 *   TX LED — blinks briefly on every DONE/ERR frame sent to master
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   DEBUG:ATTACH:RX_PIN:TX_PIN   configure two LEDs (any GPIO, active-high)
 *   DEBUG:DETACH                 disable debug LEDs, release pins
 *   DEBUG:STATUS                 → ATTACHED:RX=A0:TX=A1 or DETACHED
 *
 * The blink pulse is 20 ms — long enough to see, short enough not to distract.
 * Pins are active-HIGH (standard LED + resistor to GND).
 *
 * Example wiring:
 *   Blue LED  → A0 (RX activity)
 *   Green LED → A1 (TX activity)
 *
 * After DEBUG:ATTACH the LEDs are driven automatically by debugRxPulse()
 * and debugTxPulse() — call them from the frame dispatcher in stm32_slave.ino.
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static const unsigned long DEBUG_PULSE_MS = 20;

static int  dbgRxPin     = -1;
static int  dbgTxPin     = -1;
static bool dbgAttached  = false;

static unsigned long dbgRxOffAt = 0;
static unsigned long dbgTxOffAt = 0;

// Call from loop() to turn off LEDs after pulse duration
static void debugTick() {
    if (!dbgAttached) return;
    unsigned long now = millis();
    if (dbgRxOffAt && now >= dbgRxOffAt) { digitalWrite(dbgRxPin, LOW); dbgRxOffAt = 0; }
    if (dbgTxOffAt && now >= dbgTxOffAt) { digitalWrite(dbgTxPin, LOW); dbgTxOffAt = 0; }
}

// Call when a SEND frame is received
static void debugRxPulse() {
    if (!dbgAttached || dbgRxPin < 0) return;
    digitalWrite(dbgRxPin, HIGH);
    dbgRxOffAt = millis() + DEBUG_PULSE_MS;
}

// Call when a DONE or ERR frame is sent
static void debugTxPulse() {
    if (!dbgAttached || dbgTxPin < 0) return;
    digitalWrite(dbgTxPin, HIGH);
    dbgTxOffAt = millis() + DEBUG_PULSE_MS;
}

static void handleDebug(const String& seq, const String& args) {
    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 1) { sendErr(seq, "DEBUG:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- ATTACH -----
    if (sub == "ATTACH") {
        if (n < 3) { sendErr(seq, "DEBUG:MISSING_PINS"); return; }
        toks[1].toUpperCase();
        toks[2].toUpperCase();
        int rxPin = parsePin(toks[1]);
        int txPin = parsePin(toks[2]);
        if (rxPin < 0 || txPin < 0) { sendErr(seq, "DEBUG:BAD_PIN"); return; }
        if (rxPin == txPin) { sendErr(seq, "DEBUG:SAME_PIN"); return; }

        dbgRxPin    = rxPin;
        dbgTxPin    = txPin;
        dbgAttached = true;
        dbgRxOffAt  = dbgTxOffAt = 0;

        pinMode(dbgRxPin, OUTPUT); digitalWrite(dbgRxPin, LOW);
        pinMode(dbgTxPin, OUTPUT); digitalWrite(dbgTxPin, LOW);

        // Flash both once to confirm attachment
        digitalWrite(dbgRxPin, HIGH); digitalWrite(dbgTxPin, HIGH);
        delay(80);
        digitalWrite(dbgRxPin, LOW);  digitalWrite(dbgTxPin, LOW);

        sendDone(seq, "ATTACHED:RX=" + toks[1] + ":TX=" + toks[2]);
        return;
    }

    // ----- DETACH -----
    if (sub == "DETACH") {
        if (dbgAttached) {
            digitalWrite(dbgRxPin, LOW);
            digitalWrite(dbgTxPin, LOW);
        }
        dbgAttached = false;
        dbgRxPin = dbgTxPin = -1;
        sendDone(seq, "DETACHED");
        return;
    }

    // ----- STATUS -----
    if (sub == "STATUS") {
        if (!dbgAttached) {
            sendDone(seq, "DETACHED");
        } else {
            // Reconstruct pin token from flat index
            auto pinToTok = [](int p) -> String {
                char buf[4];
                char port = 'A' + (p / 16);
                snprintf(buf, sizeof(buf), "%c%d", port, p % 16);
                return String(buf);
            };
            sendDone(seq, "ATTACHED:RX=" + pinToTok(dbgRxPin) + ":TX=" + pinToTok(dbgTxPin));
        }
        return;
    }

    sendErr(seq, "DEBUG:UNKNOWN_SUB");
}
