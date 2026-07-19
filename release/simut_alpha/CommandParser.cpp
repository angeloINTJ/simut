/**
 * @file CommandParser.cpp
 * @brief CLI command tokenizer + router (see CommandParser.h).
 */

#include "CommandParser.h"
#include "SystemDefs_Validate.h"
#include "SystemDefs_Records.h"
#include <stdlib.h>

static void hexStringToBytes(String hex, uint8_t* out) {
 if (hex.startsWith("0x")) hex = hex.substring(2);
 for (int i = 0; i < 8; i++) {
 out[i] = (uint8_t)strtoul(hex.substring(i*2, i*2+2).c_str( ), NULL, 16);
 }
}

CliDemand parseCliCommand(String input) {
 CliDemand cmd;
 cmd.type = CMD_UNKNOWN;
 input.trim( );

 /* confirm suffix — stripped before tokenization */
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
 String t5 = count > 5 ? parts[5] : "";

 String v3 = count > 3 ? parts[3] : "";

 if (t0 == "gpio") { cmd.type = CMD_SHOW_GPIO; return cmd; }
 if (t0 == "help" || t0 == "ajuda" || t0 == "?") { cmd.type = CMD_HELP; return cmd; }
 if (t0 == "reload") { cmd.type = CMD_RELOAD; return cmd; }

 if (t0 == "touch" && t1 == "sim") {
 cmd.type = CMD_TOUCH_SIM;
 cmd.setStrVal1(t2.c_str( ));
 cmd.setStrVal2(t3.c_str( ));
 return cmd;
 }

 if (t0 == "screen") {
 cmd.type = CMD_GOTO_SCREEN;
 cmd.setStrVal1(t1.c_str( ));
 return cmd;
 }

 if (t0 == "language") {
 cmd.type = CMD_LANGUAGE;
 if (t1 == "pt" || t1 == "pt-br" || t1 == "ptbr") cmd.intVal1 = LANG_PT;
 else if (t1 == "en") cmd.intVal1 = LANG_EN;
 else cmd.intVal1 = -1;
 return cmd;
 }

 if (t0 == "show") {
 if (t1 == "themes") { cmd.type = CMD_SHOW_THEMES; return cmd; }
 if (t1 == "system" && t2 == "log") { cmd.type = CMD_SHOW_LOGS; return cmd; }
 if (t1 == "sensors") { cmd.type = CMD_SHOW_SENSORS; return cmd; }
 if (t1 == "sensor" && t2 == "types") { cmd.type = CMD_SHOW_SENSOR_TYPES; return cmd; }
 if (t1 == "gpio") { cmd.type = CMD_SHOW_GPIO; return cmd; }
 if (t1 == "storage" && t2 == "stats") { cmd.type = CMD_SHOW_STORAGE; return cmd; }
 if (t1 == "system" && t2 == "info") { cmd.type = CMD_SHOW_SYSINFO; return cmd; }
 if (t1 == "net" && t2 == "status") { cmd.type = CMD_SHOW_NET; return cmd; }
 if (t1 == "metrics") { cmd.type = CMD_SHOW_METRICS; return cmd; }
 }

 if (t0 == "conf" || t0 == "configure") {
 if (t1 == "ip") {
 if (t2 == "dhcp") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 0; return cmd; }
 if (t2 == "static") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 1; return cmd; }
 if (t2 == "addr") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 2; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "mask") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 3; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "gateway") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 4; cmd.setStrVal1(v3.c_str( )); return cmd; }
 if (t2 == "dns") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 5; cmd.setStrVal1(v3.c_str( )); return cmd; }
 }

 if (t1 == "ntp") {
 if (t2 == "on") { cmd.type = CMD_SET_NTP_ENABLED; cmd.intVal1 = 1; return cmd; }
 if (t2 == "off") { cmd.type = CMD_SET_NTP_ENABLED; cmd.intVal1 = 0; return cmd; }
 }

 if (t1 == "time") {
 cmd.type = CMD_SET_TIME;
 cmd.setStrVal1(t2.c_str( ));
 cmd.setStrVal2(t3.c_str( ));
 return cmd;
 }

 if (t1 == "net" && t2 == "dns") {
 if (t3 == "auto") { cmd.type = CMD_SET_DNS_CFG; cmd.intVal1 = 0; return cmd; }
 if (t3 == "manual") {
 cmd.type = CMD_SET_DNS_CFG; cmd.intVal1 = 1;
 cmd.setStrVal1(t4.c_str( ));
 cmd.setStrVal2(t5.c_str( ));
 return cmd;
 }
 }

 if (t1 == "sensor") {
 bool isField = (t2 == "tmin" || t2 == "tmax" || t2 == "hmin" ||
 t2 == "hmax" || t2 == "alarm");
 if (isField) {
 cmd.type = CMD_SENSOR_FIELD;
 cmd.setStrVal1(t2.c_str( ));
 cmd.intVal1Valid = parseIntStrict(t3, cmd.intVal1);
 cmd.setStrVal2(t4.c_str( ));
 return cmd;
 }
 }

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
 cmd.setStrVal1(t3.c_str( ));
 cmd.boolVal = (t3 == "on");
 return cmd;
 }
 if (t2 == "mode") {
 cmd.type = CMD_SET_TEL_MODE;
 cmd.setStrVal1(t3.c_str( ));
 if(t3 == "json") cmd.intVal1 = TEL_MODE_JSON;
 else if(t3 == "csv") cmd.intVal1 = TEL_MODE_CSV;
 else if(t3 == "custom") cmd.intVal1 = TEL_MODE_CUSTOM;
 else cmd.intVal1 = -1;
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
 cmd.setStrVal1(args.substring(0, sp3).c_str( ));
 String fname = args.substring(sp3 + 1);
 fname.replace("\"", "");
 cmd.setStrVal2(fname.c_str( ));
 cmd.type = CMD_DEFINE_SENSOR;
 return cmd;
 }
 }
 }
 }

 if (count >= 3) {
 cmd.type = CMD_SENSOR_FIELD;
 cmd.intVal1Valid = parseIntStrict(t1, cmd.intVal1);
 cmd.setStrVal1(t2.c_str());
 if (count >= 4) cmd.setStrVal2(t3.c_str());
 return cmd;
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

 if (t0 == "write" && t1 == "memory") { cmd.type = CMD_WRITE_MEMORY; return cmd; }
 if (t0 == "clear" && t1 == "log") { cmd.type = CMD_CLEAR_LOGS; return cmd; }
 if (t0 == "tel" && t1 == "sync") { cmd.type = CMD_TEL_SYNC; return cmd; }
 if (t0 == "tel" && t1 == "dump") { cmd.type = CMD_TEL_DUMP; return cmd; }
 if (t0 == "tel" && t1 == "reset") { cmd.type = CMD_TEL_RESET; return cmd; }

 if (t0 == "debug") {
 cmd.type = CMD_DEBUG;
 if (t1 == "on") cmd.intVal1 = 1;
 else if (t1 == "off") cmd.intVal1 = 0;
 else cmd.intVal1 = -1;
 return cmd;
 }

 return cmd;
}
