/**
 * @file DisplayManager_SettingsCore.cpp
 * @brief The settings screens' entry points and working state: the core share of DisplayManager_Settings.cpp, moved word for word in S10a, plus showApConfirm( ), which S10c D2 moved here (docs/analysis/UI_DRIVERS.md section 2.18).
 */

#include "DisplayManager.h"
#include "simut_config.h"   /* SIMUT_NET_LAN and SIMUT_NET_WIFI are tested below: included here, not left to another header */
#include "SensorManager.h"
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "LogManager.h"
#include "StorageManager.h" /* getBoardSerialNumber in System Status */
#include "UiWidgets.h"
#include "PasswordKeyboard.h"
#include "ui/UiDriverHooks.h"   /* repairAlarmLimits( ) and clampEditFocus( ): the core's ticks, with the hook definitions they lean on; last, they need the complete class */

void DisplayManager::showSettingsThemes(int currentThemeIdx) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_THEMES;
 _previewThemeIdx = currentThemeIdx;
 _themePage = currentThemeIdx / 4;
 _forceSettingsRedraw = true; _lastThemePage = -1; _lastPreviewThemeIdx = -1; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::refreshAlarmStatus( ) {
 mutex_enter_blocking(&_stateMutex);
 _alarmStatusDirty = true; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::showSettingsAlarms(SystemConfig* cfg) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_ALARMS; _sysConfigPtr = cfg; _activeSensorCount = 0;
 for(int i = 0; i < MAX_SENSORS; i++) { if(cfg->sensors[i].active) { _activeSensorsMap[_activeSensorCount++] = i; } }
 _alarmSelection = 0; _alarmPage = 0; _lastAlarmSelection = -1; _forceSettingsRedraw = true; _lastAlarmPage = -1; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::showAlarmEdit(int sensorIdx) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_ALARM_EDIT; _editSensorIdx = sensorIdx;
 _tempAlarmConfig = _sysConfigPtr->sensors[sensorIdx];
 _editFieldFocus = 0; _lastEditPage = -1;
 _forceSettingsRedraw = true; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::showSettingsMain( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_MAIN; _menuSelection = 0; _mainMenuPage = 0; _lastMainMenuPage = -1;
 /* v24: the menu lists what this session's bits can do. Which items, in
  * what order, and who sees each one: display/SettingsMenu.h. */
 _menuCount = settingsMenuBuild(_panelPerms, _menuItems);
 _forceSettingsRedraw = true; _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::showSettingsPassword(uint8_t purpose) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_PASSWORD;
 _kbPurpose = purpose;
 _kbCursor = 0;
 _kbShowRaw = false;
 _kbPhase = 0;
 _kbMsgKey = TR_KEYS_COUNT;
 _kbPopup = PwdKb::POPUP_NONE;
 memset(_kbBuffer, 0, sizeof(_kbBuffer));
 memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::getNewPassword(char* out, size_t maxLen) const {
 strncpy(out, _kbBuffer, maxLen - 1);
 out[maxLen - 1] = '\0';
}

void DisplayManager::setTelemetryPending(uint16_t count) {
 _sharedState.pendingPkts = count;
}

/**
 * @brief Reports the result of the last telemetry send.
 *
 * Success: starts flash animation (blue/white for 1s), then
 * stabilizes to fixed blue.
 * Failure: fixed red immediately.
 */
void DisplayManager::setTelemetrySendStatus(bool success) {
 if (success) {
 /* Publish auxiliary vars BEFORE the state (the flag that
 * triggers the flash branch in Core 1 render). */
 _pktArrowFlashOn = false;
 _pktArrowFlashTime = millis( );
 _pktArrowFlashEnd = millis( ) + 1000;
 __dmb( );
 _pktArrowState = 3; /* send flash */
 } else {
 _pktArrowState = 2; /* fixed red */
 }
}

void DisplayManager::showSettingsSounds(const SoundSettingsState& state) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_SOUNDS;
 _soundSettings = state;
 _soundSelection = 0;
 _inMelodySelect = false;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

/* The date and time (2026-10-01). A unit with no network configured asks for
 * them when it boots — the maintainer's decision, replacing the setup AP that
 * opened by itself there: with no network there is no NTP, and records stamped
 * by the provisional clock are not to be trusted. The Settings menu opens
 * the same screen at any time. The calendar arithmetic is
 * display/ClockEntry.h, tested on the host; DisplayManager_Settings.cpp draws it.
 *
 * Strings: the title is TR_MENU_CLOCK, the menu row's own string, stripped of
 * the number a pack installed before 2026-10-02 still bakes in. The hint is
 * hardcoded EN/PT, the rule DisplayManager_Settings.cpp follows for confirmations.
 *
 * Nobody answering is the 30 s idle guard in handleTouch( ): back to the
 * dashboard, which is what SKIP does. A unit back from a power cut with nobody
 * in front of it shows its readings, not a question. The guard counts from
 * _lastTouchTime, which is why it is restarted here: at boot it is the time of
 * the last touch, or zero, and the prompt would last one sample. */
void DisplayManager::showClockEntry(const ClockEntry& start, bool atBoot) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SET_CLOCK;
 _clockEntry = start;
 _clockAtBoot = atBoot;
 _clockValuesDirty = false;
 _lastTouchTime = millis( );
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::showMuteConfirm( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_CONFIRM_MUTE_ALL;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
 /* Fire SND_ATTENTION when entering the confirmation screen. */
 requestPreviewSound(SND_ATTENTION, _soundSettings.attentionMelody);
}

#if SIMUT_NET_WIFI   /* a wired build has no setup AP to ask about */
/* Starting the setup AP takes the device off the LAN for up to 15 minutes, so
 * it asks first. The original's screen (drawApConfirm( ) in DisplayManager_Settings.cpp)
 * is the two-button one the Global Mute confirmation uses, with hardcoded EN/PT message and Confirm strings
 * (the rule of the original's confirmations: the TFT renders ASCII only); since S10c D7
 * the Ângulo draws its own VmDialogo from UA_TXT( ) texts (ui/angulo/DisplayManager_UiA.cpp). */
void DisplayManager::showApConfirm( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_CONFIRM_AP;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}
#endif /* SIMUT_NET_WIFI */

bool DisplayManager::consumeTouchSound( ) {
 if (_touchSoundPending) {
 _touchSoundPending = false;
 return true;
 }
 return false;
}

bool DisplayManager::consumeErrorSound( ) {
 if (_errorSoundPending) {
 _errorSoundPending = false;
 return true;
 }
 return false;
}

/* Cross-core producers publish data BEFORE the flag with __dmb().
 * Without the barrier, Core 0 may see the flag true before the data
 * fields are visible (reordering visible on RP2040 across cores). */
void DisplayManager::requestPreviewSound(SoundEvent ev, uint8_t melIdx) {
 _previewType = (uint8_t)ev;
 _previewMelIdx = melIdx;
 __dmb( );
 _previewPending = true;
}

void DisplayManager::requestVolumePreview(uint8_t level) {
 _volumePreviewLevel = level;
 __dmb( );
 _volumePreviewPending = true;
}

void DisplayManager::requestAlarmVolumePreview(uint8_t level) {
 _alarmVolPreviewLevel = level;
 __dmb( );
 _alarmVolPreviewPending = true;
}

bool DisplayManager::consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx) {
 if (!_previewPending) return false;
 __dmb( ); /* Read data AFTER the flag */
 outEvent = (SoundEvent)_previewType;
 outIdx = _previewMelIdx;
 _previewPending = false;
 return true;
}

bool DisplayManager::consumeVolumePreview(uint8_t& outLevel) {
 if (!_volumePreviewPending) return false;
 __dmb( );
 outLevel = _volumePreviewLevel;
 _volumePreviewPending = false;
 return true;
}

bool DisplayManager::consumeAlarmVolumePreview(uint8_t& outLevel) {
 if (!_alarmVolPreviewPending) return false;
 __dmb( );
 outLevel = _alarmVolPreviewLevel;
 _alarmVolPreviewPending = false;
 return true;
}

void DisplayManager::showSystemStatus( ) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_STATUS;
 _statusPage = 0;
 _statusLastDraw = 0;
 _forceSettingsRedraw = true;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::updateSystemStatus(const SystemStatusData& data) {
 _statusData = data;
}
