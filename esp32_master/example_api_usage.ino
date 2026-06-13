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
            Serial.printf("Thermocouple: %.2f °C\n", tc * 0.25f);
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

    unsigned long t0 = millis();
    long totalPulses = 0;

    while (millis() - t0 < 5000) {  // count for 5 seconds
        stm.pump();

        String events = stm.irq.poll();  // "A0:N" or "NONE"
        if (events != "NONE" && events.length() > 0) {
            // Parse "A0:12"
            int colon = events.indexOf(':');
            if (colon >= 0) {
                totalPulses += events.substring(colon + 1).toInt();
            }
        }
        delay(100);
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
