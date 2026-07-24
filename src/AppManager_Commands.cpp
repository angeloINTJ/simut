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
#include "CommandParser.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "TelemetryManager.h"
#include "Themes.h"
#include "WebManager.h" /* _cg*Hits: por que um envio em chunks foi abortado */
#include <LittleFS.h>
#include <time.h>
#include "lwip/opt.h"
#if LWIP_STATS && MEMP_STATS
#include "lwip/stats.h"
#include "lwip/memp.h"
#endif

void AppManager::executeCommand(CliDemand cmd) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 bool changed = false;
 const bool pt = _cmdMgr->isPt( );

 /* ── Cisco IOS mode validation ──
  * Navigation commands are always processed regardless of mode mask. */
 uint8_t curMask = (1 << _cmdMgr->cliMode( ));
 bool isNav = (cmd.type == CMD_ENABLE || cmd.type == CMD_DISABLE ||
               cmd.type == CMD_CONFIGURE || cmd.type == CMD_EXIT ||
               cmd.type == CMD_END || cmd.type == CMD_DO ||
               cmd.type == CMD_HELP || cmd.type == CMD_SENSOR_ENTER);

 if (!isNav && !(getCommandModeMask(cmd.type) & curMask)) {
  /* Build a helpful error: which mode is needed? */
  uint8_t needed = getCommandModeMask(cmd.type);
  if (needed & CLI_VALID_USER) {
   _cmdMgr->printError(pt ? "Comando requer modo EXEC. Use 'enable' se estiver no modo usuario."
                          : "Command requires EXEC mode. Use 'enable' from user mode.");
  } else if (needed & CLI_VALID_PRIV) {
   _cmdMgr->printError(pt ? "Comando requer modo privilegiado. Use 'enable'."
                          : "Command requires privileged mode. Use 'enable'.");
  } else if (needed & CLI_VALID_CONFIG) {
   _cmdMgr->printError(pt ? "Comando requer modo configuracao. Use 'configure terminal'."
                          : "Command requires config mode. Use 'configure terminal'.");
  } else if (needed & CLI_VALID_SENSOR) {
   _cmdMgr->printError(pt ? "Comando requer modo sensor. Use 'sensor <N>' no modo config."
                          : "Command requires sensor config mode. Use 'sensor <N>' from config.");
  } else {
   _cmdMgr->printError(pt ? "Comando indisponivel neste modo."
                          : "Command not available in this mode.");
  }
  return;
 }

 switch (cmd.type) {
 case CMD_HELP:
  /* Mode-aware help — '?' shows context-sensitive commands */
  _cmdMgr->printModeHelp( ); break;

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
 /* Reads the COMPACT BINARY log (12-byte CompactLogRecord). The old
  * implementation streamed "/system.log"/"/system.old" — the legacy
  * CSV names that begin( ) deletes at every boot — so this command
  * always printed an empty log while the writer filled *.blog. */
 auto streamLogFile = [&](const char* path) {

 _storageMgr->enterFlashReadLock( );
 bool exists = LittleFS.exists(path);
 File f;
 if (exists) f = LittleFS.open(path, "r");
 _storageMgr->exitFlashReadLock( );
 if (exists && f) {

 CompactLogRecord rec;
 char line[96];
 while (logCount < 2000 &&
        f.read((uint8_t*)&rec, LOG_RECORD_SIZE) == (int)LOG_RECORD_SIZE) {
 feedWdt( );
 snprintf(line, sizeof(line), "%10lu up%uh C%u [%s][%-6s] code=%u ctx=%d",
          (unsigned long)rec.epoch, rec.uptimeHr, rec.getCore( ),
          LogManager::instance( ).getLevelString((LogLevel)rec.getLevel( )),
          tagIdToString(rec.getTagId( )), rec.code, (int)rec.context);
 /* Direct print: printLogEntry( ) is the legacy CSV renderer — it
  * silently drops any line without >= 7 ';'-separated fields. */
 _cmdMgr->consolePrintln(String(line));
 logCount++;
 }
 f.close( );
 }
 };
 streamLogFile(LOG_FILE_OLD);
 streamLogFile(LOG_FILE_CURRENT);
 _cmdMgr->consolePrintln("--- SYSTEM LOG END ---");
 _cmdMgr->consolePrintln("");
 break;
 }

 case CMD_SHOW_SENSORS: _cmdMgr->renderSensorTable(cfg.sensors, MAX_SENSORS); break;
 case CMD_SHOW_GPIO:    _cmdMgr->renderGpioMap(cfg.sensors, MAX_SENSORS); break;
 case CMD_SHOW_SENSOR_TYPES: {
  _cmdMgr->consolePrintln("");
  _cmdMgr->consolePrintln(_cmdMgr->isPt( ) ? "--- Tipos de Sensor Compilados ---"
                                            : "--- Compiled Sensor Types ---");
  SensorType allTypes[] = {TYPE_DS18B20, TYPE_DHT22, TYPE_BME280};
  for (SensorType t : allTypes) {
   if (!sensorTypeEnabled(t)) continue;
   auto fmt = SensorFormat::forType(t);
   /* Build pin role labels: "1-Wire", "Data", "SDA,SCL" */
   String pinStr = "";
   for (int p = 0; p < fmt.pinCount && p < 4; p++) {
    if (p > 0) pinStr += ",";
    pinStr += fmt.pins[p].label;
   }
   /* Build channel labels: "Temp", "Temp+Hum", "Temp+Hum+Press" */
   String chStr = "";
   for (int c = 0; c < fmt.valueCount && c < 3; c++) {
    if (c > 0) chStr += "+";
    if (c == 0) chStr += _cmdMgr->isPt( ) ? "Temp" : "Temp";
    else if (c == 1) chStr += _cmdMgr->isPt( ) ? "Umid" : "Hum";
    else if (c == 2) chStr += _cmdMgr->isPt( ) ? "Press" : "Press";
   }
   _cmdMgr->consolePrintf(" %-8s | %2d pin%s | %s | %s\n",
     sensorTypeName(t),
     fmt.pinCount, fmt.pinCount > 1 ? "s" : " ",
     chStr.c_str( ),
     pinStr.c_str( ));
  }
  _cmdMgr->printDivider( );
  break;
 }
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
#if LWIP_STATS && MEMP_STATS
 /* T0.3: PBUF pool watermark — decides T2.2 (pool 12→16). */
 {
 const struct stats_mem* ps = lwip_stats.memp[MEMP_PBUF_POOL];
 _cmdMgr->consolePrintf(" PBUF pool: %u em uso / pico %u / %u total, %u falhas\n",
                        (unsigned)ps->used, (unsigned)ps->max,
                        (unsigned)ps->avail, (unsigned)ps->err);
 }
#endif
 /* Why chunked responses were cut short. All three surface in the log as
  * WEB_CLIENT_DISCONNECT but call for opposite fixes, so keep them apart. */
 _cmdMgr->consolePrintf(" Abortos de envio: prazo %lu | latch %lu | desconexao %lu\n",
                        (unsigned long)_cgDeadlineHits,
                        (unsigned long)_cgGuardHits,
                        (unsigned long)_cgDisconnHits);
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
 _cmdMgr->printInfo(pt ? "Use 'system touch reset confirm'."
 : "Run 'system touch reset confirm'.");
 break;
 }
 /* Clear touch calibration in config (invalidates magic) */
 TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
 memset(cal, 0, sizeof(TouchCalData));
 _displayMgr->resetTouchCalibration( );
 /* Go straight into the wizard. Resetting only restores the default
  * corner values; on its own it leaves the panel exactly as unusable as
  * whatever prompted the reset. And the old advice — recalibrate from the
  * display menu — assumes touch works well enough to navigate there, which
  * is the very thing that fails when someone reaches for this command. */
 _displayMgr->showTouchCalibration( );
 _displayMgr->resetTouchIdle( );
 _cmdMgr->printInfo(_cmdMgr->isPt( )
 ? "Calibracao resetada. Assistente iniciado no display: siga as instrucoes na tela."
 : "Calibration reset. Wizard started on the display: follow the on-screen steps.");
 _cmdMgr->printInfo(_cmdMgr->isPt( )
 ? "Ao terminar, use 'write memory' para salvar."
 : "When finished, run 'write memory' to save.");
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

 case CMD_FORMAT_FS: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.confirmed) {
 _cmdMgr->printInfo(pt ? "ATENCAO: formata o LittleFS (config, historico, logs) + reboot."
 : "WARN: formats LittleFS (config, history, logs) + reboots.");
 _cmdMgr->printInfo(pt ? "Use 'system format confirm'."
 : "Run 'system format confirm'.");
 break;
 }
 _cmdMgr->printSuccess(pt ? "Formatando LittleFS... reboot em seguida."
 : "Formatting LittleFS... reboot follows.");
 delay(100);
 /* Core 1 dead during the multi-second erase burst; no unpause needed
  * because the device reboots right after. */
 _displayMgr->requestQuietMode( );
 {
 LogManager::WdtWindow _wdt(30000);
 LittleFS.format( );
 }
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
 /* BME280 is I2C: pins[0]=SDA, pins[1]=SCL (convention SCL = SDA+1,
  * same pairing as the scan probe). Other types are single-pin. */
 r.pins[1] = (r.sensorType == TYPE_BME280) ? (uint8_t)(cmd.intVal1 + 1)
                                           : PIN_UNUSED;
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

 case CMD_REMOVE_SENSOR: {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.confirmed) {
 _cmdMgr->printInfo(pt ? "ATENCAO: desativa e limpa o slot do sensor."
 : "WARN: deactivates and clears the sensor slot.");
 _cmdMgr->printInfo(pt ? "Use 'sensor remove <gpio> confirm'."
 : "Run 'sensor remove <gpio> confirm'.");
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
 {
 SensorRecord &r = cfg.sensors[cmd.intVal1];
 r.active = false;
 r.sensorType = TYPE_NONE;
 for (int pp = 0; pp < MAX_SENSOR_PINS; pp++) r.pins[pp] = PIN_UNUSED;
 memset(r.rom, 0, 8);
 r.hwId[0] = '\0';
 r.friendlyName[0] = '\0';
 }
 changed = true;
 _cmdMgr->printSuccess((pt ? "Slot removido: "
 : "Sensor slot removed: ") + String(cmd.intVal1));
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
 _storageMgr->flushHistoryBatch( ); /* T2.1: persist buffered samples */
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
 /* Remove the COMPACT logs (the live format) and the legacy CSVs. The
  * old code only removed the CSV names, so 'clear log' never actually
  * cleared anything since the .blog migration. */
 LittleFS.remove(LOG_FILE_CURRENT); LittleFS.remove(LOG_FILE_OLD);
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
 _storageMgr->flushHistoryBatch( ); /* T2.1: don't lose buffered samples */
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

 case CMD_USER_PERM:
 cmdHandleUserPerm(cmd, cfg, changed); break;

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
 else if (!strcmp(n, "touchcal")) _displayMgr->showTouchCalibration( );
 else if (!strcmp(n, "touchsens")) _displayMgr->showTouchSensitivity( );
 else if (!strcmp(n, "offset")) _displayMgr->showSettingsDisplayOffset( );
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



 /* ── Cisco IOS Mode Navigation ── */

 case CMD_ENABLE:
  _cmdMgr->setCliMode(CLI_MODE_PRIV_EXEC);
  _cmdMgr->consolePrintf("%s\n", pt ? "Modo privilegiado (SIMUT#). 'configure terminal' p/ configurar."
                                    : "Privileged mode (SIMUT#). 'configure terminal' to configure.");
  break;

 case CMD_DISABLE:
  _cmdMgr->setCliMode(CLI_MODE_USER_EXEC);
  _cmdMgr->consolePrintf("%s\n", pt ? "Modo EXEC (SIMUT>). 'enable' p/ privilegiado."
                                    : "EXEC mode (SIMUT>). 'enable' for privileged.");
  break;

 case CMD_CONFIGURE:
  _cmdMgr->setCliMode(CLI_MODE_GLOBAL_CONFIG);
  _cmdMgr->consolePrintf("%s\n", pt ? "Modo configuracao global. 'exit' voltar, 'end' p/ privilegiado."
                                    : "Global configuration mode. 'exit' back, 'end' to privileged.");
  break;

 case CMD_EXIT:
  switch (_cmdMgr->cliMode( )) {
   case CLI_MODE_SENSOR_CONFIG:
    _cmdMgr->setCliMode(CLI_MODE_GLOBAL_CONFIG);
    break;
   case CLI_MODE_GLOBAL_CONFIG:
    _cmdMgr->setCliMode(CLI_MODE_PRIV_EXEC);
    break;
   case CLI_MODE_PRIV_EXEC:
    _cmdMgr->setCliMode(CLI_MODE_USER_EXEC);
    break;
   default:
    _cmdMgr->consolePrintln(pt ? "Ja esta no modo raiz." : "Already at root mode.");
    break;
  }
  break;

 case CMD_END:
  _cmdMgr->setCliMode(CLI_MODE_PRIV_EXEC);
  break;

 case CMD_DO: {
  /* Execute a privileged-mode command from within config mode.
   * Parse the inner command and dispatch it directly, bypassing
   * mode validation (we validate against PRIV mask, not current mode). */
  CliDemand inner = parseCliCommand(String(cmd.strVal1));
  if (inner.type == CMD_UNKNOWN) {
   _cmdMgr->printError(pt ? "Comando invalido apos 'do'." : "Invalid command after 'do'.");
   break;
  }
  /* Validate inner command against PRIV + USER mask */
  if (!(getCommandModeMask(inner.type) & (CLI_VALID_PRIV | CLI_VALID_USER))) {
   _cmdMgr->printError(pt ? "Comando nao permitido via 'do'." : "Command not allowed via 'do'.");
   break;
  }
  /* Execute the inner command — recursive call to executeCommand.
   * Mode validation in the recursive call will use the current config
   * mode, but since we already validated against PRIV, commands that
   * are valid in PRIV mode will pass through. We temporarily change
   * mode to PRIV_EXEC, execute, then restore. */
  CLIMode savedMode = _cmdMgr->cliMode( );
  _cmdMgr->setCliMode(CLI_MODE_PRIV_EXEC);
  executeCommand(inner);
  _cmdMgr->setCliMode(savedMode);
  break;
 }

 case CMD_SENSOR_ENTER: {
  /* Enter sensor sub-config mode — 'sensor <N>' from global config.
   * cmd.intVal1 = slot index (already validated by parser: 0-15). */
  if (_cmdMgr->cliMode( ) != CLI_MODE_GLOBAL_CONFIG && _cmdMgr->cliMode( ) != CLI_MODE_PRIV_EXEC) {
   _cmdMgr->printError(pt ? "Use 'sensor <N>' no modo configuracao ou privilegiado."
                          : "Use 'sensor <N>' from config or privileged mode.");
   break;
  }
  if (!cfg.sensors[cmd.intVal1].active) {
   _cmdMgr->printInfo((pt ? "Slot " : "Slot ") + String(cmd.intVal1)
     + (pt ? " nao configurado. Use 'create <tipo>' para configurar."
           : " not configured. Use 'create <type>' to configure."));
  }
  _cmdMgr->setConfigSensorSlot((int8_t)cmd.intVal1);
  String name = cfg.sensors[cmd.intVal1].friendlyName;
  if (name.length( ) == 0) name = sensorTypeName((SensorType)cfg.sensors[cmd.intVal1].sensorType);
  _cmdMgr->consolePrintf("%s Slot %d (%s)\n",
    pt ? "Entrando configuracao do sensor —" : "Entering sensor configuration —",
    cmd.intVal1, name.c_str( ));
  /* In sensor mode, bare commands like 'type', 'name', 'pin' will be
   * pre-processed by processInput to fill the slot from _configSensorSlot. */
  break;
 }

 case CMD_UNKNOWN:
 default:
  /* Mode-aware hint for unknown commands */
  if (_cmdMgr->isConfigMode( )) {
   LOG_CODE(LOG_WARN, "CLI", CLI_UNKNOWN_CMD, 0, "");
   _cmdMgr->printError(pt
    ? "Comando desconhecido. Digite '?' ou 'exit' p/ sair."
    : "Unknown command. Type '?' or 'exit' to leave.");
  } else {
   LOG_CODE(LOG_WARN, "CLI", CLI_UNKNOWN_CMD, 0, "");
   _cmdMgr->printError(pt
    ? "Comando desconhecido. Digite '?' ou 'help'."
    : "Unknown command. Type '?' or 'help'.");
  }
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
