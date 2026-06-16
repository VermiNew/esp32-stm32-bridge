#pragma once
/**
 * cmd_i2c.h — I2C (Wire / Wire2) commands for stm32_slave
 *
 * I2C1 default pins (STM32duino GenF1): PB6=SCL, PB7=SDA
 * I2C2 default pins:                    PB10=SCL, PB11=SDA
 *
 * ⚠ I2C2 pins PB10/PB11 are shared with USART3 TX/RX.
 *   Do NOT use I2C2 and U3 simultaneously.
 *
 * Commands:
 *   I2C:CFG:SPEED_KHZ        change I2C1 speed (100 | 400)
 *   I2C:SCAN                 scan I2C1 bus
 *   I2C:PING:ADDR            probe I2C1
 *   I2C:WRITE:ADDR:HEX
 *   I2C:READ:ADDR:N
 *   I2C:WREG:ADDR:REG:HEX
 *   I2C:RREG:ADDR:REG:N
 *
 * Same sub-commands via I2C2: prefix → uses Wire2 / I2C2 bus.
 * Route: stm32_slave.ino dispatches "I2C2" → handleI2c(seq, rest, 2)
 */

#include <Wire.h>

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

// Wire2 is not pre-declared for BLUEPILL_F103C8 variant — create it here.
// PB10 = SDA, PB11 = SCL for I2C2 on Blue Pill.
static TwoWire Wire2(PB11, PB10);

static bool i2cInitialized  = false;
static bool i2c2Initialized = false;

static TwoWire* getWireBus(int bus) {
    return (bus == 2) ? &Wire2 : &Wire;
}

static void ensureI2CBus(int bus) {
    if (bus == 2) {
        if (!i2c2Initialized) {
            Wire2.begin();
            Wire2.setClock(100000);
            Wire2.setTimeout(25000);
            i2c2Initialized = true;
        }
    } else {
        if (!i2cInitialized) {
            Wire.begin();
            Wire.setClock(100000);
            Wire.setTimeout(25000);
            i2cInitialized = true;
        }
    }
}

// Keep old name for callers in stm32_slave.ino setup()
static void ensureI2C() { ensureI2CBus(1); }

static int parseHexOrDec(const String& s) {
    if (s.startsWith("0x") || s.startsWith("0X"))
        return (int)strtol(s.c_str() + 2, nullptr, 16);
    return s.toInt();
}

static void handleI2c(const String& seq, const String& args, int bus = 1) {
    ensureI2CBus(bus);
    TwoWire* w = getWireBus(bus);

    String toks[5];
    int n = splitTokens(args, ':', toks, 5);
    if (n < 1) { sendErr(seq, "I2C:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- CFG -----
    // I2C:CFG:SPEED_KHZ            → set bus speed (100 or 400)
    // I2C:CFG:SPEED_KHZ:TIMEOUT_MS → set speed + transaction timeout (1..1000 ms)
    if (sub == "CFG") {
        if (n < 2) { sendErr(seq, "I2C:CFG:MISSING_SPEED"); return; }
        uint32_t spd = (uint32_t)toks[1].toInt();
        if (spd != 100 && spd != 400) { sendErr(seq, "I2C:CFG:BAD_SPEED"); return; }
        w->setClock(spd * 1000UL);
        String reply = "I2C" + String(bus) + ":SPEED:" + String(spd) + "kHz";
        if (n >= 3) {
            uint32_t toms = (uint32_t)constrain(toks[2].toInt(), 1, 1000);
            w->setTimeout(toms * 1000UL);  // setTimeout takes microseconds
            reply += ":TIMEOUT:" + String(toms) + "ms";
        }
        sendDone(seq, reply);
        return;
    }

    // ----- SCAN -----
    if (sub == "SCAN") {
        String found;
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            w->beginTransmission(addr);
            uint8_t err = w->endTransmission();
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
        w->beginTransmission((uint8_t)addr);
        sendDone(seq, w->endTransmission() == 0 ? "ACK" : "NAK");
        return;
    }

    // ----- WRITE -----
    if (sub == "WRITE") {
        if (n < 3) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int addr = parseHexOrDec(toks[1]);
        uint8_t buf[32];
        int len = hexToBytes(toks[2], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "I2C:BAD_HEX"); return; }
        w->beginTransmission((uint8_t)addr);
        w->write(buf, (size_t)len);
        uint8_t err = w->endTransmission();
        if (err) { char e[12]; snprintf(e, sizeof(e), "NACK:%d", err); sendErr(seq, String(e)); }
        else      { sendDone(seq, "OK:" + String(len) + "B"); }
        return;
    }

    // ----- READ -----
    if (sub == "READ") {
        if (n < 3) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int count = constrain(toks[2].toInt(), 1, 32);
        uint8_t got = w->requestFrom((uint8_t)parseHexOrDec(toks[1]), (uint8_t)count);
        if (!got) { sendErr(seq, "I2C:NO_DATA"); return; }
        uint8_t buf[32]; int idx = 0;
        while (w->available() && idx < 32) buf[idx++] = w->read();
        sendDone(seq, bytesToHex(buf, idx));
        return;
    }

    // ----- WREG -----
    if (sub == "WREG") {
        if (n < 4) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int addr = parseHexOrDec(toks[1]);
        int reg  = parseHexOrDec(toks[2]);
        uint8_t buf[32];
        int len = hexToBytes(toks[3], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "I2C:BAD_HEX"); return; }
        w->beginTransmission((uint8_t)addr);
        w->write((uint8_t)reg);
        w->write(buf, (size_t)len);
        uint8_t err = w->endTransmission();
        if (err) { char e[12]; snprintf(e, sizeof(e), "NACK:%d", err); sendErr(seq, String(e)); }
        else      { sendDone(seq, "OK:" + String(len) + "B"); }
        return;
    }

    // ----- RREG -----
    if (sub == "RREG") {
        if (n < 4) { sendErr(seq, "I2C:MISSING_ARGS"); return; }
        int addr  = parseHexOrDec(toks[1]);
        int reg   = parseHexOrDec(toks[2]);
        int count = constrain(toks[3].toInt(), 1, 32);
        w->beginTransmission((uint8_t)addr);
        w->write((uint8_t)reg);
        uint8_t err = w->endTransmission(false);
        if (err && err != 4) {
            char e[12]; snprintf(e, sizeof(e), "NACK:%d", err);
            sendErr(seq, String(e)); return;
        }
        uint8_t got = w->requestFrom((uint8_t)addr, (uint8_t)count);
        if (!got) { sendErr(seq, "I2C:NO_DATA"); return; }
        uint8_t buf[32]; int idx = 0;
        while (w->available() && idx < 32) buf[idx++] = w->read();
        sendDone(seq, bytesToHex(buf, idx));
        return;
    }

    sendErr(seq, "I2C:UNKNOWN_SUB");
}
