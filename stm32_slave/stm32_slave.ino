/**
 * stm32_slave.ino — STM32F103C8T6 "Blue Pill" universal hardware agent v2
 *
 * Listens on USART1 (PA9/PA10) at 115200 8N1 for a framed ASCII protocol
 * from the ESP32 master and exposes all on-chip peripherals as commands.
 *
 * Board: STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8
 *
 * Wiring (application mode — USART1 used for comms, 8N1):
 *   PA9  (USART1 TX) ----> ESP32 GPIO16 (RX2)
 *   PA10 (USART1 RX) <---- ESP32 GPIO17 (TX2)
 *   3.3V <---------------- ESP32 3.3V   (NEVER 5V!)
 *   GND  <---------------- ESP32 GND
 *
 * Available peripheral buses (exposed via protocol):
 *   GPIO  -- all PA0..PA15, PB0..PB15, PC13..PC15
 *   ADC   -- PA0..PA7, PB0..PB1 (12-bit), internal temp, internal Vref
 *   PWM   -- TIM1/2/3/4 capable pins
 *   I2C1  -- PB6(SCL) / PB7(SDA)
 *   SPI1  -- PA5(SCK) / PA6(MISO) / PA7(MOSI), SW-CS on any GPIO
 *   USART2 -- PA2(TX) / PA3(RX)   [USART1 reserved for master]
 *   EEPROM -- 512 bytes flash-emulated
 *   IRQ   -- up to 8 GPIO external interrupts
 *   SYS   -- uptime, chip ID, CPU freq, IWDG watchdog, soft reset
 *   CALC  -- compute offload (map, crc16, sqrt, constrain, abs)
 *   RTC   -- real-time clock, LSE crystal on PC14/PC15 (32.768 kHz)
 *
 * Frame format (v2):
 *   SEND:NNN:CCCC:CMD[:ARG1[:ARG2...]]
 *   RECV:NNN
 *   DONE:NNN:CCCC:RESULT
 *   ERR:NNN:CCCC:REASON
 *   BUSY:NNN  |  POLL:NNN  |  FREE:NNN
 *   PING / PONG / HEARTBEAT / HEARTBEAT:ACK / RESET / RESET:ACK
 */

#include <Arduino.h>
#include <IWatchdog.h>

#include "protocol.h"
#include "cmd_gpio.h"
#include "cmd_adc.h"
#include "cmd_pwm.h"
#include "cmd_i2c.h"
#include "cmd_spi.h"
#include "cmd_u2.h"
#include "cmd_ee.h"
#include "cmd_irq.h"
#include "cmd_sys.h"
#include "cmd_misc.h"
#include "cmd_rtc.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const long     UART_BAUD  = 115200;
static const int      LED        = PC13;
static const int      RX_BUF_MAX = 192;

// ---------------------------------------------------------------------------
// Protocol slot (stores last DONE for POLL replay)
// ---------------------------------------------------------------------------
static String lastDoneSeq    = "";
static String lastDoneResult = "";

// ---------------------------------------------------------------------------
// Frame senders
// ---------------------------------------------------------------------------

void sendRaw(const String& line) {
    Serial1.println(line);
}

void sendRecv(const String& seq) {
    sendRaw("RECV:" + seq);
}

void sendDone(const String& seq, const String& result) {
    lastDoneSeq    = seq;
    lastDoneResult = result;
    sendRaw("DONE:" + seq + ":" + proto_crc_str(result) + ":" + result);
}

void sendErr(const String& seq, const String& reason) {
    sendRaw("ERR:" + seq + ":" + proto_crc_str(reason) + ":" + reason);
}

// ---------------------------------------------------------------------------
// Main dispatcher
// ---------------------------------------------------------------------------

static void dispatch(const String& type, const String& seq,
                     const String& crcField, const String& data) {

    // CRC check on SEND frames that carry a 4-char CRC field
    if (type == "SEND" && crcField.length() == 4) {
        uint16_t expected = (uint16_t)strtoul(crcField.c_str(), nullptr, 16);
        if (expected != proto_crc16(data)) {
            sendErr(seq, "CRC_ERR");
            return;
        }
    }

    if (type == "PING")      { sendRaw("PONG");          return; }
    if (type == "HEARTBEAT") { sendRaw("HEARTBEAT:ACK"); return; }

    if (type == "RESET") {
        lastDoneSeq = ""; lastDoneResult = "";
        blinkPeriodMs = 0;
        sendRaw("RESET:ACK");
        return;
    }
    if (type == "FREE")  { return; }
    if (type == "POLL")  {
        if (seq == lastDoneSeq && lastDoneSeq.length()) {
            sendRaw("DONE:" + seq + ":" + proto_crc_str(lastDoneResult) + ":" + lastDoneResult);
        } else {
            sendRaw("BUSY:" + seq);
        }
        return;
    }
    if (type != "SEND") { sendErr(seq.length() ? seq : "000", "UNKNOWN_FRAME"); return; }

    sendRecv(seq);
    wdogAutoKick();

    int colon  = data.indexOf(':');
    String cmd  = (colon >= 0) ? data.substring(0, colon) : data;
    String rest = (colon >= 0) ? data.substring(colon + 1) : "";

    if      (cmd == "GPIO")   handleGpio (seq, rest);
    else if (cmd == "ADC")    handleAdc  (seq, rest);
    else if (cmd == "PWM")    handlePwm  (seq, rest);
    else if (cmd == "I2C")    handleI2c  (seq, rest);
    else if (cmd == "SPI")    handleSpi  (seq, rest);
    else if (cmd == "U2")     handleU2   (seq, rest);
    else if (cmd == "EE")     handleEE   (seq, rest);
    else if (cmd == "IRQ")    handleIrq  (seq, rest);
    else if (cmd == "SYS")    handleSys  (seq, rest);
    else if (cmd == "CALC")   handleCalc (seq, rest);
    else if (cmd == "RTC")    handleRtc  (seq, rest);
    else if (cmd == "LED")    handleLed  (seq, rest);
    else if (cmd == "BLINK")  handleBlink(seq, rest);
    else if (cmd == "STATUS") handleSys  (seq, String("STATUS"));
    else sendErr(seq, "UNKNOWN_CMD:" + cmd);
}

// ---------------------------------------------------------------------------
// Line parser
// ---------------------------------------------------------------------------

static String rxBuf;

static void processLine(const String& raw) {
    String line = raw;
    line.trim();
    if (!line.length()) return;

    if (line == "PING")      { sendRaw("PONG");          return; }
    if (line == "HEARTBEAT") { sendRaw("HEARTBEAT:ACK"); return; }
    if (line == "RESET") {
        lastDoneSeq = ""; lastDoneResult = "";
        blinkPeriodMs = 0;
        sendRaw("RESET:ACK");
        return;
    }

    String parts[4];
    int np = splitTokens(line, ':', parts, 4);
    if (np < 1) { sendErr("000", "MALFORMED"); return; }

    String type = parts[0];
    if (np == 1) { dispatch(type, "", "", ""); return; }

    String seq = parts[1];
    for (int i = 0; i < (int)seq.length(); i++) {
        if (!isDigit((unsigned char)seq.charAt(i))) { sendErr("000", "BAD_SEQ"); return; }
    }

    String crcF = (np >= 3) ? parts[2] : "";
    String data = (np >= 4) ? parts[3] : "";
    dispatch(type, seq, crcF, data);
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------

void setup() {
    pinMode(LED, OUTPUT);
    digitalWrite(LED, HIGH);   // off (active-low)

    Serial1.begin(UART_BAUD);  // USART1 PA9/PA10, 8N1 — application mode
    analogReadResolution(12);

    Wire.begin();
    Wire.setClock(100000);
    Wire.setWireTimeout(25000, true);
    i2cInitialized = true;

    irqInit();

    // 3-flash boot confirmation
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED, LOW);   delay(100);
        digitalWrite(LED, HIGH);  delay(100);
    }

    rxBuf.reserve(RX_BUF_MAX);
}

void loop() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            processLine(rxBuf);
            rxBuf = "";
        } else if (c != '\r') {
            if ((int)rxBuf.length() < RX_BUF_MAX) {
                rxBuf += c;
            } else {
                sendErr("000", "LINE_OVERFLOW");
                rxBuf = "";
            }
        }
    }
    blinkTick();
}
