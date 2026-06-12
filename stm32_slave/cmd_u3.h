#pragma once
/**
 * cmd_u3.h — USART3 passthrough for stm32_slave
 *
 * Pins: PB10 = TX3,  PB11 = RX3
 *
 * ⚠ CONFLICT: PB10/PB11 are also I2C2 SCL/SDA.
 *   Do NOT use U3 and I2C2 simultaneously — they share the same pins.
 *   Call U3:CLOSE before using I2C2, and vice versa.
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   U3:CFG:BAUD[:BITS:PARITY:STOP]
 *   U3:TX:HEX
 *   U3:RX:N[:TIMEOUT_MS]
 *   U3:FLUSH
 *   U3:STATUS
 *   U3:CLOSE
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static bool u3Active = false;

static void handleU3(const String& seq, const String& args) {
    String toks[6];
    int n = splitTokens(args, ':', toks, 6);
    if (n < 1) { sendErr(seq, "U3:BAD_ARGS"); return; }

    const String& sub = toks[0];

    if (sub == "CFG") {
        if (n < 2) { sendErr(seq, "U3:MISSING_BAUD"); return; }
        long baud = toks[1].toInt();
        if (baud < 1200 || baud > 2000000L) { sendErr(seq, "U3:BAD_BAUD"); return; }

        int bits   = (n >= 3) ? toks[2].toInt() : 8;
        String par = (n >= 4) ? toks[3]         : "N";
        int stop   = (n >= 5) ? toks[4].toInt() : 1;

        uint8_t config = SERIAL_8N1;
        if      (bits==8 && par=="N" && stop==1) config = SERIAL_8N1;
        else if (bits==8 && par=="N" && stop==2) config = SERIAL_8N2;
        else if (bits==8 && par=="E" && stop==1) config = SERIAL_8E1;
        else if (bits==8 && par=="O" && stop==1) config = SERIAL_8O1;
        else if (bits==7 && par=="E" && stop==1) config = SERIAL_7E1;
        else if (bits==7 && par=="O" && stop==1) config = SERIAL_7O1;
        else { sendErr(seq, "U3:BAD_FORMAT"); return; }

        if (u3Active) Serial3.end();
        Serial3.begin(baud, config);
        u3Active = true;

        char buf[32];
        snprintf(buf, sizeof(buf), "OK:%ld_%dB%s%d", (long)baud, bits, par.c_str(), stop);
        sendDone(seq, String(buf));
        return;
    }

    if (!u3Active) { sendErr(seq, "U3:NOT_CONFIGURED"); return; }

    if (sub == "TX") {
        if (n < 2) { sendErr(seq, "U3:MISSING_DATA"); return; }
        uint8_t buf[64];
        int len = hexToBytes(toks[1], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "U3:BAD_HEX"); return; }
        Serial3.write(buf, (size_t)len);
        sendDone(seq, "TX:" + String(len) + "B");
        return;
    }

    if (sub == "RX") {
        if (n < 2) { sendErr(seq, "U3:MISSING_N"); return; }
        int maxB = constrain(toks[1].toInt(), 1, 64);
        int tms  = (n >= 3) ? toks[2].toInt() : 50;

        uint8_t buf[64]; int got = 0;
        unsigned long dl = millis() + (unsigned long)tms;
        while (got < maxB && millis() < dl) {
            if (Serial3.available()) buf[got++] = Serial3.read();
        }
        sendDone(seq, got ? bytesToHex(buf, got) : "NONE");
        return;
    }

    if (sub == "FLUSH")  { while (Serial3.available()) Serial3.read(); sendDone(seq, "OK"); return; }
    if (sub == "STATUS") { sendDone(seq, String(Serial3.available())); return; }
    if (sub == "CLOSE")  { Serial3.end(); u3Active = false; sendDone(seq, "OK"); return; }

    sendErr(seq, "U3:UNKNOWN_SUB");
}
