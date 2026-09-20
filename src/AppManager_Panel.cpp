/**
 * @file AppManager_Panel.cpp
 * @brief v24 — what Core 0 does with an identified action from the panel.
 *
 * @details Sub-file of AppManager_Events.cpp. The panel (Core 1) never writes
 * the configuration: it identifies a user by PIN (EVT_AUTH_PIN) and then
 * sends actions. Everything that has consequences happens here, in this
 * order, for every action: the session's bit is checked, the config is
 * changed and saved, the log gets a line whose context names the user and
 * the slot, the alarm line gets a record that names the user, and the panel
 * is told what happened (showPanelMessage / a repaint of the screen it is
 * on). A refusal is logged too — a bit that was not there is a fact worth
 * keeping.
 *
 * Log context convention (SystemDefs_Logging.h): user * 100 + slot for the
 * slot-bound actions, the user slot alone for the account-bound ones. The
 * binary record has no room for a name; the serial line prints it.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "SensorManager.h"
#include "SoundManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "TelemetryManager.h"      /* pushAlarm + AlarmErrCode (AlarmQueue.h) */
#include "AlarmQueue.h"            /* alarmActorFromSlot */
#include "SystemDefs_Validate.h"   /* isValidName, isValidPanelPin */
#include "sensors/SensorChannelTable.h" /* sensorHasChannel */
#include <time.h>

#if !SIMUT_DISPLAY_TFT
/* No panel, no keypad: the images without a TFT (alpha, Air) keep the three
 * entry points the event loop links against and nothing else — measured at
 * ~5 kB of flash on the Air, which lives 1 kB from its budget. */
bool AppManager::handlePanelEvent(const UiEvent& ev) { (void)ev; return false; }
void AppManager::panelSaveAlarmLimits(int slot) { (void)slot; }
void AppManager::panelSetUserPin(int slot, const char* pin, UiMode returnTo) { (void)slot; (void)pin; (void)returnTo; }
bool AppManager::panelAllowed(uint16_t bit, int8_t slot) { (void)bit; (void)slot; return false; }
#else

static inline int panelCtx(int user, int slot) {
 return (user < 0 ? 0 : user) * 100 + (slot < 0 ? 0 : slot);
}

static void wipe(char* s, size_t n) {
 volatile char* v = s;
 for (size_t i = 0; i < n; i++) v[i] = 0;
}

/* Every action passes through here first. A missing bit is a refusal the
 * operator hears (error tone) and the log keeps, with who tried what. */
bool AppManager::panelAllowed(uint16_t bit, int8_t slot) {
 if (_panelUser >= 0 && (_panelPerms & bit)) return true;
 LOG_CODE(LOG_WARN, "APP", APP_UI_PERM_DENIED, panelCtx(_panelUser, slot), "");
 _soundMgr->play(SND_ERROR);
 return false;
}

const char* AppManager::panelUserName( ) const {
 if (_panelUser < 0 || _panelUser >= MAX_USERS) return "";
 return _storageMgr->getConfig( ).users[_panelUser].username;
}

/* The one place a PIN is compared. Success opens the session with that
 * account's bits and continues to whatever the keypad was opened for. */
void AppManager::panelIdentify( ) {
 SystemConfig &cfg = _storageMgr->getConfig( );

 /* The keypad hands over, per tap, the three glyphs that were on the card and
  * never which of them was meant. The deal is rolled after every tap, so the
  * three change each time. Core 0 walks every string those taps can spell —
  * the chained digest makes the shared prefixes cheap — and the account one of
  * them belongs to is who is standing at the panel.
  *
  * Nothing in this path ever holds the PIN: the candidate that matched is not
  * kept, and there is nothing to wipe. */
 char taps[PIN_MAX_LEN][PinKb::SLOTS + 1];
 const uint8_t n = _displayMgr->getEnteredPinTaps(taps, PIN_MAX_LEN);
 bool ambiguous = false;
 const uint32_t t0 = millis( );
 const int u = _storageMgr->findUserByPinSet(taps, n, &ambiguous);
 const uint32_t took = millis( ) - t0;

 if (u < 0) {
 const int fails = _displayMgr->authResult(false);
 /* Two accounts whose PINs both fit the same taps: the panel cannot ask
  * which, so neither gets in. Its own code, because it is not a wrong PIN
  * and the operator who reads the log has to tell them apart — on a full
  * account table with four-digit PINs it is the failure that actually
  * happens, and the answer to it is longer PINs, not a retry. */
 if (ambiguous) LOG_CODE(LOG_WARN, "SEC", SEC_PIN_AMBIGUOUS, fails,
                         TRL("Two accounts match the same keypad entry."));
 /* The sixth failure is the permanent lockout (DisplayManager::authResult);
  * it gets its own code so a burst of guesses reads as one event. */
 else LOG_CODE(LOG_WARN, "SEC", (fails >= 6) ? SEC_PIN_LOCKOUT : SEC_PIN_FAIL, fails, "");
 return;
 }
 /* ctx is the account; the search time goes in the text, because it is the
  * one number that grows with the PIN length and nobody would guess it. */
 /* WARN, not INFO: LogPolicy never filters a WARN, and this is the line that
  * says WHO is at the panel. At INFO the family latch dropped it after the
  * first of a run of logins — measured on the rig with a full account table
  * on 2026-09-19, where 3 of 25 identifications left no trace at all. The
  * web's own "config changed" is WARN for the same reason. */
 LOG_CODE(LOG_WARN, "SEC", SEC_PIN_OK, u, String((unsigned)took) + " ms");

 _panelUser = (int8_t)u;
 _panelPerms = cfg.users[u].permissions;
 _displayMgr->setPanelSession(_panelUser, _panelPerms);
 _displayMgr->authResult(true);
 _soundMgr->play(SND_CONFIRM);

 if (_panelAuthFor == PAUTH_DEACTIVATE) {
 _panelAuthFor = PAUTH_SETTINGS;
 panelDeactivateAlarm( );
 return;
 }
 if (u == 0 && _storageMgr->mustChangePin( )) {
 /* The admin still holds the factory "1234": the first thing it does is
  * choose another, exactly as the device PIN always demanded. */
 _displayMgr->showPinEntry(DisplayManager::PIN_FOR_OWN);
 LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, 0,
 TRL("Default PIN detected; forcing change."));
 return;
 }
 _displayMgr->showSettingsMain( );
}

/* "Deactivate" on the alarm pop-up, after the PIN: the same per-slot,
 * per-domain switch-off it always was, now behind PERM_ALARM_BLOCK and
 * signed on the alarm line. */
void AppManager::panelDeactivateAlarm( ) {
 const int8_t slot = _alarmDeactivateSlot;
 _pendingAlarmDeactivate = false;
 _alarmDeactivateSlot = -1;
 if (!panelAllowed(PERM_ALARM_BLOCK, slot)) {
 _displayMgr->showPanelMessage(false, TR_NO_PERMISSION, MODE_DASHBOARD);
 return;
 }
 SystemConfig &cfg = _storageMgr->getConfig( );
 /* Desativação POR SLOT E POR DOMÍNIO: o domínio ATIVO do slot é que é
  * desligado — em ERRO, muta só o erro (o LIMITE permanece armado); em
  * LIMITE, desliga só o limite (o erro continua reportando). */
 if (slot >= 0 && slot < MAX_SENSORS) {
 const bool errNow = _displayMgr->isSlotErrAlarming(slot);
 if (errNow) {
 _displayMgr->setAlarmErrMuted(slot, true);
 LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_DEACTIVATED, panelCtx(_panelUser, slot),
 "erro mutado (limite permanece)");
 } else {
 cfg.sensors[slot].alarmsActive = false;
 _storageMgr->saveConfiguration( );
 LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_BLOCKED, panelCtx(_panelUser, slot), panelUserName( ));
 }
 }
 _soundMgr->stopAlarm( );
 _displayMgr->setAlarmState(0, -1);
 _displayMgr->setAlarmErrState(0);
 _displayMgr->setAlarmSilenced(false, 0);
 pushAlarmAction(slot, ALARM_ERR_ERR_OFF, ALARM_ERR_ALARM_OFF, alarmActorFromSlot(_panelUser));
 _displayMgr->forceDashboard( );
}

/* The limit editor's SAVE. Core 1 kept its working copy; the diff against
 * the config says which channels changed, and each one becomes an alarm_lim
 * record with both limits and the user. Nothing else in the record moves:
 * enable/disable is EVT_ALARM_BLOCK's, with its own bit. */
void AppManager::panelSaveAlarmLimits(int slot) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 if (slot < 0 || slot >= MAX_SENSORS || !cfg.sensors[slot].active) return;
 if (!panelAllowed(PERM_ALARM_LIMITS, (int8_t)slot)) {
 _displayMgr->showAlarmSensorMenu(slot);
 return;
 }
 const SensorRecord& edited = _displayMgr->editedAlarmRecord( );
 SensorRecord& rec = cfg.sensors[slot];
 const uint8_t actor = alarmActorFromSlot(_panelUser);
 bool changed = false;
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
 if (!sensorHasChannel((SensorType)rec.sensorType, c)) continue;
 if (rec.chMin[c] == edited.chMin[c] && rec.chMax[c] == edited.chMax[c]) continue;
 rec.chMin[c] = edited.chMin[c];
 rec.chMax[c] = edited.chMax[c];
 _telemetryMgr->pushAlarm((uint8_t)slot, c, rec.chMin[c], ALARM_ERR_ALARM_LIM, actor, rec.chMax[c]);
 changed = true;
 }
 if (changed) {
 _storageMgr->saveConfiguration( );
 _sensorMgr->syncAlarmLimits(cfg);
 checkAlarmConditions( );
 /* WARN for the same reason as SEC_PIN_OK: this is the record that says who
  * moved a limit, and a run of edits must not latch it away. Its siblings —
  * block, unblock, maintenance on and off — have always been WARN. */
 LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_SAVED, panelCtx(_panelUser, slot), panelUserName( ));
 }
 _soundMgr->play(SND_CONFIRM);
 _displayMgr->showAlarmSensorMenu(slot);
}

/* A new PIN for `slot`: one's own (no bit needed — it is the session's) or,
 * with PERM_USER_MGR, anyone's. PINs must be unique because the keypad
 * identifies BY them; setUserPin refuses a duplicate and names the owner. */
void AppManager::panelSetUserPin(int slot, const char* pin, UiMode returnTo) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 const bool own = (slot == _panelUser);
 if (!own && !panelAllowed(PERM_USER_MGR, -1)) {
 _displayMgr->showPanelMessage(false, TR_NO_PERMISSION, returnTo);
 return;
 }
 if (slot < 0 || slot >= MAX_USERS || !cfg.users[slot].active || !isValidPanelPin(pin)) {
 _displayMgr->showPanelMessage(false, TR_INVALID_PIN, returnTo);
 return;
 }
 int conflict = -1;
 if (!_storageMgr->setUserPin(slot, pin, &conflict)) {
 _soundMgr->play(SND_ERROR);
 _displayMgr->showPanelMessage(false, TR_PIN_IN_USE, returnTo);
 return;
 }
 /* The factory flag clears only when the admin picks something other than
  * "1234" — setting it again changes nothing, and the flag says so. */
 if (slot == 0 && strcmp(pin, "1234") != 0) _storageMgr->clearMustChangePin( );
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 if (own) LOG_CODE(LOG_INFO, "APP", APP_UI_PIN_CHANGED, slot, cfg.users[slot].username);
 else     LOG_CODE(LOG_WARN, "APP", APP_UI_USER_PIN_SET, slot, cfg.users[slot].username);
 _displayMgr->showPanelMessage(true, TR_PIN_SAVED, returnTo);
}

/* The v24 events. true = it was one of ours. */
bool AppManager::handlePanelEvent(const UiEvent& ev) {
 SystemConfig &cfg = _storageMgr->getConfig( );
 switch (ev.type) {

 case UiEvent::EVT_AUTH_PIN:
 panelIdentify( );
 return true;

 case UiEvent::EVT_ALARM_BLOCK: {
 const int slot = ev.id;
 const bool enable = (ev.param == 1);
 if (slot < 0 || slot >= MAX_SENSORS || !cfg.sensors[slot].active) return true;
 if (!panelAllowed(PERM_ALARM_BLOCK, (int8_t)slot)) return true;
 cfg.sensors[slot].alarmsActive = enable;
 /* re-enabling clears the error mute of the same slot — the web does the
  * same; error and limit are independent but a fresh start is one gesture */
 if (enable) _displayMgr->setAlarmErrMuted((int8_t)slot, false);
 _storageMgr->saveConfiguration( );
 _sensorMgr->syncAlarmLimits(cfg);
 checkAlarmConditions( );
 uint8_t firstCh = CH_TEMP;
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
 if (sensorHasChannel((SensorType)cfg.sensors[slot].sensorType, c)) { firstCh = c; break; }
 }
 _telemetryMgr->pushAlarm((uint8_t)slot, firstCh, NAN,
 enable ? ALARM_ERR_ALARM_ON : ALARM_ERR_ALARM_OFF,
 alarmActorFromSlot(_panelUser));
 LOG_CODE(LOG_WARN, "APP", enable ? APP_UI_ALARM_UNBLOCKED : APP_UI_ALARM_BLOCKED,
 panelCtx(_panelUser, slot), panelUserName( ));
 _soundMgr->play(SND_CONFIRM);
 _displayMgr->showAlarmSensorMenu(slot);
 return true;
 }

 case UiEvent::EVT_MAINT_SET: {
 const int slot = ev.id;
 if (slot < 0 || slot >= MAX_SENSORS || !cfg.sensors[slot].active) return true;
 if (!panelAllowed(PERM_MAINT, (int8_t)slot)) return true;
 uint32_t secs = (ev.param < 0) ? 0u : (uint32_t)ev.param;
 if (secs > MAINT_MAX_SEC) secs = MAINT_MAX_SEC;
 const uint32_t now = (uint32_t)time(nullptr);
 cfg.maint.until[slot] = secs ? now + secs : 0;
 /* the edge — and the maint_on / maint_off record — is
  * handleAlarmTelemetryEdges'; this names who caused it, and runs the
  * edge now so the server hears within this loop, not on the next pass */
 _telemetryMgr->noteMaintActor((uint8_t)slot, alarmActorFromSlot(_panelUser));
 _storageMgr->saveConfiguration( );
 LOG_CODE(LOG_WARN, "APP", secs ? APP_UI_MAINT_ON : APP_UI_MAINT_OFF,
 panelCtx(_panelUser, slot), panelUserName( ));
 handleAlarmTelemetryEdges( );
 checkAlarmConditions( );
 _soundMgr->play(SND_CONFIRM);
 _displayMgr->showAlarmSensorMenu(slot);
 return true;
 }

 case UiEvent::EVT_USER_ADD: {
 if (!panelAllowed(PERM_USER_MGR, -1)) {
 _displayMgr->clearEnteredPin( );
 _displayMgr->showPanelMessage(false, TR_NO_PERMISSION, MODE_SETTINGS_USERS);
 return true;
 }
 char name[16];
 _displayMgr->getNewName(name, sizeof(name));
 /* trailing spaces are the keyboard's, not the name's */
 for (int n = (int)strlen(name) - 1; n >= 0 && name[n] == ' '; n--) name[n] = '\0';
 bool nameOk = isValidName(name, 15) && strcasecmp(name, "admin") != 0;
 for (int i = 0; nameOk && i < MAX_USERS; i++) {
 if (cfg.users[i].active && strcasecmp(name, cfg.users[i].username) == 0) nameOk = false;
 }
 if (!nameOk) {
 _displayMgr->clearEnteredPin( );
 _displayMgr->showPanelMessage(false, TR_NAME_INVALID, MODE_SETTINGS_USERS);
 return true;
 }
 int slot = -1;
 for (int i = 1; i < MAX_USERS; i++) { if (!cfg.users[i].active) { slot = i; break; } }
 if (slot < 0) {
 _displayMgr->clearEnteredPin( );
 _displayMgr->showPanelMessage(false, TR_USERS_FULL, MODE_SETTINGS_USERS);
 return true;
 }
 char pin[PIN_MAX_LEN + 1];
 _displayMgr->getEnteredPin(pin, sizeof(pin));
 _displayMgr->clearEnteredPin( );

 UserAccount& u = cfg.users[slot];
 memset(&u, 0, sizeof(u));
 safeCopy(u.username, name, sizeof(u.username));
 /* the panel grants the three panel bits and nothing else: this account
  * has no web page until an administrator gives it one */
 u.permissions = (uint16_t)(ev.param & PERM_PANEL_ALARM_ANY);
 /* A random web password it will never be told — the account is panel-only
  * until an admin resets the password from the web or the CLI. Hashed the
  * ordinary way so that reset needs nothing special. */
 char temp[9];
 _storageMgr->generateInitialAdminPassword(temp, sizeof(temp));
 _storageMgr->generateSalt(u.salt);
 String hashed = _storageMgr->hashPasswordV1(String(u.username),
 _storageMgr->sha256Hex(String(temp)), u.salt);
 wipe(temp, sizeof(temp));
 safeCopy(u.password, hashed.c_str( ), sizeof(u.password));
 u.hashVersion = 1;
 u.mustChangePassword = true;
 u.active = true;

 int conflict = -1;
 const bool pinOk = _storageMgr->setUserPin(slot, pin, &conflict);
 wipe(pin, sizeof(pin));
 if (!pinOk) {
 memset(&u, 0, sizeof(u)); /* the slot goes back to free, whole */
 _soundMgr->play(SND_ERROR);
 _displayMgr->showPanelMessage(false, TR_PIN_IN_USE, MODE_SETTINGS_USER_EDIT);
 return true;
 }
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_WARN, "APP", APP_UI_USER_ADDED, slot,
 String(panelUserName( )) + " -> " + u.username);
 _displayMgr->showPanelMessage(true, TR_USER_SAVED, MODE_SETTINGS_USERS);
 return true;
 }

 case UiEvent::EVT_USER_DEL: {
 const int slot = ev.id;
 if (!panelAllowed(PERM_USER_MGR, -1)) {
 _displayMgr->showPanelMessage(false, TR_NO_PERMISSION, MODE_SETTINGS_USERS);
 return true;
 }
 /* slot 0 is the admin, and one does not delete the account one is
  * logged in with: the session would outlive its owner */
 if (slot <= 0 || slot >= MAX_USERS || !cfg.users[slot].active || slot == _panelUser) {
 _soundMgr->play(SND_ERROR);
 _displayMgr->showPanelMessage(false, TR_NAME_INVALID, MODE_SETTINGS_USERS);
 return true;
 }
 String gone = cfg.users[slot].username;
 memset(&cfg.users[slot], 0, sizeof(cfg.users[slot]));
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_WARN, "APP", APP_UI_USER_DELETED, slot, String(panelUserName( )) + " -> " + gone);
 _displayMgr->showPanelMessage(true, TR_USER_DELETED, MODE_SETTINGS_USERS);
 return true;
 }

 case UiEvent::EVT_USER_PERMS: {
 const int slot = ev.id;
 if (!panelAllowed(PERM_USER_MGR, -1)) {
 _displayMgr->showPanelMessage(false, TR_NO_PERMISSION, MODE_SETTINGS_USERS);
 return true;
 }
 if (slot <= 0 || slot >= MAX_USERS || !cfg.users[slot].active) {
 _displayMgr->showPanelMessage(false, TR_NAME_INVALID, MODE_SETTINGS_USERS);
 return true;
 }
 /* only the three panel bits move; whatever the web granted stays */
 const uint16_t keep = (uint16_t)(cfg.users[slot].permissions & ~PERM_PANEL_ALARM_ANY);
 cfg.users[slot].permissions = (uint16_t)(keep | (ev.param & PERM_PANEL_ALARM_ANY));
 _storageMgr->saveConfiguration( );
 _soundMgr->play(SND_CONFIRM);
 LOG_CODE(LOG_WARN, "APP", APP_UI_USER_PERMS, slot, cfg.users[slot].username);
 _displayMgr->showPanelMessage(true, TR_USER_SAVED, MODE_SETTINGS_USERS);
 return true;
 }

 case UiEvent::EVT_USER_PIN: {
 char pin[PIN_MAX_LEN + 1];
 _displayMgr->getEnteredPin(pin, sizeof(pin));
 _displayMgr->clearEnteredPin( );
 const bool own = (ev.id == _panelUser);
 panelSetUserPin(ev.id, pin, own ? MODE_SETTINGS_MAIN : MODE_SETTINGS_USER_EDIT);
 wipe(pin, sizeof(pin));
 return true;
 }

 default:
 return false;
 }
}
#endif /* SIMUT_DISPLAY_TFT */
