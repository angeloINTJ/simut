/**
 * @file CommandManager.cpp
 * @brief Implementation of CommandManager — command parsing, rendering, and I/O routing.
 * @details Implements the CLI parser that converts text input into CliDemand
 * structs, along with formatted output for sensor tables, scan results,
 * system info, and log entries. Supports dual-buffer USB/BT processing.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "CommandManager.h"
#include "CommandParser.h"
#include "LogManager.h"
#include "DisplayManager.h" /* F-LANGPACK Etapa 3: getActiveHelpText */
#include "MetricsManager.h"
#include "FlashIrqProbe.h" /* T0.1: janela real de IRQ-off no show metrics */
#include "StorageManager.h" /* getBoardSerialNumber em show system info */
#include "HelpLicenseEN.h" /* HELP_TEXT_EN inline em PROGMEM */
#include "sensors/SensorHelpers.h" /* sensorTypeName, sensorHasChannel, SensorFormat */
#include <LittleFS.h>
#include <time.h>
#include <stdarg.h>

CommandManager::CommandManager( ) {
 _usbBuffer.reserve(128);
 _btBuffer.reserve(128);
}

void CommandManager::begin(const char* btDeviceName) {
 /* BT + welcome/prompt + sink ALL moved to beginBluetooth( ).
 * This is a NO-OP (kept for signature compatibility).
 * Reason: each Serial.println can block ~1s/line pre-USB-CDC-connect
 * (ignoreFlowControl timeout). LOG_CODE -> consoleSink -> consolePrintln ->
 * Serial.print = accumulated boot stall of tens of seconds.
 * After cyw43_arch_init (in WiFi.begin), USB CDC is re-enumerated and the host
 * is generally connected, so prints are fast. */
 (void)btDeviceName;
}

void CommandManager::beginBluetooth(const char* btDeviceName) {
 /* SerialBT.begin accesses CYW43 via SPI/PIO. Caller must guarantee that
 * cyw43_arch_init already ran (WiFi.begin) — otherwise residual
 * chip state post-OTA causes hardfault. */
 _btMgr.begin(btDeviceName);

 /* Install LogManager sink — mirrors logs to USB CDC + BT.
 * Moved here to avoid Serial blocks during boot setup phase. */
 LogManager::instance( ).setConsoleSink([this](const char* line) {
 this->consolePrintln(String(line));
 });

 printWelcome( );
 printPrompt( );
}

void CommandManager::setBtValidator(BtAuthValidator validator) {
 _btMgr.setValidator(validator);
}


/* v1.4.1: Non-blocking USB serial. Guard with `if (Serial)` so writes are
 * skipped when CDC is not connected, preventing boot hang after warm boot.
 * ignoreFlowControl(true) was removed from setup() — without it,
 * Serial drops data silently when the host isn't reading. */
void CommandManager::consolePrint(const String& msg) {
 if (Serial) Serial.print(msg);
 _btMgr.print(msg);
}

void CommandManager::consolePrintln(const String& msg) {
 if (Serial) Serial.println(msg);
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
 /* Defer LOG_CODE flash writes during BT update: prevents
 * writeCompactToFlash from triggering Core 1 lockout while the
 * BluetoothManager is processing auth + banner I/O.
 * Logs stay in RAM (_pendingLogs) and are flushed on the next
 * normal write or via flushPendingIfAny in the main loop. */
 LogManager::instance( ).setForceBuffer(true);
 _btMgr.update( );
 LogManager::instance( ).setForceBuffer(false);


 while (Serial.available( )) {
 char c = (char)Serial.read( );
 consolePrint(String(c));
 if (c == '\n' || c == '\r') {
 consolePrintln("");
 _usbOverflowWarned = false; /* reset anti-spam per burst */
 if (_usbBuffer.length( ) > 0) {
 _lastRawInput = _usbBuffer;
 _lastFromBt = false; /* USB origin */
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
 _btOverflowWarned = false; /* reset anti-spam per burst */
 if (_btBuffer.length( ) > 0) {
 _lastRawInput = _btBuffer;
 _lastFromBt = true; /* authenticated BT origin */
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
 /* Line exceeding limit — discard to prevent heap DoS from a stream
 * without '\n'. Pico W has ~264KB; without this guard,
 * `yes | cat > /dev/ttyACM0` would reallocate `String` until OOM
 * and could compromise parallel ops (TLS telemetry,
 * saveConfiguration). The warning is emitted once per burst to
 * avoid log spam; `warnedFlag` is reset when a valid line arrives. */
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

CliDemand CommandManager::parseCommand(String input) {
 /* ── Sensor-mode auto-prefix (Cisco IOS sub-mode shorthand) ──
  * In SENSOR_CONFIG mode, bare commands like 'type dht22', 'name X',
  * 'pin 0,5' are automatically prefixed with 'sensor <N>' so the user
  * doesn't need to repeat the slot number.
  * Navigation commands (exit, end, ?, help, do, show) are NOT prefixed. */
 if (_cliMode == CLI_MODE_SENSOR_CONFIG && _configSensorSlot >= 0) {
  String lower = input; lower.toLowerCase( );
  lower.trim( );
  bool isNav = lower.startsWith("exit") || lower.startsWith("end") ||
               lower == "?" || lower == "help" || lower == "ajuda" ||
               lower.startsWith("do ") || lower.startsWith("show ") ||
               lower == "enable" || lower == "disable";
  if (!isNav && lower.length( ) > 0) {
   String prefixed = "sensor " + String(_configSensorSlot) + " " + input;
   return parseCliCommand(prefixed);
  }
 }
 return parseCliCommand(input);
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

void CommandManager::printPrompt( ) {
 /* Cisco IOS hierarchical prompt — mode determines character and context.
  * Debug mode streams logs inline but no longer hijacks the prompt char. */
 switch (_cliMode) {
 case CLI_MODE_SENSOR_CONFIG:
  consolePrintf("SIMUT(config-sensor-%d)# ", _configSensorSlot >= 0 ? _configSensorSlot : 0);
  break;
 case CLI_MODE_GLOBAL_CONFIG:
  consolePrint("SIMUT(config)# ");
  break;
 case CLI_MODE_PRIV_EXEC:
  consolePrint("SIMUT# ");
  break;
 case CLI_MODE_USER_EXEC:
 default:
  consolePrint("SIMUT> ");
  break;
 }
}
void CommandManager::setDebugMode(bool enabled) {
 _debugMode = enabled;
 /* Debug mode now only controls log streaming (LogManager::setConsoleStream).
  * Prompt is determined by CLI mode (_cliMode), not debug state. */
}

void CommandManager::setCliMode(CLIMode mode) {
 _cliMode = mode;
 if (mode != CLI_MODE_SENSOR_CONFIG) _configSensorSlot = -1;
}

void CommandManager::setConfigSensorSlot(int8_t slot) {
 _configSensorSlot = slot;
 if (slot >= 0) _cliMode = CLI_MODE_SENSOR_CONFIG;
}

bool CommandManager::isConfigMode( ) const {
 return _cliMode == CLI_MODE_GLOBAL_CONFIG || _cliMode == CLI_MODE_SENSOR_CONFIG;
}

bool CommandManager::isPrivOrHigher( ) const {
 return _cliMode >= CLI_MODE_PRIV_EXEC;
}

void CommandManager::printDivider( ) { consolePrintln("-------------------------------------------"); }

/* ── Mode validity mask table — one byte per DemandType ── */

uint8_t getCommandModeMask(DemandType t) {
 switch (t) {
 /* Diagnostic / Show — valid in all modes */
 case CMD_HELP:              return CLI_VALID_ALL;
 case CMD_SHOW_THEMES:       return CLI_VALID_READONLY;
 case CMD_SHOW_LOGS:         return CLI_VALID_READONLY;
 case CMD_SHOW_SENSORS:      return CLI_VALID_READONLY;
 case CMD_SHOW_STORAGE:      return CLI_VALID_READONLY;
 case CMD_SHOW_SYSINFO:      return CLI_VALID_READONLY;
 case CMD_SHOW_NET:          return CLI_VALID_READONLY;
 case CMD_SHOW_METRICS:      return CLI_VALID_READONLY;
 case CMD_SHOW_SENSOR_TYPES: return CLI_VALID_READONLY;
 case CMD_SHOW_GPIO:         return CLI_VALID_READONLY;
 /* Session — exec modes */
 case CMD_LANGUAGE:          return CLI_VALID_USER | CLI_VALID_PRIV;
 case CMD_DEBUG:             return CLI_VALID_USER | CLI_VALID_PRIV;
 case CMD_SET_TIME:          return CLI_VALID_USER | CLI_VALID_PRIV;
 case CMD_SCAN_SENSORS:      return CLI_VALID_USER | CLI_VALID_PRIV;
 /* Telemetry — exec modes */
 case CMD_TEL_SYNC:          return CLI_VALID_USER | CLI_VALID_PRIV;
 case CMD_TEL_DUMP:          return CLI_VALID_USER | CLI_VALID_PRIV;
 case CMD_TEL_RESET:         return CLI_VALID_PRIV;
 /* Privileged EXEC only */
 case CMD_WRITE_MEMORY:      return CLI_VALID_PRIV;
 case CMD_CLEAR_LOGS:        return CLI_VALID_PRIV;
 case CMD_RELOAD:            return CLI_VALID_PRIV;
 case CMD_RESET_TOUCH_CAL:   return CLI_VALID_PRIV;
 case CMD_FACTORY_RESET:     return CLI_VALID_PRIV;
 case CMD_FORMAT_FS:         return CLI_VALID_PRIV;
 case CMD_RESET_ADMIN:       return CLI_VALID_PRIV;
 case CMD_WIPE_SENSOR:       return CLI_VALID_PRIV;
 case CMD_REMOVE_SENSOR:     return CLI_VALID_PRIV;
 case CMD_DEFINE_SENSOR:     return CLI_VALID_PRIV;
 case CMD_ACCEPT_SENSOR:     return CLI_VALID_PRIV;
 case CMD_TOUCH_SIM:         return CLI_VALID_PRIV;
 case CMD_GOTO_SCREEN:       return CLI_VALID_PRIV;
 /* Global Config */
 case CMD_SET_THEME:         return CLI_VALID_CONFIG;
 case CMD_SET_DS_RES:        return CLI_VALID_CONFIG;
 case CMD_SET_SYS_NAME:      return CLI_VALID_CONFIG;
 case CMD_SET_WIFI_SSID:     return CLI_VALID_CONFIG;
 case CMD_SET_WIFI_PASS:     return CLI_VALID_CONFIG;
 case CMD_SET_TIMEZONE:      return CLI_VALID_CONFIG;
 case CMD_SET_NTP:           return CLI_VALID_CONFIG;
 case CMD_SET_TEL_SERVER:    return CLI_VALID_CONFIG;
 case CMD_SET_TEL_PORT:      return CLI_VALID_CONFIG;
 case CMD_SET_TEL_PATH:      return CLI_VALID_CONFIG;
 case CMD_SET_TEL_BATCH:     return CLI_VALID_CONFIG;
 case CMD_SET_TEL_INTERVAL:  return CLI_VALID_CONFIG;
 case CMD_SET_TEL_CRYPTO:    return CLI_VALID_CONFIG;
 case CMD_SET_TEL_MODE:      return CLI_VALID_CONFIG;
 case CMD_SET_HISTORY_INTERVAL: return CLI_VALID_CONFIG;
 case CMD_SET_NTP_ENABLED:   return CLI_VALID_CONFIG;
 case CMD_SET_DNS_CFG:       return CLI_VALID_CONFIG;
 case CMD_IP_CFG:            return CLI_VALID_CONFIG;
 case CMD_USER_ADD:          return CLI_VALID_CONFIG;
 case CMD_USER_DEL:          return CLI_VALID_CONFIG;
 case CMD_USER_PASS:         return CLI_VALID_CONFIG;
 case CMD_SET_WEB_PORT:      return CLI_VALID_CONFIG;
 /* Sensor sub-commands — privileged + sensor config mode */
 case CMD_SENSOR_FIELD:      return CLI_VALID_PRIV | CLI_VALID_SENSOR;
 case CMD_SENSOR_ENTER:      return CLI_VALID_CONFIG;  /* enter sensor mode from config */
 /* Navigation */
 case CMD_ENABLE:            return CLI_VALID_USER;
 case CMD_DISABLE:           return CLI_VALID_PRIV;
 case CMD_CONFIGURE:         return CLI_VALID_PRIV;
 case CMD_EXIT:              return CLI_VALID_PRIV | CLI_VALID_CONFIG | CLI_VALID_SENSOR;
 case CMD_END:               return CLI_VALID_CONFIG | CLI_VALID_SENSOR;
 case CMD_DO:                return CLI_VALID_CONFIG | CLI_VALID_SENSOR;
 default:                    return CLI_VALID_ALL;
 }
}

const char* getModePromptSuffix(CLIMode mode) {
 switch (mode) {
 case CLI_MODE_USER_EXEC:     return ">";
 case CLI_MODE_PRIV_EXEC:     return "#";
 case CLI_MODE_GLOBAL_CONFIG: return "(config)#";
 case CLI_MODE_SENSOR_CONFIG: return "(config-sensor)#";
 default:                     return ">";
 }
}

const char* getModeHelpLine(CLIMode mode, bool pt) {
 switch (mode) {
 case CLI_MODE_USER_EXEC:
  return pt ? "Modo EXEC — 'show' diagnostico, 'enable' p/ configurar"
            : "EXEC mode — 'show' diagnostics, 'enable' to configure";
 case CLI_MODE_PRIV_EXEC:
  return pt ? "Modo Privilegiado — 'configure terminal' p/ config, 'write memory' p/ salvar"
            : "Privileged mode — 'configure terminal' to config, 'write memory' to save";
 case CLI_MODE_GLOBAL_CONFIG:
  return pt ? "Modo Config Global — 'exit' voltar, 'end' p/ privilegiado, 'sensor <N>' sub-config"
            : "Global Config — 'exit' back, 'end' to privileged, 'sensor <N>' sub-config";
 case CLI_MODE_SENSOR_CONFIG:
  return pt ? "Modo Sensor — 'type/name/pin/active/alarm', 'exit' voltar, 'end' p/ privilegiado"
            : "Sensor Config — 'type/name/pin/active/alarm', 'exit' back, 'end' to privileged";
 default: return "";
 }
}

void CommandManager::printModeHelp( ) {
 const bool pt = isPt( );
 uint8_t curMask = (1 << _cliMode);

 printDivider( );
 consolePrintln(getModeHelpLine(_cliMode, pt));
 consolePrintln("");

 /* ── Navigation (always first) ── */
 if (_cliMode == CLI_MODE_USER_EXEC) {
  consolePrintln(pt ? "  enable                Entrar modo privilegiado"
                    : "  enable                Enter privileged mode");
 }
 if (_cliMode == CLI_MODE_PRIV_EXEC) {
  consolePrintln(pt ? "  disable               Voltar ao modo EXEC"
                    : "  disable               Return to EXEC mode");
  consolePrintln(pt ? "  configure terminal    Entrar modo configuracao"
                    : "  configure terminal    Enter configuration mode");
 }
 if (isConfigMode( )) {
  consolePrintln(pt ? "  exit                  Sair do modo atual"
                    : "  exit                  Exit current mode");
  consolePrintln(pt ? "  end                   Voltar ao modo privilegiado"
                    : "  end                   Return to privileged mode");
  consolePrintln(pt ? "  do <cmd>              Executar comando do modo #"
                    : "  do <cmd>              Execute privileged-mode command");
 }
 consolePrintln("");

 /* ── Show commands (all modes except sensor config get the full list) ── */
 if (_cliMode != CLI_MODE_SENSOR_CONFIG) {
  auto showIf = [&](DemandType t, const char* s) { if (getCommandModeMask(t) & curMask) consolePrintln(s); };
  showIf(CMD_SHOW_SENSORS,    pt ? "  show sensors           Listar sensores"       : "  show sensors           List sensors");
  showIf(CMD_SHOW_SYSINFO,    pt ? "  show system info      Info do sistema"        : "  show system info      System info");
  showIf(CMD_SHOW_NET,        pt ? "  show net status       Status da rede"         : "  show net status       Network status");
  showIf(CMD_SHOW_METRICS,    pt ? "  show metrics          Metricas operacionais"   : "  show metrics          Operational metrics");
  showIf(CMD_SHOW_STORAGE,    pt ? "  show storage stats    Estatisticas flash"     : "  show storage stats    Flash statistics");
  showIf(CMD_SHOW_THEMES,     pt ? "  show themes           Listar temas"           : "  show themes           List themes");
  showIf(CMD_SHOW_GPIO,       pt ? "  show gpio             Mapa de GPIOs"          : "  show gpio             GPIO map");
  showIf(CMD_SHOW_SENSOR_TYPES, pt?"show sensor types     Tipos compilados"        : "  show sensor types     Compiled types");
  showIf(CMD_SHOW_LOGS,       pt ? "  show system log       Log de eventos"         : "  show system log       Event log");
 }

 /* ── Privileged EXEC extras ── */
 if (_cliMode == CLI_MODE_PRIV_EXEC) {
  consolePrintln("");
  consolePrintln(pt ? "  --- Manutencao ---" : "  --- Maintenance ---");
  auto showIf = [&](DemandType t, const char* s) { if (getCommandModeMask(t) & curMask) consolePrintln(s); };
  showIf(CMD_WRITE_MEMORY,     pt ? "  write memory          Salvar config no Flash"
                                  : "  write memory          Save config to Flash");
  showIf(CMD_RELOAD,           pt ? "  reload [confirm]      Reiniciar sistema"
                                  : "  reload [confirm]      Reboot system");
  showIf(CMD_CLEAR_LOGS,       pt ? "  clear log [confirm]   Apagar logs"
                                  : "  clear log [confirm]   Clear system logs");
  showIf(CMD_DEBUG,            pt ? "  debug <on|off>        Stream logs no console"
                                  : "  debug <on|off>        Stream logs to console");
  showIf(CMD_FORMAT_FS,        pt ? "  system format [confirm]  Formatar LittleFS + reboot"
                                  : "  system format [confirm]  Format LittleFS + reboot");
  showIf(CMD_FACTORY_RESET,    pt ? "  conf system factory [confirm]  Factory reset"
                                  : "  conf system factory [confirm]  Factory reset");
  consolePrintln("");
  consolePrintln(pt ? "  --- Sensores ---" : "  --- Sensors ---");
  showIf(CMD_SCAN_SENSORS,     pt ? "  sensor scan           Escanear hardware"
                                  : "  sensor scan           Scan hardware bus");
  showIf(CMD_ACCEPT_SENSOR,    pt ? "  sensor accept <gpio>  Aceitar sensor OneWire"
                                  : "  sensor accept <gpio>  Accept OneWire sensor");
  showIf(CMD_DEFINE_SENSOR,    pt ? "  sensor define <gpio> <rom> <hwid> <nome> [tipo]"
                                  : "  sensor define <gpio> <rom> <hwid> <name> [type]");
  showIf(CMD_WIPE_SENSOR,      pt ? "  sensor wipe <gpio> [confirm]  Resetar historico"
                                  : "  sensor wipe <gpio> [confirm]  Wipe sensor history");
  showIf(CMD_REMOVE_SENSOR,    pt ? "  sensor remove <gpio> [confirm]  Remover slot"
                                  : "  sensor remove <gpio> [confirm]  Remove sensor slot");
  showIf(CMD_SENSOR_FIELD,     pt ? "  sensor <slot> <campo> <valor>  Configurar sensor"
                                  : "  sensor <slot> <field> <value>  Configure sensor");
 }

 /* ── Global Config commands ── */
 if (_cliMode == CLI_MODE_GLOBAL_CONFIG) {
  auto showIf = [&](DemandType t, const char* s) { if (getCommandModeMask(t) & curMask) consolePrintln(s); };
  consolePrintln(pt ? "  --- Sistema ---" : "  --- System ---");
  showIf(CMD_SET_SYS_NAME,      "  system name <nome>");
  showIf(CMD_SET_THEME,         "  system theme <id>");
  showIf(CMD_SET_TIMEZONE,      "  system timezone <-12..14>");
  showIf(CMD_SET_NTP,           "  system ntp <servidor>");
  showIf(CMD_SET_HISTORY_INTERVAL, "  system history_interval <min>");
  showIf(CMD_LANGUAGE,          "  language <pt|en>");
  showIf(CMD_SET_TIME,          "  time <AAAA-MM-DD> <HH:MM:SS>");
  showIf(CMD_SET_DS_RES,        "  ds18b20 resolution <9-12>");
  consolePrintln(pt ? "  --- Rede ---" : "  --- Network ---");
  showIf(CMD_SET_WIFI_SSID,     "  wifi ssid <nome>");
  showIf(CMD_SET_WIFI_PASS,     "  wifi pass <senha>");
  showIf(CMD_IP_CFG,            "  ip <dhcp|static|addr|mask|gateway|dns>");
  showIf(CMD_SET_DNS_CFG,       "  dns <auto|manual> [ip1] [ip2]");
  showIf(CMD_SET_NTP_ENABLED,   "  ntp <on|off>");
  showIf(CMD_SET_WEB_PORT,      "  web port <1..65535>");
  consolePrintln(pt ? "  --- Telemetria ---" : "  --- Telemetry ---");
  showIf(CMD_SET_TEL_SERVER,    "  tel server <url>");
  showIf(CMD_SET_TEL_PORT,      "  tel port <n>");
  showIf(CMD_SET_TEL_PATH,      "  tel path <path>");
  showIf(CMD_SET_TEL_BATCH,     "  tel batch <n>");
  showIf(CMD_SET_TEL_INTERVAL,  "  tel interval <ms>");
  showIf(CMD_SET_TEL_CRYPTO,    "  tel crypto <on|off>");
  showIf(CMD_SET_TEL_MODE,      "  tel mode <json|csv|custom>");
  consolePrintln(pt ? "  --- Usuarios ---" : "  --- Users ---");
  showIf(CMD_USER_ADD,          "  user add <nome> <senha>");
  showIf(CMD_USER_DEL,          "  user del <nome>");
  showIf(CMD_USER_PASS,         "  user pass <nome> <senha>");
  consolePrintln("");
  consolePrintln(pt ? "  sensor <slot>         Entrar configuracao do sensor"
                    : "  sensor <slot>         Enter sensor configuration");
 }

 /* ── Sensor Config commands ── */
 if (_cliMode == CLI_MODE_SENSOR_CONFIG) {
  auto showIf = [&](DemandType t, const char* s) { if (getCommandModeMask(t) & curMask) consolePrintln(s); };
  showIf(CMD_SENSOR_FIELD, pt ? "  type <ds18b20|dht22|bme280>  Definir tipo"
                               : "  type <ds18b20|dht22|bme280>  Set type");
  showIf(CMD_SENSOR_FIELD, pt ? "  create <tipo>        Criar/resetar slot"
                               : "  create <type>        Create/reset slot");
  showIf(CMD_SENSOR_FIELD, pt ? "  pin <idx> <gpio>     Atribuir GPIO"
                               : "  pin <idx> <gpio>     Assign GPIO");
  showIf(CMD_SENSOR_FIELD, pt ? "  name <nome>          Nome amigavel"
                               : "  name <name>         Friendly name");
  showIf(CMD_SENSOR_FIELD, pt ? "  hwid <id>            Hardware ID"
                               : "  hwid <id>            Hardware ID");
  showIf(CMD_SENSOR_FIELD, pt ? "  active <on|off>      Ativar/desativar"
                               : "  active <on|off>      Enable/disable");
  showIf(CMD_SENSOR_FIELD, pt ? "  alarm <on|off>       Alarmes"
                               : "  alarm <on|off>       Alarms");
  showIf(CMD_SENSOR_FIELD, pt ? "  tmin|tmax <valor>    Limites temperatura"
                               : "  tmin|tmax <value>    Temperature limits");
  showIf(CMD_SENSOR_FIELD, pt ? "  hmin|hmax <valor>    Limites umidade"
                               : "  hmin|hmax <value>    Humidity limits");
 }

 /* ── Telemetry ops (exec modes) ── */
 if (_cliMode == CLI_MODE_USER_EXEC || _cliMode == CLI_MODE_PRIV_EXEC) {
  consolePrintln("");
  auto showIf = [&](DemandType t, const char* s) { if (getCommandModeMask(t) & curMask) consolePrintln(s); };
  showIf(CMD_TEL_SYNC, pt ? "  tel sync              Enviar telemetria"
                           : "  tel sync              Force telemetry upload");
  showIf(CMD_TEL_DUMP, pt ? "  tel dump              Dump do payload"
                           : "  tel dump              Dump next payload");
  if (_cliMode == CLI_MODE_PRIV_EXEC) {
   showIf(CMD_TEL_RESET, pt ? "  tel reset             Resetar cursor telemetria"
                             : "  tel reset             Reset telemetry cursor");
  }
 }

 printDivider( );
}

String CommandManager::formatRom(const uint8_t* rom) {
 char buff[18];
 snprintf(buff, sizeof(buff), "%02X%02X%02X%02X%02X%02X%02X%02X", rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
 return String(buff);
}

void CommandManager::renderSensorTable(const SensorRecord* sensors, int maxSensors) {
 consolePrintln("");
 consolePrintln(isPt( ) ? "--- Sensores Configurados ---"
 : "--- Configured Sensors ---");
 int activeCount = 0;
 for(int i=0; i<maxSensors; i++) {
 if (!sensors[i].active) continue;
 activeCount++;

 SensorType st = (SensorType)sensors[i].sensorType;
 auto fmt = SensorFormat::forType(st);

 /* Build GPIO list: "3", "5,6", "10,SDA+11,SCL", or "--" if unassigned */
 String gpioStr = "";
 for (int p = 0; p < fmt.pinCount && p < MAX_SENSOR_PINS; p++) {
  uint8_t gpio = sensors[i].pins[p];
  if (gpio == PIN_UNUSED) {
   gpioStr += "--";
  } else {
   gpioStr += String(gpio);
  }
  if (fmt.pinCount > 1 && p + 1 < fmt.pinCount) {
   gpioStr += ",";
   gpioStr += fmt.pins[p].label;
   gpioStr += "/";
  }
 }

 /* Build channel list: "Temp", "Temp+Hum", "Temp+Hum+Press" */
 String chStr = "";
 for (int c = 0; c < fmt.valueCount && c < 3; c++) {
  if (c > 0) chStr += "+";
  if (c == 0) chStr += "T";
  else if (c == 1) chStr += "H";
  else if (c == 2) chStr += "P";
 }

 /* Slot header: [Slot 00] GPIO=5 | DHT22 | T+H | Freezer 1 */
 consolePrintf(" [Slot %02d] GPIO=%s | %-8s | %-5s | %s\n",
   i, gpioStr.c_str( ), sensorTypeName(st),
   chStr.c_str( ), sensors[i].friendlyName);

 /* ROM for 1-Wire sensors */
 if (st == TYPE_DS18B20) {
  String romStr = formatRom(sensors[i].rom);
  consolePrintf("          ROM: %s\n", romStr.c_str( ));
 }

 /* HW ID if different from default */
 if (sensors[i].hwId[0] != '\0' && strcmp(sensors[i].hwId, "LIB_SENS") != 0) {
  consolePrintf("          HWID: %s\n", sensors[i].hwId);
 }

 /* Alarm status + limits */
 consolePrintf("          %s: %s",
   isPt( ) ? "ALARMES" : "ALARMS",
   sensors[i].alarmsActive ? (isPt( ) ? "LIGADO" : "ON") : (isPt( ) ? "DESL" : "OFF"));

 if (sensors[i].alarmsActive) {
  if (sensorHasChannel(st, CH_TEMP)) {
   consolePrintf("  [T: %.1f .. %.1f]", sensors[i].tempMin, sensors[i].tempMax);
  }
  if (sensorHasChannel(st, CH_HUM)) {
   consolePrintf("  [H: %.1f .. %.1f]", sensors[i].humMin, sensors[i].humMax);
  }
 }
 consolePrintln("");
 }
 if (activeCount == 0) {
  consolePrintln(isPt( ) ? " (banco vazio)" : " (database is empty)");
 } else {
  consolePrintf(" (%d / %d %s)\n", activeCount, maxSensors,
    isPt( ) ? "slots ativos" : "slots active");
 }
 printDivider( );
}

void CommandManager::renderGpioMap(const SensorRecord* sensors, int maxSensors) {
 consolePrintln("");
 consolePrintln(isPt( ) ? "--- Mapa de GPIOs (16 totais) ---"
                       : "--- GPIO Map (16 total) ---");

 /* Build a map: gpioOwners[gpio] = slot index (or -1 if free) */
 int gpioOwner[16];
 for (int g = 0; g < 16; g++) gpioOwner[g] = -1;

 int usedCount = 0;
 for (int si = 0; si < maxSensors; si++) {
  if (!sensors[si].active) continue;
  auto fmt = SensorFormat::forType((SensorType)sensors[si].sensorType);
  for (int pi = 0; pi < fmt.pinCount && pi < MAX_SENSOR_PINS; pi++) {
   uint8_t gpio = sensors[si].pins[pi];
   if (gpio != PIN_UNUSED && gpio < 16) {
    gpioOwner[gpio] = si;
    usedCount++;
   }
  }
 }

 /* Print GPIO rows */
 for (int g = 0; g < 16; g++) {
  if (gpioOwner[g] >= 0) {
   int si = gpioOwner[g];
   SensorType st = (SensorType)sensors[si].sensorType;
   auto fmt = SensorFormat::forType(st);
   /* Find which pin role this GPIO serves */
   const char* roleLabel = "";
   for (int pi = 0; pi < fmt.pinCount && pi < MAX_SENSOR_PINS; pi++) {
    if (sensors[si].pins[pi] == (uint8_t)g) {
     roleLabel = fmt.pins[pi].label;
     break;
    }
   }
   consolePrintf(" GPIO %2d: [Slot %02d] %-8s (%s)\n",
     g, si, sensorTypeName(st), roleLabel);
  } else {
   consolePrintf(" GPIO %2d: FREE\n", g);
  }
 }

 /* Free GPIOs summary line */
 String freeList = "";
 for (int g = 0; g < 16; g++) {
  if (gpioOwner[g] < 0) {
   if (freeList.length( ) > 0) freeList += ",";
   freeList += String(g);
  }
 }
 if (freeList.length( ) > 0) {
  consolePrintf("%s: %s\n",
    isPt( ) ? "LIVRES" : "FREE",
    freeList.c_str( ));
 }

 consolePrintf(" (%d / 16 %s)\n", usedCount,
   isPt( ) ? "GPIOs em uso" : "GPIOs used");
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
 /* Pico serial (16 hex) — used as key in calib.csv
 * for ambient sensor calibration (DHT22). */
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

 /* Force fresh heap + largest block sampling for current snapshot. */
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
 consolePrintf (pt ? " Flash ops: %lu (media %lu ms)\n"
                   : " Flash ops: %lu (avg %lu ms)\n",
 (unsigned long)m.flashOps,
 (unsigned long)(m.flashOps ? m.flashOpTotalMs / m.flashOps : 0));
 consolePrintf (pt ? " Pior op: %lu ms | >50ms: %lu\n"
                   : " Worst op: %lu ms | >50ms: %lu\n",
 (unsigned long)m.flashOpMaxMs,
 (unsigned long)m.flashOpsOver50ms);
 /* T0.1 (completed): the real IRQ-off window, not the FLASH_OP proxy above.
  * Plan acceptance criterion for the 72 h soak is stated on this number. */
 {
  const uint32_t irqOps = g_flashIrqEraseCount + g_flashIrqProgCount;
  consolePrintf (pt ? " IRQ-off max: %lu us | media: %lu us\n"
                    : " IRQ-off max: %lu us | avg: %lu us\n",
  (unsigned long)g_flashIrqMaxUs,
  (unsigned long)(irqOps ? (uint32_t)(g_flashIrqTotalUs / irqOps) : 0u));
  consolePrintf (pt ? " IRQ-off erase: %lu | prog: %lu | >1ms: %lu\n"
                    : " IRQ-off erase: %lu | prog: %lu | >1ms: %lu\n",
  (unsigned long)g_flashIrqEraseCount,
  (unsigned long)g_flashIrqProgCount,
  (unsigned long)g_flashIrqOver1msCount);
 }
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
 /* PT comes from @HELP in .lng (UTF-8 -> unaccent for the ASCII terminal).
 * EN is now always inline in PROGMEM (HELP_TEXT_EN), no LittleFS dependency. */
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
 /* PT requested but .lng has no @HELP — falls through to EN PROGMEM below. */
 }

 /* Iterate PROGMEM lines: pgm_read_byte for byte-by-byte access */
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

/* Stub — printHelpExtras content now lives in /help_{pt,en}.txt
 * (appended to the end of the main help). Kept only for compatibility
 * with any external caller that still references the symbol. */
void CommandManager::printHelpExtras( ) { /* no-op: text moved to FS */ }
