/**
 * @file AppManager_Commands.cpp
 * @brief CLI command execution (40+ cases across config, sensors, network, users).
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "CommandManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "TelemetryManager.h"
#include "Themes.h"
#include <LittleFS.h>
#include <time.h>

void AppManager::executeCommand(CliDemand cmd) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 bool changed = false;

 switch (cmd.type) {
 case CMD_HELP:
 _cmdMgr->printHelp( ); break;

 case CMD_SHOW_THEMES:
 _cmdMgr->consolePrintln("");
 _cmdMgr->consolePrintln("--- Available Themes ---");
 for(int i=0; i<getThemeCount( ); i++) {
 _cmdMgr->consolePrintf(" %2d %-12s %s\n", i, getThemeId(i).c_str( ), getThemePalette(i)->displayName);
 }
 _cmdMgr->consolePrintln("-------------------------------------------");
 break;

 case CMD_SET_THEME: {
 /* Functions accept String; wrap the char[] in a temporary. */
 int idx = getThemeIndexByName(String(cmd.strVal1));
 if (idx == -1) {
 /* Didn't match by name — try as numeric index, but only if
 * the string is a well-formed number (avoids "abc".toInt( )==0
 * applying theme 0). */
 int numericIdx = 0;
 if (!parseIntStrict(String(cmd.strVal1), numericIdx)) idx = -1;
 else idx = numericIdx;
 }
 if (idx >= 0 && idx < getThemeCount( )) {
 cfg.themeIndex = idx;
 loadTheme(idx);
 _displayMgr->refreshTheme( );
 changed = true;
 LOG_CODE(LOG_INFO, "CFG", CFG_THEME_APPLIED, idx, String(getThemePalette(idx)->displayName));
 _cmdMgr->printSuccess(String(_cmdMgr->isPt( ) ? "Tema: " : "Theme: ")
 + getThemePalette(idx)->displayName);
 } else {
 LOG_CODE(LOG_WARN, "CFG", CFG_THEME_NOT_FOUND, 0, "");
 _cmdMgr->printError(_cmdMgr->isPt( )
 ? "Tema nao encontrado. Veja 'show themes'."
 : "Theme not found. Try 'show themes'.");
 }
 break;
 }

 case CMD_SHOW_LOGS: {
 _cmdMgr->consolePrintln("");
 _cmdMgr->consolePrintln("--- SYSTEM LOG START ---");
 int logCount = 0;
 auto streamLogFile = [&](const char* path) {

 _storageMgr->enterFlashReadLock( );
 bool exists = LittleFS.exists(path);
 File f;
 if (exists) f = LittleFS.open(path, "r");
 _storageMgr->exitFlashReadLock( );
 if (exists && f) {

 char lineBuf[256];
 while (f.available( ) && logCount < 2000) {
 feedWdt( );
 size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
 if (len == 0) continue;
 lineBuf[len] = '\0';
 _cmdMgr->printLogEntry(String(lineBuf));
 logCount++;
 }
 f.close( );
 }
 };
 streamLogFile("/system.old");
 streamLogFile("/system.log");
 _cmdMgr->consolePrintln("--- SYSTEM LOG END ---");
 _cmdMgr->consolePrintln("");
 break;
 }

 case CMD_SHOW_SENSORS: _cmdMgr->renderSensorTable(cfg.sensors, MAX_SENSORS); break;
 case CMD_SHOW_METRICS: _cmdMgr->renderMetrics( ); break;
 case CMD_SHOW_STORAGE: {
 String rep = _storageMgr->getStatsReport( );
 LOG_CODE(LOG_INFO, "STO", STO_STATS_REPORT, 0, rep);
 _cmdMgr->consolePrintln("");
 _cmdMgr->consolePrintln(_cmdMgr->isPt( )
 ? "--- Estatisticas do Flash ---"
 : "--- Storage Stats ---");
 _cmdMgr->consolePrintln(rep);
 _cmdMgr->printDivider( );
 break;
 }
 case CMD_SHOW_SYSINFO: _cmdMgr->renderSystemInfo(cfg); break;
 case CMD_SHOW_NET: {
 String ip = _netMgr->getIpAddress( );
 LOG_CODE(LOG_INFO, "NET", NET_SHOW_IP, 0, ip);
 _cmdMgr->consolePrintln("");
 _cmdMgr->consolePrintln(_cmdMgr->isPt( )
 ? "--- Status da Rede ---"
 : "--- Network Status ---");
 _cmdMgr->consolePrintf (" IP: %s\n", ip.c_str( ));
 _cmdMgr->consolePrintf (" RSSI: %ld dBm\n", (long)_netMgr->getRssi( ));
 _cmdMgr->printDivider( );
 break;
 }

#if SIMUT_SENSOR_DS18B20
#if SIMUT_SENSOR_DS18B20
 case CMD_SET_DS_RES: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para resolucao"
 : "Invalid number for resolution");
 break;
 }
 if (cmd.intVal1 < 9 || cmd.intVal1 > 12) {
 _cmdMgr->printError(pt ? "Resolucao fora de range (9-12)"
 : "Resolution out of range (9-12)");
 break;
 }
 if (!_sensorMgr->setDs18Resolution((DS18B20PIO::Resolution)cmd.intVal1)) {
 _cmdMgr->printError(pt ? "Falha ao aplicar resolucao no sensor"
 : "Failed to apply resolution");
 break;
 }
 cfg.ds18Resolution = cmd.intVal1;
 changed = true;
 break;
 }
#endif
#endif

 case CMD_SET_SYS_NAME: {
 const bool pt = _cmdMgr->isPt( );
 if (!isValidName(cmd.strVal1, sizeof(cfg.deviceName) - 1)) {
 _cmdMgr->printError(pt ? "Nome invalido (1-31 chars, sem ctrl chars)"
 : "Invalid name (1-31 chars, no ctrl chars)");
 break;
 }
 safeCopy(cfg.deviceName, cmd.strVal1, sizeof(cfg.deviceName));
 changed = true;
 break;
 }
 case CMD_SET_WIFI_SSID: {
 const bool pt = _cmdMgr->isPt( );
 if (!isValidCfgString(cmd.strVal1, sizeof(cfg.wifiSsid) - 1)) {
 _cmdMgr->printError(pt ? "SSID invalido (max 31, sem ctrl chars)"
 : "Invalid SSID (max 31, no ctrl chars)");
 break;
 }
 safeCopy(cfg.wifiSsid, cmd.strVal1, sizeof(cfg.wifiSsid));
 changed = true;
 break;
 }
 case CMD_SET_WIFI_PASS: {
 const bool pt = _cmdMgr->isPt( );
 if (!isValidCfgString(cmd.strVal1, sizeof(cfg.wifiPass) - 1)) {
 _cmdMgr->printError(pt ? "Senha invalida (max 31, sem ctrl chars)"
 : "Invalid pass (max 31, no ctrl chars)");
 break;
 }
 safeCopy(cfg.wifiPass, cmd.strVal1, sizeof(cfg.wifiPass));
 changed = true;
 break;
 }
 case CMD_SET_TIMEZONE: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para timezone"
 : "Invalid number for timezone");
 break;
 }
 if (cmd.intVal1 < -12 || cmd.intVal1 > 14) {
 _cmdMgr->printError(pt ? "Timezone fora de range (-12 a +14)"
 : "Timezone out of range (-12 to +14)");
 break;
 }
 cfg.timezoneOffset = (int8_t)cmd.intVal1;
 NetworkManager::applyTimezone(cfg.timezoneOffset);
 changed = true;
 break;
 }

 case CMD_SET_NTP: {
 const bool pt = _cmdMgr->isPt( );
 if (!isValidCfgString(cmd.strVal1, sizeof(cfg.ntpServer) - 1)) {
 _cmdMgr->printError(pt ? "NTP invalido (max 31, sem ctrl chars)"
 : "Invalid NTP (max 31, no ctrl chars)");
 break;
 }
 safeCopy(cfg.ntpServer, cmd.strVal1, sizeof(cfg.ntpServer));
 cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
 changed = true;
 break;
 }

 case CMD_SET_TEL_SERVER: {
 const bool pt = _cmdMgr->isPt( );
 if (!isValidCfgString(cmd.strVal1, sizeof(cfg.telServer) - 1)) {
 _cmdMgr->printError(pt ? "URL invalida (max 63, sem ctrl chars)"
 : "Invalid URL (max 63, no ctrl chars)");
 break;
 }
 safeCopy(cfg.telServer, cmd.strVal1, sizeof(cfg.telServer));
 changed = true;
 break;
 }
 case CMD_SET_TEL_PORT: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para porta"
 : "Invalid number for port");
 break;
 }
 if (cmd.intVal1 < 1 || cmd.intVal1 > 65535) {
 _cmdMgr->printError(pt ? "Porta fora de range (1-65535)"
 : "Port out of range (1-65535)");
 break;
 }
 cfg.telPort = (uint16_t)cmd.intVal1;
 changed = true;
 break;
 }
 case CMD_SET_TEL_PATH: {
 const bool pt = _cmdMgr->isPt( );
 if (!isValidCfgString(cmd.strVal1, sizeof(cfg.telPath) - 1)) {
 _cmdMgr->printError(pt ? "Path invalido (max 31, sem ctrl chars)"
 : "Invalid path (max 31, no ctrl chars)");
 break;
 }
 safeCopy(cfg.telPath, cmd.strVal1, sizeof(cfg.telPath));
 changed = true;
 break;
 }
 case CMD_SET_TEL_BATCH: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para batch"
 : "Invalid number for batch");
 break;
 }
 if (cmd.intVal1 < 1 || cmd.intVal1 > 50) {
 _cmdMgr->printError(pt ? "Batch fora de range (1-50)"
 : "Batch out of range (1-50)");
 break;
 }
 cfg.telBatchSize = (uint8_t)cmd.intVal1;
 changed = true;
 break;
 }
 case CMD_SET_TEL_INTERVAL: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para intervalo"
 : "Invalid number for interval");
 break;
 }
 if (cmd.intVal1 < 0) {
 _cmdMgr->printError(pt ? "Intervalo deve ser >= 0 (0 = off)"
 : "Interval must be >= 0 (0 = off)");
 break;
 }
 cfg.telInterval = (uint32_t)cmd.intVal1;
 changed = true;
 break;
 }
 case CMD_SET_TEL_CRYPTO: {
 const bool pt = _cmdMgr->isPt( );
 if (strcmp(cmd.strVal1, "on") != 0 && strcmp(cmd.strVal1, "off") != 0) {
 _cmdMgr->printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
 break;
 }
 cfg.telEncryption = cmd.boolVal;
 changed = true;
 break;
 }
 case CMD_SET_TEL_MODE: {
 const bool pt = _cmdMgr->isPt( );
 if (cmd.intVal1 < 0) {
 _cmdMgr->printError(pt ? "Modo desconhecido (use json|csv|custom)"
 : "Unknown mode (use json|csv|custom)");
 break;
 }
 cfg.telMode = cmd.intVal1;
 changed = true;
 break;
 }
 case CMD_SET_HISTORY_INTERVAL: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para intervalo"
 : "Invalid number for interval");
 break;
 }
 if (cmd.intVal1 < HISTORY_INTERVAL_MIN_MIN || cmd.intVal1 > HISTORY_INTERVAL_MAX_MIN) {
 _cmdMgr->printError(pt ? "Intervalo deve estar entre 1 e 1440 minutos (24h)"
 : "Interval must be between 1 and 1440 minutes (24h)");
 break;
 }
 _storageMgr->setHistoryIntervalMin((uint16_t)cmd.intVal1);
 changed = true;
 break;
 }

 case CMD_RESET_ADMIN:
 cmdHandleResetAdmin(cmd, cfg, changed); break;

 case CMD_RESET_TOUCH_CAL: {
 if (!cmd.confirmed) {
 const bool pt = _cmdMgr->isPt( );
 _cmdMgr->printInfo(pt ? "ATENCAO: reseta calibracao do touch."
 : "WARN: resets touch calibration.");
 _cmdMgr->printInfo(pt ? "Use 'conf system touch reset confirm'."
 : "Run 'conf system touch reset confirm'.");
 break;
 }
 /* Clear touch calibration in config (invalidates magic) */
 TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
 memset(cal, 0, sizeof(TouchCalData));
 _displayMgr->resetTouchCalibration( );
 _cmdMgr->printInfo(_cmdMgr->isPt( )
 ? "Calibracao do touch resetada p/ default."
 : "Touch calibration reset to factory defaults.");
 changed = true;
 break;
 }

 case CMD_FACTORY_RESET: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.confirmed) {
 _cmdMgr->printInfo(pt ? "ATENCAO: factory reset APAGA TODA config + reboot."
 : "WARN: factory reset WIPES ALL config + reboots.");
 _cmdMgr->printInfo(pt ? "Use 'conf system factory confirm'."
 : "Run 'conf system factory confirm'.");
 break;
 }
 LOG_CODE(LOG_WARN, "SYS", SYS_REBOOT_USER, 0, TRL("Factory reset"));
 _storageMgr->resetToFactory( );
 delay(100);
 LogManager::instance( ).safeReboot( );
 }

 case CMD_SET_NTP_ENABLED: {
 const bool pt = _cmdMgr->isPt( );
 bool en = (cmd.intVal1 != 0);
 _storageMgr->setNtpEnabled(en);
 _cmdMgr->printSuccess(en ? (pt ? "NTP: habilitado" : "NTP: enabled")
 : (pt ? "NTP: desabilitado" : "NTP: disabled"));
 changed = true;
 break;
 }

 case CMD_SET_DNS_CFG:
 cmdHandleDnsCfg(cmd, cfg, changed); break;

 case CMD_SET_TIME:
 cmdHandleSetTime(cmd); break; /* No changed flag: immediate action */

 case CMD_DEFINE_SENSOR: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para GPIO"
 : "Invalid number for GPIO");
 break;
 }
 if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
 _cmdMgr->printError(pt ? "Slot fora de range (0-15)"
 : "Slot out of range (0-15)");
 break;
 }
 SensorRecord &r = cfg.sensors[cmd.intVal1];
 r.active = true;
 r.pins[0] = cmd.intVal1;
 memcpy(r.rom, cmd.rom, 8);
 /* Determine sensor type: explicit (v16 extended syntax) or auto-detect from ROM. */
 if (cmd.strVal3[0] != '\0') {
 if (strcmp(cmd.strVal3, "dht22") == 0) r.sensorType = TYPE_DHT22;
 else if (strcmp(cmd.strVal3, "ds18b20") == 0) r.sensorType = TYPE_DS18B20;
 else if (strcmp(cmd.strVal3, "bme280") == 0) r.sensorType = TYPE_BME280;
 else r.sensorType = TYPE_NONE;
 } else {
 /* Legacy: auto-detect from ROM (non-zero = DS18B20, zero = DHT22). */
 bool isDs18 = false;
 for (int k = 0; k < 8; k++) if (cmd.rom[k] != 0) isDs18 = true;
 r.sensorType = isDs18 ? TYPE_DS18B20 : TYPE_DHT22;
 }
 safeCopy(r.hwId, cmd.strVal1, sizeof(r.hwId));
 safeCopy(r.friendlyName, cmd.strVal2, sizeof(r.friendlyName));
 _cmdMgr->printSuccess(pt ? "Sensor mapeado em RAM."
 : "Sensor mapped in RAM.");
 break;
 }

 case CMD_WIPE_SENSOR: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.confirmed) {
 _cmdMgr->printInfo(pt ? "ATENCAO: reseta historico do sensor."
 : "WARN: resets sensor history epoch.");
 _cmdMgr->printInfo(pt ? "Use 'sensor wipe <gpio> confirm'."
 : "Run 'sensor wipe <gpio> confirm'.");
 break;
 }
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para GPIO"
 : "Invalid number for GPIO");
 break;
 }
 if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
 _cmdMgr->printError(pt ? "Slot fora de range (0-15)"
 : "Slot out of range (0-15)");
 break;
 }
 cfg.sensors[cmd.intVal1].provisionEpoch = _netMgr->getEpoch( );
 changed = true;
 _cmdMgr->printSuccess((pt ? "Historico resetado no Slot "
 : "Sensor history wiped for Slot ") + String(cmd.intVal1));
 break;
 }

 case CMD_ACCEPT_SENSOR:
 cmdHandleAcceptSensor(cmd, cfg, changed); break;

 case CMD_SCAN_SENSORS:
 if (!_sensorMgr->isScanning( )) { _sensorMgr->startScan( ); _waitingScan = true; }
 break;

 case CMD_WRITE_MEMORY: {
 /* Wraps save + reload of sensors in the same quiet mode
 * (re-entrant). loadAndCalibrateSensors emits
 * APP_SENSORS_CALIBRATED via LOG_CODE → LogManager.requestFsLock
 * which, outside quiet mode, would fall into IRQ-based lockout and
 * get stuck. */
 _displayMgr->requestQuietMode( ); /* default 15s timeout */
 bool saved = _storageMgr->saveConfiguration( );
 if (saved) {
 loadAndCalibrateSensors( );
 }
 _displayMgr->releaseQuietMode( );
 if (saved) {
 _cmdMgr->printSuccess(_cmdMgr->isPt( )
 ? "Config salva no Flash!"
 : "Config saved to Flash!");
 }
 break;
 }

 case CMD_CLEAR_LOGS:
 if (!cmd.confirmed) {
 const bool pt = _cmdMgr->isPt( );
 _cmdMgr->printInfo(pt ? "ATENCAO: apaga todos os logs."
 : "WARN: deletes all system logs.");
 _cmdMgr->printInfo(pt ? "Use 'clear log confirm' para prosseguir."
 : "Run 'clear log confirm' to proceed.");
 break;
 }
 _storageMgr->enterFlashSafeMode( );
 LittleFS.remove("/system.log"); LittleFS.remove("/system.old");
 _storageMgr->exitFlashSafeMode( );
 LogManager::instance( ).begin(true, LOG_DEBUG);
 _cmdMgr->printSuccess(_cmdMgr->isPt( ) ? "Logs apagados." : "Logs cleared.");
 break;

 case CMD_RELOAD:
 if (!cmd.confirmed) {
 const bool pt = _cmdMgr->isPt( );
 _cmdMgr->printInfo(pt ? "ATENCAO: vai reiniciar o dispositivo."
 : "WARN: will reboot the device.");
 _cmdMgr->printInfo(pt ? "Use 'reload confirm' para prosseguir."
 : "Run 'reload confirm' to proceed.");
 break;
 }
 LOG_CODE(LOG_WARN, "SYS", SYS_REBOOT_USER, 0, TRL("Reboot via CLI"));
 delay(100); /* Ensures log is flushed to flash */
 LogManager::instance( ).safeReboot( );
 break;
 case CMD_TEL_SYNC:
 /* Silent by design: user sees the natural log
 * "Telemetria enviada: ..." when there is data to send. */
 _telemetryMgr->forceSync( );
 break;

 case CMD_TEL_DUMP:
 _telemetryMgr->armPayloadDump( );
 _telemetryMgr->forceSync( );
 /* If there was data, _dumpPayloadNext was consumed (dump already
 * emitted). If not, the flag is armed and triggers on next sync. */
 if (_telemetryMgr->isPayloadDumpArmed( )) {
 _cmdMgr->printSuccess(_cmdMgr->isPt( )
 ? "Sem dados pendentes; dump armado para o proximo sync."
 : "No pending data; dump armed for next sync.");
 }
 break;

 case CMD_TEL_RESET:
 /* Resets telemetry cursor — invalidates RAM cache + removes flash
 * file. Used in ops (re-send data after prolonged server outage)
 * and in tests (tools/stress_test). No reboot. Next collectBatch
 * falls back to "lastRecorded - 30 days". */
 _storageMgr->resetTelemetryCursor( );
 _cmdMgr->printSuccess(_cmdMgr->isPt( )
 ? "Cursor de telemetria resetado. Proximos envios cobrem ate 30 dias atras."
 : "Telemetry cursor reset. Next sends cover up to 30 days back.");
 break;

 case CMD_DEBUG: {
 CliConfigData* cli = reinterpret_cast<CliConfigData*>(
 cfg.reserved + CLI_CONFIG_OFFSET);
 const bool pt = _cmdMgr->isPt( );
 if (cmd.intVal1 == 1 || cmd.intVal1 == 0) {
 bool on = (cmd.intVal1 == 1);
 cli->magic = CLI_CONFIG_MAGIC;
 cli->debugMode = on ? 1 : 0;
 LogManager::instance( ).setConsoleStream(on);
 _cmdMgr->setDebugMode(on);
 _cmdMgr->printSuccess(on ? (pt ? "Debug: LIGADO" : "Debug: ON")
 : (pt ? "Debug: DESLIGADO" : "Debug: OFF"));
 changed = true;
 } else {
 _cmdMgr->printInfo(_cmdMgr->isDebugMode( )
 ? (pt ? "Debug: LIGADO" : "Debug: ON")
 : (pt ? "Debug: DESLIGADO" : "Debug: OFF"));
 }
 break;
 }

 case CMD_LANGUAGE: {
 if (cmd.intVal1 == LANG_PT || cmd.intVal1 == LANG_EN) {
 cfg.displayLang = (uint8_t)cmd.intVal1;
 _displayMgr->setLanguage(cfg.displayLang);
 _cmdMgr->setCliLang(cfg.displayLang);
 LogManager::instance( ).setLanguage(cfg.displayLang);
 LOG_CODE(LOG_INFO, "APP", APP_UI_LANG_CHANGED, cmd.intVal1, "");
 _cmdMgr->printSuccess(cmd.intVal1 == LANG_PT
 ? "Idioma: Portugues (BR)"
 : "Language: English");
 changed = true;
 } else {
 _cmdMgr->printInfo(_cmdMgr->isPt( )
 ? "Idioma atual: Portugues (BR)"
 : "Current language: English");
 }
 break;
 }

 case CMD_IP_CFG:
 cmdHandleIpCfg(cmd, cfg, changed); break;

 case CMD_SENSOR_FIELD:
 cmdHandleSensorField(cmd, cfg, changed); break;

 case CMD_USER_ADD:
 cmdHandleUserAdd(cmd, cfg, changed); break;

 case CMD_USER_DEL: {
 const bool pt = _cmdMgr->isPt( );
 if (strcmp(cmd.strVal1, "admin") == 0) {
 _cmdMgr->printError(pt ? "Nao e permitido deletar 'admin'"
 : "Cannot delete 'admin'");
 break;
 }
 bool found = false;
 for (int i = 1; i < MAX_USERS; i++) {
 if (cfg.users[i].active && strcmp(cmd.strVal1, cfg.users[i].username) == 0) {
 cfg.users[i].active = false;
 memset(cfg.users[i].password, 0, sizeof(cfg.users[i].password));
 _cmdMgr->printSuccess(String(pt ? "Usuario removido: " : "User deleted: ") + cmd.strVal1);
 LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, i,
 String(TRL("CLI deleted user: ")) + cmd.strVal1);
 changed = true;
 found = true;
 break;
 }
 }
 if (!found) _cmdMgr->printError(pt ? "Usuario nao encontrado" : "User not found");
 break;
 }

 case CMD_USER_PASS:
 cmdHandleUserPass(cmd, cfg, changed); break;

 case CMD_SET_WEB_PORT: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid || cmd.intVal1 < 1 || cmd.intVal1 > 65535) {
 _cmdMgr->printError(pt ? "Porta invalida (1..65535)"
 : "Invalid port (1..65535)");
 break;
 }
 WebConfigData* w = reinterpret_cast<WebConfigData*>(
 cfg.reserved + WEB_CONFIG_OFFSET);
 w->port = (uint16_t)cmd.intVal1;
 char buf[64];
 snprintf(buf, sizeof(buf),
 pt ? "Porta web: %d (aplica apos reload)"
 : "Web port: %d (applies after reload)",
 cmd.intVal1);
 _cmdMgr->printSuccess(buf);
 changed = true;
 break;
 }

 /* CMD_DBG_SENSOR_HISTORY_ALL removed (debug-only, test command). */

 case CMD_GOTO_SCREEN: {
 /* Switches TFT screen via show*Screen( ) directly.
 * Bypasses handleTouch pressure gates. For screenshot
 * automation. Short strings to save flash.
 * Also resets _lastTouchTime — without this, the idle guard
 * (30s without touch → forceDashboard in DisplayManager_
 * Touch.cpp:117) reverts the screen BEFORE the HTTP capture of
 * /api/screenshot completes (chunked ~5s, full ~4s).
 * resetTouchIdle( ) gives a 30s window for capture. */
 const char* n = cmd.strVal1;
 if (!strcmp(n, "dash")) _displayMgr->forceDashboard( );
 else if (!strcmp(n, "set")) _displayMgr->showSettingsMain( );
 else if (!strcmp(n, "thm")) _displayMgr->showSettingsThemes(cfg.themeIndex);
 else if (!strcmp(n, "lng")) _displayMgr->showSettingsLang(cfg.displayLang);
 else if (!strcmp(n, "pwd")) _displayMgr->showSettingsPassword( );
 else if (!strcmp(n, "lic")) _displayMgr->showSettingsLicense( );
 else if (!strcmp(n, "sts")) _displayMgr->showSystemStatus( );
 else if (!strcmp(n, "alm")) _displayMgr->showSettingsAlarms(&cfg);
 else if (!strcmp(n, "gra")) _displayMgr->forceGraphView( );
 else { _cmdMgr->printError("?screen"); break; }
 _displayMgr->resetTouchIdle( );
 _cmdMgr->printSuccess(n);
 break;
 }

 case CMD_TOUCH_SIM: {
 /* Injects a simulated touch at (x, y) screen-space.
 * Useful for TFT screenshot automation via /api/screenshot.
 * Parses X and Y from strVal1 and strVal2 using parseIntStrict. */
 int x = 0, y = 0;
 String sx(cmd.strVal1), sy(cmd.strVal2);
 if (!parseIntStrict(sx, x) || !parseIntStrict(sy, y) ||
 x < 0 || x > 319 || y < 0 || y > 239) {
 _cmdMgr->printError(_cmdMgr->isPt( )
 ? "Uso: touch sim <X> <Y> (X 0..319, Y 0..239)"
 : "Usage: touch sim <X> <Y> (X 0..319, Y 0..239)");
 break;
 }
 _displayMgr->injectTouch((int16_t)x, (int16_t)y);
 char buf[64];
 snprintf(buf, sizeof(buf), _cmdMgr->isPt( )
 ? "Toque injetado em (%d, %d)" : "Touch injected at (%d, %d)", x, y);
 _cmdMgr->printSuccess(buf);
 break;
 }



 case CMD_UNKNOWN:
 default:
 LOG_CODE(LOG_WARN, "CLI", CLI_UNKNOWN_CMD, 0, "");
 _cmdMgr->printError(_cmdMgr->isPt( )
 ? "Comando desconhecido. Digite 'help'."
 : "Unknown command. Type 'help'.");
 break;
 }

 if (changed) _cmdMgr->printInfo(_cmdMgr->isPt( )
 ? "RAM OK. Use 'write memory' para salvar."
 : "RAM updated. Run 'write memory' to persist.");
}

/* =========================================================================== */
/* CORE 0 YIELD — UI EVENTS + SOUND + SENSOR UPDATE */
/* =========================================================================== */
/**
 * @brief Process pending UI events, sound signals, and sensor readings.
 * Called from the main loop and from web server light-yield callbacks.
 * Protected against re-entrancy with a static guard flag.
 */
