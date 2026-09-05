/**
 * @file DisplayManager_None.cpp
 * @brief Headless no-op DisplayManager for the SIMUT Air build.
 *
 * Air has no display, so every DisplayManager method compiles to a no-op.
 * The translation / language-pack statics (tr, channelLabel, unaccent,
 * logcodeLookup, trlLookup, findAndLoadLangFile, getActive*, ...) are NOT
 * here: they stay in DisplayManager_i18n.cpp and DisplayManager_LangParser.cpp
 * because the web UI and the serial/BT CLI still use them.
 *
 * Only _stateMutex is initialised because a few inline getters in the header
 * (getTopSlotIdx / getTopPanelFixedIdx / setTopSlotFixedIdx) still take it,
 * and AppManager_HistoryAlarm.cpp calls them in every build.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "DisplayManager.h"

DisplayManager::DisplayManager( ) {
 mutex_init(&_stateMutex);
}

void DisplayManager::begin( ) { }
void DisplayManager::startCore1( ) { }
void DisplayManager::pauseRendering(bool pause) { (void)pause; }
uint32_t DisplayManager::getHeartbeat( ) { return millis( ); }
void DisplayManager::forceUnpause( ) { }
void DisplayManager::launchCore1IfAbsent( ) { }
void DisplayManager::markCore1Down( ) { }
void DisplayManager::accountPauseEnd( ) { }
void DisplayManager::restartCore1( ) { }
void DisplayManager::injectTouch(int16_t x, int16_t y) { (void)x; (void)y; }
void DisplayManager::forceGraphView( ) { }
bool DisplayManager::requestQuietMode(uint32_t timeoutMs) { (void)timeoutMs; return true; }
void DisplayManager::releaseQuietMode( ) { }
bool DisplayManager::stateMutexHeldByCurrentCore( ) { return false; }

void DisplayManager::setSlotData(float t, float h, float p, SensorType type, bool isValid, int slotIdx, String name) {
 (void)t; (void)h; (void)p; (void)type; (void)isValid; (void)slotIdx; (void)name;
}
void DisplayManager::setSlotsSnapshot(const SlotSnapshot* snap, uint8_t count) { (void)snap; (void)count; }
void DisplayManager::setSlotMinMax(float minT, float maxT, float minH, float maxH) {
 (void)minT; (void)maxT; (void)minH; (void)maxH;
}
void DisplayManager::setTopSlotData(float t, float h, float p, SensorType type, bool isValid, int slotIdx, String name) {
 (void)t; (void)h; (void)p; (void)type; (void)isValid; (void)slotIdx; (void)name;
}
void DisplayManager::setBottomSlotData(float t, float h, SensorType type, bool isValid, int slotIdx, String name) {
 (void)t; (void)h; (void)type; (void)isValid; (void)slotIdx; (void)name;
}
void DisplayManager::setTopSlotMinMax(float minT, float maxT, float minH, float maxH) {
 (void)minT; (void)maxT; (void)minH; (void)maxH;
}
void DisplayManager::setSystemStatus(int rssi, bool bt, String timeStr) { (void)rssi; (void)bt; (void)timeStr; }
void DisplayManager::setApMode(bool ap) { (void)ap; }

void DisplayManager::setBootStatusKey(LangKey key, const char* suffix, bool showSkip) { (void)key; (void)suffix; (void)showSkip; }
void DisplayManager::replaceBootStatusKey(LangKey key, const char* suffix, bool showSkip) { (void)key; (void)suffix; (void)showSkip; }
void DisplayManager::setBootStatus(String msg, bool showSkip) { (void)msg; (void)showSkip; }
void DisplayManager::replaceBootStatus(String msg, bool showSkip) { (void)msg; (void)showSkip; }
void DisplayManager::setApProgress(int pct) { (void)pct; }
void DisplayManager::endBoot( ) { }

void DisplayManager::forceDashboard( ) { }
bool DisplayManager::isMenuActive( ) { return false; }
bool DisplayManager::isDisplayBusy( ) { return false; }
bool DisplayManager::isHeavyRendering( ) { return false; }
bool DisplayManager::isSkipPressed( ) { return false; }
bool DisplayManager::isScreenTouched( ) { return false; }
void DisplayManager::beginTouch( ) { }

void DisplayManager::setWebBusy(bool busy, const char* username) { (void)busy; (void)username; }

void DisplayManager::setAlarmState(uint16_t slotMask, int8_t navSlot) { (void)slotMask; (void)navSlot; }
void DisplayManager::setAlarmErrState(uint16_t errMask) { (void)errMask; }
bool DisplayManager::isSlotErrAlarming(int slotIdx) const { (void)slotIdx; return false; }
void DisplayManager::setAlarmSilenced(bool silenced, uint32_t endTime) { (void)silenced; (void)endTime; }
void DisplayManager::setAlarmErrMuted(int8_t slotIdx, bool muted) { (void)slotIdx; (void)muted; }
bool DisplayManager::isAlarmErrMuted(int8_t slotIdx) const { (void)slotIdx; return false; }

void DisplayManager::showStats(const GraphDataPackage& data, float minHum, float maxHum) { (void)data; (void)minHum; (void)maxHum; }
void DisplayManager::showGraphPlot(const GraphDataPackage& data, float minHum, float maxHum) { (void)data; (void)minHum; (void)maxHum; }
void DisplayManager::showCalendar(int year, int month, uint32_t daysMask) { (void)year; (void)month; (void)daysMask; }
void DisplayManager::setCalendarDays(uint32_t daysMask) { (void)daysMask; }
void DisplayManager::setGraphNavOffset(int offset) { (void)offset; }

void DisplayManager::requestLoadingScreen( ) { }
bool DisplayManager::getUiEvent(UiEvent& ev) { (void)ev; return false; }
void DisplayManager::refreshTheme( ) { }

uint16_t DisplayManager::readPixel(int16_t x, int16_t y) { (void)x; (void)y; return 0; }
void DisplayManager::readRow(int16_t y, uint16_t* buffer, int16_t w) { (void)y; (void)buffer; (void)w; }

void DisplayManager::showSettingsThemes(int currentThemeIdx) { (void)currentThemeIdx; }
void DisplayManager::showAuthScreen(String expectedPin) { (void)expectedPin; }
void DisplayManager::requestAuthKeypadRedraw( ) { }
void DisplayManager::showSettingsMain( ) { }
void DisplayManager::showSettingsAlarms(SystemConfig* cfg) { (void)cfg; }
void DisplayManager::refreshAlarmStatus( ) { }
void DisplayManager::showAlarmEdit(int sensorIdx) { (void)sensorIdx; }
void DisplayManager::showSettingsLang(int currentLang) { (void)currentLang; }
void DisplayManager::drawSettingsLang( ) { }
void DisplayManager::showSettingsPassword( ) { }
void DisplayManager::getNewPassword(char* out, size_t maxLen) const { if (out && maxLen) out[0] = '\0'; }
void DisplayManager::showTouchCalibration( ) { }
void DisplayManager::showTouchSensitivity( ) { }
void DisplayManager::showSystemStatus( ) { }
void DisplayManager::updateSystemStatus(const SystemStatusData& data) { (void)data; }
void DisplayManager::drawSystemStatus( ) { }
void DisplayManager::loadTouchCalibration(const TouchCalData* cal) { (void)cal; }
void DisplayManager::fillCalData(TouchCalData* cal) const { (void)cal; }
void DisplayManager::resetTouchCalibration( ) { }
void DisplayManager::setLanguage(int langId) { (void)langId; }
void DisplayManager::showSettingsDisplayOffset( ) { }
void DisplayManager::loadDisplayOffset(const DisplayOffsetData* data) { (void)data; }
void DisplayManager::fillDisplayOffsetData(DisplayOffsetData* data) const { (void)data; }
int8_t DisplayManager::getDisplayOffsetX( ) const { return 0; }
int8_t DisplayManager::getDisplayOffsetY( ) const { return 0; }
void DisplayManager::showSettingsSounds(const SoundSettingsState& state) { (void)state; }
void DisplayManager::showMuteConfirm( ) { }
void DisplayManager::showSettingsLicense( ) { }

bool DisplayManager::consumeTouchSound( ) { return false; }
bool DisplayManager::consumeErrorSound( ) { return false; }
void DisplayManager::setWebNotification(const char* username) { (void)username; }
bool DisplayManager::consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx) { (void)outEvent; (void)outIdx; return false; }
bool DisplayManager::consumeVolumePreview(uint8_t& outLevel) { (void)outLevel; return false; }
bool DisplayManager::consumeAlarmVolumePreview(uint8_t& outLevel) { (void)outLevel; return false; }
void DisplayManager::requestPreviewSound(SoundEvent ev, uint8_t melIdx) { (void)ev; (void)melIdx; }
void DisplayManager::requestVolumePreview(uint8_t level) { (void)level; }
void DisplayManager::requestAlarmVolumePreview(uint8_t level) { (void)level; }

void DisplayManager::setTelemetryPending(uint16_t count) { (void)count; }
void DisplayManager::setTelemetrySendStatus(bool success) { (void)success; }

GFXcanvas16* DisplayManager::beginScreenRender( ) { return nullptr; }
void DisplayManager::commitScreenStrip(int16_t stripIdx) { (void)stripIdx; }
void DisplayManager::endScreenRender( ) { }
