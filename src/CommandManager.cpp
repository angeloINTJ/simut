/**
 * @file CommandManager.cpp
 * @brief Implementation of CommandManager — command parsing, rendering, and I/O routing.
 * @details Implements the CLI parser that converts text input into CliDemand
 * structs, along with formatted output for sensor tables, scan results,
 * system info, and log entries. Supports dual-buffer USB/BT processing.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "CommandManager.h"
#include "LogManager.h"
#include "DisplayManager.h" /* F-LANGPACK Etapa 3: getActiveHelpText */
#include "MetricsManager.h"
#include "StorageManager.h" /* getBoardSerialNumber em show system info */
#include "HelpLicenseEN.h" /* HELP_TEXT_EN inline em PROGMEM */
#include <LittleFS.h>
#include <time.h>
#include <stdarg.h>

CommandManager::CommandManager( ) {
 _usbBuffer.reserve(128);
 _btBuffer.reserve(128);
}

void CommandManager::begin(const char* btDeviceName) {
 /* BT + welcome/prompt + sink TODOS movidos pra beginBluetooth( ).
 * Aqui é NO-OP (mantido pra compatibilidade da assinatura).
 * Razão: cada Serial.println pode bloquear ~1s/linha pré-USB-CDC-connect
 * (ignoreFlowControl timeout). LOG_CODE → consoleSink → consolePrintln →
 * Serial.print = boot stall acumulado de dezenas de segundos.
 * Após cyw43_arch_init (em WiFi.begin), USB CDC re-enumerado e host
 * geralmente conectado, então prints rápidos. */
 (void)btDeviceName;
}

void CommandManager::beginBluetooth(const char* btDeviceName) {
 /* SerialBT.begin acessa CYW43 via SPI/PIO. Caller deve garantir que
 * cyw43_arch_init já rodou (WiFi.begin) — caso contrário, residual
 * state do chip pós-OTA causa hardfault. */
 _btMgr.begin(btDeviceName);

 /* A4: instala sink do LogManager — espelha logs em USB CDC + BT.
 * Movido pra cá pra evitar Serial blocks durante boot setup phase. */
 LogManager::instance( ).setConsoleSink([this](const char* line) {
 this->consolePrintln(String(line));
 });

 printWelcome( );
 printPrompt( );
}

void CommandManager::setBtValidator(BtAuthValidator validator) {
 _btMgr.setValidator(validator);
}


void CommandManager::consolePrint(const String& msg) {
 Serial.print(msg);
 _btMgr.print(msg);
}

void CommandManager::consolePrintln(const String& msg) {
 Serial.println(msg);
 _btMgr.println(msg);
}

void CommandManager::consolePrintf(const char* format, ...) {
 char buf[256];
 va_list args;
 va_start(args, format);
 vsnprintf(buf, sizeof(buf), format, args);
 va_end(args);

 Serial.print(buf);
 _btMgr.print(buf);
}

String CommandManager::getLastRawInput( ) { return _lastRawInput; }

bool CommandManager::processInput(CliDemand &demandOut) {
 /* Defer LOG_CODE flash writes durante BT update: evita que
 * writeCompactToFlash dispare lockout do Core 1 enquanto o
 * BluetoothManager está processando auth + banner I/O.
 * Logs ficam em RAM (_pendingLogs) e são flushed no próximo
 * write normal ou via flushPendingIfAny no loop principal. */
 LogManager::instance( ).setForceBuffer(true);
 _btMgr.update( );
 LogManager::instance( ).setForceBuffer(false);


 while (Serial.available( )) {
 char c = (char)Serial.read( );
 consolePrint(String(c));
 if (c == '\n' || c == '\r') {
 consolePrintln("");
 _usbOverflowWarned = false; /* reset anti-spam por rajada */
 if (_usbBuffer.length( ) > 0) {
 _lastRawInput = _usbBuffer;
 _lastFromBt = false; /* origem USB */
 demandOut = parseCommand(_usbBuffer);
 _usbBuffer = "";
 return true;
 }
 printPrompt( );
 } else if (c == 8 || c == 127) {
 if (_usbBuffer.length( ) > 0) _usbBuffer.remove(_usbBuffer.length( ) - 1);
 } else {
 appendCharWithLimit(_usbBuffer, c, _usbOverflowWarned, "USB");
 }
 }


 if (_btMgr.isAuthenticated( )) {
 while (_btMgr.available( )) {
 char c = (char)_btMgr.read( );
 consolePrint(String(c));
 if (c == '\n' || c == '\r') {
 consolePrintln("");
 _btOverflowWarned = false; /* reset anti-spam por rajada */
 if (_btBuffer.length( ) > 0) {
 _lastRawInput = _btBuffer;
 _lastFromBt = true; /* origem BT autenticada */
 demandOut = parseCommand(_btBuffer);
 _btBuffer = "";
 return true;
 }
 printPrompt( );
 } else if (c == 8 || c == 127) {
 if (_btBuffer.length( ) > 0) _btBuffer.remove(_btBuffer.length( ) - 1);
 } else {
 appendCharWithLimit(_btBuffer, c, _btOverflowWarned, "BT");
 }
 }
 }

 return false;
}

void CommandManager::appendCharWithLimit(String& buffer, char c,
 bool& warnedFlag,
 const char* channelName) {
 if (buffer.length( ) >= CLI_LINE_MAX) {
 /* SEC-005/F12.5: linha maior que o limite — descarta para evitar
 * DoS de heap por stream sem '\n'. Pico W tem ~264KB; sem este
 * guard, `yes | cat > /dev/ttyACM0` reallocaria `String` até OOM
 * e potencialmente comprometeria ops paralelas (telemetria TLS,
 * saveConfiguration). O warning é emitido 1x por rajada para não
 * spammar o log; `warnedFlag` é resetada quando linha válida chega. */
 buffer = "";
 if (!warnedFlag) {
 warnedFlag = true;
 LOG_CODE(LOG_WARN, "CLI", CLI_UNKNOWN_CMD, (int)CLI_LINE_MAX,
 String("Linha > ") + (uint32_t)CLI_LINE_MAX +
 " descartada em " + channelName);
 }
 return;
 }
 buffer += c;
}

void CommandManager::hexStringToBytes(String hex, uint8_t* out) {
 if (hex.startsWith("0x")) hex = hex.substring(2);
 for (int i = 0; i < 8; i++) {
 out[i] = (uint8_t)strtoul(hex.substring(i*2, i*2+2).c_str( ), NULL, 16);
 }
}

CliDemand CommandManager::parseCommand(String input) {
 CliDemand cmd;
 cmd.type = CMD_UNKNOWN;
 input.trim( );

 /* #4: detecta sufixo 'confirm' (case-insensitive) e remove antes do parse.
 * Seguro: nenhum comando destrutivo aceita argumentos entre aspas, então
 * 'confirm' bare só pode aparecer no final como intent explícito do user. */
 {
 String tail = input;
 tail.toLowerCase( );
 if (tail.endsWith(" confirm")) {
 cmd.confirmed = true;
 input = input.substring(0, input.length( ) - 8);
 input.trim( );
 }
 }

 int spaceIndex;
 /* expandido de 5→6 slots para acomodar `conf net dns
 * manual <ip1> <ip2>` (6 tokens). O quoted-string catch-all permanece
 * em count==4 (5º slot) — comandos pré-existentes com 5 tokens não
 * afetados. */
 String parts[6];
 int count = 0;
 String tempInput = input;

 while (count < 6 && tempInput.length( ) > 0) {
 if (count == 4 && tempInput.startsWith("\"")) {
 parts[count++] = tempInput;
 break;
 }
 spaceIndex = tempInput.indexOf(' ');
 if (spaceIndex == -1) { parts[count++] = tempInput; tempInput = ""; }
 else { parts[count++] = tempInput.substring(0, spaceIndex); tempInput = tempInput.substring(spaceIndex + 1); tempInput.trim( ); }
 }

 if (count == 0) return cmd;

 String t0 = parts[0]; t0.toLowerCase( );
 String t1 = count > 1 ? parts[1] : ""; t1.toLowerCase( );
 String t2 = count > 2 ? parts[2] : ""; t2.toLowerCase( );
 String t3 = count > 3 ? parts[3] : ""; t3.toLowerCase( );
 String t4 = count > 4 ? parts[4] : "";
 String t5 = count > 5 ? parts[5] : ""; /* dns2 em `conf net dns manual ip1 ip2` */

 /* Valores originais (preservam maiúsculas) para SSID, senha, nome, NTP, etc. */
 String v3 = count > 3 ? parts[3] : "";

 if (t0 == "help" || t0 == "ajuda" || t0 == "?") { cmd.type = CMD_HELP; return cmd; }
 if (t0 == "reload") { cmd.type = CMD_RELOAD; return cmd; }

 /* 'touch sim X Y' — injeta toque (x,y) screen-space.
 * X em [0..319], Y em [0..239]. Útil pra automação de screenshots em
 * todas as telas via /api/screenshot. strVal1=X, strVal2=Y (parsed
 * em executeCommand). */
 if (t0 == "touch" && t1 == "sim") {
 cmd.type = CMD_TOUCH_SIM;
 cmd.setStrVal1(t2.c_str( ));
 cmd.setStrVal2(t3.c_str( ));
 return cmd;
 }

 /* 'screen <NAME>' — muda tela TFT direto via show*Screen
 * methods. Mais robusto que touch sim (bypass gates de pressão).
 * Nomes: dashboard, settings, themes, lang, password, license, status,
 * touchcal, sounds, alarms, graph, stats, calendar, alarmaction. */
 if (t0 == "screen") {
 cmd.type = CMD_GOTO_SCREEN;
 cmd.setStrVal1(t1.c_str( ));
 return cmd;
 }

 if (t0 == "language") {
 cmd.type = CMD_LANGUAGE;
 if (t1 == "pt" || t1 == "pt-br" || t1 == "ptbr") cmd.intVal1 = LANG_PT;
 else if (t1 == "en") cmd.intVal1 = LANG_EN;
 else cmd.intVal1 = -1; /* query */
 return cmd;
 }

 if (t0 == "show") {
 if (t1 == "themes") { cmd.type = CMD_SHOW_THEMES; return cmd; }
 if (t1 == "system" && t2 == "log") { cmd.type = CMD_SHOW_LOGS; return cmd; }
 if (t1 == "sensors") { cmd.type = CMD_SHOW_SENSORS; return cmd; }
 if (t1 == "storage" && t2 == "stats") { cmd.type = CMD_SHOW_STORAGE; return cmd; }
 if (t1 == "system" && t2 == "info") { cmd.type = CMD_SHOW_SYSINFO; return cmd; }
 if (t1 == "net" && t2 == "status") { cmd.type = CMD_SHOW_NET; return cmd; }
 if (t1 == "metrics") { cmd.type = CMD_SHOW_METRICS; return cmd; }
 }

 if (t0 == "conf" || t0 == "configure") {
 /* #7: IP estático — conf ip <dhcp|static|addr|mask|gateway|dns> [valor] */
 if (t1 == "ip") {
 if (t2 == "dhcp") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 0; return cmd; }
 if (t2 == "static") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 1; return cmd; }
 if (t2 == "addr") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 2; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "mask") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 3; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "gateway") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 4; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "dns") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 5; cmd.setStrVal1(v3.c_str( )); return cmd; }
 }

 /* F-NET-TIME.4 — conf ntp <on|off> */
 if (t1 == "ntp") {
 if (t2 == "on") { cmd.type = CMD_SET_NTP_ENABLED; cmd.intVal1 = 1; return cmd; }
 if (t2 == "off") { cmd.type = CMD_SET_NTP_ENABLED; cmd.intVal1 = 0; return cmd; }
 }

 /* MM:SS> */
 if (t1 == "time") {
 cmd.type = CMD_SET_TIME;
 cmd.setStrVal1(t2.c_str( )); /* data (case não importa; só dígitos e '-') */
 cmd.setStrVal2(t3.c_str( )); /* hora */
 return cmd;
 }

 /* F-NET-TIME.4 — conf net dns <auto | manual <ip1> [ip2]> */
 if (t1 == "net" && t2 == "dns") {
 if (t3 == "auto") { cmd.type = CMD_SET_DNS_CFG; cmd.intVal1 = 0; return cmd; }
 if (t3 == "manual") {
 cmd.type = CMD_SET_DNS_CFG; cmd.intVal1 = 1;
 cmd.setStrVal1(t4.c_str( )); /* dns1 obrigatório */
 cmd.setStrVal2(t5.c_str( )); /* dns2 opcional ("" = limpa secundário) */
 return cmd;
 }
 }

 /* #7: Limites/alarme/calibração por sensor — conf sensor <campo> <gpio> <valor>
 * campos: tmin, tmax, hmin, hmax, alarm (on/off), calib (offset float). */
 if (t1 == "sensor") {
 bool isField = (t2 == "tmin" || t2 == "tmax" || t2 == "hmin" ||
 t2 == "hmax" || t2 == "alarm");
 if (isField) {
 cmd.type = CMD_SENSOR_FIELD;
 cmd.setStrVal1(t2.c_str( ));
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1); /* gpio */
 cmd.setStrVal2(t4.c_str( )); /* valor como string */
 return cmd;
 }
 }

 /* #7: User management — conf user <add|del|pass> <username> [pass] */
 if (t1 == "user") {
 if (t2 == "add" && v3.length( ) > 0) {
 cmd.type = CMD_USER_ADD;
 cmd.setStrVal1(v3.c_str( ));
 cmd.setStrVal2(count > 4 ? parts[4].c_str( ) : "");
 return cmd;
 }
 if (t2 == "del" && v3.length( ) > 0) {
 cmd.type = CMD_USER_DEL; cmd.setStrVal1(v3.c_str( )); return cmd;
 }
 if (t2 == "pass" && v3.length( ) > 0) {
 cmd.type = CMD_USER_PASS; cmd.setStrVal1(v3.c_str( ));
 cmd.setStrVal2(count > 4 ? parts[4].c_str( ) : "");
 return cmd;
 }
 }

 if (t1 == "system") {
 if (t2 == "theme") { cmd.type = CMD_SET_THEME; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "name") { cmd.type = CMD_SET_SYS_NAME; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "ssid") { cmd.type = CMD_SET_WIFI_SSID; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "pass") { cmd.type = CMD_SET_WIFI_PASS; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "timezone") {
 cmd.type = CMD_SET_TIMEZONE;
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1);
 return cmd;
 }
 if (t2 == "ntp") { cmd.type = CMD_SET_NTP; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "admin" && t3 == "reset") { cmd.type = CMD_RESET_ADMIN; return cmd; }
 if (t2 == "touch" && t3 == "reset") { cmd.type = CMD_RESET_TOUCH_CAL; return cmd; }
 if (t2 == "factory") { cmd.type = CMD_FACTORY_RESET; return cmd; }
 if (t2 == "history_interval") {
 cmd.type = CMD_SET_HISTORY_INTERVAL;
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1);
 return cmd;
 }
 }
 if (t1 == "sensor" && t2 == "ds18b20" && t3 == "resolution") {
 cmd.type = CMD_SET_DS_RES;
 cmd.intVal1Valid = parseIntStrict(t4, cmd.intVal1);
 return cmd;
 }
 /* Porta do servidor web: conf web port <n> */
 if (t1 == "web" && t2 == "port") {
 cmd.type = CMD_SET_WEB_PORT;
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1);
 return cmd;
 }

 if (t1 == "tel") {
 if (t2 == "server") { cmd.type = CMD_SET_TEL_SERVER; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "port") {
 cmd.type = CMD_SET_TEL_PORT;
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1);
 return cmd;
 }
 if (t2 == "path") { cmd.type = CMD_SET_TEL_PATH; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "batch") {
 cmd.type = CMD_SET_TEL_BATCH;
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1);
 return cmd;
 }
 if (t2 == "interval") {
 cmd.type = CMD_SET_TEL_INTERVAL;
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1);
 return cmd;
 }
 if (t2 == "crypto") {
 cmd.type = CMD_SET_TEL_CRYPTO;
 /* strVal1 preserva o token original p/ validação no executor. */
 cmd.setStrVal1(t3.c_str( ));
 cmd.boolVal = (t3 == "on");
 return cmd;
 }
 if (t2 == "mode") {
 cmd.type = CMD_SET_TEL_MODE;
 cmd.setStrVal1(t3.c_str( )); /* preserva p/ validação */
 if(t3 == "json") cmd.intVal1 = TEL_MODE_JSON;
 else if(t3 == "csv") cmd.intVal1 = TEL_MODE_CSV;
 else if(t3 == "custom") cmd.intVal1 = TEL_MODE_CUSTOM;
 else cmd.intVal1 = -1; /* sinaliza modo desconhecido */
 return cmd;
 }
 }
 }

 if (t0 == "sensor") {
 if (t1 == "scan") { cmd.type = CMD_SCAN_SENSORS; return cmd; }
 if (t1 == "define") {
 int idx = input.indexOf("define");
 String args = input.substring(idx + 7); args.trim( );

 int sp1 = args.indexOf(' ');
 if (sp1 != -1) {
 cmd.intVal1Valid = parseIntStrict(args.substring(0, sp1), cmd.intVal1);
 args = args.substring(sp1 + 1); args.trim( );

 int sp2 = args.indexOf(' ');
 if (sp2 != -1) {
 String romHex = args.substring(0, sp2);
 hexStringToBytes(romHex, cmd.rom);

 args = args.substring(sp2 + 1); args.trim( );
 int sp3 = args.indexOf(' ');
 if (sp3 != -1) {
 /* CON-005b: String temporária vive até o ; final — safeCopy copia dentro. */
 cmd.setStrVal1(args.substring(0, sp3).c_str( ));
 /* friendlyName pode ter aspas circundantes — strip antes do copy. */
 String fname = args.substring(sp3 + 1);
 fname.replace("\"", "");
 cmd.setStrVal2(fname.c_str( ));
 cmd.type = CMD_DEFINE_SENSOR;
 return cmd;
 }
 }
 }
 }
 }

 if (t0 == "sensor" && t1 == "wipe") {
 cmd.type = CMD_WIPE_SENSOR;
 cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
 return cmd;
 }

 if (t0 == "sensor" && t1 == "accept") {
 cmd.type = CMD_ACCEPT_SENSOR;
 cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
 return cmd;
 }

 /* removido CMD_DBG_SENSOR_HISTORY_ALL (debug TEST-ONLY
 * introduzido em v3.24.12). Liberou ~1-2 KB pra F-FLASH-DIET. */

 if (t0 == "write" && t1 == "memory") { cmd.type = CMD_WRITE_MEMORY; return cmd; }
 if (t0 == "clear" && t1 == "log") { cmd.type = CMD_CLEAR_LOGS; return cmd; }
 if (t0 == "tel" && t1 == "sync") { cmd.type = CMD_TEL_SYNC; return cmd; }
 if (t0 == "tel" && t1 == "dump") { cmd.type = CMD_TEL_DUMP; return cmd; }
 if (t0 == "tel" && t1 == "reset") { cmd.type = CMD_TEL_RESET; return cmd; }

 if (t0 == "debug") {
 cmd.type = CMD_DEBUG;
 if (t1 == "on") cmd.intVal1 = 1;
 else if (t1 == "off") cmd.intVal1 = 0;
 else cmd.intVal1 = -1; /* query sem argumento */
 return cmd;
 }

 return cmd;
}

void CommandManager::printLogEntry(String line) {
 if (line.length( ) < 5) return;
 String parts[8]; int idx = 0; int start = 0; int end = line.indexOf(';');
 while (end != -1 && idx < 8) {
 parts[idx++] = line.substring(start, end); start = end + 1; end = line.indexOf(';', start);
 }
 if (idx < 8) parts[idx++] = line.substring(start);
 if (idx < 7) return;

 time_t ts = (time_t)parts[0].toInt( );
 unsigned long ms = (unsigned long)parts[1].toInt( );
 int core = parts[2].toInt( );
 LogLevel lvl = (LogLevel)parts[3].toInt( );
 String tag = parts[4];
 int code = parts[5].toInt( );
 int ctx = parts[6].toInt( );
 String extra = (idx > 7) ? parts[7] : "";

 uint32_t sec = ms / 1000;
 uint32_t d = sec / 86400; sec %= 86400;
 uint32_t h = sec / 3600; sec %= 3600;
 uint32_t m = sec / 60; sec %= 60;

 char dateBuf[32], upBuf[20];
 if (ts > 1000000000) {
 struct tm *ti = localtime(&ts);
 snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d %02d:%02d:%02d", ti->tm_year+1900, ti->tm_mon+1, ti->tm_mday, ti->tm_hour, ti->tm_min, ti->tm_sec);
 } else { snprintf(dateBuf, sizeof(dateBuf), "BOOT+%lus", ms/1000); }

 if (d > 0) snprintf(upBuf, sizeof(upBuf), "%lud %02lu:%02lu:%02lu", d, h, m, sec);
 else snprintf(upBuf, sizeof(upBuf), "%02lu:%02lu:%02lu", h, m, sec);

 const char* lvlStr = LogManager::instance( ).getLevelString(lvl);
 consolePrintf("%s C%d/%s [%s]\n", dateBuf, core, lvlStr, tag.c_str( ));
 consolePrintf(" UP %s Code:%d", upBuf, code);
 if (ctx != 0) consolePrintf(" (ctx:%d)", ctx);
 consolePrintln("");
 if (extra.length( ) > 0) consolePrintf(" %s\n", extra.c_str( ));
}

void CommandManager::printWelcome( ) {
 consolePrintln("");
 consolePrintln("===========================================");
 consolePrintf(" SIMUT IoT CLI %s\n", SIMUT_VERSION);
 if (isPt( )) {
 consolePrintln(" Digite 'help' (ou 'ajuda', '?')");
 consolePrintln(" For English: 'language en'");
 } else {
 consolePrintln(" Type 'help' for commands");
 consolePrintln(" Para Portugues: 'language pt'");
 }
 consolePrintln("===========================================");
}

void CommandManager::printPrompt( ) { consolePrint(_debugMode ? "SIMUT# " : "SIMUT> "); }
void CommandManager::printDivider( ) { consolePrintln("-------------------------------------------"); }

String CommandManager::formatRom(const uint8_t* rom) {
 char buff[18];
 snprintf(buff, sizeof(buff), "%02X%02X%02X%02X%02X%02X%02X%02X", rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
 return String(buff);
}

void CommandManager::renderSensorTable(const SensorRecord* sensors, int maxSensors) {
 consolePrintln("");
 consolePrintln(isPt( ) ? "--- Sensores Configurados ---"
 : "--- Configured Sensors ---");
 bool found = false;
 for(int i=0; i<maxSensors; i++) {
 if (sensors[i].active) {
 found = true;
 char line[64];
 String romStr = formatRom(sensors[i].rom);
 snprintf(line, sizeof(line), " [Slot %02d] %s", sensors[i].gpio, sensors[i].hwId);
 consolePrintln(line);
 consolePrintf(" ROM: %s\n", romStr.c_str( ));
 consolePrintf(isPt( ) ? " Nome: %s\n" : " Name: %s\n",
 sensors[i].friendlyName);
 }
 }
 if (!found) consolePrintln(isPt( ) ? " (banco vazio)" : " (database is empty)");
 printDivider( );
}

void CommandManager::renderScanResults(const std::vector<ScanResult> &results) {
 consolePrintln("");
 consolePrintln(isPt( ) ? "--- Varredura de Hardware ---"
 : "--- Hardware Scan Results ---");
 if (results.empty( )) {
 consolePrintln(isPt( ) ? " (nenhum sensor fisico detectado)"
 : " (no physical sensors detected)");
 } else {
 for (const auto &res : results) {
 const char* typeStr;
 String details;
 if (res.type == TYPE_DS18B20) { typeStr = "DS18B20"; details = formatRom(res.rom); }
 else if (res.type == TYPE_DHT22) {
 typeStr = "DHT22";
 details = isPt( ) ? "(Temp/Umid capaz)" : "(Temp/Hum capable)";
 }
 else {
 typeStr = "UNKNOWN";
 details = isPt( ) ? "Sinal detectado" : "Signal detected";
 }
 consolePrintf(" [Pin %02d] %s\n", res.pin, typeStr);
 consolePrintf(" %s\n", details.c_str( ));
 }
 }
 printInfo(isPt( ) ? " Dica: use 'sensor define' para mapear."
 : " Tip: use 'sensor define' to map.");
 printDivider( );
}

void CommandManager::renderSystemInfo(const SystemConfig &cfg) {
 const bool pt = isPt( );
 printDivider( );
 consolePrintln(pt ? " [SISTEMA]" : " [SYSTEM]");
 consolePrintln(pt ? " Dispositivo:" : " Device:");
 consolePrintf (" %s\n", cfg.deviceName);
 consolePrintf (" Firmware: %s\n", SIMUT_VERSION);
 /* serial do Pico (16 hex) — usado como chave em calib.csv
 * para a calibração do sensor ambient (DHT22). */
 consolePrintf (" Serial: %s\n", StorageManager::getBoardSerialNumber( ).c_str( ));
 consolePrintln(pt ? " [SENSORES]" : " [SENSORS]");
 consolePrintf (pt ? " Precisao DS18: %d-bit\n" : " DS18 Precision: %d-bit\n",
 cfg.ds18Resolution);
 consolePrintln(pt ? " [CONECTIVIDADE]" : " [CONNECTIVITY]");
 consolePrintln(" WiFi SSID:");
 consolePrintf (" %s\n", (strlen(cfg.wifiSsid) > 0 ? cfg.wifiSsid
 : (pt ? "<nao configurado>" : "<not configured>")));
 consolePrintf (pt ? " Fuso: GMT%s%d\n" : " Timezone: GMT%s%d\n",
 (cfg.timezoneOffset >= 0 ? "+" : ""), cfg.timezoneOffset);
 consolePrintln(pt ? " Servidor NTP:" : " NTP Server:");
 consolePrintf (" %s\n", (strlen(cfg.ntpServer) > 0 ? cfg.ntpServer : "pool.ntp.org (default)"));
 consolePrintf (pt ? " Logging: %s\n" : " Logging: %s\n",
 cfg.loggingEnabled ? (pt ? "ATIVO" : "ENABLED")
 : (pt ? "INATIVO" : "DISABLED"));
 printDivider( );
}

void CommandManager::renderMetrics( ) {
 const bool pt = isPt( );
 const SystemMetrics& m = MetricsManager::instance( ).data( );

 /* Força amostragem fresca de heap + largest block para o snapshot atual. */
 MetricsManager::instance( ).sampleHeap( );
 MetricsManager::instance( ).sampleLargestBlock( );

 uint32_t upSec = millis( ) / 1000;
 uint32_t d = upSec / 86400; upSec %= 86400;
 uint32_t h = upSec / 3600; upSec %= 3600;
 uint32_t mn = upSec / 60; upSec %= 60;

 printDivider( );
 consolePrintln(pt ? " [METRICAS OPERACIONAIS]" : " [OPERATIONAL METRICS]");
 if (d > 0) consolePrintf (pt ? " Uptime: %lud %02lu:%02lu:%02lu\n"
 : " Uptime: %lud %02lu:%02lu:%02lu\n",
 d, h, mn, upSec);
 else consolePrintf (pt ? " Uptime: %02lu:%02lu:%02lu\n"
 : " Uptime: %02lu:%02lu:%02lu\n",
 h, mn, upSec);
 consolePrintf (pt ? " Heap: %lu B (min: %lu B)\n"
 : " Heap: %lu B (min: %lu B)\n",
 (unsigned long)m.heapFreeNow,
 (unsigned long)(m.heapMinSeen == 0xFFFFFFFF ? 0 : m.heapMinSeen));
 consolePrintf (pt ? " Maior bloco: %lu B (min: %lu B)\n"
 : " Largest blk: %lu B (min: %lu B)\n",
 (unsigned long)m.heapLargestBlock,
 (unsigned long)(m.heapLargestMin == 0xFFFFFFFF ? 0 : m.heapLargestMin));

 consolePrintln(pt ? " [REDE]" : " [NETWORK]");
 consolePrintf (pt ? " WiFi conns: %lu\n" : " WiFi conns: %lu\n",
 (unsigned long)m.wifiReconnects);
 consolePrintf (pt ? " MQTT conns: %lu\n" : " MQTT conns: %lu\n",
 (unsigned long)m.mqttReconnects);
 if (m.rssiMax > -127) {
 consolePrintf(pt ? " RSSI: %ld dBm (min:%ld max:%ld)\n"
 : " RSSI: %ld dBm (min:%ld max:%ld)\n",
 (long)m.rssiNow, (long)m.rssiMin, (long)m.rssiMax);
 } else {
 consolePrintln(pt ? " RSSI: (nao amostrado)" : " RSSI: (not sampled yet)");
 }

 consolePrintln(pt ? " [TELEMETRIA]" : " [TELEMETRY]");
 consolePrintf (pt ? " Enviadas: %lu\n" : " Sent OK: %lu\n",
 (unsigned long)m.telSent);
 consolePrintf (pt ? " Falhas: %lu\n" : " Failed: %lu\n",
 (unsigned long)m.telFailed);
 consolePrintf (pt ? " Retries: %lu\n" : " Retries: %lu\n",
 (unsigned long)m.telRetries);
 consolePrintf (pt ? " Bytes: %lu\n" : " Bytes: %lu\n",
 (unsigned long)m.telTotalBytes);
 consolePrintf (pt ? " Ult. lat: %lu ms\n" : " Last lat: %lu ms\n",
 (unsigned long)m.telLastLatencyMs);

 consolePrintln(pt ? " [SENSORES]" : " [SENSORS]");
 consolePrintf (pt ? " Leituras OK: %lu\n" : " Reads OK: %lu\n",
 (unsigned long)m.sensorReadsOk);
 consolePrintf (pt ? " Leituras erro: %lu\n" : " Reads error: %lu\n",
 (unsigned long)m.sensorReadsErr);

 consolePrintln(pt ? " [STORAGE]" : " [STORAGE]");
 consolePrintf (pt ? " Config saves: %lu\n" : " Config saves: %lu\n",
 (unsigned long)m.configSaves);
 printDivider( );
}

void CommandManager::renderSensorReading(const SensorReading &reading) {
 if (!reading.isValid) {
 consolePrintf(isPt( ) ? "[%s] Erro de leitura/checksum\n"
 : "[%s] Read/Checksum Error\n",
 reading.typeName);
 return;
 }
 if (strcmp(reading.typeName, "DHT22") == 0) { consolePrintf("[DHT] T:%.2fC H:%.2f%%\n", reading.value1, reading.value2); }
 else if (strcmp(reading.typeName, "DS18B20") == 0) { consolePrintf("[DS18] T:%.4fC\n", reading.value1); }
 else { consolePrintf("[%s] %.2f\n", reading.typeName, reading.value1); }
}

void CommandManager::printSuccess(String msg) { consolePrint("OK: "); consolePrintln(msg); }
void CommandManager::printError(String msg) { consolePrint("ERROR: "); consolePrintln(msg); }
void CommandManager::printInfo(String msg) { consolePrintln(msg); }

void CommandManager::printHelp( ) {
 /* F-LANGPACK alpha18: PT vem do @HELP do .lng (UTF-8 → unaccent
 * para o terminal ASCII). EN agora sempre inline em PROGMEM
 * (HELP_TEXT_EN), sem dependência de LittleFS. */
 if (isPt( )) {
 const char* langHelp = DisplayManager::getActiveHelpText( );
 if (langHelp) {
 const char* line = langHelp;
 char asciiBuf[160];
 char rawBuf[160];
 while (*line) {
 feedWdt( );
 size_t i = 0;
 while (line[i] && line[i] != '\n' && i + 1 < sizeof(rawBuf)) {
 rawBuf[i] = line[i]; i++;
 }
 rawBuf[i] = '\0';
 if (i > 0 && rawBuf[i-1] == '\r') rawBuf[i-1] = '\0';
 DisplayManager::unaccent(rawBuf, asciiBuf, sizeof(asciiBuf));
 consolePrintln(String(asciiBuf));
 line += i;
 if (*line == '\n') line++;
 }
 return;
 }
 /* PT pedido mas .lng não tem @HELP — cai pro EN PROGMEM abaixo. */
 }

 /* Itera linhas do PROGMEM: pgm_read_byte para acesso byte-a-byte */
 char buf[160];
 size_t bi = 0;
 size_t i = 0;
 char c;
 while ((c = (char)pgm_read_byte(&HELP_TEXT_EN[i++])) != '\0') {
 if (c == '\n') {
 buf[bi] = '\0';
 if (bi > 0 && buf[bi-1] == '\r') buf[bi-1] = '\0';
 consolePrintln(String(buf));
 bi = 0;
 feedWdt( );
 } else if (bi + 1 < sizeof(buf)) {
 buf[bi++] = c;
 }
 }
 if (bi > 0) { buf[bi] = '\0'; consolePrintln(String(buf)); }
}

/* Stub — conteúdo de printHelpExtras agora vive em /help_{pt,en}.txt
 * (concatenado ao final do help principal). Mantido apenas para compat
 * com qualquer chamador externo que ainda referencie o símbolo. */
void CommandManager::printHelpExtras( ) { /* no-op: text moved to FS */ }
