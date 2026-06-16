#pragma once
/**
 * cmd_sys.h — system/diagnostic commands for stm32_slave
 *
 * Commands (DATA field after SEND:NNN:CCCC:):
 *   SYS:STATUS        full status: version, uptime, RAM, reset cause
 *   SYS:UPTIME        milliseconds since boot → decimal
 *   SYS:CHIPID        unique 96-bit device ID → 24-char hex string
 *   SYS:CPUFREQ       CPU clock frequency → MHz
 *   SYS:FWVER         firmware version string
 *   SYS:RESET         soft reset via NVIC_SystemReset()
 *   SYS:WDOG:EN:MS    enable IWDG with timeout in ms (max ~26000 ms)
 *   SYS:WDOG:KICK     reload (pet) the IWDG
 *   SYS:WDOG:DIS      disable WWDG (IWDG cannot be disabled once started)
 *   SYS:ECHO:TEXT     echo TEXT back → useful for latency / link test
 *   SYS:FREERAM       estimated free heap in bytes
 *
 * IWDG notes:
 *   - Once started, IWDG CANNOT be stopped (STM32 hardware limitation).
 *   - The master should send SYS:WDOG:KICK at intervals < the timeout.
 *   - Alternatively, the slave auto-kicks the watchdog on every received frame
 *     if wdog was enabled (see stm32_slave.ino).
 */

void sendDone(const String& seq, const String& result);
void sendErr (const String& seq, const String& reason);

static const char FW_VERSION_V2[] = "2.0";

static bool wdogEnabled = false;

// ---------------------------------------------------------------------------
// Free RAM estimate — reads stack pointer vs. heap end
// (heuristic for ARM Cortex-M, may vary by newlib config)
// ---------------------------------------------------------------------------
extern "C" {
    extern char _end;      // end of BSS / start of heap
    extern char _estack;   // top of stack (defined in linker script)
}

static int estimateFreeRAM() {
    char stackVar;
    // Distance from current stack frame to top of heap
    // Both addresses are in SRAM for STM32F1
    int stackAddr = (int)&stackVar;
    int heapEnd   = (int)&_end;
    return stackAddr - heapEnd;
}

// ---------------------------------------------------------------------------
// Chip ID — 96-bit unique device identifier
// STM32F1 UID base address: 0x1FFFF7E8
// ---------------------------------------------------------------------------
static String getChipId() {
    const uint32_t* uid = (const uint32_t*)0x1FFFF7E8UL;
    char buf[25];
    snprintf(buf, sizeof(buf), "%08lX%08lX%08lX",
             (unsigned long)uid[0],
             (unsigned long)uid[1],
             (unsigned long)uid[2]);
    return String(buf);
}

// ---------------------------------------------------------------------------
// Reset cause (read RCC_CSR bits)
// ---------------------------------------------------------------------------
static String getResetCause() {
    // STM32duino/HAL exposes RCC->CSR
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;   // clear reset flags for next time

    if (csr & RCC_CSR_IWDGRSTF) return "IWDG";
    if (csr & RCC_CSR_WWDGRSTF) return "WWDG";
    if (csr & RCC_CSR_SFTRSTF)  return "SOFT";
    if (csr & RCC_CSR_PORRSTF)  return "POR";
    if (csr & RCC_CSR_PINRSTF)  return "PIN";
    return "UNK";
}

static void handleSys(const String& seq, const String& args) {
    String toks[4];
    int n = splitTokens(args, ':', toks, 4);
    if (n < 1) { sendErr(seq, "SYS:BAD_ARGS"); return; }

    const String& sub = toks[0];

    // ----- STATUS -----
    if (sub == "STATUS") {
        String s = "v";
        s += FW_VERSION_V2;
        s += ":UP:";   s += String(millis());
        s += ":RAM:";  s += String(estimateFreeRAM());
        s += ":RST:";  s += getResetCause();
        s += ":CPU:";  s += String(F_CPU / 1000000);
        s += "MHz:ID:"; s += getChipId().substring(0, 8); // first 8 chars
        sendDone(seq, s);
        return;
    }

    // ----- UPTIME -----
    if (sub == "UPTIME") {
        sendDone(seq, String(millis()));
        return;
    }

    // ----- CHIPID -----
    if (sub == "CHIPID") {
        sendDone(seq, getChipId());
        return;
    }

    // ----- CPUFREQ -----
    if (sub == "CPUFREQ") {
        sendDone(seq, String(F_CPU / 1000000) + "MHz");
        return;
    }

    // ----- FWVER -----
    if (sub == "FWVER") {
        sendDone(seq, String(FW_VERSION_V2));
        return;
    }

    // ----- FREERAM -----
    if (sub == "FREERAM") {
        sendDone(seq, String(estimateFreeRAM()));
        return;
    }

    // ----- ECHO -----
    if (sub == "ECHO") {
        String echo = (n >= 2) ? toks[1] : "";
        sendDone(seq, echo);
        return;
    }

    // ----- WDOG -----
    if (sub == "WDOG") {
        if (n < 2) { sendErr(seq, "SYS:WDOG:MISSING_SUB"); return; }
        String wsub = toks[1]; wsub.toUpperCase();

        if (wsub == "EN") {
            if (n < 3) { sendErr(seq, "SYS:WDOG:MISSING_MS"); return; }
            uint32_t ms = (uint32_t)toks[2].toInt();
            if (ms < 100 || ms > 26000) { sendErr(seq, "SYS:WDOG:BAD_MS"); return; }

            // IWatchdog: STM32duino provides IWatchdog.h
            // IWatchdog.begin() takes timeout in MICROSECONDS
            IWatchdog.begin(ms * 1000UL);
            wdogEnabled = true;
            sendDone(seq, "WDOG_ON:" + String(ms) + "ms");
            return;
        }

        if (wsub == "KICK") {
            if (wdogEnabled) IWatchdog.reload();
            sendDone(seq, wdogEnabled ? "KICKED" : "WDOG_OFF");
            return;
        }

        if (wsub == "DIS") {
            // IWDG cannot be disabled once started — but we stop kicking it
            // so it will eventually reset the MCU.
            // WWDG can be disabled by simply not refreshing it.
            // We just report this limitation clearly.
            wdogEnabled = false;
            sendDone(seq, "NOTE:IWDG_HW_CANNOT_STOP");
            return;
        }

        sendErr(seq, "SYS:WDOG:UNKNOWN_SUB");
        return;
    }

    // ----- TEMP (convenience wrapper — same as ADC:TEMP, returns tenths of °C) -----
    if (sub == "TEMP") {
#if defined(ATEMP)
        int raw = analogRead(ATEMP);
#elif defined(AVTEMP)
        int raw = analogRead(AVTEMP);
#else
        analogRead(A0);
        int raw = analogRead(ADC_CHANNEL_TEMPSENSOR);
#endif
        const float v25 = 1430.0f, slope = 4.3f, vdda = 3300.0f;
        float vsense  = (float)raw * vdda / 4095.0f;
        float temp_c  = (v25 - vsense) / slope + 25.0f;
        sendDone(seq, String((int)(temp_c * 10.0f)));
        return;
    }

    // ----- RESET -----
    if (sub == "RESET") {
        // Send reply first, then reset (best-effort)
        sendDone(seq, "RESETTING");
        delay(20);
        NVIC_SystemReset();
        // Never reached
        return;
    }

    sendErr(seq, "SYS:UNKNOWN_SUB");
}

// Called by the main loop whenever a valid frame is received,
// so the slave auto-pets the watchdog as long as the master is communicating.
static void wdogAutoKick() {
    if (wdogEnabled) IWatchdog.reload();
}
