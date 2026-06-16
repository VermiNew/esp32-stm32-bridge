#pragma once
/**
 * ports.h — pin usage display for esp32_master
 *
 * Builds a live map of ESP32 pin usage from master-side state,
 * and/or requests the slave to report its own pin usage via PORTS:*.
 *
 * CLI usage (from parser.h):
 *   show ports              → both master and slave, all pins
 *   show ports free         → only free pins on both
 *   show ports --id master  → ESP32 only
 *   show ports --id slave   → STM32 only
 *   show ports free --id slave
 *
 * Output is printed directly to Serial (formatted table).
 * Slave data is fetched synchronously via startCommand().
 */

// Forward declarations from esp32_master.ino
extern bool   masterDbgActive;
extern int    masterDbgTxPin;
extern int    masterDbgRxPin;
extern bool   portsPending;
extern bool   portsPendingFreeOnly;

// Forward declaration from wifi_ntp.h
extern String storedSsid;

void logInfo(const String& msg);
void logWarn(const String& msg);

// ---------------------------------------------------------------------------
// ESP32 DevKit v1 — static pin map
// All 38 exposed pins with their base function and conflict notes.
// ---------------------------------------------------------------------------
struct Esp32PinInfo {
    int         gpio;
    const char* base;     // hardware function when not used by master firmware
    const char* note;     // conflict or constraint ("INPUT_ONLY", "STRAPPING" …)
};

static const Esp32PinInfo ESP32_PINS[] = {
    {  0, "BOOT/GPIO",   "STRAPPING — boot mode, avoid driving LOW at boot" },
    {  1, "USB_TX",      "RESERVED — Serial console TX" },
    {  2, "GPIO/LED",    "STRAPPING — onboard LED on many boards" },
    {  3, "USB_RX",      "RESERVED — Serial console RX" },
    {  4, "GPIO",        "" },
    {  5, "GPIO",        "STRAPPING" },
    { 12, "GPIO",        "STRAPPING — must be LOW at boot (3.3 V flash)" },
    { 13, "GPIO",        "" },
    { 14, "GPIO",        "outputs PWM signal at boot" },
    { 15, "GPIO",        "STRAPPING — outputs PWM signal at boot" },
    { 16, "UART2_RX",    "RESERVED — STM32 slave RX" },
    { 17, "UART2_TX",    "RESERVED — STM32 slave TX" },
    { 18, "GPIO/SPI_SCK","" },
    { 19, "GPIO/SPI_MISO","" },
    { 21, "GPIO/I2C_SDA","" },
    { 22, "GPIO/I2C_SCL","" },
    { 23, "GPIO/SPI_MOSI","" },
    { 25, "GPIO/DAC1",   "" },
    { 26, "GPIO/DAC2",   "" },
    { 27, "GPIO",        "" },
    { 32, "GPIO/ADC",    "" },
    { 33, "GPIO/ADC",    "" },
    { 34, "GPIO/ADC",    "INPUT_ONLY" },
    { 35, "GPIO/ADC",    "INPUT_ONLY" },
    { 36, "ADC/VP",      "INPUT_ONLY" },
    { 39, "ADC/VN",      "INPUT_ONLY" },
};
static const int ESP32_PINS_COUNT = (int)(sizeof(ESP32_PINS) / sizeof(ESP32_PINS[0]));

// ---------------------------------------------------------------------------
// Determine live master-side function of an ESP32 GPIO
// Returns "" if free (beyond static reservations)
// ---------------------------------------------------------------------------
static String esp32PinFunction(int gpio) {
    // Always reserved
    if (gpio == 1)  return "USB_TX[RESERVED]";
    if (gpio == 3)  return "USB_RX[RESERVED]";
    if (gpio == 16) return "UART2_RX->STM32";
    if (gpio == 17) return "UART2_TX->STM32";

    // Debug LEDs
    if (masterDbgActive) {
        if (gpio == masterDbgTxPin) return "DBG_TX_LED";
        if (gpio == masterDbgRxPin) return "DBG_RX_LED";
    }

    return "";
}

// ---------------------------------------------------------------------------
// Print ESP32 pin table to Serial
// ---------------------------------------------------------------------------
static void printMasterPorts(bool freeOnly) {
    Serial.println();
    Serial.print(CLR_BLUE "  ESP32 Master  (WiFi SSID: " CLR_RESET);
    Serial.print(storedSsid.length() ? storedSsid : "not connected");
    Serial.println(CLR_BLUE ")" CLR_RESET);
    Serial.println(CLR_DIM "  GPIO  Base function        Live state / notes" CLR_RESET);
    Serial.println(CLR_DIM "  ────  ──────────────────  ─────────────────────────────────────" CLR_RESET);

    for (int i = 0; i < ESP32_PINS_COUNT; i++) {
        const Esp32PinInfo& p = ESP32_PINS[i];
        String live = esp32PinFunction(p.gpio);
        bool used = live.length() > 0 || String(p.base).indexOf("RESERVED") >= 0;

        if (freeOnly && used) continue;
        if (!freeOnly && false) continue;  // show all when not freeOnly

        char gpioStr[5];
        snprintf(gpioStr, sizeof(gpioStr), "%3d", p.gpio);

        String baseStr = String(p.base);
        while (baseStr.length() < 18) baseStr += ' ';

        if (!used) {
            Serial.print(CLR_DIM "  " CLR_RESET);
            Serial.print(gpioStr);
            Serial.print("  ");
            Serial.print(CLR_DIM);
            Serial.print(baseStr);
            Serial.print("  FREE");
            if (strlen(p.note)) { Serial.print("  ("); Serial.print(p.note); Serial.print(")"); }
            Serial.println(CLR_RESET);
        } else {
            String liveDisp = live.length() ? live : String(p.base) + "[RESERVED]";
            Serial.print("  ");
            Serial.print(CLR_AMBER);
            Serial.print(gpioStr);
            Serial.print(CLR_RESET "  ");
            Serial.print(baseStr);
            Serial.print("  ");
            Serial.print(CLR_AMBER);
            Serial.print(liveDisp);
            Serial.println(CLR_RESET);
        }
    }
    Serial.println();
}

// ---------------------------------------------------------------------------
// Parse slave PORTS result and print formatted table
// ---------------------------------------------------------------------------
static void printSlavePorts(const String& raw, bool freeOnly) {
    Serial.println();
    Serial.println(CLR_BLUE "  STM32 Slave  (Blue Pill)" CLR_RESET);
    Serial.println(CLR_DIM "  Pin   Function" CLR_RESET);
    Serial.println(CLR_DIM "  ───   ────────────────────────────────" CLR_RESET);

    if (raw == "NONE" || raw.length() == 0) {
        Serial.println(CLR_DIM "  (no data)" CLR_RESET);
        Serial.println();
        return;
    }

    // Parse "A0=FREE;B6=I2C1_SCL;..."
    String entry = raw;
    int start = 0;
    while (start <= (int)entry.length()) {
        int semi = entry.indexOf(';', start);
        if (semi < 0) semi = entry.length();
        String tok = entry.substring(start, semi);
        start = semi + 1;

        int eq = tok.indexOf('=');
        if (eq < 0) continue;
        String pin = tok.substring(0, eq);
        String fn  = tok.substring(eq + 1);
        bool used  = fn != "FREE";

        if (freeOnly && used) continue;

        if (used) {
            Serial.print("  ");
            Serial.print(CLR_AMBER);
            while (pin.length() < 4) pin += ' ';
            Serial.print(pin);
            Serial.print(CLR_RESET "  ");
            Serial.println(fn);
        } else {
            Serial.print("  ");
            Serial.print(CLR_DIM);
            while (pin.length() < 4) pin += ' ';
            Serial.print(pin);
            Serial.print("  FREE" CLR_RESET);
            Serial.println();
        }
    }
    Serial.println();
}
