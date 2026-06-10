#Requires -Version 7.0
<#
.SYNOPSIS
    Interactive flashing wizard for the Supermikrokontroler project.
    Guides you through flashing stm32_slave firmware onto the STM32 Blue Pill
    using the ESP32 as a USB-to-UART bridge (esp32_flasher firmware).

.NOTES
    - Requires stm32flash.exe in the same directory or on PATH.
    - Requires stm32_slave/stm32_slave.ino.bin to exist (compiled in Arduino IDE).
    - PowerShell 7+ for true cross-platform ANSI and System.IO.Ports.
    - 24-bit ANSI truecolor output — terminal must support it (Windows Terminal,
      modern cmd.exe with VT enabled, etc.).
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# 24-bit ANSI color helpers
# ---------------------------------------------------------------------------

function rgb($r, $g, $b) { return [char]27 + "[38;2;${r};${g};${b}m" }
function bgRgb($r, $g, $b) { return [char]27 + "[48;2;${r};${g};${b}m" }
function Reset { return [char]27 + "[0m" }

$CLR_TITLE   = rgb 130 200 255   # light blue
$CLR_OK      = rgb  80 220 100   # green
$CLR_WARN    = rgb 255 200  60   # amber
$CLR_ERR     = rgb 255  80  80   # red
$CLR_INFO    = rgb 200 200 200   # light grey
$CLR_PROMPT  = rgb 180 130 255   # purple
$CLR_DIM     = rgb 100 100 100   # dim grey
$RESET       = Reset

function Write-Title($msg) {
    Write-Host "${CLR_TITLE}${msg}${RESET}"
}
function Write-Ok($msg) {
    Write-Host "${CLR_OK}[OK]${RESET}  $msg"
}
function Write-Warn($msg) {
    Write-Host "${CLR_WARN}[!!]${RESET}  $msg"
}
function Write-Err($msg) {
    Write-Host "${CLR_ERR}[ERR]${RESET} $msg"
}
function Write-Info($msg) {
    Write-Host "${CLR_INFO}      $msg${RESET}"
}
function Write-Dim($msg) {
    Write-Host "${CLR_DIM}$msg${RESET}"
}
function Prompt-User($msg) {
    Write-Host -NoNewline "${CLR_PROMPT}[?>]${RESET}  $msg "
    return (Read-Host)
}

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
Write-Info "  2. Verify the bridge firmware is responding"
Write-Info "  3. Guide you through setting BOOT0 = 1 on the STM32"
Write-Info "  4. Flash stm32_slave.ino.bin using stm32flash"
Write-Info "  5. Guide you through setting BOOT0 = 0 to run the app"
Write-Host ""

# ---------------------------------------------------------------------------
# Locate required files
# ---------------------------------------------------------------------------

$ScriptDir  = $PSScriptRoot
$BinPath    = Join-Path $ScriptDir "stm32_slave\stm32_slave.ino.bin"
$FlashExe   = Join-Path $ScriptDir "stm32flash.exe"
if (-not (Test-Path $FlashExe)) {
    # Fall back to PATH
    $FlashExe = "stm32flash.exe"
}

$flashOnPath = $null -ne (Get-Command $FlashExe -ErrorAction SilentlyContinue)
if (-not (Test-Path (Join-Path $ScriptDir "stm32flash.exe")) -and -not $flashOnPath) {
    Write-Err "stm32flash.exe not found."
    Write-Info "Download it from: https://sourceforge.net/projects/stm32flash/"
    Write-Info "Place stm32flash.exe next to this script."
    exit 1
}
Write-Ok "Found stm32flash: $FlashExe"

if (-not (Test-Path $BinPath)) {
    Write-Err "Firmware binary not found at: $BinPath"
    Write-Info "Compile stm32_slave in Arduino IDE first, then locate the .bin"
    Write-Info "in your Arduino build output and copy it to that path."
    exit 1
}
Write-Ok "Found firmware: $BinPath"
Write-Host ""

# ---------------------------------------------------------------------------
# Auto-detect ESP32 COM port
# ---------------------------------------------------------------------------

Add-Type -AssemblyName System.IO.Ports

Write-Info "Scanning for ESP32 COM port..."

$espPort = $null
try {
    $devices = Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match 'CH340|CP210|USB-SERIAL|Silicon Labs|FTDI|UART' -and
                       $_.Name -match 'COM\d' }

    $comPorts = foreach ($dev in $devices) {
        if ($dev.Name -match '(COM\d+)') { $Matches[1] }
    }
    $comPorts = @($comPorts | Select-Object -Unique)
} catch {
    $comPorts = @()
}

if ($comPorts.Count -eq 1) {
    $espPort = $comPorts[0]
    Write-Ok "Auto-detected port: $espPort"
} elseif ($comPorts.Count -gt 1) {
    Write-Warn "Multiple matching COM ports found:"
    $comPorts | ForEach-Object { Write-Info "  $_" }
    $espPort = Prompt-User "Enter the correct COM port (e.g. COM3):"
} else {
    Write-Warn "Could not auto-detect an ESP32 COM port."
    $espPort = Prompt-User "Enter the COM port manually (e.g. COM3):"
}

if ([string]::IsNullOrWhiteSpace($espPort)) {
    Write-Err "No COM port specified. Exiting."
    exit 1
}
$espPort = $espPort.Trim().ToUpper()
Write-Host ""

# ---------------------------------------------------------------------------
# Optional loopback test (GPIO16 <-> GPIO17 shorted)
# ---------------------------------------------------------------------------

$doLoopback = Prompt-User "Run loopback test? (GPIO16 and GPIO17 must be shorted) [y/N]:"
if ($doLoopback -match '^[yY]') {
    Write-Info "Opening $espPort for loopback test..."
    Write-Warn "IMPORTANT: Opening the port triggers DTR/RTS which RESETS the ESP32."
    Write-Info "Disabling DTR and RTS to prevent reset..."

    try {
        $port = New-Object System.IO.Ports.SerialPort($espPort, 115200, [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One)
        $port.DtrEnable = $false
        $port.RtsEnable = $false
        $port.ReadTimeout  = 500
        $port.WriteTimeout = 500
        $port.Open()

        # Wait for ESP32 to finish booting (it resets when powered up even without DTR)
        Write-Info "Waiting 3 s for ESP32 to boot..."
        Start-Sleep -Seconds 3

        $testBytes = [byte[]](0xAA, 0x55, 0x12, 0x34)
        $pass = $true
        foreach ($b in $testBytes) {
            $port.Write([byte[]]$b, 0, 1)
            Start-Sleep -Milliseconds 50
            if ($port.BytesToRead -gt 0) {
                $echo = $port.ReadByte()
                if ($echo -ne $b) {
                    Write-Err ("Loopback mismatch: sent 0x{0:X2}, got 0x{1:X2}" -f $b, $echo)
                    $pass = $false
                    break
                }
            } else {
                Write-Err "Loopback: no echo received for byte 0x$('{0:X2}' -f $b)"
                $pass = $false
                break
            }
        }
        $port.Close()
        if ($pass) {
            Write-Ok "Loopback test passed — UART bridge wiring confirmed."
        } else {
            Write-Warn "Loopback failed. Check GPIO16/GPIO17 jumper and continue anyway? [y/N]:"
            $cont = Prompt-User ""
            if ($cont -notmatch '^[yY]') { exit 1 }
        }
    } catch {
        Write-Warn "Loopback test error: $_"
        Write-Warn "Continuing without loopback test."
    }
}
Write-Host ""

# ---------------------------------------------------------------------------
# BOOT0 = 1 instruction
# ---------------------------------------------------------------------------

Write-Title "--- Step 1: Set BOOT0 = 1 (bootloader mode) ---"
Write-Info "On the STM32 Blue Pill:"
Write-Info "  - Locate the BOOT0 jumper (labeled 'BOOT0' near the USB connector)."
Write-Info "  - Move it from position 0 to position 1."
Write-Info "  - Reconnect 3.3V if you disconnected it, or press RESET on the Blue Pill."
Write-Host ""
Write-Warn "After BOOT0=1 + RESET, the onboard LED (PC13) should NOT blink."
Write-Warn "If it blinks, the application is still running — check BOOT0 jumper."
Write-Host ""
$null = Prompt-User "Press ENTER when BOOT0=1 and the Blue Pill has been reset..."
Write-Host ""

# ---------------------------------------------------------------------------
# Bootloader detection (send 0x7F, expect 0x79 ACK)
# ---------------------------------------------------------------------------

Write-Title "--- Step 2: Bootloader detection ---"
Write-Info "Opening $espPort at 115200 8E1 to probe the STM32 ROM bootloader..."
Write-Info "(Disabling DTR/RTS so the ESP32 does not reset when we open the port)"

$detected = $false
$maxProbeAttempts = 5

for ($attempt = 1; $attempt -le $maxProbeAttempts; $attempt++) {
    try {
        $port = New-Object System.IO.Ports.SerialPort($espPort, 115200, [System.IO.Ports.Parity]::Even, 8, [System.IO.Ports.StopBits]::One)
        $port.DtrEnable    = $false
        $port.RtsEnable    = $false
        $port.ReadTimeout  = 1000
        $port.WriteTimeout = 500
        $port.Open()

        Write-Info "Attempt $attempt / $maxProbeAttempts — sending autobaud byte 0x7F..."
        # The STM32 ROM bootloader does autobaud on the FIRST byte after reset.
        # 0x7F sent immediately triggers it; it replies 0x79 (ACK).
        $port.Write([byte[]](0x7F), 0, 1)
        Start-Sleep -Milliseconds 300

        if ($port.BytesToRead -gt 0) {
            $ack = $port.ReadByte()
            if ($ack -eq 0x79) {
                Write-Ok "Bootloader ACK (0x79) received — STM32 is in bootloader mode!"
                $detected = $true
            } elseif ($ack -eq 0x00) {
                Write-Warn "Got 0x00 — STM32 RX line is held LOW."
                Write-Warn "Check: Is USART1 (PA9/PA10) wired to ESP32 GPIO17/GPIO16?"
                Write-Warn "Check: Is BOOT0 really set to 1?"
            } else {
                Write-Warn ("Unexpected byte: 0x{0:X2} (expected 0x79)" -f $ack)
            }
        } else {
            Write-Warn "No response. Is BOOT0=1 and was RESET pressed AFTER the port opened?"
            Write-Info "Tip: press RESET on the Blue Pill NOW, then we will retry..."
        }
        $port.Close()
        if ($detected) { break }

        if ($attempt -lt $maxProbeAttempts) {
            Write-Info "Waiting 2 s before retry..."
            Start-Sleep -Seconds 2
        }
    } catch {
        Write-Err "Serial port error: $_"
        break
    }
}

if (-not $detected) {
    Write-Err "Failed to detect STM32 bootloader after $maxProbeAttempts attempts."
    Write-Info ""
    Write-Info "Troubleshooting:"
    Write-Info "  1. Confirm BOOT0 jumper is on position '1' (not '0')."
    Write-Info "  2. Press RESET on the Blue Pill AFTER the wizard opens the COM port."
    Write-Info "  3. Confirm the ESP32 is running esp32_flasher (LED blinks fast ~150ms)."
    Write-Info "  4. Confirm wiring: ESP32 GPIO17->PA10, GPIO16<-PA9, 3.3V, GND."
    Write-Info "  5. If stm32flash says 'interface not closed properly' later — it is harmless."
    $cont = Prompt-User "Try flashing anyway? [y/N]:"
    if ($cont -notmatch '^[yY]') { exit 1 }
}
Write-Host ""

# ---------------------------------------------------------------------------
# Flash
# ---------------------------------------------------------------------------

Write-Title "--- Step 3: Flashing firmware ---"
Write-Info "Running stm32flash on $espPort..."
Write-Info "  Binary: $BinPath"
Write-Host ""

$flashArgs = @("-b", "115200", "-w", "`"$BinPath`"", "-v", $espPort)
Write-Dim "$ $FlashExe $($flashArgs -join ' ')"
Write-Host ""

try {
    $proc = Start-Process -FilePath $FlashExe `
                          -ArgumentList $flashArgs `
                          -NoNewWindow -Wait -PassThru
    if ($proc.ExitCode -eq 0) {
        Write-Ok "stm32flash completed successfully."
    } else {
        Write-Err "stm32flash exited with code $($proc.ExitCode)."
        Write-Warn "If the error is 'Failed to init device': hold RESET on the Blue Pill,"
        Write-Warn "run the script again, then release RESET ~3 s after it opens the port."
        exit 1
    }
} catch {
    Write-Err "Could not run stm32flash: $_"
    exit 1
}
Write-Host ""

# ---------------------------------------------------------------------------
# BOOT0 = 0 instruction
# ---------------------------------------------------------------------------

Write-Title "--- Step 4: Set BOOT0 = 0 (run application) ---"
Write-Info "Move the BOOT0 jumper back to position 0."
Write-Info "Then press RESET on the Blue Pill."
Write-Info "The onboard LED (PC13) should blink 3 times as the slave firmware boots."
Write-Host ""
$null = Prompt-User "Press ENTER when done..."
Write-Host ""

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------

Write-Ok "All done!"
Write-Info "Now flash esp32_master.ino onto the ESP32 (via Arduino IDE, normal upload)."
Write-Info "Open the Serial Monitor at 115200 baud, line ending = Newline."
Write-Info "Type 'ping' — you should get PONG and [OK]."
Write-Host ""
Write-Title "==========================================================="
Write-Title "  Happy hacking!  -- Supermikrokontroler"
Write-Title "==========================================================="
Write-Host ""
