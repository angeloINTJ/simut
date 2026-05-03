/**
 * @file    SystemDefs_Validate.h
 * @brief   Input validation helpers (EXT-003 split).
 * @details parseIntStrict, isValidCfgString, isValidName, isSafeUploadFilename,
 *          isValidIpv4, isInRange. Helpers `inline` puros, sem dependências
 *          fora de Arduino String e <string.h>. Sub-header de SystemDefs.h
 *          (facade). EXT-003 / F17 etapa 4.
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <string.h>

/** Parse uma String como int estrito (opcional '+' ou '-' + só dígitos).
 *  Retorna true se bem-formado; false se vazio, contém espaços/letras, ou só tem sinal.
 *  Diferencia "0" legítimo de entrada não-numérica (que String::toInt() silenciosamente mapeia para 0). */
inline bool parseIntStrict(const String& s, int& out) {
    if (s.length() == 0) return false;
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') {
        if (s.length() == 1) return false;  /* só sinal, inválido */
        start = 1;
    }
    for (size_t i = start; i < s.length(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    out = s.toInt();
    return true;
}

/** v3.36.1: Parse estrito de float. Aceita opcional '+'/'-' + dígitos com no
 *  máximo 1 ponto decimal (sem expoentes). Diferencia "0" / "0.0" legítimos
 *  de entrada não-numérica (que String::toFloat() silenciosamente mapeia → 0).
 *  Não aceita espaços, vírgulas decimais (locale), nem notação científica. */
inline bool parseFloatStrict(const String& s, float& out) {
    if (s.length() == 0) return false;
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') {
        if (s.length() == 1) return false;
        start = 1;
    }
    bool seenDot = false;
    bool seenDigit = false;
    for (size_t i = start; i < s.length(); i++) {
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
    out = s.toFloat();
    return true;
}

/** Valida string de config genérica: permite vazio, rejeita control chars (<32).
 *  Aceita qualquer char imprimível (incluindo " e \) porque senhas WPA2, URLs e
 *  paths legítimos podem conter esses. O parser CLI não usa escape de aspas
 *  neste contexto — o valor é lido como raw até o fim da linha.
 *  maxLen é o tamanho útil (sem contar o '\0' final do buffer de destino). */
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
 * @brief Valida um nome de arquivo para operações de upload/download HTTP.
 *
 * Regras (rejeita ataques de path traversal em `handleUploadData`):
 *   - Não-vazio e len ≤ UPLOAD_FILENAME_MAX (64) chars (após strip de '/' inicial).
 *   - Sem sequência ".." em qualquer posição (escape de diretório).
 *   - Sem bytes de controle (<32, 127).
 *   - Sem caracteres problemáticos em paths LittleFS/URL: '\' '"' ':' '<' '>' '|' '?' '*'.
 *   - Sem '%' (bloqueia bypass via percent-encoding: `%2e%2e%2f` → "../").
 *     O parser multipart do Arduino-Pico não faz URL-decode do filename, então
 *     chars encoded pelo cliente chegam literais — sem '%' na blocklist, um
 *     atacante escaparia qualquer filtro de chars simples.
 *
 * Complementa a sanitização do `uploadDir` (rejeita `..` por `indexOf`).
 * O `upload.filename` vem direto do cliente HTTP multipart, sem qualquer
 * garantia — SEMPRE validar antes de montar `finalPath`.
 *
 * @param  name  Nome vindo do cliente (pode começar com '/'; stripped internamente).
 * @return true se seguro para uso em LittleFS path; false caso contrário.
 */
constexpr size_t UPLOAD_FILENAME_MAX = 64;
inline bool isSafeUploadFilename(const char* name) {
    if (!name) return false;
    if (name[0] == '/') name++;                       /* strip leading slash */
    const size_t len = strlen(name);
    if (len == 0 || len > UPLOAD_FILENAME_MAX) return false;
    if (strstr(name, "..") != nullptr) return false;  /* traversal guard */
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == 127) return false;
        if (c == '\\' || c == '"' || c == ':' || c == '<'
            || c == '>'  || c == '|' || c == '?' || c == '*'
            || c == '%') return false;                /* bloqueia percent-encoding */
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
