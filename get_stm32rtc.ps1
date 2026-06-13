#Requires -Version 7.0
<#
.SYNOPSIS
    Downloads the STM32RTC library from GitHub and installs it via arduino-cli.

.NOTES
    Run from the supermikrokontroler project root.
    Requires internet access, PowerShell 7+, and arduino-cli on PATH.
#>

$ErrorActionPreference = 'Stop'
$ZipUrl  = 'https://github.com/stm32duino/STM32RTC/archive/refs/heads/main.zip'
$ZipPath = Join-Path $PSScriptRoot 'STM32RTC-main.zip'

Write-Host "Downloading STM32RTC from GitHub..."
Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath

Write-Host "Installing via arduino-cli..."
arduino-cli config set library.enable_unsafe_install true
arduino-cli lib install --zip-path $ZipPath

Remove-Item $ZipPath
Write-Host "Done. STM32RTC library installed."
