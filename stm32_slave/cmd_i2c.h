#pragma once
/**
 * cmd_i2c.h — I2C (Wire) commands for stm32_slave
 *
 * Default pins (I2C1, STM32duino GenF1):
 *   PB6 = SCL,  PB7 = SDA  (or PB8/PB9 with remap — not handled here)
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   I2C:SCAN               scan 0x08..0x77 → comma-separated hex addresses, or NONE
 *   I2C:WRITE:ADDR:HEX     write bytes to device, ADDR in hex (e.g. 68)
 *   I2C:READ:ADDR:N        read N bytes → uppercase hex string, or ERR
 *   I2C:WREG:ADDR:REG:HEX  write register: send REG byte then data bytes
 *   I2C:RREG:ADDR:REG:N    write REG then read N bytes → hex string
 *   I2C:PING:ADDR          check if a device ACKs at ADDR → ACK or NAK
 *
 * ADDR is decimal or 0x-prefixed hex (e.g. "104" or "0x68" — both = MPU6050).
 * REG  is decimal or 0x-prefixed hex.
 * HEX  is hex bytes string e.g. "FF00AB".
 * N    max = 32 bytes per transaction (Wire buffer limit).
 *
 * Wire.begin() is called in setup() of stm32_slave.ino.
 * Timeout 25 ms is set to prevent hanging on bus errors.
 */

#include <Wire.h>

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static bool i2cInitialized = false;

static void ensureI2C() {
    if (!i2cInitialized) {
        Wire.begin();
        Wire.setClock(100000);  // 100 kHz standard mode
        // STM32duino supports setWireTimeout to avoid infinite hang
        Wire.setWireTimeout(25000, true);  // 25 ms, reset on timeout
        i2cInitialized = true;
    }
}

// Parse address/register token: accepts decimal or "0x" prefixed hex.
static int parseHexOrDec(const String& s) {
    if (s.startsWith("0x") || s.startsWith("0X")) {
        return (int)strtol(s.c_str() + 2, nullptr, 16);
    }
    return s.toInt();
}

static void handleI2c(const String& seq, const String& args) {
    ensureI2C();

    String toks[5];
    int n = splitTokens(args, ':', toks, 5);
    if (n < 1) { sendErr(seq, "I2C:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- SCAN -----
    if (sub == "SCAN") {
        String found;
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            Wire.beginTransmission(addr);
            uint8_t err = Wire.endTransmission();
            if (err == 0) {
                if (found.length()) found += ',';
                char buf[5]; snprintf(buf, sizeof(buf), "0x%02X", addr);
                found += buf;
            }
            delay(2);
        }
        sendDone(seq, found.length() ? found : "NONE");
        return;
    }

    // ----- PING -----
    if (sub == "PING") {
        if (n < 2) { sendErr(seq, "I2C:MISSING_ADDR"); return; }
        int addr = parseHexOrDec(toks[1]);
        Wire.beginTransmission((uint8_t)addr);
        uint8_t err = Wire.endTransmission();
        sendDone(seq, err == 0 ? "ACK" : "NAK");
        return;
    }

    // ----- WRITE -----
    if (sub == "WRITE") {
        if (n < 3) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int addr = parseHexOrDec(toks[1]);
        uint8_t buf[32];
        int len = hexToBytes(toks[2], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "I2C:BAD_HEX"); return; }

        Wire.beginTransmission((uint8_t)addr);
        Wire.write(buf, (size_t)len);
        uint8_t err = Wire.endTransmission();
        if (err != 0) {
            char e[16]; snprintf(e, sizeof(e), "NACK:%d", err);
            sendErr(seq, String(e));
        } else {
            sendDone(seq, "OK:" + String(len) + "B");
        }
        return;
    }

    // ----- READ -----
    if (sub == "READ") {
        if (n < 3) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int addr = parseHexOrDec(toks[1]);
        int count = constrain(toks[2].toInt(), 1, 32);

        uint8_t received = Wire.requestFrom((uint8_t)addr, (uint8_t)count);
        if (received == 0) { sendErr(seq, "I2C:NO_DATA"); return; }

        uint8_t buf[32];
        int idx = 0;
        while (Wire.available() && idx < 32) buf[idx++] = Wire.read();
        sendDone(seq, bytesToHex(buf, idx));
        return;
    }

    // ----- WREG (write register) -----
    if (sub == "WREG") {
        if (n < 4) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int addr = parseHexOrDec(toks[1]);
        int reg  = parseHexOrDec(toks[2]);
        uint8_t buf[32];
        int len = hexToBytes(toks[3], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "I2C:BAD_HEX"); return; }

        Wire.beginTransmission((uint8_t)addr);
        Wire.write((uint8_t)reg);
        Wire.write(buf, (size_t)len);
        uint8_t err = Wire.endTransmission();
        if (err != 0) {
            char e[16]; snprintf(e, sizeof(e), "NACK:%d", err);
            sendErr(seq, String(e));
        } else {
            sendDone(seq, "OK:" + String(len) + "B");
        }
        return;
    }

    // ----- RREG (write register pointer, then read) -----
    if (sub == "RREG") {
        if (n < 4) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int addr  = parseHexOrDec(toks[1]);
        int reg   = parseHexOrDec(toks[2]);
        int count = constrain(toks[3].toInt(), 1, 32);

        // Write register pointer (no STOP: use endTransmission(false))
        Wire.beginTransmission((uint8_t)addr);
        Wire.write((uint8_t)reg);
        uint8_t err = Wire.endTransmission(false);  // repeated start
        if (err != 0 && err != 4) {
            // err==4 on some platforms means "other error" but data was sent
            char e[16]; snprintf(e, sizeof(e), "NACK:%d", err);
            sendErr(seq, String(e));
            return;
        }

        uint8_t received = Wire.requestFrom((uint8_t)addr, (uint8_t)count);
        if (received == 0) { sendErr(seq, "I2C:NO_DATA"); return; }

        uint8_t buf[32];
        int idx = 0;
        while (Wire.available() && idx < 32) buf[idx++] = Wire.read();
        sendDone(seq, bytesToHex(buf, idx));
        return;
    }

    sendErr(seq, "I2C:UNKNOWN_SUB");
}
