#Requires -Version 7.0
<#
.SYNOPSIS
    Shared helpers for Supermikrokontroler scripts.
    Import with: Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force [-ArgumentList "pl"|"en"]

.PARAMETER Lang
    Language code: "pl" or "en". Defaults to auto-detect from $PSUICulture.
#>

param([string]$Lang = "")

# ---------------------------------------------------------------------------
# Language loading
# ---------------------------------------------------------------------------

$_langDir = Join-Path $PSScriptRoot "lang"

if (-not $Lang) {
    $Lang = if ($PSUICulture -match '^pl') { "pl" } else { "en" }
}

$_langFile = Join-Path $_langDir "${Lang}.psd1"
if (-not (Test-Path $_langFile)) {
    $Lang = "en"
    $_langFile = Join-Path $_langDir "en.psd1"
}

$script:L = Import-PowerShellDataFile $_langFile
Set-Variable -Name L -Value $script:L -Scope Global

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
        & $getterScript
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

Export-ModuleMember -Function * -Variable L

