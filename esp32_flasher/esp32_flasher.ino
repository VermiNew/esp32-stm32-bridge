/**
 * esp32_flasher — transparent USB <-> UART bridge
 *
 * Purpose: lets stm32flash (running on a PC) reach the STM32 ROM bootloader
 * through the ESP32's UART2. This sketch must be flashed onto the ESP32 BEFORE
 * attempting to flash the STM32 slave.
 *
 * Both serial ports run at 115200 8E1 — the STM32 ROM bootloader requires
 * even parity. This differs from the 8N1 used in application mode.
 *
 * Wiring (same as application mode):
 *   ESP32 GPIO17 (TX2) -----> STM32 PA10 (USART1 RX)
 *   ESP32 GPIO16 (RX2) <----- STM32 PA9  (USART1 TX)
 *   ESP32 3.3V  ------------- STM32 3.3V   (NEVER 5V!)
 *   ESP32 GND   ------------- STM32 GND
 *
 * The onboard LED (GPIO2) blinks every ~150 ms so you can tell the flasher
 * firmware (not esp32_master) is running.
 */

// UART2 talks to the STM32; RX=GPIO16, TX=GPIO17.
// Do NOT use GPIO1/GPIO3 — those are the USB console (Serial).
HardwareSerial STM(2);

static const int LED_PIN        = 2;
static const int STM_RX_PIN     = 16;
static const int STM_TX_PIN     = 17;
static const long BAUD          = 115200;
static const long BLINK_INTERVAL_MS = 150;

static unsigned long lastBlink = 0;
static bool ledState = false;

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // USB console — parity doesn't matter here, host talks vanilla 8N1
    Serial.begin(BAUD);

    // STM32 bridge — 8E1 because the ROM bootloader autobaud uses even parity.
    // If this is set to 8N1 the bootloader will not ACK the 0x7F sync byte.
    STM.begin(BAUD, SERIAL_8E1, STM_RX_PIN, STM_TX_PIN);

    Serial.println("esp32_flasher ready — transparent bridge active (8E1)");
}

void loop() {
    // --- Transparent byte pump in both directions ---
    // USB -> STM32
    while (Serial.available()) {
        STM.write(Serial.read());
    }
    // STM32 -> USB
    while (STM.available()) {
        Serial.write(STM.read());
    }

    // --- Non-blocking LED heartbeat ---
    // Blink every BLINK_INTERVAL_MS to signal "flasher is running".
    // Never use delay() here — it would stall the byte pump.
    unsigned long now = millis();
    if (now - lastBlink >= BLINK_INTERVAL_MS) {
        lastBlink = now;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
}
