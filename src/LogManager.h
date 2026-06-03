/**
 * @file LogManager.h
 * @brief System logger with flash persistence, ring buffer, and cross-core watchdog.
 * @details Singleton logger supporting multiple severity levels with both serial
 * and LittleFS CSV output. Features a pending-log ring buffer for
 * heavy task periods, touch-priority-aware buffering, and a black-box
 * profiler that tracks per-core module execution and performs crash
 * autopsies via watchdog scratch registers.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <functional>
#include "pico/mutex.h"
#include <hardware/watchdog.h>
#include "SystemDefs.h"

#define LOG_FILE_CURRENT "/system.blog"
#define LOG_FILE_OLD "/system.old.blog"
#define MAX_RECORDS_PER_FILE 800

typedef void (*FlashLockCallback)(bool);

/** Sink de saída de log no console (USB+BT) — uma linha por chamada, sem '\n'. */
typedef std::function<void(const char*)> ConsoleSink;

enum LogLevel {
 LOG_DEBUG = 0,
 LOG_INFO = 1,
 LOG_WARN = 2,
 LOG_ERROR = 3,
 LOG_FATAL = 4,
 LOG_NONE = 5
};

class LogManager {
public:
 static LogManager& instance( ) {
 static LogManager _instance;
 return _instance;
 }

 void setLockCallback(FlashLockCallback cb);
 void setHeavyTaskChecker(bool (*fn)( ));
 /* REF-004: setTouchPriorityChecker removido — ver TouchPriority.h */
 void setConsoleSink(ConsoleSink sink);
 void setConsoleStream(bool enabled); /**< false = modo CONFIG (console silencioso) */

 /** Força bufferização em RAM para todos os writes de log (writeCompactToFlash).
 * Usado para deferir flash durante operações sensíveis (ex: login BT)
 * sem competir com multicore_lockout. Flush acontece no próximo write
 * normal ou via flushPendingIfAny( ). */
 void setForceBuffer(bool force);

 /** Escreve uma linha diretamente no console (USB+BT via sink; fallback Serial).
 * Ignora `setConsoleStream(false)` — destinado a output solicitado pelo usuário
 * (ex: dump de payload via `tel dump`), não logs automáticos. */
 void writeConsole(const char* line);

 /** Marcar que o WDT do sistema está ativo (chamado pelo SIMUT.ino no primeiro
 * loop). Paths de flash write usam esse flag para decidir se podem estender
 * a janela WDT — NÃO estender durante setup evita WDT armado cedo demais. */
 static void markWdtActive( ) { _wdtActive = true; }
 static bool isWdtActive( ) { return _wdtActive; }

 /*
 * Contexto de WDT aninhado: o outer caller (ex: TelemetryManager::update
 * com 120s) define o timeout de contexto. Inner callers (writeCompactToFlash
 * com 30s durante LOG_CODE do audit) chamam setWdtCtxMs para estender,
 * depois restoreWdtCtx pra voltar ao timeout do outer — não ao default
 * curto (8.3s), que destruiria a janela do outer.
 */
 static void setWdtCtxMs(uint32_t ms) { _wdtCtxMs = ms; }
 static uint32_t getWdtCtxMs( ) { return _wdtCtxMs; }

 /*
 * RAII: estende a janela WDT para `ms` no scope de construção,
 * restaura o contexto outer no destrutor. max(outerCtx, ms) para
 * nunca reduzir janela quando aninhado dentro de outer já maior.
 * No-op se _wdtActive=false (setup).
 */
 class WdtWindow {
 public:
 explicit WdtWindow(uint32_t ms) {
 _saved = LogManager::getWdtCtxMs( );
 uint32_t target = (ms > _saved) ? ms : _saved;
 LogManager::setWdtCtxMs(target);
 if (LogManager::isWdtActive( )) watchdog_enable(target, 1);
 }
 ~WdtWindow( ) {
 LogManager::setWdtCtxMs(_saved);
 if (LogManager::isWdtActive( )) watchdog_enable(_saved, 1);
 }
 WdtWindow(const WdtWindow&) = delete;
 WdtWindow& operator=(const WdtWindow&) = delete;
 private:
 uint32_t _saved;
 };
 bool isConsoleStream( ) const { return _consoleStreamEnabled; }

 /** Captura snapshot one-shot de watchdog_hw->scratch[3] (módulo do boot anterior)
 * antes que setModule( ) o sobrescreva. Idempotente. begin( ) chama isto antes de
 * performCrashAutopsy( ); chame explicitamente se precisar rodar autópsia em outro
 * fluxo sem passar por begin( ). */
 void captureBootSnapshot( );

 void begin(bool saveToFile = false, LogLevel minSerialLevel = LOG_INFO);

 /** Reseta estado do logger após wipe externo dos arquivos de log
 * (ex: handleApiClearLogs). Re-conta registros sem re-inicializar
 * todo o sistema de log. Não re-captura boot snapshot nem re-roda
 * autópsia — apenas zera contadores e reabre handles se necessário. */
 void resetAfterExternalWipe( );


 void log(LogLevel level, const char* tag, LogCode code, String msg);
 void logCode(LogLevel level, const char* tag, LogCode code, int contextVal = 0, String extraMsg = "");

 void info(const char* tag, String msg);
 void warn(const char* tag, String msg);
 void error(const char* tag, String msg);
 void debug(const char* tag, String msg);

 void setSaveToFile(bool enable);
 void setMinSerialLevel(LogLevel level);

 const char* getLevelString(LogLevel level);
 const char* translateCode(uint16_t code);

 /** Idioma dos labels dos códigos de log. Sincronizado com cfg.displayLang. */
 void setLanguage(uint8_t lang) { _language = lang; }

 /** F-LANGPACK: seleciona string conforme idioma corrente. EN é
 * inline; non-EN faz lookup em DisplayManager::trlLookup( ) via
 * hash FNV-1a do EN. Implementação out-of-line em LogManager.cpp
 * para evitar incluir DisplayManager.h aqui. */
 const char* tr(const char* en) const;


 void setEpochSource(time_t (*fn)( ));


 static String uptimeString( );


 void setModule(int core, uint8_t mod);
 uint8_t getModule(int core);
 void heartbeat(int core);

 /** U23: RAII para TRACE_MOD — salva mod atual na construção, aplica
 * novo mod; no destructor restaura o anterior. Permite instrumentar
 * funções internas (saveConfiguration, writeCompactToFlash) sem
 * "vazar" o módulo pro resto do handler caller. */
 class TraceScope {
 public:
 explicit TraceScope(int core, uint8_t newMod) : _core(core) {
 _saved = LogManager::instance( ).getModule(core);
 LogManager::instance( ).setModule(core, newMod);
 }
 ~TraceScope( ) { LogManager::instance( ).setModule(_core, _saved); }
 TraceScope(const TraceScope&) = delete;
 TraceScope& operator=(const TraceScope&) = delete;
 private:
 int _core;
 uint8_t _saved;
 };
 void checkCrossCoreHealth( );
 void enableHealthCheck( ); /**< Habilita o monitoramento cross-core (chamar após boot) */
 void performCrashAutopsy( );
 void setCorePaused(int core, bool paused);
 void markCleanReboot( ); /**< Chamar ANTES de rp2040.reboot( ) para não disparar autópsia HW WDT */

 /** F-USB-REBOOT: reboot defensivo que dá tempo do USB CDC desconectar
 * limpo no host. Resolve "ttyACM0 não reaparece após reload" no Linux,
 * causado por watchdog_reboot(0,0,10) interromper o USB no meio do
 * envio. Faz: markCleanReboot + Serial.flush + Serial.end + delays +
 * watchdog_enable(500ms). NÃO retorna. */
 [[noreturn]] void safeReboot( );

 /** flush imediato de logs pendentes bufferizados durante touch
 * priority. AppManager chama logo após `isUserInteracting( )` transicionar
 * para false, pra fechar a janela "dado em RAM, não em flash". No-op
 * se não há logs pendentes. */
 void flushPendingIfAny( );

private:
 LogManager( );

 static volatile bool _wdtActive;
 static volatile uint32_t _wdtCtxMs;

 mutex_t _logMutex;
 bool _saveToFile;
 LogLevel _minSerialLevel;
 uint16_t _currentLineCount;

 FlashLockCallback _lockCb = nullptr;
 ConsoleSink _consoleSink = nullptr;
 bool _consoleStreamEnabled = true; /**< true durante boot; AppManager aplica preferência do user após load */
 uint8_t _language = LANG_EN; /**< idioma dos labels de log (translateCode) */
 void emitLine(const char* line);
 void requestFsLock(bool lock);


 static const int LOG_PENDING_MAX = 32;
 CompactLogRecord _pendingLogs[LOG_PENDING_MAX];
 volatile int _pendingCount = 0;
 uint16_t _pendingOverflow = 0;
 bool _heavyTaskCheckEnabled = false;
 bool _forceBuffer = false; /**< Buffer forçado temporário (ex: login BT) */


 bool (*_isHeavyTaskFn)( ) = nullptr;

 /* REF-004: _isTouchPriorityFn removido — usa TouchPriority::isActive( ) */

 void writeCompactToFlash(const CompactLogRecord& rec);
 void flushPendingLogs( );
 int getCoreID( );

 uint16_t countFileRecords(const char* filename);

 time_t (*_epochFn)( ) = nullptr;
 time_t getEpochNow( );
};


#define LOG_DBG(tag, msg) LogManager::instance( ).debug(tag, msg)
#define LOG_INF(tag, msg) LogManager::instance( ).info(tag, msg)
#define LOG_WRN(tag, msg) LogManager::instance( ).warn(tag, msg)
#define LOG_ERR(tag, msg) LogManager::instance( ).error(tag, msg)
#define LOG_CODE(lvl, tag, code, ctx, msg) LogManager::instance( ).logCode(lvl, tag, code, ctx, msg)

/** F-LANGPACK: seleciona EN inline ou tradução do .lng (via hash). */
#define TRL(en) (LogManager::instance( ).tr(en))

#define TRACE_MOD(core, mod) LogManager::instance( ).setModule(core, mod)
#define TRACE_BEAT(core) LogManager::instance( ).heartbeat(core)

/** Feed hardware watchdog + trace heartbeat on Core 0 (PER-001).
 * Substitui a dupla watchdog_update( ); TRACE_BEAT(0); em paths críticos. */
inline void feedWdt( ) { watchdog_update( ); TRACE_BEAT(0); }
