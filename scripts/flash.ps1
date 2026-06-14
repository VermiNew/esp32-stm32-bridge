#Requires -Version 7.0
<#
.SYNOPSIS
    Interactive wizard: detects COM port, probes STM32 bootloader, flashes stm32_slave firmware.

.PARAMETER Lang
    Language: "pl" or "en" (default: auto-detect from system locale).

.EXAMPLE
    .\flash.ps1
    .\flash.ps1 -Lang pl

.NOTES
    Requires: stm32flash.exe in tools\ (auto-downloaded by get-stm32flash.ps1),
              stm32_slave\stm32_slave.ino.bin (compile in Arduino IDE first).
    ESP32 must be running esp32_master firmware. Set BOOT0=1 on Blue Pill before flashing.
#>

param([string]$Lang = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force -ArgumentList $Lang

$Root     = Split-Path $PSScriptRoot
$ToolsDir = Join-Path $Root "tools"
$FlashExe = Join-Path $ToolsDir "stm32flash.exe"
$BinPath  = Join-Path $Root "stm32_slave\stm32_slave.ino.bin"
$Getter   = Join-Path $PSScriptRoot "get-stm32flash.ps1"

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

Write-Host ""
Write-Title "==========================================================="
Write-Title $L.FlashBannerTitle
Write-Title "==========================================================="
Write-Host ""
Write-Info $L.FlashBannerStep1
Write-Info $L.FlashBannerStep2
Write-Info $L.FlashBannerStep3
Write-Info $L.FlashBannerStep4
Write-Info $L.FlashBannerStep5
Write-Host ""

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

Assert-ArduinoCli
Assert-StmFlash $FlashExe $Getter
Assert-FirmwareBin $BinPath
Write-Host ""

# ---------------------------------------------------------------------------
# COM port
# ---------------------------------------------------------------------------

Add-Type -AssemblyName System.IO.Ports
$espPort = Select-EspPort
if ([string]::IsNullOrWhiteSpace($espPort)) { Write-Err $L.PortNone; exit 1 }
$espPort = $espPort.Trim().ToUpper()
Write-Host ""

# ---------------------------------------------------------------------------
# Optional loopback test
# ---------------------------------------------------------------------------

$doLoopback = Prompt-User $L.FlashLoopbackPrompt
if ($doLoopback -match '^[ytYT]') {
    Write-Info ($L.FlashLoopbackOpening -f $espPort)
    Write-Warn $L.FlashLoopbackDtrWarn
    try {
        $port = New-Object System.IO.Ports.SerialPort($espPort, 115200,
            [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One)
        $port.DtrEnable = $false; $port.RtsEnable = $false
        $port.ReadTimeout = 500; $port.WriteTimeout = 500
        $port.Open()
        Write-Info $L.FlashLoopbackWaiting
        Start-Sleep -Seconds 3

        $pass = $true
        foreach ($b in [byte[]](0xAA, 0x55, 0x12, 0x34)) {
            $port.Write([byte[]]$b, 0, 1)
            Start-Sleep -Milliseconds 50
            if ($port.BytesToRead -gt 0) {
                $echo = $port.ReadByte()
                if ($echo -ne $b) {
                    Write-Err ($L.FlashLoopbackMismatch -f $b, $echo)
                    $pass = $false; break
                }
            } else {
                Write-Err ($L.FlashLoopbackNoEcho -f $b)
                $pass = $false; break
            }
        }
        $port.Close()

        if ($pass) {
            Write-Ok $L.FlashLoopbackPassed
        } else {
            $cont = Prompt-User $L.FlashLoopbackFailPrompt
            if ($cont -notmatch '^[ytYT]') { exit 1 }
        }
    } catch {
        Write-Warn ($L.FlashLoopbackError -f $_)
    }
}
Write-Host ""

# ---------------------------------------------------------------------------
# Step 1: BOOT0 = 1
# ---------------------------------------------------------------------------

Write-Title $L.FlashBoot0Title
Write-Info  $L.FlashBoot0Line1
Write-Info  $L.FlashBoot0Line2
Write-Info  $L.FlashBoot0Line3
Write-Host ""
Write-Warn  $L.FlashBoot0Warn
Write-Host ""
$null = Prompt-User $L.FlashBoot0Prompt
Write-Host ""

# ---------------------------------------------------------------------------
# Step 2: Bootloader detection
# ---------------------------------------------------------------------------

Write-Title $L.FlashDetectTitle
Write-Info ($L.FlashDetectProbing -f $espPort)

$detected = $false
for ($attempt = 1; $attempt -le 5; $attempt++) {
    try {
        $port = New-Object System.IO.Ports.SerialPort($espPort, 115200,
            [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One)
        $port.DtrEnable = $false; $port.RtsEnable = $false
        $port.ReadTimeout = 1000; $port.WriteTimeout = 500
        $port.Open()

        Write-Info ($L.FlashDetectAttempt -f $attempt)
        $port.Write([byte[]](0x7F), 0, 1)
        Start-Sleep -Milliseconds 300

        if ($port.BytesToRead -gt 0) {
            $ack = $port.ReadByte()
            if ($ack -eq 0x79) {
                Write-Ok $L.FlashDetectAck; $detected = $true
            } elseif ($ack -eq 0x00) {
                Write-Warn $L.FlashDetectLow
            } else {
                Write-Warn ($L.FlashDetectUnexpected -f $ack)
            }
        } else {
            Write-Warn $L.FlashDetectNoResp
        }
        $port.Close()
        if ($detected) { break }
        if ($attempt -lt 5) { Write-Info $L.FlashDetectRetry; Start-Sleep -Seconds 2 }
    } catch {
        Write-Err ($L.FlashDetectSerialErr -f $_); break
    }
}

if (-not $detected) {
    Write-Err  $L.FlashDetectFailed
    Write-Info $L.FlashDetectTrouble1
    Write-Info $L.FlashDetectTrouble2
    Write-Info $L.FlashDetectTrouble3
    Write-Info $L.FlashDetectTrouble4
    Write-Info $L.FlashDetectTrouble5
    $cont = Prompt-User $L.FlashDetectForcePrompt
    if ($cont -notmatch '^[ytYT]') { exit 1 }
}
Write-Host ""

# ---------------------------------------------------------------------------
# Step 3: Flash
# ---------------------------------------------------------------------------

Write-Title $L.FlashWriteTitle
Write-Info ($L.FlashWriteBin  -f $BinPath)
Write-Info ($L.FlashWritePort -f $espPort)
Write-Host ""

$flashArgs = @("-b", "115200", "-w", "`"$BinPath`"", "-v", $espPort)
Write-Dim "$ $FlashExe $($flashArgs -join ' ')"
Write-Host ""

try {
    $proc = Start-Process -FilePath $FlashExe -ArgumentList $flashArgs -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -eq 0) {
        Write-Ok $L.FlashWriteOk
    } else {
        Write-Err  ($L.FlashWriteFail -f $proc.ExitCode)
        Write-Warn $L.FlashWriteHint
        exit 1
    }
} catch {
    Write-Err ($L.FlashWriteErr -f $_); exit 1
}
Write-Host ""

# ---------------------------------------------------------------------------
# Step 4: BOOT0 = 0
# ---------------------------------------------------------------------------

Write-Title $L.FlashBoot0BackTitle
Write-Info  $L.FlashBoot0BackLine1
Write-Info  $L.FlashBoot0BackLine2
Write-Host ""
$null = Prompt-User $L.FlashBoot0BackPrompt
Write-Host ""

Write-Ok   $L.FlashDone1
Write-Info $L.FlashDone2
Write-Info $L.FlashDone3
Write-Info $L.FlashDone4
Write-Host ""
Write-Title "==========================================================="
Write-Title $L.FlashDoneTitle
Write-Title "==========================================================="
Write-Host ""


