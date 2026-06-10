/**
 * stm32_slave — universal agent firmware for STM32F103C8T6 "Blue Pill"
 *
 * Listens on USART1 (PA9/PA10) at 115200 8N1 for a line-based ASCII protocol
 * from the ESP32 master and executes commands on the Blue Pill hardware.
 *
 * Protocol frame format:  TYPE:SEQ:DATA\n
 *   SEQ  = 3-digit zero-padded sequence number (001..999, wraps to 001)
 *   DATA = command-specific payload (may be empty)
 *
 * Board: STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8
 *
 * Wiring:
 *   STM32 PA10 (USART1 RX) <---- ESP32 GPIO17 (TX2)
 *   STM32 PA9  (USART1 TX) ----> ESP32 GPIO16 (RX2)
 *   STM32 3.3V               --- ESP32 3.3V   (NEVER 5V!)
 *   STM32 GND                --- ESP32 GND
 *
 * PC13 is the onboard LED and is ACTIVE-LOW: LOW = on, HIGH = off.
 */

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Hardware constants
// ---------------------------------------------------------------------------

// PC13 is active-low on every Blue Pill clone.
static const int LED_PIN = PC13;
static const long UART_BAUD = 115200;

// Firmware version reported by STATUS
static const char FW_VERSION[] = "1.0";

// ---------------------------------------------------------------------------
// Pin helper — parse "A0".."A15", "B0".."B15", "C13".."C15" -> pin number
// Returns -1 on parse failure.
// STM32duino pin numbering for GenF1: PA0=0..PA15=15, PB0=16..PB15=31,
// PC0=32..PC15=47.
// ---------------------------------------------------------------------------

static int parsePin(const String& token) {
    if (token.length() < 2) return -1;

    char port = (char)toupper((unsigned char)token.charAt(0));
    if (port < 'A' || port > 'C') return -1;

    // Pin number is everything after the first character
    String numStr = token.substring(1);
    for (int i = 0; i < (int)numStr.length(); i++) {
        if (!isDigit((unsigned char)numStr.charAt(i))) return -1;
    }

    int num = numStr.toInt();
    if (num < 0 || num > 15) return -1;

    // Compute flat pin index: A->0, B->1, C->2
    return (port - 'A') * 16 + num;
}

// ---------------------------------------------------------------------------
// Non-blocking blink engine
// ---------------------------------------------------------------------------

static unsigned long blinkPeriodMs = 0;  // 0 = blink off
static unsigned long lastBlinkMs   = 0;
static bool          blinkState    = false;

static void blinkTick() {
    if (blinkPeriodMs == 0) return;
    unsigned long now = millis();
    if (now - lastBlinkMs >= blinkPeriodMs / 2) {
        lastBlinkMs = now;
        blinkState  = !blinkState;
        // LED is active-low
        digitalWrite(LED_PIN, blinkState ? LOW : HIGH);
    }
}

static void blinkStop() {
    blinkPeriodMs = 0;
    // Leave LED in whatever state it is; caller may set it explicitly
}

// ---------------------------------------------------------------------------
// Protocol state — last completed command (for POLL replay)
// ---------------------------------------------------------------------------

static String lastCompletedSeq    = "";
static String lastCompletedResult = "";

// ---------------------------------------------------------------------------
// Line buffer (transport layer)
// ---------------------------------------------------------------------------

static String rxBuf = "";

// ---------------------------------------------------------------------------
// Frame sender helpers
// ---------------------------------------------------------------------------

static void sendLine(const String& line) {
    // Serial1 is USART1 (PA9/PA10) in STM32duino GenF1
    Serial1.println(line);
}

static void sendRecv(const String& seq) {
    sendLine("RECV:" + seq);
}

static void sendDone(const String& seq, const String& result) {
    lastCompletedSeq    = seq;
    lastCompletedResult = result;
    sendLine("DONE:" + seq + ":" + result);
}

static void sendErr(const String& seq, const String& reason) {
    sendLine("ERR:" + seq + ":" + reason);
}

// ---------------------------------------------------------------------------
// Command handlers — all must return quickly (never block loop)
// ---------------------------------------------------------------------------

static void handleLed(const String& seq, const String& arg) {
    if (arg == "ON") {
        blinkStop();
        digitalWrite(LED_PIN, LOW);   // active-low: LOW = on
        sendDone(seq, "ON");
    } else if (arg == "OFF") {
        blinkStop();
        digitalWrite(LED_PIN, HIGH);  // active-low: HIGH = off
        sendDone(seq, "OFF");
    } else if (arg == "STATUS") {
        // Read current LED state from the pin
        bool isOn = (digitalRead(LED_PIN) == LOW);
        sendDone(seq, isOn ? "ON" : "OFF");
    } else {
        sendErr(seq, "BAD_ARG");
    }
}

static void handleBlink(const String& seq, const String& arg) {
    // arg = milliseconds for full period; "0" = stop
    unsigned long ms = (unsigned long)arg.toInt();
    if (ms == 0) {
        blinkStop();
        sendDone(seq, "BLINK_OFF");
    } else if (ms < 20) {
        // Reject unreasonably fast blink that would thrash the pin
        sendErr(seq, "BAD_VALUE");
    } else {
        blinkPeriodMs = ms;
        lastBlinkMs   = millis();
        blinkState    = false;
        // LED starts off at the beginning of the blink cycle
        digitalWrite(LED_PIN, HIGH);
        sendDone(seq, "BLINK_ON:" + String(ms) + "ms");
    }
}

static void handleAdc(const String& seq, const String& arg) {
    // Accept a numeric channel (0=PA0, 1=PA1, ...) OR a pin token (A0, A1, ...)
    int pin = -1;

    if (isDigit((unsigned char)arg.charAt(0))) {
        int ch = arg.toInt();
        // Map numeric channel to PA<ch>
        if (ch >= 0 && ch <= 9) {
            pin = ch; // PA0..PA9 = pin 0..9 in STM32duino GenF1 layout
        }
    } else {
        pin = parsePin(arg);
    }

    if (pin < 0) {
        sendErr(seq, "BAD_PIN");
        return;
    }

    pinMode(pin, INPUT_ANALOG);
    int value = analogRead(pin);
    sendDone(seq, String(value));
}

static void handlePwm(const String& seq, const String& pinToken, const String& valStr) {
    int pin = parsePin(pinToken);
    if (pin < 0) {
        sendErr(seq, "BAD_PIN");
        return;
    }

    int val = valStr.toInt();
    if (val < 0 || val > 255) {
        sendErr(seq, "BAD_VALUE");
        return;
    }

    // Note: not every pin supports PWM on the Blue Pill — the caller should
    // use known PWM-capable pins (PA8, PB0, PB1, PA0, PA1, PA2, PA3...).
    // analogWrite on a non-PWM pin silently falls back to digital behaviour
    // in some STM32duino versions; there is no portable way to test at runtime
    // without the variant's pwm table.
    analogWrite(pin, val);
    sendDone(seq, "PWM:" + pinToken + ":" + String(val));
}

static void handleGpioSet(const String& seq, const String& pinToken, const String& valStr) {
    int pin = parsePin(pinToken);
    if (pin < 0) {
        sendErr(seq, "BAD_PIN");
        return;
    }

    int val = valStr.toInt();
    if (val != 0 && val != 1) {
        sendErr(seq, "BAD_VALUE");
        return;
    }

    pinMode(pin, OUTPUT);
    digitalWrite(pin, val ? HIGH : LOW);
    sendDone(seq, "SET:" + pinToken + ":" + String(val));
}

static void handleGpioGet(const String& seq, const String& pinToken) {
    int pin = parsePin(pinToken);
    if (pin < 0) {
        sendErr(seq, "BAD_PIN");
        return;
    }

    pinMode(pin, INPUT);
    int val = digitalRead(pin);
    sendDone(seq, String(val));
}

static void handleStatus(const String& seq) {
    String msg = "STM32F103C8 slave v";
    msg += FW_VERSION;
    msg += " uptime:";
    msg += String(millis());
    msg += "ms";
    sendDone(seq, msg);
}

// ---------------------------------------------------------------------------
// Frame dispatcher — receives a fully parsed TYPE, SEQ, and DATA payload
// ---------------------------------------------------------------------------

static void dispatch(const String& type, const String& seq, const String& data) {
    // --- Protocol control ---
    if (type == "PING") {
        sendLine("PONG");
        return;
    }

    if (type == "RESET") {
        // Clear all protocol state
        lastCompletedSeq    = "";
        lastCompletedResult = "";
        blinkStop();
        sendLine("RESET:ACK");
        return;
    }

    if (type == "FREE") {
        // Master acknowledged DONE for SEQ; we can discard it.
        // We keep only the single last result so just leave it;
        // no reply needed per spec.
        return;
    }

    if (type == "POLL") {
        if (seq == lastCompletedSeq && lastCompletedSeq.length() > 0) {
            // Re-send the stored DONE so a lost reply is recoverable
            sendLine("DONE:" + seq + ":" + lastCompletedResult);
        } else {
            // Still running (or seq unknown) — report busy
            sendLine("BUSY:" + seq);
        }
        return;
    }

    if (type != "SEND") {
        sendErr(seq, "UNKNOWN_FRAME");
        return;
    }

    // --- SEND:NNN:CMD[:args...] ---
    // Acknowledge receipt immediately before doing any work
    sendRecv(seq);

    // Split DATA on ':' to extract command and optional arguments
    String cmd  = data;
    String arg1 = "";
    String arg2 = "";

    int c1 = data.indexOf(':');
    if (c1 >= 0) {
        cmd  = data.substring(0, c1);
        String rest = data.substring(c1 + 1);
        int c2 = rest.indexOf(':');
        if (c2 >= 0) {
            arg1 = rest.substring(0, c2);
            arg2 = rest.substring(c2 + 1);
        } else {
            arg1 = rest;
        }
    }

    cmd.trim();
    arg1.trim();
    arg2.trim();

    // Dispatch on CMD (protocol uses uppercase; the master always sends uppercase)
    if      (cmd == "LED")    { handleLed(seq, arg1); }
    else if (cmd == "BLINK")  { handleBlink(seq, arg1); }
    else if (cmd == "ADC")    { handleAdc(seq, arg1); }
    else if (cmd == "PWM")    { handlePwm(seq, arg1, arg2); }
    else if (cmd == "STATUS") { handleStatus(seq); }
    else if (cmd == "GPIO") {
        if      (arg1 == "SET") { handleGpioSet(seq, arg2, ""); }  // needs 3rd field
        else if (arg1 == "GET") { handleGpioGet(seq, arg2); }
        else                    { sendErr(seq, "BAD_ARG"); }
    }
    else {
        sendErr(seq, "UNKNOWN_CMD");
    }
}

// GPIO:SET and GPIO:GET need a dedicated re-parse because they carry
// three sub-fields after SEND:NNN:  GPIO:SET:B5:1  or  GPIO:GET:B5
// The generic dispatch above only splits DATA into cmd/arg1/arg2 which
// works for LED:ON, PWM:A8:128 but not for GPIO:SET:B5:1 (four colons total).
// Re-parse DATA specifically for GPIO here.

static void dispatchGpio(const String& seq, const String& data) {
    // data = "SET:B5:1" or "GET:B5"
    int c1 = data.indexOf(':');
    if (c1 < 0) { sendErr(seq, "BAD_ARG"); return; }

    String op  = data.substring(0, c1);
    String rest = data.substring(c1 + 1);

    if (op == "SET") {
        int c2 = rest.indexOf(':');
        if (c2 < 0) { sendErr(seq, "BAD_ARG"); return; }
        String pin = rest.substring(0, c2);
        String val = rest.substring(c2 + 1);
        handleGpioSet(seq, pin, val);
    } else if (op == "GET") {
        handleGpioGet(seq, rest);
    } else {
        sendErr(seq, "BAD_ARG");
    }
}

// ---------------------------------------------------------------------------
// Full frame parser — called once a complete \n-terminated line is received
// ---------------------------------------------------------------------------

static void processLine(const String& raw) {
    String line = raw;
    line.trim();
    if (line.length() == 0) return;

    // Special case: bare PING / RESET (allowed without SEQ per spec)
    if (line == "PING") { sendLine("PONG"); return; }
    if (line == "RESET") {
        lastCompletedSeq    = "";
        lastCompletedResult = "";
        blinkStop();
        sendLine("RESET:ACK");
        return;
    }

    // General frame: TYPE:SEQ:DATA or TYPE:SEQ (no data)
    int c1 = line.indexOf(':');
    if (c1 < 0) {
        sendErr("000", "MALFORMED");
        return;
    }

    String type = line.substring(0, c1);
    String rest = line.substring(c1 + 1);

    // Extract SEQ (next colon-separated field, must be 1-3 digits)
    int c2 = rest.indexOf(':');
    String seq;
    String data;
    if (c2 >= 0) {
        seq  = rest.substring(0, c2);
        data = rest.substring(c2 + 1);
    } else {
        seq  = rest;
        data = "";
    }

    seq.trim();
    data.trim();

    // Validate SEQ: must be 1-3 digit string
    for (int i = 0; i < (int)seq.length(); i++) {
        if (!isDigit((unsigned char)seq.charAt(i))) {
            sendErr("000", "BAD_SEQ");
            return;
        }
    }

    // GPIO needs special handling due to extra colon in payload (GPIO:SET:PIN:VAL)
    if (type == "SEND") {
        // Peek at the command inside DATA to route GPIO differently
        String cmd = data;
        int dc = data.indexOf(':');
        if (dc >= 0) cmd = data.substring(0, dc);

        if (cmd == "GPIO") {
            sendRecv(seq);
            String gpioData = (dc >= 0) ? data.substring(dc + 1) : "";
            dispatchGpio(seq, gpioData);
            return;
        }
    }

    dispatch(type, seq, data);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    // Onboard LED — configure early so it doesn't float
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // active-low: HIGH = off at startup

    // STM32duino GenF1: Serial1 maps to USART1 (PA9=TX, PA10=RX).
    // Application mode uses 8N1; the bootloader used 8E1 — do NOT mix them.
    Serial1.begin(UART_BAUD);

    // 12-bit ADC resolution (STM32F1 native; Arduino default is 10-bit)
    analogReadResolution(12);

    // Brief LED flash to confirm firmware booted (visible even without a console)
    digitalWrite(LED_PIN, LOW);   delay(120);
    digitalWrite(LED_PIN, HIGH);  delay(120);
    digitalWrite(LED_PIN, LOW);   delay(120);
    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    // --- Transport: accumulate bytes into rxBuf until \n ---
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            processLine(rxBuf);
            rxBuf = "";
        } else if (c != '\r') {
            // Guard against runaway input — drop oversized lines
            if (rxBuf.length() < 128) {
                rxBuf += c;
            }
        }
    }

    // --- Hardware tick: non-blocking blink engine ---
    blinkTick();
}
