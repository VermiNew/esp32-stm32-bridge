#Requires -Version 7.0
<#
.SYNOPSIS
    Smoke-test harness for Supermikrokontroler v2.
    Connects to the ESP32 master over a COM port, sends a sequence of
    test commands and verifies responses. Reports PASS/FAIL per test.

.PARAMETER Port
    COM port the ESP32 master is connected to (e.g. COM3). Auto-detected
    if omitted.

.PARAMETER Baud
    Baud rate (default 115200).

.PARAMETER Timeout
    Per-command timeout in seconds (default 6).

.EXAMPLE
    .\test_harness.ps1 -Port COM3
    .\test_harness.ps1            # auto-detect port
#>

param(
    [string]$Port    = "",
    [int]   $Baud    = 115200,
    [int]   $Timeout = 6
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Ports

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------
function rgb($r,$g,$b) { return [char]27 + "[38;2;${r};${g};${b}m" }
$G = rgb  80 220 100; $R = rgb 255  80  80
$A = rgb 255 200  60; $B = rgb 130 200 255
$D = rgb 120 120 120; $RST = [char]27 + "[0m"

function ok  ($m) { Write-Host "${G}[PASS]${RST} $m" }
function fail ($m) { Write-Host "${R}[FAIL]${RST} $m" }
function warn ($m) { Write-Host "${A}[WARN]${RST} $m" }
function info ($m) { Write-Host "${B}      $m${RST}" }
function dim  ($m) { Write-Host "${D}$m${RST}" }

# ---------------------------------------------------------------------------
# Port detection
# ---------------------------------------------------------------------------
if ($Port -eq "") {
    $devices = Get-CimInstance -ClassName Win32_PnPEntity -EA SilentlyContinue |
        Where-Object { $_.Name -match 'CH340|CP210|USB-SERIAL|Silicon Labs|FTDI' -and
                       $_.Name -match 'COM\d' }
    $ports = @($devices | ForEach-Object { if ($_.Name -match '(COM\d+)') { $Matches[1] } } | Select-Object -Unique)

    if ($ports.Count -eq 1) {
        $Port = $ports[0]
        info "Auto-detected: $Port"
    } elseif ($ports.Count -gt 1) {
        warn "Multiple ports found: $($ports -join ', ')"
        $Port = Read-Host "Enter COM port"
    } else {
        Write-Host "${R}ERROR:${RST} No ESP32 COM port found. Plug in the ESP32 and retry."
        exit 1
    }
}

# ---------------------------------------------------------------------------
# Open serial port
# ---------------------------------------------------------------------------
info "Opening $Port at $Baud baud..."
$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false
$sp.ReadTimeout  = 500
$sp.WriteTimeout = 1000
$sp.NewLine      = "`n"
$sp.Open()
Start-Sleep -Seconds 3   # wait for ESP32 to boot

$sp.DiscardInBuffer()
$sp.DiscardOutBuffer()

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
$passCount = 0
$failCount = 0

# Read lines from port until we see a line matching $pattern (regex)
# within $Timeout seconds. Returns the matching line or "".
function WaitForLine([string]$pattern, [int]$secs = $Timeout) {
    $deadline = [DateTime]::Now.AddSeconds($secs)
    $accum    = ""
    while ([DateTime]::Now -lt $deadline) {
        try {
            $ch = [char]$sp.ReadChar()
            if ($ch -eq "`n") {
                $line = $accum.Trim()
                $accum = ""
                dim "  << $line"
                if ($line -match $pattern) { return $line }
            } else {
                $accum += $ch
            }
        } catch [System.TimeoutException] {
            # continue
        }
    }
    return ""
}

# Send a command and wait for a matching response line.
function RunTest([string]$description, [string]$command, [string]$expectPattern) {
    dim "  >> $command"
    $sp.WriteLine($command)
    $result = WaitForLine $expectPattern
    if ($result -ne "") {
        ok  "[$description]  got: $result"
        $script:passCount++
    } else {
        fail "[$description]  expected pattern: $expectPattern  (timeout)"
        $script:failCount++
    }
}

# ---------------------------------------------------------------------------
# Test suite
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "${B}==================================================${RST}"
Write-Host "${B}  Supermikrokontroler v2 — Smoke Test Harness${RST}"
Write-Host "${B}==================================================${RST}"
Write-Host ""
info "Port: $Port  Baud: $Baud  Timeout: ${Timeout}s"
Write-Host ""

# 1. Link
RunTest "PING/PONG"          "ping"          '\[OK\].*PONG'

# 2. System info
RunTest "SYS:STATUS"         "sys status"    '\[OK\].*v2\.0'
RunTest "SYS:UPTIME"         "sys uptime"    '\[OK\]\s+\d+'
RunTest "SYS:CHIPID"         "sys chipid"    '\[OK\]\s+[0-9A-F]{24}'
RunTest "SYS:CPUFREQ"        "sys cpufreq"   '\[OK\].*72MHz'
RunTest "SYS:FWVER"          "sys fwver"     '\[OK\]\s+2\.0'
RunTest "SYS:FREERAM"        "sys freeram"   '\[OK\]\s+\d+'
RunTest "SYS:ECHO"           "sys echo hello" '\[OK\]\s+hello'

# 3. GPIO
RunTest "GPIO:MODE:OUT"      "gpio mode C13 out"   '\[OK\].*MODE.*C13.*OUT'
RunTest "GPIO:WRITE:1"       "gpio write C13 0"    '\[OK\]\s+0'
RunTest "GPIO:WRITE:0"       "gpio write C13 1"    '\[OK\]\s+1'
RunTest "GPIO:TOGGLE"        "gpio toggle C13"     '\[OK\]\s+[01]'
RunTest "GPIO:MODE:IN"       "gpio mode A0 in"     '\[OK\].*MODE.*A0.*IN'
RunTest "GPIO:READ"          "gpio read A0"        '\[OK\]\s+[01]'
RunTest "GPIO:PORT"          "gpio port A"         '\[OK\]\s+0x[0-9A-Fa-f]{2}'

# 4. ADC
RunTest "ADC:READ"           "adc read A0"         '\[OK\]\s+\d{1,4}'
RunTest "ADC:AVG"            "adc avg A0 8"        '\[OK\]\s+\d{1,4}'
RunTest "ADC:MV"             "adc mv A0"           '\[OK\]\s+\d{1,4}'
RunTest "ADC:MULTI"          "adc multi A0,A1"     '\[OK\]\s+\d+,\d+'
RunTest "ADC:TEMP"           "adc temp"            '\[OK\]\s+-?\d+'
RunTest "ADC:VREF"           "adc vref"            '\[OK\]\s+\d{3,4}'
RunTest "ADC:STREAM 4 samp"  "adc stream A0 4 10"  '\[OK\]\s+[0-9A-F]{16}'

# 5. PWM
RunTest "PWM:SET"            "pwm set A8 500"       '\[OK\].*PWM.*A8.*500'
RunTest "PWM:READ"           "pwm read A8"          '\[OK\]\s+500'
RunTest "PWM:STOP"           "pwm stop A8"          '\[OK\].*STOP'

# 6. EEPROM
RunTest "EE:SIZE"            "ee size"              '\[OK\]\s+512'
RunTest "EE:WRITE"           "ee write 0 0xAB"      '\[OK\]\s+OK'
RunTest "EE:READ"            "ee read 0"            '\[OK\]\s+171'   # 0xAB=171
RunTest "EE:WRWORD"          "ee wrword 4 12345678" '\[OK\]\s+OK'
RunTest "EE:RDWORD"          "ee rdword 4"          '\[OK\]\s+12345678'
RunTest "EE:WRHEX"           "ee wrhex 10 DEADBEEF" '\[OK\].*OK'
RunTest "EE:RDHEX"           "ee rdhex 10 4"        '\[OK\]\s+DEADBEEF'

# 7. I2C scan (just checks it completes — result depends on hardware)
RunTest "I2C:SCAN"           "i2c scan"             '\[OK\]'

# 8. IRQ
RunTest "IRQ:LIST empty"     "irq list"             '\[OK\]\s+NONE'
RunTest "IRQ:ATTACH"         "irq attach A0 change" '\[OK\].*ATTACH'
RunTest "IRQ:LIST"           "irq list"             '\[OK\].*A0'
RunTest "IRQ:POLL"           "irq poll"             '\[OK\]'   # NONE or counts
RunTest "IRQ:DETACH"         "irq detach A0"        '\[OK\].*DETACH'

# 9. CALC
RunTest "CALC:MAP"           "calc map 512 0 1023 0 100"  '\[OK\]\s+50'
RunTest "CALC:SQRT"          "calc sqrt 144"              '\[OK\]\s+12'
RunTest "CALC:CONSTRAIN hi"  "calc constrain 200 0 100"   '\[OK\]\s+100'
RunTest "CALC:ABS"           "calc abs -42"               '\[OK\]\s+42'
RunTest "CALC:CRC16"         "calc crc16 48656C6C6F"      '\[OK\]\s+[0-9A-F]{4}'

# 10. CAN loopback (no transceiver needed)
info "CAN loopback test (no transceiver required):"
$sp.WriteLine("can begin 250 loopback")
$canInit = WaitForLine '\[OK\]|\[ERR\]' 4
if ($canInit -match '\[OK\]') {
    ok  "[CAN:BEGIN:LOOPBACK] ready"
    $passCount++
    RunTest "CAN:TX loopback"  "can tx 123 DEADBEEF"   '\[OK\].*TX'
    RunTest "CAN:RX loopback"  "can rx"                 '\[OK\].*123.*DEAD'
    RunTest "CAN:STATUS"       "can status"             '\[OK\].*STATE'
    RunTest "CAN:END"          "can end"                '\[OK\].*OFF'
} else {
    warn "[CAN] Init failed — skipping CAN tests"
}

# 11. RTC (if crystal is present — fail is acceptable if not configured)
info "RTC tests (require LSE crystal — soft fail if hardware absent):"
$sp.WriteLine("rtc init")
$rtcInit = WaitForLine '\[OK\]|\[ERR\]' 5
if ($rtcInit -match '\[OK\]') {
    ok  "[RTC:INIT] crystal detected"
    $passCount++

    RunTest "RTC:SET"  "rtc set 2025-01-15 12:00:00" '\[OK\].*SET'
    RunTest "RTC:GET"  "rtc get"                      '\[OK\].*2025.*12:00'
    RunTest "RTC:GETTS" "rtc getts"                   '\[OK\]\s+\d{8,10}'
    RunTest "RTC:EPOCH" "rtc epoch"                   '\[OK\]\s+\d{10}'
} else {
    warn "[RTC] Init failed or no crystal — skipping RTC tests"
}

# 12. Protocol stress: rapid ping x5
info "Protocol stress: 5 rapid PINGs..."
for ($i = 1; $i -le 5; $i++) {
    $sp.WriteLine("ping")
    $r = WaitForLine '\[OK\].*PONG' 2
    if ($r) { $passCount++ } else { $failCount++; fail "[PING #$i] no response" }
}

# 13. RESET and re-ping
RunTest "PROTOCOL:RESET"     "reset"       'RESET:ACK|\[OK\]'
Start-Sleep -Milliseconds 500
RunTest "POST-RESET PING"    "ping"        '\[OK\].*PONG'

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
$sp.Close()
Write-Host ""
Write-Host "${B}==================================================${RST}"
$total = $passCount + $failCount
$color = if ($failCount -eq 0) { $G } else { $R }
Write-Host "${color}  Results: $passCount / $total passed  ($failCount failed)${RST}"
Write-Host "${B}==================================================${RST}"
Write-Host ""

if ($failCount -gt 0) { exit 1 } else { exit 0 }
