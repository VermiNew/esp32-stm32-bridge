#pragma once
/**
 * cmd_dac.h — DAC (Digital-to-Analog Converter) commands for stm32_slave
 *
 * STM32F103C8T6 has a 12-bit DAC with two channels:
 *   DAC1 → PA4
 *   DAC2 → PA5
 *
 * ⚠ CONFLICT: PA4 and PA5 are shared with SPI1 (NSS/SCK).
 *   Do NOT use DAC and SPI simultaneously on these pins.
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   DAC:SET:CH:VALUE      set output, CH=1|2, VALUE=0..4095 (12-bit raw)
 *   DAC:MV:CH:MV          set output in millivolts (0..3300 mV)
 *   DAC:READ:CH           read last set raw value (0..4095)
 *   DAC:OFF:CH            disable DAC channel, pin returns to analog input
 *   DAC:OFF               disable both channels
 */

#include <Arduino.h>

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static bool     dac1Active = false;
static bool     dac2Active = false;
static uint16_t dac1Value  = 0;
static uint16_t dac2Value  = 0;

static void dacEnable(int ch) {
    if (ch == 1 && !dac1Active) {
        // Enable DAC1 clock and channel via HAL
        __HAL_RCC_DAC_CLK_ENABLE();
        DAC->CR |= DAC_CR_EN1;
        dac1Active = true;
    } else if (ch == 2 && !dac2Active) {
        __HAL_RCC_DAC_CLK_ENABLE();
        DAC->CR |= DAC_CR_EN2;
        dac2Active = true;
    }
}

static void dacWrite(int ch, uint16_t raw) {
    raw = constrain(raw, 0, 4095);
    dacEnable(ch);
    if (ch == 1) {
        DAC->DHR12R1 = raw;
        DAC->SWTRIGR |= DAC_SWTRIGR_SWTRIG1;
        dac1Value = raw;
    } else {
        DAC->DHR12R2 = raw;
        DAC->SWTRIGR |= DAC_SWTRIGR_SWTRIG2;
        dac2Value = raw;
    }
}

static void handleDac(const String& seq, const String& args) {
    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 1) { sendErr(seq, "DAC:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- SET (raw 12-bit value) -----
    if (sub == "SET") {
        if (n < 3) { sendErr(seq, "DAC:MISSING_ARGS"); return; }
        int ch  = toks[1].toInt();
        if (ch != 1 && ch != 2) { sendErr(seq, "DAC:BAD_CH"); return; }
        uint16_t raw = (uint16_t)constrain(toks[2].toInt(), 0, 4095);
        dacWrite(ch, raw);
        char buf[24];
        snprintf(buf, sizeof(buf), "DAC%d:%u", ch, raw);
        sendDone(seq, String(buf));
        return;
    }

    // ----- MV (millivolts, 0–3300) -----
    if (sub == "MV") {
        if (n < 3) { sendErr(seq, "DAC:MISSING_ARGS"); return; }
        int ch = toks[1].toInt();
        if (ch != 1 && ch != 2) { sendErr(seq, "DAC:BAD_CH"); return; }
        int mv  = constrain(toks[2].toInt(), 0, 3300);
        uint16_t raw = (uint16_t)((float)mv * 4095.0f / 3300.0f);
        dacWrite(ch, raw);
        char buf[32];
        snprintf(buf, sizeof(buf), "DAC%d:%dmV:%u", ch, mv, raw);
        sendDone(seq, String(buf));
        return;
    }

    // ----- READ (last set raw value) -----
    if (sub == "READ") {
        if (n < 2) { sendErr(seq, "DAC:MISSING_CH"); return; }
        int ch = toks[1].toInt();
        if (ch != 1 && ch != 2) { sendErr(seq, "DAC:BAD_CH"); return; }
        sendDone(seq, String(ch == 1 ? dac1Value : dac2Value));
        return;
    }

    // ----- OFF (disable channel(s)) -----
    if (sub == "OFF") {
        if (n >= 2) {
            int ch = toks[1].toInt();
            if (ch == 1) { DAC->CR &= ~DAC_CR_EN1; dac1Active = false; dac1Value = 0; }
            else if (ch == 2) { DAC->CR &= ~DAC_CR_EN2; dac2Active = false; dac2Value = 0; }
            else { sendErr(seq, "DAC:BAD_CH"); return; }
            sendDone(seq, "DAC" + String(ch) + ":OFF");
        } else {
            DAC->CR &= ~(DAC_CR_EN1 | DAC_CR_EN2);
            dac1Active = dac2Active = false;
            dac1Value  = dac2Value  = 0;
            sendDone(seq, "DAC:ALL_OFF");
        }
        return;
    }

    sendErr(seq, "DAC:UNKNOWN_SUB");
}
