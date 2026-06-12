#pragma once
/**
 * wifi_ntp.h — WiFi connection and NTP time synchronisation for esp32_master
 *
 * After a successful NTP sync, automatically sends RTC:SETTSS:<epoch2000>
 * to the STM32 slave so both devices share the same time reference.
 *
 * Commands added to the CLI:
 *   wifi connect <ssid> <password>   connect to a 2.4 GHz AP
 *   wifi status                      IP, RSSI, SSID
 *   wifi disconnect                  leave the AP
 *   wifi scan                        list visible APs
 *   ntp sync                         query NTP and push time to slave RTC
 *   ntp status                       last sync result + elapsed time
 *   ntp server <hostname>            change NTP server (default: pool.ntp.org)
 *
 * NTP sync flow:
 *   1. configTime() points ESP32's SNTP client at the NTP server (UTC, no DST)
 *   2. Wait up to NTP_WAIT_MS for the first response
 *   3. Read Unix timestamp via time()
 *   4. Subtract EPOCH_2000_OFFSET to get epoch-2000
 *   5. Queue "RTC:INIT" → wait → "RTC:SETTSS:<n>" → report result
 *
 * The slave's RTC:SETTSS command is sent through the normal protocol
 * state machine (startCommand), so it respects retries and CRC.
 */

#include <WiFi.h>
#include <time.h>

// Forward declarations from esp32_master.ino
void startCommand(const String& cmd);
void logOk  (const String& m);
void logErr (const String& m);
void logWarn(const String& m);
void logInfo(const String& m);

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const unsigned long NTP_WAIT_MS      = 8000;   // max wait for first NTP reply
static const uint32_t      EPOCH_2000_OFFSET = 946684800UL; // Unix epoch → epoch-2000

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static String ntpServer       = "pool.ntp.org";
static bool   ntpSynced       = false;
static time_t lastNtpUnix     = 0;
static unsigned long lastNtpMs = 0;

// NTP sync progress (non-blocking handoff to state machine)
enum class NtpStep { IDLE, WAIT_RTC_INIT, WAIT_SETTSS };
static NtpStep  ntpStep        = NtpStep::IDLE;
static uint32_t ntpEpoch2000   = 0;

// ---------------------------------------------------------------------------
// WiFi helpers
// ---------------------------------------------------------------------------

static void wifiConnect(const String& ssid, const String& password) {
    if (WiFi.status() == WL_CONNECTED) {
        logInfo("Already connected. Disconnecting first...");
        WiFi.disconnect(true);
        delay(500);
    }

    logInfo("Connecting to: " + ssid + " ...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        logOk("WiFi connected. IP: " + WiFi.localIP().toString()
              + "  RSSI: " + String(WiFi.RSSI()) + " dBm");
    } else {
        logErr("WiFi connection failed. Check SSID/password and signal strength.");
    }
}

static void wifiStatus() {
    if (WiFi.status() != WL_CONNECTED) {
        logWarn("WiFi not connected.");
        return;
    }
    logOk("WiFi: " + String(WiFi.SSID())
          + "  IP: " + WiFi.localIP().toString()
          + "  RSSI: " + String(WiFi.RSSI()) + " dBm"
          + "  CH: " + String(WiFi.channel()));
}

static void wifiScan() {
    logInfo("Scanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    if (n <= 0) { logWarn("No networks found."); return; }
    for (int i = 0; i < n; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "  %2d  %-32s  %4d dBm  CH%2d  %s",
                 i + 1,
                 WiFi.SSID(i).c_str(),
                 WiFi.RSSI(i),
                 WiFi.channel(i),
                 (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED");
        Serial.println(buf);
    }
    WiFi.scanDelete();
}

// ---------------------------------------------------------------------------
// NTP sync — initiates a two-step command sequence
// ---------------------------------------------------------------------------

static void ntpSync() {
    if (WiFi.status() != WL_CONNECTED) {
        logErr("WiFi not connected. Run: wifi connect <ssid> <password>");
        return;
    }

    logInfo("Querying NTP server: " + ntpServer);
    // Configure ESP32 SNTP (UTC, no DST offset)
    configTime(0, 0, ntpServer.c_str());

    // Wait for valid time (tm_year > 70 means post-1970)
    struct tm ti;
    unsigned long t0 = millis();
    bool gotTime = false;
    while (millis() - t0 < NTP_WAIT_MS) {
        if (getLocalTime(&ti) && ti.tm_year > 70) {
            gotTime = true;
            break;
        }
        delay(200);
        Serial.print('.');
    }
    Serial.println();

    if (!gotTime) {
        logErr("NTP timeout — no response from " + ntpServer);
        return;
    }

    time_t now;
    time(&now);
    ntpEpoch2000  = (uint32_t)((unsigned long)now - (unsigned long)EPOCH_2000_OFFSET);
    lastNtpUnix   = now;
    lastNtpMs     = millis();
    ntpSynced     = true;

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &ti);
    logOk(String("NTP time: ") + buf);
    logInfo("Epoch-2000: " + String(ntpEpoch2000));
    logInfo("Sending time to slave RTC...");

    // Step 1: initialise RTC on slave first (in case it was never started)
    ntpStep = NtpStep::WAIT_RTC_INIT;
    startCommand("RTC:INIT");
}

static void ntpStatus() {
    if (!ntpSynced) {
        logWarn("NTP: never synced.");
        return;
    }
    unsigned long elapsedS = (millis() - lastNtpMs) / 1000UL;
    char buf[32];
    struct tm* ti = gmtime(&lastNtpUnix);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", ti);
    logOk("Last NTP sync: " + String(buf)
          + " (" + String(elapsedS) + " s ago)");
}

// ---------------------------------------------------------------------------
// NTP state machine tick — called from loop() after startCommand returns
// Must be called ONLY when the protocol state machine is IDLE.
// ---------------------------------------------------------------------------
// extern declaration — state is in esp32_master.ino
extern State state;

static void ntpTick() {
    if (ntpStep == NtpStep::IDLE) return;
    // Only proceed when master state machine is idle (previous command done)
    if (state != State::IDLE) return;

    if (ntpStep == NtpStep::WAIT_RTC_INIT) {
        // RTC:INIT completed — now send the timestamp
        ntpStep = NtpStep::WAIT_SETTSS;
        startCommand("RTC:SETTSS:" + String(ntpEpoch2000));
        return;
    }

    if (ntpStep == NtpStep::WAIT_SETTSS) {
        // RTC:SETTSS completed
        ntpStep = NtpStep::IDLE;
        logOk("Slave RTC updated. Run 'rtc get' to confirm.");
        return;
    }
}

// ---------------------------------------------------------------------------
// Credential store — SSID/password can be saved to NVS or kept in RAM
// For simplicity we keep them in RAM (lost on ESP32 reset).
// ---------------------------------------------------------------------------
static String storedSsid = "";
static String storedPass = "";

static void wifiAutoReconnect() {
    // Called from loop() — if we lost connection and have credentials, retry
    static unsigned long lastRetryMs = 0;
    if (storedSsid.length() == 0) return;
    if (WiFi.status() == WL_CONNECTED) return;
    if (millis() - lastRetryMs < 30000) return;  // retry every 30 s
    lastRetryMs = millis();
    logWarn("WiFi lost. Reconnecting to " + storedSsid + "...");
    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
}
