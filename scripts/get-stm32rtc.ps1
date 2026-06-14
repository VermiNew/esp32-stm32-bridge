#Requires -Version 7.0
<#
.SYNOPSIS
    Downloads the STM32RTC library from GitHub and installs it via arduino-cli.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force

Assert-ArduinoCli

$ZipUrl  = 'https://github.com/stm32duino/STM32RTC/archive/refs/heads/main.zip'
$ZipPath = Join-Path $env:TEMP "STM32RTC-main.zip"

Write-Host ""
Write-Info "Downloading STM32RTC from GitHub..."
$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath -UseBasicParsing
$ProgressPreference = 'Continue'
Write-Ok ("Downloaded: {0:N0} bytes" -f (Get-Item $ZipPath).Length)

Write-Info "Installing via arduino-cli..."
arduino-cli config set library.enable_unsafe_install true
arduino-cli lib install --zip-path $ZipPath

Remove-Item $ZipPath
Write-Host ""
Write-Ok "STM32RTC library installed."
Write-Host ""
