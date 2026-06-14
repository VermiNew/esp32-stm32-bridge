#Requires -Version 7.0
<#
.SYNOPSIS
    Shared helpers for Supermikrokontroler scripts.
    Import with: Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force
#>

# ---------------------------------------------------------------------------
# ANSI 24-bit color helpers
# ---------------------------------------------------------------------------

function Get-AnsiColor([int]$r, [int]$g, [int]$b) {
    return [char]27 + "[38;2;${r};${g};${b}m"
}
function Get-AnsiReset { return [char]27 + "[0m" }

$script:T  = Get-AnsiColor 130 200 255  # title  — light blue
$script:OK = Get-AnsiColor  80 220 100  # ok     — green
$script:WN = Get-AnsiColor 255 200  60  # warn   — amber
$script:ER = Get-AnsiColor 255  80  80  # error  — red
$script:IN = Get-AnsiColor 200 200 200  # info   — light grey
$script:PR = Get-AnsiColor 180 130 255  # prompt — purple
$script:DM = Get-AnsiColor 100 100 100  # dim    — dark grey
$script:RS = Get-AnsiReset

function Write-Title([string]$msg) { Write-Host "${script:T}${msg}${script:RS}" }
function Write-Ok   ([string]$msg) { Write-Host "${script:OK}[OK]${script:RS}  $msg" }
function Write-Warn ([string]$msg) { Write-Host "${script:WN}[!!]${script:RS}  $msg" }
function Write-Err  ([string]$msg) { Write-Host "${script:ER}[ERR]${script:RS} $msg" }
function Write-Info ([string]$msg) { Write-Host "${script:IN}      $msg${script:RS}" }
function Write-Dim  ([string]$msg) { Write-Host "${script:DM}$msg${script:RS}" }

function Prompt-User([string]$msg) {
    Write-Host -NoNewline "${script:PR}[?>]${script:RS}  $msg "
    return (Read-Host)
}

# ---------------------------------------------------------------------------
# COM port auto-detection
# ---------------------------------------------------------------------------

function Find-EspPort {
    <#
    .SYNOPSIS
        Returns an array of COM port names matching common ESP32 USB chips.
    #>
    try {
        $devices = Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'CH340|CP210|USB-SERIAL|Silicon Labs|FTDI|UART' -and
                           $_.Name -match 'COM\d' }
        return @($devices | ForEach-Object {
            if ($_.Name -match '(COM\d+)') { $Matches[1] }
        } | Select-Object -Unique)
    } catch {
        return @()
    }
}

function Select-EspPort {
    <#
    .SYNOPSIS
        Auto-detects or prompts for a COM port. Returns the selected port or exits.
    #>
    $ports = Find-EspPort
    if ($ports.Count -eq 1) {
        Write-Ok "Auto-detected port: $($ports[0])"
        return $ports[0]
    } elseif ($ports.Count -gt 1) {
        Write-Warn "Multiple matching COM ports found:"
        $ports | ForEach-Object { Write-Info "  $_" }
        return (Prompt-User "Enter the correct COM port (e.g. COM3):")
    } else {
        Write-Warn "Could not auto-detect an ESP32 COM port."
        return (Prompt-User "Enter the COM port manually (e.g. COM3):")
    }
}

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

function Assert-ArduinoCli {
    <#
    .SYNOPSIS
        Exits with an error message if arduino-cli is not on PATH.
    #>
    $cmd = Get-Command "arduino-cli" -ErrorAction SilentlyContinue
    if (-not $cmd) {
        Write-Err "arduino-cli not found on PATH."
        Write-Info "Install it: winget install ArduinoSA.ArduinoCLI"
        Write-Info "Or download from: https://arduino.github.io/arduino-cli/latest/installation/"
        exit 1
    }
    Write-Ok "arduino-cli: $($cmd.Source)"
}

function Assert-StmFlash([string]$exePath, [string]$getterScript) {
    <#
    .SYNOPSIS
        Checks that stm32flash.exe exists; auto-downloads it if a getter script is provided.
    #>
    if (-not (Test-Path $exePath)) {
        Write-Warn "stm32flash.exe not found — downloading automatically..."
        if (-not (Test-Path $getterScript)) {
            Write-Err "Getter script not found: $getterScript"
            Write-Info "Place stm32flash.exe manually in: $(Split-Path $exePath)"
            exit 1
        }
        & $getterScript
        if (-not (Test-Path $exePath)) {
            Write-Err "Download failed — stm32flash.exe still missing."
            exit 1
        }
    }
    Write-Ok "stm32flash: $exePath"
}

function Assert-FirmwareBin([string]$binPath) {
    if (-not (Test-Path $binPath)) {
        Write-Err "Firmware binary not found: $binPath"
        Write-Info "Compile stm32_slave in Arduino IDE first, then copy the .bin to:"
        Write-Info "  $binPath"
        exit 1
    }
    Write-Ok "Firmware: $binPath"
}

Export-ModuleMember -Function *
