/**
 * @file CommandParser.cpp
 * @brief CLI command tokenizer + router (see CommandParser.h).
 *
 * v1.5.3 parser repair — restores 21 unreachable commands:
 *   1. 'conf'/'configure' prefix is now NORMALIZED (stripped + reparse),
 *      so 'conf user add x y' ≡ 'user add x y'. The old wrapper block
 *      tested t0=="user"/"system"/"tel"/... while t0 was "conf" — dead
 *      code that made every setter family unreachable in any form.
 *   2. All setter families (user/system/wifi/ds18b20/web/tel/ip/ntp/
 *      time/dns) now live at top level, matching the bare syntax that
 *      printModeHelp() advertises in global-config mode.
 *   3. 'sensor wipe'/'sensor accept' moved BEFORE the slot-first
 *      catch-all that used to swallow them into CMD_SENSOR_FIELD.
 *   4. Payload values now use RAW tokens (case preserved) — t0..t3 are
 *      lowercased for MATCHING only. Fixes SSIDs/names/paths being
 *      silently lowercased (and satisfies the preserves-case tests).
 * No command or accepted syntax was removed: every form that parsed
 * before this repair still parses to the same CliDemand.
 */

#include "CommandParser.h"
#include "SystemDefs_Validate.h"
#include "SystemDefs_Records.h"
#include "SensorChannelTable.h" /* channelByKey — which fields name a quantity */
#include <stdlib.h>

#if SIMUT_CLI_FULL
static void hexStringToBytes(String hex, uint8_t* out) {
	if (hex.startsWith("0x")) hex = hex.substring(2);
	for (int i = 0; i < 8; i++) {
		out[i] = (uint8_t)strtoul(hex.substring(i * 2, i * 2 + 2).c_str( ), NULL, 16);
	}
}
#endif

CliDemand parseCliCommand(String input) {
	CliDemand cmd;
	cmd.type = CMD_UNKNOWN;
	input.trim( );

	/* ── 'confirm' suffix — stripped before tokenization ── */
	{
		String tail = input;
		tail.toLowerCase( );
		if (tail.endsWith(" confirm")) {
			cmd.confirmed = true;
			input = input.substring(0, input.length( ) - 8);
			input.trim( );
		}
	}

	/* ── Prefix normalization: 'conf|configure <cmd>' ≡ '<cmd>' ──
	 * Keeps the legacy 'conf ip dhcp' style working while the modal CLI
	 * uses bare forms inside (config)#. The single exception is
	 * 'configure terminal', which is mode navigation, not a prefix.
	 * Recursion depth is exactly 1: the stripped input cannot start
	 * with 'conf ' again unless the user typed 'conf conf ...'. */
	{
		String low = input;
		low.toLowerCase( );
		if (low.startsWith("conf ") || low.startsWith("configure ")) {
			String rest = input.substring(input.indexOf(' ') + 1);
			rest.trim( );
			String restLow = rest;
			restLow.toLowerCase( );
			/* '!(a == b)' instead of '!=': the native test stub for String
			 * only provides operator== (firmware String has both). */
			if (!(restLow == "terminal") && rest.length( ) > 0) {
				CliDemand inner = parseCliCommand(rest);
				inner.confirmed = inner.confirmed || cmd.confirmed;
				return inner;
			}
		}
	}

	/* ── Tokenization: up to 6 parts; from the 5th, a leading quote
	 * glues the remainder (quoted sensor names with spaces). ── */
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
		if (spaceIndex == -1) {
			parts[count++] = tempInput;
			tempInput = "";
		} else {
			parts[count++] = tempInput.substring(0, spaceIndex);
			tempInput = tempInput.substring(spaceIndex + 1);
			tempInput.trim( );
		}
	}

	if (count == 0) return cmd;

	/* Lowercased tokens for MATCHING; raw tokens for PAYLOADS. */
	String t0 = parts[0]; t0.toLowerCase( );
	String t1 = count > 1 ? parts[1] : ""; t1.toLowerCase( );
	String t2 = count > 2 ? parts[2] : ""; t2.toLowerCase( );
	String t3 = count > 3 ? parts[3] : ""; t3.toLowerCase( );
	String r1 = count > 1 ? parts[1] : "";
	String r2 = count > 2 ? parts[2] : "";
	String r3 = count > 3 ? parts[3] : "";
	String r4 = count > 4 ? parts[4] : "";
	String r5 = count > 5 ? parts[5] : "";

	/* ── Global shortcuts ── */
	if (t0 == "help" || t0 == "ajuda" || t0 == "?") { cmd.type = CMD_HELP; return cmd; }
	if (t0 == "reload") { cmd.type = CMD_RELOAD; return cmd; }
#if SIMUT_CLI_FULL
	if (t0 == "gpio") { cmd.type = CMD_SHOW_GPIO; return cmd; }

	/* ── Cisco IOS-style mode navigation ── */
	if (t0 == "enable")  { cmd.type = CMD_ENABLE;  return cmd; }
	if (t0 == "disable") { cmd.type = CMD_DISABLE; return cmd; }
	if ((t0 == "config" || t0 == "configure") && t1 == "terminal") { cmd.type = CMD_CONFIGURE; return cmd; }
	if (t0 == "exit") { cmd.type = CMD_EXIT; return cmd; }
	if (t0 == "end")  { cmd.type = CMD_END;  return cmd; }
	if (t0 == "do") {
		/* Everything after 'do ' becomes the inner command (strVal1). */
		String innerCmd = input.substring(2);
		innerCmd.trim( );
		cmd.type = CMD_DO;
		cmd.setStrVal1(innerCmd.c_str( ));
		return cmd;
	}

	/* ── Privileged utilities ── */
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
#endif /* SIMUT_CLI_FULL */

	/* ── show <...> — read-only diagnostics ──
	 * The emergency image keeps the three that answer "why can I not reach
	 * the web": the IP, the identity, and the log. */
	if (t0 == "show") {
		if (t1 == "system" && t2 == "log")     { cmd.type = CMD_SHOW_LOGS;         return cmd; }
		if (t1 == "system" && t2 == "info")    { cmd.type = CMD_SHOW_SYSINFO;      return cmd; }
		if (t1 == "net" && t2 == "status")     { cmd.type = CMD_SHOW_NET;          return cmd; }
#if SIMUT_CLI_FULL
		if (t1 == "themes")                    { cmd.type = CMD_SHOW_THEMES;       return cmd; }
		if (t1 == "sensors")                   { cmd.type = CMD_SHOW_SENSORS;      return cmd; }
		if (t1 == "sensor" && t2 == "types")   { cmd.type = CMD_SHOW_SENSOR_TYPES; return cmd; }
		if (t1 == "gpio")                      { cmd.type = CMD_SHOW_GPIO;         return cmd; }
		if (t1 == "storage" && t2 == "stats")  { cmd.type = CMD_SHOW_STORAGE;      return cmd; }
		if (t1 == "metrics")                   { cmd.type = CMD_SHOW_METRICS;      return cmd; }
#endif
	}

#if SIMUT_CLI_FULL

	/* ── Network addressing (global-config family) ──
	 * Bare forms as advertised by help; 'conf'-prefixed forms arrive
	 * here too via prefix normalization. */
	if (t0 == "ip") {
		if (t1 == "dhcp")    { cmd.type = CMD_IP_CFG; cmd.intVal1 = 0; return cmd; }
		if (t1 == "static")  { cmd.type = CMD_IP_CFG; cmd.intVal1 = 1; return cmd; }
		if (t1 == "addr")    { cmd.type = CMD_IP_CFG; cmd.intVal1 = 2; cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "mask")    { cmd.type = CMD_IP_CFG; cmd.intVal1 = 3; cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "gateway") { cmd.type = CMD_IP_CFG; cmd.intVal1 = 4; cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "dns")     { cmd.type = CMD_IP_CFG; cmd.intVal1 = 5; cmd.setStrVal1(r2.c_str( )); return cmd; }
	}
	if (t0 == "ntp") {
		if (t1 == "on")  { cmd.type = CMD_SET_NTP_ENABLED; cmd.intVal1 = 1; return cmd; }
		if (t1 == "off") { cmd.type = CMD_SET_NTP_ENABLED; cmd.intVal1 = 0; return cmd; }
	}
	if (t0 == "time") {
		cmd.type = CMD_SET_TIME;
		cmd.setStrVal1(t1.c_str( ));
		cmd.setStrVal2(t2.c_str( ));
		return cmd;
	}
	/* 'dns auto|manual ...' (bare, per help) and legacy 'net dns ...'
	 * (reached as 'conf net dns ...' before normalization). */
	if (t0 == "dns" || (t0 == "net" && t1 == "dns")) {
		const int base = (t0 == "dns") ? 1 : 2;          /* index of auto|manual  */
		String sel = count > base ? parts[base] : "";
		sel.toLowerCase( );
		if (sel == "auto") { cmd.type = CMD_SET_DNS_CFG; cmd.intVal1 = 0; return cmd; }
		if (sel == "manual") {
			cmd.type = CMD_SET_DNS_CFG;
			cmd.intVal1 = 1;
			cmd.setStrVal1((count > base + 1 ? parts[base + 1] : String("")).c_str( ));
			cmd.setStrVal2((count > base + 2 ? parts[base + 2] : String("")).c_str( ));
			return cmd;
		}
	}

	/* ── Web users (RBAC) ── */
	if (t0 == "user") {
		if (t1 == "add" && t2.length( ) > 0) {
			cmd.type = CMD_USER_ADD;
			cmd.setStrVal1(r2.c_str( ));
			cmd.setStrVal2(r3.c_str( ));
			return cmd;
		}
		if (t1 == "del" && t2.length( ) > 0) {
			cmd.type = CMD_USER_DEL;
			cmd.setStrVal1(r2.c_str( ));
			return cmd;
		}
		if (t1 == "pass" && t2.length( ) > 0) {
			cmd.type = CMD_USER_PASS;
			cmd.setStrVal1(r2.c_str( ));
			cmd.setStrVal2(r3.c_str( ));
			return cmd;
		}
		if (t1 == "perm" && t2.length( ) > 0 && t3.length( ) > 0) {
			cmd.type = CMD_USER_PERM;
			cmd.setStrVal1(r2.c_str( ));
			cmd.setStrVal2(t3.c_str( )); /* role/mask — matching is case-insensitive */
			return cmd;
		}
	}

#endif /* SIMUT_CLI_FULL */

	/* ── System settings ──
	 * admin reset / factory / format survive into the emergency image: they
	 * are the three recoveries that cannot be performed from the web, either
	 * because the web is what is locked (admin reset) or because it is what
	 * is broken (factory, format). */
	if (t0 == "system") {
		if (t1 == "admin" && t2 == "reset") { cmd.type = CMD_RESET_ADMIN;     return cmd; }
		if (t1 == "factory")                { cmd.type = CMD_FACTORY_RESET;   return cmd; }
		if (t1 == "format")                 { cmd.type = CMD_FORMAT_FS;      return cmd; }
#if SIMUT_CLI_FULL
		if (t1 == "theme")    { cmd.type = CMD_SET_THEME;     cmd.setStrVal1(t2.c_str( )); return cmd; }
		if (t1 == "name")     { cmd.type = CMD_SET_SYS_NAME;  cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "ssid")     { cmd.type = CMD_SET_WIFI_SSID; cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "pass")     { cmd.type = CMD_SET_WIFI_PASS; cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "timezone") {
			cmd.type = CMD_SET_TIMEZONE;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}
		if (t1 == "ntp")      { cmd.type = CMD_SET_NTP;       cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "touch" && t2 == "reset") { cmd.type = CMD_RESET_TOUCH_CAL; return cmd; }
		if (t1 == "history_interval") {
			cmd.type = CMD_SET_HISTORY_INTERVAL;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}
#endif
	}

#if SIMUT_CLI_FULL
	/* ── Wi-Fi aliases (same handlers as 'system ssid/pass') ── */
	if (t0 == "wifi") {
		if (t1 == "ssid") { cmd.type = CMD_SET_WIFI_SSID; cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "pass") { cmd.type = CMD_SET_WIFI_PASS; cmd.setStrVal1(r2.c_str( )); return cmd; }
	}
	if (t0 == "ds18b20" && t1 == "resolution") {
		cmd.type = CMD_SET_DS_RES;
		cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
		return cmd;
	}
	if (t0 == "web" && t1 == "port") {
		cmd.type = CMD_SET_WEB_PORT;
		cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
		return cmd;
	}

	/* ── Telemetry: setters (config) + operations (exec) ── */
	if (t0 == "tel") {
		if (t1 == "server") { cmd.type = CMD_SET_TEL_SERVER; cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "port") {
			cmd.type = CMD_SET_TEL_PORT;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}
		if (t1 == "path")   { cmd.type = CMD_SET_TEL_PATH;   cmd.setStrVal1(r2.c_str( )); return cmd; }
		if (t1 == "batch") {
			cmd.type = CMD_SET_TEL_BATCH;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}
		if (t1 == "interval") {
			cmd.type = CMD_SET_TEL_INTERVAL;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}
		if (t1 == "crypto") {
			cmd.type = CMD_SET_TEL_CRYPTO;
			cmd.setStrVal1(t2.c_str( ));
			cmd.boolVal = (t2 == "on");
			return cmd;
		}
		if (t1 == "mode") {
			cmd.type = CMD_SET_TEL_MODE;
			cmd.setStrVal1(t2.c_str( ));
			if      (t2 == "json")   cmd.intVal1 = TEL_MODE_JSON;
			else if (t2 == "csv")    cmd.intVal1 = TEL_MODE_CSV;
			else if (t2 == "custom") cmd.intVal1 = TEL_MODE_CUSTOM;
			else cmd.intVal1 = -1;
			return cmd;
		}
		if (t1 == "sync")  { cmd.type = CMD_TEL_SYNC;  return cmd; }
		if (t1 == "dump")  { cmd.type = CMD_TEL_DUMP;  return cmd; }
		if (t1 == "reset") { cmd.type = CMD_TEL_RESET; return cmd; }
	}

	/* ── Sensors ──
	 * Order matters: named sub-commands (scan/define/wipe/accept) and the
	 * legacy field-first form MUST precede the slot-first catch-all,
	 * which previously swallowed 'wipe'/'accept'. */
	if (t0 == "sensor") {
		if (t1 == "scan") { cmd.type = CMD_SCAN_SENSORS; return cmd; }

		if (t1 == "define") {
			/* sensor define <gpio> <rom16hex> <hwid> "<name>" [tipo]
			 * [tipo]: ds18b20 | dht22 | bme280 — trailing token, fills
			 * strVal3 for the explicit-type path in CMD_DEFINE_SENSOR
			 * (without it, zero ROM always falls back to DHT22, which
			 * made BME280 impossible to define via CLI). */
			int idx = input.indexOf("define");
			String args = input.substring(idx + 7);
			args.trim( );

			int sp1 = args.indexOf(' ');
			if (sp1 != -1) {
				cmd.intVal1Valid = parseIntStrict(args.substring(0, sp1), cmd.intVal1);
				args = args.substring(sp1 + 1);
				args.trim( );

				int sp2 = args.indexOf(' ');
				if (sp2 != -1) {
					hexStringToBytes(args.substring(0, sp2), cmd.rom);
					args = args.substring(sp2 + 1);
					args.trim( );

					int sp3 = args.indexOf(' ');
					if (sp3 != -1) {
						cmd.setStrVal1(args.substring(0, sp3).c_str( ));
						String fname = args.substring(sp3 + 1);
						fname.replace("\"", "");
						fname.trim( );
						int lastSp = fname.lastIndexOf(' ');
						if (lastSp != -1) {
							String tk = fname.substring(lastSp + 1);
							/* bmp280 belongs here too: it stopped being an alias of
							 * the BME280 when the parts were split into separate
							 * types, and nothing added it to this list — so the one
							 * chip on the bench that has pressure and no humidity
							 * could not be named, and fell through to DHT22. */
							if (tk == "ds18b20" || tk == "dht22" || tk == "bme280" || tk == "bmp280") {
								cmd.setStrVal3(tk.c_str( ));
								fname = fname.substring(0, lastSp);
								fname.trim( );
							}
						} else if (fname == "ds18b20" || fname == "dht22"
						           || fname == "bme280" || fname == "bmp280") {
							/* Name omitted, only the type given: use it for both. */
							cmd.setStrVal3(fname.c_str( ));
						}
						cmd.setStrVal2(fname.c_str( ));
						cmd.type = CMD_DEFINE_SENSOR;
						return cmd;
					}
				}
			}
		}

		if (t1 == "wipe") {
			cmd.type = CMD_WIPE_SENSOR;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}
		if (t1 == "reschema") {
			cmd.type = CMD_RESCHEMA_SENSORS;
			return cmd;
		}
		if (t1 == "remove") {
			cmd.type = CMD_REMOVE_SENSOR;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}
		if (t1 == "accept") {
			cmd.type = CMD_ACCEPT_SENSOR;
			cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
			return cmd;
		}

		/* Legacy field-first form: 'sensor <field> <gpio> <value>'
		 * (previously 'conf sensor tmin 4 -20'; arrives here bare
		 * after prefix normalization). */
		{
			/* tmin/tmax/hmin/hmax stay because the help text and existing
			 * scripts use them. Anything shaped <channel-key>min / <channel-key>max
			 * is accepted from the table, so a new quantity is settable the day
			 * its row lands — this list used to be the whole vocabulary, and a
			 * pressure limit could not be typed at all. */
			bool isField = (t1 == "tmin" || t1 == "tmax" || t1 == "hmin" ||
			                t1 == "hmax" || t1 == "alarm");
			if (!isField && t1.length( ) > 3) {
				String suffix = t1.substring(t1.length( ) - 3);
				if (suffix == "min" || suffix == "max") {
					String key = t1.substring(0, t1.length( ) - 3);
					isField = (channelByKey(key.c_str( )) >= 0);
				}
			}
			if (isField) {
				cmd.type = CMD_SENSOR_FIELD;
				cmd.setStrVal1(t1.c_str( ));
				cmd.intVal1Valid = parseIntStrict(t2, cmd.intVal1);
				cmd.setStrVal2(r3.c_str( ));
				return cmd;
			}
		}

		/* Modal slot-first form: 'sensor <slot> <field> [value]'
		 * (also produced by the config-sensor-N auto-prefix). */
		if (count >= 3) {
			cmd.type = CMD_SENSOR_FIELD;
			cmd.intVal1Valid = parseIntStrict(t1, cmd.intVal1);
			cmd.setStrVal1(t2.c_str( ));
			if (count >= 4) cmd.setStrVal2(r3.c_str( ));
			return cmd;
		}

		/* Bare 'sensor <N>' — enter sensor config mode (0..MAX_SENSORS-1). */
		if (count == 2) {
			bool slotOk = parseIntStrict(t1, cmd.intVal1);
			if (slotOk && cmd.intVal1 >= 0 && cmd.intVal1 < MAX_SENSORS) {
				cmd.type = CMD_SENSOR_ENTER;
				return cmd;
			}
		}
	}

	/* ── Maintenance ── */
	if (t0 == "write" && t1 == "memory") { cmd.type = CMD_WRITE_MEMORY; return cmd; }
	if (t0 == "clear" && t1 == "log")    { cmd.type = CMD_CLEAR_LOGS;   return cmd; }
#endif /* SIMUT_CLI_FULL */

	/* Debug streaming is the one live-diagnosis tool the web cannot replace:
	 * it shows the log as it happens, including during a boot that never
	 * reaches the web server. */
	if (t0 == "debug") {
		cmd.type = CMD_DEBUG;
		if      (t1 == "on")  cmd.intVal1 = 1;
		else if (t1 == "off") cmd.intVal1 = 0;
		else cmd.intVal1 = -1;
		return cmd;
	}

	return cmd;
}
