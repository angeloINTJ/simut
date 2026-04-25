/**
 * @file    AppManager_Commands.cpp
 * @brief   CLI command execution (40+ cases across config, sensors, network, users).
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "SystemDefs.h"
#include "Themes.h"
#include <LittleFS.h>
#include <time.h>

void AppManager::executeCommand(CliDemand cmd) {
    SystemConfig &cfg = _storageMgr.getConfig();
    bool changed = false;

    switch (cmd.type) {
        case CMD_HELP:
            _cmdMgr.printHelp(); break;

        case CMD_SHOW_THEMES:
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln("--- Available Themes ---");
            for(int i=0; i<getThemeCount(); i++) {
                _cmdMgr.consolePrintf(" %2d %-12s %s\n", i, getThemeId(i).c_str(), availableThemes[i].displayName);
            }
            _cmdMgr.consolePrintln("-------------------------------------------");
            break;

        case CMD_SET_THEME: {
            /* CON-005b: funções aceitam String; envolve o char[] em temporário. */
            int idx = getThemeIndexByName(String(cmd.strVal1));
            if (idx == -1) {
                /* Não bateu como nome — tenta como índice numérico, mas só se
                 * for número bem-formado (evita "abc".toInt()==0 aplicar tema 0). */
                int numericIdx = 0;
                if (!parseIntStrict(String(cmd.strVal1), numericIdx)) idx = -1;
                else idx = numericIdx;
            }
            if (idx >= 0 && idx < getThemeCount()) {
                cfg.themeIndex = idx;
                loadTheme(idx);
                _displayMgr.refreshTheme();
                changed = true;
                LOG_CODE(LOG_INFO, "CFG", CFG_THEME_APPLIED, idx, String(availableThemes[idx].displayName));
                _cmdMgr.printSuccess(String(_cmdMgr.isPt() ? "Tema: " : "Theme: ")
                                     + availableThemes[idx].displayName);
            } else {
                LOG_CODE(LOG_WARN, "CFG", CFG_THEME_NOT_FOUND, 0, "");
                _cmdMgr.printError(_cmdMgr.isPt()
                    ? "Tema nao encontrado. Veja 'show themes'."
                    : "Theme not found. Try 'show themes'.");
            }
            break;
        }

        case CMD_SHOW_LOGS: {
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln("--- SYSTEM LOG START ---");
            int logCount = 0;
            auto streamLogFile = [&](const char* path) {

                _storageMgr.enterFlashReadLock();
                bool exists = LittleFS.exists(path);
                File f;
                if (exists) f = LittleFS.open(path, "r");
                _storageMgr.exitFlashReadLock();
                if (exists && f) {

                    char lineBuf[256];
                    while (f.available() && logCount < 2000) {
                        feedWdt();
                        size_t len = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                        if (len == 0) continue;
                        lineBuf[len] = '\0';
                        _cmdMgr.printLogEntry(String(lineBuf));
                        logCount++;
                    }
                    f.close();
                }
            };
            streamLogFile("/system.old");
            streamLogFile("/system.log");
            _cmdMgr.consolePrintln("--- SYSTEM LOG END ---");
            _cmdMgr.consolePrintln("");
            break;
        }

        case CMD_SHOW_SENSORS: _cmdMgr.renderSensorTable(cfg.sensors, MAX_SENSORS); break;
        case CMD_SHOW_METRICS: _cmdMgr.renderMetrics(); break;
        case CMD_SHOW_STORAGE: {
            String rep = _storageMgr.getStatsReport();
            LOG_CODE(LOG_INFO, "STO", STO_STATS_REPORT, 0, rep);
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln(_cmdMgr.isPt()
                ? "--- Estatisticas do Flash ---"
                : "--- Storage Stats ---");
            _cmdMgr.consolePrintln(rep);
            _cmdMgr.printDivider();
            break;
        }
        case CMD_SHOW_SYSINFO: _cmdMgr.renderSystemInfo(cfg); break;
        case CMD_SHOW_NET: {
            String ip = _netMgr.getIpAddress();
            LOG_CODE(LOG_INFO, "NET", NET_SHOW_IP, 0, ip);
            _cmdMgr.consolePrintln("");
            _cmdMgr.consolePrintln(_cmdMgr.isPt()
                ? "--- Status da Rede ---"
                : "--- Network Status ---");
            _cmdMgr.consolePrintf (" IP:   %s\n", ip.c_str());
            _cmdMgr.consolePrintf (" RSSI: %ld dBm\n", (long)_netMgr.getRssi());
            _cmdMgr.printDivider();
            break;
        }

        case CMD_SET_DS_RES: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para resolucao"
                                      : "Invalid number for resolution");
                break;
            }
            if (cmd.intVal1 < 9 || cmd.intVal1 > 12) {
                _cmdMgr.printError(pt ? "Resolucao fora de range (9-12)"
                                      : "Resolution out of range (9-12)");
                break;
            }
            if (!_sensorMgr.setDs18Resolution((DS18B20PIO::Resolution)cmd.intVal1)) {
                _cmdMgr.printError(pt ? "Falha ao aplicar resolucao no sensor"
                                      : "Failed to apply resolution");
                break;
            }
            cfg.ds18Resolution = cmd.intVal1;
            changed = true;
            break;
        }

        case CMD_SET_SYS_NAME: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidName(cmd.strVal1, sizeof(cfg.deviceName) - 1)) {
                _cmdMgr.printError(pt ? "Nome invalido (1-31 chars, sem ctrl chars)"
                                      : "Invalid name (1-31 chars, no ctrl chars)");
                break;
            }
            safeCopy(cfg.deviceName, cmd.strVal1, sizeof(cfg.deviceName));
            changed = true;
            break;
        }
        case CMD_SET_WIFI_SSID: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.wifiSsid) - 1)) {
                _cmdMgr.printError(pt ? "SSID invalido (max 31, sem ctrl chars)"
                                      : "Invalid SSID (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.wifiSsid, cmd.strVal1, sizeof(cfg.wifiSsid));
            changed = true;
            break;
        }
        case CMD_SET_WIFI_PASS: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.wifiPass) - 1)) {
                _cmdMgr.printError(pt ? "Senha invalida (max 31, sem ctrl chars)"
                                      : "Invalid pass (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.wifiPass, cmd.strVal1, sizeof(cfg.wifiPass));
            changed = true;
            break;
        }
        case CMD_SET_TIMEZONE: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para timezone"
                                      : "Invalid number for timezone");
                break;
            }
            if (cmd.intVal1 < -12 || cmd.intVal1 > 14) {
                _cmdMgr.printError(pt ? "Timezone fora de range (-12 a +14)"
                                      : "Timezone out of range (-12 to +14)");
                break;
            }
            cfg.timezoneOffset = (int8_t)cmd.intVal1;
            NetworkManager::applyTimezone(cfg.timezoneOffset);
            changed = true;
            break;
        }

        case CMD_SET_NTP: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.ntpServer) - 1)) {
                _cmdMgr.printError(pt ? "NTP invalido (max 31, sem ctrl chars)"
                                      : "Invalid NTP (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.ntpServer, cmd.strVal1, sizeof(cfg.ntpServer));
            cfg.ntpServer[sizeof(cfg.ntpServer) - 1] = '\0';
            changed = true;
            break;
        }

        case CMD_SET_TEL_SERVER: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.telServer) - 1)) {
                _cmdMgr.printError(pt ? "URL invalida (max 63, sem ctrl chars)"
                                      : "Invalid URL (max 63, no ctrl chars)");
                break;
            }
            safeCopy(cfg.telServer, cmd.strVal1, sizeof(cfg.telServer));
            changed = true;
            break;
        }
        case CMD_SET_TEL_PORT: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para porta"
                                      : "Invalid number for port");
                break;
            }
            if (cmd.intVal1 < 1 || cmd.intVal1 > 65535) {
                _cmdMgr.printError(pt ? "Porta fora de range (1-65535)"
                                      : "Port out of range (1-65535)");
                break;
            }
            cfg.telPort = (uint16_t)cmd.intVal1;
            changed = true;
            break;
        }
        case CMD_SET_TEL_PATH: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidCfgString(cmd.strVal1, sizeof(cfg.telPath) - 1)) {
                _cmdMgr.printError(pt ? "Path invalido (max 31, sem ctrl chars)"
                                      : "Invalid path (max 31, no ctrl chars)");
                break;
            }
            safeCopy(cfg.telPath, cmd.strVal1, sizeof(cfg.telPath));
            changed = true;
            break;
        }
        case CMD_SET_TEL_BATCH: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para batch"
                                      : "Invalid number for batch");
                break;
            }
            if (cmd.intVal1 < 1 || cmd.intVal1 > 50) {
                _cmdMgr.printError(pt ? "Batch fora de range (1-50)"
                                      : "Batch out of range (1-50)");
                break;
            }
            cfg.telBatchSize = (uint8_t)cmd.intVal1;
            changed = true;
            break;
        }
        case CMD_SET_TEL_INTERVAL: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para intervalo"
                                      : "Invalid number for interval");
                break;
            }
            if (cmd.intVal1 < 0) {
                _cmdMgr.printError(pt ? "Intervalo deve ser >= 0 (0 = off)"
                                      : "Interval must be >= 0 (0 = off)");
                break;
            }
            cfg.telInterval = (uint32_t)cmd.intVal1;
            changed = true;
            break;
        }
        case CMD_SET_TEL_CRYPTO: {
            const bool pt = _cmdMgr.isPt();
            if (strcmp(cmd.strVal1, "on") != 0 && strcmp(cmd.strVal1, "off") != 0) {
                _cmdMgr.printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
                break;
            }
            cfg.telEncryption = cmd.boolVal;
            changed = true;
            break;
        }
        case CMD_SET_TEL_MODE: {
            const bool pt = _cmdMgr.isPt();
            if (cmd.intVal1 < 0) {
                _cmdMgr.printError(pt ? "Modo desconhecido (use json|csv|custom)"
                                      : "Unknown mode (use json|csv|custom)");
                break;
            }
            cfg.telMode = cmd.intVal1;
            changed = true;
            break;
        }

        case CMD_RESET_ADMIN: {
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: reseta senha do admin p/ aleatoria."
                                     : "WARN: resets admin password to random 8-char.");
                _cmdMgr.printInfo(pt ? "Use 'conf system admin reset confirm'."
                                     : "Run 'conf system admin reset confirm'.");
                break;
            }
            /* SEC-003/F12.3: gera senha random no reset CLI também (em vez de "simut"
             * hardcoded, que era tão vulnerável quanto o "admin" pré-patch). Mostra
             * no próprio CLI — quem pode rodar o comando já tem acesso USB.
             * SEC-007/009 (F15): salt random + hashVersion=1 (formato v1). */
            char newPlain[9];
            _storageMgr.generateInitialAdminPassword(newPlain, sizeof(newPlain));
            _storageMgr.generateSalt(cfg.users[0].salt);
            String preHash = _storageMgr.sha256Hex(String(newPlain));
            String hashed = _storageMgr.hashPasswordV1("admin", preHash, cfg.users[0].salt);
            safeCopy(cfg.users[0].password, hashed.c_str(), sizeof(cfg.users[0].password));
            cfg.users[0].hashVersion = 1;
            cfg.users[0].mustChangePassword = true;
            const bool pt = _cmdMgr.isPt();
            _cmdMgr.printInfo(pt ? "Senha admin resetada. Nova senha (unica vez):"
                                 : "Admin password reset. New password (shown once):");
            _cmdMgr.printInfo(String("  ") + newPlain);
            _cmdMgr.printInfo(pt ? "Trocar no 1o login via web (forcado)."
                                 : "Change on 1st web login (forced).");
            /* Zera plaintext local após log; storage mantém seu próprio buffer RAM
             * também atualizado para que o banner do Serial/isFactoryDefaults
             * reflita a senha atual. */
            // Atualiza o buffer interno do storage (se getter exposto, usa):
            // Como não há setter público, cria via loadDefaults seria destrutivo.
            // Alternativa: expor setter, ou deixar que o próximo boot mostre nada.
            // Decisão: só mostrar no CLI aqui e não persistir em RAM. Admin que
            // rodou o comando pode anotar; se perdeu, roda de novo (é idempotente).
            volatile char* v = newPlain;
            for (size_t i = 0; i < sizeof(newPlain); i++) v[i] = 0;
            changed = true;
            break;
        }

        case CMD_RESET_TOUCH_CAL: {
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: reseta calibracao do touch."
                                     : "WARN: resets touch calibration.");
                _cmdMgr.printInfo(pt ? "Use 'conf system touch reset confirm'."
                                     : "Run 'conf system touch reset confirm'.");
                break;
            }
            /* Limpa calibração do touch na config (invalida magic) */
            TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
            memset(cal, 0, sizeof(TouchCalData));
            _displayMgr.resetTouchCalibration();
            _cmdMgr.printInfo(_cmdMgr.isPt()
                ? "Calibracao do touch resetada p/ default."
                : "Touch calibration reset to factory defaults.");
            changed = true;
            break;
        }

        case CMD_FACTORY_RESET: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.confirmed) {
                _cmdMgr.printInfo(pt ? "ATENCAO: factory reset APAGA TODA config + reboot."
                                     : "WARN: factory reset WIPES ALL config + reboots.");
                _cmdMgr.printInfo(pt ? "Use 'conf system factory confirm'."
                                     : "Run 'conf system factory confirm'.");
                break;
            }
            LOG_CODE(LOG_WARN, "SYS", SYS_REBOOT_USER, 0, TRL("Factory reset", "Factory reset"));
            _storageMgr.resetToFactory();
            delay(100);
            LogManager::instance().markCleanReboot();
            rp2040.reboot();
        }

        case CMD_SET_NTP_ENABLED: {
            const bool pt = _cmdMgr.isPt();
            bool en = (cmd.intVal1 != 0);
            _storageMgr.setNtpEnabled(en);
            _cmdMgr.printSuccess(en ? (pt ? "NTP: habilitado" : "NTP: enabled")
                                    : (pt ? "NTP: desabilitado" : "NTP: disabled"));
            changed = true;
            break;
        }

        case CMD_SET_DNS_CFG: {
            const bool pt = _cmdMgr.isPt();
            if (cmd.intVal1 == 0) {  /* auto */
                _storageMgr.setDnsAuto(true);
                _cmdMgr.printSuccess(pt ? "DNS: automatico (DHCP)" : "DNS: auto (DHCP)");
                changed = true;
            } else {  /* manual */
                if (!isValidIpv4(cmd.strVal1)) {
                    _cmdMgr.printError(pt ? "IPv4 invalido para DNS primario"
                                          : "Invalid IPv4 for primary DNS");
                    break;
                }
                /* Secundário opcional: "" aceito (limpa o secundário). */
                if (cmd.strVal2[0] != '\0' && !isValidIpv4(cmd.strVal2)) {
                    _cmdMgr.printError(pt ? "IPv4 invalido para DNS secundario"
                                          : "Invalid IPv4 for secondary DNS");
                    break;
                }
                _storageMgr.setDnsAuto(false);
                safeCopy(cfg.staticDns, cmd.strVal1, sizeof(cfg.staticDns));
                _storageMgr.setSecondaryDns(cmd.strVal2);
                if (cmd.strVal2[0] != '\0') {
                    _cmdMgr.printSuccess(String(pt ? "DNS: manual; dns1=" : "DNS: manual; dns1=")
                                         + cmd.strVal1 + ", dns2=" + cmd.strVal2);
                } else {
                    _cmdMgr.printSuccess(String(pt ? "DNS: manual; dns1=" : "DNS: manual; dns1=")
                                         + cmd.strVal1);
                }
                changed = true;
            }
            break;
        }

        case CMD_SET_TIME: {
            const bool pt = _cmdMgr.isPt();
            int y, mo, d, h, mi, s;
            if (sscanf(cmd.strVal1, "%4d-%2d-%2d", &y, &mo, &d) != 3
                || sscanf(cmd.strVal2, "%2d:%2d:%2d", &h, &mi, &s) != 3) {
                _cmdMgr.printError(pt ? "Formato invalido. Use: conf time AAAA-MM-DD HH:MM:SS"
                                      : "Invalid format. Use: conf time YYYY-MM-DD HH:MM:SS");
                break;
            }
            if (y < 2026 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31
                || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
                _cmdMgr.printError(pt ? "Valores fora de range (ano >= 2026)"
                                      : "Values out of range (year >= 2026)");
                break;
            }
            struct tm tmLocal = {0};
            tmLocal.tm_year = y - 1900;
            tmLocal.tm_mon  = mo - 1;
            tmLocal.tm_mday = d;
            tmLocal.tm_hour = h;
            tmLocal.tm_min  = mi;
            tmLocal.tm_sec  = s;
            /* mktime() usa TZ env var (setado em applyTimezone no boot) para
             * converter local time → epoch UTC. */
            time_t epoch = mktime(&tmLocal);
            if (epoch <= 1600000000) {
                _cmdMgr.printError(pt ? "Falha na conversao de tempo"
                                      : "Time conversion failed");
                break;
            }
            _netMgr.setManualTime(epoch);
            _cmdMgr.printSuccess(pt ? "Hora aplicada (imediato, nao persiste em reboot)"
                                    : "Time applied (immediate; not persisted across reboot)");
            /* Sem changed=true: ação imediata, não vai pro flash. */
            break;
        }

        case CMD_DEFINE_SENSOR: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO"
                                      : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)"
                                      : "Slot out of range (0-9)");
                break;
            }
            SensorRecord &r = cfg.sensors[cmd.intVal1];
            r.active = true;
            r.gpio = cmd.intVal1;
            memcpy(r.rom, cmd.rom, 8);
            safeCopy(r.hwId, cmd.strVal1, sizeof(r.hwId));
            safeCopy(r.friendlyName, cmd.strVal2, sizeof(r.friendlyName));
            _cmdMgr.printSuccess(pt ? "Sensor mapeado em RAM."
                                    : "Sensor mapped in RAM.");
            break;
        }

        case CMD_WIPE_SENSOR: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.confirmed) {
                _cmdMgr.printInfo(pt ? "ATENCAO: reseta historico do sensor."
                                     : "WARN: resets sensor history epoch.");
                _cmdMgr.printInfo(pt ? "Use 'sensor wipe <gpio> confirm'."
                                     : "Run 'sensor wipe <gpio> confirm'.");
                break;
            }
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO"
                                      : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)"
                                      : "Slot out of range (0-9)");
                break;
            }
            cfg.sensors[cmd.intVal1].provisionEpoch = _netMgr.getEpoch();
            changed = true;
            _cmdMgr.printSuccess((pt ? "Historico resetado no Slot "
                                     : "Sensor history wiped for Slot ") + String(cmd.intVal1));
            break;
        }

        case CMD_ACCEPT_SENSOR: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO"
                                      : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)"
                                      : "Slot out of range (0-9)");
                break;
            }
            uint8_t gpio = (uint8_t)cmd.intVal1;
            if (gpio < MAX_SENSORS) {
                uint8_t foundRom[8];
                if (_sensorMgr.identifyPhysicalSensor(gpio, foundRom)) {
                    if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) {
                        _cmdMgr.printError((pt ? "Sensor invalido no GPIO "
                                               : "Invalid physical sensor on GPIO ") + String(gpio));
                    } else {
                        String dbId; float dbOffset = 0.0f; String dbName;
                        _storageMgr.getCalibrationData(foundRom, dbId, dbOffset, dbName);

                        String currentId = String(cfg.sensors[gpio].hwId);

                        cfg.sensors[gpio].active = true;
                        cfg.sensors[gpio].gpio = gpio;
                        memcpy(cfg.sensors[gpio].rom, foundRom, 8);

                        if (dbId.length() > 0) { safeCopy(cfg.sensors[gpio].hwId, dbId.c_str(), sizeof(cfg.sensors[gpio].hwId)); }
                        else { safeCopy(cfg.sensors[gpio].hwId, "LIB_SENS", sizeof(cfg.sensors[gpio].hwId)); }

                        if (dbName.length() > 0) { safeCopy(cfg.sensors[gpio].friendlyName, dbName.c_str(), sizeof(cfg.sensors[gpio].friendlyName)); }
                        else { safeCopy(cfg.sensors[gpio].friendlyName, pt ? "Sensor Reconhecido" : "Recognized Sensor",
                                        sizeof(cfg.sensors[gpio].friendlyName)); }
                        cfg.sensors[gpio].friendlyName[31] = '\0';

                        if (currentId != String(cfg.sensors[gpio].hwId)) {
                            cfg.sensors[gpio].provisionEpoch = _netMgr.getEpoch();
                            _cmdMgr.printInfo(pt ? "Novo hardware detectado. Epoch atualizado."
                                                 : "New Hardware Context Detected. Epoch updated.");
                        }

                        /* F-LOCKOUT-STUCK: wrappa save+reload no mesmo quiet mode (idem CMD_WRITE_MEMORY). */
                        _displayMgr.requestQuietMode();   /* default 15s timeout */
                        _storageMgr.saveConfiguration();
                        loadAndCalibrateSensors();
                        _displayMgr.releaseQuietMode();
                        _cmdMgr.printSuccess((pt ? "Sensor aceito e vinculado ao Slot "
                                                 : "Sensor accepted and bound to Slot ") + String(gpio));
                    }
                } else {
                    _cmdMgr.printError((pt ? "Nenhum sensor no GPIO "
                                           : "No physical sensor detected on GPIO ") + String(gpio));
                }
            }
            break;
        }

        case CMD_SCAN_SENSORS:
            if (!_sensorMgr.isScanning()) { _sensorMgr.startScan(); _waitingScan = true; }
            break;

        case CMD_WRITE_MEMORY: {
            /* F-LOCKOUT-STUCK: wrappa save + reload de sensores no mesmo
             * quiet mode (re-entrant). loadAndCalibrateSensors emite
             * APP_SENSORS_CALIBRATED via LOG_CODE → LogManager.requestFsLock
             * que, fora de quiet mode, caía em lockout IRQ-based e stuck. */
            _displayMgr.requestQuietMode();   /* default 15s timeout */
            bool saved = _storageMgr.saveConfiguration();
            if (saved) {
                loadAndCalibrateSensors();
            }
            _displayMgr.releaseQuietMode();
            if (saved) {
                _cmdMgr.printSuccess(_cmdMgr.isPt()
                    ? "Config salva no Flash!"
                    : "Config saved to Flash!");
            }
            break;
        }

        case CMD_CLEAR_LOGS:
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: apaga todos os logs."
                                     : "WARN: deletes all system logs.");
                _cmdMgr.printInfo(pt ? "Use 'clear log confirm' para prosseguir."
                                     : "Run 'clear log confirm' to proceed.");
                break;
            }
            _storageMgr.enterFlashSafeMode();
            LittleFS.remove("/system.log"); LittleFS.remove("/system.old");
            _storageMgr.exitFlashSafeMode();
            LogManager::instance().begin(true, LOG_DEBUG);
            _cmdMgr.printSuccess(_cmdMgr.isPt() ? "Logs apagados." : "Logs cleared.");
            break;

        case CMD_RELOAD:
            if (!cmd.confirmed) {
                const bool pt = _cmdMgr.isPt();
                _cmdMgr.printInfo(pt ? "ATENCAO: vai reiniciar o dispositivo."
                                     : "WARN: will reboot the device.");
                _cmdMgr.printInfo(pt ? "Use 'reload confirm' para prosseguir."
                                     : "Run 'reload confirm' to proceed.");
                break;
            }
            LOG_CODE(LOG_WARN, "SYS", SYS_REBOOT_USER, 0, TRL("Reboot via CLI", "Reboot via CLI"));
            delay(100);     /* Garante flush do log para flash */
            LogManager::instance().markCleanReboot();
            rp2040.reboot();
            break;
        case CMD_TEL_SYNC:
            /* Silencioso por design: usuario ve o log natural
             * "Telemetria enviada: ..." quando ha dados para enviar. */
            _telemetryMgr.forceSync();
            break;

        case CMD_TEL_DUMP:
            _telemetryMgr.armPayloadDump();
            _telemetryMgr.forceSync();
            /* Se tinha dados, _dumpPayloadNext foi consumido (dump ja saiu).
             * Se nao tinha, a flag esta armada e dispara no proximo sync. */
            if (_telemetryMgr.isPayloadDumpArmed()) {
                _cmdMgr.printSuccess(_cmdMgr.isPt()
                    ? "Sem dados pendentes; dump armado para o proximo sync."
                    : "No pending data; dump armed for next sync.");
            }
            break;

        case CMD_DEBUG: {
            CliConfigData* cli = reinterpret_cast<CliConfigData*>(
                cfg.reserved + CLI_CONFIG_OFFSET);
            const bool pt = _cmdMgr.isPt();
            if (cmd.intVal1 == 1 || cmd.intVal1 == 0) {
                bool on = (cmd.intVal1 == 1);
                cli->magic = CLI_CONFIG_MAGIC;
                cli->debugMode = on ? 1 : 0;
                LogManager::instance().setConsoleStream(on);
                _cmdMgr.setDebugMode(on);
                _cmdMgr.printSuccess(on ? (pt ? "Debug: LIGADO" : "Debug: ON")
                                        : (pt ? "Debug: DESLIGADO" : "Debug: OFF"));
                changed = true;
            } else {
                _cmdMgr.printInfo(_cmdMgr.isDebugMode()
                    ? (pt ? "Debug: LIGADO" : "Debug: ON")
                    : (pt ? "Debug: DESLIGADO" : "Debug: OFF"));
            }
            break;
        }

        case CMD_LANGUAGE: {
            if (cmd.intVal1 == LANG_PT || cmd.intVal1 == LANG_EN) {
                cfg.displayLang = (uint8_t)cmd.intVal1;
                _displayMgr.setLanguage(cfg.displayLang);
                _cmdMgr.setCliLang(cfg.displayLang);
                LogManager::instance().setLanguage(cfg.displayLang);
                LOG_CODE(LOG_INFO, "APP", APP_UI_LANG_CHANGED, cmd.intVal1, "");
                _cmdMgr.printSuccess(cmd.intVal1 == LANG_PT
                    ? "Idioma: Portugues (BR)"
                    : "Language: English");
                changed = true;
            } else {
                _cmdMgr.printInfo(_cmdMgr.isPt()
                    ? "Idioma atual: Portugues (BR)"
                    : "Current language: English");
            }
            break;
        }

        case CMD_IP_CFG: {
            const bool pt = _cmdMgr.isPt();
            switch (cmd.intVal1) {
                case 0:  /* dhcp */
                    cfg.useDhcp = true;
                    _cmdMgr.printSuccess(pt ? "Modo IP: DHCP" : "IP mode: DHCP");
                    changed = true;
                    break;
                case 1:  /* static */
                    cfg.useDhcp = false;
                    _cmdMgr.printSuccess(pt ? "Modo IP: estatico" : "IP mode: static");
                    changed = true;
                    break;
                case 2: case 3: case 4: case 5: {
                    if (!isValidIpv4(cmd.strVal1)) {
                        _cmdMgr.printError(pt ? "IPv4 invalido (ex: 192.168.1.100)"
                                              : "Invalid IPv4 (e.g. 192.168.1.100)");
                        break;
                    }
                    char* dst = nullptr; size_t dstSize = 0;
                    const char* label = "";
                    if (cmd.intVal1 == 2) { dst = cfg.staticIp;      dstSize = sizeof(cfg.staticIp);      label = "addr"; }
                    else if (cmd.intVal1 == 3) { dst = cfg.staticMask;  dstSize = sizeof(cfg.staticMask);  label = "mask"; }
                    else if (cmd.intVal1 == 4) { dst = cfg.staticGateway; dstSize = sizeof(cfg.staticGateway); label = "gateway"; }
                    else                       { dst = cfg.staticDns;     dstSize = sizeof(cfg.staticDns);     label = "dns"; }
                    safeCopy(dst, cmd.strVal1, dstSize);
                    _cmdMgr.printSuccess((pt ? "IP " : "IP ") + String(label) + ": " + cmd.strVal1);
                    changed = true;
                    break;
                }
                default:
                    _cmdMgr.printError(pt ? "Subcomando IP invalido" : "Invalid IP subcommand");
                    break;
            }
            break;
        }

        case CMD_SENSOR_FIELD: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid) {
                _cmdMgr.printError(pt ? "Numero invalido para GPIO" : "Invalid number for GPIO");
                break;
            }
            if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
                _cmdMgr.printError(pt ? "Slot fora de range (0-9)" : "Slot out of range (0-9)");
                break;
            }
            if (cmd.strVal2[0] == '\0') {
                _cmdMgr.printError(pt ? "Valor ausente" : "Missing value");
                break;
            }
            SensorRecord &r = cfg.sensors[cmd.intVal1];
            const char* field = cmd.strVal1;   /* CON-005b: strVal1 agora char[] */
            if (strcmp(field, "alarm") == 0) {
                String v = cmd.strVal2; v.toLowerCase();
                if (v != "on" && v != "off") {
                    _cmdMgr.printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
                    break;
                }
                r.alarmsActive = (v == "on");
                _cmdMgr.printSuccess((pt ? "Alarme slot " : "Alarm slot ") + String(cmd.intVal1) + ": " + v);
                changed = true;
            } else {
                /* Valores numéricos (float) com range sensato para temperaturas/umidade. */
                float val = atof(cmd.strVal2);
                /* toFloat retorna 0.0 para input inválido — distinguir "0.0" legítimo. */
                if (val == 0.0f && strcmp(cmd.strVal2, "0") != 0 && strcmp(cmd.strVal2, "0.0") != 0
                                 && strcmp(cmd.strVal2, "-0") != 0 && strcmp(cmd.strVal2, "-0.0") != 0) {
                    _cmdMgr.printError(pt ? "Valor numerico invalido" : "Invalid numeric value");
                    break;
                }
                if (field == "tmin" || field == "tmax") {
                    if (val < -50.0f || val > 150.0f) {
                        _cmdMgr.printError(pt ? "Temp fora de range (-50 a 150)"
                                              : "Temp out of range (-50 to 150)");
                        break;
                    }
                    if (field == "tmin") r.tempMin = val; else r.tempMax = val;
                } else if (field == "hmin" || field == "hmax") {
                    if (val < 0.0f || val > 100.0f) {
                        _cmdMgr.printError(pt ? "Umid fora de range (0-100)"
                                              : "Hum out of range (0-100)");
                        break;
                    }
                    if (field == "hmin") r.humMin = val; else r.humMax = val;
                } else {
                    _cmdMgr.printError(pt ? "Campo desconhecido" : "Unknown field");
                    break;
                }
                _cmdMgr.printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1)
                                      + " " + field + "=" + cmd.strVal2);
                changed = true;
            }
            break;
        }

        case CMD_USER_ADD: {
            const bool pt = _cmdMgr.isPt();
            if (!isValidName(cmd.strVal1, 31)) {
                _cmdMgr.printError(pt ? "Username invalido (1-31, sem ctrl chars)"
                                      : "Invalid username (1-31, no ctrl chars)");
                break;
            }
            if (cmd.strVal2[0] == '\0' || strlen(cmd.strVal2) > 64) {
                _cmdMgr.printError(pt ? "Senha ausente ou muito longa (1-64)"
                                      : "Password missing or too long (1-64)");
                break;
            }
            if (!isValidCfgString(cmd.strVal2, 64)) {
                _cmdMgr.printError(pt ? "Senha tem chars de controle"
                                      : "Password has control chars");
                break;
            }
            /* Nome não pode colidir com usuário existente. */
            bool exists = false;
            int freeSlot = -1;
            for (int i = 0; i < MAX_USERS; i++) {
                if (cfg.users[i].active) {
                    if (strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) {
                        exists = true; break;
                    }
                } else if (freeSlot < 0 && i >= 1) {  /* slot 0 = admin, protegido */
                    freeSlot = i;
                }
            }
            if (exists) {
                _cmdMgr.printError(pt ? "Usuario ja existe" : "User already exists");
                break;
            }
            if (freeSlot < 0) {
                _cmdMgr.printError(pt ? "Sem slot livre (max usuarios)"
                                      : "No free slot (max users)");
                break;
            }
            safeCopy(cfg.users[freeSlot].username, cmd.strVal1, sizeof(cfg.users[freeSlot].username));
            {
                /* CON-005b: sha256Hex/hashPassword aceitam String; wraps temporários. */
                String preHash = _storageMgr.sha256Hex(String(cmd.strVal2));
                String hashed = _storageMgr.hashPassword(String(cmd.strVal1), preHash);
                safeCopy(cfg.users[freeSlot].password, hashed.c_str(), sizeof(cfg.users[freeSlot].password));
            }
            cfg.users[freeSlot].active = true;
            cfg.users[freeSlot].permissions = (PERM_DASHBOARD | PERM_HISTORY);
            cfg.users[freeSlot].mustChangePassword = false;
            _cmdMgr.printSuccess(String(pt ? "Usuario criado: " : "User created: ") + cmd.strVal1);
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, freeSlot,
                     String(TRL("CLI created user: ", "CLI criou usuario: ")) + cmd.strVal1);
            changed = true;
            break;
        }

        case CMD_USER_DEL: {
            const bool pt = _cmdMgr.isPt();
            if (strcasecmp(cmd.strVal1, "admin") == 0) {
                _cmdMgr.printError(pt ? "Nao e permitido deletar 'admin'"
                                      : "Cannot delete 'admin'");
                break;
            }
            bool found = false;
            for (int i = 1; i < MAX_USERS; i++) {
                if (cfg.users[i].active && strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) {
                    cfg.users[i].active = false;
                    memset(cfg.users[i].password, 0, sizeof(cfg.users[i].password));
                    _cmdMgr.printSuccess(String(pt ? "Usuario removido: " : "User deleted: ") + cmd.strVal1);
                    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, i,
                             String(TRL("CLI deleted user: ", "CLI apagou usuario: ")) + cmd.strVal1);
                    changed = true;
                    found = true;
                    break;
                }
            }
            if (!found) _cmdMgr.printError(pt ? "Usuario nao encontrado" : "User not found");
            break;
        }

        case CMD_USER_PASS: {
            const bool pt = _cmdMgr.isPt();
            if (cmd.strVal2[0] == '\0' || strlen(cmd.strVal2) > 64
                || !isValidCfgString(cmd.strVal2, 64)) {
                _cmdMgr.printError(pt ? "Nova senha invalida (1-64, sem ctrl chars)"
                                      : "Invalid new password (1-64, no ctrl chars)");
                break;
            }
            bool found = false;
            for (int i = 0; i < MAX_USERS; i++) {
                if (cfg.users[i].active && strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) {
                    /* CON-005b: sha256Hex aceita String; wrap temporário.
                     * SEC-007/009 (F15): salt random + hashVersion=1. */
                    _storageMgr.generateSalt(cfg.users[i].salt);
                    String preHash = _storageMgr.sha256Hex(String(cmd.strVal2));
                    String hashed = _storageMgr.hashPasswordV1(String(cfg.users[i].username), preHash, cfg.users[i].salt);
                    safeCopy(cfg.users[i].password, hashed.c_str(), sizeof(cfg.users[i].password));
                    cfg.users[i].hashVersion = 1;
                    cfg.users[i].mustChangePassword = false;
                    _cmdMgr.printSuccess(String(pt ? "Senha atualizada: " : "Password updated: ") + cmd.strVal1);
                    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, i,
                             String(TRL("CLI reset password: ", "CLI resetou senha: ")) + cmd.strVal1);
                    changed = true;
                    found = true;
                    break;
                }
            }
            if (!found) _cmdMgr.printError(pt ? "Usuario nao encontrado" : "User not found");
            break;
        }

        case CMD_SET_WEB_PORT: {
            const bool pt = _cmdMgr.isPt();
            if (!cmd.intVal1Valid || cmd.intVal1 < 1 || cmd.intVal1 > 65535) {
                _cmdMgr.printError(pt ? "Porta invalida (1..65535)"
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
            _cmdMgr.printSuccess(buf);
            changed = true;
            break;
        }

        case CMD_UNKNOWN:
        default:
            LOG_CODE(LOG_WARN, "CLI", CLI_UNKNOWN_CMD, 0, "");
            _cmdMgr.printError(_cmdMgr.isPt()
                ? "Comando desconhecido. Digite 'help'."
                : "Unknown command. Type 'help'.");
            break;
    }

    if (changed) _cmdMgr.printInfo(_cmdMgr.isPt()
        ? "RAM OK. Use 'write memory' para salvar."
        : "RAM updated. Run 'write memory' to persist.");
}

/* =========================================================================== */
/*             CORE 0 YIELD — UI EVENTS + SOUND + SENSOR UPDATE              */
/* =========================================================================== */
/**
 * @brief Process pending UI events, sound signals, and sensor readings.
 * Called from the main loop and from web server light-yield callbacks.
 * Protected against re-entrancy with a static guard flag.
