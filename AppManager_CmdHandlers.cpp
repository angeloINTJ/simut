/**
 * @file    AppManager_CmdHandlers.cpp
 * @brief   v3.36.2 (Fase 18.3 / A1): handlers extraídos dos cases mais longos
 *          do switch de AppManager::executeCommand. Critério: >=30 linhas.
 *          Mantém semântica idêntica ao original — refactor mecânico só de
 *          legibilidade (executeCommand passa de 952 → ~600 linhas).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
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
#include <time.h>

void AppManager::cmdHandleSensorField(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();
    if (!cmd.intVal1Valid) {
        _cmdMgr->printError(pt ? "Numero invalido para GPIO" : "Invalid number for GPIO");
        return;
    }
    if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
        _cmdMgr->printError(pt ? "Slot fora de range (0-9)" : "Slot out of range (0-9)");
        return;
    }
    if (cmd.strVal2[0] == '\0') {
        _cmdMgr->printError(pt ? "Valor ausente" : "Missing value");
        return;
    }
    SensorRecord &r = cfg.sensors[cmd.intVal1];
    const char* field = cmd.strVal1;
    if (strcmp(field, "alarm") == 0) {
        String v = cmd.strVal2; v.toLowerCase();
        if (v != "on" && v != "off") {
            _cmdMgr->printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
            return;
        }
        r.alarmsActive = (v == "on");
        _cmdMgr->printSuccess((pt ? "Alarme slot " : "Alarm slot ") + String(cmd.intVal1) + ": " + v);
        changed = true;
        return;
    }
    /* Numérico (float) com range sensato. */
    float val = atof(cmd.strVal2);
    if (val == 0.0f && strcmp(cmd.strVal2, "0") != 0 && strcmp(cmd.strVal2, "0.0") != 0
                     && strcmp(cmd.strVal2, "-0") != 0 && strcmp(cmd.strVal2, "-0.0") != 0) {
        _cmdMgr->printError(pt ? "Valor numerico invalido" : "Invalid numeric value");
        return;
    }
    /* v3.36.2 (A4): strcmp em vez de == (comparava ponteiros — bug real). */
    if (strcmp(field, "tmin") == 0 || strcmp(field, "tmax") == 0) {
        if (val < -50.0f || val > 150.0f) {
            _cmdMgr->printError(pt ? "Temp fora de range (-50 a 150)" : "Temp out of range (-50 to 150)");
            return;
        }
        if (strcmp(field, "tmin") == 0) r.tempMin = val; else r.tempMax = val;
    } else if (strcmp(field, "hmin") == 0 || strcmp(field, "hmax") == 0) {
        if (val < 0.0f || val > 100.0f) {
            _cmdMgr->printError(pt ? "Umid fora de range (0-100)" : "Hum out of range (0-100)");
            return;
        }
        if (strcmp(field, "hmin") == 0) r.humMin = val; else r.humMax = val;
    } else {
        _cmdMgr->printError(pt ? "Campo desconhecido" : "Unknown field");
        return;
    }
    _cmdMgr->printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1) + " " + field + "=" + cmd.strVal2);
    changed = true;
}

void AppManager::cmdHandleAcceptSensor(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();
    (void)changed;  /* save é feito por saveConfiguration() abaixo, não via flag. */
    if (!cmd.intVal1Valid) {
        _cmdMgr->printError(pt ? "Numero invalido para GPIO" : "Invalid number for GPIO");
        return;
    }
    if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
        _cmdMgr->printError(pt ? "Slot fora de range (0-9)" : "Slot out of range (0-9)");
        return;
    }
    uint8_t gpio = (uint8_t)cmd.intVal1;
    if (gpio >= MAX_SENSORS) return;

    uint8_t foundRom[8];
    if (!_sensorMgr->identifyPhysicalSensor(gpio, foundRom)) {
        _cmdMgr->printError((pt ? "Nenhum sensor no GPIO " : "No physical sensor detected on GPIO ") + String(gpio));
        return;
    }
    if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) {
        _cmdMgr->printError((pt ? "Sensor invalido no GPIO " : "Invalid physical sensor on GPIO ") + String(gpio));
        return;
    }
    String dbId; float dbOffset = 0.0f; String dbName;
    _storageMgr->getCalibrationData(foundRom, dbId, dbOffset, dbName);

    String currentId = String(cfg.sensors[gpio].hwId);

    cfg.sensors[gpio].active = true;
    cfg.sensors[gpio].gpio = gpio;
    memcpy(cfg.sensors[gpio].rom, foundRom, 8);

    if (dbId.length() > 0) safeCopy(cfg.sensors[gpio].hwId, dbId.c_str(), sizeof(cfg.sensors[gpio].hwId));
    else                   safeCopy(cfg.sensors[gpio].hwId, "LIB_SENS", sizeof(cfg.sensors[gpio].hwId));

    if (dbName.length() > 0) {
        safeCopy(cfg.sensors[gpio].friendlyName, dbName.c_str(), sizeof(cfg.sensors[gpio].friendlyName));
    } else {
        safeCopy(cfg.sensors[gpio].friendlyName, pt ? "Sensor Reconhecido" : "Recognized Sensor",
                 sizeof(cfg.sensors[gpio].friendlyName));
    }
    cfg.sensors[gpio].friendlyName[31] = '\0';

    if (currentId != String(cfg.sensors[gpio].hwId)) {
        cfg.sensors[gpio].provisionEpoch = _netMgr->getEpoch();
        _cmdMgr->printInfo(pt ? "Novo hardware detectado. Epoch atualizado."
                              : "New Hardware Context Detected. Epoch updated.");
    }

    /* F-LOCKOUT-STUCK: wrap save+reload no mesmo quiet mode (idem CMD_WRITE_MEMORY). */
    _displayMgr->requestQuietMode();
    _storageMgr->saveConfiguration();
    loadAndCalibrateSensors();
    _displayMgr->releaseQuietMode();
    _cmdMgr->printSuccess((pt ? "Sensor aceito e vinculado ao Slot " : "Sensor accepted and bound to Slot ") + String(gpio));
}

void AppManager::cmdHandleUserAdd(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();
    if (!isValidName(cmd.strVal1, 31)) {
        _cmdMgr->printError(pt ? "Username invalido (1-31, sem ctrl chars)"
                              : "Invalid username (1-31, no ctrl chars)");
        return;
    }
    if (cmd.strVal2[0] == '\0' || strlen(cmd.strVal2) > 64) {
        _cmdMgr->printError(pt ? "Senha ausente ou muito longa (1-64)"
                              : "Password missing or too long (1-64)");
        return;
    }
    if (!isValidCfgString(cmd.strVal2, 64)) {
        _cmdMgr->printError(pt ? "Senha tem chars de controle" : "Password has control chars");
        return;
    }
    bool exists = false;
    int freeSlot = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (cfg.users[i].active) {
            if (strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) { exists = true; break; }
        } else if (freeSlot < 0 && i >= 1) {  /* slot 0 = admin, protegido */
            freeSlot = i;
        }
    }
    if (exists) {
        _cmdMgr->printError(pt ? "Usuario ja existe" : "User already exists");
        return;
    }
    if (freeSlot < 0) {
        _cmdMgr->printError(pt ? "Sem slot livre (max usuarios)" : "No free slot (max users)");
        return;
    }
    safeCopy(cfg.users[freeSlot].username, cmd.strVal1, sizeof(cfg.users[freeSlot].username));
    {
        String preHash = _storageMgr->sha256Hex(String(cmd.strVal2));
        String hashed  = _storageMgr->hashPassword(String(cmd.strVal1), preHash);
        safeCopy(cfg.users[freeSlot].password, hashed.c_str(), sizeof(cfg.users[freeSlot].password));
    }
    cfg.users[freeSlot].active = true;
    cfg.users[freeSlot].permissions = (PERM_DASHBOARD | PERM_HISTORY);
    cfg.users[freeSlot].mustChangePassword = false;
    _cmdMgr->printSuccess(String(pt ? "Usuario criado: " : "User created: ") + cmd.strVal1);
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, freeSlot,
             String(TRL("CLI created user: ")) + cmd.strVal1);
    changed = true;
}

void AppManager::cmdHandleResetAdmin(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();
    if (!cmd.confirmed) {
        _cmdMgr->printInfo(pt ? "ATENCAO: reseta senha do admin p/ aleatoria."
                             : "WARN: resets admin password to random 8-char.");
        _cmdMgr->printInfo(pt ? "Use 'conf system admin reset confirm'."
                             : "Run 'conf system admin reset confirm'.");
        return;
    }
    /* SEC-003/F12.3 + SEC-007/009 (F15): random + salt + hashV1. */
    char newPlain[9];
    _storageMgr->generateInitialAdminPassword(newPlain, sizeof(newPlain));
    _storageMgr->generateSalt(cfg.users[0].salt);
    String preHash = _storageMgr->sha256Hex(String(newPlain));
    String hashed  = _storageMgr->hashPasswordV1("admin", preHash, cfg.users[0].salt);
    safeCopy(cfg.users[0].password, hashed.c_str(), sizeof(cfg.users[0].password));
    cfg.users[0].hashVersion = 1;
    cfg.users[0].mustChangePassword = true;
    _cmdMgr->printInfo(pt ? "Senha admin resetada. Nova senha (unica vez):"
                         : "Admin password reset. New password (shown once):");
    _cmdMgr->printInfo(String("  ") + newPlain);
    _cmdMgr->printInfo(pt ? "Trocar no 1o login via web (forcado)."
                         : "Change on 1st web login (forced).");
    /* Zera plaintext local após log. */
    volatile char* v = newPlain;
    for (size_t i = 0; i < sizeof(newPlain); i++) v[i] = 0;
    changed = true;
}

/* Parse "NNNN-NN-NN" → 3 inteiros. Substituiu sscanf("%4d-%2d-%2d", ...)
 * porque sscanf puxa __ssvfscanf_r/__ssvfiscanf_r (~12KB de flash).
 * Retorna true se 3 valores extraídos com sucesso. */
static bool parse_3ints(const char* s, char sep, int& a, int& b, int& c) {
    char* end;
    a = (int)strtol(s, &end, 10);
    if (end == s || *end != sep) return false;
    b = (int)strtol(end + 1, &end, 10);
    if (*end != sep) return false;
    c = (int)strtol(end + 1, &end, 10);
    return (end > s + 1) && (*end == '\0' || *end == ' ');
}

void AppManager::cmdHandleSetTime(const CliDemand& cmd) {
    const bool pt = _cmdMgr->isPt();
    int y, mo, d, h, mi, s;
    if (!parse_3ints(cmd.strVal1, '-', y, mo, d)
        || !parse_3ints(cmd.strVal2, ':', h, mi, s)) {
        _cmdMgr->printError(pt ? "Formato invalido. Use: conf time AAAA-MM-DD HH:MM:SS"
                              : "Invalid format. Use: conf time YYYY-MM-DD HH:MM:SS");
        return;
    }
    if (y < 2026 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31
        || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
        _cmdMgr->printError(pt ? "Valores fora de range (ano >= 2026)"
                              : "Values out of range (year >= 2026)");
        return;
    }
    struct tm tmLocal = {};
    tmLocal.tm_year = y - 1900;
    tmLocal.tm_mon  = mo - 1;
    tmLocal.tm_mday = d;
    tmLocal.tm_hour = h;
    tmLocal.tm_min  = mi;
    tmLocal.tm_sec  = s;
    time_t epoch = mktime(&tmLocal);
    if (epoch <= 1600000000) {
        _cmdMgr->printError(pt ? "Falha na conversao de tempo" : "Time conversion failed");
        return;
    }
    _netMgr->setManualTime(epoch);
    _cmdMgr->printSuccess(pt ? "Hora aplicada (imediato, nao persiste em reboot)"
                            : "Time applied (immediate; not persisted across reboot)");
}

void AppManager::cmdHandleIpCfg(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();
    switch (cmd.intVal1) {
        case 0:  /* dhcp */
            cfg.useDhcp = true;
            _cmdMgr->printSuccess(pt ? "Modo IP: DHCP" : "IP mode: DHCP");
            changed = true;
            break;
        case 1:  /* static */
            cfg.useDhcp = false;
            _cmdMgr->printSuccess(pt ? "Modo IP: estatico" : "IP mode: static");
            changed = true;
            break;
        case 2: case 3: case 4: case 5: {
            if (!isValidIpv4(cmd.strVal1)) {
                _cmdMgr->printError(pt ? "IPv4 invalido (ex: 192.168.1.100)"
                                      : "Invalid IPv4 (e.g. 192.168.1.100)");
                break;
            }
            char* dst = nullptr; size_t dstSize = 0;
            const char* label = "";
            if (cmd.intVal1 == 2)      { dst = cfg.staticIp;      dstSize = sizeof(cfg.staticIp);      label = "addr"; }
            else if (cmd.intVal1 == 3) { dst = cfg.staticMask;    dstSize = sizeof(cfg.staticMask);    label = "mask"; }
            else if (cmd.intVal1 == 4) { dst = cfg.staticGateway; dstSize = sizeof(cfg.staticGateway); label = "gateway"; }
            else                       { dst = cfg.staticDns;     dstSize = sizeof(cfg.staticDns);     label = "dns"; }
            safeCopy(dst, cmd.strVal1, dstSize);
            _cmdMgr->printSuccess((pt ? "IP " : "IP ") + String(label) + ": " + cmd.strVal1);
            changed = true;
            break;
        }
        default:
            _cmdMgr->printError(pt ? "Subcomando IP invalido" : "Invalid IP subcommand");
            break;
    }
}

void AppManager::cmdHandleDnsCfg(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();
    if (cmd.intVal1 == 0) {  /* auto */
        _storageMgr->setDnsAuto(true);
        _cmdMgr->printSuccess(pt ? "DNS: automatico (DHCP)" : "DNS: auto (DHCP)");
        changed = true;
        return;
    }
    /* manual */
    if (!isValidIpv4(cmd.strVal1)) {
        _cmdMgr->printError(pt ? "IPv4 invalido para DNS primario" : "Invalid IPv4 for primary DNS");
        return;
    }
    if (cmd.strVal2[0] != '\0' && !isValidIpv4(cmd.strVal2)) {
        _cmdMgr->printError(pt ? "IPv4 invalido para DNS secundario" : "Invalid IPv4 for secondary DNS");
        return;
    }
    _storageMgr->setDnsAuto(false);
    safeCopy(cfg.staticDns, cmd.strVal1, sizeof(cfg.staticDns));
    _storageMgr->setSecondaryDns(cmd.strVal2);
    if (cmd.strVal2[0] != '\0') {
        _cmdMgr->printSuccess(String(pt ? "DNS: manual; dns1=" : "DNS: manual; dns1=") + cmd.strVal1 + ", dns2=" + cmd.strVal2);
    } else {
        _cmdMgr->printSuccess(String(pt ? "DNS: manual; dns1=" : "DNS: manual; dns1=") + cmd.strVal1);
    }
    changed = true;
}

void AppManager::cmdHandleUserPass(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();
    if (cmd.strVal2[0] == '\0' || strlen(cmd.strVal2) > 64
        || !isValidCfgString(cmd.strVal2, 64)) {
        _cmdMgr->printError(pt ? "Nova senha invalida (1-64, sem ctrl chars)"
                              : "Invalid new password (1-64, no ctrl chars)");
        return;
    }
    bool found = false;
    for (int i = 0; i < MAX_USERS; i++) {
        if (cfg.users[i].active && strcasecmp(cmd.strVal1, cfg.users[i].username) == 0) {
            /* SEC-007/009 (F15): salt random + hashVersion=1. */
            _storageMgr->generateSalt(cfg.users[i].salt);
            String preHash = _storageMgr->sha256Hex(String(cmd.strVal2));
            String hashed  = _storageMgr->hashPasswordV1(String(cfg.users[i].username), preHash, cfg.users[i].salt);
            safeCopy(cfg.users[i].password, hashed.c_str(), sizeof(cfg.users[i].password));
            cfg.users[i].hashVersion = 1;
            cfg.users[i].mustChangePassword = false;
            _cmdMgr->printSuccess(String(pt ? "Senha atualizada: " : "Password updated: ") + cmd.strVal1);
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, i,
                     String(TRL("CLI reset password: ")) + cmd.strVal1);
            changed = true;
            found = true;
            break;
        }
    }
    if (!found) _cmdMgr->printError(pt ? "Usuario nao encontrado" : "User not found");
}

/* ===========================================================================
 * v3.37.8 — TEST-ONLY / RECOVERY
 * Zera `provisionEpoch` dos sensores pra recuperar visualização de histórico
 * pré factory reset. O gráfico filtra registros com `ts < provisionEpoch`
 * por padrão; zerando o campo, todos os registros nos arquivos binários de
 * /history voltam a ser exibidos.
 *
 * Restrição de segurança: aceita SOMENTE de sessão Bluetooth autenticada
 * (BluetoothManager valida exclusivamente cfg.users[0] = admin). Tentativas
 * via USB serial são rejeitadas com mensagem genérica de comando inválido —
 * mantém o comando "oculto" (não documentado em help).
 *
 * Uso após executar:
 *   write memory       (persiste em flash)
 *   reload             (reinicia para reaplicar)
 * =========================================================================== */
void AppManager::cmdHandleDbgSensorHistoryAll(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
    const bool pt = _cmdMgr->isPt();

    /* Gate: só BT autenticado. USB serial -> finge que comando não existe. */
    if (!_cmdMgr->wasLastInputFromBt()) {
        _cmdMgr->printError(pt ? "Comando desconhecido. Digite 'help'."
                              : "Unknown command. Type 'help'.");
        return;
    }

    if (cmd.intVal1 == -1) {
        /* Todos os slots ativos + ambient. */
        int n = 0;
        for (int i = 0; i < MAX_SENSORS; i++) {
            if (cfg.sensors[i].active) {
                cfg.sensors[i].provisionEpoch = 0;
                n++;
            }
        }
        cfg.ambientSensor.provisionEpoch = 0;
        changed = true;
        LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, n,
                 String(TRL("BT recovery: provisionEpoch zeroed for ")) + n + " slots+amb");
        _cmdMgr->printSuccess((pt ? "provisionEpoch zerado em " : "provisionEpoch zeroed for ") +
                              String(n) +
                              (pt ? " slots + ambient. Use 'write memory' e reload."
                                  : " slots + ambient. Run 'write memory' and reload."));
        return;
    }

    /* Slot específico. */
    if (!cmd.intVal1Valid || cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
        _cmdMgr->printError(pt ? "Slot fora de range (0-9) ou 'all'"
                              : "Slot out of range (0-9) or 'all'");
        return;
    }
    cfg.sensors[cmd.intVal1].provisionEpoch = 0;
    changed = true;
    LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, cmd.intVal1,
             String(TRL("BT recovery: provisionEpoch zeroed for slot ")) + cmd.intVal1);
    _cmdMgr->printSuccess((pt ? "provisionEpoch zerado no Slot " : "provisionEpoch zeroed for Slot ") +
                          String(cmd.intVal1) +
                          (pt ? ". Use 'write memory' e reload."
                              : ". Run 'write memory' and reload."));
}

