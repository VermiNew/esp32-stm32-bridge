#Requires -Version 7.0
<#
.SYNOPSIS
    Interactive flashing wizard — flashes stm32_slave firmware onto the STM32
    Blue Pill using the ESP32 as a USB-to-UART bridge (esp32_flasher firmware).

.NOTES
    - Requires arduino-cli on PATH.
    - stm32flash.exe is auto-downloaded to tools\ if missing.
    - PowerShell 7+ required.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force

$Root      = Split-Path $PSScriptRoot
$ToolsDir  = Join-Path $Root "tools"
$FlashExe  = Join-Path $ToolsDir "stm32flash.exe"
$BinPath   = Join-Path $Root "stm32_slave\stm32_slave.ino.bin"
$Getter    = Join-Path $PSScriptRoot "get-stm32flash.ps1"

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

Write-Host ""
Write-Title "==========================================================="
Write-Title "  Supermikrokontroler — STM32 Blue Pill Flash Wizard"
Write-Title "==========================================================="
Write-Host ""
Write-Info "This wizard will:"
Write-Info "  1. Detect the ESP32 COM port"
Write-Info "  2. Guide you through setting BOOT0 = 1 on the STM32"
Write-Info "  3. Verify the STM32 bootloader is responding"
Write-Info "  4. Flash stm32_slave firmware using stm32flash"
Write-Info "  5. Guide you through setting BOOT0 = 0 to run the app"
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
if ([string]::IsNullOrWhiteSpace($espPort)) {
    Write-Err "No COM port specified. Exiting."
    exit 1
}
$espPort = $espPort.Trim().ToUpper()
Write-Host ""

# ---------------------------------------------------------------------------
# Optional loopback test
# ---------------------------------------------------------------------------

$doLoopback = Prompt-User "Run loopback test? (GPIO16 and GPIO17 must be shorted) [y/N]:"
if ($doLoopback -match '^[yY]') {
    Write-Info "Opening $espPort for loopback test..."
    Write-Warn "Opening the port triggers DTR/RTS — disabling to prevent ESP32 reset."
    try {
        $port = New-Object System.IO.Ports.SerialPort($espPort, 115200,
            [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One)
        $port.DtrEnable = $false; $port.RtsEnable = $false
        $port.ReadTimeout = 500; $port.WriteTimeout = 500
        $port.Open()
        Write-Info "Waiting 3 s for ESP32 to boot..."
        Start-Sleep -Seconds 3

        $pass = $true
        foreach ($b in [byte[]](0xAA, 0x55, 0x12, 0x34)) {
            $port.Write([byte[]]$b, 0, 1)
            Start-Sleep -Milliseconds 50
            if ($port.BytesToRead -gt 0) {
                $echo = $port.ReadByte()
                if ($echo -ne $b) {
                    Write-Err ("Loopback mismatch: sent 0x{0:X2}, got 0x{1:X2}" -f $b, $echo)
                    $pass = $false; break
                }
            } else {
                Write-Err ("Loopback: no echo for byte 0x{0:X2}" -f $b)
                $pass = $false; break
            }
        }
        $port.Close()

        if ($pass) {
            Write-Ok "Loopback passed — UART wiring confirmed."
        } else {
            $cont = Prompt-User "Loopback failed. Continue anyway? [y/N]:"
            if ($cont -notmatch '^[yY]') { exit 1 }
        }
    } catch {
        Write-Warn "Loopback error: $_ — continuing."
    }
}
Write-Host ""

# ---------------------------------------------------------------------------
# Step 1: BOOT0 = 1
# ---------------------------------------------------------------------------

Write-Title "--- Step 1: Set BOOT0 = 1 (bootloader mode) ---"
Write-Info "On the STM32 Blue Pill:"
Write-Info "  - Move the BOOT0 jumper from 0 to 1."
Write-Info "  - Press RESET on the Blue Pill."
Write-Host ""
Write-Warn "After BOOT0=1 + RESET the onboard LED (PC13) should NOT blink."
Write-Host ""
$null = Prompt-User "Press ENTER when BOOT0=1 and the Blue Pill has been reset..."
Write-Host ""

# ---------------------------------------------------------------------------
# Step 2: Bootloader detection
# ---------------------------------------------------------------------------

Write-Title "--- Step 2: Bootloader detection ---"
Write-Info "Probing STM32 ROM bootloader on $espPort (8E1, DTR/RTS disabled)..."

$detected = $false
for ($attempt = 1; $attempt -le 5; $attempt++) {
    try {
        $port = New-Object System.IO.Ports.SerialPort($espPort, 115200,
            [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One)
        $port.DtrEnable = $false; $port.RtsEnable = $false
        $port.ReadTimeout = 1000; $port.WriteTimeout = 500
        $port.Open()

        Write-Info "Attempt $attempt / 5 — sending autobaud 0x7F..."
        $port.Write([byte[]](0x7F), 0, 1)
        Start-Sleep -Milliseconds 300

        if ($port.BytesToRead -gt 0) {
            $ack = $port.ReadByte()
            if ($ack -eq 0x79) {
                Write-Ok "Bootloader ACK (0x79) — STM32 is in bootloader mode!"
                $detected = $true
            } elseif ($ack -eq 0x00) {
                Write-Warn "Got 0x00 — RX line held LOW. Check BOOT0 and wiring."
            } else {
                Write-Warn ("Unexpected byte: 0x{0:X2}" -f $ack)
            }
        } else {
            Write-Warn "No response — press RESET on Blue Pill now, then wait for retry..."
        }
        $port.Close()
        if ($detected) { break }
        if ($attempt -lt 5) { Write-Info "Retrying in 2 s..."; Start-Sleep -Seconds 2 }
    } catch {
        Write-Err "Serial error: $_"; break
    }
}

if (-not $detected) {
    Write-Err "Bootloader not detected after 5 attempts."
    Write-Info "Troubleshooting:"
    Write-Info "  1. BOOT0 jumper must be on position 1."
    Write-Info "  2. Press RESET after the wizard opens the port."
    Write-Info "  3. ESP32 must be running esp32_flasher (LED ~150 ms blink)."
    Write-Info "  4. Wiring: GPIO17->PA10, GPIO16<-PA9, 3.3V, GND."
    $cont = Prompt-User "Try flashing anyway? [y/N]:"
    if ($cont -notmatch '^[yY]') { exit 1 }
}
Write-Host ""

# ---------------------------------------------------------------------------
# Step 3: Flash
# ---------------------------------------------------------------------------

Write-Title "--- Step 3: Flashing firmware ---"
Write-Info "Binary : $BinPath"
Write-Info "Port   : $espPort"
Write-Host ""

$flashArgs = @("-b", "115200", "-w", "`"$BinPath`"", "-v", $espPort)
Write-Dim "$ $FlashExe $($flashArgs -join ' ')"
Write-Host ""

try {
    $proc = Start-Process -FilePath $FlashExe -ArgumentList $flashArgs -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -eq 0) {
        Write-Ok "stm32flash completed successfully."
    } else {
        Write-Err "stm32flash exited with code $($proc.ExitCode)."
        Write-Warn "If 'Failed to init device': hold RESET, rerun, release ~3 s after port opens."
        exit 1
    }
} catch {
    Write-Err "Could not run stm32flash: $_"
    exit 1
}
Write-Host ""

# ---------------------------------------------------------------------------
# Step 4: BOOT0 = 0
# ---------------------------------------------------------------------------

Write-Title "--- Step 4: Set BOOT0 = 0 (run application) ---"
Write-Info "Move the BOOT0 jumper back to 0, then press RESET."
Write-Info "The onboard LED (PC13) should blink 3 times on boot."
Write-Host ""
$null = Prompt-User "Press ENTER when done..."
Write-Host ""

Write-Ok "All done!"
Write-Info "Flash esp32_master onto the ESP32 via Arduino IDE."
Write-Info "Open Serial Monitor at 115200 baud, line ending = Newline."
Write-Info "Type 'ping' — you should see PONG and [OK]."
Write-Host ""
Write-Title "==========================================================="
Write-Title "  Happy hacking!  -- Supermikrokontroler"
Write-Title "==========================================================="
Write-Host ""
