/**
 * @file DisplayManager.h
 * @brief TFT display manager running on Core 1 with touchscreen input and multi-screen UI.
 * @details Drives an ILI9341 320x240 TFT via SPI with XPT2046 resistive touch.
 * Runs entirely on Core 1 with cross-core communication via mutex-
 * protected shared state and a lock-free event queue. Supports
 * dashboard, graph, stats, settings, authentication, alarm action,
 * and calibration screens. Features i18n (2 languages — EN + PT), theme system,
 * alarm flash animation, web-busy overlay, and sound event signaling.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "TftWithOffset.h"
#include <XPT2046_Touchscreen.h>
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "SystemDefs.h"
#include "Themes.h"
#include "SoundManager.h"

#define TFT_CS 28
#define TFT_DC 27
#define TFT_RST 26
#define TOUCH_CS 17
#define TOUCH_IRQ 20

enum LangKey {
	TR_AMBIENT, TR_CONFIG_MAIN, TR_CONFIG_THEMES, TR_CONFIG_LANG, TR_BACK,
	TR_APPLY, TR_CANCEL, TR_AUTH_TITLE, TR_ACCESS_BLOCKED, TR_REBOOT_REQ,
	TR_ATTEMPTS_EXCEEDED, TR_WAIT_SECONDS, TR_INVALID_PASSWORD, TR_LOADING,
	TR_READING_HISTORY, TR_NO_DATA, TR_MAX_LBL, TR_MIN_LBL, TR_TEMP,
	TR_HUMIDITY, TR_PLOT_CHART, TR_MENU_THEMES, TR_MENU_ALARMS, TR_MENU_SOUNDS,
	TR_MENU_LANG, TR_APPLYING_THEME, TR_SAVE, TR_ALARMS_TITLE, TR_TEMP_MIN,
	TR_TEMP_MAX, TR_HUM_MIN, TR_HUM_MAX, TR_ENTER, TR_SKIP,
	TR_MENU_PASSWORD, TR_NEW_PASSWORD,
	TR_MENU_TOUCH_CAL, TR_CAL_TITLE, TR_CAL_TOUCH_POINT, TR_CAL_DONE,
	TR_CAL_REJECTED,

	TR_CONFIRM_PASSWORD, TR_PWD_TOO_SHORT, TR_PWD_MISMATCH, TR_PWD_SAVED, TR_UNDERSTOOD,


	TR_SOUNDS_TITLE, TR_SND_TOUCH, TR_SND_CONFIRM, TR_SND_ERROR, TR_SND_ALARM,
	TR_SND_MUTE, TR_SND_VOLUME, TR_SND_ALARM_VOL, TR_ON, TR_OFF,

	TR_SND_WEB, TR_SND_MELODY,

	TR_MENU_LICENSE, TR_LICENSE_TITLE,

	TR_ALARM_ACTIVE,

	TR_SILENCE_120S, TR_DEACTIVATE, TR_MINMAX, TR_SILENCED,
	TR_HUM_SUFFIX,

	TR_MENU_TOUCH_SENS, TR_SENS_TITLE, TR_SENS_TAP, TR_SENS_DONE,

	TR_AVG_LBL, TR_STD_LBL, TR_ERROR_LBL, TR_AP_MODE,

	TR_MENU_STATUS, TR_STATUS_TITLE,

	TR_MENU_DISPLAY_OFFSET, TR_DISPLAY_OFFSET_TITLE, TR_DISPLAY_OFFSET_HINT,

	/* Boot terminal i18n strings — resolved via tr() at runtime.
	 * Boot lines shown before language pack load appear in EN, then
	 * are retranslated once setLanguage() loads the .lng file. */
	TR_BOOT_HOLD_AP, TR_BOOT_AP_CANCELLED,
	TR_BOOT_MOUNT_FS, TR_BOOT_START_LOG, TR_BOOT_START_CMD,
	TR_BOOT_LOAD_THEME_LANG, TR_BOOT_TOUCH_CAL_REQ,
	TR_BOOT_LOAD_PERIPH, TR_BOOT_START_AP, TR_BOOT_AP_NETWORK, TR_BOOT_AP_IP,
	TR_BOOT_START_WIFI, TR_BOOT_WIFI_SKIPPED,
	TR_BOOT_WAITING_ROUTER, TR_BOOT_SYNC_NTP,
	TR_BOOT_NET_TIMEOUT, TR_BOOT_NET_CONNECTED,
	TR_BOOT_START_TEL, TR_BOOT_START_WEB, TR_BOOT_REG_CALLBACKS,
	TR_BOOT_AP_ACTIVE,
	TR_BOOT_LOAD_MINMAX, TR_BOOT_WARMUP, TR_BOOT_CORRECT_TS,
	TR_BOOT_RELOAD_MINMAX, TR_BOOT_PREP_DASH,
	TR_BOOT_ALL_INIT, TR_BOOT_SYS_READY,
	TR_BOOT_APPLYING_CFG, TR_BOOT_REBOOTING,

	TR_KEYS_COUNT
};

/** Entry in the boot log buffer — stores a TR key (resolved at render time via
 * tr()) plus an optional suffix concatenated after (e.g. animated dots). When the
 * caller passes a raw string without a TR key (legacy), stores key=TR_KEYS_COUNT
 * and the literal text goes in suffix. */
struct BootLogEntry {
	int16_t key; /**< TR_BOOT_* or TR_KEYS_COUNT (= raw, untranslated) */
	char suffix[40];/**< Concatenated after tr(key); or raw text if key==COUNT */
};

struct SystemState {
	float ambientTemp; float ambientHum; bool ambientValid;
	float slotTemp; bool slotValid; int selectedSlotIdx; char slotName[32];
	int wifiRssi; bool btActive; char timeString[24];
	uint16_t pendingPkts;
	bool isBooting; BootLogEntry bootLogs[5]; bool showSkipButton; int apProgressPct;
	uint16_t alarmSlotMask;
};
class DisplayManager {
public:
	DisplayManager( );
	void begin( );
	void startCore1( );
	void pauseRendering(bool pause);
	uint32_t getHeartbeat( );
	uint32_t getPauseStartTime( ) { return _pauseStartTime; }
	uint32_t getLastTouchTimestamp( ) const { return _lastTouchTimestamp; }
	bool isCore1Ready( ) { return _core1Ready; }
	void forceUnpause( );
	void restartCore1( );

	/** Injects a simulated touch at (x, y). For a single frame:
	 * Core 1 reads _simTouchActive in handleTouch and uses the override
	 * coordinates instead of the real _ts->getPoint(). Useful for automation
	 * (generating screenshots of all screens via API). */
	void injectTouch(int16_t x, int16_t y);

	/** Forces MODE_GRAPH_VIEW for screenshot automation
	 * (bypasses touch). Sets _uiMode + _forceFullRedraw. */
	void forceGraphView( );

	/** Cooperative "quiet" mode for large flash saves.
	 * Core 0 signals, Core 1 enters a RAM-only loop with
	 * IRQs disabled, Core 0 does all flash ops without attempting
	 * IRQ-based multicore lockout, then releases and Core 1 redraws everything.
	 * requestQuietMode blocks waiting for ACK from Core 1 (up to timeout).
	 * Returns true if Core 1 confirmed; false if timeout or Core 1 not ready. */
	bool requestQuietMode(uint32_t timeoutMs = 15000);
	void releaseQuietMode( );
	/** Core 1 in quiet mode (or transitioning). IRQ-based lockout is impossible
	 * here (Core 1 with IRQs off) and unnecessary (Core 1 does not touch flash). */
	bool isInQuietMode( ) const { return _quietModeRequested || _quietModeActive; }

	void setAmbientData(float t, float h, bool isValid = true);
	void setAmbientMinMax(float minT, float maxT, float minH, float maxH);
	void setSlotData(float t, bool isValid, int slotIdx, String name);
	void setSlotMinMax(float minT, float maxT);
	void setSystemStatus(int rssi, bool bt, String timeStr);

	/** Boot status: stores a TR key (resolved at render via tr()) plus
	 * optional suffix. Use for messages that should follow the active language.
	 * replaceBootStatusKey is the in-place equivalent (updates current line
	 * instead of scrolling). */
	void setBootStatusKey(LangKey key, const char* suffix = nullptr, bool showSkip = false);
	void replaceBootStatusKey(LangKey key, const char* suffix = nullptr, bool showSkip = false);

	/** Legacy raw-string API: for messages already translated externally or
	 * without a TR key. Does not follow language changes. */
	void setBootStatus(String msg, bool showSkip = false);
	void replaceBootStatus(String msg, bool showSkip = false);
	void setApProgress(int pct);
	void endBoot( );

	void forceDashboard( );
	/* Resets the touch idle timer (30s → forceDashboard guard).
	 * Used by CLI 'screen NAME' so the forced screen remains
	 * stable during capture via /api/screenshot (~5s). */
	void resetTouchIdle( ) { _lastTouchTime = millis( ); }
	bool isMenuActive( );
	bool isDisplayBusy( );
	bool isHeavyRendering( );
	bool isSkipPressed( );
	bool isScreenTouched( );


	void setWebBusy(bool busy, const char* username = nullptr);
	bool isWebBusy( ) { return _webBusy; }
	bool hasWebOverlayPending( ) { return _webOverlayPending; }
	void clearWebOverlayPending( ) { _webOverlayPending = false; }


	void setAlarmState(uint16_t slotMask, int8_t navSlot = -1,
		bool ambTemp = false, bool ambHum = false);


	void setAlarmSilenced(bool silenced, uint32_t endTime = 0);
	void setAlarmDeactivated(bool deactivated);
	bool isAlarmSilenced( ) const { return _alarmSilenced; }
	uint32_t getAlarmSilenceEnd( ) const { return _alarmSilenceEnd; }
	bool isAlarmDeactivated( ) const { return _alarmDeactivated; }
	int8_t getAlarmActionSlot( ) const { return _alarmActionSlot; }

	void showStats(const GraphDataPackage& data, float minHum, float maxHum);
	void showGraphPlot(const GraphDataPackage& data, float minHum, float maxHum);

	void showCalendar(int year, int month, uint32_t daysMask);
	void setCalendarDays(uint32_t daysMask);
	void setGraphNavOffset(int offset); /**< Informs navigation offset for label */
	int getCalYear( ) const { return _calYear; }
	int getCalMonth( ) const { return _calMonth; }


	void requestLoadingScreen( );
	bool isLoadingDrawn( ) { return _loadingDrawn; }
	bool getUiEvent(UiEvent& ev);
	void refreshTheme( );
	uint16_t readPixel(int16_t x, int16_t y);
	void readRow(int16_t y, uint16_t* buffer, int16_t w = 320);

	void showSettingsThemes(int currentThemeIdx);
	void showAuthScreen(String expectedPin);
	/** Forces repaint of the MODE_AUTH keypad on the next
	 * drawAuthScreen. Needed when another screen (e.g. license) covered
	 * the keypad and the user returned to Auth — without this, only the chrome+dots
	 * are redrawn and the keypad stays blank. */
	void requestAuthKeypadRedraw( );
	void showSettingsMain( );
	void showSettingsAlarms(SystemConfig* cfg);
	void showAlarmEdit(int sensorIdx);
	void showSettingsLang(int currentLang);
	void showSettingsPassword( );
	void getNewPassword(char* out, size_t maxLen) const;
	void showTouchCalibration( );
	void showTouchSensitivity( );

	void showSystemStatus( );
	void updateSystemStatus(const SystemStatusData& data);
	void drawSystemStatus( );
	void loadTouchCalibration(const TouchCalData* cal);
	void fillCalData(TouchCalData* cal) const;
	void resetTouchCalibration( );
	bool isTouchCalibrated( ) const { return _calValid; }
	void setLanguage(int langId);

	/* Display alignment offset (+-4H / +-4V) */
	void showSettingsDisplayOffset( );
	void loadDisplayOffset(const DisplayOffsetData* data);
	void fillDisplayOffsetData(DisplayOffsetData* data) const;
	int8_t getDisplayOffsetX( ) const;
	int8_t getDisplayOffsetY( ) const;


	void showSettingsSounds(const SoundSettingsState& state);
	void showMuteConfirm( );
	SoundSettingsState getSoundSettings( ) const { return _soundSettings; }


	void showSettingsLicense( );


	bool consumeTouchSound( );
	bool consumeErrorSound( );


	void setWebNotification(const char* username);


	bool consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx);


	bool consumeVolumePreview(uint8_t& outLevel);
	bool consumeAlarmVolumePreview(uint8_t& outLevel);

	/* Cross-core producers for (data, flag) pairs. Encapsulate
	 * data write + __dmb() + flag, replacing scattered inline writes
	 * in handleTouch. Called from Core 1; consumers (Core 0)
	 * read flag + __dmb() + data in consume*. */
	void requestPreviewSound(SoundEvent ev, uint8_t melIdx);
	void requestVolumePreview(uint8_t level);
	void requestAlarmVolumePreview(uint8_t level);

	void setTelemetryPending(uint16_t count);

	/**
	 * @brief Reports the result of the last telemetry send.
	 * @param success true = send OK (blue arrow), false = failure (red arrow).
	 *
	 * On success, the arrow flashes briefly (blue/white)
	 * to indicate activity, then stabilizes to the final color.
	 */
	void setTelemetrySendStatus(bool success);

	const char* tr(LangKey key);
	UiMode getUiMode( ) const { return _uiMode; }

	/** Set once at boot by AppManager. _sysConfigPtr must be
	 * valid before the first dashboard render so buildDashLayout
	 * knows which slots are active. showSettingsAlarms also writes
	 * the same pointer (override no-op with the same value). */
	void setSysConfig(SystemConfig* cfg) { _sysConfigPtr = cfg; }

private:

	/**
	 * @brief Truncates text to fit within maxPixelW pixels in the canvas's current font.
	 * @param gfx Pointer to the GFX context (canvas or tft) with font already set.
	 * @param src Original string.
	 * @param out Output buffer (must have at least outSize bytes).
	 * @param outSize Output buffer size.
	 * @param maxPixelW Maximum width in pixels allowed.
	 */
	void truncateText(Adafruit_GFX* gfx, const char* src,
		char* out, size_t outSize, int16_t maxPixelW);

	SystemState _sharedState;
	bool _isDirty;
	mutex_t _stateMutex;
	queue_t _eventQueue;

	volatile uint32_t _lastHeartbeat = 0;
	volatile int32_t _pauseRefCount = 0;
	volatile uint32_t _pauseStartTime = 0;
	volatile bool _isPausedForFlash = false;
	volatile bool _core1Ready = false;

	/* Marker: "TFT has been initialized at least once."
	 * Survives multicore_reset_core1 (member of DisplayManager, not local
	 * variable of loopCore1). On first launch, does full init (HW reset ILI9341 +
	 * SPI setup + fillScreen). On subsequent launches (post-reset for save),
	 * skips re-initialization — TFT retains last frame, avoids white flash. */
	bool _tftFirstInit = true;

	/* Cooperative quiet mode flags (see requestQuietMode).
	 * _quietModeRequested: Core 0 writes, Core 1 reads each iteration of loopCore1.
	 * _quietModeActive: Core 1 writes (ACK), Core 0 waits as handshake.
	 * _quietModeRefCount: allows re-entrant calls (e.g. CLI handler wraps
	 * save+reload, saveConfiguration calls again internally). */
	volatile bool _quietModeRequested = false;
	volatile bool _quietModeActive = false;
	volatile int32_t _quietModeRefCount = 0;

	/* RAM-resident quiet loop — called in loopCore1 when _quietModeRequested. */
	void _runQuietLoop( );

	volatile bool _repaintGraph = false;
	volatile bool _repaintLoading = false;
	volatile bool _loadingDrawn = false;
	volatile bool _themeChanged = false;
	volatile bool _langChanged = false; /**< setLanguage() sets this;
		render() forces fullRedraw during boot. */
	volatile bool _forceFullRedraw = false;
	volatile bool _rawTouchState = false;
	volatile bool _skipPressed = false;
	volatile uint32_t _lastTouchTimestamp = 0;

	/* Simulated touch injection via CLI 'touch sim X Y'.
	 * Enables automated screenshot capture for all screens via
	 * /api/screenshot — Core 0 sets x/y/active flag, Core 1 (handleTouch
	 * via _ts->getPoint) checks the override. After processing, _simActive
	 * returns to false. Coordinates in screen-space (320x240, same as
	 * mapTouchPoint output). */
	volatile int16_t _simTouchX = 0;
	volatile int16_t _simTouchY = 0;
	volatile bool _simTouchActive = false;
	volatile uint32_t _simTouchSetMs = 0;


	volatile bool _webBusy = false;
	volatile bool _webOverlayShown = false;
	volatile bool _webOverlayPending = false;
	char _webBusyUser[24];
	/* Sticky: last _webBusy read successfully via mutex_try_enter. Core 1
	 * only (no volatile); avoids overlay flicker when try_enter fails
	 * (lock held by the producer on Core 0). */
	bool _lastWebBusy = false;


	volatile uint16_t _alarmSlotMask = 0;
	volatile int8_t _alarmNavPending = -1;
	volatile bool _alarmAmbientTemp = false;
	volatile bool _alarmAmbientHum = false;

	/* Ambient panel: normal vs min/max mode */
	bool _ambientShowMinMax = false;
	bool _ambientLastMinMax = false; /* tracks previous mode for cleanup */
	float _ambMinTemp = NAN, _ambMaxTemp = NAN;
	float _ambMinHum = NAN, _ambMaxHum = NAN;

	/* Slot panel: normal vs min/max mode */
	bool _slotShowMinMax = false;
	bool _slotLastMinMax = false; /* tracks previous mode for cleanup */
	float _slotMinTemp = NAN, _slotMaxTemp = NAN;

	bool _alarmFlashPhase = false;
	uint32_t _alarmFlashTimer = 0;
	uint32_t _alarmRotateTimer = 0;
	uint16_t _prevAlarmSlotMask = 0; /* tracks changes to redraw buttons */
	bool _prevAlarmAmbTemp = false;
	bool _prevAlarmAmbHum = false;


	volatile bool _alarmSilenced = false;
	volatile uint32_t _alarmSilenceEnd = 0;
	volatile bool _alarmDeactivated = false;
	int8_t _alarmActionSlot = -1;


	void showAlarmAction(int8_t slotIdx);
	void drawAlarmAction( );

	UiMode _uiMode = MODE_DASHBOARD;
	GraphDataPackage _graphData;
	float _currentMinHum;
	float _currentMaxHum;
	uint8_t _detailPage = 0; /**< 0 = temperature, 1 = humidity */

	/* Calendar state */
	int _calYear = 2026; /**< Year shown on calendar */
	int _calMonth = 1; /**< Month shown (1-12) */
	uint32_t _calDaysMask = 0; /**< Bitmask: bit N = day N has data */
	bool _repaintCalendar = false; /**< Calendar repaint flag */
	int _graphNavOffset = 0; /**< Temporal navigation offset (<= 0) */
	bool _headerShowName = false; /**< true = showing sensor name (3s) */
	uint32_t _headerNameTimer = 0; /**< Timestamp of header touch */

	static void core1Entry( );
	void loopCore1( );
	bool pullSnapshot(SystemState& localSnapshot);

	void render(const SystemState& state);
	void drawInterfaceFixed( );
	void drawTopBar(const SystemState& state);
	void drawAmbientPanel(float t, float h, bool isValid);
	void drawSlotPanel(float t, bool isValid, int slotIdx, const char* name, bool forceNameRedraw);
	void drawBottomButtons(int selectedIdx, bool forceRedraw);

	/** Dynamic dashboard layout: omits inactive slots and the pagination
	 * button when all buttons fit on one line. Shared between
	 * drawBottomButtons() and the touch handler. */
	struct DashBtn {
		int8_t kind; /**< 0=slot, 1=cfg, 2=page */
		int8_t slotId; /**< valid only for kind==0 (0..9) */
	};
	/** Fills `out` with up to 5 buttons for the current page (left to right).
	 * Returns the number of buttons. Updates `_currentPage` if it went out
	 * of range after a config change. `hasPaging` true when >5 total
	 * buttons (slot+CFG); in that case the last button per page is PAGE. */
	int buildDashLayout(DashBtn out[5], int *totalPages, bool *hasPaging);
	void drawLoadingScreen( );
	void drawGraphScreen( );
	void drawGraphDetailScreen( ); /**< Numeric detail screen for the period */
	void drawStatsScreen( );
	void drawPeriodButtons( );
	void drawCalendarScreen( ); /**< Calendar screen with data-day indicators */
	void drawGraphHeaderBar(bool blitNow = true); /**< Redraws only the graph header.
	 * blitNow=false suppresses the internal blit when
	 * called from within a strip-render loop (the
	 * strip's external blit already covers the region). */
	void drawGraphIcon(int16_t x, int16_t y, uint16_t color);
	void drawWebBusyOverlay( );
	void blitCanvas(GFXcanvas16* canvas, int16_t dstX, int16_t dstY, int16_t w, int16_t h);

	/** Formats epoch to X-axis label (HH:MM or DD/MM HHh). */
	void formatGraphTime(time_t epoch, char* buf, bool shortRange);

	/** Draws a diamond marker with value label at the extreme point. */
	void drawPeakMarker(int16_t cx, int16_t cy, uint16_t color,
		float value, bool above, const char* unit,
		int16_t graphTop, int16_t graphBot);


	bool isSlotAlarming(int slotIdx) const;
	uint16_t slotAlarmBg(int slotIdx) const;
	bool isAnyAlarmActive( ) const;
	void fixCardCorners(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
		uint16_t borderColor);

	void maskStripCorners(GFXcanvas16* canvas,
		int16_t stripRow, int16_t stripH,
		int16_t cardW, int16_t cardH,
		int16_t r, uint16_t bgColor,
		uint16_t borderColor);
	void redrawAlarmFlash( );
	void restoreNormalDashboard( );
	void handleTouch( );

	/* Pointers MUST have explicit `= nullptr`. In BSS the implicit zero-init
	 * of C++ zeroed them (DisplayManager as value member of AppManager); on the
	 * heap (via make_unique<DisplayManager>) they held garbage, making
	 * `if (!_tft) _tft = new ...` in loopCore1 skip allocation (garbage usually
	 * evaluates to true). _tft/_ts would point to garbage and the first
	 * _ts->begin() or _tft->begin() would hang Core 1. */
	TftWithOffset* _tft = nullptr;
	XPT2046_Touchscreen* _ts = nullptr;
	GFXcanvas16* _canvasWide = nullptr;
	GFXcanvas16* _canvasSmall = nullptr;

	/* Full-screen render reuses `_canvasWide` (320x45)
	 * which already exists for the dashboard. During full-screen (auth/settings/etc),
	 * the dashboard is not rendering, so `_canvasWide` is free.
	 * A 40px strip uses only 40 of the 45 canvas rows (5 extra rows ignored).
	 * 6 strips x 40px = 240 = total screen height.
	 *
	 * Advantages vs dynamic allocation:
	 * - Zero heap pressure (canvas already allocated at boot)
	 * - Zero risk of null-buffer crash (Adafruit_GFX::buffer uninitialized)
	 * - Telemetry continues running during render (heap free intact)
	 * - No fallback needed */
	static constexpr int16_t RENDER_STRIP_H = 40;

	SystemState _lastRenderedState;
	int _currentPage = 0;
	uint32_t _lastTouchTime = 0;
	uint32_t _btnHoldStartTime = 0;
	int _lastPressedBtn = -1;


	uint8_t _lastTouchRegion = 0xFF;
	uint32_t _lastRegionTouchTime = 0;

	/**
	 * Release flag: true when the finger has been lifted since the last
	 * acceptTouch(). Ensures each touch is unique — the next
	 * is only accepted after the finger is removed.
	 */
	bool _touchReleased = true;

	/** Cooldown for buttons with hold-repeat (increment/decrement). */
	uint32_t _holdRepeatLastFire = 0;
	static constexpr uint32_t HOLD_REPEAT_MS = 300;
	bool acceptTouch(uint8_t zoneId);
	bool acceptHoldTouch(uint8_t zoneId);
	bool acceptSlideTouch(uint8_t zoneId);

	void drawSettingsThemes( );
	int _themePage = 0;
	int _previewThemeIdx = 0;
	volatile bool _repaintSettings = false;
	int _lastThemePage = -1;
	int _lastPreviewThemeIdx = -1;
	bool _forceSettingsRedraw = true;

	void drawAuthScreen( );
	void drawSettingsMain( );
	void drawSettingsLang( );
	int _langPage = 0;
	int _lastLangPage = -1;
	int _lastPreviewLangIdx = -1;
	void scrambleKeys( );

	char _keypadChars[4][5];
	String _expectedPin;
	int _authStep = 0;
	bool _authFailed = false;
	bool _isCurrentAttemptValid = true;
	int _failedAttempts = 0;
	uint32_t _lockoutUntil = 0;
	bool _permanentLockout = false;

	uint32_t _rngState = 123456789;
	uint32_t fastRandom(uint32_t maxVal);

	int _menuSelection = 0;
	int _mainMenuPage = 0;
	int _lastMainMenuPage = -1;
	int _currentLangIdx = 1;
	int _previewLangIdx = 0;

	void drawSettingsAlarms( );
	void drawAlarmEdit( );


	void drawSettingsPassword( );
	void drawPasswordMessage( );
	int _kbLayer = 0;
	bool _kbShiftLock = false;
	char _kbBuffer[9];
	char _kbConfirmBuf[9];
	int _kbCursor = 0;
	bool _kbShowRaw = false;
	int _kbPhase = 0;
	LangKey _kbMsgKey = TR_KEYS_COUNT;
	int _kbSelRow = 0; /**< Selected row in the key grid (0..2) */
	int _kbSelCol = 0; /**< Selected column in the key grid (0..9) */

	SystemConfig* _sysConfigPtr = nullptr;
	int _alarmPage = 0;
	int _lastAlarmPage = -1;
	int _activeSensorCount = 0;
	int _activeSensorsMap[MAX_SENSORS + 1];
	int _alarmSelection = 0;
	int _lastAlarmSelection = -1;
	int _editSensorIdx = -1;
	int _editFieldFocus = 0;
	SensorRecord _tempAlarmConfig;


	void drawTouchCalibration( );
	void drawCalibrationMessage( );
	void mapTouchPoint(TS_Point raw, int16_t &outX, int16_t &outY);
	void drawCrosshair(int16_t cx, int16_t cy, uint16_t color);


	void drawSettingsSounds( );
	void drawMelodySelect( );
	void drawMuteConfirm( );
	SoundSettingsState _soundSettings;
	int _soundSelection = 0;
	bool _inMelodySelect = false;
	uint8_t _melSelectType = 0;
	uint8_t _melSelectIdx = 0;
	volatile bool _touchSoundPending = false;
	volatile bool _errorSoundPending = false;


	volatile bool _previewPending = false;
	volatile uint8_t _previewType = 0;
	volatile uint8_t _previewMelIdx = 0;


	volatile bool _volumePreviewPending = false;
	volatile uint8_t _volumePreviewLevel = 0;


	volatile bool _alarmVolPreviewPending = false;
	volatile uint8_t _alarmVolPreviewLevel = 0;


	char _webNotifyUser[16] = {0};
	uint32_t _webNotifyStartMs = 0;

	/**
	 * Visual state of the telemetry send arrow.
	 * 0 = idle (hidden), 1 = success (fixed blue), 2 = error (fixed red),
	 * 3 = send flash (toggles blue/white for 1s, then -> 1).
	 */
	volatile uint8_t _pktArrowState = 0;
	volatile bool _pktArrowFlashOn = false;
	volatile uint32_t _pktArrowFlashTime = 0;
	volatile uint32_t _pktArrowFlashEnd = 0;


	void drawSettingsLicense( );
	int _licensePage = 0;
	int _licenseTotalPages = 1;
	bool _licenseFromAuth = false; /* return to auth instead of settings */

	bool _calValid = false;
	bool _calSwapXY = false;
	int16_t _calXMin = 200;
	int16_t _calXMax = 3800;
	int16_t _calYMin = 200;
	int16_t _calYMax = 3800;

	int _calStep = 0;
	int _calPhase = 0;
	int16_t _calRawX[8];
	int16_t _calRawY[8];

	/* Hold-and-release: accumulates samples while the user holds */
	bool _calHolding = false; /**< true while finger pressed on point */
	bool _calHoldReady = false; /**< true after minimum hold time */
	uint32_t _calHoldStart = 0; /**< millis() of hold start */
	int32_t _calHoldSumX = 0; /**< Sum of X readings for averaging */
	int32_t _calHoldSumY = 0; /**< Sum of Y readings for averaging */
	int _calHoldSamples = 0; /**< Number of accumulated samples */
	static constexpr uint32_t CAL_HOLD_MS = 400; /**< Minimum hold time (ms) */

	/* Touch sensitivity calibration */
	static constexpr uint8_t SENS_TARGET_TAPS = 20;
	uint16_t _sensSamples[30]; /**< Collected p.z samples */
	uint8_t _sensCount = 0; /**< Total samples collected */
	float _sensStability = 0.0f; /**< Stability index (0.0..1.0) */
	uint16_t _sensThreshold = 400; /**< Calculated threshold */
	bool _sensDone = false;/**< true when calibration is complete */
	uint32_t _sensDoneTime = 0; /**< millis() of completion moment */
	uint16_t _sensZThreshold = 400; /**< Active threshold (loaded from config) */

	void drawTouchSensitivity( );

	/* Display positioning adjustment (+-4H / +-4V) */
	int8_t _offsetPreviewX = 0; /**< Value being edited (applied live to _tft) */
	int8_t _offsetPreviewY = 0;
	int8_t _offsetSavedX = 0; /**< Snapshot of saved value (for BACK restore) */
	int8_t _offsetSavedY = 0;
	int8_t _lastOffsetDrawX = 99; /**< Sentinel: forces first redraw */
	int8_t _lastOffsetDrawY = 99;
	void drawSettingsDisplayOffset( );

	/* Real-time system status */
	SystemStatusData _statusData;
	int _statusPage = 0;
	static constexpr int STATUS_PAGES = 4;
	uint32_t _statusLastDraw = 0;

	static constexpr int16_t CAL_SCR_X[4] = { 20, 300, 20, 300 };
	static constexpr int16_t CAL_SCR_Y[4] = { 20, 20, 220, 220 };

public:
	/* Full-screen render via 3 strips of 80px.
	 * Typical usage (instead of _tft->fillScreen + sequential draws):
	 * GFXcanvas16* cv = beginScreenRender( );
	 * if (!cv) { fallback _tft->...; return; }
	 * // Strip 0 (y=0..79): same coordinates as screen
	 * cv->fillRect(4, 4, 312, 32, ...); cv->setCursor(x, 22); cv->print(title);
	 * commitScreenStrip(0);
	 * // Strip 1 (y=80..159): subtract 80 from screen coordinates
	 * commitScreenStrip(1);
	 * // Strip 2 (y=160..239): subtract 160 from screen coordinates
	 * cv->fillRoundRect(10, 202-160, 110, 32, 8, ...);
	 * commitScreenStrip(2);
	 * endScreenRender( );
	 * Alloc/free per render = frees heap for telemetry between renders. */
	GFXcanvas16* beginScreenRender( );
	void commitScreenStrip(int16_t stripIdx);
	void endScreenRender( );

	/* Dynamic language pack support.
	 * EN is hardcoded in firmware (DICTIONARY_EN, translateCodeEn, and
	 * EN literals in TRL sites). Non-EN comes from .lng:
	 * @DICT -> strings[TR_KEYS_COUNT] (TFT UI)
	 * @LOGCODES -> logcodes (sorted by code) (LogCode messages)
	 * @TRL -> trls (sorted by FNV-1a of EN) (TRL messages)
	 * @HELP / @LICENSE -> free text
	 * Lookup is binary search; fallback is inline EN.
	 * Defined in DisplayManager_LangParser.cpp. */
	struct LogCodeEntry { uint16_t code; const char* text; };
	struct TrlEntry { uint32_t hash; const char* text; };

	/** UTF-8 to ASCII 7-bit transliteration (Latin accents removed). */
	static void unaccent(const char* utf8, char* out, size_t outSize);
	/** Log code lookup in .lng. Returns nullptr if absent. */
	static const char* logcodeLookup(uint16_t code);
	/** TRL string lookup via FNV-1a hash of EN. nullptr if absent. */
	static const char* trlLookup(const char* en);
	/** FNV-1a 32-bit. Exposed for Python tooling that generates .lng. */
	static uint32_t fnv1a32(const char* s);
	/** Scans /lang/ for language_*.lng, loads the first alphabetically.
	 * Logs a warning if extras exist. Core 0 only (LittleFS).
	 * Returns true if a .lng was successfully loaded. */
	static bool findAndLoadLangFile( );
	/** Getters for @HELP/@LICENSE block from active .lng (UTF-8).
	 * Caller applies unaccent() to render on ASCII UI/CLI.
	 * Returns nullptr if .lng not loaded or section absent. */
	static const char* getActiveHelpText( );
	static const char* getActiveLicenseText( );
	/** JSON with translations for Web UI (UTF-8 directly;
	 * the browser consumes without unaccent). Served by GET /api/lang. */
	static const char* getActiveWebDict( );
	/** True if _activeLang is populated (any lookup may hit). */
	static bool isLangLoaded( );
	/** Active .lng meta info (name + code) for /api/perms to populate
	 * the language selector in the web drawer. Returns "" if !isLangLoaded(). */
	static const char* getActiveLangName( );
	static const char* getActiveLangCode( );

private:
	struct ActiveLang {
		char name[16];
		char code[8];
		char* strings[TR_KEYS_COUNT];
		char* helpText;
		char* licenseText;
		char* webDict; /**< JSON blob from @WEBDICT (UTF-8) */
		LogCodeEntry* logcodes;
		uint16_t logcodesCount;
		TrlEntry* trls;
		uint16_t trlsCount;
		char* buffer;
		size_t bufferSize;
	};
	static ActiveLang _activeLang;
	static bool _activeLangLoaded;
	static bool loadLangFile(const char* path);
	static void unloadLang( );
};
