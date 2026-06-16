#pragma once
#define CMD_BUZZER_H_INCLUDED
/**
 * cmd_buzzer.h — passive buzzer commands for stm32_slave
 *
 * Uses PWM to drive a passive buzzer on any PWM-capable pin.
 * The buzzer plays a tone at the specified frequency for a given
 * duration, then stops automatically (non-blocking via millis()).
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   BUZZER:TONE:PIN:HZ:MS    play tone at HZ for MS milliseconds (0 = continuous)
 *   BUZZER:STOP:PIN          stop tone immediately
 *   BUZZER:BEEP:PIN          short 100 ms beep at 1000 Hz (quick feedback)
 *   BUZZER:STATUS:PIN        is tone currently playing? → PLAYING:HZ or IDLE
 *
 * PWM-capable pins: PA0–PA3, PA6–PA11, PB0–PB1, PB6–PB9
 * Typical passive buzzer wiring: signal pin → buzzer+ → buzzer− → GND
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

struct BuzzerSlot {
    int           pin      = -1;
    uint32_t      hz       = 0;
    unsigned long stopAt   = 0;   // millis() when to stop; 0 = continuous
    bool          active   = false;
};

static const int BUZZER_SLOTS = 4;
static BuzzerSlot buzzerSlots[BUZZER_SLOTS];

// Call from loop() to auto-stop timed tones
static void buzzerTick() {
    unsigned long now = millis();
    for (int i = 0; i < BUZZER_SLOTS; i++) {
        BuzzerSlot& s = buzzerSlots[i];
        if (s.active && s.stopAt > 0 && now >= s.stopAt) {
            analogWrite(s.pin, 0);
            digitalWrite(s.pin, LOW);
            s.active = false;
            s.stopAt = 0;
        }
    }
}

static BuzzerSlot* buzzerFindOrAlloc(int pin) {
    // Find existing slot for this pin
    for (int i = 0; i < BUZZER_SLOTS; i++)
        if (buzzerSlots[i].pin == pin) return &buzzerSlots[i];
    // Allocate a free slot
    for (int i = 0; i < BUZZER_SLOTS; i++)
        if (!buzzerSlots[i].active) return &buzzerSlots[i];
    return nullptr;
}

static void handleBuzzer(const String& seq, const String& args) {
    String toks[5];
    int n = splitTokens(args, ':', toks, 5);
    if (n < 1) { sendErr(seq, "BUZZER:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- TONE -----
    if (sub == "TONE") {
        if (n < 4) { sendErr(seq, "BUZZER:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "BUZZER:BAD_PIN"); return; }

        uint32_t hz = (uint32_t)constrain((long)toks[2].toInt(), 20, 20000);
        uint32_t ms = (uint32_t)toks[3].toInt();

        BuzzerSlot* s = buzzerFindOrAlloc(pin);
        if (!s) { sendErr(seq, "BUZZER:NO_SLOTS"); return; }

        analogWriteFrequency(hz);
        analogWrite(pin, 128);   // 50% duty — maximum loudness for passive buzzer

        s->pin    = pin;
        s->hz     = hz;
        s->active = true;
        s->stopAt = (ms > 0) ? (millis() + ms) : 0;

        char buf[32];
        snprintf(buf, sizeof(buf), "TONE:%s:%luHz:%lums",
                 toks[1].c_str(), (unsigned long)hz, (unsigned long)ms);
        sendDone(seq, String(buf));
        return;
    }

    // ----- BEEP (100 ms at 1000 Hz) -----
    if (sub == "BEEP") {
        if (n < 2) { sendErr(seq, "BUZZER:MISSING_PIN"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "BUZZER:BAD_PIN"); return; }

        BuzzerSlot* s = buzzerFindOrAlloc(pin);
        if (!s) { sendErr(seq, "BUZZER:NO_SLOTS"); return; }

        analogWriteFrequency(1000);
        analogWrite(pin, 128);
        s->pin    = pin;
        s->hz     = 1000;
        s->active = true;
        s->stopAt = millis() + 100;

        sendDone(seq, "BEEP:" + toks[1]);
        return;
    }

    // ----- STOP -----
    if (sub == "STOP") {
        if (n < 2) { sendErr(seq, "BUZZER:MISSING_PIN"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "BUZZER:BAD_PIN"); return; }

        for (int i = 0; i < BUZZER_SLOTS; i++) {
            if (buzzerSlots[i].pin == pin) {
                analogWrite(pin, 0);
                digitalWrite(pin, LOW);
                buzzerSlots[i].active = false;
                buzzerSlots[i].stopAt = 0;
            }
        }
        sendDone(seq, "STOPPED:" + toks[1]);
        return;
    }

    // ----- STATUS -----
    if (sub == "STATUS") {
        if (n < 2) { sendErr(seq, "BUZZER:MISSING_PIN"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "BUZZER:BAD_PIN"); return; }

        for (int i = 0; i < BUZZER_SLOTS; i++) {
            if (buzzerSlots[i].pin == pin && buzzerSlots[i].active) {
                sendDone(seq, "PLAYING:" + String(buzzerSlots[i].hz) + "Hz");
                return;
            }
        }
        sendDone(seq, "IDLE");
        return;
    }

    sendErr(seq, "BUZZER:UNKNOWN_SUB");
}
