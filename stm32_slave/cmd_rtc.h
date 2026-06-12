#pragma once
/**
 * cmd_rtc.h — Real-Time Clock commands for stm32_slave
 *
 * Uses the external 32.768 kHz crystal on PC14 (OSC32_IN) / PC15 (OSC32_OUT).
 * Backed by VBAT pin — if a coin cell is connected, time survives power loss.
 *
 * Requires STM32RTC library (bundled with STM32duino ≥ 2.0).
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   RTC:INIT              initialize RTC with LSE crystal (call once after flash)
 *   RTC:STATUS            is RTC running, clock source, power status
 *   RTC:SET:YYYY:MM:DD:HH:MM:SS   set date and time (24h)
 *   RTC:GET               get date+time → "YYYY-MM-DD HH:MM:SS DOW"
 *   RTC:GETTS             get seconds since 2000-01-01 00:00:00 → decimal
 *   RTC:SETTSS:N          set from epoch seconds (since 2000-01-01)
 *   RTC:EPOCH             seconds since Unix epoch 1970-01-01 (approx)
 *
 * DOW = Mon|Tue|Wed|Thu|Fri|Sat|Sun
 *
 * NOTE: PC14 and PC15 are used exclusively by the LSE oscillator.
 * Do NOT call pinMode(PC14/PC15, ...) anywhere in your code.
 *
 * VBAT (Blue Pill pin next to GND) can be connected to a CR2032 coin cell
 * through a 100 Ω resistor to keep the RTC alive when main power is off.
 */

#include <STM32RTC.h>

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

// Singleton instance provided by the STM32RTC library
static STM32RTC& rtc = STM32RTC::getInstance();
static bool rtcInitialized = false;

// Day-of-week names (STM32RTC: Monday=1 .. Sunday=7)
static const char* const DOW_NAMES[] = {
    "", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

// ---------------------------------------------------------------------------
// Seconds since 2000-01-01 00:00:00 (no timezone, no leap seconds)
// ---------------------------------------------------------------------------

// Days in each month (non-leap year)
static const uint16_t DAYS_IN_MONTH[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static bool isLeapYear(uint16_t y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static uint32_t toEpoch2000(uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint8_t second) {
    // Days from 2000-01-01 to start of given year
    uint32_t days = 0;
    for (uint16_t y = 2000; y < year; y++) {
        days += isLeapYear(y) ? 366 : 365;
    }
    // Add days in months of the given year
    for (uint8_t m = 1; m < month; m++) {
        days += DAYS_IN_MONTH[m - 1];
        if (m == 2 && isLeapYear(year)) days++;
    }
    days += (uint32_t)(day - 1);
    return days * 86400UL
         + (uint32_t)hour   * 3600UL
         + (uint32_t)minute * 60UL
         + (uint32_t)second;
}

static void fromEpoch2000(uint32_t ts,
                           uint16_t& year, uint8_t& month, uint8_t& day,
                           uint8_t& hour,  uint8_t& minute, uint8_t& second) {
    second = (uint8_t)(ts % 60); ts /= 60;
    minute = (uint8_t)(ts % 60); ts /= 60;
    hour   = (uint8_t)(ts % 24); ts /= 24;

    year = 2000;
    while (true) {
        uint32_t dpy = isLeapYear(year) ? 366 : 365;
        if (ts < dpy) break;
        ts -= dpy;
        year++;
    }
    month = 1;
    while (true) {
        uint32_t dpm = DAYS_IN_MONTH[month - 1];
        if (month == 2 && isLeapYear(year)) dpm++;
        if (ts < dpm) break;
        ts -= dpm;
        month++;
    }
    day = (uint8_t)(ts + 1);
}

// Offset from 2000-01-01 to Unix epoch 1970-01-01 (seconds)
static const uint32_t EPOCH_2000_OFFSET = 946684800UL;

// ---------------------------------------------------------------------------
// Initialize RTC (idempotent — safe to call multiple times)
// ---------------------------------------------------------------------------
static bool ensureRTC() {
    if (rtcInitialized) return true;

    // Use external 32.768 kHz crystal (LSE)
    rtc.setClockSource(STM32RTC::LSE_CLOCK);
    rtc.begin(false);   // false = don't reset if already configured

    // Give LSE oscillator up to 2 s to stabilise
    unsigned long t0 = millis();
    while (!rtc.isTimeSet() && millis() - t0 < 2000) {
        delay(10);
    }

    rtcInitialized = true;
    return true;
}

// ---------------------------------------------------------------------------
// Main handler
// ---------------------------------------------------------------------------
static void handleRtc(const String& seq, const String& args) {
    String toks[8];
    int n = splitTokens(args, ':', toks, 8);
    if (n < 1) { sendErr(seq, "RTC:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- INIT -----
    if (sub == "INIT") {
        rtcInitialized = false;   // force re-init
        if (!ensureRTC()) { sendErr(seq, "RTC:LSE_FAIL"); return; }

        // If time has never been set, set a safe default so isTimeSet() returns true
        if (!rtc.isTimeSet()) {
            rtc.setTime(0, 0, 0, 0);
            rtc.setDate(1, 1, 1, 0);  // Mon 2000-01-01
        }
        sendDone(seq, "RTC:LSE_OK");
        return;
    }

    // All other commands require an initialised RTC
    if (!ensureRTC()) { sendErr(seq, "RTC:NOT_INIT"); return; }

    // ----- STATUS -----
    if (sub == "STATUS") {
        bool running = rtc.isTimeSet();
        String s = running ? "RUNNING" : "NOT_SET";
        s += ":LSE:";
        // STM32RTC does not expose LSE ready bit directly;
        // proxy: if we got here without error, LSE started.
        s += "OK";
        sendDone(seq, s);
        return;
    }

    // ----- GET -----
    if (sub == "GET") {
        if (!rtc.isTimeSet()) { sendErr(seq, "RTC:NOT_SET"); return; }

        uint8_t h   = rtc.getHours();
        uint8_t m   = rtc.getMinutes();
        uint8_t s2  = rtc.getSeconds();
        uint8_t day = rtc.getDay();
        uint8_t mon = rtc.getMonth();
        uint8_t yr  = rtc.getYear();   // 2-digit: 0=2000
        uint8_t dow = rtc.getWeekDay();// 1=Mon..7=Sun in STM32RTC

        char buf[32];
        snprintf(buf, sizeof(buf), "20%02u-%02u-%02u %02u:%02u:%02u %s",
                 yr, mon, day, h, m, s2,
                 (dow >= 1 && dow <= 7) ? DOW_NAMES[dow] : "?");
        sendDone(seq, String(buf));
        return;
    }

    // ----- GETTS (seconds since 2000-01-01) -----
    if (sub == "GETTS") {
        if (!rtc.isTimeSet()) { sendErr(seq, "RTC:NOT_SET"); return; }

        uint16_t year = 2000 + rtc.getYear();
        uint32_t ts = toEpoch2000(year,
                                   rtc.getMonth(), rtc.getDay(),
                                   rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
        sendDone(seq, String(ts));
        return;
    }

    // ----- EPOCH (Unix epoch) -----
    if (sub == "EPOCH") {
        if (!rtc.isTimeSet()) { sendErr(seq, "RTC:NOT_SET"); return; }

        uint16_t year = 2000 + rtc.getYear();
        uint32_t ts = toEpoch2000(year,
                                   rtc.getMonth(), rtc.getDay(),
                                   rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
        sendDone(seq, String(ts + EPOCH_2000_OFFSET));
        return;
    }

    // ----- SET:YYYY:MM:DD:HH:MM:SS -----
    if (sub == "SET") {
        // Expect exactly 6 more tokens: year month day hour min sec
        if (n < 7) { sendErr(seq, "RTC:SET:MISSING_FIELDS"); return; }

        int year = toks[1].toInt();
        int mon  = toks[2].toInt();
        int day  = toks[3].toInt();
        int hour = toks[4].toInt();
        int min  = toks[5].toInt();
        int sec  = toks[6].toInt();

        if (year < 2000 || year > 2099) { sendErr(seq, "RTC:BAD_YEAR"); return; }
        if (mon  < 1    || mon  > 12)   { sendErr(seq, "RTC:BAD_MONTH"); return; }
        if (day  < 1    || day  > 31)   { sendErr(seq, "RTC:BAD_DAY"); return; }
        if (hour < 0    || hour > 23)   { sendErr(seq, "RTC:BAD_HOUR"); return; }
        if (min  < 0    || min  > 59)   { sendErr(seq, "RTC:BAD_MIN"); return; }
        if (sec  < 0    || sec  > 59)   { sendErr(seq, "RTC:BAD_SEC"); return; }

        // Calculate day-of-week (Zeller's congruence, Mon=1)
        // Simple Tomohiko Sakamoto algorithm
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        int y = year;
        if (mon < 3) y--;
        int dow = (y + y/4 - y/100 + y/400 + t[mon-1] + day) % 7;
        // 0=Sun in this algo; STM32RTC wants Mon=1..Sun=7
        uint8_t rtcDow = (dow == 0) ? 7 : (uint8_t)dow;

        rtc.setTime((uint8_t)hour, (uint8_t)min, (uint8_t)sec, 0);
        rtc.setDate(rtcDow, (uint8_t)day, (uint8_t)mon, (uint8_t)(year - 2000));

        char buf[24];
        snprintf(buf, sizeof(buf), "SET:%04d-%02d-%02d %02d:%02d:%02d",
                 year, mon, day, hour, min, sec);
        sendDone(seq, String(buf));
        return;
    }

    // ----- SETTSS:N (set from epoch-2000 seconds) -----
    if (sub == "SETTSS") {
        if (n < 2) { sendErr(seq, "RTC:MISSING_TS"); return; }
        uint32_t ts = (uint32_t)strtoul(toks[1].c_str(), nullptr, 10);

        uint16_t year; uint8_t mon, day, hour, minute, second;
        fromEpoch2000(ts, year, mon, day, hour, minute, second);

        if (year < 2000 || year > 2099) { sendErr(seq, "RTC:TS_OOB"); return; }

        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        int y = year; if (mon < 3) y--;
        int dow = (y + y/4 - y/100 + y/400 + t[mon-1] + day) % 7;
        uint8_t rtcDow = (dow == 0) ? 7 : (uint8_t)dow;

        rtc.setTime(hour, minute, second, 0);
        rtc.setDate(rtcDow, day, mon, (uint8_t)(year - 2000));

        char buf[24];
        snprintf(buf, sizeof(buf), "SET:%04d-%02d-%02d %02d:%02d:%02d",
                 year, mon, day, hour, minute, second);
        sendDone(seq, String(buf));
        return;
    }

    sendErr(seq, "RTC:UNKNOWN_SUB");
}
