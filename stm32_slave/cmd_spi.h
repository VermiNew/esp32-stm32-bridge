#pragma once
/**
 * cmd_spi.h — SPI commands for stm32_slave
 *
 * Default SPI1 pins (STM32duino GenF1):
 *   PA5 = SCK,  PA6 = MISO,  PA7 = MOSI
 *   CS is controlled by software (any GPIO).
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   SPI:BEGIN:CS_PIN:MODE:FREQ_KHZ  init SPI, set CS pin + mode (0-3) + freq in kHz
 *   SPI:XFER:CS_PIN:HEX             full-duplex transfer (CS toggled automatically)
 *                                    → received bytes as hex string
 *   SPI:WRITE:CS_PIN:HEX            write only (discard received bytes)  → OK
 *   SPI:READ:CS_PIN:N               read N bytes (sends 0xFF as dummy)   → hex
 *   SPI:END                         release SPI bus
 *
 * CS_PIN token: A0..C15 (any GPIO output)
 * MODE: 0=CPOL0/CPHA0, 1=CPOL0/CPHA1, 2=CPOL1/CPHA0, 3=CPOL1/CPHA1
 * FREQ_KHZ: 1..36000 (36 MHz max for STM32F103 SPI1)
 */

#include <SPI.h>

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static bool    spiActive     = false;
static int     spiCsPin      = -1;
static uint8_t spiMode       = SPI_MODE0;
static uint32_t spiFreqHz    = 1000000UL;  // default 1 MHz

static void csLow (int pin) { if (pin >= 0) digitalWrite(pin, LOW);  }
static void csHigh(int pin) { if (pin >= 0) digitalWrite(pin, HIGH); }

static void handleSpi(const String& seq, const String& args) {
    String toks[5];
    int n = splitTokens(args, ':', toks, 5);
    if (n < 1) { sendErr(seq, "SPI:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- BEGIN -----
    if (sub == "BEGIN") {
        if (n < 4) { sendErr(seq, "SPI:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int csPin = parsePin(toks[1]);
        if (csPin < 0) { sendErr(seq, "SPI:BAD_CS_PIN"); return; }

        int mode = constrain(toks[2].toInt(), 0, 3);
        uint32_t freqKhz = (uint32_t)toks[3].toInt();
        if (freqKhz < 1 || freqKhz > 36000) { sendErr(seq, "SPI:BAD_FREQ"); return; }

        static const uint8_t modeMap[4] = {
            SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3
        };

        spiCsPin  = csPin;
        spiMode   = modeMap[mode];
        spiFreqHz = freqKhz * 1000UL;

        pinMode(spiCsPin, OUTPUT);
        csHigh(spiCsPin);

        if (!spiActive) {
            SPI.begin();
            spiActive = true;
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "SPI_OK:MODE%d:%lukHz", mode, (unsigned long)freqKhz);
        sendDone(seq, String(buf));
        return;
    }

    // ----- XFER (full-duplex) -----
    if (sub == "XFER") {
        if (!spiActive) { sendErr(seq, "SPI:NOT_INIT"); return; }
        if (n < 3) { sendErr(seq, "SPI:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int csPin = parsePin(toks[1]);
        if (csPin < 0) csPin = spiCsPin;

        uint8_t txBuf[64], rxBuf[64];
        int len = hexToBytes(toks[2], txBuf, sizeof(txBuf));
        if (len < 0 || len == 0) { sendErr(seq, "SPI:BAD_HEX"); return; }

        SPI.beginTransaction(SPISettings(spiFreqHz, MSBFIRST, spiMode));
        csLow(csPin);
        for (int i = 0; i < len; i++) rxBuf[i] = SPI.transfer(txBuf[i]);
        csHigh(csPin);
        SPI.endTransaction();

        sendDone(seq, bytesToHex(rxBuf, len));
        return;
    }

    // ----- WRITE (send only, discard MISO) -----
    if (sub == "WRITE") {
        if (!spiActive) { sendErr(seq, "SPI:NOT_INIT"); return; }
        if (n < 3) { sendErr(seq, "SPI:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int csPin = parsePin(toks[1]);
        if (csPin < 0) csPin = spiCsPin;

        uint8_t txBuf[64];
        int len = hexToBytes(toks[2], txBuf, sizeof(txBuf));
        if (len < 0 || len == 0) { sendErr(seq, "SPI:BAD_HEX"); return; }

        SPI.beginTransaction(SPISettings(spiFreqHz, MSBFIRST, spiMode));
        csLow(csPin);
        for (int i = 0; i < len; i++) SPI.transfer(txBuf[i]);
        csHigh(csPin);
        SPI.endTransaction();

        sendDone(seq, "OK:" + String(len) + "B");
        return;
    }

    // ----- READ (send dummy 0xFF bytes, return received) -----
    if (sub == "READ") {
        if (!spiActive) { sendErr(seq, "SPI:NOT_INIT"); return; }
        if (n < 3) { sendErr(seq, "SPI:MISSING_ARGS"); return; }
        toks[1].toUpperCase();
        int csPin = parsePin(toks[1]);
        if (csPin < 0) csPin = spiCsPin;

        int count = constrain(toks[2].toInt(), 1, 64);
        uint8_t rxBuf[64];

        SPI.beginTransaction(SPISettings(spiFreqHz, MSBFIRST, spiMode));
        csLow(csPin);
        for (int i = 0; i < count; i++) rxBuf[i] = SPI.transfer(0xFF);
        csHigh(csPin);
        SPI.endTransaction();

        sendDone(seq, bytesToHex(rxBuf, count));
        return;
    }

    // ----- END -----
    if (sub == "END") {
        if (spiActive) { SPI.end(); spiActive = false; }
        csHigh(spiCsPin);
        sendDone(seq, "SPI_OFF");
        return;
    }

    sendErr(seq, "SPI:UNKNOWN_SUB");
}
