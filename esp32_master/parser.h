#pragma once
/**
 * parser.h — human command parser for esp32_master
 *
 * Translates CLI input (from Serial Monitor) into protocol DATA strings
 * that are sent as  SEND:NNN:CCCC:DATA  to the STM32 slave.
 *
 * Returns:
 *   "" if the command was handled locally (help, empty line)
 *   "__PING__"  for a bare PING
 *   "__RESET__" for a protocol reset
 *   "__HB__"    for a manual heartbeat test
 *   otherwise the DATA string to put inside a SEND frame
 *
 * All tokens are case-insensitive on input; protocol strings are uppercase.
 */

// Forward declarations (defined in esp32_master.ino)
void printHelp();
void logOk  (const String& msg);
void logErr (const String& msg);
void logWarn(const String& msg);
void logInfo(const String& msg);

// show ports state (defined in esp32_master.ino)
extern bool portsPending;
extern bool portsPendingFreeOnly;

// master debug LED functions (defined in esp32_master.ino)
void masterDebugAttach(int txPin, int rxPin);
void masterDebugDetach();

// Forward declarations (defined in wifi_ntp.h — included before parser.h)
void wifiConnect(const String& ssid, const String& password);
void wifiStatus();
void wifiScan();
void ntpSync();
void ntpStatus();
extern String ntpServer;
extern String storedSsid;
extern String storedPass;

// ---------------------------------------------------------------------------
// Parser-local state
// ---------------------------------------------------------------------------

// When true, suppress the "transceiver required" warning for CAN normal mode.
// Set via: can nowarn   |   cleared via: can warn
static bool canNoWarn = false;

// Normalise a pin token to uppercase (e.g. "a0" -> "A0")
static inline String normPin(const String& s) {
    String t = s; t.toUpperCase(); return t;
}

static String parseHumanCmd(const String& raw) {
    String input = raw; input.trim();
    if (!input.length()) return "";

    // Lowercase for matching, but we'll keep original tokens for pin names etc.
    String low = input; low.toLowerCase();

    // Tokenize on whitespace
    String tok[10];
    int ntok = 0;
    {
        int i = 0, slen = input.length();
        while (i < slen && ntok < 10) {
            while (i < slen && input.charAt(i) == ' ') i++;
            if (i >= slen) break;
            int start = i;
            while (i < slen && input.charAt(i) != ' ') i++;
            tok[ntok++] = input.substring(start, i);
        }
    }
    if (!ntok) return "";

    String t0 = tok[0]; t0.toLowerCase();

    // ------------------------------------------------------------------ help
    if (t0 == "help" || t0 == "?") { printHelp(); return ""; }

    // ------------------------------------------------------------------ ping
    if (t0 == "ping") return "__PING__";

    // ------------------------------------------------------------------ reset
    if (t0 == "reset") return "__RESET__";

    // ------------------------------------------------------------------ heartbeat
    if (t0 == "heartbeat" || t0 == "hb") return "__HB__";

    // ------------------------------------------------------------------ gpio
    if (t0 == "gpio") {
        if (ntok < 2) { logErr("Usage: gpio mode|write|read|toggle|port ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "mode") {
            if (ntok < 4) { logErr("Usage: gpio mode <pin> in|out|pu|pd|an|od"); return ""; }
            String m = tok[3]; m.toUpperCase();
            return "GPIO:MODE:" + normPin(tok[2]) + ":" + m;
        }
        if (t1 == "write" || t1 == "w") {
            if (ntok < 4) { logErr("Usage: gpio write <pin> 0|1"); return ""; }
            return "GPIO:WRITE:" + normPin(tok[2]) + ":" + tok[3];
        }
        if (t1 == "read" || t1 == "r") {
            if (ntok < 3) { logErr("Usage: gpio read <pin>"); return ""; }
            return "GPIO:READ:" + normPin(tok[2]);
        }
        if (t1 == "toggle" || t1 == "t") {
            if (ntok < 3) { logErr("Usage: gpio toggle <pin>"); return ""; }
            return "GPIO:TOGGLE:" + normPin(tok[2]);
        }
        if (t1 == "port") {
            if (ntok < 3) { logErr("Usage: gpio port A|B|C"); return ""; }
            String p = tok[2]; p.toUpperCase();
            return "GPIO:PORT:" + p;
        }
        logErr("gpio sub-command: mode|write|read|toggle|port");
        return "";
    }

    // ------------------------------------------------------------------ adc
    if (t0 == "adc") {
        if (ntok < 2) { logErr("Usage: adc read|avg|mv|multi|temp|vref ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "read" || t1 == "r") {
            if (ntok < 3) { logErr("Usage: adc read <pin>"); return ""; }
            return "ADC:READ:" + normPin(tok[2]);
        }
        if (t1 == "avg") {
            if (ntok < 4) { logErr("Usage: adc avg <pin> <samples>"); return ""; }
            return "ADC:AVG:" + normPin(tok[2]) + ":" + tok[3];
        }
        if (t1 == "mv") {
            if (ntok < 3) { logErr("Usage: adc mv <pin>"); return ""; }
            return "ADC:MV:" + normPin(tok[2]);
        }
        if (t1 == "multi") {
            // tok[2..] are comma-separated or space-separated pins
            if (ntok < 3) { logErr("Usage: adc multi A0,A1,B0 OR adc multi A0 A1 B0"); return ""; }
            // Check if tok[2] already has commas
            String pinList;
            if (tok[2].indexOf(',') >= 0) {
                pinList = tok[2]; pinList.toUpperCase();
            } else {
                // Space-separated — join with comma
                for (int i = 2; i < ntok; i++) {
                    if (i > 2) pinList += ',';
                    String p = tok[i]; p.toUpperCase(); pinList += p;
                }
            }
            return "ADC:MULTI:" + pinList;
        }
        if (t1 == "temp") return "ADC:TEMP";
        if (t1 == "vref") return "ADC:VREF";
        if (t1 == "stream") {
            if (ntok < 5) { logErr("Usage: adc stream <pin> <n_samples 1-48> <interval_ms>"); return ""; }
            return "ADC:STREAM:" + normPin(tok[2]) + ":" + tok[3] + ":" + tok[4];
        }
        logErr("adc sub-command: read|avg|mv|multi|temp|vref|stream");
        return "";
    }

    // ------------------------------------------------------------------ pwm
    if (t0 == "pwm") {
        if (ntok < 2) { logErr("Usage: pwm set|freq|stop|read ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "set") {
            if (ntok < 4) { logErr("Usage: pwm set <pin> <duty 0-1000>"); return ""; }
            return "PWM:SET:" + normPin(tok[2]) + ":" + tok[3];
        }
        if (t1 == "freq") {
            if (ntok < 5) { logErr("Usage: pwm freq <pin> <hz> <duty 0-1000>"); return ""; }
            return "PWM:FREQ:" + normPin(tok[2]) + ":" + tok[3] + ":" + tok[4];
        }
        if (t1 == "stop") {
            if (ntok < 3) { logErr("Usage: pwm stop <pin>"); return ""; }
            return "PWM:STOP:" + normPin(tok[2]);
        }
        if (t1 == "read") {
            if (ntok < 3) { logErr("Usage: pwm read <pin>"); return ""; }
            return "PWM:READ:" + normPin(tok[2]);
        }
        if (t1 == "freqread") {
            if (ntok < 3) { logErr("Usage: pwm freqread <pin>"); return ""; }
            return "PWM:FREQREAD:" + normPin(tok[2]);
        }
        logErr("pwm sub-command: set|freq|stop|read|freqread");
        return "";
    }

    // ------------------------------------------------------------------ i2c
    if (t0 == "i2c") {
        if (ntok < 2) { logErr("Usage: i2c scan|ping|write|read|wreg|rreg ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "cfg") {
            if (ntok < 3) { logErr("Usage: i2c cfg 100|400 [timeout_ms]"); return ""; }
            String r = "I2C:CFG:" + tok[2];
            if (ntok >= 4) r += ":" + tok[3];
            return r;
        }
        if (t1 == "scan") return "I2C:SCAN";
        if (t1 == "ping") {
            if (ntok < 3) { logErr("Usage: i2c ping <addr>"); return ""; }
            return "I2C:PING:" + tok[2];
        }
        if (t1 == "write") {
            if (ntok < 4) { logErr("Usage: i2c write <addr> <hexbytes>"); return ""; }
            String h = tok[3]; h.toUpperCase();
            return "I2C:WRITE:" + tok[2] + ":" + h;
        }
        if (t1 == "read") {
            if (ntok < 4) { logErr("Usage: i2c read <addr> <n_bytes>"); return ""; }
            return "I2C:READ:" + tok[2] + ":" + tok[3];
        }
        if (t1 == "wreg") {
            if (ntok < 5) { logErr("Usage: i2c wreg <addr> <reg> <hexbytes>"); return ""; }
            String h = tok[4]; h.toUpperCase();
            return "I2C:WREG:" + tok[2] + ":" + tok[3] + ":" + h;
        }
        if (t1 == "rreg") {
            if (ntok < 5) { logErr("Usage: i2c rreg <addr> <reg> <n_bytes>"); return ""; }
            return "I2C:RREG:" + tok[2] + ":" + tok[3] + ":" + tok[4];
        }
        logErr("i2c sub-command: scan|ping|write|read|wreg|rreg");
        return "";
    }

    // ------------------------------------------------------------------ spi
    if (t0 == "spi") {
        if (ntok < 2) { logErr("Usage: spi begin|xfer|write|read|end ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "begin") {
            if (ntok < 5) { logErr("Usage: spi begin <cs_pin> <mode 0-3> <freq_khz>"); return ""; }
            return "SPI:BEGIN:" + normPin(tok[2]) + ":" + tok[3] + ":" + tok[4];
        }
        if (t1 == "xfer") {
            if (ntok < 4) { logErr("Usage: spi xfer <cs_pin> <hexbytes>"); return ""; }
            String h = tok[3]; h.toUpperCase();
            return "SPI:XFER:" + normPin(tok[2]) + ":" + h;
        }
        if (t1 == "write") {
            if (ntok < 4) { logErr("Usage: spi write <cs_pin> <hexbytes>"); return ""; }
            String h = tok[3]; h.toUpperCase();
            return "SPI:WRITE:" + normPin(tok[2]) + ":" + h;
        }
        if (t1 == "read") {
            if (ntok < 4) { logErr("Usage: spi read <cs_pin> <n_bytes>"); return ""; }
            return "SPI:READ:" + normPin(tok[2]) + ":" + tok[3];
        }
        if (t1 == "end") return "SPI:END";
        logErr("spi sub-command: begin|xfer|write|read|end");
        return "";
    }

    // ------------------------------------------------------------------ u2
    if (t0 == "u2") {
        if (ntok < 2) { logErr("Usage: u2 cfg|tx|rx|flush|status|close ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "cfg") {
            if (ntok < 3) { logErr("Usage: u2 cfg <baud> [bits parity stop]"); return ""; }
            String r = "U2:CFG:" + tok[2];
            if (ntok >= 6) r += ":" + tok[3] + ":" + tok[4] + ":" + tok[5];
            return r;
        }
        if (t1 == "tx") {
            if (ntok < 3) { logErr("Usage: u2 tx <hexbytes>"); return ""; }
            String h = tok[2]; h.toUpperCase(); return "U2:TX:" + h;
        }
        if (t1 == "rx") {
            if (ntok < 3) { logErr("Usage: u2 rx <n_bytes> [timeout_ms]"); return ""; }
            String r = "U2:RX:" + tok[2];
            if (ntok >= 4) r += ":" + tok[3];
            return r;
        }
        if (t1 == "flush")  return "U2:FLUSH";
        if (t1 == "status") return "U2:STATUS";
        if (t1 == "close")  return "U2:CLOSE";
        logErr("u2 sub-command: cfg|tx|rx|flush|status|close");
        return "";
    }

    // ------------------------------------------------------------------ ee
    if (t0 == "ee") {
        if (ntok < 2) { logErr("Usage: ee write|read|wrword|rdword|wrhex|rdhex|fill|size ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "write")  {
            if (ntok < 4) { logErr("Usage: ee write <addr> <byte>"); return ""; }
            return "EE:WRITE:" + tok[2] + ":" + tok[3];
        }
        if (t1 == "read")   { if (ntok < 3) { logErr("Usage: ee read <addr>"); return ""; } return "EE:READ:" + tok[2]; }
        if (t1 == "wrword") {
            if (ntok < 4) { logErr("Usage: ee wrword <addr> <uint32>"); return ""; }
            return "EE:WRWORD:" + tok[2] + ":" + tok[3];
        }
        if (t1 == "rdword") { if (ntok < 3) { logErr("Usage: ee rdword <addr>"); return ""; } return "EE:RDWORD:" + tok[2]; }
        if (t1 == "wrhex")  {
            if (ntok < 4) { logErr("Usage: ee wrhex <addr> <hexbytes>"); return ""; }
            String h = tok[3]; h.toUpperCase(); return "EE:WRHEX:" + tok[2] + ":" + h;
        }
        if (t1 == "rdhex")  {
            if (ntok < 4) { logErr("Usage: ee rdhex <addr> <n_bytes>"); return ""; }
            return "EE:RDHEX:" + tok[2] + ":" + tok[3];
        }
        if (t1 == "fill")   { if (ntok < 3) { logErr("Usage: ee fill <byte>"); return ""; } return "EE:FILL:" + tok[2]; }
        if (t1 == "size")   return "EE:SIZE";
        logErr("ee sub-command: write|read|wrword|rdword|wrhex|rdhex|fill|size");
        return "";
    }

    // ------------------------------------------------------------------ irq
    if (t0 == "irq") {
        if (ntok < 2) { logErr("Usage: irq attach|detach|poll|list ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "attach") {
            if (ntok < 4) { logErr("Usage: irq attach <pin> rise|fall|change"); return ""; }
            String m = tok[3]; m.toUpperCase();
            return "IRQ:ATTACH:" + normPin(tok[2]) + ":" + m;
        }
        if (t1 == "detach") {
            if (ntok < 3) { logErr("Usage: irq detach <pin>|all"); return ""; }
            return "IRQ:DETACH:" + normPin(tok[2]);
        }
        if (t1 == "poll") return "IRQ:POLL";
        if (t1 == "list") return "IRQ:LIST";
        logErr("irq sub-command: attach|detach|poll|list");
        return "";
    }

    // ------------------------------------------------------------------ sys
    if (t0 == "sys") {
        if (ntok < 2) { logErr("Usage: sys status|uptime|chipid|cpufreq|fwver|reset|freeram|echo|wdog ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "status")  return "SYS:STATUS";
        if (t1 == "uptime")  return "SYS:UPTIME";
        if (t1 == "chipid")  return "SYS:CHIPID";
        if (t1 == "cpufreq") return "SYS:CPUFREQ";
        if (t1 == "fwver")   return "SYS:FWVER";
        if (t1 == "freeram") return "SYS:FREERAM";
        if (t1 == "temp")    return "SYS:TEMP";
        if (t1 == "reset")   return "SYS:RESET";
        if (t1 == "echo") {
            String txt = (ntok >= 3) ? tok[2] : "hello";
            return "SYS:ECHO:" + txt;
        }
        if (t1 == "wdog") {
            if (ntok < 3) { logErr("Usage: sys wdog en <ms>|kick|dis"); return ""; }
            String t2 = tok[2]; t2.toUpperCase();
            if (t2 == "EN") {
                if (ntok < 4) { logErr("Usage: sys wdog en <ms>"); return ""; }
                return "SYS:WDOG:EN:" + tok[3];
            }
            if (t2 == "KICK") return "SYS:WDOG:KICK";
            if (t2 == "DIS")  return "SYS:WDOG:DIS";
            logErr("sys wdog sub: en|kick|dis");
            return "";
        }
        logErr("sys sub-command: status|uptime|chipid|cpufreq|fwver|reset|freeram|echo|wdog");
        return "";
    }

    // ------------------------------------------------------------------ calc
    if (t0 == "calc") {
        if (ntok < 2) { logErr("Usage: calc map|crc16|sqrt|constrain|abs ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "map") {
            if (ntok < 7) { logErr("Usage: calc map <v> <in_min> <in_max> <out_min> <out_max>"); return ""; }
            return "CALC:MAP:" + tok[2] + ":" + tok[3] + ":" + tok[4] + ":" + tok[5] + ":" + tok[6];
        }
        if (t1 == "crc16") {
            if (ntok < 3) { logErr("Usage: calc crc16 <hexbytes>"); return ""; }
            String h = tok[2]; h.toUpperCase(); return "CALC:CRC16:" + h;
        }
        if (t1 == "sqrt") {
            if (ntok < 3) { logErr("Usage: calc sqrt <n>"); return ""; }
            return "CALC:SQRT:" + tok[2];
        }
        if (t1 == "constrain") {
            if (ntok < 5) { logErr("Usage: calc constrain <v> <lo> <hi>"); return ""; }
            return "CALC:CONSTRAIN:" + tok[2] + ":" + tok[3] + ":" + tok[4];
        }
        if (t1 == "abs") {
            if (ntok < 3) { logErr("Usage: calc abs <v>"); return ""; }
            return "CALC:ABS:" + tok[2];
        }
        logErr("calc sub-command: map|crc16|sqrt|constrain|abs");
        return "";
    }

    // ------------------------------------------------------------------ i2c2
    if (t0 == "i2c2") {
        // Reuse i2c logic — just prefix "I2C2" instead of "I2C"
        if (ntok < 2) { logErr("Usage: i2c2 cfg|scan|ping|write|read|wreg|rreg ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();
        if (t1 == "cfg") {
            if (ntok < 3) { logErr("Usage: i2c2 cfg 100|400 [timeout_ms]"); return ""; }
            String r = "I2C2:CFG:" + tok[2];
            if (ntok >= 4) r += ":" + tok[3];
            return r;
        }
        if (t1 == "scan") return "I2C2:SCAN";
        if (t1 == "ping") {
            if (ntok < 3) { logErr("Usage: i2c2 ping <addr>"); return ""; }
            return "I2C2:PING:" + tok[2];
        }
        if (t1 == "write") {
            if (ntok < 4) { logErr("Usage: i2c2 write <addr> <hexbytes>"); return ""; }
            String h = tok[3]; h.toUpperCase(); return "I2C2:WRITE:" + tok[2] + ":" + h;
        }
        if (t1 == "read") {
            if (ntok < 4) { logErr("Usage: i2c2 read <addr> <n>"); return ""; }
            return "I2C2:READ:" + tok[2] + ":" + tok[3];
        }
        if (t1 == "wreg") {
            if (ntok < 5) { logErr("Usage: i2c2 wreg <addr> <reg> <hexbytes>"); return ""; }
            String h = tok[4]; h.toUpperCase(); return "I2C2:WREG:" + tok[2] + ":" + tok[3] + ":" + h;
        }
        if (t1 == "rreg") {
            if (ntok < 5) { logErr("Usage: i2c2 rreg <addr> <reg> <n>"); return ""; }
            return "I2C2:RREG:" + tok[2] + ":" + tok[3] + ":" + tok[4];
        }
        logErr("i2c2 sub: cfg|scan|ping|write|read|wreg|rreg");
        return "";
    }

    // ------------------------------------------------------------------ u3
    if (t0 == "u3") {
        if (ntok < 2) { logErr("Usage: u3 cfg|tx|rx|flush|status|close ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();
        if (t1 == "cfg") {
            if (ntok < 3) { logErr("Usage: u3 cfg <baud> [bits parity stop]"); return ""; }
            String r = "U3:CFG:" + tok[2];
            if (ntok >= 6) r += ":" + tok[3] + ":" + tok[4] + ":" + tok[5];
            return r;
        }
        if (t1 == "tx")     { if (ntok<3){logErr("u3 tx <hex>");return"";}  String h=tok[2];h.toUpperCase();return "U3:TX:"+h; }
        if (t1 == "rx")     { if (ntok<3){logErr("u3 rx <n> [ms]");return "";} String r="U3:RX:"+tok[2]; if(ntok>=4)r+=":"+tok[3]; return r; }
        if (t1 == "flush")  return "U3:FLUSH";
        if (t1 == "status") return "U3:STATUS";
        if (t1 == "close")  return "U3:CLOSE";
        logErr("u3 sub: cfg|tx|rx|flush|status|close");
        return "";
    }

    // ------------------------------------------------------------------ can
    if (t0 == "can") {
        if (ntok < 2) { logErr("Usage: can begin|tx|txe|rx|filter|status|end|nowarn|warn ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        // ---- nowarn / warn ----
        if (t1 == "nowarn") {
            canNoWarn = true;
            logOk("CAN transceiver warning suppressed locally.");
            return "CAN:NOWARN";   // also suppress on slave side
        }
        if (t1 == "warn") {
            canNoWarn = false;
            logOk("CAN transceiver warning restored.");
            return "CAN:WARN";     // also restore on slave side
        }

        if (t1 == "begin") {
            if (ntok < 3) { logErr("Usage: can begin 125|250|500|1000 [loopback|silent|normal]"); return ""; }
            String mode = (ntok >= 4) ? tok[3] : "normal";
            mode.toUpperCase();

            // Show transceiver warning for normal mode (unless suppressed)
            if (mode == "NORMAL" && !canNoWarn) {
                Serial.println();
                Serial.println(CLR_AMBER "  ╔══════════════════════════════════════════════════════════╗" CLR_RESET);
                Serial.println(CLR_AMBER "  ║  ⚠  CAN NORMAL MODE — TRANSCEIVER REQUIRED             ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ╠══════════════════════════════════════════════════════════╣" CLR_RESET);
                Serial.println(CLR_AMBER "  ║  PB8 (RX) and PB9 (TX) must be connected to a CAN      ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║  transceiver, e.g.:                                     ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║    TJA1050  — 5V supply, 3.3V signal tolerant           ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║    SN65HVD230 — 3.3V native (recommended for Blue Pill) ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║                                                          ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║  Without a transceiver the STM32 will go BUS-OFF        ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║  immediately and no frames will be sent or received.    ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║                                                          ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║  No transceiver?  Use loopback mode instead:            ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║    can begin " + tok[2] + " loopback                             ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║                                                          ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║  To suppress this warning permanently:                  ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ║    can nowarn                                            ║" CLR_RESET);
                Serial.println(CLR_AMBER "  ╚══════════════════════════════════════════════════════════╝" CLR_RESET);
                Serial.println();
            }

            return "CAN:BEGIN:" + tok[2] + ":" + mode;
        }
        if (t1 == "tx" || t1 == "txe") {
            if (ntok < 4) { logErr("Usage: can tx <id> <hexbytes>"); return ""; }
            String h = tok[3]; h.toUpperCase();
            String cmd = (t1 == "txe") ? "CAN:TXE:" : "CAN:TX:";
            return cmd + tok[2] + ":" + h;
        }
        if (t1 == "rx")     return "CAN:RX";
        if (t1 == "status") return "CAN:STATUS";
        if (t1 == "end")    return "CAN:END";
        if (t1 == "filter") {
            if (ntok < 3) { logErr("Usage: can filter <id> <mask>  OR  can filter off"); return ""; }
            if (tok[2].equalsIgnoreCase("off")) return "CAN:FILTER:OFF";
            if (ntok < 4) { logErr("Usage: can filter <id> <mask>"); return ""; }
            return "CAN:FILTER:" + tok[2] + ":" + tok[3];
        }
        logErr("can sub: begin|tx|txe|rx|filter|status|end|nowarn|warn");
        return "";
    }

    // ------------------------------------------------------------------ wifi
    if (t0 == "wifi") {
        if (ntok < 2) { logErr("Usage: wifi connect|status|disconnect|scan ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "connect") {
            if (ntok < 4) { logErr("Usage: wifi connect <ssid> <password>"); return ""; }
            // Spaces in SSID/password: rejoin from tok[2..] up to last
            String ssid = tok[2];
            String pass = tok[3];
            storedSsid = ssid;
            storedPass = pass;
            wifiConnect(ssid, pass);
            return "";   // handled locally
        }
        if (t1 == "status")     { wifiStatus();                   return ""; }
        if (t1 == "disconnect") { WiFi.disconnect(true); logOk("Disconnected."); return ""; }
        if (t1 == "scan")       { wifiScan();                     return ""; }
        logErr("wifi sub: connect|status|disconnect|scan");
        return "";
    }

    // ------------------------------------------------------------------ ntp
    if (t0 == "ntp") {
        if (ntok < 2) { logErr("Usage: ntp sync|status|server ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "sync")   { ntpSync();  return ""; }
        if (t1 == "status") { ntpStatus(); return ""; }
        if (t1 == "server") {
            if (ntok < 3) { logErr("Usage: ntp server <hostname>"); return ""; }
            ntpServer = tok[2];
            logOk("NTP server set to: " + ntpServer);
            return "";
        }
        logErr("ntp sub: sync|status|server");
        return "";
    }

    // ------------------------------------------------------------------ adc stream (shortcut)
    // Already handled by "adc stream ..." in the adc block above,
    // but keep an alias for convenience:
    // ------------------------------------------------------------------ rtc
    if (t0 == "rtc") {
        if (ntok < 2) { logErr("Usage: rtc init|status|get|getts|epoch|set|settss ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();

        if (t1 == "init")   return "RTC:INIT";
        if (t1 == "status") return "RTC:STATUS";
        if (t1 == "get")    return "RTC:GET";
        if (t1 == "getts")  return "RTC:GETTS";
        if (t1 == "epoch")  return "RTC:EPOCH";

        if (t1 == "set") {
            // Accept two formats:
            //   rtc set 2024 6 12 14 30 0           (7 space-separated tokens after "rtc set")
            //   rtc set 2024-06-12 14:30:00          (date and time strings)
            if (ntok >= 8) {
                // space-separated: rtc set YYYY MM DD HH MM SS
                return "RTC:SET:" + tok[2] + ":" + tok[3] + ":" + tok[4] +
                       ":" + tok[5] + ":" + tok[6] + ":" + tok[7];
            } else if (ntok >= 4) {
                // two-arg format: rtc set YYYY-MM-DD HH:MM:SS
                // tok[2] = "2024-06-12", tok[3] = "14:30:00"
                String date = tok[2]; // "2024-06-12"
                String time = tok[3]; // "14:30:00"
                // Replace - and : with :
                String d[3], t[3];
                int di = 0, ti = 0, s = 0;
                for (int i = 0; i <= (int)date.length(); i++) {
                    char c = (i < (int)date.length()) ? date.charAt(i) : '-';
                    if (c == '-' || i == (int)date.length()) {
                        if (di < 3) d[di++] = date.substring(s, i);
                        s = i + 1;
                    }
                }
                s = 0;
                for (int i = 0; i <= (int)time.length(); i++) {
                    char c = (i < (int)time.length()) ? time.charAt(i) : ':';
                    if (c == ':' || i == (int)time.length()) {
                        if (ti < 3) t[ti++] = time.substring(s, i);
                        s = i + 1;
                    }
                }
                if (di < 3 || ti < 3) { logErr("Usage: rtc set YYYY-MM-DD HH:MM:SS"); return ""; }
                return "RTC:SET:" + d[0] + ":" + d[1] + ":" + d[2] +
                       ":" + t[0] + ":" + t[1] + ":" + t[2];
            }
            logErr("Usage: rtc set YYYY MM DD HH MM SS  OR  rtc set YYYY-MM-DD HH:MM:SS");
            return "";
        }

        if (t1 == "settss") {
            if (ntok < 3) { logErr("Usage: rtc settss <seconds_since_2000>"); return ""; }
            return "RTC:SETTSS:" + tok[2];
        }

        logErr("rtc sub-command: init|status|get|getts|epoch|set|settss");
        return "";
    }

    // ------------------------------------------------------------------ show ports
    if (t0 == "show" && ntok >= 2 && String(tok[1]).equalsIgnoreCase("ports")) {
        // show ports [free] [--id master|slave|all]
        bool freeOnly  = false;
        bool doMaster  = true;
        bool doSlave   = true;

        for (int i = 2; i < ntok; i++) {
            String a = tok[i]; a.toLowerCase();
            if (a == "free")    { freeOnly = true; continue; }
            if (a == "--id" && i + 1 < ntok) {
                String id = tok[i + 1]; id.toLowerCase(); i++;
                if (id == "master") { doMaster = true;  doSlave = false; }
                else if (id == "slave")  { doMaster = false; doSlave = true; }
                else if (id == "all")    { doMaster = true;  doSlave = true; }
                else logWarn("Unknown --id value '" + id + "'. Use master|slave|all.");
            }
        }

        if (doMaster) printMasterPorts(freeOnly);

        if (doSlave) {
            // Send PORTS command to slave; result will be printed by a
            // special-cased handler in handleSlaveReply (flagged via portsPending).
            String sub = freeOnly ? "FREE" : "ALL";
            portsPendingFreeOnly = freeOnly;
            portsPending = true;
            return "PORTS:" + sub;
        }
        return "";
    }

    // ------------------------------------------------------------------ dac
    if (t0 == "dac") {
        if (ntok < 2) { logErr("Usage: dac set|mv|read|off ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();
        if (t1 == "set") {
            if (ntok < 4) { logErr("Usage: dac set 1|2 <0-4095>"); return ""; }
            return "DAC:SET:" + tok[2] + ":" + tok[3];
        }
        if (t1 == "mv") {
            if (ntok < 4) { logErr("Usage: dac mv 1|2 <0-3300>"); return ""; }
            return "DAC:MV:" + tok[2] + ":" + tok[3];
        }
        if (t1 == "read") {
            if (ntok < 3) { logErr("Usage: dac read 1|2"); return ""; }
            return "DAC:READ:" + tok[2];
        }
        if (t1 == "off") {
            if (ntok >= 3) return "DAC:OFF:" + tok[2];
            return "DAC:OFF";
        }
        logErr("dac sub: set|mv|read|off");
        return "";
    }

    // ------------------------------------------------------------------ buzzer
    if (t0 == "buzzer") {
        if (ntok < 2) { logErr("Usage: buzzer tone|beep|stop|status ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();
        if (t1 == "tone") {
            if (ntok < 5) { logErr("Usage: buzzer tone <pin> <hz> <ms>"); return ""; }
            return "BUZZER:TONE:" + normPin(tok[2]) + ":" + tok[3] + ":" + tok[4];
        }
        if (t1 == "beep") {
            if (ntok < 3) { logErr("Usage: buzzer beep <pin>"); return ""; }
            return "BUZZER:BEEP:" + normPin(tok[2]);
        }
        if (t1 == "stop") {
            if (ntok < 3) { logErr("Usage: buzzer stop <pin>"); return ""; }
            return "BUZZER:STOP:" + normPin(tok[2]);
        }
        if (t1 == "status") {
            if (ntok < 3) { logErr("Usage: buzzer status <pin>"); return ""; }
            return "BUZZER:STATUS:" + normPin(tok[2]);
        }
        logErr("buzzer sub: tone|beep|stop|status");
        return "";
    }

    // ------------------------------------------------------------------ debug (slave LEDs)
    if (t0 == "debug") {
        if (ntok < 2) { logErr("Usage: debug attach|detach|status ..."); return ""; }
        String t1 = tok[1]; t1.toLowerCase();
        if (t1 == "attach") {
            if (ntok < 4) { logErr("Usage: debug attach <rx_pin> <tx_pin>"); return ""; }
            return "DEBUG:ATTACH:" + normPin(tok[2]) + ":" + normPin(tok[3]);
        }
        if (t1 == "detach") return "DEBUG:DETACH";
        if (t1 == "status") return "DEBUG:STATUS";
        logErr("debug sub: attach|detach|status");
        return "";
    }

    // ------------------------------------------------------------------ masterdbg (ESP32 LEDs)
    if (t0 == "masterdbg") {
        if (ntok < 2) { logErr("Usage: masterdbg attach <tx_gpio> <rx_gpio>  |  masterdbg detach"); return ""; }
        String t1 = tok[1]; t1.toLowerCase();
        if (t1 == "attach") {
            if (ntok < 4) { logErr("Usage: masterdbg attach <tx_gpio> <rx_gpio>"); return ""; }
            int txPin = tok[2].toInt();
            int rxPin = tok[3].toInt();
            masterDebugAttach(txPin, rxPin);
            logOk("Master debug LEDs attached: TX=GPIO" + String(txPin) + " RX=GPIO" + String(rxPin));
            return "";
        }
        if (t1 == "detach") {
            masterDebugDetach();
            logOk("Master debug LEDs detached.");
            return "";
        }
        if (t1 == "status") {
            if (masterDbgActive)
                logOk("Master debug LEDs active: TX=GPIO" + String(masterDbgTxPin) + " RX=GPIO" + String(masterDbgRxPin));
            else
                logInfo("Master debug LEDs not attached.");
            return "";
        }
        logErr("masterdbg sub: attach|detach|status");
        return "";
    }

    // ------------------------------------------------------------------ legacy
    if (t0 == "led") {
        if (ntok < 2) { logErr("Usage: led on|off|status"); return ""; }
        String t1 = tok[1]; t1.toUpperCase();
        return "LED:" + t1;
    }
    if (t0 == "blink") {
        if (ntok < 2) { logErr("Usage: blink <ms>  (0 = stop)"); return ""; }
        return "BLINK:" + tok[1];
    }
    if (t0 == "status") return "SYS:STATUS";

    logErr("Unknown command '" + tok[0] + "'. Type 'help'.");
    return "";
}
