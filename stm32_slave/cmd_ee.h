#pragma once
/**
 * cmd_ee.h — EEPROM (flash emulation) commands for stm32_slave
 *
 * Uses the STM32duino EEPROM emulation library (EEPROM.h).
 * Emulates 512 bytes of non-volatile storage using two 1KB flash pages.
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   EE:WRITE:ADDR:BYTE     write one byte (0-255) to address (0-511)
 *   EE:READ:ADDR           read one byte → decimal
 *   EE:WRWORD:ADDR:WORD    write 32-bit word (little-endian) to address
 *   EE:RDWORD:ADDR         read 32-bit word → decimal (unsigned)
 *   EE:WRHEX:ADDR:HEX      write arbitrary bytes starting at ADDR
 *   EE:RDHEX:ADDR:N        read N bytes starting at ADDR → hex string
 *   EE:FILL:VAL            fill entire 512 bytes with VAL → OK
 *   EE:SIZE                virtual EEPROM size → 512
 *
 * Flash WEAR WARNING: Every EE:WRITE call triggers a flash commit.
 * STM32F103 flash endurance is ~10,000 erase cycles per page.
 * Do not call EE:WRITE in a tight loop.
 *
 * ADDR must be in range 0..511.
 * WORD is unsigned 32-bit, passed as decimal.
 */

#include <EEPROM.h>

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static const int EE_SIZE = 512;

static bool eeInitialized = false;
static void ensureEE() {
    if (!eeInitialized) {
        EEPROM.begin();   // no-op on STM32duino but good practice
        eeInitialized = true;
    }
}

static void handleEE(const String& seq, const String& args) {
    ensureEE();

    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 1) { sendErr(seq, "EE:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- WRITE (single byte) -----
    if (sub == "WRITE") {
        if (n < 3) { sendErr(seq, "EE:MISSING_ARGS"); return; }
        int addr = toks[1].toInt();
        int val  = toks[2].toInt();
        if (addr < 0 || addr >= EE_SIZE) { sendErr(seq, "EE:ADDR_OOB"); return; }
        if (val < 0 || val > 255)        { sendErr(seq, "EE:BAD_VAL"); return; }
        EEPROM.update(addr, (uint8_t)val);
        sendDone(seq, "OK");
        return;
    }

    // ----- READ (single byte) -----
    if (sub == "READ") {
        if (n < 2) { sendErr(seq, "EE:MISSING_ADDR"); return; }
        int addr = toks[1].toInt();
        if (addr < 0 || addr >= EE_SIZE) { sendErr(seq, "EE:ADDR_OOB"); return; }
        sendDone(seq, String(EEPROM.read(addr)));
        return;
    }

    // ----- WRWORD (32-bit little-endian) -----
    if (sub == "WRWORD") {
        if (n < 3) { sendErr(seq, "EE:MISSING_ARGS"); return; }
        int addr = toks[1].toInt();
        if (addr < 0 || addr + 3 >= EE_SIZE) { sendErr(seq, "EE:ADDR_OOB"); return; }
        uint32_t word = (uint32_t)strtoul(toks[2].c_str(), nullptr, 10);
        EEPROM.update(addr + 0, (uint8_t)(word & 0xFF));
        EEPROM.update(addr + 1, (uint8_t)((word >> 8) & 0xFF));
        EEPROM.update(addr + 2, (uint8_t)((word >> 16) & 0xFF));
        EEPROM.update(addr + 3, (uint8_t)((word >> 24) & 0xFF));
        sendDone(seq, "OK");
        return;
    }

    // ----- RDWORD (32-bit little-endian) -----
    if (sub == "RDWORD") {
        if (n < 2) { sendErr(seq, "EE:MISSING_ADDR"); return; }
        int addr = toks[1].toInt();
        if (addr < 0 || addr + 3 >= EE_SIZE) { sendErr(seq, "EE:ADDR_OOB"); return; }
        uint32_t word = (uint32_t)EEPROM.read(addr + 0)
                      | ((uint32_t)EEPROM.read(addr + 1) << 8)
                      | ((uint32_t)EEPROM.read(addr + 2) << 16)
                      | ((uint32_t)EEPROM.read(addr + 3) << 24);
        sendDone(seq, String(word));
        return;
    }

    // ----- WRHEX (arbitrary bytes) -----
    if (sub == "WRHEX") {
        if (n < 3) { sendErr(seq, "EE:MISSING_ARGS"); return; }
        int addr = toks[1].toInt();
        if (addr < 0 || addr >= EE_SIZE) { sendErr(seq, "EE:ADDR_OOB"); return; }
        uint8_t buf[64];
        int len = hexToBytes(toks[2], buf, sizeof(buf));
        if (len < 0) { sendErr(seq, "EE:BAD_HEX"); return; }
        if (addr + len > EE_SIZE) { sendErr(seq, "EE:OVERFLOW"); return; }
        for (int i = 0; i < len; i++) EEPROM.update(addr + i, buf[i]);
        sendDone(seq, "OK:" + String(len) + "B");
        return;
    }

    // ----- RDHEX -----
    if (sub == "RDHEX") {
        if (n < 3) { sendErr(seq, "EE:MISSING_ARGS"); return; }
        int addr = toks[1].toInt();
        int cnt  = toks[2].toInt();
        if (addr < 0 || addr >= EE_SIZE) { sendErr(seq, "EE:ADDR_OOB"); return; }
        cnt = constrain(cnt, 1, EE_SIZE - addr);
        uint8_t buf[64];
        cnt = min(cnt, 64);
        for (int i = 0; i < cnt; i++) buf[i] = EEPROM.read(addr + i);
        sendDone(seq, bytesToHex(buf, cnt));
        return;
    }

    // ----- FILL -----
    if (sub == "FILL") {
        if (n < 2) { sendErr(seq, "EE:MISSING_VAL"); return; }
        int val = constrain(toks[1].toInt(), 0, 255);
        for (int i = 0; i < EE_SIZE; i++) EEPROM.update(i, (uint8_t)val);
        sendDone(seq, "OK");
        return;
    }

    // ----- SIZE -----
    if (sub == "SIZE") {
        sendDone(seq, String(EE_SIZE));
        return;
    }

    sendErr(seq, "EE:UNKNOWN_SUB");
}
