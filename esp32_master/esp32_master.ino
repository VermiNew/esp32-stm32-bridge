/**
 * esp32_master.ino — ESP32 master v2 for Supermikrokontroler
 *
 * Features vs v1:
 *   - CRC16 on every SEND/DONE/ERR frame (integrity checking)
 *   - Automatic HEARTBEAT every HEARTBEAT_INTERVAL_MS
 *   - Link state tracking (CONNECTED / DEGRADED / DISCONNECTED)
 *   - Watchdog forwarding: master auto-sends SYS:WDOG:KICK when wdog is armed
 *   - Full peripheral command set (GPIO, ADC, PWM, I2C, SPI, U2, EE, IRQ, SYS, CALC)
 *   - Same state machine (IDLE/WAIT_RECV/WAIT_DONE/POLLING) with CRC verification
 *
 * Serial Monitor settings:
 *   Baud: 115200,  Line ending: Newline (NOT CR, NOT Both)
 *
 * UART2 wiring (same as v1):
 *   GPIO17 (TX2) -----> STM32 PA10 (USART1 RX)
 *   GPIO16 (RX2) <----- STM32 PA9  (USART1 TX)
 *
 * Board: esp32:esp32:esp32
 */

#include <Arduino.h>
#include "protocol.h"
#include "wifi_ntp.h"
#include "parser.h"

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
static const int  STM_RX  = 16;
static const int  STM_TX  = 17;
static const long BAUD    = 115200;
HardwareSerial STM(2);

// ---------------------------------------------------------------------------
// Timing constants
// ---------------------------------------------------------------------------
static const unsigned long TIMEOUT_RECV_MS       = 300;
static const unsigned long TIMEOUT_DONE_MS       = 2500;
static const unsigned long TIMEOUT_POLL_MS       = 400;
static const unsigned long HEARTBEAT_INTERVAL_MS = 5000;
static const unsigned long HEARTBEAT_TIMEOUT_MS  = 1500;
static const int           MAX_RETRY             = 3;
static const int           MAX_POLLS             = 15;

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class State { IDLE, WAIT_RECV, WAIT_DONE, POLLING };
static State state         = State::IDLE;
static int   currentSeq    = 1;
static String pendingData  = "";   // DATA string of in-flight SEND
static unsigned long stateTs   = 0;
static unsigned long lastPollTs = 0;
static int retryCount = 0;
static int pollCount  = 0;

// ---------------------------------------------------------------------------
// Link / heartbeat state
// ---------------------------------------------------------------------------
enum class LinkState { CONNECTED, DEGRADED, DISCONNECTED };
static LinkState linkState       = LinkState::CONNECTED;
static unsigned long lastHbSendMs  = 0;
static unsigned long lastHbRecvMs  = 0;
static int           hbMissCount   = 0;
static bool          hbWaiting     = false;
static const int     HB_MAX_MISS   = 3;

// ---------------------------------------------------------------------------
// Watchdog forwarding
// ---------------------------------------------------------------------------
static bool     wdogArmed       = false;
static unsigned long wdogPeriodMs  = 0;
static unsigned long lastWdogKickMs = 0;

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------
static String stmRxBuf = "";

// ---------------------------------------------------------------------------
// Console helpers (24-bit ANSI on ESP32 Serial)
// ---------------------------------------------------------------------------
static void ansi(const char* code) { Serial.print("\033["); Serial.print(code); }
static void clr()  { Serial.print("\033[0m"); }
static void bold() { Serial.print("\033[1m"); }

#define CLR_GREEN  "\033[38;2;80;220;100m"
#define CLR_RED    "\033[38;2;255;80;80m"
#define CLR_AMBER  "\033[38;2;255;200;60m"
#define CLR_BLUE   "\033[38;2;130;200;255m"
#define CLR_DIM    "\033[38;2;120;120;120m"
#define CLR_RESET  "\033[0m"

void logOk  (const String& m) { Serial.print(CLR_GREEN "[OK]"  CLR_RESET "  "); Serial.println(m); }
void logErr (const String& m) { Serial.print(CLR_RED   "[ERR]" CLR_RESET " "); Serial.println(m); }
void logWarn(const String& m) { Serial.print(CLR_AMBER "[!!]"  CLR_RESET "  "); Serial.println(m); }
void logTx  (const String& m) { Serial.print(CLR_DIM   "-->"   CLR_RESET "   "); Serial.println(m); }
void logRx  (const String& m) { Serial.print(CLR_DIM   "<--"   CLR_RESET "   "); Serial.println(m); }
void logInfo(const String& m) { Serial.print(CLR_BLUE  "   "   CLR_RESET " "); Serial.println(m); }

// ---------------------------------------------------------------------------
// Send to slave
// ---------------------------------------------------------------------------
static void stmSend(const String& frame) {
    logTx(frame);
    STM.println(frame);
}

// Build a SEND frame with CRC over DATA
static void sendCommand(const String& data) {
    String crc   = proto_crc_str(data);
    String frame = "SEND:" + seqStr(currentSeq) + ":" + crc + ":" + data;
    stmSend(frame);
}

// ---------------------------------------------------------------------------
// Start a new command transaction
// ---------------------------------------------------------------------------
static void startCommand(const String& cmd) {
    if (state != State::IDLE) {
        logErr("Still busy with previous command. Type 'reset' first.");
        return;
    }

    if (cmd == "__PING__") {
        stmSend("PING");
        pendingData = "__PING__";
        state   = State::WAIT_RECV;
        stateTs = millis();
        retryCount = 0;
        return;
    }
    if (cmd == "__RESET__") {
        stmSend("RESET");
        state = State::IDLE;
        logOk("Reset sent.");
        return;
    }
    if (cmd == "__HB__") {
        stmSend("HEARTBEAT");
        hbWaiting   = true;
        lastHbSendMs = millis();
        return;
    }

    pendingData = cmd;
    retryCount  = 0;
    sendCommand(cmd);
    state   = State::WAIT_RECV;
    stateTs = millis();
}

// ---------------------------------------------------------------------------
// CRC verification for incoming DONE/ERR frames
// ---------------------------------------------------------------------------
static bool verifyCrc(const String& crcField, const String& data) {
    if (crcField.length() != 4) return true; // skip check if missing
    uint16_t expected = (uint16_t)strtoul(crcField.c_str(), nullptr, 16);
    return expected == proto_crc16(data);
}

// ---------------------------------------------------------------------------
// Handle one line received from slave
// ---------------------------------------------------------------------------
static void handleSlaveReply(const String& raw) {
    String line = raw; line.trim();
    if (!line.length()) return;
    logRx(line);

    // Parse: TYPE[:SEQ[:CRC[:DATA]]]
    String parts[4];
    int np = splitTokens(line, ':', parts, 4);
    if (np < 1) return;

    String type    = parts[0];
    String seq     = (np >= 2) ? parts[1] : "";
    String crcF    = (np >= 3) ? parts[2] : "";
    String payload = (np >= 4) ? parts[3] : "";

    // ---- PONG ----
    if (type == "PONG") {
        if (state == State::WAIT_RECV && pendingData == "__PING__") {
            logOk("PONG — link alive.");
            state = State::IDLE;
            // Update link health
            hbMissCount = 0;
            linkState   = LinkState::CONNECTED;
        }
        return;
    }

    // ---- HEARTBEAT:ACK ----
    if (type == "HEARTBEAT" && seq == "ACK") {
        hbWaiting    = false;
        hbMissCount  = 0;
        lastHbRecvMs = millis();
        linkState    = LinkState::CONNECTED;
        return;
    }
    // Also handle "HEARTBEAT:ACK" parsed as single token with no colon split
    if (line == "HEARTBEAT:ACK") {
        hbWaiting    = false;
        hbMissCount  = 0;
        lastHbRecvMs = millis();
        linkState    = LinkState::CONNECTED;
        return;
    }

    // ---- RESET:ACK ----
    if (line == "RESET:ACK") {
        logOk("Slave reset acknowledged.");
        state = State::IDLE;
        return;
    }

    // ---- RECV ----
    if (type == "RECV" && seq == seqStr(currentSeq)) {
        if (state == State::WAIT_RECV) {
            state   = State::WAIT_DONE;
            stateTs = millis();
        }
        return;
    }

    // ---- DONE ----
    if (type == "DONE" && seq == seqStr(currentSeq)) {
        if (state == State::WAIT_DONE || state == State::POLLING) {
            if (!verifyCrc(crcF, payload)) {
                logErr("CRC mismatch on DONE! Payload may be corrupted.");
                // Still accept it but warn
            }
            // Send FREE to release the slot on the slave
            stmSend("FREE:" + seq);
            logOk(payload.length() ? payload : "(empty)");
            currentSeq = seqNext(currentSeq);
            state = State::IDLE;

            // Update link health
            hbMissCount = 0;
            linkState   = LinkState::CONNECTED;
        }
        return;
    }

    // ---- BUSY ----
    if (type == "BUSY") return;  // handled by polling tick

    // ---- ERR ----
    if (type == "ERR" && seq == seqStr(currentSeq)) {
        if (!verifyCrc(crcF, payload)) logWarn("CRC mismatch on ERR frame.");
        logErr("Slave: " + payload);
        stmSend("FREE:" + seq);
        currentSeq = seqNext(currentSeq);
        state = State::IDLE;
        return;
    }

    // Unexpected — log it
    Serial.print(CLR_DIM "[??] " CLR_RESET);
    Serial.println(line);
}

// ---------------------------------------------------------------------------
// State machine timeout / retry tick
// ---------------------------------------------------------------------------
static void stateTick() {
    unsigned long now = millis();

    if (state == State::WAIT_RECV) {
        if (now - stateTs < TIMEOUT_RECV_MS) return;

        if (pendingData == "__PING__") {
            logErr("PING timeout."); state = State::IDLE; return;
        }
        retryCount++;
        if (retryCount > MAX_RETRY) {
            logErr("RECV timeout after " + String(MAX_RETRY) + " retries. Type 'reset'.");
            state = State::IDLE;
            return;
        }
        Serial.print(CLR_AMBER "[RETRY " CLR_RESET);
        Serial.print(retryCount);
        Serial.print(CLR_AMBER "/" CLR_RESET);
        Serial.print(MAX_RETRY);
        Serial.println("]");
        sendCommand(pendingData);
        stateTs = now;
        return;
    }

    if (state == State::WAIT_DONE) {
        if (now - stateTs >= TIMEOUT_DONE_MS) {
            state       = State::POLLING;
            pollCount   = 0;
            lastPollTs  = now - TIMEOUT_POLL_MS;  // trigger immediately
        }
        return;
    }

    if (state == State::POLLING) {
        if (now - lastPollTs >= TIMEOUT_POLL_MS) {
            pollCount++;
            if (pollCount > MAX_POLLS) {
                logErr("Slave unresponsive after " + String(MAX_POLLS) + " polls. Type 'reset'.");
                state = State::IDLE;
                return;
            }
            stmSend("POLL:" + seqStr(currentSeq));
            lastPollTs = now;
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// Heartbeat tick (runs in IDLE too — independent of command traffic)
// ---------------------------------------------------------------------------
static void heartbeatTick() {
    unsigned long now = millis();

    // Send heartbeat periodically if idle (don't interrupt commands)
    if (!hbWaiting && state == State::IDLE &&
        (now - lastHbSendMs >= HEARTBEAT_INTERVAL_MS)) {
        stmSend("HEARTBEAT");
        hbWaiting    = true;
        lastHbSendMs = now;
    }

    // Heartbeat timeout
    if (hbWaiting && (now - lastHbSendMs >= HEARTBEAT_TIMEOUT_MS)) {
        hbWaiting = false;
        hbMissCount++;
        if (hbMissCount >= HB_MAX_MISS) {
            linkState = LinkState::DISCONNECTED;
            logErr("Link DEAD — slave not responding to heartbeats.");
        } else {
            linkState = LinkState::DEGRADED;
            logWarn("Heartbeat miss #" + String(hbMissCount) + "/" + String(HB_MAX_MISS));
        }
    }
}

// ---------------------------------------------------------------------------
// Watchdog forwarding tick
// ---------------------------------------------------------------------------
static void wdogForwardTick() {
    if (!wdogArmed || wdogPeriodMs == 0) return;
    unsigned long now = millis();
    // Kick at half the watchdog period (safety margin)
    if (now - lastWdogKickMs >= wdogPeriodMs / 2) {
        if (state == State::IDLE) {
            startCommand("SYS:WDOG:KICK");
            lastWdogKickMs = now;
        }
    }
}

// ---------------------------------------------------------------------------
// printHelp (referenced by parser.h)
// ---------------------------------------------------------------------------
void printHelp() {
    Serial.println();
    Serial.println(CLR_BLUE "=== Supermikrokontroler v2 — Command Reference ===" CLR_RESET);
    Serial.println();
    Serial.println(CLR_AMBER "[ Link ]" CLR_RESET);
    Serial.println("  ping                          test link (PING/PONG)");
    Serial.println("  reset                         resync protocol");
    Serial.println("  hb                            manual heartbeat test");
    Serial.println();
    Serial.println(CLR_AMBER "[ GPIO ]" CLR_RESET);
    Serial.println("  gpio mode  <pin> in|out|pu|pd|an|od");
    Serial.println("  gpio write <pin> 0|1");
    Serial.println("  gpio read  <pin>");
    Serial.println("  gpio toggle <pin>");
    Serial.println("  gpio port  A|B|C              read lower 8 pins of port");
    Serial.println();
    Serial.println(CLR_AMBER "[ ADC ]  12-bit, 0-4095" CLR_RESET);
    Serial.println("  adc read  <pin>               single read");
    Serial.println("  adc avg   <pin> <n>           averaged (max 64)");
    Serial.println("  adc mv    <pin>               read in millivolts");
    Serial.println("  adc multi A0,A1,B0            multiple pins → CSV");
    Serial.println("  adc stream <pin> <n> <ms>     burst: n samples every ms → hex");
    Serial.println("  adc temp                      internal temp (tenths °C)");
    Serial.println("  adc vref                      estimated VDDA in mV");
    Serial.println();
    Serial.println(CLR_AMBER "[ PWM ]" CLR_RESET);
    Serial.println("  pwm set  <pin> <duty 0-1000>  duty in per-mille");
    Serial.println("  pwm freq <pin> <hz> <duty>    custom frequency");
    Serial.println("  pwm stop <pin>");
    Serial.println("  pwm read <pin>                last set duty");
    Serial.println();
    Serial.println(CLR_AMBER "[ I2C1 ]  PB6=SCL, PB7=SDA" CLR_RESET);
    Serial.println("  i2c cfg 100|400               set bus speed (kHz)");
    Serial.println("  i2c scan|ping|write|read|wreg|rreg ...");
    Serial.println();
    Serial.println(CLR_AMBER "[ I2C2 ]  PB10=SCL, PB11=SDA  ⚠ shared with U3" CLR_RESET);
    Serial.println("  i2c2 cfg|scan|ping|write|read|wreg|rreg  (same syntax as i2c)");
    Serial.println();
    Serial.println(CLR_AMBER "[ SPI ]  default: PA5=SCK, PA6=MISO, PA7=MOSI" CLR_RESET);
    Serial.println("  spi begin <cs_pin> <mode 0-3> <freq_khz>");
    Serial.println("  spi xfer  <cs_pin> <hexbytes>   full-duplex → rx hex");
    Serial.println("  spi write <cs_pin> <hexbytes>   TX only");
    Serial.println("  spi read  <cs_pin> <n_bytes>    RX only (sends 0xFF)");
    Serial.println("  spi end");
    Serial.println();
    Serial.println(CLR_AMBER "[ USART2 ]  PA2=TX2, PA3=RX2" CLR_RESET);
    Serial.println("  u2 cfg|tx|rx|flush|status|close  (see help for detail)");
    Serial.println();
    Serial.println(CLR_AMBER "[ USART3 ]  PB10=TX3, PB11=RX3  ⚠ shared with I2C2" CLR_RESET);
    Serial.println("  u3 cfg <baud> [bits parity stop]");
    Serial.println("  u3 tx <hexbytes> | u3 rx <n> [timeout_ms]");
    Serial.println("  u3 flush | u3 status | u3 close");
    Serial.println();
    Serial.println(CLR_AMBER "[ EEPROM  flash emulation, 512 bytes ]" CLR_RESET);
    Serial.println("  ee write  <addr> <byte 0-255>");
    Serial.println("  ee read   <addr>");
    Serial.println("  ee wrword <addr> <uint32>        little-endian 32-bit");
    Serial.println("  ee rdword <addr>");
    Serial.println("  ee wrhex  <addr> <hexbytes>");
    Serial.println("  ee rdhex  <addr> <n_bytes>");
    Serial.println("  ee fill   <byte>                 fill all 512 bytes");
    Serial.println("  ee size");
    Serial.println();
    Serial.println(CLR_AMBER "[ IRQ  up to 8 pins ]" CLR_RESET);
    Serial.println("  irq attach <pin> rise|fall|change");
    Serial.println("  irq detach <pin>|all");
    Serial.println("  irq poll                         get and clear event counts");
    Serial.println("  irq list");
    Serial.println();
    Serial.println(CLR_AMBER "[ System ]" CLR_RESET);
    Serial.println("  sys status | sys uptime | sys chipid | sys cpufreq");
    Serial.println("  sys fwver  | sys freeram | sys reset");
    Serial.println("  sys echo <text>                  round-trip latency test");
    Serial.println("  sys wdog en <ms>                 arm slave IWDG watchdog");
    Serial.println("  sys wdog kick                    manually kick watchdog");
    Serial.println("  sys wdog dis");
    Serial.println();
    Serial.println(CLR_AMBER "[ CAN bus ]  PB8=RX, PB9=TX  (needs TJA1050 transceiver)" CLR_RESET);
    Serial.println("  can begin 125|250|500|1000    init (kbps)");
    Serial.println("  can tx  <id_dec> <hexbytes>   send standard frame (11-bit ID)");
    Serial.println("  can txe <id_dec> <hexbytes>   send extended frame (29-bit ID)");
    Serial.println("  can rx                         receive next frame → ID:HEX:EXT:RTR");
    Serial.println("  can filter <id> <mask>         set acceptance filter");
    Serial.println("  can filter off                 accept all");
    Serial.println("  can status                     error counters");
    Serial.println("  can end");
    Serial.println();
    Serial.println(CLR_AMBER "[ WiFi ]" CLR_RESET);
    Serial.println("  wifi connect <ssid> <password>");
    Serial.println("  wifi status | wifi disconnect | wifi scan");
    Serial.println();
    Serial.println(CLR_AMBER "[ NTP → RTC sync ]" CLR_RESET);
    Serial.println("  ntp sync                  query NTP + push UTC time to slave RTC");
    Serial.println("  ntp status                last sync time + elapsed");
    Serial.println("  ntp server <hostname>     change NTP server (default: pool.ntp.org)");
    Serial.println("  calc map <v> <in_min> <in_max> <out_min> <out_max>");
    Serial.println("  calc crc16 <hexbytes>");
    Serial.println("  calc sqrt <n> | calc constrain <v> <lo> <hi> | calc abs <v>");
    Serial.println();
    Serial.println(CLR_AMBER "[ RTC  LSE crystal PC14/PC15 ]" CLR_RESET);
    Serial.println("  rtc init                           start LSE oscillator");
    Serial.println("  rtc status                         running? clock source?");
    Serial.println("  rtc get                            date+time → YYYY-MM-DD HH:MM:SS DOW");
    Serial.println("  rtc set YYYY-MM-DD HH:MM:SS        set date and time");
    Serial.println("  rtc set YYYY MM DD HH MM SS        alternative syntax");
    Serial.println("  rtc getts                          seconds since 2000-01-01");
    Serial.println("  rtc settss <seconds>               set from epoch-2000 timestamp");
    Serial.println("  rtc epoch                          Unix timestamp (since 1970)");
    Serial.println();
    Serial.println(CLR_AMBER "[ Legacy ]" CLR_RESET);
    Serial.println("  led on|off|status  |  blink <ms>  |  status");
    Serial.println();
    Serial.println(CLR_DIM "Pin tokens: A0..A15, B0..B15, C13..C15" CLR_RESET);
    Serial.println(CLR_DIM "NOTE: Serial Monitor line ending MUST be 'Newline'." CLR_RESET);
    Serial.println();
}

// ---------------------------------------------------------------------------
// Banner
// ---------------------------------------------------------------------------
static void printBanner() {
    Serial.println();
    Serial.println(CLR_BLUE "======================================================" CLR_RESET);
    Serial.println(CLR_BLUE "  Supermikrokontroler v2 — ESP32 master" CLR_RESET);
    Serial.println(CLR_BLUE "  Slave: STM32F103C8T6 Blue Pill on UART2" CLR_RESET);
    Serial.println(CLR_BLUE "======================================================" CLR_RESET);
    Serial.println(CLR_DIM "  Protocol: framed ASCII + CRC16, heartbeat, retry" CLR_RESET);
    Serial.println(CLR_DIM "  Type 'help' for command reference." CLR_RESET);
    Serial.println();
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(BAUD);
    while (!Serial && millis() < 3000) {}

    STM.begin(BAUD, SERIAL_8N1, STM_RX, STM_TX);

    lastHbSendMs  = millis();
    lastHbRecvMs  = millis();
    lastWdogKickMs = millis();

    printBanner();
}

void loop() {
    // --- Receive from slave ---
    while (STM.available()) {
        char c = (char)STM.read();
        if (c == '\n') {
            handleSlaveReply(stmRxBuf);
            stmRxBuf = "";
        } else if (c != '\r') {
            if ((int)stmRxBuf.length() < 256) stmRxBuf += c;
        }
    }

    // --- Read command from USB console ---
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length()) {
            Serial.print(CLR_DIM "> " CLR_RESET);
            Serial.println(input);

            // Intercept watchdog arming to also set forwarding period
            // "sys wdog en <ms>" → arm forwarding on master side too
            {
                String low = input; low.toLowerCase();
                if (low.startsWith("sys wdog en ")) {
                    String msStr = input.substring(12);
                    msStr.trim();
                    unsigned long ms = (unsigned long)msStr.toInt();
                    if (ms > 0) {
                        wdogPeriodMs   = ms;
                        wdogArmed      = true;
                        lastWdogKickMs = millis();
                        logInfo("Watchdog forwarding armed at " + String(ms) + " ms period.");
                    }
                }
                if (low == "sys wdog dis" || low == "sys wdog kick") {
                    if (low == "sys wdog dis") wdogArmed = false;
                }
            }

            String cmd = parseHumanCmd(input);
            if (cmd.length()) startCommand(cmd);
        }
    }

    // --- Ticks ---
    stateTick();
    heartbeatTick();
    wdogForwardTick();
    ntpTick();
    wifiAutoReconnect();
}
