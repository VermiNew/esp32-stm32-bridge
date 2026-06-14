#Requires -Version 7.0
<#
.SYNOPSIS
    Downloads stm32flash 0.7 from SourceForge, verifies MD5, extracts to tools\.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot "Shared.psm1") -Force

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
Write-Info "stm32flash $VERSION downloader"
Write-Info "Destination: $DEST_EXE"
Write-Host ""

if (Test-Path $DEST_EXE) {
    $ans = Prompt-User "stm32flash.exe already exists. Overwrite? [y/N]:"
    if ($ans -notmatch '^[yY]') { Write-Info "Aborted."; exit 0 }
}

try {
    Write-Info "Downloading MD5SUMS..."
    Invoke-WebRequest -Uri $MD5_URL -OutFile $TEMP_MD5 -UseBasicParsing -MaximumRedirection 5 -TimeoutSec 30

    $expectedMd5 = $null
    foreach ($line in (Get-Content $TEMP_MD5)) {
        if ($line.Trim() -match '^([a-fA-F0-9]{32})\s+\*?' + [regex]::Escape($ZIP_NAME) + '$') {
            $expectedMd5 = $Matches[1].ToLower(); break
        }
    }
    if (-not $expectedMd5) {
        Write-Err "MD5 entry for $ZIP_NAME not found in MD5SUMS."; exit 1
    }
    Write-Ok "Expected MD5: $expectedMd5"

    Write-Info "Downloading $ZIP_NAME..."
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $ZIP_URL -OutFile $TEMP_ZIP -UseBasicParsing -MaximumRedirection 10 -TimeoutSec 120
    $ProgressPreference = 'Continue'
    Write-Ok ("Downloaded: {0:N0} bytes" -f (Get-Item $TEMP_ZIP).Length)

    Write-Info "Verifying MD5..."
    $actualMd5 = (Get-FileHash $TEMP_ZIP -Algorithm MD5).Hash.ToLower()
    if ($actualMd5 -ne $expectedMd5) {
        Write-Err "MD5 MISMATCH — aborting."; exit 1
    }
    Write-Ok "Checksum OK."

    Write-Info "Extracting stm32flash.exe..."
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($TEMP_ZIP)
    try {
        $entries = $zip.Entries | Where-Object { $_.Name -eq 'stm32flash.exe' }
        $chosen  = $entries | Where-Object { $_.FullName -match 'win64' } | Select-Object -First 1
        if (-not $chosen) { $chosen = $entries | Where-Object { $_.FullName -match 'win32' } | Select-Object -First 1 }
        if (-not $chosen) { $chosen = $entries | Select-Object -First 1 }
        if (-not $chosen) { Write-Err "stm32flash.exe not found in archive."; exit 1 }

        $s = $chosen.Open()
        $o = [System.IO.File]::Create($DEST_EXE)
        try { $s.CopyTo($o) } finally { $o.Close(); $s.Close() }
    } finally { $zip.Dispose() }

    $bytes = [System.IO.File]::ReadAllBytes($DEST_EXE)
    if ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) {
        Write-Ok ("Extracted: {0:N0} bytes (valid MZ header)" -f (Get-Item $DEST_EXE).Length)
    } else {
        Write-Warn "Unexpected file header — exe may not run."
    }
} finally {
    if (Test-Path $TEMP_DIR) { Remove-Item -Recurse -Force $TEMP_DIR }
}

Write-Host ""
Write-Ok "stm32flash.exe ready at: $DEST_EXE"
Write-Host ""
