#pragma once
/**
 * cmd_pwm.h — PWM commands for stm32_slave
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   PWM:SET:PIN:DUTY          analogWrite, default timer freq (~1 kHz)
 *                              DUTY = 0..1000 (per-mille, 1000 = 100%)
 *   PWM:FREQ:PIN:HZ:DUTY      set custom frequency (Hz) + duty (0..1000)
 *   PWM:STOP:PIN              stop PWM (pin goes LOW)
 *   PWM:READ:PIN              read current duty back (may be 0 if not set by us)
 *
 * PWM-capable pins on Blue Pill (STM32F103C8):
 *   TIM1: PA8, PA9, PA10, PA11
 *   TIM2: PA0, PA1, PA2, PA3
 *   TIM3: PA6, PA7, PB0, PB1
 *   TIM4: PB6, PB7, PB8, PB9
 *
 * NOTE: analogWriteFrequency() sets the frequency globally for the next
 * analogWrite call on STM32duino. Affects all channels of the same timer.
 * If custom frequency is not needed, use PWM:SET (faster, uses default freq).
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

// Track last set duty and frequency per pin index
static uint16_t pwmDutyTable[64] = {};   // per-mille 0..1000
static uint32_t pwmFreqTable[64] = {};   // Hz, 0 = default freq

static void handlePwm(const String& seq, const String& args) {
    String toks[5];
    int n = splitTokens(args, ':', toks, 5);
    if (n < 2) { sendErr(seq, "PWM:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- SET (default frequency) -----
    if (sub == "SET") {
        if (n < 3) { sendErr(seq, "PWM:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "PWM:BAD_PIN"); return; }

        int duty1000 = constrain(toks[2].toInt(), 0, 1000);
        // Map 0..1000 to 0..255 for Arduino analogWrite
        int duty8 = (int)((float)duty1000 * 255.0f / 1000.0f);
        analogWrite(pin, duty8);
        if (pin < 64) { pwmDutyTable[pin] = (uint16_t)duty1000; pwmFreqTable[pin] = 0; }

        sendDone(seq, "PWM:" + toks[1] + ":" + String(duty1000));
        return;
    }

    // ----- FREQ (custom frequency + duty) -----
    if (sub == "FREQ") {
        if (n < 4) { sendErr(seq, "PWM:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "PWM:BAD_PIN"); return; }

        uint32_t hz = (uint32_t)toks[2].toInt();
        if (hz < 1 || hz > 1000000UL) { sendErr(seq, "PWM:BAD_FREQ"); return; }

        int duty1000 = constrain(toks[3].toInt(), 0, 1000);
        int duty8    = (int)((float)duty1000 * 255.0f / 1000.0f);

        analogWriteFrequency(hz);
        analogWrite(pin, duty8);
        if (pin < 64) { pwmDutyTable[pin] = (uint16_t)duty1000; pwmFreqTable[pin] = hz; }

        char buf[32];
        snprintf(buf, sizeof(buf), "PWM:%s:%luHz:%d‰",
                 toks[1].c_str(), (unsigned long)hz, duty1000);
        sendDone(seq, String(buf));
        return;
    }

    // ----- STOP -----
    if (sub == "STOP") {
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "PWM:BAD_PIN"); return; }
        analogWrite(pin, 0);
        digitalWrite(pin, LOW);
        if (pin < 64) { pwmDutyTable[pin] = 0; pwmFreqTable[pin] = 0; }
        sendDone(seq, "STOPPED:" + toks[1]);
        return;
    }

    // ----- READ (duty) -----
    if (sub == "READ") {
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "PWM:BAD_PIN"); return; }
        uint16_t duty = (pin < 64) ? pwmDutyTable[pin] : 0;
        sendDone(seq, String(duty));
        return;
    }

    // ----- FREQREAD (last set frequency in Hz, 0 = default) -----
    if (sub == "FREQREAD") {
        toks[1].toUpperCase();
        int pin = parsePin(toks[1]);
        if (pin < 0) { sendErr(seq, "PWM:BAD_PIN"); return; }
        uint32_t hz = (pin < 64) ? pwmFreqTable[pin] : 0;
        sendDone(seq, String(hz));
        return;
    }

    sendErr(seq, "PWM:UNKNOWN_SUB");
}
