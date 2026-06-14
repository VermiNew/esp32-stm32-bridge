#Requires -Version 7.0
<#
.SYNOPSIS
    Shared helpers for all Supermikrokontroler scripts (colors, i18n, COM detection, prereq checks).

.PARAMETER Lang
    Language code: "pl" or "en". Defaults to auto-detect from $PSUICulture.

.EXAMPLE
    Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force
    Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force -ArgumentList "pl"

.NOTES
    Exports ANSI color helpers (Write-Title/Ok/Warn/Err/Info/Dim, Prompt-User),
    COM port detection (Find-EspPort, Select-EspPort),
    and prerequisite checks (Assert-ArduinoCli, Assert-StmFlash, Assert-FirmwareBin).
    Also exports $L (localization hashtable) as a global variable — loaded from lang\{Lang}.psd1.
#>

param([string]$Lang = "")

# ---------------------------------------------------------------------------
# Language selection
# ---------------------------------------------------------------------------

$_langDir = Join-Path $PSScriptRoot "lang"

if (-not $Lang) {
    $detected = if ($PSUICulture -match '^pl') { "pl" } else { "en" }
    $esc = [char]27
    Write-Host ""
    Write-Host "${esc}[38;2;130;200;255m  Select language / Wybierz język${esc}[0m"
    Write-Host "${esc}[38;2;100;100;100m  ──────────────────────────────${esc}[0m"
    Write-Host "  ${esc}[38;2;180;130;255m[1]${esc}[0m English"
    Write-Host "  ${esc}[38;2;180;130;255m[2]${esc}[0m Polski"
    Write-Host ""
    Write-Host -NoNewline "  ${esc}[38;2;180;130;255m[?>]${esc}[0m  Choice / Wybór [default: $detected]: "
    $choice = (Read-Host).Trim()
    $Lang = switch ($choice) {
        "1"  { "en" }
        "2"  { "pl" }
        default { $detected }
    }
    Write-Host ""
}

$_langFile = Join-Path $_langDir "${Lang}.psd1"
if (-not (Test-Path $_langFile)) {
    $Lang = "en"
    $_langFile = Join-Path $_langDir "en.psd1"
}

$script:L    = Import-PowerShellDataFile $_langFile
$script:Lang = $Lang
Set-Variable -Name L    -Value $script:L    -Scope Global
Set-Variable -Name Lang -Value $script:Lang -Scope Global

# ---------------------------------------------------------------------------
# ANSI 24-bit color helpers
# ---------------------------------------------------------------------------

function Get-AnsiColor([int]$r, [int]$g, [int]$b) { return [char]27 + "[38;2;${r};${g};${b}m" }
function Get-AnsiReset { return [char]27 + "[0m" }

$script:T  = Get-AnsiColor 130 200 255
$script:OK = Get-AnsiColor  80 220 100
$script:WN = Get-AnsiColor 255 200  60
$script:ER = Get-AnsiColor 255  80  80
$script:IN = Get-AnsiColor 200 200 200
$script:PR = Get-AnsiColor 180 130 255
$script:DM = Get-AnsiColor 100 100 100
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
    try {
        $devices = Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'CH340|CP210|USB-SERIAL|Silicon Labs|FTDI|UART' -and
                           $_.Name -match 'COM\d' }
        return @($devices | ForEach-Object {
            if ($_.Name -match '(COM\d+)') { $Matches[1] }
        } | Select-Object -Unique)
    } catch { return @() }
}

function Select-EspPort {
    $ports = Find-EspPort
    if ($ports.Count -eq 1) {
        Write-Ok ($L.PortAutoDetected -f $ports[0])
        return $ports[0]
    } elseif ($ports.Count -gt 1) {
        Write-Warn $L.PortMultiple
        $ports | ForEach-Object { Write-Info "  $_" }
        return (Prompt-User $L.PortPrompt)
    } else {
        Write-Warn $L.PortManualPrompt
        return (Prompt-User $L.PortManualPrompt)
    }
}

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

function Assert-ArduinoCli {
    $cmd = Get-Command "arduino-cli" -ErrorAction SilentlyContinue
    if (-not $cmd) {
        Write-Err  $L.ArduinoCliNotFound
        Write-Info $L.ArduinoCliInstall
        Write-Info $L.ArduinoCliDownload
        exit 1
    }
    Write-Ok ($L.ArduinoCliFound -f $cmd.Source)
}

function Assert-StmFlash([string]$exePath, [string]$getterScript) {
    if (-not (Test-Path $exePath)) {
        Write-Warn $L.StmFlashMissing
        if (-not (Test-Path $getterScript)) {
            Write-Err  ($L.StmFlashGetterMissing -f $getterScript)
            Write-Info ($L.StmFlashGetterHint    -f (Split-Path $exePath))
            exit 1
        }
        & $getterScript -Lang $script:Lang
        if (-not (Test-Path $exePath)) {
            Write-Err $L.StmFlashDlFailed; exit 1
        }
    }
    Write-Ok ($L.StmFlashFound -f $exePath)
}

function Assert-FirmwareBin([string]$binPath) {
    if (-not (Test-Path $binPath)) {
        Write-Err  ($L.FirmwareMissing -f $binPath)
        Write-Info $L.FirmwareHint
        Write-Info "  $binPath"
        exit 1
    }
    Write-Ok ($L.FirmwareFound -f $binPath)
}

Export-ModuleMember -Function * -Variable L, Lang

