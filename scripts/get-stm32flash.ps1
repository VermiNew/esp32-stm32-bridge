#Requires -Version 7.0
<#
.SYNOPSIS
    Downloads stm32flash 0.7 from SourceForge, verifies MD5, extracts to tools\.

.PARAMETER Lang
    Language: "pl" or "en" (default: auto-detect).

.NOTES
    Downloads stm32flash-0.7-binaries.zip from SourceForge, verifies MD5,
    extracts win64 (or win32) stm32flash.exe into tools\ next to scripts\.
    Requires internet access and PowerShell 7+.
#>

param([string]$Lang = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force -ArgumentList $Lang

$VERSION  = "0.7"
$ZIP_NAME = "stm32flash-${VERSION}-binaries.zip"
$ZIP_URL  = "https://sourceforge.net/projects/stm32flash/files/${ZIP_NAME}/download"
$MD5_URL  = "https://sourceforge.net/projects/stm32flash/files/MD5SUMS/download"
$TOOLS    = Join-Path (Split-Path $PSScriptRoot) "tools"
$DEST_EXE = Join-Path $TOOLS "stm32flash.exe"
$TEMP_DIR = Join-Path $env:TEMP "stm32flash_dl"
$TEMP_ZIP = Join-Path $TEMP_DIR $ZIP_NAME
$TEMP_MD5 = Join-Path $TEMP_DIR "MD5SUMS"

New-Item -ItemType Directory -Path $TOOLS    -Force | Out-Null
New-Item -ItemType Directory -Path $TEMP_DIR -Force | Out-Null

Write-Host ""
Write-Info ($L.GetFlashTitle -f $VERSION)
Write-Info ($L.GetFlashDest  -f $DEST_EXE)
Write-Host ""

if (Test-Path $DEST_EXE) {
    $ans = Prompt-User $L.GetFlashExistsPrompt
    if ($ans -notmatch '^[ytYT]') { Write-Info $L.GetFlashAborted; exit 0 }
}

try {
    Write-Info $L.GetFlashDlMd5
    Invoke-WebRequest -Uri $MD5_URL -OutFile $TEMP_MD5 -UseBasicParsing -MaximumRedirection 5 -TimeoutSec 30

    $expectedMd5 = $null
    foreach ($line in (Get-Content $TEMP_MD5)) {
        if ($line.Trim() -match '^([a-fA-F0-9]{32})\s+\*?' + [regex]::Escape($ZIP_NAME) + '$') {
            $expectedMd5 = $Matches[1].ToLower(); break
        }
    }
    if (-not $expectedMd5) { Write-Err ($L.GetFlashMd5Missing -f $ZIP_NAME); exit 1 }
    Write-Ok ($L.GetFlashMd5Expected -f $expectedMd5)

    Write-Info ($L.GetFlashDlZip -f $ZIP_NAME)
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $ZIP_URL -OutFile $TEMP_ZIP -UseBasicParsing -MaximumRedirection 10 -TimeoutSec 120
    $ProgressPreference = 'Continue'
    Write-Ok ($L.GetFlashDlBytes -f (Get-Item $TEMP_ZIP).Length)

    Write-Info $L.GetFlashVerifying
    $actualMd5 = (Get-FileHash $TEMP_ZIP -Algorithm MD5).Hash.ToLower()
    if ($actualMd5 -ne $expectedMd5) { Write-Err $L.GetFlashMd5Mismatch; exit 1 }
    Write-Ok $L.GetFlashMd5Ok

    Write-Info $L.GetFlashExtracting
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($TEMP_ZIP)
    try {
        $entries = $zip.Entries | Where-Object { $_.Name -eq 'stm32flash.exe' }
        $chosen  = $entries | Where-Object { $_.FullName -match 'win64' } | Select-Object -First 1
        if (-not $chosen) { $chosen = $entries | Where-Object { $_.FullName -match 'win32' } | Select-Object -First 1 }
        if (-not $chosen) { $chosen = $entries | Select-Object -First 1 }
        if (-not $chosen) { Write-Err $L.GetFlashNotInZip; exit 1 }

        $s = $chosen.Open()
        $o = [System.IO.File]::Create($DEST_EXE)
        try { $s.CopyTo($o) } finally { $o.Close(); $s.Close() }
    } finally { $zip.Dispose() }

    $bytes = [System.IO.File]::ReadAllBytes($DEST_EXE)
    if ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) {
        Write-Ok ($L.GetFlashExtracted -f (Get-Item $DEST_EXE).Length)
    } else {
        Write-Warn $L.GetFlashBadHeader
    }
} finally {
    if (Test-Path $TEMP_DIR) { Remove-Item -Recurse -Force $TEMP_DIR }
}

Write-Host ""
Write-Ok ($L.GetFlashReady -f $DEST_EXE)
Write-Host ""


