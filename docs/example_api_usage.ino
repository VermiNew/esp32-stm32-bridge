/**
 * example_api_usage.ino — demonstrates the STM32 C++ API
 *
 * This is NOT a standalone sketch — it is a reference showing how to
 * use stm32_api.h from within esp32_master.ino (or any sketch that
 * includes it).
 *
 * To use in your own code:
 *   1. Copy the entire esp32_master/ folder into your project
 *   2. Add  #include "stm32_api.h"  after the other includes
 *   3. Declare  STM32 stm;  as a global
 *   4. Call  stm.begin()  at the end of setup()
 *   5. Use stm.gpio / stm.adc / stm.rtc etc. in loop()
 *   6. Call  stm.pump()  regularly in loop()
 *
 * ════════════════════════════════════════════════════════════
 *  All API methods are BLOCKING — they return only after the
 *  slave responds (or after STM32_API_TIMEOUT_MS expires).
 *  Do not call delay() while waiting for results — use the
 *  built-in polling pattern below.
 * ════════════════════════════════════════════════════════════
 */

// ---------------------------------------------------------------------------
// EXAMPLE 1 — Basic GPIO and ADC
// ---------------------------------------------------------------------------
void example_gpio_adc(STM32& stm) {
    // Configure pins
    stm.gpio.mode("A0", GPIO_ANALOG);    // A0 as ADC input
    stm.gpio.mode("C13", GPIO_OUT);      // Blue Pill LED as output

    // Read ADC and drive LED
    int raw = stm.adc.read("A0");
    if (raw != STM32_ERR_INT) {
        int mv = stm.adc.mv("A0");
        Serial.printf("A0: raw=%d  mV=%d\n", raw, mv);

        // Blink LED based on threshold
        stm.gpio.write("C13", raw > 2048 ? 0 : 1);  // active-low LED
    }

    // Read multiple pins at once
    int vals[3];
    int n = stm.adc.multi("A0,A1,B0", vals, 3);
    for (int i = 0; i < n; i++) Serial.printf("  ch%d=%d\n", i, vals[i]);
}

// ---------------------------------------------------------------------------
// EXAMPLE 2 — I2C sensor (MPU6050 gyro/accelerometer at 0x68)
// ---------------------------------------------------------------------------
void example_i2c_mpu6050(STM32& stm) {
    const int MPU = 0x68;

    // Check if sensor is present
    if (!stm.i2c.ping(MPU)) {
        Serial.println("MPU6050 not found on I2C1");
        return;
    }

    // Wake up MPU6050: write 0x00 to PWR_MGMT_1 (register 0x6B)
    stm.i2c.writeReg(MPU, 0x6B, "00");

    // Read 6 bytes of accelerometer data from register 0x3B
    String hexData = stm.i2c.readReg(MPU, 0x3B, 6);
    if (hexData.length() == 12) {
        // Parse big-endian int16 values
        int16_t ax = (int16_t)((strtol(hexData.substring(0,2).c_str(),  nullptr, 16) << 8) |
                                strtol(hexData.substring(2,4).c_str(),  nullptr, 16));
        int16_t ay = (int16_t)((strtol(hexData.substring(4,6).c_str(),  nullptr, 16) << 8) |
                                strtol(hexData.substring(6,8).c_str(),  nullptr, 16));
        int16_t az = (int16_t)((strtol(hexData.substring(8,10).c_str(), nullptr, 16) << 8) |
                                strtol(hexData.substring(10,12).c_str(),nullptr, 16));
        Serial.printf("Accel  X=%d  Y=%d  Z=%d\n", ax, ay, az);
    }
}

// ---------------------------------------------------------------------------
// EXAMPLE 3 — SPI (e.g. MAX31855 thermocouple, CS on B12)
// ---------------------------------------------------------------------------
void example_spi_thermocouple(STM32& stm) {
    // MAX31855 — SPI mode 0, 5 MHz
    if (!stm.spi.begin("B12", 0, 5000)) {
        Serial.println("SPI init failed");
        return;
    }

    // Read 4 bytes (send 4 dummy bytes)
    String rx = stm.spi.read("B12", 4);
    stm.spi.end();

    if (rx.length() == 8) {
        uint32_t raw = (uint32_t)strtoul(rx.c_str(), nullptr, 16);
        if (!(raw & 0x7)) {   // no fault bits
            // Bits [31:18] = 14-bit thermocouple temp, 0.25°C/LSB
            int16_t tc = (int16_t)((raw >> 18) & 0x3FFF);
            if (tc & 0x2000) tc |= 0xC000;  // sign extend
            Serial.printf("Thermocouple: %.2f C\n", tc * 0.25f);
        }
    }
}

// ---------------------------------------------------------------------------
// EXAMPLE 4 — EEPROM: save/restore calibration value
// ---------------------------------------------------------------------------
void example_eeprom_calibration(STM32& stm) {
    const int CAL_ADDR = 0;
    const uint32_t MAGIC = 0xCAFEBABEUL;

    // Check magic word
    uint32_t magic = stm.ee.readWord(CAL_ADDR);
    if (magic == MAGIC) {
        // Load saved calibration
        uint32_t cal = stm.ee.readWord(CAL_ADDR + 4);
        Serial.printf("Loaded calibration: %lu\n", (unsigned long)cal);
    } else {
        // First run — write defaults
        uint32_t defaultCal = 1000;
        stm.ee.writeWord(CAL_ADDR,     MAGIC);
        stm.ee.writeWord(CAL_ADDR + 4, defaultCal);
        Serial.println("Wrote default calibration.");
    }
}

// ---------------------------------------------------------------------------
// EXAMPLE 5 — RTC + NTP sync
// ---------------------------------------------------------------------------
void example_rtc_ntp(STM32& stm) {
    // Ensure RTC is running
    if (!stm.rtc.init()) {
        Serial.println("RTC init failed — check crystal on PC14/PC15");
        return;
    }

    // Print current time
    Serial.println("RTC time: " + stm.rtc.get());

    // Sync via NTP (WiFi must be connected first)
    // ntpSync() is defined in wifi_ntp.h and handles the RTC:SETTSS chain
    // automatically through the protocol state machine.
    // Here we just demonstrate direct set:
    stm.rtc.set(2025, 6, 15, 12, 0, 0);
    Serial.println("After set: " + stm.rtc.get());
    Serial.printf("Epoch-2000: %lu\n", (unsigned long)stm.rtc.getTimestamp());
}

// ---------------------------------------------------------------------------
// EXAMPLE 6 — CAN bus loopback self-test
// ---------------------------------------------------------------------------
void example_can_loopback(STM32& stm) {
    // Loopback mode — no transceiver needed
    stm.can.setNoWarn(true);  // suppress warning (we know we're in loopback)
    if (!stm.can.begin(250, "LOOPBACK")) {
        Serial.println("CAN init failed");
        return;
    }

    // Send a test frame
    stm.can.tx(0x123, "DEADBEEF");

    // Read it back
    String frame = stm.can.rx();
    Serial.println("CAN RX: " + frame);
    // Expected: "291:DEADBEEF:0:0"  (291 decimal = 0x123)

    stm.can.end();
}

// ---------------------------------------------------------------------------
// EXAMPLE 7 — External interrupt event counter
// ---------------------------------------------------------------------------
void example_irq_counter(STM32& stm) {
    // Count rising edges on A0 (e.g. from a button or encoder)
    stm.irq.attach("A0", "RISE");

    unsigned long t0       = millis();
    unsigned long lastPoll = 0;
    long totalPulses = 0;

    while (millis() - t0 < 5000) {  // count for 5 seconds
        stm.pump();  // keep UART + heartbeat running — never delay() here

        // Poll IRQ at most every 100 ms without blocking
        if (millis() - lastPoll >= 100) {
            lastPoll = millis();
            String events = stm.irq.poll();  // "A0:N" or "NONE"
            if (events != "NONE" && events.length() > 0) {
                int colon = events.indexOf(':');
                if (colon >= 0) totalPulses += events.substring(colon + 1).toInt();
            }
        }
    }

    stm.irq.detach("A0");
    Serial.printf("Pulses in 5 s: %ld  (approx %.1f Hz)\n",
                  totalPulses, totalPulses / 5.0f);
}

// ---------------------------------------------------------------------------
// EXAMPLE 8 — ADC streaming (burst sampler, oscilloscope-style)
// ---------------------------------------------------------------------------
void example_adc_stream(STM32& stm) {
    // Sample A0 32 times at 1 ms intervals → 32 ms total
    String hexStream = stm.adc.stream("A0", 32, 1);

    if (stm.ok() && hexStream.length() == 128) {  // 32 * 4 hex chars
        Serial.print("ADC stream (32 samples): ");
        for (int i = 0; i < 32; i++) {
            uint16_t val = (uint16_t)strtoul(hexStream.substring(i*4, i*4+4).c_str(),
                                             nullptr, 16);
            Serial.printf("%4d ", val);
            if ((i + 1) % 8 == 0) Serial.println();
        }
    }
}

// ---------------------------------------------------------------------------
// EXAMPLE 9 — Error handling pattern
// ---------------------------------------------------------------------------
void example_error_handling(STM32& stm) {
    // Try to read from a pin that might not be configured
    int val = stm.adc.read("A5");

    if (!stm.ok()) {
        Serial.println("ADC error: " + stm.error());
        // Recover — configure the pin first
        stm.gpio.mode("A5", GPIO_ANALOG);
        val = stm.adc.read("A5");
    }

    if (stm.ok()) {
        Serial.println("A5 = " + String(val));
    }

    // Use raw execute for a command not in the API
    String result = stm.execute("SYS:ECHO:custom_command");
    Serial.println("Echo: " + result);
}

// ---------------------------------------------------------------------------
// EXAMPLE 10 — Watchdog enable (keep slave alive over long sessions)
// ---------------------------------------------------------------------------
void example_watchdog(STM32& stm) {
    // Arm 10-second IWDG on the slave.
    // The master will automatically send SYS:WDOG:KICK every 5 s
    // (handled by wdogForwardTick() in esp32_master.ino loop).
    stm.sys.wdogEnable(10000);
    Serial.println("Slave watchdog armed at 10 s.");

    // If the master loses connection or crashes, the slave resets after 10 s.
}

// ---------------------------------------------------------------------------
// EXAMPLE 11 — DAC voltage sweep (PA4 = DAC1, PA5 = DAC2)
//
// Ramps DAC1 from 0 V to 3.3 V in 100 steps, then turns off.
// Also demonstrates reading back the last set value.
// CONFLICT: PA4/PA5 shared with SPI1 — do not use DAC and SPI simultaneously.
// ---------------------------------------------------------------------------
void example_dac_sweep(STM32& stm) {
    Serial.println("DAC sweep: 0 V → 3.3 V on PA4 (DAC1)");

    for (int mv = 0; mv <= 3300; mv += 33) {
        stm.dac.mv(1, mv);
        if (!stm.ok()) {
            Serial.println("DAC error: " + stm.error());
            break;
        }
        // Read back the raw value the slave stored
        int raw = stm.dac.read(1);
        Serial.printf("  %4d mV → raw=%d\n", mv, raw);
        // No delay() here — each API call already takes ~5 ms round-trip
    }

    // Output a fixed 1.65 V (mid-rail) on DAC2
    stm.dac.mv(2, 1650);
    Serial.printf("DAC2 set to 1650 mV, raw=%d\n", stm.dac.read(2));

    // Timed hold — pump() keeps the link alive instead of delay()
    unsigned long t0 = millis();
    while (millis() - t0 < 2000) stm.pump();

    // Turn both channels off
    stm.dac.off();  // ch=0 → both channels
    Serial.println("DAC off.");
}

// ---------------------------------------------------------------------------
// EXAMPLE 12 — Buzzer: startup jingle + status check
//
// Plays a short melody on a passive buzzer connected to PA8.
// buzzerTick() in the slave loop handles auto-stop non-blocking.
// ---------------------------------------------------------------------------
void example_buzzer_jingle(STM32& stm) {
    const char* PIN = "A8";  // PWM-capable pin (TIM1_CH1)

    // Play three ascending tones, each 150 ms
    uint32_t notes[] = { 523, 659, 784 };  // C5, E5, G5
    for (int i = 0; i < 3; i++) {
        stm.buzzer.tone(PIN, notes[i], 150);
        if (!stm.ok()) { Serial.println("Buzzer error: " + stm.error()); return; }
        // Wait for the note to finish (150 ms), keeping link alive
        unsigned long t0 = millis();
        while (millis() - t0 < 160) stm.pump();
    }

    // Hold a victory chord (G5) for 400 ms
    stm.buzzer.tone(PIN, 784, 400);

    // Poll status while it plays
    unsigned long t0 = millis();
    while (millis() - t0 < 450) {
        stm.pump();
        if (millis() - t0 > 50) {
            bool playing = stm.buzzer.isPlaying(PIN);
            Serial.printf("  buzzer playing: %s\n", playing ? "yes" : "no");
            t0 = millis() + 9999;  // only poll once
        }
    }

    // Quick confirmation beep
    stm.buzzer.beep(PIN);
    unsigned long tw = millis();
    while (millis() - tw < 110) stm.pump();
    Serial.println("Jingle done.");
}

// ---------------------------------------------------------------------------
// EXAMPLE 13 — Debug LEDs: attach, run a few commands, detach
//
// Attach two LEDs on the STM32 side to visualise protocol traffic:
//   RX LED (B0) — blinks on every SEND frame arriving from the master
//   TX LED (B1) — blinks on every DONE/ERR frame the slave sends back
// ---------------------------------------------------------------------------
void example_debug_leds(STM32& stm) {
    // Attach LEDs
    if (!stm.debug.attach("B0", "B1")) {
        Serial.println("debug attach failed: " + stm.error());
        return;
    }
    Serial.println("Debug LEDs: " + stm.debug.status());

    // Run a few commands — watch the LEDs blink
    for (int i = 0; i < 5; i++) {
        stm.adc.read("A0");
        stm.pump();
    }

    // Detach (releases B0 and B1)
    stm.debug.detach();
    Serial.println("Debug LEDs detached.");
}

// ---------------------------------------------------------------------------
// EXAMPLE 14 — PWM with custom frequency + freqRead
//
// Drives a servo-style signal on A8: 50 Hz, 1.5 ms pulse = 75/1000 duty.
// Then reads back the frequency stored on the slave to verify.
// ---------------------------------------------------------------------------
void example_pwm_servo(STM32& stm) {
    const char* PIN = "A8";  // TIM1_CH1

    // 50 Hz, 7.5% duty = ~1.5 ms high → servo neutral
    stm.pwm.freq(PIN, 50, 75);
    if (!stm.ok()) { Serial.println("PWM error: " + stm.error()); return; }

    long hz   = stm.pwm.freqRead(PIN);
    int  duty = stm.pwm.read(PIN);
    Serial.printf("PWM on %s: %ld Hz, duty=%d/1000\n", PIN, hz, duty);

    // Hold for 2 s, then sweep duty 75→125 (1.5 ms → ~2.5 ms, servo full-right)
    unsigned long t0 = millis();
    while (millis() - t0 < 2000) stm.pump();

    for (int d = 75; d <= 125; d += 5) {
        stm.pwm.freq(PIN, 50, d);
        unsigned long tw = millis();
        while (millis() - tw < 20) stm.pump();  // 20 ms per step
    }

    stm.pwm.stop(PIN);
    Serial.println("Servo stopped.");
}

// ---------------------------------------------------------------------------
// EXAMPLE 15 — Async API: send command, do other work, collect result
//
// asyncSend() returns immediately; asyncPoll() checks completion each loop().
// Useful when the slave command takes a while (e.g. I2C scan, ADC stream).
// ---------------------------------------------------------------------------

static bool asyncDone  = false;
static bool asyncError = false;

void asyncCallback(const String& result, bool error) {
    asyncDone  = true;
    asyncError = error;
    Serial.printf("Async result: %s  (ok=%d)\n", result.c_str(), !error);
}

void example_async(STM32& stm) {
    // Kick off a slow I2C scan without blocking
    if (!stm.asyncSend("I2C:SCAN", asyncCallback)) {
        Serial.println("Busy — cannot start async command.");
        return;
    }
    Serial.println("Async I2C scan started, doing other work...");

    asyncDone = false;
    unsigned long t0 = millis();

    while (!asyncDone && millis() - t0 < 30000) {
        stm.pump();          // drives the state machine + fires callback on completion
        // ... other non-blocking work here ...
    }

    if (!asyncDone) Serial.println("Async scan timed out.");
}

// ---------------------------------------------------------------------------
// EXAMPLE 16 — Link health check + system info
//
// Demonstrates reading slave system metrics and checking the link state.
// Useful as a periodic keep-alive health report.
// ---------------------------------------------------------------------------
void example_health_check(STM32& stm) {
    // Link state (CONNECTED / DEGRADED / DISCONNECTED — from heartbeat engine)
    Serial.println("Link: " + stm.linkStatus());

    // System status
    Serial.println("SYS STATUS: " + stm.sys.status());
    Serial.printf("Uptime:  %ld ms\n", stm.sys.uptime());
    Serial.printf("FW ver:  %s\n",      stm.sys.fwVer().c_str());
    Serial.printf("Chip ID: %s\n",      stm.sys.chipId().c_str());
    Serial.printf("CPU:     %d MHz\n",  stm.sys.cpuMhz());
    Serial.printf("RAM:     %d bytes free\n", stm.sys.freeRam());

    // Internal temperature — both APIs exposed (ADC and SYS)
    int tempAdc = stm.adc.temperature();   // ADC:TEMP path
    int tempSys = stm.sys.temperature();   // SYS:TEMP path — identical reading
    Serial.printf("Temp (ADC):  %.1f C\n", tempAdc / 10.0f);
    Serial.printf("Temp (SYS):  %.1f C\n", tempSys / 10.0f);

    // Round-trip latency (echo test)
    unsigned long t0 = millis();
    String echo = stm.sys.echo("ping");
    unsigned long rtt = millis() - t0;
    Serial.printf("Echo '%s'  RTT ~%lu ms\n", echo.c_str(), rtt);
}

// ---------------------------------------------------------------------------
// EXAMPLE 17 — I2C with custom timeout (slow/clock-stretched device)
//
// Some I2C devices (e.g. soil moisture sensors) stretch the clock heavily.
// Use i2c.setTimeout() to raise the Wire timeout before talking to them.
// ---------------------------------------------------------------------------
void example_i2c_slow_device(STM32& stm) {
    const int SENSOR_ADDR = 0x20;

    // Raise timeout to 500 ms for this slow device
    stm.i2c.setTimeout(500);

    if (!stm.i2c.ping(SENSOR_ADDR)) {
        Serial.println("Slow sensor not found — reverting timeout");
        stm.i2c.setTimeout(25);  // restore default
        return;
    }

    // Read 2 bytes
    String raw = stm.i2c.read(SENSOR_ADDR, 2);
    if (stm.ok() && raw.length() == 4) {
        uint16_t val = (uint16_t)strtoul(raw.c_str(), nullptr, 16);
        Serial.printf("Sensor raw value: %u\n", val);
    }

    stm.i2c.setTimeout(25);  // restore default
}
