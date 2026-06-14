#Requires -Version 7.0
<#
.SYNOPSIS
    Smoke-test harness for Supermikrokontroler v2.

.PARAMETER Port
    COM port (e.g. COM3). Auto-detected if omitted.

.PARAMETER Baud
    Baud rate (default 115200).

.PARAMETER Timeout
    Per-command timeout in seconds (default 6).

.PARAMETER Lang
    Language: "pl" or "en" (default: auto-detect).

.EXAMPLE
    .\test.ps1 -Port COM3
    .\test.ps1 -Lang pl

.NOTES
    Connects to the ESP32 master over UART, sends the full command suite
    and reports PASS/FAIL per test. Exit code 0 = all passed, 1 = failures.
    RTC tests are soft-fail (skipped if no LSE crystal present).
#>

param(
    [string]$Port    = "",
    [int]   $Baud    = 115200,
    [int]   $Timeout = 6,
    [string]$Lang    = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force -ArgumentList $Lang
Add-Type -AssemblyName System.IO.Ports

if ($Port -eq "") { $Port = Select-EspPort }
if ([string]::IsNullOrWhiteSpace($Port)) { Write-Err $L.TestNoPort; exit 1 }
$Port = $Port.Trim().ToUpper()

Write-Info ($L.TestOpening -f $Port, $Baud)
$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud,
    [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.ReadTimeout = 500; $sp.WriteTimeout = 1000
$sp.NewLine = "`n"
$sp.Open()
Start-Sleep -Seconds 3
$sp.DiscardInBuffer(); $sp.DiscardOutBuffer()

$passCount = 0
$failCount = 0

function WaitForLine([string]$pattern, [int]$secs = $Timeout) {
    $deadline = [DateTime]::Now.AddSeconds($secs)
    $accum    = ""
    while ([DateTime]::Now -lt $deadline) {
        try {
            $ch = [char]$sp.ReadChar()
            if ($ch -eq "`n") {
                $line = $accum.Trim(); $accum = ""
                Write-Dim "  << $line"
                if ($line -match $pattern) { return $line }
            } else { $accum += $ch }
        } catch [System.TimeoutException] {}
    }
    return ""
}

function RunTest([string]$desc, [string]$cmd, [string]$pattern) {
    Write-Dim "  >> $cmd"
    $sp.WriteLine($cmd)
    $result = WaitForLine $pattern
    if ($result -ne "") {
        Write-Ok  "[$desc]  $result"
        $script:passCount++
    } else {
        Write-Err "[$desc]  $($L.TestTimeout -f $pattern)"
        $script:failCount++
    }
}

Write-Host ""
Write-Title "=================================================="
Write-Title $L.TestBannerTitle
Write-Title "=================================================="
Write-Host ""
Write-Info ($L.TestPortInfo -f $Port, $Baud, $Timeout)
Write-Host ""

RunTest "PING"            "ping"                   '\[OK\].*PONG'
RunTest "SYS:STATUS"      "sys status"             '\[OK\].*v2\.0'
RunTest "SYS:UPTIME"      "sys uptime"             '\[OK\]\s+\d+'
RunTest "SYS:CHIPID"      "sys chipid"             '\[OK\]\s+[0-9A-F]{24}'
RunTest "SYS:CPUFREQ"     "sys cpufreq"            '\[OK\].*72MHz'
RunTest "SYS:FWVER"       "sys fwver"              '\[OK\]\s+2\.0'
RunTest "SYS:FREERAM"     "sys freeram"            '\[OK\]\s+\d+'
RunTest "SYS:ECHO"        "sys echo hello"         '\[OK\]\s+hello'

RunTest "GPIO:MODE:OUT"   "gpio mode C13 out"      '\[OK\].*MODE.*C13.*OUT'
RunTest "GPIO:WRITE:0"    "gpio write C13 0"       '\[OK\]\s+0'
RunTest "GPIO:WRITE:1"    "gpio write C13 1"       '\[OK\]\s+1'
RunTest "GPIO:TOGGLE"     "gpio toggle C13"        '\[OK\]\s+[01]'
RunTest "GPIO:MODE:IN"    "gpio mode A0 in"        '\[OK\].*MODE.*A0.*IN'
RunTest "GPIO:READ"       "gpio read A0"           '\[OK\]\s+[01]'
RunTest "GPIO:PORT"       "gpio port A"            '\[OK\]\s+0x[0-9A-Fa-f]{2}'

RunTest "ADC:READ"        "adc read A0"            '\[OK\]\s+\d{1,4}'
RunTest "ADC:AVG"         "adc avg A0 8"           '\[OK\]\s+\d{1,4}'
RunTest "ADC:MV"          "adc mv A0"              '\[OK\]\s+\d{1,4}'
RunTest "ADC:MULTI"       "adc multi A0,A1"        '\[OK\]\s+\d+,\d+'
RunTest "ADC:TEMP"        "adc temp"               '\[OK\]\s+-?\d+'
RunTest "ADC:VREF"        "adc vref"               '\[OK\]\s+\d{3,4}'
RunTest "ADC:STREAM"      "adc stream A0 4 10"     '\[OK\]\s+[0-9A-F]{16}'

RunTest "PWM:SET"         "pwm set A8 500"         '\[OK\].*PWM.*A8.*500'
RunTest "PWM:READ"        "pwm read A8"            '\[OK\]\s+500'
RunTest "PWM:STOP"        "pwm stop A8"            '\[OK\].*STOP'

RunTest "EE:SIZE"         "ee size"                '\[OK\]\s+512'
RunTest "EE:WRITE"        "ee write 0 0xAB"        '\[OK\]\s+OK'
RunTest "EE:READ"         "ee read 0"              '\[OK\]\s+171'
RunTest "EE:WRWORD"       "ee wrword 4 12345678"   '\[OK\]\s+OK'
RunTest "EE:RDWORD"       "ee rdword 4"            '\[OK\]\s+12345678'
RunTest "EE:WRHEX"        "ee wrhex 10 DEADBEEF"   '\[OK\].*OK'
RunTest "EE:RDHEX"        "ee rdhex 10 4"          '\[OK\]\s+DEADBEEF'

RunTest "I2C:SCAN"        "i2c scan"               '\[OK\]'

RunTest "IRQ:LIST empty"  "irq list"               '\[OK\]\s+NONE'
RunTest "IRQ:ATTACH"      "irq attach A0 change"   '\[OK\].*ATTACH'
RunTest "IRQ:LIST"        "irq list"               '\[OK\].*A0'
RunTest "IRQ:POLL"        "irq poll"               '\[OK\]'
RunTest "IRQ:DETACH"      "irq detach A0"          '\[OK\].*DETACH'

RunTest "CALC:MAP"        "calc map 512 0 1023 0 100" '\[OK\]\s+50'
RunTest "CALC:SQRT"       "calc sqrt 144"             '\[OK\]\s+12'
RunTest "CALC:CONSTRAIN"  "calc constrain 200 0 100"  '\[OK\]\s+100'
RunTest "CALC:ABS"        "calc abs -42"              '\[OK\]\s+42'
RunTest "CALC:CRC16"      "calc crc16 48656C6C6F"     '\[OK\]\s+[0-9A-F]{4}'

Write-Info $L.TestCanLoopback
$sp.WriteLine("can begin 250 loopback")
$r = WaitForLine '\[OK\]|\[ERR\]' 4
if ($r -match '\[OK\]') {
    $passCount++; Write-Ok $L.TestCanReady
    RunTest "CAN:TX"     "can tx 123 DEADBEEF" '\[OK\].*TX'
    RunTest "CAN:RX"     "can rx"              '\[OK\].*123.*DEAD'
    RunTest "CAN:STATUS" "can status"          '\[OK\].*STATE'
    RunTest "CAN:END"    "can end"             '\[OK\].*OFF'
} else { Write-Warn $L.TestCanSkip }

Write-Info $L.TestRtcInfo
$sp.WriteLine("rtc init")
$r = WaitForLine '\[OK\]|\[ERR\]' 5
if ($r -match '\[OK\]') {
    $passCount++; Write-Ok $L.TestRtcCrystal
    RunTest "RTC:SET"   "rtc set 2025-01-15 12:00:00" '\[OK\].*SET'
    RunTest "RTC:GET"   "rtc get"                      '\[OK\].*2025.*12:00'
    RunTest "RTC:GETTS" "rtc getts"                    '\[OK\]\s+\d{8,10}'
    RunTest "RTC:EPOCH" "rtc epoch"                    '\[OK\]\s+\d{10}'
} else { Write-Warn $L.TestRtcSkip }

Write-Info $L.TestStress
for ($i = 1; $i -le 5; $i++) {
    $sp.WriteLine("ping")
    if (WaitForLine '\[OK\].*PONG' 2) { $passCount++ }
    else { $failCount++; Write-Err ($L.TestPingFail -f $i) }
}

RunTest "PROTOCOL:RESET"  "reset" 'RESET:ACK|\[OK\]'
Start-Sleep -Milliseconds 500
RunTest "POST-RESET PING" "ping"  '\[OK\].*PONG'

$sp.Close()
Write-Host ""
Write-Title "=================================================="
$total = $passCount + $failCount
if ($failCount -eq 0) {
    Write-Ok  ($L.TestResultsPass -f $passCount, $total)
} else {
    Write-Err ($L.TestResultsFail -f $passCount, $total, $failCount)
}
Write-Title "=================================================="
Write-Host ""

if ($failCount -gt 0) { exit 1 } else { exit 0 }


