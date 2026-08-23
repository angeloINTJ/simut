/**
 * @file AppManager_CmdHandlers.cpp
 * @brief Command handler functions extracted from the longest cases
 * in AppManager::executeCommand (>=30 lines each).
 * Semantics are identical to the original — mechanical refactor for
 * readability (executeCommand reduced from 952 to ~600 lines).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "ParseFloat.h"
#include "CommandManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include <time.h>

#if SIMUT_CLI_FULL
void AppManager::cmdHandleSensorField(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
 const bool pt = _cmdMgr->isPt( );
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para GPIO" : "Invalid number for GPIO");
 return;
 }
 if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
 _cmdMgr->printError(pt ? "Slot fora de range (0-15)" : "Slot out of range (0-15)");
 return;
 }
 if (cmd.strVal2[0] == '\0') {
 _cmdMgr->printError(pt ? "Valor ausente" : "Missing value");
 return;
 }
 SensorRecord &r = cfg.sensors[cmd.intVal1];
 const char* field = cmd.strVal1;

 /* ── sensor <slot> pin <index> <gpio> — GPIO resource assignment ── */
 if (strcmp(field, "pin") == 0) {
  /* parseIntStrict, not sscanf. This was the last sscanf in the image, and it
   * pulled __ssvfscanf_r and its integer sibling in for one line: 7524 B,
   * measured by A/B build, all of it charged to pico_w_test because the whole
   * handler is inside #if SIMUT_CLI_FULL. The same trade is already recorded
   * further down this file for the date parser.
   * Behaviour is unchanged for anything the tokenizer can produce — strVal2
   * arrives as a single whitespace-free token — except that trailing junk
   * ("1,5x"), which sscanf accepted, now gets the usage line. */
  int pinIdx=-1,gpio=-1;
  String pinSpec(cmd.strVal2);
  int pinComma=pinSpec.indexOf(',');
  if(pinComma<0||!parseIntStrict(pinSpec.substring(0,pinComma),pinIdx)
     ||!parseIntStrict(pinSpec.substring(pinComma+1),gpio)
     ||pinIdx<0||gpio<0||gpio>15){
   _cmdMgr->printError(pt?"Uso: sensor <slot> pin <idx>,<gpio> (0-15)"
                         :"Usage: sensor <slot> pin <idx>,<gpio> (0-15)");
   return;
  }
  if(pinIdx>=MAX_SENSOR_PINS){
   _cmdMgr->printError((pt?"Maximo ":"Max ")+String(MAX_SENSOR_PINS)+(pt?" pinos":" pins"));
   return;
  }
  /* GPIO conflict check */
  for(int si=0;si<MAX_SENSORS;si++){
   if(si==cmd.intVal1||!cfg.sensors[si].active)continue;
   for(int pp=0;pp<MAX_SENSOR_PINS;pp++){
    if(cfg.sensors[si].pins[pp]==(uint8_t)gpio){
     _cmdMgr->printError((pt?"GPIO ":"GPIO ")+String(gpio)
       +(pt?" ja em uso pelo Slot ":" in use by Slot ")+String(si));
     return;
    }
   }
  }
  r.pins[pinIdx]=(uint8_t)gpio;

  /* Show role label for context */
  auto fmt = SensorFormat::forType((SensorType)r.sensorType);
  const char* roleLabel = "";
  if (pinIdx < fmt.pinCount) roleLabel = fmt.pins[pinIdx].label;
  _cmdMgr->printSuccess((pt?"Slot ":"Slot ")+String(cmd.intVal1)
    +" pin["+String(pinIdx)+"]=GPIO "+String(gpio)
    +String(" (")+roleLabel+")");

  /* Check if all required pins are now assigned */
  bool allAssigned = true;
  int missingCount = 0;
  for (int pp = 0; pp < fmt.pinCount && pp < MAX_SENSOR_PINS; pp++) {
   if (r.pins[pp] == PIN_UNUSED) { allAssigned = false; missingCount++; }
  }
  if (allAssigned) {
   _cmdMgr->printInfo((pt ? "Todos os pinos atribuidos. Proximo: sensor "
                           : "All pins assigned. Next: sensor ")
     + String(cmd.intVal1) + (pt ? " name \"<nome>\"" : " name \"<name>\""));
  } else if (missingCount > 0) {
   _cmdMgr->consolePrintf("  (%d %s)\n", missingCount,
     pt ? "pino(s) restante(s)" : "pin(s) remaining");
  }
  changed=true;
  return;
 }

 /* ── sensor <slot> create <type> — guided slot creation ── */
 if (strcmp(field, "create") == 0) {
  String v = cmd.strVal2; v.toLowerCase( );
  SensorType newType = TYPE_NONE;
  if (v == "ds18b20")           newType = TYPE_DS18B20;
  else if (v == "dht22")        newType = TYPE_DHT22;
  else if (v == "bme280")       newType = TYPE_BME280;
  else if (v == "bmp280")       newType = TYPE_BMP280;
  else {
   _cmdMgr->printError(pt ? "Tipo invalido. Use: ds18b20, dht22, bme280, bmp280"
                         : "Invalid type. Use: ds18b20, dht22, bme280, bmp280");
   return;
  }
  if (!sensorTypeEnabled(newType)) {
   _cmdMgr->printError(pt ? "Tipo de sensor nao compilado nesta firmware"
                         : "Sensor type not compiled in this firmware");
   return;
  }

  /* Warn if slot already configured */
  if (r.active) {
   _cmdMgr->printInfo((pt ? "Slot ja configurado — sobrescrevendo." : "Slot already configured — overwriting.")
     + String(" (") + sensorTypeName((SensorType)r.sensorType) + ")");
  }

  /* Reset slot: clear pins, set type, activate */
  for (int pp = 0; pp < MAX_SENSOR_PINS; pp++) r.pins[pp] = PIN_UNUSED;
  r.sensorType = (uint8_t)newType;
  r.active = true;
  /* Reset defaults */
  r.alarmsActive = false;
  /* Widest band the channel admits: a retyped slot must not carry the previous
   * chip's alarm limits, and "no limit" is safer than a guess. */
  for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
   r.chMin[c] = channelInfo(c).saneMin;
   r.chMax[c] = channelInfo(c).saneMax;
  }
  r.hwId[0] = '\0';
  r.friendlyName[0] = '\0';

  /* Show pin requirements */
  auto fmt = SensorFormat::forType(newType);
  _cmdMgr->printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1)
    + ": " + sensorTypeName(newType) + " — "
    + String(fmt.pinCount) + (pt ? " pino(s) necessarios:" : " pin(s) required:"));

  for (int pp = 0; pp < fmt.pinCount && pp < MAX_SENSOR_PINS; pp++) {
   String flagsStr = "";
   if (fmt.pins[pp].flags & FLAG_PULLUP) flagsStr += " pull-up";
   if (fmt.pins[pp].flags & FLAG_PULLDOWN) flagsStr += " pull-down";
   if (fmt.pins[pp].flags & FLAG_OPENDRAIN) flagsStr += " open-drain";
   if (flagsStr.length( ) > 0) flagsStr = " (" + flagsStr.substring(1) + ")";
   _cmdMgr->consolePrintf("  pin[%d] = %s%s\n",
     pp, fmt.pins[pp].label, flagsStr.c_str( ));
  }

  /* List free GPIOs */
  bool freeGpios[16];
  for (int g = 0; g < 16; g++) freeGpios[g] = true;
  for (int si = 0; si < MAX_SENSORS; si++) {
   if (si == cmd.intVal1 || !cfg.sensors[si].active) continue;
   auto sfmt = SensorFormat::forType((SensorType)cfg.sensors[si].sensorType);
   for (int pp = 0; pp < sfmt.pinCount && pp < MAX_SENSOR_PINS; pp++) {
    uint8_t g = cfg.sensors[si].pins[pp];
    if (g != PIN_UNUSED && g < 16) freeGpios[g] = false;
   }
  }
  String freeList = "";
  for (int g = 0; g < 16; g++) {
   if (freeGpios[g]) {
    if (freeList.length( ) > 0) freeList += ",";
    freeList += String(g);
   }
  }
  if (freeList.length( ) > 0) {
   _cmdMgr->consolePrintf("%s: %s\n",
     pt ? "GPIOs disponiveis" : "Available GPIOs",
     freeList.c_str( ));
  } else {
   _cmdMgr->printInfo(pt ? "(nenhum GPIO livre)" : "(no free GPIOs)");
  }

  _cmdMgr->printInfo(String(pt ? "Atribua com: sensor " : "Assign with: sensor ")
    + String(cmd.intVal1) + " pin <idx>,<gpio>");
  changed = true;
  return;
 }

 if (strcmp(field, "alarm") == 0) {
 String v = cmd.strVal2; v.toLowerCase( );
 if (v != "on" && v != "off") {
 _cmdMgr->printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
 return;
 }
 r.alarmsActive = (v == "on");
 /* Reativar o slot limpa o bit de desativação daquele sensor — o âmbar
 * de erro volta a reportar (só este slot; os demais não são tocados). */
 if (v == "on") _alarmDeactBits &= (uint16_t)~(1u << cmd.intVal1);
 _cmdMgr->printSuccess((pt ? "Alarme slot " : "Alarm slot ") + String(cmd.intVal1) + ": " + v);
 changed = true;
 return;
 }

 /* ── Non-numeric string fields: type, name, hwid, active ── */

 if (strcmp(field, "type") == 0) {
  String v = cmd.strVal2; v.toLowerCase( );
  SensorType newType = TYPE_NONE;
  if (v == "ds18b20")           newType = TYPE_DS18B20;
  else if (v == "dht22")        newType = TYPE_DHT22;
  else if (v == "bme280")       newType = TYPE_BME280;
  else if (v == "bmp280")       newType = TYPE_BMP280;
  else {
   _cmdMgr->printError(pt ? "Tipo invalido. Use: ds18b20, dht22, bme280, bmp280"
                         : "Invalid type. Use: ds18b20, dht22, bme280, bmp280");
   return;
  }
  if (!sensorTypeEnabled(newType)) {
   _cmdMgr->printError(pt ? "Tipo de sensor nao compilado nesta firmware"
                         : "Sensor type not compiled in this firmware");
   return;
  }
  r.sensorType = (uint8_t)newType;
  _cmdMgr->printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1) + " type=" + v);

  /* Show pin requirements for this type */
  auto fmt = SensorFormat::forType(newType);
  _cmdMgr->consolePrintf("  %s: %d pin%s\n",
    sensorTypeName(newType), fmt.pinCount, fmt.pinCount > 1 ? "s" : "");
  for (int pp = 0; pp < fmt.pinCount && pp < MAX_SENSOR_PINS; pp++) {
   uint8_t curGpio = r.pins[pp];
   String curStr = (curGpio != PIN_UNUSED && curGpio < 16)
     ? ("GPIO " + String(curGpio)) : String(pt ? "LIVRE" : "FREE");
   _cmdMgr->consolePrintf("  pin[%d] %-5s → %s\n",
     pp, fmt.pins[pp].label, curStr.c_str( ));
  }
  changed = true;
  return;
 }

 if (strcmp(field, "name") == 0) {
  if (!isValidName(cmd.strVal2, sizeof(r.friendlyName) - 1)) {
   _cmdMgr->printError(pt ? "Nome invalido (1-31 chars, sem ctrl chars)"
                         : "Invalid name (1-31 chars, no ctrl chars)");
   return;
  }
  safeCopy(r.friendlyName, cmd.strVal2, sizeof(r.friendlyName));
  _cmdMgr->printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1) + " name=" + cmd.strVal2);
  changed = true;
  return;
 }

 if (strcmp(field, "hwid") == 0) {
  if (!isValidCfgString(cmd.strVal2, sizeof(r.hwId) - 1)) {
   _cmdMgr->printError(pt ? "HW ID invalido (max 15, sem ctrl chars)"
                         : "Invalid HW ID (max 15, no ctrl chars)");
   return;
  }
  safeCopy(r.hwId, cmd.strVal2, sizeof(r.hwId));
  _cmdMgr->printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1) + " hwid=" + cmd.strVal2);
  changed = true;
  return;
 }

 if (strcmp(field, "active") == 0) {
  String v = cmd.strVal2; v.toLowerCase( );
  bool wantActive;
  if (v == "on" || v == "1" || v == "true")        wantActive = true;
  else if (v == "off" || v == "0" || v == "false") wantActive = false;
  else {
   _cmdMgr->printError(pt ? "Use 'on' ou 'off'" : "Use 'on' or 'off'");
   return;
  }

  /* Validate prerequisites when activating */
  if (wantActive) {
   if (r.sensorType == TYPE_NONE) {
    _cmdMgr->printError(pt ? "Defina o tipo primeiro: sensor <slot> type <tipo>"
                          : "Set type first: sensor <slot> type <type>");
    return;
   }
   if (!sensorTypeEnabled((SensorType)r.sensorType)) {
    _cmdMgr->printError(pt ? "Tipo de sensor nao compilado nesta firmware"
                          : "Sensor type not compiled in this firmware");
    return;
   }
   auto fmt = SensorFormat::forType((SensorType)r.sensorType);
   String missingPins = "";
   for (int pp = 0; pp < fmt.pinCount && pp < MAX_SENSOR_PINS; pp++) {
    if (r.pins[pp] == PIN_UNUSED) {
     if (missingPins.length( ) > 0) missingPins += ", ";
     missingPins += String(pp) + "(" + fmt.pins[pp].label + ")";
    }
   }
   if (missingPins.length( ) > 0) {
    _cmdMgr->printError((pt ? "Atribua os pinos restantes: " : "Assign remaining pins: ")
      + missingPins);
    _cmdMgr->printInfo((pt ? "Use: sensor " : "Use: sensor ")
      + String(cmd.intVal1) + " pin <idx>,<gpio>");
    return;
   }
  }

  r.active = wantActive;
  _cmdMgr->printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1)
    + " active=" + (r.active ? "on" : "off"));
  if (wantActive) {
   _cmdMgr->printInfo(pt ? "Pronto. Execute 'write memory' para salvar."
                         : "Ready. Run 'write memory' to persist.");
  }
  changed = true;
  return;
 }

 /* Numeric (float) with sensible range. */
 float val = parseFloat(cmd.strVal2);
 if (val == 0.0f && strcmp(cmd.strVal2, "0") != 0 && strcmp(cmd.strVal2, "0.0") != 0
 && strcmp(cmd.strVal2, "-0") != 0 && strcmp(cmd.strVal2, "-0.0") != 0) {
 _cmdMgr->printError(pt ? "Valor numerico invalido" : "Invalid numeric value");
 return;
 }
 /* `<key>min` / `<key>max`, key from the channel table — `tempmin`, `pressmax`.
  * The short `tmin`/`tmax`/`hmin`/`hmax` still work, because they are what the
  * help text and every existing script say. Both used to be the only form, and
  * a quantity with no two-letter name of its own could not be set at all. */
 size_t flen = strlen(field);
 int fch = -1; bool fIsMax = false;
 if (flen > 3 && (strcmp(field + flen - 3, "min") == 0 || strcmp(field + flen - 3, "max") == 0)) {
 char key[16];
 size_t klen = flen - 3;
 if (klen < sizeof(key)) {
 memcpy(key, field, klen); key[klen] = '\0';
 fch = channelByKey(key);
 fIsMax = (strcmp(field + flen - 3, "max") == 0);
 }
 }
 if (fch < 0) {
 if (strcmp(field, "tmin") == 0)      { fch = CH_TEMP; fIsMax = false; }
 else if (strcmp(field, "tmax") == 0) { fch = CH_TEMP; fIsMax = true;  }
 else if (strcmp(field, "hmin") == 0) { fch = CH_HUM;  fIsMax = false; }
 else if (strcmp(field, "hmax") == 0) { fch = CH_HUM;  fIsMax = true;  }
 }
 if (fch < 0) {
 _cmdMgr->printError(pt ? "Campo desconhecido" : "Unknown field");
 return;
 }
 {
 const ChannelInfo& ci = channelInfo((uint8_t)fch);
 if (val < ci.saneMin || val > ci.saneMax) {
 char msg[80];
 snprintf(msg, sizeof(msg), pt ? "%s fora de range (%.1f a %.1f)" : "%s out of range (%.1f to %.1f)",
          ci.name, ci.saneMin, ci.saneMax);
 _cmdMgr->printError(msg);
 return;
 }
 if (fIsMax) r.chMax[fch] = val; else r.chMin[fch] = val;
 }
 _cmdMgr->printSuccess((pt ? "Slot " : "Slot ") + String(cmd.intVal1) + " " + field + "=" + cmd.strVal2);
 changed = true;
}

void AppManager::cmdHandleAcceptSensor(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
 const bool pt = _cmdMgr->isPt( );
 (void)changed; /* save is done by saveConfiguration() below, not via flag. */
 if (!cmd.intVal1Valid) {
 _cmdMgr->printError(pt ? "Numero invalido para GPIO" : "Invalid number for GPIO");
 return;
 }
 if (cmd.intVal1 < 0 || cmd.intVal1 >= MAX_SENSORS) {
 _cmdMgr->printError(pt ? "Slot fora de range (0-15)" : "Slot out of range (0-15)");
 return;
 }
 uint8_t gpio = (uint8_t)cmd.intVal1;
 if (gpio >= MAX_SENSORS) return;

 uint8_t foundRom[8];
 String dbId; CalibCurve dbCurve; String dbName;
#if SIMUT_SENSOR_DS18B20
 if (!_sensorMgr->identifyPhysicalSensor(gpio, foundRom)) {
 _cmdMgr->printError((pt ? "Nenhum sensor no GPIO " : "No physical sensor detected on GPIO ") + String(gpio));
 return;
 }
 if (foundRom[0] == 0x00 || dallasCrc8(foundRom, 7) != foundRom[7]) {
 _cmdMgr->printError((pt ? "Sensor invalido no GPIO " : "Invalid physical sensor on GPIO ") + String(gpio));
 return;
 }
 _storageMgr->getCalibrationData(foundRom, dbId, dbCurve, dbName);
#endif

 String currentId = String(cfg.sensors[gpio].hwId);

 cfg.sensors[gpio].active = true;
 cfg.sensors[gpio].pins[0] = gpio;
#if SIMUT_SENSOR_DS18B20
 cfg.sensors[gpio].sensorType = TYPE_DS18B20;
 memcpy(cfg.sensors[gpio].rom, foundRom, 8);
#else
 memset(cfg.sensors[gpio].rom, 0, 8);
#endif

 if (dbId.length( ) > 0) safeCopy(cfg.sensors[gpio].hwId, dbId.c_str( ), sizeof(cfg.sensors[gpio].hwId));
 else safeCopy(cfg.sensors[gpio].hwId, "LIB_SENS", sizeof(cfg.sensors[gpio].hwId));

 if (dbName.length( ) > 0) {
 safeCopy(cfg.sensors[gpio].friendlyName, dbName.c_str( ), sizeof(cfg.sensors[gpio].friendlyName));
 } else {
 safeCopy(cfg.sensors[gpio].friendlyName, pt ? "Sensor Reconhecido" : "Recognized Sensor",
 sizeof(cfg.sensors[gpio].friendlyName));
 }
 cfg.sensors[gpio].friendlyName[31] = '\0';

 if (currentId != String(cfg.sensors[gpio].hwId)) {
 cfg.sensors[gpio].provisionEpoch = _netMgr->getEpoch( );
 _cmdMgr->printInfo(pt ? "Novo hardware detectado. Epoch atualizado."
 : "New Hardware Context Detected. Epoch updated.");
 }

 /* wrap save+reload no mesmo quiet mode (idem CMD_WRITE_MEMORY). */
 _displayMgr->requestQuietMode( );
 _storageMgr->saveConfiguration( );
 loadAndCalibrateSensors( );
 _displayMgr->releaseQuietMode( );
 _cmdMgr->printSuccess((pt ? "Sensor aceito e vinculado ao Slot " : "Sensor accepted and bound to Slot ") + String(gpio));
}

void AppManager::cmdHandleUserAdd(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
 const bool pt = _cmdMgr->isPt( );
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
 } else if (freeSlot < 0 && i >= 1) { /* slot 0 = admin, protegido */
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
 /* SEC-007/009 (F15): salt random + hashVersion=1 — mesmo esquema de
  * 'user pass' e 'admin reset'. Antes gravava hashPassword( ), que salga
  * com o username, enquanto verifyPasswordFor( ) verifica com
  * hashPasswordV1( ) e o salt do slot: os dois nunca batiam e o usuario
  * criado pela CLI jamais conseguia logar na web — justamente o que este
  * comando existe para fazer. E sem definir salt/hashVersion o slot ainda
  * herdava os bytes do usuario anterior. */
 _storageMgr->generateSalt(cfg.users[freeSlot].salt);
 String preHash = _storageMgr->sha256Hex(String(cmd.strVal2));
 String hashed = _storageMgr->hashPasswordV1(String(cmd.strVal1), preHash,
                                             cfg.users[freeSlot].salt);
 safeCopy(cfg.users[freeSlot].password, hashed.c_str( ), sizeof(cfg.users[freeSlot].password));
 cfg.users[freeSlot].hashVersion = 1;
 }
 cfg.users[freeSlot].active = true;
 cfg.users[freeSlot].permissions = (PERM_DASHBOARD | PERM_HISTORY | PERM_CALIB);
 cfg.users[freeSlot].mustChangePassword = false;
 _cmdMgr->printSuccess(String(pt ? "Usuario criado: " : "User created: ") + cmd.strVal1);
 LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, freeSlot,
 String(TRL("CLI created user: ")) + cmd.strVal1);
 changed = true;
}

#endif /* SIMUT_CLI_FULL */

void AppManager::cmdHandleResetAdmin(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
 const bool pt = _cmdMgr->isPt( );
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
 String hashed = _storageMgr->hashPasswordV1("admin", preHash, cfg.users[0].salt);
 safeCopy(cfg.users[0].password, hashed.c_str( ), sizeof(cfg.users[0].password));
 cfg.users[0].hashVersion = 1;
 cfg.users[0].mustChangePassword = true;
 _cmdMgr->printInfo(pt ? "Senha admin resetada. Nova senha (unica vez):"
 : "Admin password reset. New password (shown once):");
 _cmdMgr->printInfo(String(" ") + newPlain);
 _cmdMgr->printInfo(pt ? "Trocar no 1o login via web (forcado)."
 : "Change on 1st web login (forced).");
 /* Zero local plaintext after log. */
 volatile char* v = newPlain;
 for (size_t i = 0; i < sizeof(newPlain); i++) v[i] = 0;
 changed = true;
}

/* Parse "NNNN-NN-NN" → 3 inteiros. Substituiu sscanf("%4d-%2d-%2d", ...)
 * porque sscanf puxa __ssvfscanf_r/__ssvfiscanf_r (~12KB de flash).
 * Returns true if 3 values successfully extracted. */
static bool parse_3ints(const char* s, char sep, int& a, int& b, int& c) {
 char* end;
 a = (int)strtol(s, &end, 10);
 if (end == s || *end != sep) return false;
 b = (int)strtol(end + 1, &end, 10);
 if (*end != sep) return false;
 c = (int)strtol(end + 1, &end, 10);
 return (end > s + 1) && (*end == '\0' || *end == ' ');
}

#if SIMUT_CLI_FULL
void AppManager::cmdHandleSetTime(const CliDemand& cmd) {
 const bool pt = _cmdMgr->isPt( );
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
 tmLocal.tm_mon = mo - 1;
 tmLocal.tm_mday = d;
 tmLocal.tm_hour = h;
 tmLocal.tm_min = mi;
 tmLocal.tm_sec = s;
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
 const bool pt = _cmdMgr->isPt( );
 switch (cmd.intVal1) {
 case 0: /* dhcp */
 cfg.useDhcp = true;
 _cmdMgr->printSuccess(pt ? "Modo IP: DHCP" : "IP mode: DHCP");
 changed = true;
 break;
 case 1: /* static */
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
 if (cmd.intVal1 == 2) { dst = cfg.staticIp; dstSize = sizeof(cfg.staticIp); label = "addr"; }
 else if (cmd.intVal1 == 3) { dst = cfg.staticMask; dstSize = sizeof(cfg.staticMask); label = "mask"; }
 else if (cmd.intVal1 == 4) { dst = cfg.staticGateway; dstSize = sizeof(cfg.staticGateway); label = "gateway"; }
 else { dst = cfg.staticDns; dstSize = sizeof(cfg.staticDns); label = "dns"; }
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
 const bool pt = _cmdMgr->isPt( );
 if (cmd.intVal1 == 0) { /* auto */
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
 const bool pt = _cmdMgr->isPt( );
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
 String hashed = _storageMgr->hashPasswordV1(String(cfg.users[i].username), preHash, cfg.users[i].salt);
 safeCopy(cfg.users[i].password, hashed.c_str( ), sizeof(cfg.users[i].password));
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

/**
 * @brief `user perm <name> <role|0xMASK>` — set a web user's permission bits.
 *
 * The CLI could create web users but never grant them anything: `user add`
 * hardcodes dashboard+history+calibration, and nothing else could change it.
 * So any account beyond that had to be made through the web UI, which needs an
 * account that can already reach it — a loop that blocked automated testing of
 * every admin-gated route.
 *
 * Roles are shorthand for the masks in SystemDefs_Limits.h; a raw 0xHEX is
 * accepted for anything they do not cover. Reachable only from config mode,
 * which already allows `user add`, `user del` and `admin reset` — this widens
 * nothing that serial access did not already grant.
 */
void AppManager::cmdHandleUserPerm(const CliDemand& cmd, SystemConfig& cfg, bool& changed) {
 const bool pt = _cmdMgr->isPt( );

 uint16_t mask = 0;
 String role(cmd.strVal2);
 role.toLowerCase( );

 if (role == "admin" || role == "full") {
  mask = PERM_FULL_ADMIN;
 } else if (role == "viewer" || role == "leitor") {
  mask = PERM_DASHBOARD | PERM_HISTORY;
 } else if (role == "operator" || role == "operador") {
  mask = PERM_DASHBOARD | PERM_HISTORY | PERM_LOGS | PERM_CALIB;
 } else if (role == "none" || role == "nenhum") {
  mask = 0;
 } else if (role.startsWith("0x")) {
  char* endp = nullptr;
  unsigned long v = strtoul(role.c_str( ) + 2, &endp, 16);
  if (endp == role.c_str( ) + 2 || *endp != '\0' || v > 0xFFFFUL) {
   _cmdMgr->printError(pt ? "Mascara invalida (use 0x0000..0xFFFF)"
                          : "Invalid mask (use 0x0000..0xFFFF)");
   return;
  }
  mask = (uint16_t)v;
 } else {
  _cmdMgr->printError(pt ? "Papel invalido. Use admin|operator|viewer|none ou 0xMASCARA"
                         : "Invalid role. Use admin|operator|viewer|none or 0xMASK");
  return;
 }

 for (int i = 0; i < MAX_USERS; i++) {
  if (!cfg.users[i].active || strcasecmp(cmd.strVal1, cfg.users[i].username) != 0) continue;
  cfg.users[i].permissions = mask;
  _cmdMgr->consolePrintf(pt ? "OK: Permissoes de %s = 0x%04X\n"
                            : "OK: Permissions for %s = 0x%04X\n",
                         cfg.users[i].username, (unsigned)mask);
  LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, i,
           String(TRL("CLI set permissions: ")) + cmd.strVal1);
  changed = true;
  return;
 }
 _cmdMgr->printError(pt ? "Usuario nao encontrado" : "User not found");
}

/* cmdHandleDbgSensorHistoryAll removido (debug TEST-ONLY
 * de v3.24.12 — recovery de provisionEpoch via BT). Liberou ~1-2 KB.
 * Recovery alternativo: editar system.bin via /api/restore?op=apply com .bkp. */

#endif /* SIMUT_CLI_FULL */
