/**
 * esp32_master — command console + protocol master for the ESP32 DevKit
 *
 * Accepts human commands on the USB Serial console (115200 8N1) and
 * translates them into the line-based ASCII protocol spoken over UART2
 * (GPIO16/GPIO17) to the STM32 Blue Pill slave.
 *
 * *** Serial Monitor line ending MUST be set to "Newline" (not CR, not Both).
 *     The master reads lines with readStringUntil('\n') and the command is
 *     silently dropped if no '\n' is received. ***
 *
 * UART GPIO assignment:
 *   GPIO17 (TX2) -----> STM32 PA10 (USART1 RX)
 *   GPIO16 (RX2) <----- STM32 PA9  (USART1 TX)
 *
 * These are NOT the pins silkscreened "TX" and "RX" on most DevKit boards —
 * those are GPIO1/GPIO3 which belong to the USB console (Serial).
 *
 * Board: esp32:esp32:esp32
 */

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Hardware & baud
// ---------------------------------------------------------------------------

static const int  STM_RX_PIN  = 16;
static const int  STM_TX_PIN  = 17;
static const long BAUD        = 115200;

// UART2 to the slave (8N1 for application firmware; flasher uses 8E1)
HardwareSerial STM(2);

// ---------------------------------------------------------------------------
// Protocol constants (must match stm32_slave)
// ---------------------------------------------------------------------------

static const unsigned long TIMEOUT_RECV_MS  = 300;   // wait for RECV after SEND
static const unsigned long TIMEOUT_DONE_MS  = 2000;  // wait for DONE after RECV
static const unsigned long TIMEOUT_POLL_MS  = 400;   // interval between POLLs
static const int           MAX_RETRY        = 3;
static const int           MAX_POLLS        = 15;

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

enum class State {
    IDLE,
    WAIT_RECV,   // sent SEND, waiting for RECV from slave
    WAIT_DONE,   // got RECV, waiting for DONE
    POLLING,     // timed out waiting for DONE, sending periodic POLL
};

static State         state       = State::IDLE;
static int           currentSeq  = 1;   // 1..999, wraps to 1
static String        pendingCmd  = "";  // protocol CMD sent to slave
static unsigned long stateTs     = 0;   // millis() when we entered current state
static unsigned long lastPollTs  = 0;   // millis() of last POLL send
static int           retryCount  = 0;
static int           pollCount   = 0;

// ---------------------------------------------------------------------------
// Sequence number helpers
// ---------------------------------------------------------------------------

static String seqStr(int n) {
    // 3-digit zero-padded
    char buf[4];
    snprintf(buf, sizeof(buf), "%03d", n);
    return String(buf);
}

static void advanceSeq() {
    currentSeq++;
    if (currentSeq > 999) currentSeq = 1;
}

// ---------------------------------------------------------------------------
// Line buffer for slave replies
// ---------------------------------------------------------------------------

static String stmRxBuf = "";

// ---------------------------------------------------------------------------
// Console output helpers
// ---------------------------------------------------------------------------

static void printBanner() {
    Serial.println();
    Serial.println("===================================================");
    Serial.println("  Supermikrokontroler — ESP32 master v1.0");
    Serial.println("  Slave: STM32F103C8T6 Blue Pill on UART2");
    Serial.println("===================================================");
    Serial.println("Type 'help' for commands.");
    Serial.println();
}

static void printHelp() {
    Serial.println("Commands (case-insensitive):");
    Serial.println("  ping              -- test link to slave");
    Serial.println("  led on            -- turn slave LED on");
    Serial.println("  led off           -- turn slave LED off");
    Serial.println("  led status        -- read slave LED state");
    Serial.println("  blink <ms>        -- blink LED at <ms> period (0 = stop)");
    Serial.println("  adc <ch>          -- read ADC channel (0=PA0, 1=PA1, or pin token)");
    Serial.println("  pwm <pin> <v>     -- analogWrite pin 0..255 (e.g. pwm A8 128)");
    Serial.println("  gpio set <P> <v>  -- digitalWrite pin P to 0 or 1");
    Serial.println("  gpio get <P>      -- digitalRead pin P");
    Serial.println("  status            -- slave firmware info");
    Serial.println("  reset             -- resynchronize protocol");
    Serial.println("  help              -- this message");
    Serial.println();
    Serial.println("Pin tokens: A0..A15, B0..B15, C13..C15");
    Serial.println("NOTE: Serial Monitor line ending must be 'Newline'.");
    Serial.println();
}

static void logTx(const String& frame) {
    Serial.print("--> ");
    Serial.println(frame);
}

static void logRx(const String& frame) {
    Serial.print("<-- ");
    Serial.println(frame);
}

static void logOk(const String& msg) {
    Serial.print("[OK] ");
    Serial.println(msg);
}

static void logErr(const String& msg) {
    Serial.print("[ERR] ");
    Serial.println(msg);
}

// ---------------------------------------------------------------------------
// Send a frame to the slave
// ---------------------------------------------------------------------------

static void sendFrame(const String& frame) {
    logTx(frame);
    STM.println(frame);
}

// ---------------------------------------------------------------------------
// Human command -> protocol CMD translation
// ---------------------------------------------------------------------------

// Returns the protocol CMD string (e.g. "LED:ON") or "" for local-only commands.
// Prints an error and returns "" on bad syntax.
static String parseHumanCommand(const String& raw) {
    String input = raw;
    input.trim();
    input.toLowerCase();

    if (input.length() == 0) return "";

    // Split on whitespace into tokens
    String tokens[6];
    int    ntok = 0;
    int    i = 0;
    while (i < (int)input.length() && ntok < 6) {
        while (i < (int)input.length() && input.charAt(i) == ' ') i++;
        if (i >= (int)input.length()) break;
        int start = i;
        while (i < (int)input.length() && input.charAt(i) != ' ') i++;
        tokens[ntok++] = input.substring(start, i);
    }
    if (ntok == 0) return "";

    // Local-only commands
    if (tokens[0] == "help") {
        printHelp();
        return "";
    }

    // ping → bare PING (no SEQ per spec)
    if (tokens[0] == "ping") {
        return "__PING__";  // special sentinel handled in startCommand()
    }

    if (tokens[0] == "led") {
        if (ntok < 2) { logErr("Usage: led on|off|status"); return ""; }
        String sub = tokens[1];
        if      (sub == "on")     return "LED:ON";
        else if (sub == "off")    return "LED:OFF";
        else if (sub == "status") return "LED:STATUS";
        else { logErr("Unknown led sub-command: " + sub); return ""; }
    }

    if (tokens[0] == "blink") {
        if (ntok < 2) { logErr("Usage: blink <ms>"); return ""; }
        return "BLINK:" + tokens[1];
    }

    if (tokens[0] == "adc") {
        if (ntok < 2) { logErr("Usage: adc <channel>"); return ""; }
        // Uppercase pin token if it looks like a letter+digits (e.g. a0 -> A0)
        String ch = tokens[1];
        ch.toUpperCase();
        return "ADC:" + ch;
    }

    if (tokens[0] == "pwm") {
        if (ntok < 3) { logErr("Usage: pwm <pin> <value 0-255>"); return ""; }
        String pin = tokens[1]; pin.toUpperCase();
        return "PWM:" + pin + ":" + tokens[2];
    }

    if (tokens[0] == "gpio") {
        if (ntok < 2) { logErr("Usage: gpio set <pin> <0|1>  OR  gpio get <pin>"); return ""; }
        if (tokens[1] == "set") {
            if (ntok < 4) { logErr("Usage: gpio set <pin> <0|1>"); return ""; }
            String pin = tokens[2]; pin.toUpperCase();
            return "GPIO:SET:" + pin + ":" + tokens[3];
        } else if (tokens[1] == "get") {
            if (ntok < 3) { logErr("Usage: gpio get <pin>"); return ""; }
            String pin = tokens[2]; pin.toUpperCase();
            return "GPIO:GET:" + pin;
        } else {
            logErr("Usage: gpio set|get ...");
            return "";
        }
    }

    if (tokens[0] == "status") {
        return "STATUS";
    }

    if (tokens[0] == "reset") {
        return "__RESET__";  // special sentinel
    }

    logErr("Unknown command '" + tokens[0] + "'. Type 'help'.");
    return "";
}

// ---------------------------------------------------------------------------
// Start a new protocol transaction
// ---------------------------------------------------------------------------

static void startCommand(const String& cmd) {
    if (state != State::IDLE) {
        logErr("Still waiting for previous command. Try 'reset'.");
        return;
    }

    // --- PING: bare frame, no SEQ ---
    if (cmd == "__PING__") {
        sendFrame("PING");
        // PING is answered directly with PONG; use WAIT_RECV with a tight timeout.
        // We reuse the RECV wait since PONG ≈ RECV semantically (quick reply).
        pendingCmd  = "__PING__";
        stateTs     = millis();
        retryCount  = 0;
        state       = State::WAIT_RECV;
        return;
    }

    // --- RESET: bare frame ---
    if (cmd == "__RESET__") {
        sendFrame("RESET");
        state      = State::IDLE;
        retryCount = 0;
        pollCount  = 0;
        logOk("Protocol reset sent.");
        return;
    }

    // --- Normal SEND:NNN:CMD ---
    pendingCmd = cmd;
    retryCount = 0;
    String frame = "SEND:" + seqStr(currentSeq) + ":" + cmd;
    sendFrame(frame);
    state   = State::WAIT_RECV;
    stateTs = millis();
}

// ---------------------------------------------------------------------------
// Process a line received from the slave
// ---------------------------------------------------------------------------

static void handleSlaveReply(const String& raw) {
    String line = raw;
    line.trim();
    if (line.length() == 0) return;

    logRx(line);

    // Parse TYPE:SEQ:DATA (SEQ and DATA optional for bare frames)
    String type = line;
    String seq  = "";
    String data = "";
    int c1 = line.indexOf(':');
    if (c1 >= 0) {
        type = line.substring(0, c1);
        String rest = line.substring(c1 + 1);
        int c2 = rest.indexOf(':');
        if (c2 >= 0) {
            seq  = rest.substring(0, c2);
            data = rest.substring(c2 + 1);
        } else {
            seq = rest;
        }
    }

    // PONG — reply to PING
    if (type == "PONG") {
        if (state == State::WAIT_RECV && pendingCmd == "__PING__") {
            logOk("PONG received — link is alive.");
            state = State::IDLE;
        }
        return;
    }

    // RECV — slave acknowledged our SEND
    if (type == "RECV") {
        if (state == State::WAIT_RECV && seq == seqStr(currentSeq)) {
            state   = State::WAIT_DONE;
            stateTs = millis();
        }
        return;
    }

    // DONE — command completed
    if (type == "DONE") {
        if ((state == State::WAIT_DONE || state == State::POLLING)
            && seq == seqStr(currentSeq))
        {
            String freeFrame = "FREE:" + seq;
            sendFrame(freeFrame);
            logOk(data.length() > 0 ? data : "(empty result)");
            advanceSeq();
            state = State::IDLE;
        }
        return;
    }

    // BUSY — slave still working (reply to our POLL)
    if (type == "BUSY") {
        // Nothing to do; next POLL will be sent by the timeout tick
        return;
    }

    // ERR — slave reported an error
    if (type == "ERR") {
        logErr("Slave error: " + data);
        // Still need to free the slot if SEQ matches
        if (seq == seqStr(currentSeq)) {
            String freeFrame = "FREE:" + seq;
            sendFrame(freeFrame);
            advanceSeq();
            state = State::IDLE;
        }
        return;
    }

    // Unexpected frame — log it but don't crash
    Serial.print("[??] Unexpected: ");
    Serial.println(line);
}

// ---------------------------------------------------------------------------
// Timeout and retry tick — called every loop iteration
// ---------------------------------------------------------------------------

static void stateTick() {
    unsigned long now = millis();

    if (state == State::WAIT_RECV) {
        if (now - stateTs >= TIMEOUT_RECV_MS) {
            if (pendingCmd == "__PING__") {
                // PING not answered
                logErr("PING timed out — no PONG.");
                state = State::IDLE;
                return;
            }
            // SEND not acknowledged — retry
            retryCount++;
            if (retryCount > MAX_RETRY) {
                logErr("RECV timeout after " + String(MAX_RETRY) + " retries. Use 'reset'.");
                state = State::IDLE;
                return;
            }
            // Retransmit with the same SEQ
            String frame = "SEND:" + seqStr(currentSeq) + ":" + pendingCmd;
            Serial.print("[RETRY ");
            Serial.print(retryCount);
            Serial.print("] ");
            sendFrame(frame);
            stateTs = now;
        }
        return;
    }

    if (state == State::WAIT_DONE) {
        if (now - stateTs >= TIMEOUT_DONE_MS) {
            // Switch to polling mode
            state       = State::POLLING;
            pollCount   = 0;
            lastPollTs  = now - TIMEOUT_POLL_MS;  // trigger first POLL immediately
        }
        return;
    }

    if (state == State::POLLING) {
        if (now - lastPollTs >= TIMEOUT_POLL_MS) {
            pollCount++;
            if (pollCount > MAX_POLLS) {
                logErr("Slave did not complete after " + String(MAX_POLLS) + " polls. Use 'reset'.");
                state = State::IDLE;
                return;
            }
            String frame = "POLL:" + seqStr(currentSeq);
            sendFrame(frame);
            lastPollTs = now;
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(BAUD);
    // Wait for USB serial to come up (important on ESP32 with USB CDC)
    while (!Serial && millis() < 3000) {}

    // UART2 to slave — 8N1 for application firmware
    // See esp32_flasher.ino for 8E1 used during ROM bootloader flashing
    STM.begin(BAUD, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);

    printBanner();
}

void loop() {
    // --- Read lines from slave ---
    while (STM.available()) {
        char c = (char)STM.read();
        if (c == '\n') {
            handleSlaveReply(stmRxBuf);
            stmRxBuf = "";
        } else if (c != '\r') {
            if (stmRxBuf.length() < 128) stmRxBuf += c;
        }
    }

    // --- Read command from USB console ---
    // readStringUntil blocks only until the '\n' arrives or times out (0 = no
    // wait). We check Serial.available() first to avoid blocking at all.
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() > 0) {
            Serial.print("> ");
            Serial.println(input);
            String cmd = parseHumanCommand(input);
            if (cmd.length() > 0) {
                startCommand(cmd);
            }
        }
    }

    // --- State machine timeout / retry tick ---
    stateTick();
}
