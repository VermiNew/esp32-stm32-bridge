#Requires -Version 7.0
<#
.SYNOPSIS
    Downloads STM32RTC from GitHub and installs it via arduino-cli (unsafe zip install).

.PARAMETER Lang
    Language: "pl" or "en" (default: auto-detect).

.EXAMPLE
    .\get-stm32rtc.ps1
    .\get-stm32rtc.ps1 -Lang pl

.NOTES
    Fetches the latest STM32RTC zip from https://github.com/stm32duino/STM32RTC and
    installs it with: arduino-cli lib install --zip-path.
    Requires arduino-cli on PATH, internet access, and unsafe library installs enabled
    (arduino-cli config set library.enable_unsafe_install true).
#>

param([string]$Lang = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force -ArgumentList $Lang

Assert-ArduinoCli

$ZipUrl  = 'https://github.com/stm32duino/STM32RTC/archive/refs/heads/main.zip'
$ZipPath = Join-Path $env:TEMP "STM32RTC-main.zip"

Write-Host ""
Write-Info $L.GetRtcDownloading
$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath -UseBasicParsing
$ProgressPreference = 'Continue'
Write-Ok ($L.GetRtcDownloaded -f (Get-Item $ZipPath).Length)

Write-Info $L.GetRtcInstalling
arduino-cli config set library.enable_unsafe_install true
arduino-cli lib install --zip-path $ZipPath

Remove-Item $ZipPath
Write-Host ""
Write-Ok $L.GetRtcDone
Write-Host ""


