/**
 * stm32_api.h — Programmatic C++ API for controlling the STM32 slave
 *
 * Wraps the protocol state machine into a synchronous, blocking API.
 * Include this file in any sketch that runs on the ESP32 master to
 * control the STM32 Blue Pill from code instead of the Serial console.
 *
 * ════════════════════════════════════════════════════════════════════
 *  QUICK START
 * ════════════════════════════════════════════════════════════════════
 *
 *  #include "stm32_api.h"
 *
 *  STM32 stm;
 *
 *  void setup() {
 *      // ... normal esp32_master setup() ...
 *
 *      stm.begin();                     // waits for link
 *      stm.gpio.mode("A0", GPIO_IN_PU); // configure pin
 *      stm.rtc.init();                  // start LSE clock
 *  }
 *
 *  void loop() {
 *      stm.pump();                      // must be called regularly
 *
 *      int raw = stm.adc.read("A0");    // read ADC
 *      stm.gpio.write("C13", raw > 2048 ? 0 : 1); // drive LED
 *  }
 *
 * ════════════════════════════════════════════════════════════════════
 *  BLOCKING vs NON-BLOCKING
 * ════════════════════════════════════════════════════════════════════
 *
 *  All methods in this file are BLOCKING — they send a command and
 *  spin-wait until the slave responds (or timeout expires).
 *  During the wait, stm.pump() is called internally so heartbeats
 *  and retries still work. Do not call delay() while waiting.
 *
 *  Maximum wait time per call = STM32_API_TIMEOUT_MS (default 6000 ms).
 *  Methods return a sentinel value on timeout/error:
 *    int    → STM32_ERR_INT  (-32768)
 *    bool   → false
 *    String → ""
 *
 * ════════════════════════════════════════════════════════════════════
 *  ERROR HANDLING
 * ════════════════════════════════════════════════════════════════════
 *
 *  stm.ok()       → true if the last call succeeded
 *  stm.error()    → error string from last failed call (or "")
 *  stm.lastRaw()  → raw result string from last call (DONE payload)
 *
 */

#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// External linkage — defined in esp32_master.ino
// ---------------------------------------------------------------------------
extern String        apiLastResult;
extern bool          apiLastWasErr;
extern State         state;
extern HardwareSerial STM;
extern String        stmRxBuf;

void startCommand    (const String& cmd);
void stateTick       ();
void heartbeatTick   ();
void wdogForwardTick ();
void ntpTick         ();
void handleSlaveReply(const String& raw);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const unsigned long STM32_API_TIMEOUT_MS = 6000;
static const int           STM32_ERR_INT        = -32768;

// ---------------------------------------------------------------------------
// GPIO mode constants
// ---------------------------------------------------------------------------
static const char GPIO_IN[]    = "IN";
static const char GPIO_OUT[]   = "OUT";
static const char GPIO_IN_PU[] = "PU";    // input pull-up
static const char GPIO_IN_PD[] = "PD";    // input pull-down
static const char GPIO_ANALOG[]= "AN";    // analog (for ADC)
static const char GPIO_OD[]    = "OD";    // open-drain output

// ---------------------------------------------------------------------------
// Internal pump — drains UART and runs all ticks while waiting
// ---------------------------------------------------------------------------
static void _api_pump() {
    while (STM.available()) {
        char c = (char)STM.read();
        if (c == '\n') {
            handleSlaveReply(stmRxBuf);
            stmRxBuf = "";
        } else if (c != '\r') {
            if ((int)stmRxBuf.length() < 256) stmRxBuf += c;
        }
    }
    stateTick();
    heartbeatTick();
    wdogForwardTick();
    ntpTick();
}

// ---------------------------------------------------------------------------
// Core execute — sends DATA string, blocks until IDLE, returns payload
// ---------------------------------------------------------------------------
static String _api_exec(const String& data,
                         unsigned long timeoutMs = STM32_API_TIMEOUT_MS) {
    apiLastResult = "";
    apiLastWasErr = false;
    startCommand(data);

    unsigned long deadline = millis() + timeoutMs;
    while (state != State::IDLE && millis() < deadline) {
        _api_pump();
        yield();   // allow ESP32 RTOS tasks (WiFi, BT) to run
    }

    if (state != State::IDLE) {
        state         = State::IDLE;
        apiLastResult = "TIMEOUT";
        apiLastWasErr = true;
    }
    return apiLastResult;
}

// ===========================================================================
// Sub-interface structs (grouped by peripheral)
// ===========================================================================

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------
struct GPIO_API {
    /** Set pin mode. mode = GPIO_IN | GPIO_OUT | GPIO_IN_PU | GPIO_IN_PD |
     *                       GPIO_ANALOG | GPIO_OD                          */
    bool mode(const String& pin, const char* mode) {
        String p = pin; p.toUpperCase();
        return !apiLastWasErr && _api_exec("GPIO:MODE:" + p + ":" + String(mode)).length() > 0;
    }

    /** Set digital output (0 or 1). Automatically sets pin to OUTPUT mode. */
    bool write(const String& pin, int val) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("GPIO:WRITE:" + p + ":" + String(val ? 1 : 0));
        return !apiLastWasErr;
    }

    /** Read digital input (0 or 1). Returns STM32_ERR_INT on error. */
    int read(const String& pin) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("GPIO:READ:" + p);
        if (apiLastWasErr) return STM32_ERR_INT;
        return r.toInt();
    }

    /** Toggle digital output. Returns new state (0/1) or STM32_ERR_INT. */
    int toggle(const String& pin) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("GPIO:TOGGLE:" + p);
        if (apiLastWasErr) return STM32_ERR_INT;
        return r.toInt();
    }

    /** Read the lower 8 pins of a port as a byte. port = 'A', 'B', or 'C'.
     *  Returns -1 on error. Result is hex string like "0x3F". */
    int port(char portLetter) {
        String r = _api_exec("GPIO:PORT:" + String((char)toupper(portLetter)));
        if (apiLastWasErr || r.length() < 3) return -1;
        return (int)strtol(r.c_str(), nullptr, 16);
    }
};

// ---------------------------------------------------------------------------
// ADC
// ---------------------------------------------------------------------------
struct ADC_API {
    /** Single 12-bit read (0–4095). Returns STM32_ERR_INT on error. */
    int read(const String& pin) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("ADC:READ:" + p);
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }

    /** Average of n samples (1–64). Returns STM32_ERR_INT on error. */
    int avg(const String& pin, int samples = 16) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("ADC:AVG:" + p + ":" + String(samples));
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }

    /** Read and convert to millivolts (assumes 3.3 V VDDA). */
    int mv(const String& pin) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("ADC:MV:" + p);
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }

    /** Read multiple pins at once.
     *  pins = comma-separated token list, e.g. "A0,A1,B0"
     *  Returns array of raw values. results[] must hold at least n elements.
     *  Returns number of values read, or -1 on error. */
    int multi(const String& pins, int* results, int maxResults) {
        String p = pins; p.toUpperCase();
        String r = _api_exec("ADC:MULTI:" + p);
        if (apiLastWasErr) return -1;
        int count = 0;
        int start = 0;
        for (int i = 0; i <= (int)r.length() && count < maxResults; i++) {
            if (i == (int)r.length() || r.charAt(i) == ',') {
                results[count++] = r.substring(start, i).toInt();
                start = i + 1;
            }
        }
        return count;
    }

    /** Burst sample: n readings at interval_ms apart.
     *  Returns raw hex string (4 hex chars per sample, big-endian 16-bit).
     *  Decode with hexToBytes() from protocol.h. */
    String stream(const String& pin, int n, int interval_ms) {
        String p = pin; p.toUpperCase();
        return _api_exec("ADC:STREAM:" + p + ":" + String(n) + ":" + String(interval_ms),
                         STM32_API_TIMEOUT_MS + (unsigned long)n * interval_ms + 500);
    }

    /** Internal temperature sensor. Returns tenths of °C (e.g. 254 = 25.4 °C). */
    int temperature() {
        String r = _api_exec("ADC:TEMP");
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }

    /** Internal voltage reference — estimated VDDA in mV. */
    int vref() {
        String r = _api_exec("ADC:VREF");
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }
};

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------
struct PWM_API {
    /** Set duty cycle (0–1000 per-mille) at default timer frequency (~1 kHz). */
    bool set(const String& pin, int duty) {
        String p = pin; p.toUpperCase();
        _api_exec("PWM:SET:" + p + ":" + String(constrain(duty, 0, 1000)));
        return !apiLastWasErr;
    }

    /** Set custom frequency (Hz) and duty (0–1000). */
    bool freq(const String& pin, long hz, int duty) {
        String p = pin; p.toUpperCase();
        _api_exec("PWM:FREQ:" + p + ":" + String(hz) + ":" + String(constrain(duty, 0, 1000)));
        return !apiLastWasErr;
    }

    /** Stop PWM output (pin goes LOW). */
    bool stop(const String& pin) {
        String p = pin; p.toUpperCase();
        _api_exec("PWM:STOP:" + p);
        return !apiLastWasErr;
    }

    /** Read last set duty (0–1000). Returns STM32_ERR_INT on error. */
    int read(const String& pin) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("PWM:READ:" + p);
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }

    /** Read last set frequency in Hz. Returns 0 if default freq was used. */
    long freqRead(const String& pin) {
        String p = pin; p.toUpperCase();
        String r = _api_exec("PWM:FREQREAD:" + p);
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }
};

// ---------------------------------------------------------------------------
// I2C (bus 1 = PB6/PB7, bus 2 = PB10/PB11)
// ---------------------------------------------------------------------------
struct I2C_API {
    int _bus;
    I2C_API(int bus = 1) : _bus(bus) {}

    String _prefix() const { return _bus == 2 ? "I2C2:" : "I2C:"; }

    /** Set bus speed. speed_khz = 100 or 400. */
    bool setSpeed(int speed_khz) {
        _api_exec(_prefix() + "CFG:" + String(speed_khz));
        return !apiLastWasErr;
    }

    /** Set transaction timeout in ms (1–1000). Useful for slow/missing devices. */
    bool setTimeout(int timeout_ms) {
        _api_exec(_prefix() + "CFG:100:" + String(timeout_ms));
        return !apiLastWasErr;
    }

    /** Scan bus 0x08–0x77. Returns comma-separated hex addresses or "NONE". */
    String scan() {
        return _api_exec(_prefix() + "SCAN", 60000);
    }

    /** Check if a device ACKs at addr. addr decimal or 0x-hex string. */
    bool ping(int addr) {
        char buf[8]; snprintf(buf, sizeof(buf), "0x%02X", addr);
        String r = _api_exec(_prefix() + "PING:" + String(buf));
        return !apiLastWasErr && r == "ACK";
    }

    /** Write bytes to device. data = hex string e.g. "FF0102". */
    bool write(int addr, const String& hexData) {
        char buf[8]; snprintf(buf, sizeof(buf), "0x%02X", addr);
        _api_exec(_prefix() + "WRITE:" + String(buf) + ":" + hexData);
        return !apiLastWasErr;
    }

    /** Read n bytes. Returns hex string or "" on error. */
    String read(int addr, int n) {
        char buf[8]; snprintf(buf, sizeof(buf), "0x%02X", addr);
        return _api_exec(_prefix() + "READ:" + String(buf) + ":" + String(n));
    }

    /** Write register: send reg byte then data bytes. */
    bool writeReg(int addr, int reg, const String& hexData) {
        char a[8], r[8];
        snprintf(a, sizeof(a), "0x%02X", addr);
        snprintf(r, sizeof(r), "0x%02X", reg);
        _api_exec(_prefix() + "WREG:" + String(a) + ":" + String(r) + ":" + hexData);
        return !apiLastWasErr;
    }

    /** Write register pointer then read n bytes. Returns hex or "". */
    String readReg(int addr, int reg, int n) {
        char a[8], r[8];
        snprintf(a, sizeof(a), "0x%02X", addr);
        snprintf(r, sizeof(r), "0x%02X", reg);
        return _api_exec(_prefix() + "RREG:" + String(a) + ":" + String(r) + ":" + String(n));
    }
};

// ---------------------------------------------------------------------------
// SPI (PA5=SCK, PA6=MISO, PA7=MOSI, CS on any GPIO)
// ---------------------------------------------------------------------------
struct SPI_API {
    /** Init SPI. mode = 0–3, freqKhz = 1–36000. */
    bool begin(const String& csPin, int mode, int freqKhz) {
        String p = csPin; p.toUpperCase();
        _api_exec("SPI:BEGIN:" + p + ":" + String(mode) + ":" + String(freqKhz));
        return !apiLastWasErr;
    }

    /** Full-duplex transfer. Returns received bytes as hex string. */
    String xfer(const String& csPin, const String& hexTx) {
        String p = csPin; p.toUpperCase();
        String h = hexTx; h.toUpperCase();
        return _api_exec("SPI:XFER:" + p + ":" + h);
    }

    /** TX only (discard MISO). */
    bool write(const String& csPin, const String& hexTx) {
        String p = csPin; p.toUpperCase();
        String h = hexTx; h.toUpperCase();
        _api_exec("SPI:WRITE:" + p + ":" + h);
        return !apiLastWasErr;
    }

    /** RX only — sends n dummy 0xFF bytes, returns received hex. */
    String read(const String& csPin, int n) {
        String p = csPin; p.toUpperCase();
        return _api_exec("SPI:READ:" + p + ":" + String(n));
    }

    /** Release SPI bus. */
    bool end() {
        _api_exec("SPI:END");
        return !apiLastWasErr;
    }
};

// ---------------------------------------------------------------------------
// USART2 / USART3  (templated on command prefix "U2" or "U3")
// ---------------------------------------------------------------------------
struct UART_API {
    String _pfx;
    UART_API(const String& prefix) : _pfx(prefix) {}

    /** Configure. e.g. cfg(9600) or cfg(115200, 8, "N", 1) */
    bool cfg(long baud, int bits = 8, const char* parity = "N", int stop = 1) {
        String r = _pfx + ":CFG:" + String(baud) +
                   ":" + String(bits) + ":" + parity + ":" + String(stop);
        _api_exec(r);
        return !apiLastWasErr;
    }

    /** Transmit hex bytes. */
    bool tx(const String& hexData) {
        String h = hexData; h.toUpperCase();
        _api_exec(_pfx + ":TX:" + h);
        return !apiLastWasErr;
    }

    /** Receive up to n bytes (timeout_ms default 50). Returns hex or "NONE". */
    String rx(int n, int timeout_ms = 50) {
        return _api_exec(_pfx + ":RX:" + String(n) + ":" + String(timeout_ms));
    }

    /** Flush RX buffer. */
    bool flush() { _api_exec(_pfx + ":FLUSH"); return !apiLastWasErr; }

    /** Bytes available in RX buffer. */
    int  available() {
        String r = _api_exec(_pfx + ":STATUS");
        return apiLastWasErr ? -1 : r.toInt();
    }

    /** Close / de-init port. */
    bool close() { _api_exec(_pfx + ":CLOSE"); return !apiLastWasErr; }
};

// ---------------------------------------------------------------------------
// EEPROM (512-byte flash emulation)
// ---------------------------------------------------------------------------
struct EE_API {
    /** Write one byte (0–255) to address (0–511). */
    bool write(int addr, uint8_t val) {
        _api_exec("EE:WRITE:" + String(addr) + ":" + String(val));
        return !apiLastWasErr;
    }

    /** Read one byte. Returns -1 on error. */
    int read(int addr) {
        String r = _api_exec("EE:READ:" + String(addr));
        return apiLastWasErr ? -1 : r.toInt();
    }

    /** Write 32-bit little-endian word. */
    bool writeWord(int addr, uint32_t val) {
        _api_exec("EE:WRWORD:" + String(addr) + ":" + String(val));
        return !apiLastWasErr;
    }

    /** Read 32-bit little-endian word. Returns 0xFFFFFFFF on error. */
    uint32_t readWord(int addr) {
        String r = _api_exec("EE:RDWORD:" + String(addr));
        return apiLastWasErr ? 0xFFFFFFFFUL : (uint32_t)strtoul(r.c_str(), nullptr, 10);
    }

    /** Write arbitrary bytes starting at addr. hexData e.g. "DEADBEEF". */
    bool writeHex(int addr, const String& hexData) {
        String h = hexData; h.toUpperCase();
        _api_exec("EE:WRHEX:" + String(addr) + ":" + h);
        return !apiLastWasErr;
    }

    /** Read n bytes starting at addr. Returns hex string or "". */
    String readHex(int addr, int n) {
        return _api_exec("EE:RDHEX:" + String(addr) + ":" + String(n));
    }

    /** Fill all 512 bytes with val. */
    bool fill(uint8_t val) {
        _api_exec("EE:FILL:" + String(val));
        return !apiLastWasErr;
    }
};

// ---------------------------------------------------------------------------
// IRQ (external interrupts, up to 8 pins)
// ---------------------------------------------------------------------------
struct IRQ_API {
    /** Attach interrupt. mode = "RISE" | "FALL" | "CHANGE". */
    bool attach(const String& pin, const char* mode) {
        String p = pin; p.toUpperCase();
        _api_exec("IRQ:ATTACH:" + p + ":" + String(mode));
        return !apiLastWasErr;
    }

    /** Detach interrupt from pin. Use "ALL" to detach all. */
    bool detach(const String& pin) {
        String p = pin; p.toUpperCase();
        _api_exec("IRQ:DETACH:" + p);
        return !apiLastWasErr;
    }

    /** Get and clear pending event counts.
     *  Returns raw string "A0:3,B5:1" or "NONE". */
    String poll() {
        return _api_exec("IRQ:POLL");
    }

    /** List attached pins and modes. Returns "A0:RISE,B5:CHANGE" or "NONE". */
    String list() {
        return _api_exec("IRQ:LIST");
    }
};

// ---------------------------------------------------------------------------
// RTC (LSE crystal on PC14/PC15)
// ---------------------------------------------------------------------------
struct RTC_API {
    /** Initialise RTC with LSE crystal. Must be called once after flashing.
     *  Safe to call repeatedly — does nothing if already running. */
    bool init() {
        _api_exec("RTC:INIT", 8000);
        return !apiLastWasErr;
    }

    /** Get current date+time string: "YYYY-MM-DD HH:MM:SS DOW" */
    String get() {
        return _api_exec("RTC:GET");
    }

    /** Set date and time. */
    bool set(int year, int month, int day, int hour, int minute, int second) {
        String cmd = "RTC:SET:" + String(year) + ":" + String(month) +
                     ":" + String(day) + ":" + String(hour) +
                     ":" + String(minute) + ":" + String(second);
        _api_exec(cmd);
        return !apiLastWasErr;
    }

    /** Get seconds since 2000-01-01 00:00:00. Returns 0 on error. */
    uint32_t getTimestamp() {
        String r = _api_exec("RTC:GETTS");
        return apiLastWasErr ? 0UL : (uint32_t)strtoul(r.c_str(), nullptr, 10);
    }

    /** Set from epoch-2000 timestamp. */
    bool setTimestamp(uint32_t ts) {
        _api_exec("RTC:SETTSS:" + String(ts));
        return !apiLastWasErr;
    }

    /** Get Unix epoch (seconds since 1970-01-01). Returns 0 on error. */
    uint32_t epoch() {
        String r = _api_exec("RTC:EPOCH");
        return apiLastWasErr ? 0UL : (uint32_t)strtoul(r.c_str(), nullptr, 10);
    }
};

// ---------------------------------------------------------------------------
// CAN bus (bxCAN, PB8=RX, PB9=TX)
// ---------------------------------------------------------------------------
struct CAN_API {
    /** Init CAN. mode = "NORMAL" | "LOOPBACK" | "SILENT". kbps = 125/250/500/1000.
     *  LOOPBACK: no transceiver needed. NORMAL: needs TJA1050 or SN65HVD230. */
    bool begin(int kbps, const char* mode = "LOOPBACK") {
        _api_exec("CAN:BEGIN:" + String(kbps) + ":" + String(mode));
        return !apiLastWasErr;
    }

    /** Suppress "transceiver required" warning on both master and slave. */
    bool setNoWarn(bool suppress) {
        _api_exec(suppress ? "CAN:NOWARN" : "CAN:WARN");
        return !apiLastWasErr;
    }

    /** Send standard 11-bit CAN frame. id ≤ 0x7FF, hexData max 8 bytes. */
    bool tx(uint32_t id, const String& hexData) {
        String h = hexData; h.toUpperCase();
        _api_exec("CAN:TX:" + String(id) + ":" + h);
        return !apiLastWasErr;
    }

    /** Send extended 29-bit CAN frame. id ≤ 0x1FFFFFFF. */
    bool txExtended(uint32_t id, const String& hexData) {
        String h = hexData; h.toUpperCase();
        _api_exec("CAN:TXE:" + String(id) + ":" + h);
        return !apiLastWasErr;
    }

    /** Receive next frame from ring buffer.
     *  Returns "ID:HEX:EXT:RTR" string or "NONE". */
    String rx() {
        return _api_exec("CAN:RX");
    }

    /** Set acceptance filter. Only frames matching (id & mask) == id pass. */
    bool setFilter(uint32_t id, uint32_t mask) {
        _api_exec("CAN:FILTER:" + String(id) + ":" + String(mask));
        return !apiLastWasErr;
    }

    /** Disable filter (accept all frames). */
    bool clearFilter() {
        _api_exec("CAN:FILTER:OFF");
        return !apiLastWasErr;
    }

    /** Get bus status and error counters. */
    String status() {
        return _api_exec("CAN:STATUS");
    }

    /** De-init CAN. */
    bool end() {
        _api_exec("CAN:END");
        return !apiLastWasErr;
    }
};

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------
struct SYS_API {
    String status()  { return _api_exec("SYS:STATUS"); }
    long   uptime()  { String r = _api_exec("SYS:UPTIME");  return apiLastWasErr ? -1 : r.toInt(); }
    String chipId()  { return _api_exec("SYS:CHIPID"); }
    int    cpuMhz()      { String r = _api_exec("SYS:CPUFREQ"); return apiLastWasErr ? -1 : r.toInt(); }
    int    freeRam()     { String r = _api_exec("SYS:FREERAM"); return apiLastWasErr ? -1 : r.toInt(); }
    String fwVer()       { return _api_exec("SYS:FWVER"); }
    /** Internal temperature in tenths of °C (e.g. 254 = 25.4 °C). */
    int    temperature() { String r = _api_exec("SYS:TEMP"); return apiLastWasErr ? STM32_ERR_INT : r.toInt(); }
    String echo(const String& text) { return _api_exec("SYS:ECHO:" + text); }
    bool   reset()   { _api_exec("SYS:RESET", 500); return true; }

    /** Arm IWDG watchdog on slave. timeoutMs 100–26000.
     *  Master will automatically send KICK every timeoutMs/2. */
    bool wdogEnable(int timeoutMs) {
        _api_exec("SYS:WDOG:EN:" + String(timeoutMs));
        return !apiLastWasErr;
    }

    /** Manually kick the watchdog. */
    bool wdogKick() {
        _api_exec("SYS:WDOG:KICK");
        return !apiLastWasErr;
    }
};

// ---------------------------------------------------------------------------
// Compute offload
// ---------------------------------------------------------------------------
struct CALC_API {
    /** Arduino map() on the STM32. */
    long map(long v, long inMin, long inMax, long outMin, long outMax) {
        String r = _api_exec("CALC:MAP:" + String(v) + ":" + String(inMin) +
                             ":" + String(inMax) + ":" + String(outMin) +
                             ":" + String(outMax));
        return apiLastWasErr ? STM32_ERR_INT : r.toInt();
    }

    /** CRC16-CCITT of hexData bytes. Returns 4-char hex string. */
    String crc16(const String& hexData) {
        String h = hexData; h.toUpperCase();
        return _api_exec("CALC:CRC16:" + h);
    }

    long   sqrtOf(long n)                   { String r = _api_exec("CALC:SQRT:" + String(n));                                       return apiLastWasErr ? STM32_ERR_INT : r.toInt(); }
    long   constrain(long v, long lo, long hi){ String r = _api_exec("CALC:CONSTRAIN:" + String(v) + ":" + String(lo) + ":" + String(hi)); return apiLastWasErr ? STM32_ERR_INT : r.toInt(); }
    long   absVal(long v)                    { String r = _api_exec("CALC:ABS:" + String(v));                                        return apiLastWasErr ? STM32_ERR_INT : r.toInt(); }
};

// ===========================================================================
// Top-level STM32 class — compose all sub-interfaces
// ===========================================================================

class STM32 {
public:
    GPIO_API  gpio;
    ADC_API   adc;
    PWM_API   pwm;
    I2C_API   i2c  { 1 };   // I2C1: PB6/PB7
    I2C_API   i2c2 { 2 };   // I2C2: PB10/PB11
    SPI_API   spi;
    UART_API  uart2 { "U2" };  // USART2: PA2/PA3
    UART_API  uart3 { "U3" };  // USART3: PB10/PB11
    EE_API    ee;
    IRQ_API   irq;
    RTC_API   rtc;
    CAN_API   can;
    SYS_API   sys;
    CALC_API  calc;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /** Wait for the slave to come online (up to timeoutMs).
     *  Returns true if PING succeeded. Call from setup() after normal init. */
    bool begin(unsigned long timeoutMs = 10000) {
        unsigned long deadline = millis() + timeoutMs;
        while (millis() < deadline) {
            startCommand("__PING__");
            unsigned long t0 = millis();
            while (state != State::IDLE && millis() - t0 < 500) {
                _api_pump(); yield();
            }
            if (!apiLastWasErr && apiLastResult == "") {
                // PING succeeded (PONG was received — state went IDLE with no ERR)
                return true;
            }
            delay(500);
        }
        return false;
    }

    /** Must be called from loop() when using the API alongside the console.
     *  Processes incoming bytes and runs heartbeat / watchdog ticks.
     *  Skip if you're inside a blocking execute() — it pumps internally. */
    void pump() { _api_pump(); }

    // -----------------------------------------------------------------------
    // Status helpers
    // -----------------------------------------------------------------------

    /** True if the last API call completed without error. */
    bool ok()    const { return !apiLastWasErr; }

    /** Error reason from the last failed call, or "" if last call succeeded. */
    String error() const { return apiLastWasErr ? apiLastResult : ""; }

    /** Raw DONE payload string from the last call (success or failure). */
    String lastRaw() const { return apiLastResult; }

    /** Link state: CONNECTED | DEGRADED | DISCONNECTED (from heartbeat engine). */
    String linkStatus() const {
        extern LinkState linkState;
        switch (linkState) {
            case LinkState::CONNECTED:    return "CONNECTED";
            case LinkState::DEGRADED:     return "DEGRADED";
            case LinkState::DISCONNECTED: return "DISCONNECTED";
            default:                      return "UNKNOWN";
        }
    }

    // -----------------------------------------------------------------------
    // Direct execute (escape hatch for commands not in the API)
    // -----------------------------------------------------------------------

    /** Execute a raw DATA string and return the DONE payload.
     *  Use for commands not yet wrapped in the API above. */
    String execute(const String& data, unsigned long timeoutMs = STM32_API_TIMEOUT_MS) {
        return _api_exec(data, timeoutMs);
    }
};
