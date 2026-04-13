/**
 * @file    CommandManager.cpp
 * @brief   Implementation of CommandManager — command parsing, rendering, and I/O routing.
 * @details Implements the CLI parser that converts text input into CliDemand
 * structs, along with formatted output for sensor tables, scan results,
 * system info, and log entries. Supports dual-buffer USB/BT processing.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.4.7
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "CommandManager.h"
#include "LogManager.h"
#include <time.h>
#include <stdarg.h>

CommandManager::CommandManager() {
    _usbBuffer.reserve(128);
    _btBuffer.reserve(128);
}

void CommandManager::begin() {
    _btMgr.begin("SIMUT_CLI");
    printWelcome();
    printPrompt();
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

String CommandManager::getLastRawInput() { return _lastRawInput; }

bool CommandManager::processInput(CliDemand &demandOut) {
    _btMgr.update();


    while (Serial.available()) {
        char c = (char)Serial.read();
        consolePrint(String(c));
        if (c == '\n' || c == '\r') {
            consolePrintln("");
            if (_usbBuffer.length() > 0) {
                _lastRawInput = _usbBuffer;
                demandOut = parseCommand(_usbBuffer);
                _usbBuffer = "";
                return true;
            }
            printPrompt();
        } else if (c == 8 || c == 127) {
            if (_usbBuffer.length() > 0) _usbBuffer.remove(_usbBuffer.length() - 1);
        } else {
            _usbBuffer += c;
        }
    }


    if (_btMgr.isAuthenticated()) {
        while (_btMgr.available()) {
            char c = (char)_btMgr.read();
            consolePrint(String(c));
            if (c == '\n' || c == '\r') {
                consolePrintln("");
                if (_btBuffer.length() > 0) {
                    _lastRawInput = _btBuffer;
                    demandOut = parseCommand(_btBuffer);
                    _btBuffer = "";
                    return true;
                }
                printPrompt();
            } else if (c == 8 || c == 127) {
                if (_btBuffer.length() > 0) _btBuffer.remove(_btBuffer.length() - 1);
            } else {
                _btBuffer += c;
            }
        }
    }

    return false;
}

void CommandManager::hexStringToBytes(String hex, uint8_t* out) {
    if (hex.startsWith("0x")) hex = hex.substring(2);
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)strtoul(hex.substring(i*2, i*2+2).c_str(), NULL, 16);
    }
}

CliDemand CommandManager::parseCommand(String input) {
    CliDemand cmd;
    cmd.type = CMD_UNKNOWN;
    input.trim();

    int spaceIndex;
    String parts[5];
    int count = 0;
    String tempInput = input;

    while (count < 5 && tempInput.length() > 0) {
        if (count == 4 && tempInput.startsWith("\"")) {
            parts[count++] = tempInput;
            break;
        }
        spaceIndex = tempInput.indexOf(' ');
        if (spaceIndex == -1) { parts[count++] = tempInput; tempInput = ""; }
        else { parts[count++] = tempInput.substring(0, spaceIndex); tempInput = tempInput.substring(spaceIndex + 1); tempInput.trim(); }
    }

    if (count == 0) return cmd;

    String t0 = parts[0]; t0.toLowerCase();
    String t1 = count > 1 ? parts[1] : ""; t1.toLowerCase();
    String t2 = count > 2 ? parts[2] : ""; t2.toLowerCase();
    String t3 = count > 3 ? parts[3] : ""; t3.toLowerCase();
    String t4 = count > 4 ? parts[4] : "";

    /* Valores originais (preservam maiúsculas) para SSID, senha, nome, NTP, etc. */
    String v3 = count > 3 ? parts[3] : "";

    if (t0 == "help") { cmd.type = CMD_HELP; return cmd; }
    if (t0 == "reload") { cmd.type = CMD_RELOAD; return cmd; }

    if (t0 == "show") {
        if (t1 == "themes") { cmd.type = CMD_SHOW_THEMES; return cmd; }
        if (t1 == "system" && t2 == "log") { cmd.type = CMD_SHOW_LOGS; return cmd; }
        if (t1 == "sensors") { cmd.type = CMD_SHOW_SENSORS; return cmd; }
        if (t1 == "storage" && t2 == "stats") { cmd.type = CMD_SHOW_STORAGE; return cmd; }
        if (t1 == "system" && t2 == "info") { cmd.type = CMD_SHOW_SYSINFO; return cmd; }
        if (t1 == "net" && t2 == "status") { cmd.type = CMD_SHOW_NET; return cmd; }
    }

    if (t0 == "conf" || t0 == "configure") {
        if (t1 == "system") {
            if (t2 == "theme") { cmd.type = CMD_SET_THEME; cmd.strVal1 = v3; return cmd; }
            if (t2 == "name") { cmd.type = CMD_SET_SYS_NAME; cmd.strVal1 = v3; return cmd; }
            if (t2 == "ssid") { cmd.type = CMD_SET_WIFI_SSID; cmd.strVal1 = v3; return cmd; }
            if (t2 == "pass") { cmd.type = CMD_SET_WIFI_PASS; cmd.strVal1 = v3; return cmd; }
            if (t2 == "timezone") { cmd.type = CMD_SET_TIMEZONE; cmd.intVal1 = t3.toInt(); return cmd; }
            if (t2 == "ntp") { cmd.type = CMD_SET_NTP; cmd.strVal1 = v3; return cmd; }
            if (t2 == "admin" && t3 == "reset") { cmd.type = CMD_RESET_ADMIN; return cmd; }
            if (t2 == "touch" && t3 == "reset") { cmd.type = CMD_RESET_TOUCH_CAL; return cmd; }
        }
        if (t1 == "sensor" && t2 == "ds18b20" && t3 == "resolution") {
            cmd.type = CMD_SET_DS_RES; cmd.intVal1 = t4.toInt(); return cmd;
        }
        if (t1 == "tel") {
            if (t2 == "server") { cmd.type = CMD_SET_TEL_SERVER; cmd.strVal1 = v3; return cmd; }
            if (t2 == "port") { cmd.type = CMD_SET_TEL_PORT; cmd.intVal1 = t3.toInt(); return cmd; }
            if (t2 == "path") { cmd.type = CMD_SET_TEL_PATH; cmd.strVal1 = v3; return cmd; }
            if (t2 == "batch") { cmd.type = CMD_SET_TEL_BATCH; cmd.intVal1 = t3.toInt(); return cmd; }
            if (t2 == "interval") { cmd.type = CMD_SET_TEL_INTERVAL; cmd.intVal1 = t3.toInt(); return cmd; }
            if (t2 == "crypto") { cmd.type = CMD_SET_TEL_CRYPTO; cmd.boolVal = (t3 == "on"); return cmd; }
            if (t2 == "mode") {
                cmd.type = CMD_SET_TEL_MODE;
                if(t3 == "json") cmd.intVal1 = TEL_MODE_JSON;
                else if(t3 == "csv") cmd.intVal1 = TEL_MODE_CSV;
                else cmd.intVal1 = TEL_MODE_CUSTOM;
                return cmd;
            }
        }
    }

    if (t0 == "sensor") {
        if (t1 == "scan") { cmd.type = CMD_SCAN_SENSORS; return cmd; }
        if (t1 == "define") {
            int idx = input.indexOf("define");
            String args = input.substring(idx + 7); args.trim();

            int sp1 = args.indexOf(' ');
            if (sp1 != -1) {
                cmd.intVal1 = args.substring(0, sp1).toInt();
                args = args.substring(sp1 + 1); args.trim();

                int sp2 = args.indexOf(' ');
                if (sp2 != -1) {
                    String romHex = args.substring(0, sp2);
                    hexStringToBytes(romHex, cmd.rom);

                    args = args.substring(sp2 + 1); args.trim();
                    int sp3 = args.indexOf(' ');
                    if (sp3 != -1) {
                        cmd.strVal1 = args.substring(0, sp3);
                        cmd.strVal2 = args.substring(sp3 + 1);
                        cmd.strVal2.replace("\"", "");
                        cmd.type = CMD_DEFINE_SENSOR;
                        return cmd;
                    }
                }
            }
        }
    }

    if (t0 == "sensor" && t1 == "wipe") {
        cmd.type = CMD_WIPE_SENSOR;
        cmd.intVal1 = t2.toInt();
        return cmd;
    }

    if (t0 == "sensor" && t1 == "accept") {
        cmd.type = CMD_ACCEPT_SENSOR;
        cmd.intVal1 = t2.toInt();
        return cmd;
    }

    if (t0 == "write" && t1 == "memory") { cmd.type = CMD_WRITE_MEMORY; return cmd; }
    if (t0 == "clear" && t1 == "log") { cmd.type = CMD_CLEAR_LOGS; return cmd; }
    if (t0 == "tel" && t1 == "sync") { cmd.type = CMD_TEL_SYNC; return cmd; }

    return cmd;
}

void CommandManager::printLogEntry(String line) {
    if (line.length() < 5) return;
    String parts[8]; int idx = 0; int start = 0; int end = line.indexOf(';');
    while (end != -1 && idx < 8) {
        parts[idx++] = line.substring(start, end); start = end + 1; end = line.indexOf(';', start);
    }
    if (idx < 8) parts[idx++] = line.substring(start);
    if (idx < 7) return;

    time_t ts = (time_t)parts[0].toInt();
    unsigned long ms = (unsigned long)parts[1].toInt();
    int core = parts[2].toInt();
    LogLevel lvl = (LogLevel)parts[3].toInt();
    String tag = parts[4];
    int code = parts[5].toInt();
    int ctx = parts[6].toInt();
    String extra = (idx > 7) ? parts[7] : "";

    uint32_t sec = ms / 1000;
    uint32_t d = sec / 86400; sec %= 86400;
    uint32_t h = sec / 3600;  sec %= 3600;
    uint32_t m = sec / 60;    sec %= 60;

    char dateBuf[32], upBuf[20];
    if (ts > 1000000000) {
        struct tm *ti = localtime(&ts);
        snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d %02d:%02d:%02d", ti->tm_year+1900, ti->tm_mon+1, ti->tm_mday, ti->tm_hour, ti->tm_min, ti->tm_sec);
    } else { snprintf(dateBuf, sizeof(dateBuf), "BOOT+%lus", ms/1000); }

    if (d > 0) snprintf(upBuf, sizeof(upBuf), "%lud %02lu:%02lu:%02lu", d, h, m, sec);
    else       snprintf(upBuf, sizeof(upBuf), "%02lu:%02lu:%02lu", h, m, sec);

    const char* lvlStr = LogManager::instance().getLevelString(lvl);
    consolePrintf("[%s][UP %s][C%d][%s][%s] Code:%d", dateBuf, upBuf, core, lvlStr, tag.c_str(), code);
    if (extra.length() > 0) consolePrintf(" %s", extra.c_str());
    if (ctx != 0) consolePrintf(" (ctx:%d)", ctx);
    consolePrintln("");
}

void CommandManager::printWelcome() {
    consolePrintln("\n");
    consolePrintln("************************************************");
    consolePrintln("* SIMUT IoT MODULAR CLI v2.6 (Log Viewer)      *");
    consolePrintln("* Type 'help' for command list                 *");
    consolePrintln("************************************************");
}

void CommandManager::printPrompt() { consolePrint("SIMUT> "); }
void CommandManager::printDivider() { consolePrintln("-----------------------------------------"); }

String CommandManager::formatRom(const uint8_t* rom) {
    char buff[18];
    snprintf(buff, sizeof(buff), "%02X%02X%02X%02X%02X%02X%02X%02X", rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
    return String(buff);
}

void CommandManager::renderSensorTable(const SensorRecord* sensors, int maxSensors) {
    consolePrintln("\n--- Configured Sensors Database ---");
    consolePrintln("GPIO | HW_ID     | ROM              | NAME");
    consolePrintln("-----|-----------|------------------|--------------------");
    bool found = false;
    for(int i=0; i<maxSensors; i++) {
        if (sensors[i].active) {
            found = true;
            char line[128];
            String romStr = formatRom(sensors[i].rom);
            snprintf(line, sizeof(line), " %02d  | %-9s | %s | %s", sensors[i].gpio, sensors[i].hwId, romStr.c_str(), sensors[i].friendlyName);
            consolePrintln(line);
        }
    }
    if (!found) consolePrintln("(Database is empty)");
    printDivider();
}

void CommandManager::renderScanResults(const std::vector<ScanResult> &results) {
    consolePrintln("\n--- Hardware Scan Results ---");
    consolePrintln("PIN | TYPE       | DETAILS");
    consolePrintln("----|------------|-------------------------");
    if (results.empty()) { consolePrintln("(No physical sensors detected)"); }
    else {
        for (const auto &res : results) {
            char line[128]; char typeStr[12]; String details;
            if (res.type == TYPE_DS18B20) { strcpy(typeStr, "DS18B20"); details = formatRom(res.rom); }
            else if (res.type == TYPE_DHT22) { strcpy(typeStr, "DHT22"); details = "(Temp/Hum Capable)"; }
            else { strcpy(typeStr, "UNKNOWN"); details = "Signal detected"; }
            snprintf(line, sizeof(line), " %02d | %-10s | %s", res.pin, typeStr, details.c_str());
            consolePrintln(line);
        }
    }
    printInfo("Tip: Use 'sensor define' to map these to logic IDs.");
    printDivider();
}

void CommandManager::renderSystemInfo(const SystemConfig &cfg) {
    printDivider();
    consolePrintln(" [SYSTEM INFO]");
    consolePrintf(" Device Name:    %s\n", cfg.deviceName);
    consolePrintf(" Firmware Ver:   %d\n", cfg.version);
    consolePrintln(" [SENSORS]");
    consolePrintf(" DS18 Precision: %d-bit\n", cfg.ds18Resolution);
    consolePrintln(" [CONNECTIVITY]");
    consolePrintf(" WiFi SSID:      %s\n", (strlen(cfg.wifiSsid) > 0 ? cfg.wifiSsid : "<Not Configured>"));
    consolePrintf(" Timezone:       GMT%s%d\n", (cfg.timezoneOffset >= 0 ? "+" : ""), cfg.timezoneOffset);
    consolePrintf(" NTP Server:     %s\n", (strlen(cfg.ntpServer) > 0 ? cfg.ntpServer : "pool.ntp.org (default)"));
    consolePrintf(" Logging:        %s\n", cfg.loggingEnabled ? "ENABLED" : "DISABLED");
    printDivider();
}

void CommandManager::renderSensorReading(const SensorReading &reading) {
    if (!reading.isValid) { consolePrintf("[%s] Error: Read Failed or Checksum Error\n", reading.typeName); return; }
    if (strcmp(reading.typeName, "DHT22") == 0) { consolePrintf("[DHT] Temp: %.2f C | Hum: %.2f %%\n", reading.value1, reading.value2); }
    else if (strcmp(reading.typeName, "DS18B20") == 0) { consolePrintf("[DS18] Temp: %.4f C\n", reading.value1); }
    else { consolePrintf("[%s] Value: %.2f\n", reading.typeName, reading.value1); }
}

void CommandManager::printSuccess(String msg) { consolePrint("OK: "); consolePrintln(msg); }
void CommandManager::printError(String msg) { consolePrint("ERROR: "); consolePrintln(msg); }
void CommandManager::printInfo(String msg) { consolePrintln(msg); }

void CommandManager::printHelp() {
    consolePrintln("\n====================== [ SIMUT HELP MENU ] ======================");
    consolePrintln("\n--- 1. SYSTEM MONITORING ---");
    consolePrintln(" show system info           : View device name, version and config");
    consolePrintln(" show system log            : Dump system event log from Flash");
    consolePrintln(" show storage stats         : Flash usage statistics");
    consolePrintln(" show net status            : View IP, RSSI, and Sync Time");
    consolePrintln(" show themes                : List available UI color themes");
    consolePrintln("\n--- 2. SENSOR DIAGNOSTICS ---");
    consolePrintln(" show sensors               : List mapped sensors (Database)");
    consolePrintln(" sensor scan                : Hardware scan for connected sensors");
    consolePrintln("\n--- 3. CONFIGURATION (Requires 'write memory' + 'reload') ---");
    consolePrintln(" conf system name <value>   : Set device friendly name");
    consolePrintln(" conf system ssid <name>    : Set WiFi SSID (Case sensitive)");
    consolePrintln(" conf system pass <pass>    : Set WiFi Password");
    consolePrintln(" conf system timezone <val> : Set Offset (e.g., -3 for Brazil)");
    consolePrintln(" conf system ntp <server>   : Set NTP server (empty = pool.ntp.org)");
    consolePrintln(" conf system theme <id>     : Set UI Theme (use ID name or index)");
    consolePrintln(" conf system admin reset    : Resets the Admin account password to default");
    consolePrintln(" conf system touch reset    : Reset touch calibration to factory defaults");
    consolePrintln(" conf sensor ds18b20 resolution <9-12> : Set global DS18 resolution");
    consolePrintln(" -- Telemetry Configuration --");
    consolePrintln(" conf tel server <url>      : Set server address (e.g., api.mysite.com)");
    consolePrintln(" conf tel port <port>       : Set server port (e.g., 80, 443)");
    consolePrintln(" conf tel path <path>       : Set endpoint path (e.g., /api/v1/data)");
    consolePrintln(" conf tel batch <size>      : Records per upload (Max 50)");
    consolePrintln(" conf tel interval <ms>     : Auto-upload interval (0 = Disabled)");
    consolePrintln(" conf tel crypto <on/off>   : Enable SSL/HTTPS connection");
    consolePrintln(" conf tel mode <mode>       : Payload format (json, csv, custom)");
    consolePrintln("\n--- 4. SENSOR MAPPING ---");
    consolePrintln(" sensor define <gpio> <rom> <hwid> \"<name>\"");
    consolePrintln("    Ex: sensor define 0 28AA.. S1 \"Oven_Top\"");
    consolePrintln("    Note: GPIO 10 is reserved for Ambient Sensor");
    consolePrintln("\n--- 5. MAINTENANCE & ACTIONS ---");
    consolePrintln(" sensor accept <gpio>       : Authorize new physical sensor on a slot");
    consolePrintln(" sensor wipe <gpio>         : Reset graph history context for a slot");
    consolePrintln(" tel sync                   : Force manual telemetry upload");
    consolePrintln(" clear log                  : Delete system log file");
    consolePrintln(" write memory               : Persist RAM config to Flash");
    consolePrintln(" reload                     : Reboot system");
    consolePrintln("=================================================================");
}
