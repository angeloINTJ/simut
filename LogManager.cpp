/**
 * @file    LogManager.cpp
 * @brief   Implementation of LogManager — log output, flash persistence, and crash forensics.
 * @details Implements dual-format logging (syslog-style serial + CSV flash),
 * automatic log rotation (500 lines max), ring buffer for logs during
 * heavy tasks or touch interactions, and cross-core health monitoring
 * with configurable timeout thresholds and watchdog-triggered reboot.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "LogManager.h"
#include "DisplayManager.h"   /* F-LANGPACK: logcodeLookup / trlLookup do .lng */
#include "TouchPriority.h"
#include <LittleFS.h>
#include <time.h>
#include "pico/multicore.h"
#include <hardware/watchdog.h>
#include <stdio.h>

/* Black-box profiler state — tracks per-core activity for crash forensics. */
volatile uint32_t _coreHeartbeat[2] = {0, 0};
volatile uint8_t _coreModule[2] = {0, 0};
volatile uint32_t _moduleStartTime[2] = {0, 0};
volatile bool _corePaused[2] = {false, false};
volatile uint32_t _healthCheckEnabledAt = 0;

/* Snapshot de scratch[3] do boot anterior, capturado na primeira chamada
 * de setModule (antes de sobrescrever). Usado pela autópsia para recuperar
 * o módulo ativo no momento do HW WATCHDOG mesmo após AppManager::setup
 * ter chamado `TRACE_MOD(0, MOD_BOOT)` no início. */
static volatile uint32_t _preBootScratch4 = 0;
static volatile bool     _preBootSnapshotTaken = false;

/* Guard one-shot: performCrashAutopsy() roda uma única vez por sessão.
 * begin() é chamado múltiplas vezes em runtime (clear log, web clear logs) —
 * sem este guard, a 2ª autópsia re-dispararia HW WATCHDOG porque
 * watchdog_caused_reboot() mantém true pela sessão inteira. */
static volatile bool     _autopsyPerformed = false;

const char* MOD_NAMES[] = {"BOOT", "IDLE", "WIFI", "WEB_SERVER", "STORAGE_RD", "STORAGE_WR", "SENSOR", "TELEMETRY", "DISPLAY", "CLI",
                            "SAVE_CFG", "LOG_FLASH", "HIST_FLASH", "CORE1_LOCK"};
static constexpr uint8_t MOD_NAMES_MAX = 13;  /* último índice válido */

volatile bool LogManager::_wdtActive = false;
volatile uint32_t LogManager::_wdtCtxMs = WATCHDOG_TIMEOUT_MS;

LogManager::LogManager() {
    mutex_init(&_logMutex);
    _saveToFile = false;
    _minSerialLevel = LOG_INFO;
    _currentLineCount = 0;
    _epochFn = nullptr;
}

void LogManager::setLockCallback(FlashLockCallback cb) { _lockCb = cb; }

void LogManager::setConsoleSink(ConsoleSink sink) { _consoleSink = sink; }

void LogManager::setConsoleStream(bool enabled) { _consoleStreamEnabled = enabled; }

/* Emite uma linha no console.
 * Se modo CONFIG (stream OFF): silencioso, flash continua gravando normalmente.
 * Se sink instalado (CommandManager): espelha USB+BT via consolePrintln.
 * Caso contrário: fallback em Serial direto (pré-boot, antes de _cmdMgr.begin()). */
void LogManager::emitLine(const char* line) {
    if (!_consoleStreamEnabled) return;
    if (_consoleSink) _consoleSink(line);
    else Serial.println(line);
}

void LogManager::writeConsole(const char* line) {
    /* BT streams silenciosamente descartam writes grandes (buffer ~256 B).
     * Chunka linhas longas em pedaços que cabem na janela de transmissão BT.
     * Cada chunk sai como sua própria linha — output longo vira múltiplas linhas
     * no receptor, mas nenhum byte é perdido. */
    constexpr size_t MAX_CHUNK = 200;
    const size_t len = strlen(line);
    if (len <= MAX_CHUNK) {
        if (_consoleSink) _consoleSink(line);
        else Serial.println(line);
        return;
    }

    char buf[MAX_CHUNK + 1];
    size_t off = 0;
    while (off < len) {
        const size_t n = (len - off > MAX_CHUNK) ? MAX_CHUNK : (len - off);
        memcpy(buf, line + off, n);
        buf[n] = '\0';
        if (_consoleSink) _consoleSink(buf);
        else Serial.println(buf);
        off += n;
        /* Pausa curta pra dar tempo do BT tx buffer drenar. */
        delay(2);
    }
}


void LogManager::setHeavyTaskChecker(bool (*fn)()) {
    _isHeavyTaskFn = fn;
    _heavyTaskCheckEnabled = (fn != nullptr);
}


/* REF-004: setTouchPriorityChecker removido — provider agora via TouchPriority::setProvider. */

void LogManager::setEpochSource(time_t (*fn)()) { _epochFn = fn; }

time_t LogManager::getEpochNow() {
    if (_epochFn) return _epochFn();
    time_t t = time(nullptr);
    if (t > 1600000000) return t;
    return 0;
}

String LogManager::uptimeString() {
    uint32_t sec = millis() / 1000;
    uint32_t d = sec / 86400; sec %= 86400;
    uint32_t h = sec / 3600;  sec %= 3600;
    uint32_t m = sec / 60;    sec %= 60;
    char buf[24];
    if (d > 0) snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", d, h, m, sec);
    else       snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, sec);
    return String(buf);
}


void LogManager::requestFsLock(bool lock) {
    if (_lockCb) _lockCb(lock);
    if (lock) delay(1);
}

void LogManager::setForceBuffer(bool force) {
    _forceBuffer = force;
}

void LogManager::captureBootSnapshot() {
    if (!_preBootSnapshotTaken) {
        _preBootScratch4 = watchdog_hw->scratch[3];
        _preBootSnapshotTaken = true;
    }
}

void LogManager::begin(bool saveToFile, LogLevel minSerialLevel) {
    /* Captura o módulo do boot anterior ANTES de qualquer TRACE_MOD sobrescrever
     * scratch[3]. Idempotente: chamadas subsequentes (ex: CMD_CLEAR_LOGS) são no-op. */
    captureBootSnapshot();

    _saveToFile = saveToFile;
    _minSerialLevel = minSerialLevel;

    if (_saveToFile) {
        requestFsLock(true);

        /* Migração: remove logs CSV antigos do formato anterior */
        if (LittleFS.exists("/system.log")) LittleFS.remove("/system.log");
        if (LittleFS.exists("/system.old")) LittleFS.remove("/system.old");

        if (LittleFS.exists(LOG_FILE_CURRENT)) {
            _currentLineCount = countFileRecords(LOG_FILE_CURRENT);
        } else {
            _currentLineCount = 0;
        }
        requestFsLock(false);
    }
    performCrashAutopsy();
}

void LogManager::resetAfterExternalWipe() {
    if (_saveToFile) {
        requestFsLock(true);
        if (LittleFS.exists(LOG_FILE_CURRENT)) {
            _currentLineCount = countFileRecords(LOG_FILE_CURRENT);
        } else {
            _currentLineCount = 0;
        }
        requestFsLock(false);
    }
}

int LogManager::getCoreID() { return get_core_num(); }

uint16_t LogManager::countFileRecords(const char* filename) {
    File f = LittleFS.open(filename, "r");
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return (uint16_t)(sz / LOG_RECORD_SIZE);
}

const char* LogManager::getLevelString(LogLevel level) {
    switch (level) {
        case LOG_DEBUG: return "DBG";
        case LOG_INFO:  return "INF";
        case LOG_WARN:  return "WRN";
        case LOG_ERROR: return "ERR";
        case LOG_FATAL: return "FTL";
        default:        return "---";
    }
}


/* =========================================================================== */
/*                      LOG OUTPUT — SERIAL + FLASH CSV                      */
/* =========================================================================== */
/**
 * @brief Log a structured event with code, context value, and optional message.
 * Serial format: [timestamp][UP uptime][Core][Level][Tag] [Code] Message (ctx:N)
 * Flash CSV: epoch;millis;core;level;tag;code;ctx;message
 */
void LogManager::logCode(LogLevel level, const char* tag, LogCode code, int contextVal, String extraMsg) {
    if (level < _minSerialLevel && level < LOG_WARN) return;

    /* D11: formatar em buffer dentro do mutex, Serial I/O fora */
    char serialBuf[192];
    int spos = 0;

    mutex_enter_blocking(&_logMutex);

    time_t epoch = getEpochNow();
    int core = get_core_num();

    if (epoch > 1600000000) {
        struct tm ti; localtime_r(&epoch, &ti);
        spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[BOOT+%lus]", millis()/1000);
    }

    const char* desc = translateCode((uint16_t)code);
    spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[UP %s][C%d][%s][%s] %s",
        uptimeString().c_str(), core, getLevelString(level), tag, desc);
    if (extraMsg.length() > 0) spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, ": %s", extraMsg.c_str());
    if (contextVal != 0) spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, " (%d)", contextVal);

    if (_saveToFile) {
        CompactLogRecord rec;
        rec.epoch     = (uint32_t)epoch;
        rec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
        rec.code      = (uint16_t)code;
        rec.context   = (int16_t)constrain(contextVal, -32767, 32767);
        rec.flags     = CompactLogRecord::packFlags((uint8_t)level, (uint8_t)core, tagStringToId(tag));
        rec.reserved  = 0;
        writeCompactToFlash(rec);
    }
    mutex_exit(&_logMutex);

    emitLine(serialBuf);
}


void LogManager::log(LogLevel level, const char* tag, LogCode code, String msg) {
    if (level < _minSerialLevel && level < LOG_WARN) return;

    char serialBuf[192];
    int spos = 0;

    mutex_enter_blocking(&_logMutex);

    time_t epoch = getEpochNow();
    int core = get_core_num();

    if (epoch > 1600000000) {
        struct tm ti; localtime_r(&epoch, &ti);
        spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[%02d:%02d:%02d]", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[BOOT+%lus]", millis()/1000);
    }
    spos += snprintf(serialBuf + spos, sizeof(serialBuf) - spos, "[UP %s][C%d][%s][%s] %s",
        uptimeString().c_str(), core, getLevelString(level), tag, msg.c_str());

    if (_saveToFile && level >= LOG_INFO) {
        CompactLogRecord rec;
        rec.epoch     = (uint32_t)epoch;
        rec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
        rec.code      = (uint16_t)code;
        rec.context   = 0;
        rec.flags     = CompactLogRecord::packFlags((uint8_t)level, (uint8_t)core, tagStringToId(tag));
        rec.reserved  = 0;
        writeCompactToFlash(rec);
    }
    mutex_exit(&_logMutex);

    emitLine(serialBuf);
}


/**
 * @brief Write a CSV log line to LittleFS with intelligent buffering.
 *
 * During touch interactions or heavy tasks, logs are buffered in RAM
 * instead of written to flash (which would pause Core 1). The buffer
 * is flushed automatically on the next non-critical write.
 */
void LogManager::writeCompactToFlash(const CompactLogRecord& rec) {

    /* Buffer forçado temporário (ex: login BT): evita flash + lockout
     * síncrono em paths sensíveis que não toleram latência de GC. */
    if (_forceBuffer) {
        int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
        if (idx < LOG_PENDING_MAX) {
            _pendingLogs[idx] = rec;
            __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
        } else {
            _pendingOverflow++;
        }
        return;
    }

    /* Durante interação de toque: bufferiza em RAM */
    if (TouchPriority::isActive()) {
        /* (buffer em RAM — não toca flash, sem TraceScope) */
        int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
        if (idx < LOG_PENDING_MAX) {
            _pendingLogs[idx] = rec;
            __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
        } else {
            _pendingOverflow++;
        }
        return;
    }

    /* Durante tarefa pesada: bufferiza em RAM */
    if (_heavyTaskCheckEnabled && _isHeavyTaskFn && _isHeavyTaskFn()) {
        int idx = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
        if (idx < LOG_PENDING_MAX) {
            _pendingLogs[idx] = rec;
            __atomic_store_n(&_pendingCount, idx + 1, __ATOMIC_RELEASE);
        } else {
            _pendingOverflow++;
        }
        return;
    }

    flushPendingLogs();

    /* U23: instrumentação autópsia. Se travar aqui ficamos em LOG_FLASH.
     * Se travar esperando Core 1 ackear o lockout, ficamos em CORE1_LOCK. */
    TraceScope _tr(0, MOD_LOG_FLASH);

    /* RAII context-aware: estende ctx WDT para 30s (ou mantém outer se maior).
     * Cobre requestFsLock (multicore_lockout wait) + flash ops sob GC.
     * Auto-restaura no destrutor. */
    WdtWindow _wdt(30000);
    {
        TraceScope _trLock(0, MOD_CORE1_LOCK);
        requestFsLock(true);
    }
    watchdog_update();

    if (_currentLineCount >= MAX_RECORDS_PER_FILE) {
        if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
        watchdog_update();
        if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
        watchdog_update();
        _currentLineCount = 0;

        /* Registra a rotação como primeiro entry do novo arquivo (console + flash) */
        emitLine("[LOG] Log file rotated.");
        CompactLogRecord rotRec;
        rotRec.epoch     = (uint32_t)getEpochNow();
        rotRec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
        rotRec.code      = SYS_STORAGE_ROTATE;
        rotRec.context   = MAX_RECORDS_PER_FILE;
        rotRec.flags     = CompactLogRecord::packFlags(LOG_INFO, get_core_num(), TAG_STO);
        rotRec.reserved  = 0;

        File rf = LittleFS.open(LOG_FILE_CURRENT, "a");
        watchdog_update();
        if (rf) { rf.write((const uint8_t*)&rotRec, LOG_RECORD_SIZE); rf.close(); _currentLineCount++; }
        watchdog_update();
    }

    File f = LittleFS.open(LOG_FILE_CURRENT, "a");
    watchdog_update();
    if (f) {
        f.write((const uint8_t*)&rec, LOG_RECORD_SIZE);
        f.close();
        _currentLineCount++;
    }
    watchdog_update();

    requestFsLock(false);
    /* WdtWindow destrutor auto-restaura ctx WDT */
}


/** @brief Public wrapper: flush pending logs on demand (Fase 5). */
void LogManager::flushPendingIfAny() {
    int count = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
    if (count == 0 && _pendingOverflow == 0) return;
    flushPendingLogs();
}


/** @brief Flush buffered log entries that accumulated during heavy tasks. */
void LogManager::flushPendingLogs() {
    int count = __atomic_load_n(&_pendingCount, __ATOMIC_ACQUIRE);
    if (count == 0 && _pendingOverflow == 0) return;

    TraceScope _tr(0, MOD_LOG_FLASH);

    /* RAII context-aware (igual writeCompactToFlash). */
    WdtWindow _wdt(30000);
    {
        TraceScope _trLock(0, MOD_CORE1_LOCK);
        requestFsLock(true);
    }
    watchdog_update();

    /* Batch write: abrir 1x, escrever N entries, fechar — tudo dentro do lock */
    File f = LittleFS.open(LOG_FILE_CURRENT, "a");
    watchdog_update();

    for (int i = 0; i < count; i++) {
        if (_currentLineCount >= MAX_RECORDS_PER_FILE) {
            if (f) f.close();
            if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
            watchdog_update();
            if (LittleFS.exists(LOG_FILE_CURRENT)) LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
            watchdog_update();
            _currentLineCount = 0;

            CompactLogRecord rotRec;
            rotRec.epoch     = (uint32_t)getEpochNow();
            rotRec.uptimeHr  = (uint16_t)(millis() / 3600000UL);
            rotRec.code      = SYS_STORAGE_ROTATE;
            rotRec.context   = MAX_RECORDS_PER_FILE;
            rotRec.flags     = CompactLogRecord::packFlags(LOG_INFO, get_core_num(), TAG_STO);
            rotRec.reserved  = 0;

            f = LittleFS.open(LOG_FILE_CURRENT, "a");
            if (f) { f.write((const uint8_t*)&rotRec, LOG_RECORD_SIZE); _currentLineCount++; }
            watchdog_update();
        }

        if (!f) f = LittleFS.open(LOG_FILE_CURRENT, "a");
        if (f) {
            f.write((const uint8_t*)&_pendingLogs[i], LOG_RECORD_SIZE);
            _currentLineCount++;
        }
        /* Feed a cada 8 entries para lote grande (pending max 32). */
        if ((i & 7) == 7) watchdog_update();
    }

    if (f) f.close();
    watchdog_update();

    /* Registrar overflow se houve perda de entries */
    if (_pendingOverflow > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[LOG] WARN: %u log entries dropped (buffer full)", _pendingOverflow);
        emitLine(buf);
        _pendingOverflow = 0;
    }

    requestFsLock(false);
    __atomic_store_n(&_pendingCount, 0, __ATOMIC_RELEASE);
    /* WdtWindow auto-restaura ctx WDT */
}


void LogManager::info(const char* tag, String msg)  { log(LOG_INFO,  tag, SYS_OK, msg); }
void LogManager::warn(const char* tag, String msg)  { log(LOG_WARN,  tag, SYS_OK, msg); }
void LogManager::error(const char* tag, String msg) { log(LOG_ERROR, tag, SYS_OK, msg); }
void LogManager::debug(const char* tag, String msg) { log(LOG_DEBUG, tag, SYS_OK, msg); }

void LogManager::setSaveToFile(bool enable) { _saveToFile = enable; }
void LogManager::setMinSerialLevel(LogLevel level) { _minSerialLevel = level; }


/* =========================================================================== */
/*                            BLACK BOX PROFILER                               */
/* ===========================================================================
 * SCRATCH REGISTER MAP (watchdog_hw->scratch[0..7])
 * ---------------------------------------------------------------------------
 * Os scratch registers do watchdog sobrevivem a reset por WDT e a reboot via
 * `watchdog_reboot()`, mas são zerados em power cycle / reset físico. São o
 * canal forense pós-crash deste firmware (ver performCrashAutopsy).
 *
 *   scratch[0..2] — reservados pelo SDK Pico (boot/runtime). Não tocar.
 *   scratch[3]   — trace de módulo (Core 0 + Core 1) via TRACE_MOD/setModule.
 *                  Packing:
 *                    bits  0..7  = Core 0 mod atual
 *                    bits  8..15 = 0x80 (magic "valid") se Core 0 já setou
 *                    bits 16..23 = Core 1 mod atual
 *                    bits 24..31 = 0x80 (magic "valid") se Core 1 já setou
 *                  Magic byte evita false-positive de scratch zerado.
 *   scratch[4]   — DOCUMENTADO pelo SDK como sobrescrito por
 *                  `watchdog_reboot(pc, ...)` passando PC. NÃO usar como
 *                  canal de dado — o valor passado a watchdog_reboot aparece
 *                  aqui pós-reset.
 *   scratch[5]   — DOCUMENTADO pelo SDK como sobrescrito por
 *                  `watchdog_reboot(, sp, )` passando SP, porém
 *                  empiricamente persiste através de `watchdog_reboot(0,0,0)`.
 *                  Usado como magic discriminador pela autópsia:
 *                    0xCA11B007 → SOFT PANIC (Core 1 heartbeat stuck).
 *                    0xC1EA8007 → reboot limpo (markCleanReboot).
 *                  Se uma SDK futura parar de preservar scratch[5], migrar
 *                  o magic para scratch[3] (compartilhar com trace via
 *                  reserva de um bitfield adicional).
 *   scratch[6]   — payload SOFT PANIC: (deadCore<<24)|(mod0<<16)|(mod1<<8).
 *   scratch[7]   — payload SOFT PANIC: elapsed_ms desde último heartbeat.
 * ========================================================================= */

/** @brief Set the currently executing module for crash forensics.
 *  Pré-condição: LogManager::begin() (ou captureBootSnapshot() explícito) já rodou —
 *  caso contrário o scratch[3] do boot anterior é perdido antes da autópsia. */
void LogManager::setModule(int core, uint8_t mod) {
    _coreModule[core] = mod;
    _moduleStartTime[core] = millis();
    _coreHeartbeat[core] = millis();
    /* Persiste o módulo corrente em scratch[3] (ver SCRATCH REGISTER MAP acima
     * para packing e uso na autópsia pós-HW WDT). */
    if (core == 0) {
        watchdog_hw->scratch[3] = (watchdog_hw->scratch[3] & 0xFFFF0000u) | 0x8000u | (mod & 0xFFu);
    } else if (core == 1) {
        watchdog_hw->scratch[3] = (watchdog_hw->scratch[3] & 0x0000FFFFu) | 0x80000000u | (((uint32_t)mod & 0xFFu) << 16);
    }
}

uint8_t LogManager::getModule(int core) {
    if (core < 0 || core > 1) return 0;
    return _coreModule[core];
}

void LogManager::heartbeat(int core) {
    _coreHeartbeat[core] = millis();
}

void LogManager::setCorePaused(int core, bool paused) {
    _corePaused[core] = paused;
    if (!paused) {
        uint32_t now = millis();
        _coreHeartbeat[core] = now;
        _moduleStartTime[core] = now;
    }
}

/**
 * @brief Habilita o monitoramento cross-core após boot completo.
 *
 * Deve ser chamado no final do setup(), após todos os subsistemas
 * estarem inicializados e ambos os cores estarem em operação normal.
 * Força refresh de heartbeats para evitar detecção falsa de heartbeat
 * estagnado durante o boot.
 */
void LogManager::enableHealthCheck() {
    uint32_t now = millis();
    /* Força heartbeats frescos para ambos os cores */
    _coreHeartbeat[0] = now;
    _coreHeartbeat[1] = now;
    _moduleStartTime[0] = now;
    _moduleStartTime[1] = now;
    _healthCheckEnabledAt = now;
}

/**
 * @brief Monitor the other core's heartbeat and trigger reboot if frozen.
 *
 * Grace period: skips monitoring during the first 5 seconds after boot
 * completes (_bootReady), instead of using a fixed millis() threshold.
 * This covers boots of any duration (30-60s with graph preloading).
 *
 * Phase 1 (>8s stale): Reboot with crash data in watchdog scratch registers.
 */

void LogManager::checkCrossCoreHealth() {
    uint32_t now = millis();
    int thisCore = get_core_num();
    int otherCore = (thisCore == 0) ? 1 : 0;

    /* Grace period dinâmico: desativado até 5s após habilitação explícita */
    if (_healthCheckEnabledAt == 0) return;
    if (now - _healthCheckEnabledAt < 5000) return;

    if (_corePaused[otherCore]) return;

    uint32_t lastBeat = _coreHeartbeat[otherCore];

    /* Patch C: cast signed para evitar falso-positivo em cross-core race.
     * Cenário: Core 0 lê now=T, Core 1 escreve heartbeat=T+δ (δ>0, race
     * entre os dois loads), Core 0 lê lastBeat=T+δ. Com unsigned subtract,
     * elapsed = T - (T+δ) = UINT32_MAX - δ + 1 ≈ 4e9 ms — dispara pânico falso.
     * Signed subtract (wrap-safe até 24 dias) trata δ pequeno como elapsed
     * negativo pequeno, que não entra no branch do pânico. */
    int32_t elapsed = (int32_t)(now - lastBeat);

    /* Threshold 15s: maior que o WDT normal (8.3s) e tolera rajadas onde
     * saveConfiguration estende WDT para 30s + multicore_lockout bloqueia
     * Core 1 por vários segundos cumulativos entre saves consecutivos.
     * HW WDT no Core 0 continua sendo o backstop para travas reais. */
    if (elapsed > 15000) {
        watchdog_hw->scratch[5] = 0xCA11B007;
        watchdog_hw->scratch[6] = (otherCore << 24) | (_coreModule[0] << 16) | (_coreModule[1] << 8);

        /* Patch A: guarda o elapsed real (tempo desde o último heartbeat). */
        watchdog_hw->scratch[7] = (uint32_t)elapsed;

        watchdog_reboot(0, 0, 0);
        while(1);
    }
}

void LogManager::markCleanReboot() {
    /* Arduino-Pico implementa rp2040.reboot() via watchdog_reboot, então
     * watchdog_caused_reboot() retorna true mesmo em reboot intencional.
     * Marcamos scratch[5] com magic distinto do soft panic — autópsia lê,
     * reconhece e pula a emissão de FATAL (ver SCRATCH REGISTER MAP acima). */
    watchdog_hw->scratch[5] = 0xC1EA8007;
}

void LogManager::safeReboot() {
    /* F-USB-REBOOT + Achado #5 v2 (v3.44.0-alpha9): aplica padrão fix #3
     * v3.43.21 IDÊNTICO ao applier_reboot. Esta versão é a correta —
     * alpha4 tentou mas omitiu Clear ENABLE + scratch[4]=0 (sequência
     * incorreta: LOAD antes de PSM_WDSEL, sem CLR_ENABLE_alias). Resultado:
     * regrediu loop20 pra 1/20 PASS. Análise + revert em alpha5.
     *
     * Causa raiz "reload confirm bricks 100%" do Achado #5: watchdog_enable
     * (500, 1) deixa ENABLE bit setado no watchdog ctrl. Após o reset disparar,
     * watchdog HW NÃO é resetado pelo PSM (não está em PSM_WDSEL_BITS — só
     * power cycle físico zera). LOAD=500ms persiste; ENABLE persiste. Boot
     * pós-reset tem ~150ms BootROM + boot2 + arduino-pico init ≈ 500ms+
     * — bem na janela do disparo. Watchdog fires antes de runtime chamar
     * watchdog_update → reset loop infinito até power cycle.
     *
     * Mesmo bug que applier_reboot tinha pré-fix #3. Pattern correto (igual
     * SDK pico-sdk hardware_watchdog/watchdog.c::_watchdog_enable com
     * delay_ms=0):
     *   1. PSM_WDSEL: todos peripherals exceto ROSC/XOSC (mantém PLLs)
     *   2. Clear ENABLE explícito (via CLR alias)
     *   3. scratch[4] = 0 (boot mode = normal, não stage2)
     *   4. LOAD = 0xFFFFFF (24-bit max ≈ 8s) — tempo pro firmware chegar
     *      ao primeiro watchdog_update no loop()
     *   5. TRIGGER apenas (NÃO ENABLE) — força reset imediato sem armar
     *      timer pós-reset
     *
     * Sequência completa:
     *   1. markCleanReboot — autopsy não loga FATAL no próximo boot
     *   2. Serial.flush + Serial.end — DETACH limpo USB CDC
     *   3. delay 100ms — host processa disconnect
     *   4-8. MMIO sequence acima
     *   9. while(1) — aguarda reset chegar */
    markCleanReboot();
    Serial.println("[SYS] Rebooting...");
    Serial.flush();
    delay(50);
    Serial.end();
    delay(100);

    /* Endereços MMIO RP2040 — prefixo LM_ pra evitar clash com #defines do
     * pico-sdk (hardware/regs/watchdog.h define WATCHDOG_CTRL_OFFSET etc).
     * Mesmos valores que src/ota/applier.cpp; padrão fix #3 v3.43.21. */
    constexpr uint32_t LM_WD_BASE       = 0x40058000u;
    constexpr uint32_t LM_WD_CTRL_OFF   = 0x00u;
    constexpr uint32_t LM_WD_LOAD_OFF   = 0x04u;
    constexpr uint32_t LM_WD_SCRATCH4   = 0x1Cu;
    constexpr uint32_t LM_WD_SET_ALIAS  = 0x2000u;
    constexpr uint32_t LM_WD_CLR_ALIAS  = 0x3000u;
    constexpr uint32_t LM_WD_ENABLE_BIT = (1u << 30);
    constexpr uint32_t LM_WD_TRIG_BIT   = (1u << 31);
    constexpr uint32_t LM_PSM_BASE      = 0x40010000u;
    constexpr uint32_t LM_PSM_WDSEL_OFF = 0x18u;
    constexpr uint32_t LM_PSM_BITS_ALL  = 0x0001FFFFu;
    constexpr uint32_t LM_PSM_ROSC_BIT  = 0x00000001u;
    constexpr uint32_t LM_PSM_XOSC_BIT  = 0x00000002u;
    constexpr uint32_t LM_PSM_RESET_MASK = LM_PSM_BITS_ALL & ~(LM_PSM_ROSC_BIT | LM_PSM_XOSC_BIT);

    /* (1) PSM_WDSEL: todos peripherals exceto ROSC/XOSC. */
    *(volatile uint32_t*)(LM_PSM_BASE + LM_PSM_WDSEL_OFF) = LM_PSM_RESET_MASK;

    /* (2) Clear ENABLE explícito via CLR alias — desativa o timer
     * antes do trigger pra que ele não persista pós-reset. */
    *(volatile uint32_t*)(LM_WD_BASE + LM_WD_CLR_ALIAS + LM_WD_CTRL_OFF) = LM_WD_ENABLE_BIT;

    /* (3) scratch[4] = 0 (boot mode = normal). */
    *(volatile uint32_t*)(LM_WD_BASE + LM_WD_SCRATCH4) = 0;

    /* (4) LOAD = max (24-bit = 0xFFFFFF ≈ 8s). Watchdog HW persiste pós-reset
     * (não está em PSM_WDSEL); LOAD grande dá tempo de boot completar. */
    *(volatile uint32_t*)(LM_WD_BASE + LM_WD_LOAD_OFF) = 0xFFFFFFu;

    /* (5) TRIGGER via SET alias — só TRIGGER, NÃO ENABLE. Reset imediato. */
    *(volatile uint32_t*)(LM_WD_BASE + LM_WD_SET_ALIAS + LM_WD_CTRL_OFF) = LM_WD_TRIG_BIT;

    __asm volatile("dsb");
    while (1) tight_loop_contents();
}

/**
 * @brief Analyze watchdog scratch registers after a crash-triggered reboot.
 * Logs the dead core, module, and duration of the freeze.
 */
void LogManager::performCrashAutopsy() {
    /* Idempotência: autópsia roda exatamente uma vez por sessão. Chamadas
     * subsequentes (ex: begin() re-chamado por clear log) são no-op — evita
     * falsa HW WATCHDOG, já que watchdog_caused_reboot() permanece true
     * durante toda a sessão pós-reboot. */
    if (_autopsyPerformed) return;
    _autopsyPerformed = true;

    /* Cenários possíveis:
     *  (1) scratch[5] == 0xCA11B007: soft panic nosso (Core 1 travou).
     *  (2) scratch[5] == 0xC1EA8007: reboot limpo (markCleanReboot foi chamado).
     *  (3) scratch[5] == 0xA11FA1E5: previous session was ALIVE (autopsy passed)
     *      mas terminou com wdReset — causa EXTERNA (picotool upload, hard
     *      fault, hardware reset). NÃO é crash de codigo aplicacional pois
     *      SIMUT nunca habilita WDT em operação normal (markWdtActive nunca
     *      chamado → WdtWindow é no-op). Demote para INFO.
     *  (4) watchdog_caused_reboot() && nenhum magic: WDT REASON setada por
     *      reset externo SEM passar pela autopsia anterior (raro: 1ª boot
     *      pós-pio upload se previous session crashou antes do autopsy).
     *  (5) nenhum dos acima: power cycle / botão reset. */

    /* Rede de segurança para refactors: captureBootSnapshot() deveria ter
     * rodado antes (via begin()). Se não rodou, o scratch[3] já pode ter sido
     * sobrescrito por algum setModule prévio e o modTrace abaixo será o estado
     * vivo, não o pré-boot. */
    if (!_preBootSnapshotTaken) {
        Serial.println("[LOG] performCrashAutopsy called before captureBootSnapshot!");
    }

    bool wdReset = watchdog_caused_reboot();
    uint32_t mark = watchdog_hw->scratch[5];

    if (mark == 0xCA11B007) {
        uint32_t data = watchdog_hw->scratch[6];
        uint32_t stuckTime = watchdog_hw->scratch[7];
        int deadCore = (data >> 24) & 0xFF;
        int mod0 = (data >> 16) & 0xFF;
        int mod1 = (data >> 8) & 0xFF;

        char msg[200];
        snprintf(msg, sizeof(msg), "SOFT PANIC: Core %d heartbeat stuck in [%s] for %lums. C0=[%s] C1=[%s]",
                 deadCore,
                 deadCore == 0 ? (mod0 <= MOD_NAMES_MAX ? MOD_NAMES[mod0] : "UNK") : (mod1 <= MOD_NAMES_MAX ? MOD_NAMES[mod1] : "UNK"),
                 stuckTime,
                 mod0 <= MOD_NAMES_MAX ? MOD_NAMES[mod0] : "UNK",
                 mod1 <= MOD_NAMES_MAX ? MOD_NAMES[mod1] : "UNK");

        logCode(LOG_FATAL, "SYS", SYS_BOOT, deadCore, String(msg));
        watchdog_hw->scratch[5] = 0;
    } else if (wdReset && mark == 0xC1EA8007) {
        /* Reboot intencional via markCleanReboot(). Silencioso. */
        watchdog_hw->scratch[5] = 0;
    } else if (wdReset && mark == 0xA11FA1E5) {
        /* "Alive" magic setada no fim do autopsy anterior — significa que a
         * sessao passada chegou a rodar normalmente. Como SIMUT nunca habilita
         * WDT em operacao (markWdtActive nunca chamado, WdtWindow no-op),
         * wdReset+alive = causa EXTERNA: picotool upload, hard fault, reset
         * pin (que clearia REASON, mas se chega aqui via outra path), etc.
         * NAO e crash de codigo aplicacional. */
        uint32_t modTrace = _preBootSnapshotTaken ? _preBootScratch4
                                                  : watchdog_hw->scratch[3];
        uint8_t c0Mod = (modTrace >> 0)  & 0xFF;
        uint8_t c1Mod = (modTrace >> 16) & 0xFF;
        const char* c0Name = (c0Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c0Mod] : "UNK";
        const char* c1Name = (c1Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c1Mod] : "UNK";
        char msg[200];
        snprintf(msg, sizeof(msg),
                 "Boot after external reset (likely picotool upload). C0 last=[%s] C1 last=[%s]",
                 c0Name, c1Name);
        logCode(LOG_INFO, "SYS", SYS_BOOT, 0, String(msg));
        /* Nao limpa scratch[5] aqui — o set abaixo (alive) sobrescreve. */
    } else if (wdReset) {
        /* Hardware watchdog estourou (8.3s) sem nosso soft panic ter disparado
         * e sem marca de reboot limpo. Core 0 não chamou watchdog_update()
         * — loop travado no Core 0. RAM foi zerada; contexto vem do scratch[4]
         * que setModule() atualiza em tempo real.
         * Usa o snapshot pré-boot (capturado em captureBootSnapshot(), chamado
         * por begin() antes do 1º TRACE_MOD) para ver o módulo do crash ANTERIOR. */
        uint32_t modTrace = _preBootSnapshotTaken ? _preBootScratch4
                                                  : watchdog_hw->scratch[3];
        uint8_t c0Valid = (modTrace >> 8)  & 0xFF;
        uint8_t c0Mod   = (modTrace >> 0)  & 0xFF;
        uint8_t c1Valid = (modTrace >> 24) & 0xFF;
        uint8_t c1Mod   = (modTrace >> 16) & 0xFF;

        char msg[200];
        if (c0Valid == 0x80) {
            /* Crash real: Core 0 estava ativo (chamou TRACE_MOD) e travou.
             * Trace identifica módulo que estava executando no momento. */
            const char* c0Name = (c0Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c0Mod] : "UNK";
            if (c1Valid == 0x80) {
                const char* c1Name = (c1Mod <= MOD_NAMES_MAX) ? MOD_NAMES[c1Mod] : "UNK";
                snprintf(msg, sizeof(msg),
                         "HW WATCHDOG: Core 0 loop stalled (no feed in WDT window). C0=[%s] C1=[%s] sc3=0x%08lx",
                         c0Name, c1Name, (unsigned long)modTrace);
            } else {
                snprintf(msg, sizeof(msg),
                         "HW WATCHDOG: Core 0 loop stalled (no feed in WDT window). C0=[%s] sc3=0x%08lx",
                         c0Name, (unsigned long)modTrace);
            }
            logCode(LOG_FATAL, "SYS", SYS_BOOT, 0, String(msg));
        } else {
            /* Sem trace de Core 0 (c0Valid != 0x80) → boot anterior NUNCA chamou
             * TRACE_MOD(0,...). Causa mais provavel: picotool restart pos-upload
             * usa watchdog reset como mecanismo, deixando WATCHDOG.REASON setado
             * (registro so limpa em POR/external reset, datasheet RP2040 §4.7.6).
             * Crash real pre-TRACE_MOD existe mas e' raro. Demote para INFO
             * para nao alarmar — repeats suprimidos via scratch[5] magic. */
            snprintf(msg, sizeof(msg),
                     "Boot after watchdog reset (no trace — likely post-flash by picotool; sc3=0x%08lx)",
                     (unsigned long)modTrace);
            logCode(LOG_INFO, "SYS", SYS_BOOT, 0, String(msg));
            /* scratch[5] sera setada como ALIVE no fim do autopsy. */
        }
        watchdog_hw->scratch[3] = 0;  /* Limpa pra proxima autopsia */
    } else {
        /* Power cycle / reset fisico: limpa scratch[4] para nao contaminar
         * autopsia subsequente caso o registrador tenha lixo inicial. */
        watchdog_hw->scratch[3] = 0;
    }

    /* F-WDT-ALIVE: marca a sessao corrente como "viva". No proximo boot,
     * se wdReset for true E mark for este magic, sabemos que a sessao
     * passada estava rodando normalmente quando foi interrompida — quase
     * sempre causa externa (picotool upload, reset pin via certas paths,
     * hard fault), nao crash de codigo aplicacional. markCleanReboot e
     * soft panic sobrescrevem este magic com seus proprios. */
    watchdog_hw->scratch[5] = 0xA11FA1E5;
}


/* =========================================================================== */
/*              TRANSLATION TABLE — LogCode → Human-Readable Text            */
/* =========================================================================== */
/**
 * @brief Traduz um LogCode numérico para texto legível.
 *
 * Usada pela serial (quando extraMsg é vazio) e pela API /api/logs
 * para reconstruir mensagens a partir dos registros binários compactos.
 *
 * @param  code  Código de log (LogCode enum).
 * @return Ponteiro para string constante com a descrição.
 */
static const char* translateCodeEn(uint16_t code) {
    switch ((LogCode)code) {
        /* ── System (0–9) ── */
        case SYS_OK:              return "OK";
        case SYS_BOOT:            return "System boot";
        case SYS_REBOOT_USER:     return "User-requested reboot";
        case SYS_HEAP_LOW:        return "Heap memory low";
        case SYS_UPTIME_MARK:     return "Uptime milestone";

        /* ── WiFi (10–15) ── */
        case SYS_WIFI_CONNECT:    return "WiFi connecting";
        case SYS_WIFI_DISCONNECT: return "WiFi disconnected";
        case SYS_WIFI_SCAN:       return "WiFi scanning";
        case SYS_NTP_SYNC:        return "NTP synced";
        case SYS_IP_ACQUIRED:     return "IP acquired";
        case SYS_AP_START:        return "AP mode started";

        /* ── Storage (20–25) ── */
        case SYS_STORAGE_FAIL:    return "Storage failure";
        case SYS_STORAGE_SAVE:    return "Config saved";
        case SYS_STORAGE_ROTATE:  return "Storage rotated";
        case SYS_STORAGE_FORMAT:  return "Flash formatting";
        case SYS_STORAGE_RECOVER: return "Storage recovered";
        case SYS_STORAGE_MIGRATED:return "Config migrated";

        /* ── Telemetry (30–37) ── */
        case SYS_TEL_SENT:        return "Telemetry sent";
        case SYS_TEL_FAIL:        return "Telemetry failed";
        case SYS_TEL_RETRY:       return "Telemetry retry";
        case SYS_TEL_QUEUE:       return "Telemetry queued";
        case SYS_TEL_SSL:         return "SSL cert loaded";
        case SYS_TEL_MQTT_CONN:   return "MQTT connected";
        case SYS_TEL_MQTT_DISC:   return "MQTT disconnected";
        case SYS_TEL_MQTT_PUB:    return "MQTT published";

        /* ── Sensor (100–106) ── */
        case LOG_SENSOR_REC:      return "Sensor recovered";
        case ERR_SENSOR_TIMEOUT:  return "Sensor timeout";
        case ERR_SENSOR_CHECKSUM: return "Sensor checksum error";
        case ERR_SENSOR_CRC:      return "Sensor CRC error";
        case ERR_SENSOR_RANGE:    return "Sensor out of range";
        case ERR_SENSOR_MISMATCH: return "Hardware mismatch";
        case ERR_SENSOR_MISSING:  return "Sensor missing";

        /* ── UI events (200–202) ── */
        case EVT_UI_TOUCH:        return "Touch event";
        case EVT_DISPLAY_RESTART: return "Display restarted";
        case EVT_GRAPH_RENDER:    return "Graph rendered";

        /* ── Security (300–306) ── */
        case SEC_LOGIN_SUCCESS:   return "Login success";
        case SEC_LOGIN_FAIL:      return "Login failed";
        case SEC_UNAUTHORIZED:    return "Unauthorized access";
        case SEC_CONFIG_CHANGED:  return "Config changed";
        case SEC_SESSION_EXPIRE:  return "Session expired";
        case SEC_FILE_UPLOAD:     return "File uploaded";
        case SEC_FILE_DELETE:     return "File deleted";

        /* ── App lifecycle (400–410) ── */
        case APP_DISPLAY_LAUNCHED:    return "Display launched on Core 1";
        case APP_TOUCH_CAL_INITIAL:   return "Initial touch cal saved";
        case APP_TOUCH_CAL_REQUIRED:  return "Touch calibration required";
        case APP_AP_MODE_TRIGGERED:   return "AP mode triggered by user";
        case APP_READY:               return "System ready";
        case APP_READY_AP:            return "System ready (AP mode)";
        case APP_STORAGE_CRITICAL:    return "Storage critical failure";
        case APP_SENSORS_CALIBRATED:  return "Sensors calibrated";
        case APP_NTP_CORRECTING:      return "NTP correcting timestamps";
        case APP_NTP_CORRECTED:       return "Timestamps corrected";
        case APP_CACHE_INVALIDATED:   return "Graph caches invalidated";

        /* ── App UI (440–449) ── */
        case APP_UI_THEME_CHANGED:    return "Theme changed via UI";
        case APP_UI_LANG_CHANGED:     return "Language changed via UI";
        case APP_UI_ALARM_SAVED:      return "Alarm limits saved via UI";
        case APP_UI_TOUCH_CAL_SAVED:  return "Touch cal saved to flash";
        case APP_UI_TOUCH_SENS_SAVED: return "Touch sensitivity saved";
        case APP_UI_PIN_CHANGED:      return "Display PIN changed";
        case APP_UI_SOUND_SAVED:      return "Sound settings saved";
        case APP_UI_ALARM_SILENCED:   return "Alarm silenced via UI";
        case APP_UI_ALARM_SILENCE_EXP:return "Alarm silence expired";
        case APP_UI_ALARM_DEACTIVATED:return "All alarms deactivated (RAM)";

        /* ── Alarm state (470–472) ── */
        case APP_ALARM_TRIGGERED:     return "Alarm triggered";
        case APP_ALARM_CLEARED:       return "Alarm cleared";
        case APP_ALARM_SILENCE_CANCEL:return "Alarm silence cancelled";

        /* ── Cache (480–489) ── */
        case APP_CACHE_MINMAX_FULL:   return "Min/Max cache loaded";
        case APP_CACHE_MINMAX_PARTIAL:return "Min/Max cache partial";
        case APP_CACHE_GRAPH_STARTED: return "Graph cache refresh started";
        case APP_CACHE_GRAPH_DONE:    return "Graph cache refresh done";
        case APP_CACHE_GRAPH_AMBIENT: return "Graph cache: ambient";
        case APP_CACHE_GRAPH_BOARD:   return "Graph cache: board temp";
        case APP_CACHE_PRELOAD_DONE:  return "Graph cache preload done";
        case APP_GRAPH_LOADING:       return "Graph loading";
        case APP_GRAPH_BUDGET:        return "Graph render budget exceeded";
        case APP_PRELOAD_BUDGET:      return "Preload budget exceeded";

        /* ── Safety (500–503) ── */
        case APP_DISPLAY_PAUSE_STUCK: return "Display pause stuck >5s";
        case APP_YIELD_STUCK:         return "Yield stuck >10s";
        case APP_CORE1_DEAD:          return "Core 1 dead >10s, restarting";
        case APP_FLASH_BUSY:          return "Flash busy collision";

        /* ── History (510–513) ── */
        case APP_HISTORY_SAVED:       return "History record saved";
        case APP_HEAP_REPORT:         return "Heap status report";
        case APP_HIST_NO_TIME_REF:        return "History skip: no time reference";
        case APP_HIST_TIME_REF_RECOVERED: return "History resumed: time reference acquired";

        /* ── Network extended (520–527) ── */
        case NET_DHCP_MODE:           return "DHCP mode enabled";
        case NET_STATIC_MODE:         return "Static IP mode enabled";
        case NET_STARTING:            return "WiFi manager starting";
        case NET_SSID_MISSING:        return "WiFi SSID not configured";
        case NET_PROVISIONAL_TIME:    return "Provisional time set from flash";
        case NET_CONNECT_TIMEOUT:     return "WiFi connect timeout";
        case NET_DORMANT_MODE:        return "WiFi dormant mode";
        case NET_SHOW_IP:             return "Show IP";

        /* ── Telemetry extended (540–547) ── */
        case TEL_HTTP_INIT:           return "HTTP transport initialized";
        case TEL_MQTT_INIT:           return "MQTT transport initialized";
        case TEL_MQTT_CONNECTING:     return "MQTT connecting";
        case TEL_CERT_EMPTY:          return "cert.pem empty, insecure mode";
        case TEL_CERT_READ_ERR:       return "cert.pem read error";
        case TEL_CERT_MISSING:        return "No cert.pem, insecure mode";
        case TEL_FORCE_SYNC:          return "Forcing telemetry sync";
        case TEL_BACKOFF_SUPPRESSED:  return "Retry logs suppressed";

        /* ── Storage extended (560–565) ── */
        case STO_WRITE_FAILED:        return "History write failed";
        case STO_CORRECT_BUDGET:      return "Timestamp correction budget exceeded";
        case STO_ENFORCE_BUDGET:      return "Storage limit budget exceeded";
        case STO_ENFORCE_SKIP_ACTIVE: return "Skipping active log file";
        case STO_STATS_REPORT:        return "Storage stats report";
        case STO_CONFIG_REPORT:       return "Config report";

        /* ── Web (570–574) ── */
        case WEB_SERVER_STARTED:      return "Web server started";
        case WEB_DISCONNECT_FILE:     return "Client disconnected (file)";
        case WEB_DISCONNECT_HISTORY:  return "Client disconnected (history)";
        case WEB_SCREENSHOT_ABORTED:  return "Screenshot aborted by client";
        case WEB_UPLOAD:              return "File uploaded";

        /* ── Config (580–581) ── */
        case CFG_THEME_APPLIED:       return "Theme applied";
        case CFG_THEME_NOT_FOUND:     return "Theme not found";

        /* ── CLI (585) ── */
        case CLI_UNKNOWN_CMD:         return "Unknown command";

        /* ── Sensor (590) ── */
        case SENSOR_RUNTIME_LOADED:   return "Runtime sensors loaded";

        /* ── Display (600) ── */
        case DSP_FORCE_UNPAUSE:       return "Force unpause";

        case ERR_UNKNOWN:             return "Unknown error";
        default:                      return "?";
    }
}


const char* LogManager::translateCode(uint16_t code) {
    /* F-LANGPACK: PT (e quaisquer outros) vêm do .lng via @LOGCODES.
     * Sem .lng carregado ou em EN: usa translateCodeEn inline.
     * Lookup miss (entrada não traduzida): também cai pro EN. */
    if (_language != LANG_EN) {
        const char* t = DisplayManager::logcodeLookup(code);
        if (t) return t;
    }
    return translateCodeEn(code);
}

/* F-LANGPACK: TRL via hash. Em EN ou sem .lng, retorna o literal EN
 * passado pelo caller (zero-cost). Em non-EN, faz binary search no
 * @TRL do .lng pelo hash FNV-1a do EN; miss cai pro EN também. */
const char* LogManager::tr(const char* en) const {
    if (_language == LANG_EN || !en) return en;
    const char* t = DisplayManager::trlLookup(en);
    return t ? t : en;
}
