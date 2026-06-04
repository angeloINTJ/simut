/**
 * @file SystemDefs_Validate.h
 * @brief Input validation helpers.
 * @details parseIntStrict, isValidCfgString, isValidName, isSafeUploadFilename,
 * isValidIpv4, isInRange. Pure inline helpers, no dependencies
 * outside Arduino String and <string.h>. Sub-header of SystemDefs.h
 * (facade).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <string.h>

/** Parse a String as strict int (optional '+' or '-' + digits only).
 * Returns true if well-formed; false if empty, contains spaces/letters, or sign-only.
 * Differentiates legitimate "0" from non-numeric input (which String::toInt() silently maps to 0). */
inline bool parseIntStrict(const String& s, int& out) {
 if (s.length( ) == 0) return false;
 size_t start = 0;
 if (s[0] == '-' || s[0] == '+') {
 if (s.length( ) == 1) return false; /* sign only, invalid */
 start = 1;
 }
 for (size_t i = start; i < s.length( ); i++) {
 if (s[i] < '0' || s[i] > '9') return false;
 }
 out = s.toInt( );
 return true;
}

/** Strict float parse. Accepts optional '+'/'-' + digits with at most
 * 1 decimal point (no exponents). Differentiates legitimate "0"/"0.0"
 * from non-numeric input (which String::toFloat() silently maps to 0).
 * Does not accept spaces, decimal commas (locale), nor scientific notation. */
inline bool parseFloatStrict(const String& s, float& out) {
 if (s.length( ) == 0) return false;
 size_t start = 0;
 if (s[0] == '-' || s[0] == '+') {
 if (s.length( ) == 1) return false;
 start = 1;
 }
 bool seenDot = false;
 bool seenDigit = false;
 for (size_t i = start; i < s.length( ); i++) {
 char c = s[i];
 if (c == '.') {
 if (seenDot) return false;
 seenDot = true;
 } else if (c >= '0' && c <= '9') {
 seenDigit = true;
 } else {
 return false;
 }
 }
 if (!seenDigit) return false;
 out = s.toFloat( );
 return true;
}

/** Validate generic config string: allows empty, rejects control chars (<32).
 * Accepts any printable char (including " and \) because WPA2 passwords, URLs and
 * legitimate paths may contain them. The CLI parser does not use quote escaping
 * in this context — the value is read raw until end of line.
 * maxLen is the usable size (not counting the terminating '\0' of the destination buffer). */
inline bool isValidCfgString(const char* s, size_t maxLen) {
 if (!s) return false;
 size_t len = strlen(s);
 if (len > maxLen) return false;
 for (size_t i = 0; i < len; i++) {
 if ((unsigned char)s[i] < 32) return false;
 }
 return true;
}

/** Validate names (device, username): no control chars, no quotes/backslash, 1-31 chars. */
inline bool isValidName(const char* name, size_t maxLen = 31) {
 if (!name) return false;
 size_t len = strlen(name);
 if (len == 0 || len > maxLen) return false;
 for (size_t i = 0; i < len; i++) {
 if ((unsigned char)name[i] < 32 || name[i] == '"' || name[i] == '\\') return false;
 }
 return true;
}


/**
 * @brief Validates a filename for HTTP upload/download operations.
 *
 * Rules (rejects path traversal attacks in handleUploadData):
 * - Non-empty and len ≤ UPLOAD_FILENAME_MAX (64) chars (after stripping leading '/').
 * - No ".." sequence in any position (directory escape).
 * - No control bytes (<32, 127).
 * - No characters problematic in LittleFS/URL paths: '\' '"' ':' '<' '>' '|' '?' '*'.
 * - No '%' (blocks bypass via percent-encoding: %2e%2e%2f → "../").
 * The Arduino-Pico multipart parser does not URL-decode the filename, so
 * client-encoded chars arrive literal — without '%' in the blocklist, an
 * attacker could escape any simple char filter.
 *
 * Complements uploadDir sanitization (rejects .. via indexOf).
 * The upload.filename comes directly from the HTTP multipart client, without any
 * guarantee — ALWAYS validate before assembling finalPath.
 *
 * @param name Name from client (may start with '/'; stripped internally).
 * @return true if safe for use in LittleFS path; false otherwise.
 */
constexpr size_t UPLOAD_FILENAME_MAX = 64;
inline bool isSafeUploadFilename(const char* name) {
 if (!name) return false;
 if (name[0] == '/') name++; /* strip leading slash */
 const size_t len = strlen(name);
 if (len == 0 || len > UPLOAD_FILENAME_MAX) return false;
 if (strstr(name, "..") != nullptr) return false; /* traversal guard */
 for (size_t i = 0; i < len; i++) {
 const unsigned char c = (unsigned char)name[i];
 if (c < 32 || c == 127) return false;
 if (c == '\\' || c == '"' || c == ':' || c == '<'
 || c == '>' || c == '|' || c == '?' || c == '*'
 || c == '%') return false; /* blocks percent-encoding */
 }
 return true;
}


/** Validate IPv4 address format (e.g., "192.168.1.100"). */
inline bool isValidIpv4(const char* ip) {
 if (!ip || strlen(ip) < 7 || strlen(ip) > 15) return false;
 int parts = 0;
 int val = 0;
 bool hasDigit = false;
 for (const char* p = ip; ; p++) {
 if (*p >= '0' && *p <= '9') {
 val = val * 10 + (*p - '0');
 if (val > 255) return false;
 hasDigit = true;
 } else if (*p == '.' || *p == '\0') {
 if (!hasDigit) return false;
 parts++;
 val = 0;
 hasDigit = false;
 if (*p == '\0') break;
 } else {
 return false;
 }
 }
 return (parts == 4);
}


/** Check if a numeric value falls within [minVal, maxVal]. */
inline bool isInRange(int value, int minVal, int maxVal) {
 return (value >= minVal && value <= maxVal);
}
