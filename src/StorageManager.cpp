/**
 * @file StorageManager.cpp
 * @brief Implementation of StorageManager — config I/O, history logging, and flash locks.
 * @details Implements atomic config save (tmp→rename), storage limit enforcement
 * with budget-limited cleanup, provisional timestamp correction across
 * history files, HMAC-SHA256 password hashing with board serial pepper,
 * and calibration CSV parsing.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "StorageManager.h"
#include "ParseFloat.h"
#include "TouchPriority.h"
#include <hardware/uart.h>
#include "ota/metadata.h"
#include "ota/config_snapshot.h"
#include <time.h>
#include <algorithm>
#include "LogManager.h"
#include "MetricsManager.h"
#include "ConcurrencyAsserts.h" /* Wave 2: invariant-3 tripwire (opt-in) */
#include "pico/unique_id.h"
#include <hardware/watchdog.h>
#include <stdio.h>
#include <new>
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_hmac.h>

const uint32_t CONFIG_MAGIC = 0xCAFEBABE;

/* Chunked flash operation wrapper.
 * Acquires the FS mutex with timeout + watchdog feed to prevent
 * main-loop stalls when a long-running read (e.g. web API history)
 * holds the mutex. After 5 seconds of waiting, gives up silently —
 * the write will be retried on the next history interval.
 * WARNING: FLASH_OP does NOT pause Core 1 — and neither does LittleFS
 * (arduino-pico's idleOtherCore( ) is a no-op without setup1/loop1;
 * there is no flash_safe_execute in its write path). Any FLASH_OP whose
 * BLOCK programs/erases flash MUST be preceded by Core1FlashPause (or
 * run inside quiet mode), or Core 1's XIP fetches wedge the QSPI. */
#define FLASH_OP(BLOCK) do { \
 SIMUT_ASSERT_NO_STATE_MUTEX(); /* Wave 2: invariant 3 (no-op unless -DSIMUT_CONCURRENCY_ASSERTS) */ \
 uint32_t _fopStart = millis( ); \
 while (!mutex_enter_timeout_ms(&_fsReadMutex, 100)) { \
  watchdog_update( ); \
  if (timeSince(_fopStart, 5000)) break; \
 } \
 if (timeSince(_fopStart, 5000)) { \
  LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "fs_mutex_timeout"); \
 } else { \
  watchdog_update( ); \
  uint32_t _fopT1 = millis( ); \
  BLOCK; \
  /* T0.1 stability metrics: op duration is the observable proxy for \
   * the IRQ-off windows of flash program/erase (see \
   * docs/CONCURRENCY.md). >50 ms ~= one 4 KB erase reached. */ \
  { \
   uint32_t _fopDur = millis( ) - _fopT1; \
   SystemMetrics& _fm = MetricsManager::instance( ).data( ); \
   _fm.flashOps++; \
   _fm.flashOpTotalMs += _fopDur; \
   if (_fopDur > _fm.flashOpMaxMs) _fm.flashOpMaxMs = _fopDur; \
   if (_fopDur > 50) _fm.flashOpsOver50ms++; \
  } \
  watchdog_update( ); \
  mutex_exit(&_fsReadMutex); \
 } \
} while (0)

/* RAII: pause Core 1 rendering across flash program/erase bursts.
 * The FLASH_OP comment above used to claim LittleFS handles the
 * multicore lockout — it does NOT: arduino-pico's idleOtherCore( ) is
 * a no-op without setup1/loop1, so flash_range_program/erase would run
 * with Core 1 executing from XIP, wedging the QSPI arbiter. Core 0
 * then spins IRQs-off inside the flash op, the WDT never gets fed and
 * the HW watchdog fires (autopsy: C0=[HIST_FLASH] C1=[DISPLAY]).
 * Same protection the LogManager flush path takes via requestFsLock:
 * refcounted pauseRendering, no-op while in quiet mode. */
struct Core1FlashPause {
 StorageManager* _s;
 explicit Core1FlashPause(StorageManager* s) : _s(s) { _s->enterFlashSafeMode( ); }
 ~Core1FlashPause( ) { _s->exitFlashSafeMode( ); }
};

/* 20, not 18: the jump is a marker. 17 was the last schema with a migration
 * path into it, and 2.0.0 accepts nothing older than itself, so a version in
 * 18..19 would look like a routine step that some future reader might try to
 * migrate from. There is no such path and there is not meant to be one. */
const uint16_t CONFIG_VERSION = 20;

/* -------------------------------------------------------------------------- */
/* Legacy UserAccount layout (v14 and earlier) — used ONLY by the */

/* Keystream derivation via SHA-256(chip_id + domain + counter).
 * Generates arbitrary-size keystream iterating the counter and expanding
 * the hash. Equivalent to HKDF-Expand in construction and security. */
static void deriveKeystream(uint8_t* out, size_t outLen, const char* domain) {
 pico_unique_board_id_t board_id;
 pico_get_unique_board_id(&board_id);

 size_t generated = 0;
 uint32_t counter = 0;
 while (generated < outLen) {
 br_sha256_context ctx;
 br_sha256_init(&ctx);
 br_sha256_update(&ctx, board_id.id, sizeof(board_id.id));
 br_sha256_update(&ctx, ":", 1);
 br_sha256_update(&ctx, domain, strlen(domain));
 br_sha256_update(&ctx, ":", 1);
 br_sha256_update(&ctx, &counter, sizeof(counter));
 uint8_t hash[32];
 br_sha256_out(&ctx, hash);
 size_t take = (outLen - generated < 32) ? (outLen - generated) : 32;
 memcpy(out + generated, hash, take);
 generated += take;
 counter++;
 }
}

/* Applies XOR in-place to a buffer with keystream derived from the (chip_id, domain) pair.
 * Since XOR is involutive, the same function encrypts and decrypts. */
static void xorWithDerivedKey(uint8_t* buf, size_t len, const char* domain) {
 uint8_t keystream[64]; /* current max size is telApiKey[64] */
 if (len > sizeof(keystream)) return; /* protection against future overrun */
 deriveKeystream(keystream, len, domain);
 for (size_t i = 0; i < len; i++) buf[i] ^= keystream[i];
}

void StorageManager::obfuscateSensitiveFields(SystemConfig& cfg) {
 xorWithDerivedKey((uint8_t*)cfg.wifiPass, sizeof(cfg.wifiPass), "wifi");
 xorWithDerivedKey((uint8_t*)cfg.mqttPass, sizeof(cfg.mqttPass), "mqtt");
 xorWithDerivedKey((uint8_t*)cfg.telApiKey, sizeof(cfg.telApiKey), "telapi");
}

StorageManager::StorageManager( ) {
 mutex_init(&_fsReadMutex);
 loadDefaults( );
}


/* =========================================================================== */
/* FLASH READ LOCK (LIGHTWEIGHT — NO CORE 1 PAUSE) */
/* =========================================================================== */
/**
 * @brief Acquire lightweight read lock for LittleFS operations.
 * Uses a mutex to serialize filesystem reads without pausing Core 1.
 * The RP2040 flash is accessed via QSPI (not SPI0/SPI1), so there
 * is no bus conflict between flash reads and display SPI traffic.
 */
/* Owner breadcrumbs for the read lock. Written by the acquirer, read by a
 * starving acquirer's log line — the difference between "the device rebooted
 * again" and the name of the frame that never let go. */
static volatile uint8_t  g_fsLockOwnerMod = 0xFF;
static volatile uint32_t g_fsLockSinceMs  = 0;

void StorageManager::enterFlashReadLock( ) {
 /* Bounded, FED acquisition. mutex_enter_blocking parked here with the
  * watchdog starving whenever a holder never released — measured on the
  * bench as hp=301: the V5 block-load's guard against a lock nobody
  * alive was holding, HW WDT 8.4 s later. A fed spin turns that reboot
  * into a diagnosable stall; at 5 s the log names the owner; at 10 s
  * the owner is declared a corpse and the mutex is re-initialized — the
  * same judgement requestQuietMode already applies to _stateMutex at
  * kill time, applied here at starvation time. A same-core recursive
  * acquisition (the light-yield reaching back in — see the note in
  * handleApiHistoryDays) lands in the corpse path too: ugly, logged,
  * and alive beats silent and rebooting. */
 uint32_t t0 = millis( );
 bool logged = false;
 while (!mutex_enter_timeout_ms(&_fsReadMutex, 100)) {
 watchdog_update( );
 if (!logged && timeSince(t0, 5000)) {
 logged = true;
 LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, (int)g_fsLockOwnerMod,
          "fsReadMutex starving; ctx=owner module");
 }
 if (timeSince(t0, 10000)) {
 LOG_CODE(LOG_ERROR, "STO", STO_WRITE_FAILED, (int)g_fsLockOwnerMod,
          "fsReadMutex owner presumed dead — reinit");
 mutex_init(&_fsReadMutex);
 mutex_enter_blocking(&_fsReadMutex);
 break;
 }
 }
 g_fsLockOwnerMod = LogManager::instance( ).getModule(0);
 g_fsLockSinceMs  = millis( );
}

void StorageManager::exitFlashReadLock( ) {
 g_fsLockOwnerMod = 0xFF;
 mutex_exit(&_fsReadMutex);
}

/* timeout variant — caller decides whether to abort. */
bool StorageManager::enterFlashReadLockTimeout(uint32_t timeout_ms) {
 return mutex_enter_timeout_ms(&_fsReadMutex, timeout_ms);
}


/* =========================================================================== */
/* FLASH SAFE MODE (HEAVY — PAUSES CORE 1) */
/* =========================================================================== */
/**
 * @brief Acquire exclusive flash access for write/delete operations.
 * Pauses Core 1 via multicore_lockout to protect XIP during erase/program.
 * NEVER use for read-only operations — use enterFlashReadLock( ) instead.
 */
void StorageManager::enterFlashSafeMode( ) {
 /* If we're inside saveConfiguration with quiet mode
 * active, Core 1 is already frozen in a RAM-only loop — skip the
 * IRQ-based lockout to avoid stucks/cascades. */
 if (_inBigSave) return;
 if (_lockCb) _lockCb(true);
}


void StorageManager::exitFlashSafeMode( ) {
 if (_inBigSave) return;
 if (_lockCb) _lockCb(false);
}


bool StorageManager::lockHeavyTask( ) { bool expected = false; return __atomic_compare_exchange_n(&_heavyTaskLocked, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED); }
void StorageManager::unlockHeavyTask( ) { __atomic_store_n(&_heavyTaskLocked, false, __ATOMIC_RELEASE); }


bool StorageManager::isHeavyTaskLocked( ) const { return __atomic_load_n(&_heavyTaskLocked, __ATOMIC_ACQUIRE); }

/* Filesystem manual dropped at the root.
 *
 * Lives in the firmware rather than in data/ because it has to survive a
 * `system format` and reappear on the next boot — and because publishing it
 * any other way would mean `uploadfs`, which reformats the partition and
 * takes /history and calib.csv with it.
 *
 * Keep it accurate: a wrong map is worse than no map. Every path below is
 * the one the firmware really uses. */
static const char FS_README_TEXT[] PROGMEM =
"SIMUT - mapa do sistema de arquivos (LittleFS)\n"
"\n"
"Escrito pelo firmware. Nao aparece com caixa de selecao em /files e nao\n"
"pode ser apagado por la; se sumir, volta no proximo boot.\n"
"\n"
"/config/        Configuracao do sistema. Nao editar a mao.\n"
"  system.bin    Config ativa (binaria, com CRC32).\n"
"  system.bak    Copia de seguranca, usada se a ativa corromper.\n"
"  system.tmp    Temporario de escrita; so sobra apos queda de energia.\n"
"  t_cursor.bin  Ate onde a telemetria ja enviou.\n"
"\n"
"/history/       Historico de medicoes, um arquivo por dia.\n"
"  AAAAMMDD.h5   Formato V5, comprimido e autodescritivo. O arquivo comeca\n"
"                por um chunk SCHEMA que diz quais canais existem, que\n"
"                grandeza cada um mede e em que escala; depois vem um\n"
"                bloco por hora, cada um com CRC e com o minimo/maximo de\n"
"                cada canal no proprio cabecalho.\n"
"                Trocar sensores no meio do dia NAO custa mais o resto do\n"
"                dia: grava-se um SCHEMA novo no mesmo arquivo e o que ja\n"
"                estava la continua legivel.\n"
"                Para ler no computador: tools/history_v5.py --dump-csv\n"
"  .wip          Instantaneo do bloco ainda aberto na RAM, regravado a\n"
"                cada 10 min. E adotado no boot seguinte apos queda de\n"
"                energia; se estiver corrompido, e descartado.\n"
"\n"
"/lang/          Pacotes de idioma (.lng), um por idioma. Suba por /files.\n"
"                Nunca por 'uploadfs': aquilo reformata a particao.\n"
"/themes/        Temas personalizados (.thm). Opcional.\n"
"/web/           Paginas servidas do disco em vez do firmware. Em geral\n"
"                vazia; so tem conteudo se alguma pagina foi movida para\n"
"                ca por falta de espaco no firmware.\n"
"\n"
"/calib.csv      Offsets de calibracao. DS18B20 e indexado pela ROM;\n"
"                sensor sem ROM (DHT22, BMP280) vai pelo numero de serie\n"
"                da placa mais o hwId, em linhas t<hwId> e u<hwId>.\n"
"/cert.pem       Certificado TLS da telemetria. Opcional.\n"
"/system.blog    Log de eventos (binario). O .old.blog e o anterior.\n"
"\n"
"Espaco: mantenha o uso abaixo de 86%. Acima disso o firmware comeca a\n"
"apagar os arquivos de historico mais antigos para abrir espaco.\n";

/* Written only when missing or stale (size differs), never on every boot —
 * a flash write per boot is exactly the exposure the stability work spent
 * months reducing.
 *
 * Unwrapped on purpose. Boot-time README writes were removed from here once
 * because they sat inside enterFlashSafeMode and deadlocked: LittleFS already
 * takes multicore_lockout internally, so wrapping it re-enters. The mkdirs
 * below run raw for the same reason; this follows them. */
/* One-line notes that also keep their folder ALIVE.
 *
 * LittleFS drops a directory with no entries from the parent listing, so an
 * empty /themes simply did not exist as far as /files was concerned — and a
 * folder you cannot see is a folder you cannot upload into. That was the
 * reported symptom: no way to add a .thm because there was nowhere to put it.
 *
 * handleApiMkdir already used this trick for user-created folders; the system
 * ones just never got it, because the boot-time version was removed years ago
 * over the flash-safe-mode deadlock (see begin( )).
 *
 * They are protected from deletion for the same reason: without that, removing
 * your last theme takes the folder with it. */
struct FsDirNote { const char* path; const char* text; };
static const FsDirNote FS_DIR_NOTES[] = {
 { DIR_THEMES, "Arquivos de tema (.thm), um por tema. Suba por /files.\n" },
 { DIR_WEB,    "Paginas servidas do disco em vez do firmware. Normalmente vazia.\n" },
 { DIR_LANG,   "Pacotes de idioma (.lng), um por idioma. Suba por /files.\n" },
};

/* Writes only when missing or when the size no longer matches, so the common
 * boot performs zero flash writes. */
static void writeIfStale(const char* path, const char* text, bool progmem) {
 const size_t want = progmem ? strlen_P(text) : strlen(text);
 if (LittleFS.exists(path)) {
 File probe = LittleFS.open(path, "r");
 if (probe) {
 size_t have = probe.size( );
 probe.close( );
 if (have == want) return;
 }
 }
 File f = LittleFS.open(path, "w");
 if (!f) return; /* Non-fatal: it is documentation, not state. */
 /* Copied through a small stack buffer — the root manual is ~1.8 KB of
  * PROGMEM and print() would want it contiguous in RAM. */
 char chunk[128];
 size_t off = 0;
 while (off < want) {
 size_t n = want - off;
 if (n > sizeof(chunk) - 1) n = sizeof(chunk) - 1;
 if (progmem) memcpy_P(chunk, text + off, n);
 else         memcpy(chunk, text + off, n);
 chunk[n] = '\0';
 f.write((const uint8_t*)chunk, n);
 off += n;
 }
 f.close( );
}

void StorageManager::ensureFsReadme( ) {
 writeIfStale(FILE_FS_README, FS_README_TEXT, true);

 for (size_t i = 0; i < sizeof(FS_DIR_NOTES) / sizeof(FS_DIR_NOTES[0]); i++) {
 char p[48];
 snprintf(p, sizeof(p), "%s/%s", FS_DIR_NOTES[i].path, FS_DIR_NOTE_NAME);
 writeIfStale(p, FS_DIR_NOTES[i].text, false);
 }
}

bool StorageManager::begin( ) {
 /* REMOVED enterFlashSafeMode wrap of mkdirs.
 * LittleFS internally already uses flash_safe_execute → multicore_lockout. Wrap
 * external creates reentrant deadlock. UART markers showed boot
 * hanging consistently in '012' (post-enterFSM, before mkdir).
 * LFS protects its own writes — do not wrap. */
 uart_putc_raw(uart1, '0');
 if (!mountFS( )) return false;
 uart_putc_raw(uart1, '1');
 if (!LittleFS.exists(DIR_CONFIG)) LittleFS.mkdir(DIR_CONFIG);
 uart_putc_raw(uart1, '3');
 if (!LittleFS.exists(DIR_HISTORY)) LittleFS.mkdir(DIR_HISTORY);
 uart_putc_raw(uart1, '4');
 if (!LittleFS.exists(DIR_LANG)) LittleFS.mkdir(DIR_LANG);
 uart_putc_raw(uart1, '6');
 /* /themes and /web were never created here — only handleApiMkdir made them,
  * and only if the user thought to. Combined with LittleFS hiding empty
  * directories, /themes was invisible in /files on a fresh filesystem, so
  * there was no folder to upload a .thm into. */
 if (!LittleFS.exists(DIR_THEMES)) LittleFS.mkdir(DIR_THEMES);
 if (!LittleFS.exists(DIR_WEB)) LittleFS.mkdir(DIR_WEB);
 uart_putc_raw(uart1, '7');
 ensureFsReadme( );
 uart_putc_raw(uart1, '8');
 /* Removed boot-time README.md placeholders for sysDirs.
 * The writes inside enterFlashSafeMode caused a reentrant deadlock
 * (LittleFS.open+write+close internally already use
 * flash_safe_execute → multicore_lockout, and we were already in another
 * lockout via enterFlashSafeMode).
 *
 * Trade-off: /lang and /themes are invisible in /api/ls root
 * while empty. handleApiLs lists via dir.next( ) —
 * empty dirs disappear from listing. /config has system.bin and /history
 * has logs, so they're automatically visible. handleApiMkdir
 * still creates README.md in custom dirs — that path is
 * outside wrap and safe. */

 /* Config restore after OTA apply.
 *
 * If metadata.state == APPLYING (real apply, not test stub) and there is a
 * CRC-valid snapshot in the metadata partition, restore `system.bin`
 * BEFORE loadConfiguration — so the pre-apply config
 * is loaded normally instead of falling to loadDefaults.
 *
 * Restore failure is non-fatal: loadConfiguration will fail to
 * find the file, saveConfiguration writes defaults, device boots
 * in factory mode (user restores via .bkp downloaded by browser).
 *
 * Metadata partition cleanup continues in AppManager_Boot.cpp
 * (existing path) — kept idempotent here.
 *
 * Do NOT wrap in enterFlashSafeMode. LittleFS.write internally already
 * uses multicore_lockout via flash_safe_execute; wrapping in another
 * lockout creates reentrant deadlock (Core 0 holding lockout +
 * LittleFS trying to obtain again). LFS handles protection alone.
 */
 {
 uart_putc_raw(uart1, '7');
 ota::UpdateMetadata m;
 if (ota::ota_metadata_read(m) &&
 m.state == ota::STATE_APPLYING &&
 ota::ota_snapshot_present( )) {
 uart_putc_raw(uart1, '8');
 (void)ota::ota_snapshot_restore_to_lfs( );
 uart_putc_raw(uart1, '9');
 }
 uart_putc_raw(uart1, 'A');
 }

 uart_putc_raw(uart1, 'B');
 if (!loadConfiguration( )) {
 uart_putc_raw(uart1, 'C');
 saveConfiguration( );
 uart_putc_raw(uart1, 'D');
 }
 uart_putc_raw(uart1, 'E');
 return true;
}

bool StorageManager::mountFS( ) {
 if (LittleFS.begin( )) { _isMounted = true; return true; }
 LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_FORMAT, 0, TRL("Formatting Flash FS..."));
 enterFlashSafeMode( );
 bool formatted = LittleFS.format( );
 if (formatted) { bool mounted = LittleFS.begin( ); exitFlashSafeMode( ); _isMounted = mounted; return mounted; }
 exitFlashSafeMode( );
 return false;
}

void StorageManager::update( ) {
 /* T1.4 maintenance slice (stability wave 1): drain deferred storage
  * cleanup one file at a time, at least 15 s apart, instead of the old
  * 4 s deletion burst. Runs from the Core-0 loop; enforceStorageLimit
  * caps itself at 2 deletions and re-arms _cleanupPending as needed. */
 if (_cleanupPending && _isMounted && !TouchPriority::isActive( )) {
  static uint32_t _lastCleanupSlice = 0;
  if (timeSince(_lastCleanupSlice, 15000)) {
   _lastCleanupSlice = millis( );
   Core1FlashPause _c1(this); /* file deletion = erase burst */
   FLASH_OP(enforceStorageLimit( ));
  }
 }

 /* V5: the open block is snapshotted to /history/.wip once per record, from
  * writeHistoryEntryV5 itself. What used to be here — the age-out drain of
  * the V4 RAM batch — has no batch to drain. */
}

void StorageManager::loadDefaults( ) {
 memset(&_currentConfig, 0, sizeof(SystemConfig));

 _currentConfig.magic = CONFIG_MAGIC;
 _currentConfig.version = CONFIG_VERSION;
 safeCopy(_currentConfig.deviceName, "simut", sizeof(_currentConfig.deviceName));

 safeCopy(_currentConfig.wifiSsid, "", sizeof(_currentConfig.wifiSsid));
 safeCopy(_currentConfig.wifiPass, "", sizeof(_currentConfig.wifiPass));
 _currentConfig.useDhcp = true;
 safeCopy(_currentConfig.staticIp, "192.168.1.100", sizeof(_currentConfig.staticIp));
 safeCopy(_currentConfig.staticMask, "255.255.255.0", sizeof(_currentConfig.staticMask));
 safeCopy(_currentConfig.staticGateway, "192.168.1.1", sizeof(_currentConfig.staticGateway));
 safeCopy(_currentConfig.staticDns, "8.8.8.8", sizeof(_currentConfig.staticDns));
 _currentConfig.useHttps = false;

 for(int i = 0; i < MAX_USERS; i++) _currentConfig.users[i].active = false;

 _currentConfig.users[0].active = true;
 safeCopy(_currentConfig.users[0].username, "admin", sizeof(_currentConfig.users[0].username));

 /* Generate random admin password instead of hardcoded "admin".
 * SHA-256 hash of "admin" (`8c6976e5...a918`) is public in rainbow tables,
 * exposing the window between factory boot and mandatory password change.
 * Plaintext goes to RAM only; Serial+display show it for those with
 * physical access; `mustChangePassword=true` forces change on 1st web login. */
 generateInitialAdminPassword(_initialAdminPassword, sizeof(_initialAdminPassword));

 /* Frontend JS sends SHA256(plaintext) as `pass`, so the persisted hash
 * must be `hashPassword(user, SHA256(plaintext))`.
 * Factory defaults use v1 format (random salt,
 * PASSWORD_HMAC_ROUNDS rounds, 32 hex chars / 128 bits). */
 String preHash = sha256Hex(String(_initialAdminPassword));
 generateSalt(_currentConfig.users[0].salt);
 String defaultAdminHash = hashPasswordV1("admin", preHash, _currentConfig.users[0].salt);
 safeCopy(_currentConfig.users[0].password, defaultAdminHash.c_str( ), sizeof(_currentConfig.users[0].password));
 _currentConfig.users[0].permissions = PERM_FULL_ADMIN;
 _currentConfig.users[0].mustChangePassword = true;
 _currentConfig.users[0].hashVersion = 1;

 _currentConfig.users[1].active = true;
 safeCopy(_currentConfig.users[1].username, "viewer", sizeof(_currentConfig.users[1].username));
 generateSalt(_currentConfig.users[1].salt);
 String defaultViewerHash = hashPasswordV1("viewer", "0b58331da2913b41e21b7b04938632e1858a729e28cf6914b4334380f339b6f1", _currentConfig.users[1].salt);
 safeCopy(_currentConfig.users[1].password, defaultViewerHash.c_str( ), sizeof(_currentConfig.users[1].password));
 _currentConfig.users[1].permissions = (PERM_DASHBOARD | PERM_HISTORY);
 _currentConfig.users[1].mustChangePassword = true;
 _currentConfig.users[1].hashVersion = 1;

 safeCopy(_currentConfig.telServer, "", sizeof(_currentConfig.telServer));
 _currentConfig.telPort = 80;
 safeCopy(_currentConfig.telPath, "/api.php", sizeof(_currentConfig.telPath));
 safeCopy(_currentConfig.telApiKey, "", sizeof(_currentConfig.telApiKey));
 _currentConfig.telInterval = 0;
 _currentConfig.telBatchSize = 10;
 _currentConfig.telEncryption = false;
 _currentConfig.telMode = TEL_MODE_JSON;

 safeCopy(_currentConfig.telGlobalTemplate, "{\"dev\":\"{DEV}\",\"mac\":\"{MAC}\",\"data\":[{DATA}]}", sizeof(_currentConfig.telGlobalTemplate));
 /* Slot 0, keyed by its own hwId. The old default was
  * {"ts":{TS},"tAmb":{tAMB},"hAmb":{uAMB}} — both AMB tokens read record
  * columns nothing had written since V4, so a device shipped on factory
  * defaults published nothing but a timestamp. The compound "<k>_ID":{<k>}
  * form drops the whole field when the slot has no reading, so this is
  * safe on a board where slot 0 is empty or has no humidity. */
 safeCopy(_currentConfig.telLineTemplate, "{\"ts\":{TS},\"t0_ID\":{t0},\"u0_ID\":{u0}}", sizeof(_currentConfig.telLineTemplate));
 safeCopy(_currentConfig.telLineSeparator, ",", sizeof(_currentConfig.telLineSeparator));

 _currentConfig.telTransport = TEL_TRANSPORT_HTTP;
 safeCopy(_currentConfig.mqttTopic, "simut/data", sizeof(_currentConfig.mqttTopic));
 safeCopy(_currentConfig.mqttUser, "", sizeof(_currentConfig.mqttUser));
 safeCopy(_currentConfig.mqttPass, "", sizeof(_currentConfig.mqttPass));
 _currentConfig.mqttQos = 0;
 _currentConfig.mqttRetain = false;
 safeCopy(_currentConfig.mqttClientId, "", sizeof(_currentConfig.mqttClientId));
 _currentConfig.mqttKeepAlive = 60;

 _currentConfig.timezoneOffset = -3;
 _currentConfig.sampleIntervalMs = 2000;
 _currentConfig.loggingEnabled = true;
 #if SIMUT_SENSOR_DS18B20
 _currentConfig.ds18Resolution = 12;
#endif
 _currentConfig.themeIndex = 0;

 safeCopy(_currentConfig.displayPin, "1234", sizeof(_currentConfig.displayPin));
 /* Force change of default PIN "1234" on first access to the
 * config menu. Overlay in reserved[26..27] — cleared when user
 * saves a PIN != "1234". Set here (loadDefaults = factory reset). */
 setMustChangePin( );
 _currentConfig.displayLang = LANG_PT;

 /* Initialize NetworkTimeData overlay via helper (magic
 * still 0 after initial memset → helper populates defaults). */
 (void)ensureNetworkTimeOverlay( );

 /* 16 universal slots, ALL empty. No slot is pre-provisioned and no GPIO is
  * claimed: a factory-reset board owns none of GP0..GP15 and every one of
  * them is offerable in the /config pin picker.
  *
  * Until 1.5.6 this came up with eleven slots already active — DHT22 on GP0,
  * DS18B20 on GP1..GP9, and the ambient DHT22 on GP10 — a snapshot of one
  * protoboard baked in as everyone's starting point. The /config pin picker
  * greys out every GPIO owned by an active slot, so those eleven phantom
  * sensors, wired to nothing, made their GPIOs unassignable until each was
  * freed by hand, and a factory reset put them all back.
  *
  * pins[0] = i is a *suggestion* for a slot that has never been configured,
  * not a claim: the slot is inactive, so sensOwners( ) and the commit
  * validator both ignore it. */
 for (int i = 0; i < MAX_SENSORS; i++) {
 _currentConfig.sensors[i].active = false;
 _currentConfig.sensors[i].sensorType = TYPE_NONE;
 memset(_currentConfig.sensors[i].pins, 255, sizeof(_currentConfig.sensors[i].pins));
 _currentConfig.sensors[i].pins[0] = i; /* Suggested GPIO, not a claim */
 memset(_currentConfig.sensors[i].rom, 0, 8);
 safeCopy(_currentConfig.sensors[i].hwId, "", sizeof(_currentConfig.sensors[i].hwId));
 safeCopy(_currentConfig.sensors[i].friendlyName, "", sizeof(_currentConfig.sensors[i].friendlyName));
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
 _currentConfig.sensors[i].chMin[c] = channelInfo(c).defMin;
 _currentConfig.sensors[i].chMax[c] = channelInfo(c).defMax;
 }
 _currentConfig.sensors[i].alarmsActive = true;
 }
}

uint32_t StorageManager::calculateCRC32(const uint8_t *data, size_t length) {
 uint32_t crc = 0xFFFFFFFF;
 for (size_t i = 0; i < length; i++) {
 crc ^= data[i];
 for (int j = 0; j < 8; j++) {
 if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
 else crc >>= 1;
 }
 }
 return ~crc;
}

/* Read config in current format. Accepts v13 (plaintext, pre-obfuscation) or v14 (fields
 * encrypted via XOR+KDF). Decrypts in-place when v14.
 * The caller (attemptLoad) accepts only the current schema.
 * so it is re-saved as v14 encrypted. */
bool StorageManager::loadCurrentBlob(File& f, SystemConfig& outCfg) {
 size_t bytesRead = f.read((uint8_t*)&outCfg, sizeof(SystemConfig));
 uint32_t readCrc = 0;
 size_t crcRead = f.read((uint8_t*)&readCrc, sizeof(readCrc));
 if (bytesRead != sizeof(SystemConfig)) return false;
 if (outCfg.magic != CONFIG_MAGIC) return false;
 /* v15 is the only native format accepted here — v13/v14 fall to loadAndMigrateV14
 * (file size smaller due to UserAccount[52] instead of [62]). */
 if (outCfg.version != CONFIG_VERSION) return false;
 if (crcRead == sizeof(readCrc)) {
 uint32_t calcCrc = calculateCRC32((uint8_t*)&outCfg, sizeof(SystemConfig));
 if (calcCrc != readCrc) return false;
 }
 /* v16 always writes with sensitive fields obfuscated (XOR keystream). */
 obfuscateSensitiveFields(outCfg);
 return true;
}

bool StorageManager::attemptLoad(const char* path, SystemConfig& outCfg) {
 File f = LittleFS.open(path, "r");
 if (!f) return false;
 size_t fileSize = f.size( );

 /* One accepted format, by deliberate decision for 2.0.0-alpha.
  *
  * Every migration path this function used to carry (v12, v13/v14, v15, v16)
  * described the old layout in terms of the CURRENT struct: sizeof(SensorRecord)
  * as the record stride, offsetof(SystemConfig, sensors) as the head size. That
  * holds only while the record keeps its size, and 2.0.0 changes it — per-channel
  * alarm limits and eight channel slots take SensorRecord from 87 B to 139 B.
  * Every derived "historical" size would move with it, so those readers would
  * have walked old files at the wrong stride. Silently: a wrong stride still
  * produces bytes, and the CRC covers the file as written, not as interpreted.
  *
  * Rather than freeze four historical layouts to keep paths off a version nobody
  * is being asked to stay on, the schema breaks here. A config written by 1.6.x
  * is not recognised, and the device comes up on defaults. The rejection is
  * recorded so the caller can say so — a user whose settings vanished is owed
  * the reason. */
 const size_t expected = sizeof(SystemConfig) + sizeof(uint32_t);
 if (fileSize == expected) {
 bool ok = loadCurrentBlob(f, outCfg);
 f.close( );
 return ok;
 }

 f.close( );
 _rejectedConfigSize = fileSize;
 return false;
}

bool StorageManager::loadConfiguration( ) {

 _rejectedConfigSize = 0;

 enterFlashReadLock( );
 SystemConfig* tempConfig = new (std::nothrow) SystemConfig;
 bool loaded = false;
 bool fromBackup = false;
 if (tempConfig) {
 if (LittleFS.exists(FILE_CONFIG) && attemptLoad(FILE_CONFIG, *tempConfig)) {
 _currentConfig = *tempConfig;
 loaded = true;
 } else if (LittleFS.exists(FILE_BACKUP) && attemptLoad(FILE_BACKUP, *tempConfig)) {
 _currentConfig = *tempConfig;
 loaded = true;
 fromBackup = true;
 }
 delete tempConfig;
 }
 exitFlashReadLock( );

 if (!loaded) {
 /* The rejection is NOT logged here. loadConfiguration( ) runs inside
  * StorageManager::begin( ), which the boot sequence calls before
  * LogManager::begin( ) — anything emitted at this point goes nowhere. The
  * previous "Config schema migrated" warning sat in exactly this spot and
  * was never seen once. _rejectedConfigSize survives for the caller to
  * report after the logger exists; see AppManager::setup( ). */
 loadDefaults( );
 return false;
 }

 /* Valid config was loaded from flash — the random password generated
 * by the constructor (via loadDefaults) is now garbage. Zero it to avoid leaking
 * via logs/display. If the loaded config STILL has mustChangePassword,
 * it means a previous factory was never changed; but the original plaintext
 * was lost on some reboot — unrecoverable. */
 clearInitialAdminPassword( );

 /* Limit TOTAL_LANGS from 8 to 2 (EN+PT). Devices that
 * had displayLang=ES..ZH in flash fall to PT (default) on next
 * boot to avoid out-of-bounds in DICTIONARY[]/LICENSE_TEXT[]. */
 if (_currentConfig.displayLang > LANG_PT) {
 _currentConfig.displayLang = LANG_PT;
 }

 if (fromBackup) {
 LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_RECOVER, 0, TRL("Primary config corrupt, recovered from backup"));
 }

 if (fromBackup) {
 saveConfiguration( );
 }
 return true;
}

/**
 * @brief Atomic configuration save: write to temp file, then rename.
 * Maintains a backup copy for recovery if the primary is corrupted.
 * CRC32 appended after the binary blob for integrity verification.
 */
bool StorageManager::saveConfiguration( ) {
 /* Granular instrumentation — autopsy distinguishes if stuck here
 * vs in LOG_CODE audit or webMgr handler. */
 LogManager::TraceScope _tr(0, MOD_SAVE_CONFIG);

 /* Flush pending cursor before saving config — ensures consistency */
 if (_cursorDirty) { _cursorDirty = false; /* force flush below */ }

 _currentConfig.magic = CONFIG_MAGIC;
 _currentConfig.version = CONFIG_VERSION;

 /* If admin changed password (mustChangePassword = false),
 * the initial RAM password lost value — zero before save to ensure
 * it doesn't leak in logs/display/CLI from here on. */
 if (_initialAdminPassword[0] != '\0' && !_currentConfig.users[0].mustChangePassword) {
 clearInitialAdminPassword( );
 }

 /*
 * Skip no-op: user clicks "Save" multiple times without changing fields →
 * burst of identical saves that pressures LittleFS GC without real gain.
 * If RAM content matches last saved, skip the write.
 * _lastSavedCrc is now a private class member (was previously
 * static local) — aligns with the pattern of other _last* fields.
 */
 uint32_t currentCrc = calculateCRC32((uint8_t*)&_currentConfig, sizeof(SystemConfig));
 if (currentCrc == _lastSavedCrc && _lastSavedCrc != 0) {
 _lastSaveWasNoOp = true;
 MetricsManager::instance( ).data( ).configSaves++; /* still counts as requested save */
 /* No LOG_CODE here: log file write pressures GC on click bursts. */
 return true;
 }
 _lastSaveWasNoOp = false;

 /*
 * RAII context-aware: extends WDT ctx to 30s (or keeps outer if larger,
 * ex: telemetry at 120s). Auto-restore on any exit path.
 */
 LogManager::WdtWindow _wdt(30000);

 /* Enter cooperative quiet mode. Core 1 is signaled
 * to freeze in a RAM-only loop with IRQs off; Core 0 then does all
 * flash ops without attempting IRQ-based multicore_lockout per chunk (which
 * could stuck and cascade). RAII releases on any return path. */
 struct BigSaveGuard {
 bool& inBigSaveRef;
 BigSaveQuietCallback cb;
 bool entered;
 /* `cb(c)` is a member initialisation, not a call. cppcheck parses it as a
  * call through the function pointer and then wants a null check it already
  * has one line below. */
 /* cppcheck-suppress nullPointerRedundantCheck */
 BigSaveGuard(bool& r, BigSaveQuietCallback c) : inBigSaveRef(r), cb(c), entered(false) {
 if (cb) {
 entered = cb(true);
 inBigSaveRef = entered;
 }
 }
 ~BigSaveGuard( ) {
 if (entered && cb) cb(false);
 inBigSaveRef = false;
 }
 } _bigSave(_inBigSave, _bigSaveQuietCb);

 /* With _inBigSave active, enterFlashSafeMode/exitFlashSafeMode skip the
 * IRQ-based lockCb (see method in StorageManager.cpp), so each
 * FLASH_OP becomes just watchdog_update + BLOCK + watchdog_update — without
 * pausing Core 1 per op (Core 1 is already frozen in the quiet loop). */

 /* Flush cursor to flash (if pending) */
 if (_cachedLastSent > 0) {
 FLASH_OP({
 File cf = LittleFS.open(FILE_TCURSOR, "w");
 if (cf) { cf.write((uint8_t*)&_cachedLastSent, sizeof(_cachedLastSent)); cf.close( ); }
 });
 }

 /* Open TMP and write encrypted config + CRC */
 File f;
 FLASH_OP(f = LittleFS.open(FILE_TMP, "w"));
 if (!f) {
 LOG_CODE(LOG_ERROR, "STO", SYS_STORAGE_FAIL, 0, "open FILE_TMP failed");
 return false;
 }

 /* Copy with sensitive fields encrypted. _currentConfig in
 * RAM stays plaintext. CRC over the encrypted version. */
 SystemConfig* encBuf = new (std::nothrow) SystemConfig;
 if (!encBuf) { f.close( ); return false; }
 *encBuf = _currentConfig;
 obfuscateSensitiveFields(*encBuf);
 uint32_t crc = calculateCRC32((uint8_t*)encBuf, sizeof(SystemConfig));

 size_t bytesWritten = 0, crcWritten = 0;
 FLASH_OP({
 bytesWritten = f.write((uint8_t*)encBuf, sizeof(SystemConfig));
 crcWritten = f.write((uint8_t*)&crc, sizeof(crc));
 f.close( );
 });
 delete encBuf;

 if (bytesWritten != sizeof(SystemConfig) || crcWritten != sizeof(crc)) {
 FLASH_OP(LittleFS.remove(FILE_TMP));
 LOG_CODE(LOG_ERROR, "STO", SYS_STORAGE_FAIL, (int)bytesWritten, "Config save failed");
 return false;
 }

 /* Atomic rename: backup, rename, rename. Each in its own lockout. */
 if (LittleFS.exists(FILE_CONFIG)) {
 FLASH_OP(LittleFS.remove(FILE_BACKUP));
 FLASH_OP(LittleFS.rename(FILE_CONFIG, FILE_BACKUP));
 }
 FLASH_OP(LittleFS.rename(FILE_TMP, FILE_CONFIG));

 MetricsManager::instance( ).data( ).configSaves++;
 _lastSavedCrc = currentCrc; /* Mark persisted content for future no-op skip */
 _lastSaveMs = millis( ); /* Timestamp for server-side rate-limit */
 LOG_CODE(LOG_INFO, "STO", SYS_STORAGE_SAVE, (int)(sizeof(SystemConfig)), "");
 return true;
}

bool StorageManager::canSaveNow( ) const {
 constexpr uint32_t MIN_SAVE_INTERVAL_MS = 1000;
 if (_lastSaveMs == 0) return true;
 return timeSince(_lastSaveMs, MIN_SAVE_INTERVAL_MS);
}

void StorageManager::resetToFactory( ) { loadDefaults( ); saveConfiguration( ); }
SystemConfig& StorageManager::getConfig( ) { return _currentConfig; }

/* Helpers for random initial admin password. */
void StorageManager::generateInitialAdminPassword(char* outPlain, size_t bufSize) {
 static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; /* 32 chars, without O/0/I/1 */
 const size_t alphabetLen = sizeof(alphabet) - 1; /* -1 for '\0' */
 const size_t len = (bufSize > 9) ? 8 : (bufSize > 0 ? bufSize - 1 : 0);
 for (size_t i = 0; i < len; i++) {
 outPlain[i] = alphabet[rp2040.hwrand32( ) % alphabetLen];
 }
 if (bufSize > 0) outPlain[len] = '\0';
}

bool StorageManager::isFactoryDefaults( ) const {
 if (!_currentConfig.users[0].active) return false;
 if (strcmp(_currentConfig.users[0].username, "admin") != 0) return false;
 return _currentConfig.users[0].mustChangePassword;
}

void StorageManager::clearInitialAdminPassword( ) {
 /* volatile to prevent optimizer from eliminating "dead" memset. */
 volatile char* p = _initialAdminPassword;
 for (size_t i = 0; i < sizeof(_initialAdminPassword); i++) p[i] = 0;
}

/* mustChangePin flag in reserved[26..27]. */
bool StorageManager::mustChangePin( ) const {
 const SetupFlagsData* sf = reinterpret_cast<const SetupFlagsData*>(
 _currentConfig.reserved + SETUP_FLAGS_OFFSET);
 /* Overlay without magic = legacy config (v13/v14 without setup flags) — don't force
 * change to not break firmware upgrades for existing users. */
 if (sf->magic != SETUP_FLAGS_MAGIC) return false;
 return (sf->flags & FLAG_MUST_CHANGE_PIN) != 0;
}

void StorageManager::clearMustChangePin( ) {
 SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(
 _currentConfig.reserved + SETUP_FLAGS_OFFSET);
 if (sf->magic != SETUP_FLAGS_MAGIC) {
 /* Initialize overlay if not yet. */
 sf->magic = SETUP_FLAGS_MAGIC;
 sf->flags = 0;
 } else {
 sf->flags &= ~FLAG_MUST_CHANGE_PIN;
 }
}

void StorageManager::setMustChangePin( ) {
 SetupFlagsData* sf = reinterpret_cast<SetupFlagsData*>(
 _currentConfig.reserved + SETUP_FLAGS_OFFSET);
 sf->magic = SETUP_FLAGS_MAGIC;
 sf->flags |= FLAG_MUST_CHANGE_PIN;
}

/* ===========================================================================
 * NetworkTimeData overlay in reserved[28..47]
 * =========================================================================== */

NetworkTimeData* StorageManager::ensureNetworkTimeOverlay( ) {
 NetworkTimeData* nt = reinterpret_cast<NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) {
 nt->magic = NETTIME_MAGIC;
 nt->flags = FLAG_DNS_AUTO | FLAG_NTP_ENABLED;
 nt->dns2[0] = '\0';
 nt->pad[0] = nt->pad[1] = 0;
 }
 return nt;
}

bool StorageManager::isDnsAuto( ) const {
 const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) return true; /* legacy = default AUTO */
 return (nt->flags & FLAG_DNS_AUTO) != 0;
}

void StorageManager::setDnsAuto(bool auto_) {
 NetworkTimeData* nt = ensureNetworkTimeOverlay( );
 if (auto_) nt->flags |= FLAG_DNS_AUTO;
 else nt->flags &= ~FLAG_DNS_AUTO;
}

bool StorageManager::isNtpEnabled( ) const {
 const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) return true;
 return (nt->flags & FLAG_NTP_ENABLED) != 0;
}

void StorageManager::setNtpEnabled(bool enabled) {
 NetworkTimeData* nt = ensureNetworkTimeOverlay( );
 if (enabled) nt->flags |= FLAG_NTP_ENABLED;
 else nt->flags &= ~FLAG_NTP_ENABLED;
}

const char* StorageManager::getSecondaryDns( ) const {
 const NetworkTimeData* nt = reinterpret_cast<const NetworkTimeData*>(
 _currentConfig.reserved + NETTIME_OFFSET);
 if (nt->magic != NETTIME_MAGIC) return "";
 return nt->dns2;
}

void StorageManager::setSecondaryDns(const char* ip) {
 NetworkTimeData* nt = ensureNetworkTimeOverlay( );
 safeCopy(nt->dns2, ip ? ip : "", sizeof(nt->dns2));
}

uint16_t StorageManager::getHistoryIntervalMin( ) const {
 const HistoryConfigData* hc = reinterpret_cast<const HistoryConfigData*>(
 _currentConfig.reserved + HISTORY_CONFIG_OFFSET);
 if (hc->magic != HISTORY_CONFIG_MAGIC) return HISTORY_INTERVAL_DEFAULT_MIN;
 uint16_t v = hc->intervalMin;
 if (v < HISTORY_INTERVAL_MIN_MIN) return HISTORY_INTERVAL_DEFAULT_MIN;
 if (v > HISTORY_INTERVAL_MAX_MIN) return HISTORY_INTERVAL_MAX_MIN;
 return v;
}

void StorageManager::setHistoryIntervalMin(uint16_t minutes) {
 if (minutes < HISTORY_INTERVAL_MIN_MIN) minutes = HISTORY_INTERVAL_MIN_MIN;
 if (minutes > HISTORY_INTERVAL_MAX_MIN) minutes = HISTORY_INTERVAL_MAX_MIN;
 HistoryConfigData* hc = reinterpret_cast<HistoryConfigData*>(
 _currentConfig.reserved + HISTORY_CONFIG_OFFSET);
 hc->magic = HISTORY_CONFIG_MAGIC;
 hc->pad = 0;
 hc->intervalMin = minutes;
}

bool StorageManager::isHaDiscoveryEnabled( ) const {
 const HaDiscoveryData* ha = reinterpret_cast<const HaDiscoveryData*>(
 _currentConfig.reserved + HA_DISCOVERY_OFFSET);
 if (ha->magic != HA_DISCOVERY_MAGIC) return false; /* legacy = OFF */
 return (ha->flags & FLAG_HA_DISCOVERY) != 0;
}

void StorageManager::setHaDiscoveryEnabled(bool enabled) {
 HaDiscoveryData* ha = reinterpret_cast<HaDiscoveryData*>(
 _currentConfig.reserved + HA_DISCOVERY_OFFSET);
 if (ha->magic != HA_DISCOVERY_MAGIC) { ha->magic = HA_DISCOVERY_MAGIC; ha->flags = 0; }
 if (enabled) ha->flags |= FLAG_HA_DISCOVERY;
 else ha->flags &= ~FLAG_HA_DISCOVERY;
}

bool StorageManager::wasHaDiscoveryPublished( ) const {
 const HaDiscoveryData* ha = reinterpret_cast<const HaDiscoveryData*>(
 _currentConfig.reserved + HA_DISCOVERY_OFFSET);
 if (ha->magic != HA_DISCOVERY_MAGIC) return false;
 return (ha->flags & FLAG_HA_PUBLISHED) != 0;
}

void StorageManager::markHaDiscoveryPublished(bool published) {
 if (wasHaDiscoveryPublished( ) == published) return; /* no flash churn */
 HaDiscoveryData* ha = reinterpret_cast<HaDiscoveryData*>(
 _currentConfig.reserved + HA_DISCOVERY_OFFSET);
 if (ha->magic != HA_DISCOVERY_MAGIC) { ha->magic = HA_DISCOVERY_MAGIC; ha->flags = 0; }
 if (published) ha->flags |= FLAG_HA_PUBLISHED;
 else ha->flags &= ~FLAG_HA_PUBLISHED;
 /* Persist now: this bit must survive the commit_all reboot, which is the
  * ordinary path between "published" and "user turned it off". */
 saveConfiguration( );
}

/* ── Syslog overlay (reserved[56..63]) ───────────────────────────────────── */

bool StorageManager::isSyslogEnabled( ) const {
 const SyslogConfigData* sl = reinterpret_cast<const SyslogConfigData*>(
 _currentConfig.reserved + SYSLOG_CONFIG_OFFSET);
 if (sl->magic != SYSLOG_CONFIG_MAGIC) return false; /* legacy = OFF */
 /* A forwarder with no destination is off, whatever the flag says. */
 return (sl->flags & FLAG_SYSLOG_ENABLED) != 0 && sl->serverIp != 0;
}

bool StorageManager::getSyslogEnabledFlag( ) const {
 const SyslogConfigData* sl = reinterpret_cast<const SyslogConfigData*>(
 _currentConfig.reserved + SYSLOG_CONFIG_OFFSET);
 if (sl->magic != SYSLOG_CONFIG_MAGIC) return false;
 return (sl->flags & FLAG_SYSLOG_ENABLED) != 0;
}

uint32_t StorageManager::getSyslogServerIp( ) const {
 const SyslogConfigData* sl = reinterpret_cast<const SyslogConfigData*>(
 _currentConfig.reserved + SYSLOG_CONFIG_OFFSET);
 if (sl->magic != SYSLOG_CONFIG_MAGIC) return 0;
 return sl->serverIp;
}

uint16_t StorageManager::getSyslogPort( ) const {
 const SyslogConfigData* sl = reinterpret_cast<const SyslogConfigData*>(
 _currentConfig.reserved + SYSLOG_CONFIG_OFFSET);
 if (sl->magic != SYSLOG_CONFIG_MAGIC || sl->port == 0) return SYSLOG_DEFAULT_PORT;
 return sl->port;
}

uint8_t StorageManager::getSyslogMinLevel( ) const {
 const SyslogConfigData* sl = reinterpret_cast<const SyslogConfigData*>(
 _currentConfig.reserved + SYSLOG_CONFIG_OFFSET);
 if (sl->magic != SYSLOG_CONFIG_MAGIC) return 1; /* LOG_INFO */
 return syslogMinLevel(sl->flags);
}

void StorageManager::setSyslogConfig(bool enabled, uint32_t serverIp,
                                     uint16_t port, uint8_t minLevel) {
 SyslogConfigData* sl = reinterpret_cast<SyslogConfigData*>(
 _currentConfig.reserved + SYSLOG_CONFIG_OFFSET);
 sl->magic = SYSLOG_CONFIG_MAGIC;
 sl->flags = syslogPackFlags(enabled, minLevel);
 sl->port = port;
 sl->serverIp = serverIp;
}

SensorRecord* StorageManager::getSensorByGpio(uint8_t gpio) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (_currentConfig.sensors[i].active && _currentConfig.sensors[i].pins[0] == gpio) {
 return &_currentConfig.sensors[i];
 }
 }
 return nullptr;
}

bool StorageManager::canWriteHistory(size_t sizeToWrite) { return _isMounted; }

/* ── V4 history file name ──────────────────────────────────── */

String StorageManager::getHistoryFileNameV4( ) {
	 return getHistoryFileNameV4((uint32_t)time(nullptr));
}

String StorageManager::getHistoryFileNameV4(uint32_t epoch) {
	 time_t t = (time_t)epoch;
	 struct tm timeinfo;
	 localtime_r(&t, &timeinfo);
	 char buff[42];
	 snprintf(buff, sizeof(buff), "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY,
	          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
	 return String(buff);
}

void StorageManager::getHistoryFileNameV4(char* buf, size_t len) {
	 time_t now = time(nullptr);
	 struct tm timeinfo;
	 localtime_r(&now, &timeinfo);
	 snprintf(buf, len, "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY,
	          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

/* ── V4 Schema Builder ──────────────────────────────────────── */

bool StorageManager::buildMeasureSchema(
     HistV4SensorDef *sensors, uint8_t &sensorCount,
     HistV4MeasureDef *measures, uint8_t &measureCount,
     uint8_t *strPool, uint8_t &strPoolSize)
{
	 sensorCount   = 0;
	 measureCount  = 0;
	 strPoolSize   = 0;

	 for (int slot = 0; slot < MAX_SENSORS; slot++) {
	 if (!_currentConfig.sensors[slot].active) continue;

	 const auto &sr = _currentConfig.sensors[slot];
	 SensorFormat fmt = SensorFormat::forType((SensorType)sr.sensorType);

	 /* ── Add sensor to sensor table ── */
	 uint8_t hwIdOff = strPoolSize;
	 uint8_t hwIdLen = (uint8_t)strlen(sr.hwId);
	 if (hwIdLen > 0 && strPoolSize + hwIdLen <= HIST_V4_MAX_STRPOOL) {
	 memcpy(strPool + strPoolSize, sr.hwId, hwIdLen);
	 strPoolSize += hwIdLen;
	 } else {
	 continue;
	 }

	 uint8_t nameOff = strPoolSize;
	 uint8_t nameLen = (uint8_t)strlen(sr.friendlyName);
	 if (strPoolSize + nameLen <= HIST_V4_MAX_STRPOOL) {
	 memcpy(strPool + strPoolSize, sr.friendlyName, nameLen);
	 strPoolSize += nameLen;
	 } else {
	 nameLen = 0;
	 }

	 uint8_t chMask = 0;
	 for (uint8_t ch = 0; ch < MAX_SENSOR_CHANNELS; ch++) {
	 if (sensorHasChannel((SensorType)sr.sensorType, ch)) {
	 chMask |= (1 << ch);
	 }
	 }

	 sensors[sensorCount].hwIdOffset  = hwIdOff;
	 sensors[sensorCount].hwIdLen     = hwIdLen;
	 sensors[sensorCount].nameOffset  = nameOff;
	 sensors[sensorCount].nameLen     = nameLen;
	 sensors[sensorCount].sensorType  = sr.sensorType;
	 sensors[sensorCount].channelMask = chMask;
	 sensors[sensorCount].flags       = 0;
	 memset(sensors[sensorCount].reserved, 0, 2);

	 /* ── Add measurements (one per channel of this sensor) ── */
	 for (uint8_t ch = 0; ch < MAX_SENSOR_CHANNELS; ch++) {
	 if (!sensorHasChannel((SensorType)sr.sensorType, ch)) continue;

	 const auto &vf = fmt.values[ch];

	 uint8_t unitOff = strPoolSize;
	 uint8_t unitLen = (uint8_t)strlen(vf.unit);
	 if (strPoolSize + unitLen > HIST_V4_MAX_STRPOOL) break;
	 memcpy(strPool + strPoolSize, vf.unit, unitLen);
	 strPoolSize += unitLen;

	 /* Use user-configured bit width if set, else default for channel */
	 uint8_t bw = sr.channelBitWidth[ch];
	 if (bw == 0) bw = histV4DefaultBitWidth(ch);

	 measures[measureCount].sensorIdx  = sensorCount;
	 measures[measureCount].channel    = ch;
	 measures[measureCount].bitWidth   = bw;
	 measures[measureCount].decimals   = vf.decimals;
	 measures[measureCount].unitOffset = unitOff;
	 measures[measureCount].unitLen    = unitLen;
	 measures[measureCount].scale      = histV4DefaultScale(ch);
	 measureCount++;
	 if (measureCount >= HIST_V4_MAX_MEASUREMENTS) break;
	 }

	 sensorCount++;
	 if (sensorCount >= HIST_V4_MAX_SENSORS) break;
	 if (measureCount >= HIST_V4_MAX_MEASUREMENTS) break;
	 }

	 return sensorCount > 0 && measureCount > 0;
}



/**
 * @brief Delete oldest history files to keep flash usage below 86%.
 * Uses dirty-flag caching and a 4-second budget timer to avoid
 * blocking the main loop during extensive cleanup.
 */
void StorageManager::enforceStorageLimit( ) {
 FSInfo info; LittleFS.info(info);
 if (info.totalBytes == 0) return;


 if (!_storageDirty && !_cleanupPending && ((info.usedBytes * 100) / info.totalBytes) <= 86) return;

 /* T1.4 (stability wave 1): the old version deleted in a 4 s burst
  * (up to 30 files). Each delete costs sector erases with Core-0 IRQs
  * off, starving the CYW43 radio for seconds — the "Wi-Fi drops during
  * cleanup" symptom. Now: at most 2 deletions per call; the remainder
  * is flagged in _cleanupPending and drained one file per maintenance
  * slice by update( ) (>= 15 s apart). Same total work over time,
  * ~100x lower duty cycle of IRQ-off windows. */
 int deletionsLeft = 2;
 while (deletionsLeft > 0 && ((info.usedBytes * 100) / info.totalBytes) > 86) {
 feedWdt( );


 String oldestFile = _cachedOldestFile;
 if (oldestFile.length( ) == 0) {
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 int dirCount = 0;
 while (dir.next( )) {
 feedWdt( );
 if (++dirCount % 20 == 0) delay(1);
 String fileName = dir.fileName( );


 if (fileName.endsWith(HISTORY_FILE_EXT) && isValidHistoryFileName(fileName.c_str( ))) {
 if (oldestFile == "" || fileName < oldestFile) oldestFile = fileName;
 }
 }
 }

 if (oldestFile != "") {
 String fullPath = String(DIR_HISTORY) + "/" + oldestFile;

 if (fullPath == _currentLogFileName || fullPath == _v4CurrentLogFileName) {
 LOG_CODE(LOG_WARN, "STO", STO_ENFORCE_SKIP_ACTIVE, 0, "");
 break;
 }
 LittleFS.remove(fullPath);
 deletionsLeft--;
 _cachedOldestFile = "";
 LittleFS.info(info);
 } else break;
 }


 if (((info.usedBytes * 100) / info.totalBytes) <= 86) {
 _storageDirty = false;
 _cleanupPending = false;
 } else {
 /* Still above the limit — defer the rest to maintenance slices. */
 if (!_cleanupPending) LOG_CODE(LOG_INFO, "STO", STO_ENFORCE_BUDGET, 0, "deferred");
 _cleanupPending = true;
 }
}

uint32_t StorageManager::getLastSentTimestamp( ) {
 if (_cachedLastSent > 0) return _cachedLastSent;

 enterFlashReadLock( );
 if (!LittleFS.exists(FILE_TCURSOR)) { exitFlashReadLock( ); return 0; }
 File f = LittleFS.open(FILE_TCURSOR, "r");
 uint32_t ts = 0;
 if (f) { f.read((uint8_t*)&ts, sizeof(ts)); f.close( ); }
 exitFlashReadLock( );
 _cachedLastSent = ts;
 return ts;
}

void StorageManager::setLastSentTimestamp(uint32_t ts) {
 _cachedLastSent = ts;
 _cursorDirty = true;
 _cursorCoalesceTime = millis( );
}

/**
 * @brief CMD_TEL_RESET: reset telemetry cursor without needing reboot.
 * Invalidates RAM cache (_cachedLastSent=0), clears pending coalescer,
 * and removes the flash file. Next getLastSentTimestamp returns 0; next
 * collectBatch applies the "lastRecorded - 30 days" fallback.
 *
 * Use cases:
 * - Operations: re-send data after prolonged server outage (cursor
 * got well beyond what the server has).
 * - Maintenance: move data to another destination and want to re-send from scratch.
 * - Testing: tools/stress_test/run_stress_test.sh calls this path
 * (previously used /api/delete + reboot via Serial).
 */
void StorageManager::resetTelemetryCursor( ) {
 _cachedLastSent = 0;
 _cursorDirty = false;
 _cursorCoalesceTime = 0;

 LogManager::WdtWindow _wdt(15000);
 enterFlashSafeMode( );
 if (LittleFS.exists(FILE_TCURSOR)) {
 LittleFS.remove(FILE_TCURSOR);
 }
 exitFlashSafeMode( );
 /* TelemetryManager._pendingDirty is set by update( ) post-send; we don't
 * touch it from here to avoid circular dependency. Next tick redoes the
 * count with the zeroed cursor. */
}

void StorageManager::flushCursorIfDirty( ) {
 if (!_cursorDirty) return;
 if (!timeSince(_cursorCoalesceTime, CURSOR_COALESCE_MS)) return;

 /* Touch priority: if user is interacting, cursor stays dirty and flush
 * happens on next call after interaction ends. */
 if (TouchPriority::isActive( )) return;

	_cursorDirty = false;
	LogManager::WdtWindow _wdt(30000); /* context-aware */

	/* A4: a pausa do Core 1 aqui era um par manual enter/exit. Funciona
	 * hoje porque não há `return` entre os dois, mas qualquer guarda
	 * futura no meio do corpo deixaria o Core 1 congelado para sempre.
	 * RAII fecha a classe — e aninha de graça (refcount) se algum
	 * chamador já pausou. */
	Core1FlashPause _c1(this);
	watchdog_update( );
	File f = LittleFS.open(FILE_TCURSOR, "w");
	watchdog_update( );
	if (f) {
		f.write((uint8_t*)&_cachedLastSent, sizeof(_cachedLastSent));
		f.close( );
		watchdog_update( );
	}
	/* Core1FlashPause e WdtWindow restauram no fim do escopo */
}

String StorageManager::getStatsReport( ) {
 if (!_isMounted) return String("FS Not Mounted");

 enterFlashReadLock( );
 FSInfo info; LittleFS.info(info);
 exitFlashReadLock( );

 size_t available = (size_t)(info.totalBytes - info.usedBytes);
 String s = "=== Storage Stats ===\n";
 s += "Total: " + String((unsigned long)info.totalBytes) + " B\n";
 s += "Used: " + String((unsigned long)info.usedBytes) + " B\n";
 s += "Free: " + String((unsigned long)available) + " B";
 return s;
}

/* Epoch of the newest record on disk. Seeds the virtual RTC at boot and
 * bounds the telemetry cursor.
 *
 * Records are variable-length in V4, so there is no seeking to the end: the
 * newest day file is walked in full. Once per boot, acceptable.
 *
 * Filenames are YYYYMMDD, so lexical order IS chronological order. Only
 * .sim4 is considered — this used to scan .bin, and once nothing wrote that
 * extension any more the function answered 0 on every board. */
uint32_t StorageManager::getLastRecordedTimestamp( ) {
	/* Under V4 this walked the newest day file record by record, because a
	 * variable-length stream has no seekable end. V5 blocks are framed, so
	 * the walk is over ~24 headers and only the LAST block is decoded —
	 * and the block still open in RAM beats anything on flash. */
	if (_h5Valid && _h5Enc.count( )) {
		const uint32_t ram = _h5Enc.lastEpoch( );
		if (ram) return ram;
	}

	enterFlashReadLock( );
	Dir dir = LittleFS.openDir(DIR_HISTORY);
	String newestFile = "";
	while (dir.next( )) {
		feedWdt( );
		const String fn = dir.fileName( );
		/* Names are YYYYMMDD, so lexical order IS chronological order. */
		if (fn.endsWith(HISTORY_FILE_EXT) && fn > newestFile) newestFile = fn;
	}
	exitFlashReadLock( );
	if (newestFile.length( ) == 0) return 0;

	/* A block in 20260801.h5 belongs to 1 Aug — the file name IS the bound,
	 * and it is the only clock available before NTP. Without it a single
	 * corrupt t0 poisons the boot: this value becomes the provisional clock,
	 * so one block stamped into the future starts the device ahead of real
	 * time, and every record written before the sync inherits the error. */
	uint32_t dayStart = 0, dayEnd = 0;
	h5DayWindowFromName(newestFile.c_str( ), newestFile.length( ), dayStart, dayEnd);

	uint32_t lastTs = 0;
	{
		ReadGuard rg(this);
		if (h5OpenDay(String(DIR_HISTORY) + "/" + newestFile, /*verifyPayload=*/false)) {
			uint32_t lastT0 = 0;
			uint8_t  lastCount = 0;
			/* The NEWEST block, not the last one in the file. Those are the
			 * same thing only while appends happen in time order, and they
			 * stop being the same the moment a boot writes a block out of
			 * order — which is exactly when this function is asked.
			 *
			 * Taking the last one was the seed of a cascade: this value
			 * becomes the provisional clock (lastTs + 60), the provisional
			 * clock decides the NTP delta, and handleTimeSync( ) shifts every
			 * block with t0 >= that base. Start the clock stale and the shift
			 * moves blocks that were already correct — one bench file ended up
			 * with a block stamped 46 min into the future, and a reader that
			 * stops at the window's end saw nothing past it. */
			for (;;) {
				H5DataHeader hdr;
				const int16_t *mn = nullptr, *mx = nullptr;
				if (!h5NextBlock(hdr, mn, mx)) break;
				if (dayEnd && (hdr.t0 < dayStart || hdr.t0 >= dayEnd)) {
					LOG_CODE(LOG_WARN, "STO", STO_SCHEMA_MISMATCH,
					         (int)(hdr.t0 - dayStart), "h5_t0_off_day");
					feedWdt( );
					continue;
				}
				if (hdr.t0 >= lastT0) {
					lastT0 = hdr.t0;
					lastCount = hdr.pre.a;
				}
				feedWdt( );
			}
			lastTs = lastT0;
			if (lastT0 && lastCount > 1 && h5SeekTo(lastT0)) {
				int16_t vals[H5_MAX_CHANNELS];
				uint32_t epoch = 0;
				for (uint8_t r = 0; r < lastCount && h5NextRecord(epoch, vals); r++) {
					if (epoch > lastTs) lastTs = epoch;
				}
			}
			h5CloseDay( );
		}
	}

	/* The open block has not reached the day file yet — it lives in the .wip
	 * snapshot, and on a reboot it is exactly the data recoverWipV5( ) is about
	 * to restore with its own, already-correct timestamps. Reading only the day
	 * file seeds the provisional clock from the last SEALED block, minutes behind
	 * the real newest record. That stale base becomes the NTP shift floor
	 * (handleTimeSync → shiftHistoryTimeV5, which moves every block with
	 * t0 >= base), so the correction then drags the just-recovered block into the
	 * future by the whole error — a reboot at 06:04 filed 05:48–06:03 as
	 * 06:04–06:18 on the bench, 15 min off, for exactly this reason.
	 *
	 * Including the .wip's newest record keeps the clock close to real AND lifts
	 * the shift floor above the recovered block (base = lastTs + 60 > its t0), so
	 * it stays where its own timestamps put it. Header t0 alone already exempts
	 * the block; decoding to the last record just shrinks the residual error and,
	 * with it, how much post-boot data the shift has to touch. Read under the
	 * same lock the day scan uses; decode afterwards on the RAM buffer, since
	 * ensureH5Schema( ) must not run holding _fsReadMutex. */
	{
		size_t wipLen = 0;
		{
			ReadGuard rg(this);
			if (LittleFS.exists(FILE_H5_WIP)) {
				File f = LittleFS.open(FILE_H5_WIP, "r");
				if (f) {
					const int got = f.read(_h5Chunk, sizeof(_h5Chunk));
					if (got > 0) wipLen = (size_t)got;
					f.close( );
				}
			}
		}
		if (wipLen >= sizeof(H5DataHeader)) {
			/* Every gate — magic, version, plausibility of t0, and the CRC that
			 * has to pass before any field is believed — lives in
			 * h5SeedFromSnapshot, next to the format it judges and reachable
			 * from the host test suite. What used to be here trusted t0 from an
			 * unverified header and bounded it by a flat 24 h, which is what let
			 * the 2026-08-14 seed start the clock 4 h 43 min into the future. */
			ensureH5Schema( );
			const uint16_t nominalS = h5NominalSeconds(getHistoryIntervalMin( ));
			uint32_t seed = 0;
			if (_h5Valid) {
				seed = h5SeedFromSnapshot(_h5Chunk, wipLen, _h5Schema, _h5NCh,
				                          dayStart, dayEnd, nominalS);
			}
			if (seed > lastTs) lastTs = seed;
			else if (seed == 0) {
				/* Outside the _h5Valid branch on purpose. A snapshot that
				 * contributes nothing is the interesting case either way, and
				 * the two reasons are not the same thing: ctx 0 is a snapshot
				 * that failed a gate, ctx 1 is no schema to judge it with — the
				 * whole path skipped, sealed data standing alone. Logging only
				 * inside the branch would make the second one invisible, which
				 * is how a seed path comes to be dead without anyone noticing. */
				LOG_CODE(LOG_WARN, "STO", STO_H5_WIP, _h5Valid ? 0 : 1,
				         "wip_seed_rejected");
			}
		}
	}
	return lastTs;
}


/* ─────────────────────────────────────────────────────────────────────────── */
/* getHistoryDaysMask( ) — bitmask of days with history file */
/* ─────────────────────────────────────────────────────────────────────────── */
/**
 * @brief Returns bitmask of days in a month that have a .sim4 file.
 *
 * Bit N set = day N has data (bit 1 = day 1, bit 31 = day 31).
 * Used by the calendar screen on the TFT display.
 *
 * @param year Year (ex: 2026).
 * @param month Month (1-12).
 * @return 32-bit bitmask with available days.
 */
uint32_t StorageManager::getHistoryDaysMask(int year, int month) {
 uint32_t mask = 0;

 /* Build expected prefix: "YYYYMM" */
 char prefix[8];
 snprintf(prefix, sizeof(prefix), "%04d%02d", year, month);

 enterFlashReadLock( );
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 feedWdt( );
 String fn = dir.fileName( );
 if (!fn.endsWith(HISTORY_FILE_EXT)) continue;

 /* File: "YYYYMMDD.sim4" — check month prefix */
 if (fn.length( ) >= 8 && fn.startsWith(prefix)) {
 int day = fn.substring(6, 8).toInt( );
 if (day >= 1 && day <= 31) {
 mask |= (1UL << day);
 }
 }
 }
 exitFlashReadLock( );

 return mask;
}


/* =========================================================================== */
/* PROVISIONAL TIMESTAMP CORRECTION (NTP SYNC) */
/* =========================================================================== */
String StorageManager::getBoardSerialNumber( ) {
 pico_unique_board_id_t board_id; pico_get_unique_board_id(&board_id);
 char hex[17]; snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X%02X%02X", board_id.id[0], board_id.id[1], board_id.id[2], board_id.id[3], board_id.id[4], board_id.id[5], board_id.id[6], board_id.id[7]);
 return String(hex);
}

long StorageManager::getCalibrationVersion(String path) {
 if (!LittleFS.exists(path)) return -1;

 enterFlashReadLock( );
 File f = LittleFS.open(path, "r"); long ver = -1;
 if (f) {
 char lineBuf[64]; size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 lineBuf[len] = '\0'; String line = String(lineBuf); line.trim( );
 if (line.startsWith("VERSION,")) ver = line.substring(8).toInt( );
 f.close( );
 }
 exitFlashReadLock( );
 return ver;
}

/* Shared tail of a calib.csv row — everything after the id column. The row
 * shape is identified by field count inside calibRowParseTail (CalibCurve.h,
 * host-tested): name-only, legacy `offset,name`, canonical `name,raw,ref...`
 * or the transitional `offset,name,raw,ref...`. A false return means point
 * cells were present but malformed and a fallback was taken. */
static void parseCalibRowTail(const String& line, int p2,
                              CalibCurve& outCurve, String& outName) {
 char nameBuf[40];
 const bool clean = calibRowParseTail(line.c_str( ) + p2 + 1, outCurve, nameBuf, sizeof(nameBuf));
 outName = nameBuf;
 outName.replace("\"", "");
 if (!clean) {
 LOG_CODE(LOG_WARN, "CFG", SEC_CONFIG_CHANGED, 0, "bad point cells in calib.csv — fallback");
 }
}

bool StorageManager::getCalibrationData(const uint8_t* rom, String& outId, CalibCurve& outCurve, String& outName) {
 char romStr[17]; snprintf(romStr, sizeof(romStr), "%02X%02X%02X%02X%02X%02X%02X%02X", rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
 if (!LittleFS.exists("/calib.csv")) return false;


 enterFlashReadLock( );
 File f = LittleFS.open("/calib.csv", "r"); bool found = false;
 if (f) {
 /* 320, not 256: a 5-point pts column on a pressure row is ~155 chars on
  * top of key+id+offset+name. A row longer than the buffer is read in two
  * halves and neither matches a key — skipped, not misparsed. */
 char lineBuf[320];
 while (f.available( )) {
 feedWdt( );
 size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 if (len == 0) continue;
 lineBuf[len] = '\0'; if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
 String line = String(lineBuf); line.trim( );
 if (line.length( ) >= 16 && line.substring(0, 16).equalsIgnoreCase(romStr)) {
 int p1 = line.indexOf(','); int p2 = line.indexOf(',', p1 + 1);
 if (p1 > 0 && p2 > p1) {
 outId = line.substring(p1 + 1, p2);
 parseCalibRowTail(line, p2, outCurve, outName);
 found = true; break;
 }
 }
 }
 f.close( );
 }
 exitFlashReadLock( );
 return found;
}

/* Lookup for sensors with no 1-Wire ROM (DHT22, BMP280) in calib.csv.
 * Key column = picoUID 16 hex (same shape as a DS18B20 ROM). The ID column
 * is `prefix` + the sensor's hwId: `t` for temperature, `u` for humidity,
 * `p` for pressure — so `<picoUID>,tAMB,-0.4,Sala` is the temperature row of
 * the sensor whose hwId is AMB. The whole ID is compared, which is what keeps
 * two ROM-less sensors on one board apart. */
bool StorageManager::getCalibrationByHwId(char prefix, const char* hwId, CalibCurve& outCurve, String& outName) {
 /* Any letter the channel table claims. This was a literal whitelist of 't'
  * and 'u', so the writer could emit a `p<hwId>` row and this reader refused
  * it before even opening the file — the offset persisted correctly and was
  * applied to nothing. A new quantity now becomes readable the moment it has
  * a table row, with no edit here. */
 if (channelByLetter(prefix) < 0) return false;
 if (!hwId || hwId[0] == '\0') return false;
 String picoUID = getBoardSerialNumber( ); /* 16 hex without separator */
 if (!LittleFS.exists("/calib.csv")) return false;

 String wanted = String(prefix) + hwId;

 enterFlashReadLock( );
 File f = LittleFS.open("/calib.csv", "r"); bool found = false;
 if (f) {
 char lineBuf[320]; /* sized for a 5-point pts column, see getCalibrationData */
 while (f.available( )) {
 feedWdt( );
 size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 if (len == 0) continue;
 lineBuf[len] = '\0'; if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
 String line = String(lineBuf); line.trim( );
 if (line.length( ) < 16) continue;
 if (!line.substring(0, 16).equalsIgnoreCase(picoUID)) continue;
 int p1 = line.indexOf(','); int p2 = line.indexOf(',', p1 + 1);
 if (p1 <= 0 || p2 <= p1) continue;
 String idCol = line.substring(p1 + 1, p2); idCol.trim( );
 if (!idCol.equalsIgnoreCase(wanted)) continue;
 parseCalibRowTail(line, p2, outCurve, outName);
 found = true; break;
 }
 f.close( );
 }
 exitFlashReadLock( );
 return found;
}

bool StorageManager::bindDs18Identity(const uint8_t* rom, const char* hwId, const char* name, const CalibCurve& curve) {
 if (!rom || !hwId || hwId[0] == '\0') return false;
 char romHex[17];
 snprintf(romHex, sizeof(romHex), "%02X%02X%02X%02X%02X%02X%02X%02X",
          rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
 /* The board-serial row this sensor used while unpaired. Letter from the
  * channel table — a DS18B20 is temperature-only. */
 char oldId[20];
 snprintf(oldId, sizeof(oldId), "%c%s", channelInfo(CH_TEMP).letter, hwId);
 String picoUID = getBoardSerialNumber( );

 /* Version: the epoch when the clock is trustworthy, else one past the
  * stored version — this can run at boot, before NTP, and the commit gate
  * only accepts strictly newer. */
 long storedVer = getCalibrationVersion("/calib.csv");
 uint32_t newVer = (uint32_t)((storedVer > 0 ? storedVer : 0) + 1);

 char nameSan[32];
 safeCopy(nameSan, (name && name[0]) ? name : hwId, sizeof(nameSan));
 for (char* p = nameSan; *p; p++) { if (*p == ',' || *p == '"') *p = ' '; }

 enterFlashSafeMode( );
 File fout = LittleFS.open("/calib.tmp", "w");
 if (!fout) { exitFlashSafeMode( ); return false; }
 fout.printf("VERSION,%lu\n", (unsigned long)newVer);

 File fin = LittleFS.open("/calib.csv", "r");
 if (fin) {
 char lineBuf[320];
 while (fin.available( )) {
 feedWdt( );
 size_t len = fin.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 if (len == 0) continue;
 lineBuf[len] = '\0';
 if (len > 0 && lineBuf[len - 1] == '\r') lineBuf[len - 1] = '\0';
 if (lineBuf[0] == '\0') continue;
 if (strncmp(lineBuf, "VERSION,", 8) == 0) continue;

 char* p1 = strchr(lineBuf, ',');
 if (!p1) continue;
 *p1 = '\0';
 char* keyStr = lineBuf;
 char* idStr = p1 + 1;
 char* p2 = strchr(idStr, ',');
 if (p2) *p2 = '\0';

 const bool isRomRow = (strcasecmp(keyStr, romHex) == 0);
 const bool isOldRow = (strcasecmp(keyStr, picoUID.c_str( )) == 0
                        && strcasecmp(idStr, oldId) == 0);
 if (isRomRow || isOldRow) continue; /* replaced / migrated below */

 *p1 = ',';
 if (p2) *p2 = ',';
 fout.printf("%s\n", lineBuf);
 }
 fin.close( );
 }

 char line[352];
 if (calibRowFormat(line, sizeof(line), romHex, hwId, nameSan, curve) > 0) {
 fout.printf("%s\n", line);
 }
 fout.close( );
 exitFlashSafeMode( );

 return processCalibrationUpload( );
}

/**
 * @brief Boot-time recovery of a /calib.tmp left behind by a reset.
 *
 * processCalibrationUpload( ) only ever ran from the two web handlers, so a
 * reset landing between the write of calib.tmp and its rename stranded the
 * file: the calibration sat on flash but never took effect, and nothing on any
 * later boot collected it.
 *
 * Commits only a structurally complete file. Both writers terminate every row
 * with '\n', so a tmp that does not end in one is a truncated write — it gets
 * discarded rather than promoted over a good calib.csv. Anything that survives
 * that check goes through the normal version gate.
 */
bool StorageManager::recoverCalibrationTmp( ) {
 if (!LittleFS.exists("/calib.tmp")) return false;

 bool complete = false;
 enterFlashReadLock( );
 File f = LittleFS.open("/calib.tmp", "r");
 if (f) {
 size_t sz = f.size( );
 if (sz > 0 && f.seek(sz - 1)) complete = (f.read( ) == '\n');
 f.close( );
 }
 exitFlashReadLock( );

 if (!complete) {
 enterFlashSafeMode( );
 LittleFS.remove("/calib.tmp");
 exitFlashSafeMode( );
 LOG_CODE(LOG_WARN, "CFG", SEC_CONFIG_CHANGED, 0, "truncated calib.tmp discarded at boot");
 return false;
 }

 bool committed = processCalibrationUpload( );
 LOG_CODE(LOG_INFO, "CFG", SEC_CONFIG_CHANGED, 0,
          committed ? "orphan calib.tmp committed at boot"
                    : "orphan calib.tmp dropped at boot (version not newer)");
 return committed;
}

bool StorageManager::processCalibrationUpload( ) {
 if (!LittleFS.exists("/calib.tmp")) return false;
 long currentVer = getCalibrationVersion("/calib.csv");
 long newVer = getCalibrationVersion("/calib.tmp");

 enterFlashSafeMode( );
 if (newVer > currentVer) {
 LittleFS.remove("/calib.csv"); LittleFS.rename("/calib.tmp", "/calib.csv");
 exitFlashSafeMode( ); return true;
 } else {
 LittleFS.remove("/calib.tmp"); exitFlashSafeMode( ); return false;
 }
}

/**
 * @brief Simple SHA256 — returns 64-char hex digest.
 *
 * Mirrors the JavaScript SHA256 behavior of the frontend:
 * each character is treated as 1 byte by its Unicode code point
 * (Latin-1), not by UTF-8 encoding. This is relevant for
 * characters like ç (U+00E7): JS processes as byte 0xE7,
 * but UTF-8 encodes as 0xC3 0xA7 (2 bytes).
 */
String StorageManager::sha256Hex(const String& input) {
 br_sha256_context ctx;
 br_sha256_init(&ctx);

 /* Decode UTF-8 → code points → Latin-1 bytes (like JS charCodeAt). */
 const uint8_t* s = (const uint8_t*)input.c_str( );
 size_t len = input.length( );
 for (size_t i = 0; i < len; ) {
 uint8_t c = s[i];
 uint8_t byte;
 if (c < 0x80) {
 byte = c;
 i += 1;
 } else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
 /* 2-byte UTF-8 → code point 0x80–0x7FF (Latin-1 covers up to 0xFF) */
 byte = (uint8_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F));
 i += 2;
 } else {
 /* 3+ byte UTF-8 or invalid byte → skip (JS returns undefined for >255) */
 i += (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 1;
 continue;
 }
 br_sha256_update(&ctx, &byte, 1);
 }

 unsigned char hash[32];
 br_sha256_out(&ctx, hash);
 char hex[65];
 for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", hash[i]);
 hex[64] = '\0';
 return String(hex);
}

/**
 * @brief Core HMAC-SHA256 password hashing — explicit parameters.
 *
 * @param username Username (used as salt if salt==nullptr).
 * @param plainPassword SHA-256 hex of password (64 chars, from frontend).
 * @param salt Buffer with salt bytes (nullptr → derive from username).
 * @param saltLen Salt length in bytes.
 * @param rounds Number of HMAC-SHA256 iterations.
 * @param outputBytes Hash bytes to emit in hex (15 → 30 chars, 16 → 32 chars).
 * @return Hex hash string (outputBytes*2 chars).
 */
static String hashPasswordCore(const String& username, const String& plainPassword,
 const uint8_t* salt, size_t saltLen,
 uint16_t rounds, int outputBytes) {
 String pepper = StorageManager::getBoardSerialNumber( );
 String keyData = plainPassword + pepper;
 br_hmac_key_context kc; br_hmac_context ctx;
 br_hmac_key_init(&kc, &br_sha256_vtable, keyData.c_str( ), keyData.length( ));
 unsigned char currentHash[32];
 br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, salt, saltLen); br_hmac_out(&ctx, currentHash);
 for (int r = 0; r < rounds; r++) {
 if (r % 50 == 0) watchdog_update( );
 br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, currentHash, 32); br_hmac_out(&ctx, currentHash);
 }

 char hashHex[65];
 for (int i = 0; i < outputBytes; i++) snprintf(hashHex + (i * 2), 3, "%02x", currentHash[i]);
 hashHex[outputBytes * 2] = '\0';
 return String(hashHex);
}

String StorageManager::hashPassword(const String& username, const String& plainPassword) {
 String saltStr = username; saltStr.toLowerCase( );
 return hashPasswordCore(username, plainPassword,
 (const uint8_t*)saltStr.c_str( ), saltStr.length( ),
 PASSWORD_HMAC_ROUNDS, 16);
}

String StorageManager::hashPasswordLegacy(const String& username, const String& plainPassword) {
 String saltStr = username; saltStr.toLowerCase( );
 return hashPasswordCore(username, plainPassword,
 (const uint8_t*)saltStr.c_str( ), saltStr.length( ),
 2500, 15);
}

String StorageManager::hashPasswordV1(const String& username, const String& plainPassword,
 const uint8_t* userSalt) {
 return hashPasswordCore(username, plainPassword,
 userSalt, 8, PASSWORD_HMAC_ROUNDS, 16);
}

void StorageManager::generateSalt(uint8_t* buf) {
 for (int i = 0; i < 8; i += 4) {
 uint32_t r = rp2040.hwrand32( );
 memcpy(buf + i, &r, (8 - i >= 4) ? 4 : (8 - i));
 }
}

/* ============================================================================
 * V4 HISTORY — write, scan, build schema
 * ============================================================================ */

/* ── V4 entry points, kept as the delegating shims §2 asks for ───────────
 * R7 is explicit: the firmware carries no reader for the legacy format. The
 * V4 codec is gone from the build, so these keep their signatures and do the
 * V5 thing instead. Callers — the CLI, the web rebind endpoint — did not have
 * to change, and what they get back is better than what they asked for. */

void StorageManager::ensureV4Schema( ) {
	ensureH5Schema( );
}

bool StorageManager::rebindV4Schema(uint8_t *outMeasures) {
	/* Was DESTRUCTIVE: a .sim4 froze its schema in the file header, so
	 * changing a sensor identity meant recreating the day's file and losing
	 * every record already in it — which is why the CLI demanded `confirm`.
	 *
	 * V5 has no such trade. A schema change writes a new SCHEMA chunk into
	 * the same file (§3.7-2) and the blocks before it stay readable under
	 * the schema that was in force when they were written. Nothing is lost,
	 * so there is nothing to confirm and nothing to force. */
	onSensorSetChangedV5( );
	if (outMeasures) *outMeasures = _h5NCh;
	return _h5Valid;
}

bool StorageManager::migrateV4Schema(uint8_t *outMeasures, uint32_t *outRecords,
                                     uint8_t *outCarried) {
	/* The streaming rewrite this used to do — read the day, remap columns,
	 * verify, swap — exists to carry records across a schema change the
	 * format could not express. V5 expresses it, so the migration is the
	 * schema change itself: every record is "carried" because none moves. */
	const bool ok = rebindV4Schema(outMeasures);
	if (outRecords) *outRecords = 0;
	if (outCarried) *outCarried = _h5NCh;
	return ok;
}

bool StorageManager::writeHistoryEntryV4(const int64_t *values, uint8_t measureCount,
                                        uint32_t epoch) {
	/* Signature preserved, semantics moved: the V4 record was a measurement
	 * vector of int64 scaled by each channel's own factor, and V5 wants the
	 * int16 the schema declares. Nothing in the firmware calls this any
	 * more — processHistoryLogging builds the V5 vector directly — so it
	 * refuses rather than guessing a mapping between two schemas it cannot
	 * see. Kept so no caller outside this tree stops compiling. */
	(void)values; (void)measureCount; (void)epoch;
	return false;
}

bool StorageManager::flushHistoryBatchIfDue( ) {
	/* T2.1's RAM batch existed because V4 wrote to flash once per sample and
	 * a touch in progress could not afford the lockout. V5's hot path never
	 * touches flash, so there is no batch to drain — what these two persist
	 * now is the open block's .wip snapshot. */
	return flushHistoryBatch( );
}

bool StorageManager::flushHistoryBatch( ) {
	if (!_isMounted) return false;
	if (!_h5Valid || _h5Enc.count( ) == 0) return true;
	return flushWipV5( );
}

bool StorageManager::writeHistoryEntryFlashV4(const int64_t *values, uint8_t measureCount,
                                              uint32_t epoch) {
	(void)values; (void)measureCount; (void)epoch;
	return false;                       /* see writeHistoryEntryV4 */
}

String StorageManager::getHistoryFileNameV5( ) {
	return getHistoryFileNameV5((uint32_t)time(nullptr));
}

String StorageManager::getHistoryFileNameV5(uint32_t epoch) {
	time_t t = (time_t)epoch;
	struct tm timeinfo;
	localtime_r(&t, &timeinfo);
	char buff[42];
	snprintf(buff, sizeof(buff), "%s/%04d%02d%02d" HISTORY_FILE_EXT, DIR_HISTORY,
	         timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
	return String(buff);
}

uint8_t StorageManager::buildH5Schema(H5ChannelDesc* out, uint8_t cap) const {
	uint8_t n = 0;
	for (int slot = 0; slot < MAX_SENSORS && n < cap; slot++) {
		const auto &sr = _currentConfig.sensors[slot];
		if (!sr.active) continue;
		for (uint8_t ch = 0; ch < MAX_SENSOR_CHANNELS && n < cap; ch++) {
			if (!sensorHasChannel((SensorType)sr.sensorType, ch)) continue;
			const ChannelInfo &ci = channelInfo(ch);

			/* scaleExp is the base-10 exponent of the channel table's scale:
			 * x100 -> -2, x10 -> -1. Written into the SCHEMA chunk so a
			 * reader needs nothing but the file to get real units back. */
			int8_t exp = 0;
			for (uint32_t s = ci.scale; s >= 10; s /= 10) exp--;

			uint8_t kind;
			switch (ch) {
				case CH_TEMP:  kind = H5_KIND_TEMP_C;    break;
				case CH_HUM:   kind = H5_KIND_HUM_PCT;   break;
				case CH_PRESS: kind = H5_KIND_PRESS_HPA; break;
				default:
					/* V5 values are int16. A channel wider than that — lux is
					 * 24-bit at x100 — keeps its channel but drops to whole
					 * units, which is the honest reading of a 16-bit slot. */
					kind = H5_KIND_GENERIC;
					if (ci.bitWidth > 16) exp = 0;
					break;
			}

			/* slot*4 + channel: unique across the device, never recycled
			 * while the sensor keeps its slot. Moving a sensor to another
			 * GPIO is a reconfiguration, and §3.7-2 answers that with a new
			 * SCHEMA chunk rather than a reused id. */
			out[n].id       = (uint8_t)(slot * MAX_SENSOR_CHANNELS + ch);
			out[n].kind     = kind;
			out[n].scaleExp = exp;
			out[n].flags    = 0;
			n++;
		}
	}
	return n;
}

void StorageManager::ensureH5Schema( ) {
	H5ChannelDesc want[H5_MAX_CHANNELS];
	const uint8_t n = buildH5Schema(want, H5_MAX_CHANNELS);
	if (n == 0) { _h5Valid = false; return; }
	if (_h5Valid && h5SchemaEquals(_h5Schema, _h5NCh, want, n)) return;  /* §14-7 */

	if (_h5Valid) onSensorSetChangedV5( );      /* seals the old block first */
	memcpy(_h5Schema, want, (size_t)n * sizeof(H5ChannelDesc));
	_h5NCh = n;
	_h5Valid = true;
	_h5Enc.begin(_h5Schema, _h5NCh, h5NominalSeconds(getHistoryIntervalMin( )));
}

bool StorageManager::writeHistoryEntryV5(const int16_t* values, uint8_t nCh, uint32_t epoch) {
	if (!_isMounted) return false;
	if (epoch < HIST_EPOCH_MIN) return false;
	{
		const uint32_t nowEpoch = (uint32_t)time(nullptr);
		if (nowEpoch > HIST_EPOCH_MIN && epoch > nowEpoch + 86400UL) return false;
	}
	if (!_h5Valid) ensureH5Schema( );
	if (!_h5Valid || nCh != _h5NCh) return false;

	/* A block never spans two day files: the file is chosen from the
	 * block's own t0, so a block open across midnight would put today's
	 * records in yesterday's file. Same trap A2 fixed for the V4 batch. */
	const String day = getHistoryFileNameV5(epoch);
	if (_h5Enc.count( ) && day != _h5CurrentDay) {
		if (!sealHourV5(true)) {                /* §14-6 */
			/* The seal failed, so the old day's block is still in RAM and
			 * still belongs to the old day. Adopting `day` here — which is
			 * what this did — let the add() below splice today's record into
			 * yesterday's block, and the whole block then got filed under
			 * today: §14-6 broken, and "the file name IS the bound" with it.
			 * Silently, too: nothing in that path reported an error. */
			if (++_h5SealFails < H5_SEAL_MAX_FAILS) {
				LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, (int)_h5SealFails,
				         "h5_rollover_seal_retry");
				return false;   /* keep the old bound AND the block intact */
			}
			LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, (int)_h5Enc.count( ),
			         "h5_rollover_seal_lost");
			/* Written off. Empty the encoder so the new day starts clean
			 * rather than inheriting yesterday's t0. */
			_h5Enc.begin(_h5Schema, _h5NCh, h5NominalSeconds(getHistoryIntervalMin( )));
			_h5WipDirty = false;
		}
		_h5SealFails = 0;
	}
	_h5CurrentDay = day;

	if (_h5Enc.count( ) == 0) {
		_h5Enc.reset(epoch, values);
		_h5WipDirty = true;
		flushWipUnlessBlocked( );
		return true;
	}
	if (_h5Enc.add(epoch, values)) {
		_h5WipDirty = true;
		flushWipUnlessBlocked( );
		return true;
	}

	/* Full, or the clock moved past what RAW can address from t0. Either
	 * way this block is done; the sample opens the next one.
	 *
	 * This seal runs even with a gate closed, and so does the day-rollover
	 * one above: a full block cannot take another record, so the choice is
	 * a lockout window or a lost sample. It costs 24 forced windows a day
	 * against R8's promise that none are lost. */
	const bool wasFull = _h5Enc.full( );
	if (!sealHourV5(!wasFull)) {
		/* The reset below empties the encoder unconditionally, so a failure
		 * here used to discard the whole held block — up to 60 records — with
		 * nothing said beyond sealHourV5's generic write warning. And unlike
		 * the two paths above this one fires every hour, so it is the seal
		 * most likely to ever fail. Refuse the record while the block still
		 * has retries left; the block is what is worth protecting. */
		if (++_h5SealFails < H5_SEAL_MAX_FAILS) {
			LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, (int)_h5SealFails,
			         "h5_seal_retry");
			return false;
		}
		LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, (int)_h5Enc.count( ),
		         "h5_seal_lost");
	}
	_h5SealFails = 0;
	_h5Enc.reset(epoch, values);
	_h5WipDirty = true;
	flushWipUnlessBlocked( );
	return true;
}

/* The snapshot is the only part of the record path that touches flash, and
 * the two gates exist to keep flash off Core 1's back mid-gesture and out of
 * a heavy task's way. Deferring it — rather than skipping the whole sample,
 * which is what the loop used to do — keeps the measurement and moves only
 * the write. h5WipPending( ) is what gets it written moments later. */
void StorageManager::flushWipUnlessBlocked( ) {
	if (!_h5WipDirty) return;
	if (isHeavyTaskLocked( ) || TouchPriority::isActive( )) return;
	flushWipV5( );
}

bool StorageManager::h5WriteSchemaTo(File& f, uint8_t seq) {
	uint8_t buf[H5_SCHEMA_CHUNK_SIZE(H5_MAX_CHANNELS)];
	const size_t n = h5BuildSchemaChunk(buf, sizeof(buf), _h5Schema, _h5NCh, seq);
	if (n == 0) return false;
	return f.write(buf, n) == n;
}

bool StorageManager::h5FileHasSchema(const String& path, bool* outMatches,
                                     uint8_t* outLastSeq) {
	if (outMatches) *outMatches = false;
	if (outLastSeq) *outLastSeq = 0;
	File f = LittleFS.open(path, "r");
	if (!f) return false;
	const uint32_t size = (uint32_t)f.size( );

	/* Two different questions, and they have two different answers.
	 *
	 * "Is this a V5 file at all" is about the FIRST chunk — a file that does
	 * not open with a SCHEMA has nothing to interpret its blocks against, and
	 * the caller deletes it. But "does the schema match" is about the one in
	 * FORCE, which is the LAST SCHEMA in the file (§3.7-2: a set change adds a
	 * SCHEMA, it does not rewrite the opening one).
	 *
	 * Comparing against the opening one made §14-7 unreachable: this bench
	 * file was created with 6 channels and the equipment now runs 11, so the
	 * comparison could never match and every single append wrote a fresh
	 * SCHEMA first — 36 of them for 36 blocks, 54 B each against blocks of
	 * 46 B, roughly a fifth of the daily byte budget spent restating what the
	 * previous chunk already said. */
	uint8_t buf[H5_DATA_HEADER_SIZE(H5_MAX_CHANNELS)];
	static_assert(sizeof(buf) >= H5_SCHEMA_CHUNK_SIZE(H5_MAX_CHANNELS),
	              "buffer must hold the widest chunk header");

	bool opensWithSchema = false, first = true;
	uint32_t pos = 0;
	while (pos + sizeof(H5ChunkPreamble) <= size) {
		size_t want = sizeof(buf);
		if (pos + want > size) want = size - pos;
		if (!f.seek(pos, SeekSet)) break;
		const int got = f.read(buf, want);
		if (got < (int)sizeof(H5ChunkPreamble)) break;

		H5ChunkPreamble pre;
		memcpy(&pre, buf, sizeof(pre));
		if (pre.magic != H5_MAGIC || pre.version != H5_VERSION) break;

		if (pre.type == H5_CHUNK_SCHEMA) {
			const uint8_t n = pre.a;
			if (n == 0 || n > H5_MAX_CHANNELS) break;
			const size_t sz = H5_SCHEMA_CHUNK_SIZE(n);
			if ((size_t)got < sz) break;
			uint16_t stored;
			memcpy(&stored, buf + 8 + 4u * n, 2);
			if (h5Crc16(buf, 8 + 4u * n) != stored) break;
			if (first) opensWithSchema = true;
			/* Keep overwriting: what survives the walk is the last one. */
			if (outMatches) {
				*outMatches = h5SchemaEquals((const H5ChannelDesc*)(buf + 8), n,
				                             _h5Schema, _h5NCh);
			}
			if (outLastSeq) *outLastSeq = pre.b;
			pos += sz;
		} else if (pre.type == H5_CHUNK_DATA) {
			if (first) break;                   /* §3: files open with SCHEMA */
			const uint8_t n = pre.b;
			if (n == 0 || n > H5_MAX_CHANNELS) break;
			const size_t hdrLen = H5_DATA_HEADER_SIZE(n);
			if ((size_t)got < hdrLen) break;
			uint16_t payloadLen;
			memcpy(&payloadLen, buf + 12, sizeof(payloadLen));
			pos += hdrLen + payloadLen;
		} else {
			break;
		}
		first = false;
		feedWdt( );
	}
	f.close( );
	/* A walk that stops on a damaged tail still answers the first question,
	 * and leaves outMatches on the last SCHEMA it could trust — at worst one
	 * redundant SCHEMA gets written, which is the safe way to be wrong. */
	return opensWithSchema;
}

/** Sink that appends to an open File, for HistoryV5Encoder::sealStream. */
static bool h5FileSink(void* ctx, const uint8_t* data, size_t len) {
	File* f = (File*)ctx;
	return f->write(data, len) == len;
}

bool StorageManager::h5AppendChunk(const String& path, uint8_t extraFlags) {
	LogManager::TraceScope _tr(0, MOD_HIST_FLASH);
	LogManager::WdtWindow _wdt(30000);
	Core1FlashPause _c1(this);

	bool exists = false, schemaOk = false, schemaMatches = false;
	uint8_t lastSeq = 0;
	FLASH_OP({ exists = LittleFS.exists(path); });
	if (exists) {
		FLASH_OP({ schemaOk = h5FileHasSchema(path, &schemaMatches, &lastSeq); });
		if (!schemaOk) {
			/* A file whose opening SCHEMA does not parse is not a V5 file.
			 * Appending to it would bury good blocks behind garbage, and
			 * every reader would stop at byte 0 anyway. */
			LOG_CODE(LOG_WARN, "STO", STO_SCHEMA_MISMATCH, 0, "h5_no_schema");
			FLASH_OP(LittleFS.remove(path));
			exists = false;
		}
	}
	if (!exists) {
		FLASH_OP(enforceStorageLimit( ));
		bool ok = false;
		FLASH_OP({
			File f = LittleFS.open(path, "w");
			if (f) { ok = h5WriteSchemaTo(f, 0); f.close( ); }
		});
		if (!ok) {
			LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "h5_schema_write");
			return false;
		}
		_h5SchemaSeq = 0;
	} else if (!schemaMatches) {
		/* §3.7-2: the set changed since this file opened. A second SCHEMA
		 * in the same file keeps the day intact — the blocks before it stay
		 * readable under the schema that was in force when they were made. */
		/* Numbered from the file, not from the member: _h5SchemaSeq starts at
		 * zero on every boot, so a device that reboots twice in a day used to
		 * write three different schemas all claiming seq 1. */
		bool ok = false;
		FLASH_OP({
			File f = LittleFS.open(path, "a");
			if (f) { ok = h5WriteSchemaTo(f, (uint8_t)(lastSeq + 1)); f.close( ); }
		});
		if (!ok) {
			LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "h5_schema_append");
			return false;
		}
		_h5SchemaSeq = (uint8_t)(lastSeq + 1);
		LOG_CODE(LOG_INFO, "STO", STO_SCHEMA_MISMATCH, (int)_h5NCh, "h5_new_schema");
	}

	size_t written = 0;
	FLASH_OP({
		File f = LittleFS.open(path, "a");
		if (f) {
			written = _h5Enc.sealStream(h5FileSink, &f, extraFlags);
			f.close( );
		}
	});
	if (written == 0) {
		LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "h5_seal");
		return false;
	}
	_storageDirty = true;
	return true;
}

bool StorageManager::sealHourV5(bool partial) {
	if (!_isMounted || !_h5Valid || _h5Enc.count( ) == 0) return true;

	/* No scope of its own until 2026-08-16, so a stall here traced as whatever
	 * the caller had set. It matters: past the append (which h5AppendChunk
	 * marks) this erases the .wip, and an erase is the slowest flash op there
	 * is. h5AppendChunk's own scope still wins while it runs — RAII restores
	 * this one after. */
	LogManager::TraceScope _tr(0, MOD_HIST_SEAL);

	const String path = _h5CurrentDay.length( ) ? _h5CurrentDay
	                                            : getHistoryFileNameV5(_h5Enc.t0( ));
	const uint8_t flags = partial ? H5_FLAG_PARTIAL : 0;
	const uint8_t records = _h5Enc.count( );
	const bool ok = h5AppendChunk(path, flags);
	if (ok) {
		LOG_CODE(LOG_INFO, "STO", STO_H5_SEALED, (int)records, "");
		/* The .wip only ever holds the block still open. Once that block is
		 * on flash, a stale snapshot would be replayed on the next boot.
		 *
		 * The pause is its own scope rather than the whole function:
		 * h5AppendChunk already pauses for its own writes and releases on
		 * return, so this remove — an erase burst like any other — would
		 * otherwise run with Core 1 back in XIP. Keeping the scope tight
		 * leaves the append's lockout duty cycle where T1.4 tuned it. */
		{
			Core1FlashPause _c1(this);
			FLASH_OP({ if (LittleFS.exists(FILE_H5_WIP)) LittleFS.remove(FILE_H5_WIP); });
		}
		_h5Enc.begin(_h5Schema, _h5NCh, h5NominalSeconds(getHistoryIntervalMin( )));
		/* The records the .wip was covering are in the day file now, and the
		 * block that replaces it is empty — nothing left to snapshot. */
		_h5WipDirty = false;
		/* Cleared here so every caller's patience resets on a seal that
		 * worked, not just the two in writeHistoryEntryV5 that count. */
		_h5SealFails = 0;
	}
	return ok;
}

uint8_t StorageManager::h5ClockFlag( ) const {
	/* Records which clock stamped the open block. At the next boot the
	 * snapshot's t0 is the newest time that exists, but whether it can be
	 * believed depends on where it came from — and this is the last moment
	 * that knows. No callback reads as "provisional", so a build that forgets
	 * to wire it gets the strict seed gate rather than a free pass. */
	return (_clockTrustedCb && _clockTrustedCb( )) ? H5_FLAG_CLOCK_SYNCED : 0u;
}

bool StorageManager::flushWipV5( ) {
	if (!_isMounted || !_h5Valid) return true;
	/* Nothing open to snapshot: the block was just sealed, and sealHourV5
	 * already removed the .wip. Clearing the flag here keeps the loop sweep
	 * from retrying a write with no content for the rest of the minute. */
	if (_h5Enc.count( ) == 0) { _h5WipDirty = false; return true; }

	/* Own module, not MOD_HIST_FLASH: that one belongs to the record write.
	 * Sharing it meant the snapshot and the write were indistinguishable in
	 * an autopsy, and they fail for different reasons — this one rewrites the
	 * whole block every time, the other appends one record. */
	LogManager::TraceScope _tr(0, MOD_HIST_WIP);
	LogManager::WdtWindow _wdt(30000);
	Core1FlashPause _c1(this);

	/* Written whole every time, never appended: the snapshot has to be
	 * either the current block or nothing. A half-updated .wip that still
	 * passed CRC would replay a block that never existed. */
	const uint8_t clockFlag = h5ClockFlag( );
	size_t written = 0;
	FLASH_OP({
		File f = LittleFS.open(FILE_H5_WIP, "w");
		if (f) {
			written = _h5Enc.sealStream(h5FileSink, &f,
			                            (uint8_t)(H5_FLAG_PARTIAL | clockFlag));
			f.close( );
		}
	});
	if (written == 0) {
		LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "h5_wip");
		return false;                       /* stays dirty; the sweep retries */
	}
	_h5WipDirty = false;
	LOG_CODE(LOG_INFO, "STO", STO_H5_WIP, (int)_h5Enc.count( ), "");
	return true;
}

size_t StorageManager::h5StreamOpenBlock(H5WriteFn sink, void* ctx) {
	if (!_h5Valid || !sink || _h5Enc.count( ) == 0) return 0;

	/* The schema seq is the one the block would land with, so a reader that
	 * tracks schema changes sees the same number here and in the day file. */
	uint8_t schemaBuf[H5_SCHEMA_CHUNK_SIZE(H5_MAX_CHANNELS)];
	const size_t sn = h5BuildSchemaChunk(schemaBuf, sizeof(schemaBuf),
	                                     _h5Schema, _h5NCh, _h5SchemaSeq);
	if (sn == 0 || !sink(ctx, schemaBuf, sn)) return 0;

	/* Same block, same provenance as the .wip on flash — the two renderings of
	 * one open block must not disagree about where their timestamps came from. */
	const size_t dn = _h5Enc.sealStream(sink, ctx,
	                                    (uint8_t)(H5_FLAG_PARTIAL | h5ClockFlag( )));
	return dn ? sn + dn : 0;
}

void StorageManager::recoverWipV5( ) {
	if (!_isMounted) return;

	/* The pause covers the whole function, not just the append. The .wip is
	 * removed on EVERY path out of here — including the ones that never
	 * decode it — and a remove is an erase burst, which the FLASH_OP warning
	 * above says must never run with Core 1 loose in XIP. The pause used to
	 * sit inside the decode branch, leaving that final remove unprotected;
	 * the QSPI wedge that follows is a dead hang rather than a reboot,
	 * because setup( ) runs before main.cpp arms the watchdog. */
	LogManager::WdtWindow _wdt(30000);
	Core1FlashPause _c1(this);

	bool exists = false;
	FLASH_OP({ exists = LittleFS.exists(FILE_H5_WIP); });
	if (!exists) return;

	/* The snapshot is exactly one sealed DATA chunk, so validating it is
	 * the ordinary decoder path — no special case, no repair attempt
	 * (§14-4: never try to fix a chunk). */
	size_t len = 0;
	bool read = false;
	FLASH_OP({
		File f = LittleFS.open(FILE_H5_WIP, "r");
		if (f) {
			const int got = f.read(_h5Chunk, sizeof(_h5Chunk));
			if (got > 0) { len = (size_t)got; read = true; }
			f.close( );
		}
	});

	bool adopted = false;
	if (read && len >= sizeof(H5DataHeader)) {
		ensureH5Schema( );
		const H5DataHeader* h = (const H5DataHeader*)_h5Chunk;
		HistoryV5Decoder dec;
		if (_h5Valid && dec.begin(_h5Chunk, len, _h5Schema, _h5NCh)) {
			const String path = getHistoryFileNameV5(h->t0);
			bool schemaOk = false, matches = false, fileExists = false;
			uint8_t lastSeq = 0;
			FLASH_OP({ fileExists = LittleFS.exists(path); });
			if (fileExists) {
				FLASH_OP({ schemaOk = h5FileHasSchema(path, &matches, &lastSeq); });
			}
			bool ready = fileExists && schemaOk && matches;
			if (!ready) {
				FLASH_OP({
					File f = LittleFS.open(path, fileExists && schemaOk ? "a" : "w");
					if (f) {
						ready = h5WriteSchemaTo(f, fileExists && schemaOk
						                        ? (uint8_t)(lastSeq + 1) : 0);
						_h5SchemaSeq = (fileExists && schemaOk) ? (uint8_t)(lastSeq + 1) : 0;
						f.close( );
					}
				});
			}
			if (ready) {
				FLASH_OP({
					File f = LittleFS.open(path, "a");
					if (f) { adopted = (f.write(_h5Chunk, len) == len); f.close( ); }
				});
			}
		}
	}

	FLASH_OP({ LittleFS.remove(FILE_H5_WIP); });
	LOG_CODE(adopted ? LOG_INFO : LOG_WARN, "STO", STO_H5_WIP,
	         adopted ? (int)((const H5DataHeader*)_h5Chunk)->pre.a : -1,
	         adopted ? "wip_adopted" : "wip_discarded");
	if (adopted) {
		_storageDirty = true;
		/* Remember it so the NTP shift leaves it alone: its records were
		 * stamped by the previous session's clock and are already correct. */
		_h5AdoptedT0 = ((const H5DataHeader*)_h5Chunk)->t0;
	}
}

void StorageManager::onSensorSetChangedV5( ) {
	/* §3.7-2. The seal is PARTIAL by definition: the block is closing for a
	 * reason other than being full. h5AppendChunk writes the new SCHEMA
	 * when it sees the file's opening one no longer matches. */
	if (_h5Valid && _h5Enc.count( )) {
		const uint8_t held = _h5Enc.count( );
		/* Nothing can rescue the block if this fails, and it has to be said
		 * out loud. ensureH5Schema( ) below re-runs _h5Enc.begin( ), which
		 * drops whatever block is in progress — so a failed seal here used to
		 * discard up to 60 records with no trace but a generic write warning.
		 * The .wip is no escape either: it would carry the OLD schema, and
		 * recoverWipV5( ) validates against the compiled one, so the next boot
		 * would reject it. Retrying once covers a transient mutex timeout,
		 * which is the only failure that a retry can actually fix. */
		if (!sealHourV5(true) && !sealHourV5(true)) {
			LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, (int)held,
			         "h5_schema_change_seal_lost");
		}
	}
	_h5Valid = false;
	ensureH5Schema( );
}

int32_t StorageManager::shiftHistoryTimeV5(int32_t deltaS, const String& path,
                                           uint32_t fromEpoch) {
	if (!_isMounted || deltaS == 0) return 0;
	const String src = path.length( ) ? path : getHistoryFileNameV5( );
	const String tmp = src + ".tmp";

	LogManager::TraceScope _tr(0, MOD_HIST_FLASH);
	LogManager::WdtWindow _wdt(30000);
	Core1FlashPause _c1(this);

	bool exists = false;
	FLASH_OP({ exists = LittleFS.exists(src); });
	if (!exists) return 0;

	/* Only t0 moves: SCHEMA carries no time and a block's interior is
	 * relative to its own t0 (§7.3). LittleFS has no dependable partial
	 * overwrite, so the file is streamed to .tmp and renamed — the same
	 * shape the config write uses. */
	int32_t blocks = 0;
	bool ok = false;
	FLASH_OP({
		File in = LittleFS.open(src, "r");
		File out = LittleFS.open(tmp, "w");
		if (in && out) {
			ok = true;
			for (;;) {
				H5ChunkPreamble pre;
				const int got = in.read((uint8_t*)&pre, sizeof(pre));
				if (got <= 0) break;
				if (got < (int)sizeof(pre) || pre.magic != H5_MAGIC
				    || pre.version != H5_VERSION) { ok = false; break; }

				if (pre.type == H5_CHUNK_SCHEMA) {
					const size_t sz = H5_SCHEMA_CHUNK_SIZE(pre.a);
					memcpy(_h5Chunk, &pre, sizeof(pre));
					if (in.read(_h5Chunk + sizeof(pre), sz - sizeof(pre))
					    != (int)(sz - sizeof(pre))) { ok = false; break; }
					if (out.write(_h5Chunk, sz) != sz) { ok = false; break; }
					continue;
				}
				if (pre.type != H5_CHUNK_DATA) { ok = false; break; }

				const uint8_t n = pre.b;
				if (n == 0 || n > H5_MAX_CHANNELS) { ok = false; break; }
				const size_t hdrLen = H5_DATA_HEADER_SIZE(n);
				memcpy(_h5Chunk, &pre, sizeof(pre));
				if (in.read(_h5Chunk + sizeof(pre), hdrLen - sizeof(pre))
				    != (int)(hdrLen - sizeof(pre))) { ok = false; break; }
				H5DataHeader* h = (H5DataHeader*)_h5Chunk;
				const size_t total = hdrLen + h->payloadLen;
				if (total > sizeof(_h5Chunk)) { ok = false; break; }
				if (h->payloadLen
				    && in.read(_h5Chunk + hdrLen, h->payloadLen) != (int)h->payloadLen) {
					ok = false; break;
				}
				/* fromEpoch is the provisional base, which is only as good as
				 * the seed behind it; _h5AdoptedT0 names the block this boot
				 * inherited from the previous session outright, so a seed that
				 * came out low cannot drag it. See recoverWipV5( ). */
				const bool inherited = (_h5AdoptedT0 && h->t0 == _h5AdoptedT0);
				if (h->t0 >= fromEpoch && !inherited) {
					h->t0 = (uint32_t)((int32_t)h->t0 + deltaS);
					h->crc16 = 0;
					uint16_t crc = h5Crc16(_h5Chunk, 14);
					crc = h5Crc16(_h5Chunk + 16, total - 16, crc);
					memcpy(_h5Chunk + 14, &crc, 2);
					blocks++;
				}
				if (out.write(_h5Chunk, total) != total) { ok = false; break; }
				feedWdt( );
			}
		}
		if (in) in.close( );
		if (out) out.close( );
	});

	if (!ok) {
		FLASH_OP({ LittleFS.remove(tmp); });
		LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "h5_shift");
		return -1;
	}
	bool renamed = false;
	FLASH_OP({
		LittleFS.remove(src);
		renamed = LittleFS.rename(tmp, src);
	});
	if (!renamed) { FLASH_OP({ LittleFS.remove(tmp); }); return -1; }

	/* The block still open in RAM has to move with the file it will join. */
	_h5Enc.shiftTime(deltaS);
	_storageDirty = true;
	return blocks;
}

uint16_t StorageManager::purgeNonV5History( ) {
	if (!_isMounted) return 0;
	uint16_t removed = 0;

	/* §11: nothing in /history that is not V5 survives the update. .sim4
	 * files, half-written junk, and anything a user dropped in there — the
	 * firmware carries no reader for any of it, so leaving them costs flash
	 * the retention policy needs and makes the rotation scan lie. */
	LogManager::WdtWindow _wdt(30000);
	Core1FlashPause _c1(this);

	for (;;) {
		String victim = "";
		FLASH_OP({
			Dir dir = LittleFS.openDir(DIR_HISTORY);
			while (dir.next( )) {
				feedWdt( );
				const String fn = dir.fileName( );
				if (fn == FS_DIR_NOTE_NAME) continue;
				if (fn.endsWith(HISTORY_FILE_EXT)) continue;
				if (fn == ".wip") continue;     /* handled by recoverWipV5 */
				victim = fn;
				break;
			}
		});
		if (victim.length( ) == 0) break;
		FLASH_OP({ LittleFS.remove(String(DIR_HISTORY) + "/" + victim); });
		removed++;
		if (removed > 400) break;               /* refuse to spin forever */
	}

	if (removed) {
		_storageDirty = true;
		_h5PurgedLegacy = removed;
		LOG_CODE(LOG_WARN, "STO", STO_LEGACY_PURGED, (int)removed, "");
	}
	return removed;
}

/* ── V5 sequential reader ─────────────────────────────────────────────────
 * One reader, shared by the web graph, CSV export, telemetry and the TFT
 * graph. Under V4 each of those carried its own ~2.8 KiB HistV4State and
 * its own copy of the decode loop; four copies of a loop is how the refill
 * bug (A1) came to exist in five places at once. */

static int h5FileRead(void* ctx, uint32_t off, uint8_t* buf, size_t len) {
	File* f = (File*)ctx;
	/* The scanner walks forward, so the cursor is usually already where the
	 * next read wants it. A LittleFS seek is not free — it can walk the
	 * file's index — and skipping the redundant one is most of the cost of
	 * a header walk over a month of blocks. */
	if ((uint32_t)f->position( ) != off && !f->seek(off, SeekSet)) return -1;
	return f->read(buf, len);
}

bool StorageManager::h5OpenDay(const String& path, bool verifyPayload) {
	h5CloseDay( );
	_h5RdFile = LittleFS.open(path, "r");
	if (!_h5RdFile) return false;
	/* The scanner never verifies here, whatever the caller asked for: a
	 * payload CRC costs a walk of the whole chunk in 64 B reads, and the
	 * decode path is about to read those same bytes anyway. The check moves
	 * to h5LoadNextBlock( ), over the copy in RAM — §3.7-4 unchanged, since
	 * a chunk that fails it is still never decoded. */
	_h5RdVerify = verifyPayload;
	_h5Scan.begin(h5FileRead, &_h5RdFile, (uint32_t)_h5RdFile.size( ), false);
	_h5RdSchema = nullptr;
	_h5RdNCh = 0;
	_h5RdBlockOpen = false;

	/* Every file opens with a SCHEMA (§3); without one there is nothing to
	 * interpret the DATA against and the file is rejected whole. */
	uint8_t seq = 0;
	if (!_h5Scan.nextSchema(nullptr, _h5RdNCh, seq)) { h5CloseDay( ); return false; }
	_h5RdSchema = _h5Scan.schema( );
	return true;
}

void StorageManager::h5CloseDay( ) {
	if (_h5RdFile) _h5RdFile.close( );
	_h5RdSchema = nullptr;
	_h5RdNCh = 0;
	_h5RdBlockOpen = false;
}

bool StorageManager::h5NextBlock(H5DataHeader& hdr, const int16_t*& mn, const int16_t*& mx) {
	for (;;) {
		const H5ScanChunk t = _h5Scan.next( );
		if (t == H5_SCAN_END) return false;
		if (t == H5_SCAN_SCHEMA) {
			/* A schema change mid-file re-points the reader; records after
			 * it mean something different from records before it (R5). */
			_h5RdSchema = _h5Scan.schema( );
			_h5RdNCh = _h5Scan.nCh( );
			continue;
		}
		if (_h5Scan.header( ).pre.b != _h5RdNCh) {
			LOG_CODE(LOG_WARN, "STO", STO_SCHEMA_MISMATCH,
			         (int)_h5Scan.header( ).pre.b, "h5_data_nch");
			continue;                                    /* §3.7-3 */
		}
		hdr = _h5Scan.header( );
		mn = _h5Scan.chMin( );
		mx = _h5Scan.chMax( );
		return true;
	}
}

bool StorageManager::h5DecodeNext(uint32_t& epoch, int16_t* v) {
	if (_h5RdBlockOpen && _h5Dec.next(epoch, v)) return true;
	_h5RdBlockOpen = false;
	return false;
}

bool StorageManager::h5LoadNextBlock( ) {
	for (;;) {
		H5DataHeader hdr;
		const int16_t *mn, *mx;
		if (!h5NextBlock(hdr, mn, mx)) return false;

		size_t len = 0;
		if (!_h5Scan.readChunk(_h5Chunk, sizeof(_h5Chunk), len, _h5RdVerify)) continue;
		if (!_h5Dec.begin(_h5Chunk, len, _h5RdSchema, _h5RdNCh)) continue;
		_h5RdBlockOpen = true;
		return true;
	}
}

bool StorageManager::h5NextRecord(uint32_t& epoch, int16_t* v) {
	for (;;) {
		if (h5DecodeNext(epoch, v)) return true;
		if (!h5LoadNextBlock( )) return false;
	}
}

bool StorageManager::h5SeekTo(uint32_t epoch) {
	_h5RdBlockOpen = false;
	if (!_h5Scan.seek(epoch)) return false;
	_h5RdSchema = _h5Scan.schema( );
	_h5RdNCh = _h5Scan.nCh( );
	return true;
}

