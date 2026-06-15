#!/usr/bin/env python3
"""
test.py — Smoke-test harness for Supermikrokontroler v2

Connects to the ESP32 master over a serial port, sends a sequence of
test commands and verifies responses against regex patterns.

Usage:
    python test.py                        # auto-detect port
    python test.py COM3                   # Windows
    python test.py /dev/ttyUSB0           # Linux
    python test.py /dev/cu.usbserial-0001 # macOS

Requirements:
    pip install pyserial
"""

import sys
import re
import time
import serial
import serial.tools.list_ports

# ---------------------------------------------------------------------------
# ANSI colors
# ---------------------------------------------------------------------------
G   = "\033[38;2;80;220;100m"
R   = "\033[38;2;255;80;80m"
A   = "\033[38;2;255;200;60m"
B   = "\033[38;2;130;200;255m"
DIM = "\033[38;2;120;120;120m"
RST = "\033[0m"

def ok  (m): print(f"{G}[PASS]{RST} {m}")
def fail(m): print(f"{R}[FAIL]{RST} {m}")
def warn(m): print(f"{A}[WARN]{RST} {m}")
def info(m): print(f"{B}      {m}{RST}")
def dim (m): print(f"{DIM}{m}{RST}")

# ---------------------------------------------------------------------------
# Port detection
# ---------------------------------------------------------------------------
def detect_port():
    ports = list(serial.tools.list_ports.comports())
    candidates = [
        p for p in ports
        if any(kw in (p.description or "").upper()
               for kw in ["CH340", "CP210", "USB-SERIAL", "SILICON", "FTDI", "UART"])
    ]
    if len(candidates) == 1:
        info(f"Auto-detected: {candidates[0].device}")
        return candidates[0].device
    if len(candidates) > 1:
        warn("Multiple candidates:")
        for p in candidates:
            info(f"  {p.device}  {p.description}")
        return input("      Enter port: ").strip()
    # Fallback: list all and ask
    for p in ports:
        info(f"  {p.device}  {p.description}")
    return input("      Enter port: ").strip()

# ---------------------------------------------------------------------------
# Serial helper
# ---------------------------------------------------------------------------
class Master:
    def __init__(self, port: str, baud: int = 115200, default_timeout: float = 6.0):
        self.sp = serial.Serial(
            port=port, baudrate=baud,
            bytesize=8, parity='N', stopbits=1,
            xonxoff=False, rtscts=False, dsrdtr=False,
            timeout=0.2
        )
        self.sp.dtr = False
        self.sp.rts = False
        self.default_timeout = default_timeout
        time.sleep(3)  # wait for ESP32 boot
        self.sp.reset_input_buffer()
        self.sp.reset_output_buffer()

    def close(self):
        self.sp.close()

    def send(self, cmd: str):
        dim(f"  >> {cmd}")
        self.sp.write((cmd + "\n").encode())

    def wait_for(self, pattern: str, timeout: float | None = None) -> str:
        """Read lines until one matches 'pattern' or timeout expires."""
        deadline = time.time() + (timeout if timeout is not None else self.default_timeout)
        buf = ""
        rx = re.compile(pattern)
        while time.time() < deadline:
            try:
                chunk = self.sp.read(64).decode(errors="replace")
                buf += chunk
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if line:
                        dim(f"  << {line}")
                        if rx.search(line):
                            return line
            except Exception:
                pass
        return ""

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------
pass_count = 0
fail_count = 0

def run_test(m: Master, description: str, command: str,
             expect: str, timeout: float | None = None) -> bool:
    global pass_count, fail_count
    m.send(command)
    result = m.wait_for(expect, timeout)
    if result:
        ok(f"[{description}]  {result[:80]}")
        pass_count += 1
        return True
    else:
        fail(f"[{description}]  expected: {expect}  (timeout)")
        fail_count += 1
        return False

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    global pass_count, fail_count
    port = sys.argv[1] if len(sys.argv) > 1 else detect_port()

    print()
    print(f"{B}=================================================={RST}")
    print(f"{B}  Supermikrokontroler v2 — Python Smoke Test{RST}")
    print(f"{B}=================================================={RST}")
    print()
    info(f"Port: {port}  Baud: 115200")
    print()

    m = Master(port)

    # 1. Link
    run_test(m, "PING/PONG",   "ping",       r"\[OK\].*PONG")

    # 2. System
    run_test(m, "SYS:STATUS",  "sys status", r"\[OK\].*v2\.0")
    run_test(m, "SYS:UPTIME",  "sys uptime", r"\[OK\]\s+\d+")
    run_test(m, "SYS:CHIPID",  "sys chipid", r"\[OK\]\s+[0-9A-F]{24}")
    run_test(m, "SYS:CPUFREQ", "sys cpufreq",r"\[OK\].*72MHz")
    run_test(m, "SYS:FREERAM", "sys freeram",r"\[OK\]\s+\d+")
    run_test(m, "SYS:ECHO",    "sys echo hi",r"\[OK\]\s+hi")

    # 3. GPIO
    run_test(m, "GPIO:MODE:OUT",   "gpio mode C13 out",   r"\[OK\].*MODE.*C13.*OUT")
    run_test(m, "GPIO:WRITE:0",    "gpio write C13 0",    r"\[OK\]\s+0")
    run_test(m, "GPIO:WRITE:1",    "gpio write C13 1",    r"\[OK\]\s+1")
    run_test(m, "GPIO:TOGGLE",     "gpio toggle C13",     r"\[OK\]\s+[01]")
    run_test(m, "GPIO:MODE:IN",    "gpio mode A0 in",     r"\[OK\].*MODE.*A0.*IN")
    run_test(m, "GPIO:READ",       "gpio read A0",        r"\[OK\]\s+[01]")
    run_test(m, "GPIO:PORT",       "gpio port A",         r"\[OK\]\s+0x[0-9A-Fa-f]{2}")

    # 4. ADC
    run_test(m, "ADC:READ",        "adc read A0",         r"\[OK\]\s+\d{1,4}")
    run_test(m, "ADC:AVG",         "adc avg A0 8",        r"\[OK\]\s+\d{1,4}")
    run_test(m, "ADC:MV",          "adc mv A0",           r"\[OK\]\s+\d{1,4}")
    run_test(m, "ADC:MULTI",       "adc multi A0,A1",     r"\[OK\]\s+\d+,\d+")
    run_test(m, "ADC:TEMP",        "adc temp",            r"\[OK\]\s+-?\d+")
    run_test(m, "ADC:VREF",        "adc vref",            r"\[OK\]\s+\d{3,4}")
    run_test(m, "ADC:STREAM",      "adc stream A0 4 10",  r"\[OK\]\s+[0-9A-F]{16}", 10)

    # 5. PWM
    run_test(m, "PWM:SET",         "pwm set A8 500",      r"\[OK\].*PWM.*A8.*500")
    run_test(m, "PWM:READ",        "pwm read A8",         r"\[OK\]\s+500")
    run_test(m, "PWM:STOP",        "pwm stop A8",         r"\[OK\].*STOP")

    # 6. EEPROM
    run_test(m, "EE:SIZE",         "ee size",             r"\[OK\]\s+512")
    run_test(m, "EE:WRITE",        "ee write 0 171",      r"\[OK\]\s+OK")
    run_test(m, "EE:READ",         "ee read 0",           r"\[OK\]\s+171")
    run_test(m, "EE:WRWORD",       "ee wrword 4 12345678",r"\[OK\]\s+OK")
    run_test(m, "EE:RDWORD",       "ee rdword 4",         r"\[OK\]\s+12345678")
    run_test(m, "EE:WRHEX",        "ee wrhex 10 DEADBEEF",r"\[OK\].*OK")
    run_test(m, "EE:RDHEX",        "ee rdhex 10 4",       r"\[OK\]\s+DEADBEEF")

    # 7. I2C scan
    run_test(m, "I2C:SCAN",        "i2c scan",            r"\[OK\]", 30)

    # 8. IRQ
    run_test(m, "IRQ:LIST empty",  "irq list",            r"\[OK\]\s+NONE")
    run_test(m, "IRQ:ATTACH",      "irq attach A0 change",r"\[OK\].*ATTACH")
    run_test(m, "IRQ:LIST",        "irq list",            r"\[OK\].*A0")
    run_test(m, "IRQ:POLL",        "irq poll",            r"\[OK\]")
    run_test(m, "IRQ:DETACH",      "irq detach A0",       r"\[OK\].*DETACH")

    # 9. CALC
    run_test(m, "CALC:MAP",        "calc map 512 0 1023 0 100", r"\[OK\]\s+50")
    run_test(m, "CALC:SQRT",       "calc sqrt 144",             r"\[OK\]\s+12")
    run_test(m, "CALC:CONSTRAIN",  "calc constrain 200 0 100",  r"\[OK\]\s+100")
    run_test(m, "CALC:ABS",        "calc abs -42",              r"\[OK\]\s+42")
    run_test(m, "CALC:CRC16",      "calc crc16 48656C6C6F",     r"\[OK\]\s+[0-9A-F]{4}")

    # 10. CAN loopback (no transceiver needed)
    info("CAN loopback test (no transceiver required):")
    m.send("can begin 250 loopback")
    can_init = m.wait_for(r"\[OK\]|\[ERR\]", 4)
    if "[OK]" in can_init:
        ok("[CAN:BEGIN:LOOPBACK] ready")
        pass_count += 1
        run_test(m, "CAN:TX loopback", "can tx 123 DEADBEEF",  r"\[OK\].*TX")
        run_test(m, "CAN:RX loopback", "can rx",               r"\[OK\].*123.*DEAD")
        run_test(m, "CAN:STATUS",      "can status",           r"\[OK\].*STATE")
        run_test(m, "CAN:END",         "can end",              r"\[OK\].*OFF")
    else:
        warn("[CAN] Init failed — skipping CAN tests")

    # 11. RTC (soft-fail if no crystal)
    info("RTC tests (require LSE crystal — soft fail if absent):")
    m.send("rtc init")
    rtc_init = m.wait_for(r"\[OK\]|\[ERR\]", 6)
    if "[OK]" in rtc_init:
        ok("[RTC:INIT] crystal OK")
        pass_count += 1
        run_test(m, "RTC:SET",   "rtc set 2025-01-15 12:00:00", r"\[OK\].*SET")
        run_test(m, "RTC:GET",   "rtc get",                      r"\[OK\].*2025.*12:0[01]")
        run_test(m, "RTC:GETTS", "rtc getts",                    r"\[OK\]\s+\d{8,10}")
        run_test(m, "RTC:EPOCH", "rtc epoch",                    r"\[OK\]\s+\d{10}")
    else:
        warn("[RTC] Init failed or no crystal — skipping RTC suite")

    # 11. Stress: 5 rapid pings
    info("Protocol stress: 5 rapid PINGs...")
    for i in range(1, 6):
        m.send("ping")
        r = m.wait_for(r"\[OK\].*PONG", 2)
        if r:
            pass_count += 1
        else:
            fail_count += 1
            fail(f"[PING #{i}] no response")

    # 12. Reset + re-ping
    run_test(m, "PROTOCOL:RESET", "reset",  r"RESET:ACK|\[OK\]", 3)
    time.sleep(0.5)
    run_test(m, "POST-RESET PING","ping",   r"\[OK\].*PONG")

    m.close()

    # ---------------------------------------------------------------------------
    # Summary
    # ---------------------------------------------------------------------------
    total = pass_count + fail_count
    color = G if fail_count == 0 else R
    print()
    print(f"{B}=================================================={RST}")
    print(f"{color}  Results: {pass_count} / {total} passed  ({fail_count} failed){RST}")
    print(f"{B}=================================================={RST}")
    print()
    sys.exit(0 if fail_count == 0 else 1)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{A}[ABORTED]{RST} Test interrupted by user.")
        sys.exit(1)
