#Requires -Version 7.0
<#
.SYNOPSIS
    Downloads stm32flash 0.7 binaries from SourceForge, verifies the MD5
    checksum against the official MD5SUMS file, extracts stm32flash.exe
    into the current directory, and removes the temporary zip.

.NOTES
    Run from the supermikrokontroler project root.
    Requires internet access and PowerShell 7+.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# ANSI color helpers (24-bit truecolor)
# ---------------------------------------------------------------------------
function rgb($r, $g, $b) { return [char]27 + "[38;2;${r};${g};${b}m" }
function Reset { return [char]27 + "[0m" }

$CLR_OK   = rgb  80 220 100
$CLR_ERR  = rgb 255  80  80
$CLR_WARN = rgb 255 200  60
$CLR_INFO = rgb 200 200 200
$CLR_DIM  = rgb 100 100 100
$RESET    = Reset

function Write-Ok($msg)   { Write-Host "${CLR_OK}[OK]${RESET}  $msg" }
function Write-Err($msg)  { Write-Host "${CLR_ERR}[ERR]${RESET} $msg" }
function Write-Warn($msg) { Write-Host "${CLR_WARN}[!!]${RESET}  $msg" }
function Write-Info($msg) { Write-Host "${CLR_INFO}      $msg${RESET}" }
function Write-Dim($msg)  { Write-Host "${CLR_DIM}$msg${RESET}" }

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
$VERSION     = "0.7"
$ZIP_NAME    = "stm32flash-${VERSION}-binaries.zip"
$ZIP_URL     = "https://sourceforge.net/projects/stm32flash/files/${ZIP_NAME}/download"
$MD5_URL     = "https://sourceforge.net/projects/stm32flash/files/MD5SUMS/download"
$TOOLS_DIR   = Join-Path $PSScriptRoot "tools"
$DEST_EXE    = Join-Path $TOOLS_DIR "stm32flash.exe"
$TEMP_DIR    = Join-Path $env:TEMP "stm32flash_download_$$"
$TEMP_ZIP    = Join-Path $TEMP_DIR $ZIP_NAME
$TEMP_MD5    = Join-Path $TEMP_DIR "MD5SUMS"

New-Item -ItemType Directory -Path $TOOLS_DIR -Force | Out-Null

Write-Host ""
Write-Info "stm32flash $VERSION downloader"
Write-Info "Destination: $DEST_EXE"
Write-Host ""

# ---------------------------------------------------------------------------
# Safety: don't overwrite an existing exe without asking
# ---------------------------------------------------------------------------
if (Test-Path $DEST_EXE) {
    Write-Warn "stm32flash.exe already exists at $DEST_EXE"
    Write-Host -NoNewline "      Overwrite? [y/N]: "
    $ans = Read-Host
    if ($ans -notmatch '^[yY]') {
        Write-Info "Aborted. Existing file kept."
        exit 0
    }
}

# ---------------------------------------------------------------------------
# Create temp dir
# ---------------------------------------------------------------------------
New-Item -ItemType Directory -Path $TEMP_DIR -Force | Out-Null

try {
    # -----------------------------------------------------------------------
    # Step 1 — Download MD5SUMS
    # -----------------------------------------------------------------------
    Write-Info "Downloading MD5SUMS..."
    Write-Dim "  $MD5_URL"
    Invoke-WebRequest -Uri $MD5_URL `
                      -OutFile $TEMP_MD5 `
                      -UseBasicParsing `
                      -MaximumRedirection 5 `
                      -TimeoutSec 30

    # Parse the MD5 for our zip from the file
    # Format: "<hash>  <filename>"
    $expectedMd5 = $null
    foreach ($line in (Get-Content $TEMP_MD5)) {
        $line = $line.Trim()
        if ($line -match '^([a-fA-F0-9]{32})\s+\*?' + [regex]::Escape($ZIP_NAME) + '$') {
            $expectedMd5 = $Matches[1].ToLower()
            break
        }
    }

    if (-not $expectedMd5) {
        Write-Err "Could not find MD5 for $ZIP_NAME in MD5SUMS file."
        Write-Info "MD5SUMS content:"
        Get-Content $TEMP_MD5 | ForEach-Object { Write-Dim "  $_" }
        exit 1
    }
    Write-Ok "Expected MD5: $expectedMd5"

    # -----------------------------------------------------------------------
    # Step 2 — Download zip
    # -----------------------------------------------------------------------
    Write-Info "Downloading $ZIP_NAME..."
    Write-Dim "  $ZIP_URL"

    $ProgressPreference = 'SilentlyContinue'  # Invoke-WebRequest is slow with progress bar
    Invoke-WebRequest -Uri $ZIP_URL `
                      -OutFile $TEMP_ZIP `
                      -UseBasicParsing `
                      -MaximumRedirection 10 `
                      -TimeoutSec 120
    $ProgressPreference = 'Continue'

    $zipSize = (Get-Item $TEMP_ZIP).Length
    Write-Ok ("Downloaded: {0:N0} bytes" -f $zipSize)

    # -----------------------------------------------------------------------
    # Step 3 — Verify MD5
    # -----------------------------------------------------------------------
    Write-Info "Verifying MD5 checksum..."
    $actualMd5 = (Get-FileHash -Path $TEMP_ZIP -Algorithm MD5).Hash.ToLower()
    Write-Info "  Expected : $expectedMd5"
    Write-Info "  Actual   : $actualMd5"

    if ($actualMd5 -ne $expectedMd5) {
        Write-Err "MD5 MISMATCH — download may be corrupted or tampered."
        Write-Err "Do NOT use this file."
        exit 1
    }
    Write-Ok "Checksum verified."

    # -----------------------------------------------------------------------
    # Step 4 — Extract stm32flash.exe (prefer win64, fall back to win32)
    # -----------------------------------------------------------------------
    Write-Info "Extracting stm32flash.exe..."

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($TEMP_ZIP)

    try {
        # List all .exe entries for visibility
        $exeEntries = $zip.Entries | Where-Object { $_.Name -eq 'stm32flash.exe' }
        Write-Info "Executables found in archive:"
        $exeEntries | ForEach-Object { Write-Dim "  $($_.FullName)  ($($_.Length) bytes)" }

        # Pick win64 first, then win32, then anything
        $chosen = $exeEntries | Where-Object { $_.FullName -match 'win64' } | Select-Object -First 1
        if (-not $chosen) {
            $chosen = $exeEntries | Where-Object { $_.FullName -match 'win32' } | Select-Object -First 1
        }
        if (-not $chosen) {
            $chosen = $exeEntries | Select-Object -First 1
        }

        if (-not $chosen) {
            Write-Err "stm32flash.exe not found inside the zip."
            exit 1
        }

        Write-Info "Extracting: $($chosen.FullName)"
        $stream   = $chosen.Open()
        $outStream = [System.IO.File]::Create($DEST_EXE)
        try { $stream.CopyTo($outStream) }
        finally { $outStream.Close(); $stream.Close() }

    } finally {
        $zip.Dispose()
    }

    $exeSize = (Get-Item $DEST_EXE).Length
    Write-Ok ("Extracted stm32flash.exe  ({0:N0} bytes)" -f $exeSize)

    # -----------------------------------------------------------------------
    # Step 5 — Quick sanity check (file header)
    # -----------------------------------------------------------------------
    $bytes = [System.IO.File]::ReadAllBytes($DEST_EXE)
    if ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) {
        Write-Ok "File header valid (MZ — Windows PE executable)."
    } else {
        Write-Warn "File header unexpected (not MZ). The exe may not run correctly."
    }

} finally {
    # Clean up temp dir regardless of success or failure
    if (Test-Path $TEMP_DIR) {
        Remove-Item -Recurse -Force $TEMP_DIR
        Write-Dim "  Temp files cleaned up."
    }
}

Write-Host ""
Write-Ok "stm32flash.exe is ready at: $DEST_EXE"
Write-Info "You can now run flash_script.ps1 to flash the STM32."
Write-Host ""
