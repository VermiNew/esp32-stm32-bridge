#pragma once
/**
 * protocol.h — shared protocol definitions
 *
 * Included by both stm32_slave and esp32_master sketches.
 * Pure C++11, no platform-specific dependencies beyond Arduino String.
 *
 * Frame format (v2):
 *   With payload  : TYPE:SEQ:CCCC:DATA\n
 *   Without payload: TYPE:SEQ\n
 *   Bare control  : PING / PONG / RESET / HEARTBEAT / etc.
 *
 *   SEQ  = 3-digit zero-padded (001..999, wraps to 001)
 *   CCCC = CRC16-CCITT of DATA field (4 uppercase hex digits)
 *   DATA = command or result string (ASCII, no \n)
 *
 * Frames WITHOUT CRC (no payload): RECV, BUSY, FREE, POLL
 * Frames WITH CRC (have payload)  : SEND, DONE, ERR
 * Bare frames (no SEQ)            : PING, PONG, RESET, RESET:ACK,
 *                                   HEARTBEAT, HEARTBEAT:ACK
 */

// ---------------------------------------------------------------------------
// CRC16-CCITT  (poly 0x1021, init 0xFFFF)
// Computed over the DATA field bytes only.
// ---------------------------------------------------------------------------

static inline uint16_t proto_crc16(const char* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(uint8_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
        }
    }
    return crc;
}

static inline uint16_t proto_crc16(const String& s) {
    return proto_crc16(s.c_str(), s.length());
}

static inline String proto_crc_str(const String& data) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%04X", proto_crc16(data));
    return String(buf);
}

// ---------------------------------------------------------------------------
// String splitting
// ---------------------------------------------------------------------------

// Split 's' on 'delim' into tokens[0..maxTokens-1].
// Returns number of tokens found.
static inline int splitTokens(const String& s, char delim,
                               String* tokens, int maxTokens) {
    int count = 0;
    int start = 0;
    int slen  = (int)s.length();
    for (int i = 0; i <= slen && count < maxTokens; i++) {
        if (i == slen || s.charAt(i) == delim) {
            tokens[count++] = s.substring(start, i);
            start = i + 1;
        }
    }
    return count;
}

// Join tokens[0..n-1] with 'delim'.
static inline String joinTokens(const String* tokens, int n, char delim = ':') {
    String out;
    for (int i = 0; i < n; i++) {
        if (i) out += delim;
        out += tokens[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pin token parser
// Token format: "A0".."A15", "B0".."B15", "C13".."C15"
// Returns STM32duino flat pin index:  PA0=0, PB0=16, PC0=32
// Returns -1 on parse error.
// ---------------------------------------------------------------------------

static inline int parsePin(const String& tok) {
    if (tok.length() < 2) return -1;
    char port = (char)toupper((unsigned char)tok.charAt(0));
    if (port < 'A' || port > 'C') return -1;
    for (int i = 1; i < (int)tok.length(); i++)
        if (!isDigit((unsigned char)tok.charAt(i))) return -1;
    int n = tok.substring(1).toInt();
    if (n < 0 || n > 15) return -1;
    return (port - 'A') * 16 + n;
}

// ---------------------------------------------------------------------------
// Hex encode / decode (uppercase, no separators: "FF0102AB")
// ---------------------------------------------------------------------------

static inline String bytesToHex(const uint8_t* data, size_t len) {
    String out;
    out.reserve(len * 2 + 1);
    char buf[3];
    for (size_t i = 0; i < len; i++) {
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        out += buf;
    }
    return out;
}

// Returns number of bytes decoded, -1 on error.
static inline int hexToBytes(const String& hex, uint8_t* out, size_t maxLen) {
    size_t hexLen = hex.length();
    if (hexLen % 2 != 0) return -1;
    size_t count = hexLen / 2;
    if (count > maxLen) return -1;
    for (size_t i = 0; i < count; i++) {
        char hi = hex.charAt(i * 2);
        char lo = hex.charAt(i * 2 + 1);
        auto h2n = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int hv = h2n(hi), lv = h2n(lo);
        if (hv < 0 || lv < 0) return -1;
        out[i] = (uint8_t)((hv << 4) | lv);
    }
    return (int)count;
}

// ---------------------------------------------------------------------------
// Sequence number helpers
// ---------------------------------------------------------------------------

static inline String seqStr(int n) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%03d", n);
    return String(buf);
}

// Advance seq 1..999, wrapping back to 1.
static inline int seqNext(int seq) {
    return (seq >= 999) ? 1 : seq + 1;
}

// ---------------------------------------------------------------------------
// Master state machine states (shared between esp32_master.ino and wifi_ntp.h)
// ---------------------------------------------------------------------------
enum class State { IDLE, WAIT_RECV, WAIT_DONE, POLLING };
