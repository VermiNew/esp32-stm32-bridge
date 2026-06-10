#!/usr/bin/env bash
# get_stm32flash.sh — download stm32flash 0.7, verify MD5, install binary
#
# Usage:
#   ./get_stm32flash.sh            # installs to ./stm32flash
#   ./get_stm32flash.sh /usr/local/bin/stm32flash
#
# Requires: curl or wget, unzip, md5sum (Linux) or md5 (macOS)

set -euo pipefail

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
VERSION="0.7"
ZIP_NAME="stm32flash-${VERSION}-binaries.zip"
ZIP_URL="https://sourceforge.net/projects/stm32flash/files/${ZIP_NAME}/download"
MD5_URL="https://sourceforge.net/projects/stm32flash/files/MD5SUMS/download"
DEST="${1:-$(dirname "$0")/stm32flash}"
TMPDIR_LOCAL="$(mktemp -d)"

# ---------------------------------------------------------------------------
# Color helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; DIM='\033[2m'; NC='\033[0m'

ok()   { echo -e "${GREEN}[OK]${NC}  $*"; }
err()  { echo -e "${RED}[ERR]${NC} $*" >&2; }
warn() { echo -e "${YELLOW}[!!]${NC}  $*"; }
info() { echo -e "${CYAN}      $*${NC}"; }
dim()  { echo -e "${DIM}$*${NC}"; }

# ---------------------------------------------------------------------------
# Cleanup on exit
# ---------------------------------------------------------------------------
cleanup() { rm -rf "$TMPDIR_LOCAL"; dim "  Temp files cleaned up."; }
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Detect download tool
# ---------------------------------------------------------------------------
if command -v curl &>/dev/null; then
    download() { curl -fsSL --max-redirs 10 --retry 3 -o "$1" "$2"; }
    DL_TOOL="curl"
elif command -v wget &>/dev/null; then
    download() { wget -q --max-redirect=10 -O "$1" "$2"; }
    DL_TOOL="wget"
else
    err "Neither curl nor wget found. Install one and retry."
    exit 1
fi

# ---------------------------------------------------------------------------
# Detect MD5 tool
# ---------------------------------------------------------------------------
if command -v md5sum &>/dev/null; then
    md5_of() { md5sum "$1" | awk '{print $1}'; }
elif command -v md5 &>/dev/null; then
    md5_of() { md5 -q "$1"; }
else
    err "No MD5 tool found (md5sum / md5). Cannot verify checksum."
    exit 1
fi

echo ""
info "stm32flash ${VERSION} downloader (using ${DL_TOOL})"
info "Destination: ${DEST}"
echo ""

# ---------------------------------------------------------------------------
# Safety: don't overwrite without asking
# ---------------------------------------------------------------------------
if [[ -f "$DEST" ]]; then
    warn "stm32flash already exists at $DEST"
    read -rp "      Overwrite? [y/N]: " ans
    if [[ ! "$ans" =~ ^[yY]$ ]]; then
        info "Aborted. Existing file kept."
        exit 0
    fi
fi

# ---------------------------------------------------------------------------
# Step 1 — Download MD5SUMS
# ---------------------------------------------------------------------------
info "Downloading MD5SUMS..."
dim "  $MD5_URL"
download "${TMPDIR_LOCAL}/MD5SUMS" "$MD5_URL"

# Parse MD5 for our zip
EXPECTED_MD5=$(grep -E "[a-f0-9]{32}[[:space:]]+\*?${ZIP_NAME}$" \
               "${TMPDIR_LOCAL}/MD5SUMS" | awk '{print $1}' | tr '[:upper:]' '[:lower:]' || true)

if [[ -z "$EXPECTED_MD5" ]]; then
    err "Could not find MD5 for $ZIP_NAME in MD5SUMS."
    info "MD5SUMS contents:"
    cat "${TMPDIR_LOCAL}/MD5SUMS" | while IFS= read -r line; do dim "  $line"; done
    exit 1
fi
ok "Expected MD5: $EXPECTED_MD5"

# ---------------------------------------------------------------------------
# Step 2 — Download zip
# ---------------------------------------------------------------------------
ZIP_PATH="${TMPDIR_LOCAL}/${ZIP_NAME}"
info "Downloading $ZIP_NAME..."
dim "  $ZIP_URL"
download "$ZIP_PATH" "$ZIP_URL"

ZIP_SIZE=$(wc -c < "$ZIP_PATH")
ok "Downloaded: ${ZIP_SIZE} bytes"

# ---------------------------------------------------------------------------
# Step 3 — Verify MD5
# ---------------------------------------------------------------------------
info "Verifying MD5 checksum..."
ACTUAL_MD5=$(md5_of "$ZIP_PATH" | tr '[:upper:]' '[:lower:]')
info "  Expected : $EXPECTED_MD5"
info "  Actual   : $ACTUAL_MD5"

if [[ "$ACTUAL_MD5" != "$EXPECTED_MD5" ]]; then
    err "MD5 MISMATCH — download may be corrupted or tampered."
    err "Do NOT use this file."
    exit 1
fi
ok "Checksum verified."

# ---------------------------------------------------------------------------
# Step 4 — Detect platform and extract the right binary
# ---------------------------------------------------------------------------
info "Inspecting zip contents..."
if ! command -v unzip &>/dev/null; then
    err "unzip not found. Install it (apt install unzip / brew install unzip)."
    exit 1
fi

unzip -l "$ZIP_PATH" | grep -i 'stm32flash' | while IFS= read -r line; do
    dim "  $line"
done

ARCH=$(uname -m)
OS=$(uname -s)

# Determine which path inside the zip to extract
if [[ "$OS" == "Linux" ]]; then
    if [[ "$ARCH" == "x86_64" ]]; then
        ENTRY_PATTERN="linux-x86_64/stm32flash"
    elif [[ "$ARCH" == arm* ]] || [[ "$ARCH" == aarch64 ]]; then
        ENTRY_PATTERN="linux-arm/stm32flash"
    else
        ENTRY_PATTERN="linux/stm32flash"
    fi
elif [[ "$OS" == "Darwin" ]]; then
    ENTRY_PATTERN="macos/stm32flash"
    # Some releases use 'darwin' or 'osx'
else
    warn "Unknown OS '$OS'. Trying generic Linux x86_64 binary."
    ENTRY_PATTERN="linux-x86_64/stm32flash"
fi

# Find the matching entry (case-insensitive, partial match)
ENTRY=$(unzip -l "$ZIP_PATH" | awk '{print $4}' | grep -i "${ENTRY_PATTERN}" | head -1 || true)

if [[ -z "$ENTRY" ]]; then
    # Fallback: grab any non-.exe file named 'stm32flash'
    ENTRY=$(unzip -l "$ZIP_PATH" | awk '{print $4}' | grep -v '\.exe$' | grep -i 'stm32flash$' | head -1 || true)
fi

if [[ -z "$ENTRY" ]]; then
    err "Could not find a suitable stm32flash binary for ${OS}/${ARCH} in the zip."
    info "Available entries:"
    unzip -l "$ZIP_PATH" | awk '{print $4}' | grep -i 'stm32flash' | while IFS= read -r e; do
        dim "  $e"
    done
    exit 1
fi

info "Extracting: $ENTRY"
unzip -p "$ZIP_PATH" "$ENTRY" > "$DEST"
chmod +x "$DEST"

EXE_SIZE=$(wc -c < "$DEST")
ok "Extracted stm32flash  (${EXE_SIZE} bytes) -> $DEST"

# ---------------------------------------------------------------------------
# Step 5 — Sanity check: run --help
# ---------------------------------------------------------------------------
info "Running stm32flash --help to verify..."
if "$DEST" --help 2>&1 | head -3 | grep -qi 'stm32flash'; then
    ok "Binary runs correctly."
else
    warn "Binary ran but output was unexpected. It may still work — check manually."
fi

echo ""
ok "stm32flash is ready at: $DEST"
info "You can now run flash_stm32.sh to flash the STM32."
echo ""
