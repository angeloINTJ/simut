/**
 * @file    DisplayManager.h
 * @brief   TFT display manager running on Core 1 with touchscreen input and multi-screen UI.
 * @details Drives an ILI9341 320x240 TFT via SPI with XPT2046 resistive touch.
 * Runs entirely on Core 1 with cross-core communication via mutex-
 * protected shared state and a lock-free event queue. Supports
 * dashboard, graph, stats, settings, authentication, alarm action,
 * and calibration screens. Features i18n (2 languages — EN + PT), theme system,
 * alarm flash animation, web-busy overlay, and sound event signaling.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
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

#define TFT_CS    28
#define TFT_DC    27
#define TFT_RST   26
#define TOUCH_CS  17
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

    TR_KEYS_COUNT
};

struct SystemState {
    float ambientTemp; float ambientHum; bool ambientValid;
    float slotTemp; bool slotValid; int selectedSlotIdx; char slotName[32];
    int wifiRssi; bool btActive; char timeString[24];
    uint16_t pendingPkts;
    bool isBooting; char bootLogs[5][40]; bool showSkipButton; int apProgressPct;
    uint16_t alarmSlotMask;
};
class DisplayManager {
public:
    DisplayManager();
    void begin();
    void startCore1();
    void pauseRendering(bool pause);
    uint32_t getHeartbeat();
    uint32_t getPauseStartTime() { return _pauseStartTime; }
    uint32_t getLastTouchTimestamp() const { return _lastTouchTimestamp; }
    bool isCore1Ready() { return _core1Ready; }
    void forceUnpause();
    void restartCore1();

    void setAmbientData(float t, float h, bool isValid = true);
    void setAmbientMinMax(float minT, float maxT, float minH, float maxH);
    void setSlotData(float t, bool isValid, int slotIdx, String name);
    void setSlotMinMax(float minT, float maxT);
    void setSystemStatus(int rssi, bool bt, String timeStr);

    void setBootStatus(String msg, bool showSkip = false);
    void replaceBootStatus(String msg, bool showSkip = false);
    void setApProgress(int pct);
    void endBoot();

    void forceDashboard();
    bool isMenuActive();
    bool isDisplayBusy();
    bool isHeavyRendering();
    bool isSkipPressed();
    bool isScreenTouched();


    void setWebBusy(bool busy, const char* username = nullptr);
    bool isWebBusy() { return _webBusy; }
    bool hasWebOverlayPending() { return _webOverlayPending; }
    void clearWebOverlayPending() { _webOverlayPending = false; }


    void setAlarmState(uint16_t slotMask, int8_t navSlot = -1,
                       bool ambTemp = false, bool ambHum = false);


    void setAlarmSilenced(bool silenced, uint32_t endTime = 0);
    void setAlarmDeactivated(bool deactivated);
    bool isAlarmSilenced() const { return _alarmSilenced; }
    uint32_t getAlarmSilenceEnd() const { return _alarmSilenceEnd; }
    bool isAlarmDeactivated() const { return _alarmDeactivated; }
    int8_t getAlarmActionSlot() const { return _alarmActionSlot; }

    void showStats(const GraphDataPackage& data, float minHum, float maxHum);
    void showGraphPlot(const GraphDataPackage& data, float minHum, float maxHum);

    void showCalendar(int year, int month, uint32_t daysMask);
    void setCalendarDays(uint32_t daysMask);
    void setGraphNavOffset(int offset);  /**< Informa offset de navegação para label */
    int  getCalYear()  const { return _calYear; }
    int  getCalMonth() const { return _calMonth; }


    void requestLoadingScreen();
    bool isLoadingDrawn() { return _loadingDrawn; }
    bool getUiEvent(UiEvent& ev);
    void refreshTheme();
    uint16_t readPixel(int16_t x, int16_t y);
    void readRow(int16_t y, uint16_t* buffer, int16_t w = 320);

    void showSettingsThemes(int currentThemeIdx);
    void showAuthScreen(String expectedPin);
    void showSettingsMain();
    void showSettingsAlarms(SystemConfig* cfg);
    void showAlarmEdit(int sensorIdx);
    void showSettingsLang(int currentLang);
    void showSettingsPassword();
    void getNewPassword(char* out, size_t maxLen) const;
    void showTouchCalibration();
    void showTouchSensitivity();

    void showSystemStatus();
    void updateSystemStatus(const SystemStatusData& data);
    void drawSystemStatus();
    void loadTouchCalibration(const TouchCalData* cal);
    void fillCalData(TouchCalData* cal) const;
    void resetTouchCalibration();
    bool isTouchCalibrated() const { return _calValid; }
    void setLanguage(int langId);

    /* ── Display alignment offset (±4H / ±4V) ── */
    void showSettingsDisplayOffset();
    void loadDisplayOffset(const DisplayOffsetData* data);
    void fillDisplayOffsetData(DisplayOffsetData* data) const;
    int8_t getDisplayOffsetX() const;
    int8_t getDisplayOffsetY() const;


    void showSettingsSounds(const SoundSettingsState& state);
    SoundSettingsState getSoundSettings() const { return _soundSettings; }


    void showSettingsLicense();


    bool consumeTouchSound();
    bool consumeErrorSound();


    void setWebNotification(const char* username);


    bool consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx);


    bool consumeVolumePreview(uint8_t& outLevel);
    bool consumeAlarmVolumePreview(uint8_t& outLevel);

    /* BUG-002: producers dos pares (data, flag) cross-core. Encapsulam
     * a escrita de dados + __dmb() + flag, substituindo writes inline
     * espalhados por handleTouch. Chamados de Core 1; consumers (Core 0)
     * leem flag + __dmb() + dados em `consume*`. */
    void requestPreviewSound(SoundEvent ev, uint8_t melIdx);
    void requestVolumePreview(uint8_t level);
    void requestAlarmVolumePreview(uint8_t level);

    void setTelemetryPending(uint16_t count);

    /**
     * @brief Informa o resultado do último envio de telemetria.
     * @param success  true = envio OK (seta azul), false = falha (seta vermelha).
     *
     * Ao chamar com success=true ou false, a seta pisca brevemente (azul/branco)
     * para indicar atividade, depois estabiliza na cor final.
     */
    void setTelemetrySendStatus(bool success);

    const char* tr(LangKey key);
    UiMode getUiMode() const { return _uiMode; }

private:

    /**
     * @brief Trunca um texto para caber em maxPixelW pixels na fonte atual do canvas.
     * @param gfx      Ponteiro para o contexto GFX (canvas ou tft) com fonte já setada.
     * @param src      String original.
     * @param out      Buffer de saída (deve ter pelo menos outSize bytes).
     * @param outSize  Tamanho do buffer de saída.
     * @param maxPixelW Largura máxima em pixels permitida.
     */
    void truncateText(Adafruit_GFX* gfx, const char* src,
                      char* out, size_t outSize, int16_t maxPixelW);

    SystemState _sharedState;
    bool        _isDirty;
    mutex_t     _stateMutex;
    queue_t     _eventQueue;

    volatile uint32_t _lastHeartbeat = 0;
    volatile int32_t _pauseRefCount = 0;
    volatile uint32_t _pauseStartTime = 0;
    volatile bool _isPausedForFlash = false;
    volatile bool _core1Ready = false;

    volatile bool _repaintGraph = false;
    volatile bool _repaintLoading = false;
    volatile bool _loadingDrawn = false;
    volatile bool _themeChanged = false;
    volatile bool _forceFullRedraw = false;
    volatile bool _rawTouchState = false;
    volatile bool _skipPressed = false;
    volatile uint32_t _lastTouchTimestamp = 0;


    volatile bool _webBusy = false;
    volatile bool _webOverlayShown = false;
    volatile bool _webOverlayPending = false;
    char _webBusyUser[24];
    /* Sticky: último _webBusy lido com sucesso via mutex_try_enter. Core 1
     * only (sem volatile); evita flicker do overlay quando o try_enter falha
     * (lock ocupado pelo producer em Core 0 — ver BUG-004). */
    bool _lastWebBusy = false;


    volatile uint16_t _alarmSlotMask     = 0;
    volatile int8_t   _alarmNavPending   = -1;
    volatile bool     _alarmAmbientTemp  = false;
    volatile bool     _alarmAmbientHum   = false;

    /* Painel ambient: modo normal vs min/max */
    bool  _ambientShowMinMax = false;
    bool  _ambientLastMinMax = false;  /* rastreia modo anterior para limpeza */
    float _ambMinTemp = NAN, _ambMaxTemp = NAN;
    float _ambMinHum  = NAN, _ambMaxHum  = NAN;

    /* Painel slot: modo normal vs min/max */
    bool  _slotShowMinMax = false;
    bool  _slotLastMinMax = false;     /* rastreia modo anterior para limpeza */
    float _slotMinTemp = NAN, _slotMaxTemp = NAN;

    bool     _alarmFlashPhase   = false;
    uint32_t _alarmFlashTimer   = 0;
    uint32_t _alarmRotateTimer  = 0;
    uint16_t _prevAlarmSlotMask = 0;     /* rastreia mudanças para redesenhar botões */
    bool     _prevAlarmAmbTemp  = false;
    bool     _prevAlarmAmbHum   = false;


    volatile bool     _alarmSilenced     = false;
    volatile uint32_t _alarmSilenceEnd   = 0;
    volatile bool     _alarmDeactivated  = false;
    int8_t            _alarmActionSlot   = -1;


    void showAlarmAction(int8_t slotIdx);
    void drawAlarmAction();

    UiMode _uiMode = MODE_DASHBOARD;
    GraphDataPackage _graphData;
    float _currentMinHum;
    float _currentMaxHum;
    uint8_t _detailPage = 0;            /**< 0 = temperatura, 1 = umidade        */

    /* ── Estado do calendário ── */
    int      _calYear  = 2026;          /**< Ano exibido no calendário            */
    int      _calMonth = 1;             /**< Mês exibido (1-12)                   */
    uint32_t _calDaysMask = 0;          /**< Bitmask: bit N = dia N tem dados     */
    bool     _repaintCalendar = false;  /**< Flag de repintura do calendário      */
    int      _graphNavOffset = 0;       /**< Offset de navegação temporal (≤ 0)   */
    bool     _headerShowName = false;   /**< true = mostra nome do sensor (3s)     */
    uint32_t _headerNameTimer = 0;      /**< Timestamp do toque no header          */

    static void core1Entry();
    void loopCore1();
    bool pullSnapshot(SystemState& localSnapshot);

    void render(const SystemState& state);
    void drawInterfaceFixed();
    void drawTopBar(const SystemState& state);
    void drawAmbientPanel(float t, float h, bool isValid);
    void drawSlotPanel(float t, bool isValid, int slotIdx, const char* name, bool forceNameRedraw);
    void drawBottomButtons(int selectedIdx, bool forceRedraw);
    void drawLoadingScreen();
    void drawGraphScreen();
    void drawGraphDetailScreen();   /**< Tela numérica de detalhes do período */
    void drawStatsScreen();
    void drawPeriodButtons();
    void drawCalendarScreen();          /**< Tela de calendário com dias de dados */
    void drawGraphHeaderBar();          /**< Redesenha apenas o header do gráfico */
    void drawGraphIcon(int16_t x, int16_t y, uint16_t color);
    void drawWebBusyOverlay();
    void blitCanvas(GFXcanvas16* canvas, int16_t dstX, int16_t dstY, int16_t w, int16_t h);

    /** Formata epoch para label do eixo X (HH:MM ou DD/MM HHh). */
    void formatGraphTime(time_t epoch, char* buf, bool shortRange);

    /** Desenha marcador de diamante com label de valor no ponto extremo. */
    void drawPeakMarker(int16_t cx, int16_t cy, uint16_t color,
                        float value, bool above, const char* unit,
                        int16_t graphTop, int16_t graphBot);


    bool     isSlotAlarming(int slotIdx) const;
    uint16_t slotAlarmBg(int slotIdx) const;
    bool     isAnyAlarmActive() const;
    void     fixCardCorners(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                            uint16_t borderColor);

    void     maskStripCorners(GFXcanvas16* canvas,
                              int16_t stripRow, int16_t stripH,
                              int16_t cardW, int16_t cardH,
                              int16_t r, uint16_t bgColor,
                              uint16_t borderColor);
    void     redrawAlarmFlash();
    void     restoreNormalDashboard();
    void handleTouch();

    TftWithOffset* _tft;
    XPT2046_Touchscreen* _ts;
    GFXcanvas16* _canvasWide = nullptr;
    GFXcanvas16* _canvasSmall = nullptr;

    SystemState _lastRenderedState;
    int _currentPage = 0;
    uint32_t _lastTouchTime = 0;
    uint32_t _btnHoldStartTime = 0;
    int _lastPressedBtn = -1;


    uint8_t  _lastTouchRegion     = 0xFF;
    uint32_t _lastRegionTouchTime = 0;

    /**
     * Flag de release: true quando o dedo foi levantado desde o último
     * acceptTouch(). Garante que cada toque seja único — o próximo
     * só é aceito após o dedo ser retirado.
     */
    bool     _touchReleased       = true;

    /** Cooldown para botões com hold-repeat (incremento/decremento). */
    uint32_t _holdRepeatLastFire  = 0;
    static constexpr uint32_t HOLD_REPEAT_MS = 300;
    bool acceptTouch(uint8_t zoneId);
    bool acceptHoldTouch(uint8_t zoneId);
    bool acceptSlideTouch(uint8_t zoneId);

    void drawSettingsThemes();
    int _themePage = 0;
    int _previewThemeIdx = 0;
    volatile bool _repaintSettings = false;
    int _lastThemePage = -1;
    int _lastPreviewThemeIdx = -1;
    bool _forceSettingsRedraw = true;

    void drawAuthScreen();
    void drawSettingsMain();
    void drawSettingsLang();
    int _langPage = 0;
    int _lastLangPage = -1;
    int _lastPreviewLangIdx = -1;
    void scrambleKeys();

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

    void drawSettingsAlarms();
    void drawAlarmEdit();


    void drawSettingsPassword();
    void drawPasswordMessage();
    int  _kbLayer = 0;
    bool _kbShiftLock = false;
    char _kbBuffer[9];
    char _kbConfirmBuf[9];
    int  _kbCursor = 0;
    bool _kbShowRaw = false;
    int  _kbPhase = 0;
    LangKey _kbMsgKey = TR_KEYS_COUNT;
    int  _kbSelRow = 0;      /**< Fila selecionada no grid de teclas (0..2) */
    int  _kbSelCol = 0;      /**< Coluna selecionada no grid de teclas (0..9) */

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


    void drawTouchCalibration();
    void drawCalibrationMessage();
    void mapTouchPoint(TS_Point raw, int16_t &outX, int16_t &outY);
    void drawCrosshair(int16_t cx, int16_t cy, uint16_t color);


    void drawSettingsSounds();
    void drawMelodySelect();
    SoundSettingsState _soundSettings;
    int  _soundSelection = 0;
    bool _inMelodySelect = false;
    uint8_t _melSelectType = 0;
    uint8_t _melSelectIdx  = 0;
    volatile bool _touchSoundPending = false;
    volatile bool _errorSoundPending = false;


    volatile bool      _previewPending  = false;
    volatile uint8_t   _previewType     = 0;
    volatile uint8_t   _previewMelIdx   = 0;


    volatile bool      _volumePreviewPending = false;
    volatile uint8_t   _volumePreviewLevel   = 0;


    volatile bool      _alarmVolPreviewPending = false;
    volatile uint8_t   _alarmVolPreviewLevel   = 0;


    char     _webNotifyUser[16] = {0};
    uint32_t _webNotifyStartMs  = 0;

    /**
     * Estado visual da seta de envio de telemetria.
     * 0 = idle (oculta), 1 = sucesso (azul fixo), 2 = erro (vermelho fixo),
     * 3 = flash de envio (alterna azul/branco por 1s, depois → 1).
     */
    volatile uint8_t  _pktArrowState     = 0;
    volatile bool     _pktArrowFlashOn   = false;
    volatile uint32_t _pktArrowFlashTime = 0;
    volatile uint32_t _pktArrowFlashEnd  = 0;


    void drawSettingsLicense();
    int  _licensePage   = 0;
    int  _licenseTotalPages = 1;
    bool _licenseFromAuth = false;  /* voltar para auth em vez de settings */

    bool    _calValid   = false;
    bool    _calSwapXY  = false;
    int16_t _calXMin    = 200;
    int16_t _calXMax    = 3800;
    int16_t _calYMin    = 200;
    int16_t _calYMax    = 3800;

    int     _calStep    = 0;
    int     _calPhase   = 0;
    int16_t _calRawX[8];
    int16_t _calRawY[8];

    /* Hold-and-release: acumula amostras enquanto o usuário segura */
    bool     _calHolding    = false;  /**< true enquanto dedo pressionado no ponto */
    bool     _calHoldReady  = false;  /**< true após tempo mínimo de hold          */
    uint32_t _calHoldStart  = 0;      /**< millis() do início do hold              */
    int32_t  _calHoldSumX   = 0;      /**< Soma das leituras X para média          */
    int32_t  _calHoldSumY   = 0;      /**< Soma das leituras Y para média          */
    int      _calHoldSamples = 0;     /**< Número de amostras acumuladas           */
    static constexpr uint32_t CAL_HOLD_MS = 400; /**< Tempo mínimo de hold (ms)    */

    /* ── Calibração de sensibilidade do touch ── */
    static constexpr uint8_t SENS_TARGET_TAPS = 20;
    uint16_t _sensSamples[30];       /**< Amostras de p.z coletadas           */
    uint8_t  _sensCount      = 0;    /**< Total de amostras coletadas         */
    float    _sensStability   = 0.0f; /**< Índice de estabilidade (0.0..1.0)   */
    uint16_t _sensThreshold   = 400;  /**< Threshold calculado                 */
    bool     _sensDone        = false;/**< true quando calibração concluída    */
    uint32_t _sensDoneTime    = 0;   /**< millis() do momento da conclusão    */
    uint16_t _sensZThreshold  = 400; /**< Threshold ativo (carregado da config)*/

    void drawTouchSensitivity();

    /* ── Ajuste de posicionamento do display (±4H / ±4V) ── */
    int8_t _offsetPreviewX = 0;   /**< Valor sendo editado (aplicado live ao _tft) */
    int8_t _offsetPreviewY = 0;
    int8_t _offsetSavedX   = 0;   /**< Snapshot do valor salvo (para BACK restaurar) */
    int8_t _offsetSavedY   = 0;
    int8_t _lastOffsetDrawX = 99; /**< Sentinel: força primeiro redraw              */
    int8_t _lastOffsetDrawY = 99;
    void drawSettingsDisplayOffset();

    /* ── Status do sistema em tempo real ── */
    SystemStatusData _statusData;
    int              _statusPage     = 0;
    static constexpr int STATUS_PAGES = 4;
    uint32_t         _statusLastDraw = 0;

    static constexpr int16_t CAL_SCR_X[4] = {  20, 300,  20, 300 };
    static constexpr int16_t CAL_SCR_Y[4] = {  20,  20, 220, 220 };
};
