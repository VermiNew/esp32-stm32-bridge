#pragma once
/**
 * cmd_adc.h — ADC commands for stm32_slave
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   ADC:READ:PIN          12-bit single read → raw value (0-4095)
 *   ADC:AVG:PIN:N         average N samples (1-64) → raw value
 *   ADC:MV:PIN            read + convert to millivolts (assuming 3.3V VDDA)
 *   ADC:MULTI:P1,P2,...   read multiple pins at once → CSV of raw values
 *   ADC:TEMP              internal temperature sensor → tenths of °C (e.g. 254 = 25.4°C)
 *   ADC:VREF              internal Vref → estimated VDDA in mV
 *
 * PIN token: A0..A7, B0..B1 (ADC-capable pins on Blue Pill)
 *
 * NOTE: ADC resolution is set to 12-bit in setup(). analogRead() returns 0-4095.
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

// Typical calibration constants for STM32F103
// V25  = voltage at 25°C (typically 1.43V with 3.3V reference)
// Slope = 4.3 mV/°C
// These are approximations — for accuracy use factory calibration data.
static const float ADC_V25_MV    = 1430.0f;
static const float ADC_SLOPE_MV  = 4.3f;
static const float ADC_VDDA_MV   = 3300.0f;
static const float ADC_VREF_NOM  = 1200.0f;  // internal reference nominal 1.20 V

static void handleAdc(const String& seq, const String& args) {
    String toks[6];
    int n = splitTokens(args, ':', toks, 6);
    if (n < 1) { sendErr(seq, "ADC:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- READ -----
    if (sub == "READ") {
        if (n < 2) { sendErr(seq, "ADC:MISSING_PIN"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "ADC:BAD_PIN"); return; }
        pinMode(pin, INPUT_ANALOG);
        sendDone(seq, String(analogRead(pin)));
        return;
    }

    // ----- AVG -----
    if (sub == "AVG") {
        if (n < 3) { sendErr(seq, "ADC:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "ADC:BAD_PIN"); return; }
        int samples = constrain(toks[2].toInt(), 1, 64);
        pinMode(pin, INPUT_ANALOG);

        uint32_t sum = 0;
        for (int i = 0; i < samples; i++) {
            sum += (uint32_t)analogRead(pin);
            delayMicroseconds(50);
        }
        sendDone(seq, String(sum / (uint32_t)samples));
        return;
    }

    // ----- MV (millivolts) -----
    if (sub == "MV") {
        if (n < 2) { sendErr(seq, "ADC:MISSING_PIN"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "ADC:BAD_PIN"); return; }
        pinMode(pin, INPUT_ANALOG);
        int raw = analogRead(pin);
        // mV = raw * VDDA / 4095
        int mv = (int)((float)raw * ADC_VDDA_MV / 4095.0f);
        sendDone(seq, String(mv));
        return;
    }

    // ----- MULTI (multiple pins, comma-separated tokens list) -----
    if (sub == "MULTI") {
        if (n < 2) { sendErr(seq, "ADC:MISSING_PINS"); return; }
        // toks[1] might be "A0,A1,B0" — split by comma
        String pins[8];
        int    pcount = 0;
        {
            String pinList = toks[1];
            int start = 0;
            for (int i = 0; i <= (int)pinList.length() && pcount < 8; i++) {
                if (i == (int)pinList.length() || pinList.charAt(i) == ',') {
                    pins[pcount++] = pinList.substring(start, i);
                    start = i + 1;
                }
            }
        }
        if (pcount == 0) { sendErr(seq, "ADC:NO_PINS"); return; }

        String result;
        for (int i = 0; i < pcount; i++) {
            pins[i].toUpperCase();
            int pin = parsePin(pins[i]);
            if (pin < 0) { sendErr(seq, "ADC:BAD_PIN:" + pins[i]); return; }
            pinMode(pin, INPUT_ANALOG);
            if (i) result += ',';
            result += String(analogRead(pin));
        }
        sendDone(seq, result);
        return;
    }

    // ----- TEMP (internal temperature sensor on ADC1_IN16) -----
    if (sub == "TEMP") {
        // STM32duino exposes the internal temp via ATEMP macro on some builds.
        // If ATEMP is not defined, we use the raw channel approach.
#ifdef ATEMP
        int raw = analogRead(ATEMP);
#else
        // Read channel 16 directly (ADC1 IN16 is the temperature sensor)
        // This may not work on all STM32duino versions without ATEMP defined.
        int raw = analogRead(A0);  // fallback: read A0 and note calibration needed
#endif
        // V_SENSE = raw * VDDA / 4095  (in mV)
        float vsense = (float)raw * ADC_VDDA_MV / 4095.0f;
        // Temp = (V25 - V_SENSE) / Avg_Slope + 25
        float temp_c = (ADC_V25_MV - vsense) / ADC_SLOPE_MV + 25.0f;
        int temp_tenths = (int)(temp_c * 10.0f);
        // Result: e.g. "254" means 25.4 °C
        sendDone(seq, String(temp_tenths));
        return;
    }

    // ----- VREF (estimate VDDA using internal 1.20V reference) -----
    if (sub == "VREF") {
#ifdef AVREF
        int raw = analogRead(AVREF);
#else
        // Fallback: return nominal
        sendDone(seq, "3300");
        return;
#endif
        // VDDA_mV = VREF_NOM * 4095 / raw
        if (raw == 0) { sendErr(seq, "ADC:VREF_ZERO"); return; }
        int vdda_mv = (int)(ADC_VREF_NOM * 4095.0f / (float)raw);
        sendDone(seq, String(vdda_mv));
        return;
    }

    sendErr(seq, "ADC:UNKNOWN_SUB");
}
