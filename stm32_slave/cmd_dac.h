#pragma once
#define CMD_DAC_H_INCLUDED
/**
 * cmd_dac.h — DAC (Digital-to-Analog Converter) commands for stm32_slave
 *
 * STM32F103C8T6 has a 12-bit DAC with two channels:
 *   DAC1 → PA4
 *   DAC2 → PA5
 *
 * Uses STM32duino analogWrite() — the core automatically routes DAC-capable
 * pins to the DAC peripheral. analogWriteResolution(12) gives full 12-bit range.
 *
 * ⚠ CONFLICT: PA4 and PA5 are shared with SPI1 (NSS/SCK).
 *   Do NOT use DAC and SPI simultaneously on these pins.
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   DAC:SET:CH:VALUE    set output, CH=1|2, VALUE=0..4095 (12-bit raw)
 *   DAC:MV:CH:MV        set output in millivolts (0..3300 mV)
 *   DAC:READ:CH         read last set raw value (0..4095)
 *   DAC:OFF:CH          drive pin LOW and release (ch = 1 or 2)
 *   DAC:OFF             drive both pins LOW and release
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static const int DAC_PIN[3] = { 0, PA4, PA5 };  // index 1=DAC1, 2=DAC2

static uint16_t dacValue[3] = { 0, 0, 0 };  // last set raw value per channel
static bool     dacActive[3] = { false, false, false };

static void dacWrite(int ch, uint16_t raw) {
    analogWriteResolution(12);
    analogWrite(DAC_PIN[ch], raw);
    dacValue[ch]  = raw;
    dacActive[ch] = true;
}

static void dacOff(int ch) {
    analogWrite(DAC_PIN[ch], 0);
    pinMode(DAC_PIN[ch], INPUT_ANALOG);
    dacValue[ch]  = 0;
    dacActive[ch] = false;
}

static void handleDac(const String& seq, const String& args) {
    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 1) { sendErr(seq, "DAC:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- SET (raw 12-bit, 0–4095) -----
    if (sub == "SET") {
        if (n < 3) { sendErr(seq, "DAC:MISSING_ARGS"); return; }
        int ch = toks[1].toInt();
        if (ch < 1 || ch > 2) { sendErr(seq, "DAC:BAD_CH"); return; }
        uint16_t raw = (uint16_t)constrain(toks[2].toInt(), 0, 4095);
        dacWrite(ch, raw);
        char buf[16]; snprintf(buf, sizeof(buf), "DAC%d:%u", ch, raw);
        sendDone(seq, String(buf));
        return;
    }

    // ----- MV (millivolts, 0–3300) -----
    if (sub == "MV") {
        if (n < 3) { sendErr(seq, "DAC:MISSING_ARGS"); return; }
        int ch = toks[1].toInt();
        if (ch < 1 || ch > 2) { sendErr(seq, "DAC:BAD_CH"); return; }
        int mv = constrain(toks[2].toInt(), 0, 3300);
        uint16_t raw = (uint16_t)((long)mv * 4095 / 3300);
        dacWrite(ch, raw);
        char buf[24]; snprintf(buf, sizeof(buf), "DAC%d:%dmV:%u", ch, mv, raw);
        sendDone(seq, String(buf));
        return;
    }

    // ----- READ (last set raw value) -----
    if (sub == "READ") {
        if (n < 2) { sendErr(seq, "DAC:MISSING_CH"); return; }
        int ch = toks[1].toInt();
        if (ch < 1 || ch > 2) { sendErr(seq, "DAC:BAD_CH"); return; }
        sendDone(seq, String(dacValue[ch]));
        return;
    }

    // ----- OFF -----
    if (sub == "OFF") {
        if (n >= 2) {
            int ch = toks[1].toInt();
            if (ch < 1 || ch > 2) { sendErr(seq, "DAC:BAD_CH"); return; }
            dacOff(ch);
            sendDone(seq, "DAC" + String(ch) + ":OFF");
        } else {
            dacOff(1);
            dacOff(2);
            sendDone(seq, "DAC:ALL_OFF");
        }
        return;
    }

    sendErr(seq, "DAC:UNKNOWN_SUB");
}
