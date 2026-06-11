#pragma once
/**
 * cmd_u2.h — USART2 passthrough for stm32_slave
 *
 * USART2 pins (STM32duino GenF1):
 *   PA2 = TX2,  PA3 = RX2
 *
 * USART1 (PA9/PA10) is used for master communication and MUST NOT be touched.
 * USART3 (PB10/PB11) is available for future use via U3: prefix (not implemented here).
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   U2:CFG:BAUD[:BITS:PARITY:STOP]   configure USART2
 *                                      BAUD = 1200..2000000
 *                                      BITS = 7|8|9  (optional, default 8)
 *                                      PARITY = N|E|O (optional, default N)
 *                                      STOP = 1|2    (optional, default 1)
 *   U2:TX:HEX                         transmit bytes (hex string)
 *   U2:RX:N[:TIMEOUT_MS]              receive up to N bytes, optional timeout
 *                                      → hex string or NONE
 *   U2:FLUSH                          flush RX buffer
 *   U2:STATUS                         bytes available in RX buffer → decimal
 *   U2:CLOSE                          end USART2
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static bool u2Active = false;

static void handleU2(const String& seq, const String& args) {
    String toks[6];
    int n = splitTokens(args, ':', toks, 6);
    if (n < 1) { sendErr(seq, "U2:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- CFG -----
    if (sub == "CFG") {
        if (n < 2) { sendErr(seq, "U2:MISSING_BAUD"); return; }

        long baud = toks[1].toInt();
        if (baud < 1200 || baud > 2000000L) { sendErr(seq, "U2:BAD_BAUD"); return; }

        // Optional: bits, parity, stop
        int bits   = (n >= 3) ? toks[2].toInt() : 8;
        String par = (n >= 4) ? toks[3]         : "N";
        int stop   = (n >= 5) ? toks[4].toInt() : 1;

        uint8_t config = SERIAL_8N1;

        if      (bits == 8 && par == "N" && stop == 1) config = SERIAL_8N1;
        else if (bits == 8 && par == "N" && stop == 2) config = SERIAL_8N2;
        else if (bits == 8 && par == "E" && stop == 1) config = SERIAL_8E1;
        else if (bits == 8 && par == "O" && stop == 1) config = SERIAL_8O1;
        else if (bits == 7 && par == "E" && stop == 1) config = SERIAL_7E1;
        else if (bits == 7 && par == "O" && stop == 1) config = SERIAL_7O1;
        else { sendErr(seq, "U2:BAD_FORMAT"); return; }

        if (u2Active) Serial2.end();
        Serial2.begin(baud, config);
        u2Active = true;

        char buf[32];
        snprintf(buf, sizeof(buf), "OK:%ld_%dB%s%d", (long)baud, bits, par.c_str(), stop);
        sendDone(seq, String(buf));
        return;
    }

    if (!u2Active) { sendErr(seq, "U2:NOT_CONFIGURED"); return; }

    // ----- TX -----
    if (sub == "TX") {
        if (n < 2 || toks[1].length() == 0) { sendErr(seq, "U2:MISSING_DATA"); return; }
        uint8_t buf[64];
        int len = hexToBytes(toks[1], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "U2:BAD_HEX"); return; }
        size_t written = Serial2.write(buf, (size_t)len);
        sendDone(seq, "TX:" + String((int)written) + "B");
        return;
    }

    // ----- RX -----
    if (sub == "RX") {
        if (n < 2) { sendErr(seq, "U2:MISSING_N"); return; }
        int maxBytes = constrain(toks[1].toInt(), 1, 64);
        int timeoutMs = (n >= 3) ? toks[2].toInt() : 50;

        uint8_t buf[64];
        int received = 0;
        unsigned long deadline = millis() + (unsigned long)timeoutMs;

        while (received < maxBytes && millis() < deadline) {
            if (Serial2.available()) {
                buf[received++] = (uint8_t)Serial2.read();
            }
        }

        if (received == 0) {
            sendDone(seq, "NONE");
        } else {
            sendDone(seq, bytesToHex(buf, received));
        }
        return;
    }

    // ----- FLUSH -----
    if (sub == "FLUSH") {
        while (Serial2.available()) Serial2.read();
        sendDone(seq, "OK");
        return;
    }

    // ----- STATUS -----
    if (sub == "STATUS") {
        sendDone(seq, String(Serial2.available()));
        return;
    }

    // ----- CLOSE -----
    if (sub == "CLOSE") {
        Serial2.end();
        u2Active = false;
        sendDone(seq, "OK");
        return;
    }

    sendErr(seq, "U2:UNKNOWN_SUB");
}
