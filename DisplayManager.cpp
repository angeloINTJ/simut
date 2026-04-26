/**
 * @file    DisplayManager.cpp
 * @brief   Implementation of DisplayManager — Core 1 render loop, touch handling, and all UI screens.
 * @details Contains the complete rendering engine: Core 1 entry point, snapshot-
 * based dirty rendering, dashboard with ambient/slot panels, graph
 * plotting with dual Y-axis, settings menus (themes, alarms, sounds,
 * language, password, calibration, license), authentication keypad with
 * scrambled layout and lockout, alarm flash animation with per-slot
 * masking, and the i18n dictionary for 2 languages (EN + PT).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "DisplayManager.h"
#include "LogManager.h"
#include "DisplayManager_Fonts.h"  /* REF-001: simutFont{9,12,24}pt — wrappers compartilhados */
#include "DisplayManager_FmtFloat.h"
#include <LittleFS.h>

#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <stdlib.h>

#include "pico/multicore.h"


/* F-I18N-TRIM.1 (v3.22.0): reduzido de 8 para 2 idiomas (EN + PT) para
 * economizar flash. Os outros 6 idiomas ficaram no histórico git.
 * CON-002 (v3.23.4): TOTAL_LANGS local substituído por LANG_COUNT do enum
 * em SystemDefs.h; static_assert amarra o tamanho dos arrays à sentinela. */






/* REF/F17: LICENSE moved to /system/license_{en,pt}.txt — economiza ~3.4 KB
 * de flash. Carregado em RAM (_licenseBuf) quando o user troca de idioma.
 * drawSettingsLicense (Core 1) lê do buffer. Se arquivo missing, mostra
 * fallback. setLanguage é chamada apenas pelo Core 0 (boot e EVT_APPLY_LANG),
 * então o LittleFS.open aqui é livre de race com Core 1. */
static char _licenseBuf[2048];

static void loadLicenseFromFs(int langIdx) {
    const char* path = (langIdx == 1) ? "/system/license_pt.txt" : "/system/license_en.txt";
    File f = LittleFS.open(path, "r");
    if (f) {
        size_t n = f.readBytes(_licenseBuf, sizeof(_licenseBuf) - 1);
        _licenseBuf[n] = '\0';
        f.close();
    } else {
        snprintf(_licenseBuf, sizeof(_licenseBuf),
            "License file not installed.\n\n"
            "Upload via web UI (/files):\n%s\n\n"
            "MIT License - SIMUT v3", path);
    }
}

static int wrapLineCount(const char* text, int maxCols) {
    int lines = 1;
    int col = 0;
    while (*text) {
        if (*text == '\n') { lines++; col = 0; text++; continue; }
        if (*text == ' ')  { if (col > 0 && col < maxCols) col++; text++; continue; }

        int wlen = 0;
        const char* w = text;
        while (*w && *w != ' ' && *w != '\n') { wlen++; w++; }

        if (col > 0 && col + wlen > maxCols) { lines++; col = 0; }
        col += wlen;
        text += wlen;
    }
    return lines;
}


static void renderWrapped(Adafruit_ILI9341* tft, const char* text,
                          int x0, int y0, int maxCols, int lineH,
                          int skip, int maxVis) {
    int curLine = 0;
    int col = 0;
    while (*text) {
        if (curLine >= skip + maxVis) break;
        if (*text == '\n') { curLine++; col = 0; text++; continue; }
        if (*text == ' ')  { if (col > 0 && col < maxCols) col++; text++; continue; }

        char word[52];
        int wlen = 0;
        while (*text && *text != ' ' && *text != '\n' && wlen < 50) {
            word[wlen++] = *text++;
        }
        word[wlen] = '\0';

        if (col > 0 && col + wlen > maxCols) { curLine++; col = 0; }
        if (curLine >= skip + maxVis) break;

        if (curLine >= skip) {
            int sy = y0 + (curLine - skip) * lineH;
            tft->setCursor(x0 + col * 6, sy);
            tft->print(word);
        }
        col += wlen;
    }
}

static DisplayManager* _instance = nullptr;


constexpr int16_t DisplayManager::CAL_SCR_X[4];
constexpr int16_t DisplayManager::CAL_SCR_Y[4];

DisplayManager::DisplayManager() {
    _instance = this;
    mutex_init(&_stateMutex);
    queue_init(&_eventQueue, sizeof(UiEvent), 10);
    _sharedState.ambientTemp = NAN;
    _sharedState.ambientHum = NAN;
    _sharedState.ambientValid = true;
    _sharedState.slotTemp = NAN;
    _sharedState.slotValid = false;
    _sharedState.selectedSlotIdx = 0;
    _sharedState.wifiRssi = -100;
    _sharedState.btActive = false;
    _sharedState.isBooting = true;
    _sharedState.showSkipButton = false;
    _sharedState.apProgressPct = -1;
    for(int i = 0; i < 5; i++) strcpy(_sharedState.bootLogs[i], "");
    strcpy(_sharedState.timeString, "--/-- --:--");
    strcpy(_sharedState.slotName, "Sensor 1");
    _lastRenderedState.isBooting = false;
    _lastRenderedState.apProgressPct = -2;
    _lastRenderedState.selectedSlotIdx = -1;
    _isDirty = true;
    _currentPage = 0;
    _lastTouchTime = 0;
    _btnHoldStartTime = 0;
    _lastPressedBtn = -1;
    _menuSelection = 0;
    _isPausedForFlash = false;
    _lastHeartbeat = millis();
    _uiMode = MODE_DASHBOARD;
    _webBusyUser[0] = '\0';
    _repaintGraph = false;
    _repaintLoading = false;
    _loadingDrawn = false;
    _themeChanged = false;
    _forceFullRedraw = false;
    _rawTouchState = false;
    _skipPressed = false;
}

void DisplayManager::begin() {}

void DisplayManager::startCore1() { multicore_launch_core1(core1Entry); }

void DisplayManager::restartCore1() {
    multicore_reset_core1();
    delay(50);
    mutex_init(&_stateMutex);
    _isPausedForFlash = false;
    _lastHeartbeat = millis();
    multicore_launch_core1(core1Entry);
}

void DisplayManager::setLanguage(int langId) {
    if (langId >= 0 && langId < LANG_COUNT) _currentLangIdx = langId;
    else _currentLangIdx = 1;
    /* REF/F17: carrega license do FS para _licenseBuf. Chamada apenas no
     * Core 0 (boot via setup, ou EVT_APPLY_LANG via AppManager). LittleFS
     * acesso seguro nesses contextos. */
    loadLicenseFromFs(_currentLangIdx);
}



/**
 * @brief Trunca um texto para caber em maxPixelW pixels na fonte atual do GFX.
 *
 * Se o texto original já cabe, é copiado integralmente para out.
 * Caso contrário, remove caracteres do final e acrescenta "..." de modo
 * que o resultado caiba na largura máxima. A fonte já deve estar setada
 * no contexto GFX antes da chamada.
 */
void DisplayManager::truncateText(Adafruit_GFX* gfx, const char* src,
                                  char* out, size_t outSize, int16_t maxPixelW) {
    if (!gfx || !src || !out || outSize < 4) {
        if (out && outSize > 0) out[0] = '\0';
        return;
    }

    /* Medir largura do texto original */
    int16_t bx, by;
    uint16_t tw, th;
    gfx->getTextBounds(src, 0, 0, &bx, &by, &tw, &th);

    /* Se cabe inteiro, copia e retorna */
    if ((int16_t)tw <= maxPixelW) {
        strncpy(out, src, outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }

    /* Medir largura das reticências */
    uint16_t ellW, ellH;
    gfx->getTextBounds("...", 0, 0, &bx, &by, &ellW, &ellH);
    int16_t targetW = maxPixelW - (int16_t)ellW;
    if (targetW < 0) targetW = 0;

    /* Busca binária do comprimento máximo que cabe */
    int srcLen = (int)strlen(src);
    int lo = 0, hi = srcLen;
    int best = 0;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        /* Monta string candidata no buffer */
        int copyLen = mid;
        if (copyLen > (int)(outSize - 4)) copyLen = (int)(outSize - 4);
        memcpy(out, src, copyLen);
        out[copyLen] = '\0';

        gfx->getTextBounds(out, 0, 0, &bx, &by, &tw, &th);

        if ((int16_t)tw <= targetW) {
            best = copyLen;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    /* Remove espaços finais antes das reticências */
    while (best > 0 && out[best - 1] == ' ') best--;

    /* Monta resultado final */
    memcpy(out, src, best);
    out[best]     = '.';
    out[best + 1] = '.';
    out[best + 2] = '.';
    out[best + 3] = '\0';
}

bool DisplayManager::isMenuActive() {
    mutex_enter_blocking(&_stateMutex);
    bool active = (_uiMode >= MODE_AUTH);
    mutex_exit(&_stateMutex);
    return active;
}


bool DisplayManager::isDisplayBusy() {
    mutex_enter_blocking(&_stateMutex);
    bool busy = (_uiMode != MODE_DASHBOARD);
    mutex_exit(&_stateMutex);
    return busy;
}


bool DisplayManager::isHeavyRendering() {
    mutex_enter_blocking(&_stateMutex);
    bool heavy = (_uiMode == MODE_GRAPH_LOADING || _uiMode == MODE_GRAPH_VIEW
                  || _uiMode == MODE_GRAPH_DETAIL);
    mutex_exit(&_stateMutex);
    return heavy;
}

void DisplayManager::pauseRendering(bool pause) {

    if (!_core1Ready) return;
    if (pause) {

        int32_t prev = __atomic_fetch_add(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);
        if (prev == 0) {
            _pauseStartTime = millis();
            LogManager::instance().setCorePaused(1, true);

            /*
             * U23 (2026-04-19): timeout-based lockout em vez de blocking
             * eterno. Autópsia consistente mostrava C0=[CORE1_LOCK] — Core 0
             * preso em `multicore_lockout_start_blocking` sem feed de WDT.
             * Possível causa: lockout_mutex do SDK ficou preso de um
             * start/end desbalanceado (ex: restartCore1 mid-lockout).
             *
             * Fix: loop com `start_timeout_us(500ms)` + `watchdog_update`
             * entre tentativas. Se 5s passou sem sucesso, chama
             * `end_blocking` pra limpar estado interno (incrementa
             * lockout_request_id, libera mutex se travado) e reinicia.
             * Retry forever — prefiro sistema "lento" visivelmente a
             * reboot com autópsia truncada.
             */
            uint32_t retryStart = millis();
            uint32_t lastCleanup = retryStart;
            while (!multicore_lockout_start_timeout_us(500000)) {
                watchdog_update();
                if (timeSince(lastCleanup, 2000)) {
                    /* Lockout state possivelmente corrompido: limpa antes
                     * de nova tentativa. end_blocking é idempotente se
                     * mutex já foi liberado. */
                    multicore_lockout_end_blocking();
                    lastCleanup = millis();
                    watchdog_update();
                }
                /* Reduzido de 60s → 10s: user não deve esperar mais que
                 * isso por um save. Após 10s sem sucesso, assume Core 1
                 * morto e reinicia ele antes de seguir. */
                if (timeSince(retryStart, 10000)) {
                    Serial.println("[DSP] Lockout stuck >10s, restarting Core 1");
                    __atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
                    LogManager::instance().setCorePaused(1, false);
                    multicore_reset_core1();
                    delay(50);
                    multicore_launch_core1(core1Entry);
                    return;
                }
            }
        }
    } else {
        int32_t prev = __atomic_fetch_sub(&_pauseRefCount, 1, __ATOMIC_ACQ_REL);
        if (prev <= 1) {

            __atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
            _pauseStartTime = 0;
            multicore_lockout_end_blocking();
            LogManager::instance().setCorePaused(1, false);
        }
    }
}


void DisplayManager::forceUnpause() {
    int32_t prev = __atomic_load_n(&_pauseRefCount, __ATOMIC_ACQUIRE);
    if (prev > 0) {
        LOG_CODE(LOG_ERROR, "DSP", DSP_FORCE_UNPAUSE, prev, String(TRL("forceUnpause: refCount=", "forceUnpause: refCount=")) + prev);
        __atomic_store_n(&_pauseRefCount, 0, __ATOMIC_RELEASE);
        _pauseStartTime = 0;
        multicore_lockout_end_blocking();
        LogManager::instance().setCorePaused(1, false);
    }
}

uint32_t DisplayManager::getHeartbeat() {
    if (_isPausedForFlash) return millis();
    return _lastHeartbeat;
}

void DisplayManager::refreshTheme() { _themeChanged = true; }




/* ─────────────────────────────────────────────────────────────────────────── */
/*                        CALENDÁRIO DE HISTÓRICO                            */
/* ─────────────────────────────────────────────────────────────────────────── */

void DisplayManager::setBootStatus(String msg, bool showSkip) {
    mutex_enter_blocking(&_stateMutex);
    if (msg.length() > 0) {
        for (int i = 0; i < 4; i++) strcpy(_sharedState.bootLogs[i], _sharedState.bootLogs[i+1]);
        safeCopy(_sharedState.bootLogs[4], msg.c_str(), sizeof(_sharedState.bootLogs[4]));
    }
    _sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::replaceBootStatus(String msg, bool showSkip) {
    mutex_enter_blocking(&_stateMutex);
    if (msg.length() > 0) {
        safeCopy(_sharedState.bootLogs[4], msg.c_str(), sizeof(_sharedState.bootLogs[4]));
    }
    _sharedState.showSkipButton = showSkip; _sharedState.isBooting = true; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setApProgress(int pct) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.apProgressPct = pct; _sharedState.isBooting = true; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::endBoot() {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.isBooting = false; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::forceDashboard() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true;
    mutex_exit(&_stateMutex);
}

bool DisplayManager::isSkipPressed() {
    if (_skipPressed) { _skipPressed = false; return true; }
    return false;
}

bool DisplayManager::isScreenTouched() { return _rawTouchState; }


void DisplayManager::setWebBusy(bool busy, const char* username) {
    mutex_enter_blocking(&_stateMutex);
    if (busy) {
        if (username) safeCopy(_webBusyUser, username, sizeof(_webBusyUser));
        else safeCopy(_webBusyUser, "web", sizeof(_webBusyUser));
        _webBusy = true;
    } else {
        _webBusy = false;
    }
    mutex_exit(&_stateMutex);
}

void DisplayManager::setAmbientData(float t, float h, bool isValid) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.ambientTemp = t; _sharedState.ambientHum = h; _sharedState.ambientValid = isValid; _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setAmbientMinMax(float minT, float maxT, float minH, float maxH) {
    _ambMinTemp = minT;
    _ambMaxTemp = maxT;
    _ambMinHum  = minH;
    _ambMaxHum  = maxH;
}

void DisplayManager::setSlotData(float t, bool isValid, int slotIdx, String name) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.slotTemp = t; _sharedState.slotValid = isValid; _sharedState.selectedSlotIdx = slotIdx;
    safeCopy(_sharedState.slotName, name.c_str(), sizeof(_sharedState.slotName)); _isDirty = true;
    mutex_exit(&_stateMutex);
}

void DisplayManager::setSlotMinMax(float minT, float maxT) {
    _slotMinTemp = minT;
    _slotMaxTemp = maxT;
}

void DisplayManager::setSystemStatus(int rssi, bool bt, String timeStr) {
    mutex_enter_blocking(&_stateMutex);
    _sharedState.wifiRssi = rssi; _sharedState.btActive = bt;
    safeCopy(_sharedState.timeString, timeStr.c_str(), sizeof(_sharedState.timeString)); _isDirty = true;
    mutex_exit(&_stateMutex);
}




bool DisplayManager::getUiEvent(UiEvent& ev) { return queue_try_remove(&_eventQueue, &ev); }
void DisplayManager::core1Entry() { if (_instance) _instance->loopCore1(); }

/* F-LOCKOUT-STUCK fix v3.24.9: abordagem HARD-RESET (substitui a cooperativa
 * cujo handshake falhava com Core 1 em estados inesperados).
 *
 * Fluxo: Core 0 faz `multicore_reset_core1()` → Core 1 para imediatamente
 * (instrução parada, SPI em idle, IRQs mortas). Core 0 faz todas as flash
 * ops sem conflito porque Core 1 está LITERALMENTE desligado. Ao fim,
 * Core 0 re-inicia via `multicore_launch_core1` → Core 1 executa core1Entry
 * fresh, re-inicializa TFT/canvases e redesenha a tela inteira.
 *
 * Vantagem: determinístico, sem timeout, sem handshake, sem dependência do
 * estado de Core 1. Match exato com a descrição do user: "core 1 congela,
 * core 0 faz trabalho, libera e reinicia toda a tela".
 *
 * Trade-off: Core 1 leva ~500ms-2s para re-inicializar TFT e desenhar a
 * primeira frame pós-resume. Aceitável — durante o reset, o TFT retém a
 * última frame (memória do controlador ILI9341), então o user vê o último
 * estado (normalmente "Aplicando configuração...") até o novo render.
 *
 * _runQuietLoop não é mais usado (mantido como stub em caso de referência
 * orfã; será removido em bump futuro). */
void __not_in_flash_func(DisplayManager::_runQuietLoop)() {
    /* Não chamado mais — hard reset substitui. */
}

/* Core 0 API: HARD-RESET de Core 1. RE-ENTRANT via refcount. Apenas o
 * primeiro caller (refcount 0→1) executa o reset efetivo; chamadas aninhadas
 * incrementam e retornam true imediatamente. */
bool DisplayManager::requestQuietMode(uint32_t /*timeoutMs*/) {
    int32_t prev = __atomic_fetch_add(&_quietModeRefCount, 1, __ATOMIC_ACQ_REL);
    if (prev > 0) {
        /* Já em quiet mode — caller externo segura. */
        return true;
    }
    /* First level: hard-reset Core 1. Para imediatamente; flash work seguro. */
    multicore_reset_core1();
    delay(50);  /* Pequena pausa para SSI/SPI estabilizarem. */
    /* Core 1 agora está morto. Marca flags para consumers:
     *  - _core1Ready = false: pauseRendering vira no-op (sem lockout IRQ).
     *  - _quietModeActive = true: isInQuietMode() retorna true. */
    __atomic_store_n(&_core1Ready,       false, __ATOMIC_RELEASE);
    __atomic_store_n(&_quietModeActive,  true,  __ATOMIC_RELEASE);
    __atomic_store_n(&_pauseRefCount,    0,     __ATOMIC_RELEASE);
    _isPausedForFlash = false;
    LogManager::instance().setCorePaused(1, true);
    return true;
}

/* Core 0 API: re-launch Core 1 após o trabalho em flash. Apenas o último
 * release (refcount → 0) faz o launch efetivo. */
void DisplayManager::releaseQuietMode() {
    int32_t prev = __atomic_fetch_sub(&_quietModeRefCount, 1, __ATOMIC_ACQ_REL);
    if (prev > 1) {
        /* Outro caller externo ainda segura — não re-lança. */
        return;
    }
    if (prev <= 0) {
        __atomic_store_n(&_quietModeRefCount, 0, __ATOMIC_RELEASE);
        return;
    }
    /* Re-inicializa mutex (Core 1 pode ter sido resetado segurando-o) e
     * zera flags de pausa. Core 1 novo redesenhará tudo no core1Entry. */
    mutex_init(&_stateMutex);
    __atomic_store_n(&_pauseRefCount,   0,     __ATOMIC_RELEASE);
    _isPausedForFlash = false;
    _lastHeartbeat = millis();
    __atomic_store_n(&_quietModeActive, false, __ATOMIC_RELEASE);
    LogManager::instance().setCorePaused(1, false);
    multicore_launch_core1(core1Entry);
    /* Core 1 setará _core1Ready=true após victim_init em loopCore1. */
}

void DisplayManager::loopCore1() {

    multicore_lockout_victim_init();
    _core1Ready = true;

    /* F-LOCKOUT-STUCK v3.24.10-11: alocações de heap preservadas entre resets.
     * Touch DEVE ser re-inicializado a cada launch — attachInterrupt liga
     * handler na NVIC de Core 1, que é zerada no multicore_reset. Sem
     * re-init, touch para de responder após o 1º save.
     * TFT begin() faz HW reset do ILI9341 (flash branco visível); pulado
     * em launches subsequentes. */
    if (!_tft) _tft = new TftWithOffset(TFT_CS, TFT_DC, TFT_RST);
    if (!_ts)  _ts  = new XPT2046_Touchscreen(TOUCH_CS, TOUCH_IRQ);
    if (!_canvasWide)  _canvasWide  = new GFXcanvas16(320, 45);
    if (!_canvasSmall) _canvasSmall = new GFXcanvas16(140, 40);

    /* Touch: re-atacha IRQ a cada launch (NVIC de Core 1 foi zerada). */
    _ts->begin();
    _ts->setRotation(3);

    if (_tftFirstInit) {
        _tft->begin();
        _tft->setRotation(3);
        _tft->fillScreen(C_BG_MAIN);
        if (!_sharedState.isBooting) drawInterfaceFixed();
        _lastRenderedState.selectedSlotIdx = -1;
        _tftFirstInit = false;
    } else {
        /* Post-reset resume: TFT retém última frame (memória do ILI9341).
         * Força delta render na próxima iteração para atualizar dados. */
        mutex_enter_blocking(&_stateMutex);
        _isDirty = true;
        mutex_exit(&_stateMutex);
    }

    SystemState currentSnapshot;

    while (true) {
        /* F-LOCKOUT-STUCK v3.24.9: abordagem cooperativa removida (não era
         * confiável — Core 1 ficava não-responsivo por >15s em cenários de
         * flash GC + render pesado). Agora Core 0 usa `multicore_reset_core1`
         * para HARD-RESETAR Core 1 antes de flash writes. Este loop só
         * executa se Core 1 estiver ativo. */

        TRACE_MOD(1, MOD_DISPLAY);
        TRACE_BEAT(1);

        _lastHeartbeat = millis();
        _rawTouchState = _ts->touched();

        /* Processa toque ANTES da renderização para resposta no mesmo frame */
        handleTouch();

        if (_themeChanged) {
            SystemState snap;
            mutex_enter_blocking(&_stateMutex);
            snap = _sharedState;
            mutex_exit(&_stateMutex);

            if (!snap.isBooting) {
                _tft->fillScreen(C_BG_MAIN);
                _tft->setFont(&simutFont12pt);
                _tft->setTextColor(C_TEXT_MAIN);
                int16_t x1, y1; uint16_t w, h;
                String msg = tr(TR_APPLYING_THEME);
                _tft->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
                _tft->setCursor(160 - (w/2), 127);
                _tft->print(msg);
                delay(200);

                mutex_enter_blocking(&_stateMutex);
                snap = _sharedState;
                _isDirty = false;
                mutex_exit(&_stateMutex);

                drawInterfaceFixed();
                drawTopBar(snap);
                drawAmbientPanel(snap.ambientTemp, snap.ambientHum, snap.ambientValid);
                drawSlotPanel(snap.slotTemp, snap.slotValid, snap.selectedSlotIdx, snap.slotName, true);
                drawBottomButtons(snap.selectedSlotIdx, true);
                _lastRenderedState = snap;
                _uiMode = MODE_DASHBOARD;
            } else {
                _tft->fillScreen(C_BG_MAIN);
                _lastRenderedState.isBooting = false;

                mutex_enter_blocking(&_stateMutex);
                _isDirty = true;
                mutex_exit(&_stateMutex);
            }
            _themeChanged = false;
        }

        if (_uiMode == MODE_DASHBOARD) {

            /* BUG-004: fallback para _lastWebBusy em vez de false quando
             * mutex_try_enter falha — evita flicker do overlay. */
            bool webBusyNow = _lastWebBusy;
            if (mutex_try_enter(&_stateMutex, NULL)) {
                webBusyNow = _webBusy;
                _lastWebBusy = webBusyNow;
                mutex_exit(&_stateMutex);
            }


            if (_alarmNavPending >= 0) {
                int8_t navTarget = _alarmNavPending;
                _alarmNavPending = -1;
                if      (navTarget < 4) _currentPage = 0;
                else if (navTarget < 8) _currentPage = 1;
                else                    _currentPage = 2;
                _alarmRotateTimer = millis();
                mutex_enter_blocking(&_stateMutex);
                _isDirty = true;
                mutex_exit(&_stateMutex);
            }


            if (_alarmSlotMask != 0 && !_alarmSilenced) {
                uint16_t m = _alarmSlotMask;
                int alarmCount = 0;
                while (m) { alarmCount += (m & 1); m >>= 1; }

                if (alarmCount >= 2 && timeSince(_alarmRotateTimer, ALARM_ROTATE_INTERVAL_MS)) {
                    _alarmRotateTimer = millis();
                    int current = _lastRenderedState.selectedSlotIdx;
                    for (int i = 1; i <= 10; i++) {
                        int idx = (current + i) % 10;
                        if (_alarmSlotMask & (1 << idx)) {
                            if      (idx < 4) _currentPage = 0;
                            else if (idx < 8) _currentPage = 1;
                            else              _currentPage = 2;
                            UiEvent ev;
                            ev.type = UiEvent::EVT_SLOT_SELECT;
                            ev.id   = idx;
                            queue_try_add(&_eventQueue, &ev);
                            break;
                        }
                    }
                }
            }


            if (isAnyAlarmActive()) {
                uint32_t now = millis();
                if (now - _alarmFlashTimer >= ALARM_FLASH_INTERVAL_MS) {
                    _alarmFlashTimer = now;
                    _alarmFlashPhase = !_alarmFlashPhase;
                    if (!_webOverlayShown) {
                        redrawAlarmFlash();
                    }
                }
            } else if (_alarmFlashPhase) {

                _alarmFlashPhase  = false;
                _alarmFlashTimer  = 0;
                _alarmRotateTimer = 0;
                if (!_webOverlayShown) {
                    restoreNormalDashboard();
                }
            }


            if (_webOverlayShown) {
                if (!webBusyNow) {
                    _webOverlayShown = false;
                    _forceFullRedraw = true;
                    _isDirty = true;

                    if (pullSnapshot(currentSnapshot)) render(currentSnapshot);
                }

            } else {
                if (pullSnapshot(currentSnapshot)) render(currentSnapshot);
            }
        }
        else if (_uiMode == MODE_GRAPH_LOADING) {
            if (_repaintLoading) { drawLoadingScreen(); _repaintLoading = false; }
        }
        else if (_uiMode == MODE_STATS_VIEW) {
            if (_repaintGraph) { drawStatsScreen(); _repaintGraph = false; }
        }
        else if (_uiMode == MODE_GRAPH_VIEW) {
            if (_repaintGraph) { drawGraphScreen(); _repaintGraph = false; }
        }
        else if (_uiMode == MODE_GRAPH_DETAIL) {
            if (_repaintGraph) { drawGraphDetailScreen(); _repaintGraph = false; }
        }
        else if (_uiMode == MODE_CALENDAR) {
            if (_repaintCalendar) { drawCalendarScreen(); _repaintCalendar = false; }
        }

        /* ── Reverte header para data/hora após 3s de exibição do nome ── */
        if ((_uiMode == MODE_GRAPH_VIEW || _uiMode == MODE_GRAPH_DETAIL)
            && _headerShowName
            && timeSince(_headerNameTimer, 3000))
        {
            _headerShowName = false;
            drawGraphHeaderBar();
        }
        else if (_uiMode == MODE_SETTINGS_THEMES) {
            if (_repaintSettings) { drawSettingsThemes(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_ALARMS) {
            if (_repaintSettings) { drawSettingsAlarms(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_ALARM_EDIT) {
            if (_repaintSettings) { drawAlarmEdit(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_AUTH) {
            if (_permanentLockout) {
                /* Wrap-safe: comparação direta com millis() falha no wrap a cada ~49,7d. */
                if (timeReached(_lockoutUntil)) forceDashboard();
            } else if (_lockoutUntil > 0) {
                if (!timeReached(_lockoutUntil)) _repaintSettings = true;
                else { _lockoutUntil = 0; _forceSettingsRedraw = true; _repaintSettings = true; }
            }
            if (_repaintSettings) { drawAuthScreen(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_MAIN) {
            if (_repaintSettings) { drawSettingsMain(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_LANG) {
            if (_repaintSettings) { drawSettingsLang(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_PASSWORD) {
            if (_repaintSettings) { drawSettingsPassword(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
            if (_repaintSettings) { drawTouchCalibration(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_TOUCH_SENS) {
            if (_repaintSettings) { drawTouchSensitivity(); _repaintSettings = false; }
            /* Após 1.5s da conclusão, avança para calibração de posição */
            if (_sensDone && timeSince(_sensDoneTime, 1500)) {
                _uiMode = MODE_SETTINGS_TOUCH_CAL;
                _calStep = 0;
                _calPhase = 0;
                _forceSettingsRedraw = true;
                _repaintSettings = true;
            }
        }
        else if (_uiMode == MODE_SETTINGS_SOUNDS) {

            if (_repaintSettings) {
                if (_inMelodySelect) drawMelodySelect();
                else                 drawSettingsSounds();
                _repaintSettings = false;
            }
        }
        else if (_uiMode == MODE_SETTINGS_STATUS) {
            /* Renderiza a cada 1 segundo ou quando forçado */
            if (_repaintSettings || timeSince(_statusLastDraw, 1000)) {
                drawSystemStatus();
                _repaintSettings = false;
            }
        }
        else if (_uiMode == MODE_SETTINGS_LICENSE) {
            if (_repaintSettings) { drawSettingsLicense(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_SETTINGS_DISPLAY_OFFSET) {
            if (_repaintSettings) { drawSettingsDisplayOffset(); _repaintSettings = false; }
        }
        else if (_uiMode == MODE_ALARM_ACTION) {

            if (_repaintSettings) { drawAlarmAction(); _repaintSettings = false; }
        }

        /*
         * Delay adaptativo: mínimo durante interação, maior quando ocioso.
         * - Toque ativo ou repaint pendente: 1ms (máxima responsividade)
         * - Idle: 5ms (economia de CPU para Core 0)
         */
        bool touchActive = _rawTouchState;
        bool repaintPending = _isDirty || _repaintGraph || _repaintSettings || _repaintLoading;
        delay(touchActive || repaintPending ? 1 : 2);
    }
}

bool DisplayManager::pullSnapshot(SystemState& localSnapshot) {
    bool updated = false;


    if (mutex_enter_timeout_us(&_stateMutex, 1000)) {
        if (_isDirty) {
            localSnapshot = _sharedState;
            _isDirty = false;
            updated = true;
        }
        mutex_exit(&_stateMutex);
    }
    return updated;
}

void DisplayManager::render(const SystemState& state) {
    if (state.isBooting) {
        bool fullRedraw = (_lastRenderedState.isBooting == false) || (_lastRenderedState.apProgressPct != state.apProgressPct);
        if (state.apProgressPct >= 0) {
            if (fullRedraw) _tft->fillScreen(C_BG_MAIN);
            _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
            _tft->setCursor(55, 120); _tft->print(tr(TR_AP_MODE));
            _tft->drawRoundRect(40, 140, 240, 20, 6, C_TEXT_SUB);
            int wBar = map(state.apProgressPct, 0, 100, 0, 236);
            if (wBar > 0) {
                _tft->fillRoundRect(42, 142, wBar, 16, 4, C_ACCENT);
            }
            _lastRenderedState = state;
            return;
        }

        int boxY = 105;

        if (fullRedraw) {
            _tft->fillScreen(C_BG_MAIN);
            _tft->setFont(&simutFont24pt); _tft->setTextColor(C_TEXT_MAIN);
            int16_t x1, y1; uint16_t w, h;
            _tft->getTextBounds("SIMUT", 0, 0, &x1, &y1, &w, &h);
            _tft->setCursor((320 - w) / 2, 60); _tft->print("SIMUT");
            _tft->setFont(&simutFont9pt); _tft->setTextColor(C_ACCENT);
            _tft->getTextBounds(SIMUT_VERSION, 0, 0, &x1, &y1, &w, &h);
            _tft->setCursor((320 - w) / 2, 85); _tft->print(SIMUT_VERSION);

            _tft->fillRoundRect(10, boxY, 300, 80, 8, C_CARD_BG);
            _tft->drawRoundRect(10, boxY, 300, 80, 8, C_TEXT_OFF);
            _tft->setFont(NULL);
            _tft->setTextSize(1);
            _tft->setTextColor(C_ACCENT_HIGH, C_CARD_BG);
            _tft->setCursor(20, boxY + 8);
            _tft->print("> system_init()                       ");
        }
        _tft->setFont(NULL);
        _tft->setTextSize(1);
        _tft->setTextColor(C_TEXT_SUB, C_CARD_BG);
        for(int i=0; i<5; i++) {
            _tft->setCursor(20, boxY + 22 + (i*10));
            String logLine = state.bootLogs[i];
            while(logLine.length() < 46) logLine += " ";
            _tft->print(logLine);
        }

        if (state.showSkipButton) {
            _tft->fillRoundRect(80, 195, 160, 35, 8, C_ACCENT_HIGH);
            _tft->setFont(&simutFont9pt); _tft->setTextColor(C_BG_MAIN);
            int16_t x1, y1; uint16_t w, h;
            const char* skipLabel = tr(TR_SKIP);
            _tft->getTextBounds(skipLabel, 0, 0, &x1, &y1, &w, &h);
            _tft->setCursor(80 + (160 - w)/2, 218); _tft->print(skipLabel);
        } else if (fullRedraw) {
            _tft->fillRect(80, 195, 160, 35, C_BG_MAIN);
        }

        _lastRenderedState = state;
        return;
    }
    if (_lastRenderedState.isBooting && !state.isBooting) {


        _forceFullRedraw = true;
    }

    bool full = _forceFullRedraw;
    if (full) {
        drawInterfaceFixed();
        drawTopBar(state);


        drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        drawSlotPanel(state.slotTemp, state.slotValid, state.selectedSlotIdx, state.slotName, true);
        drawBottomButtons(state.selectedSlotIdx, true);
        _forceFullRedraw = false;
        _lastRenderedState = state;
        return;
    }

    /* BUG-002: barrier antes de ler _pktArrowState (publicado por Core 0
     * junto com as vars de flash em setTelemetrySendStatus). */
    __dmb();
    if (state.wifiRssi != _lastRenderedState.wifiRssi ||
        state.btActive != _lastRenderedState.btActive ||
        strcmp(state.timeString, _lastRenderedState.timeString) != 0 ||
        _webNotifyStartMs > 0 ||
        _alarmSilenced ||
        _pktArrowState == 3) {
        drawTopBar(state);
    }

    if (!_ambientShowMinMax) {
        if (abs(state.ambientTemp - _lastRenderedState.ambientTemp) > 0.01 ||
            abs(state.ambientHum - _lastRenderedState.ambientHum) > 0.01 ||
            state.ambientValid != _lastRenderedState.ambientValid) {
            drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        }
    }

    /* Retornar painéis ao modo normal após 30s sem toque */
    if ((_ambientShowMinMax || _slotShowMinMax) &&
        timeSince(_lastTouchTime, 30000)) {
        if (_ambientShowMinMax) {
            _ambientShowMinMax = false;
            drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        }
        if (_slotShowMinMax) {
            _slotShowMinMax = false;
            drawSlotPanel(state.slotTemp, state.slotValid,
                          state.selectedSlotIdx, state.slotName, true);
        }
    }

    bool slotChanged = (state.selectedSlotIdx != _lastRenderedState.selectedSlotIdx);
    bool nameChanged = (strcmp(state.slotName, _lastRenderedState.slotName) != 0);
    bool tempChanged = (abs(state.slotTemp - _lastRenderedState.slotTemp) > 0.01) || (state.slotValid != _lastRenderedState.slotValid);

    if (slotChanged || nameChanged || (!_slotShowMinMax && tempChanged)) {
        if (slotChanged) {
             drawBottomButtons(state.selectedSlotIdx, false);
        }

        drawSlotPanel(state.slotTemp, state.slotValid, state.selectedSlotIdx, state.slotName, (slotChanged || nameChanged));
    }

    /* Detectar mudança de estado de alarme e redesenhar botões + painéis */
    if (_alarmSlotMask != _prevAlarmSlotMask ||
        _alarmAmbientTemp != _prevAlarmAmbTemp ||
        _alarmAmbientHum  != _prevAlarmAmbHum) {
        drawBottomButtons(state.selectedSlotIdx, true);
        if (!_ambientShowMinMax) {
            drawAmbientPanel(state.ambientTemp, state.ambientHum, state.ambientValid);
        }
        if (!_slotShowMinMax) {
            drawSlotPanel(state.slotTemp, state.slotValid,
                          state.selectedSlotIdx, state.slotName, true);
        }
        _prevAlarmSlotMask = _alarmSlotMask;
        _prevAlarmAmbTemp  = _alarmAmbientTemp;
        _prevAlarmAmbHum   = _alarmAmbientHum;
    }

    _lastRenderedState = state;
}







void DisplayManager::setWebNotification(const char* username) {
    if (!username) return;
    safeCopy(_webNotifyUser, username, sizeof(_webNotifyUser));
    _webNotifyUser[sizeof(_webNotifyUser) - 1] = '\0';
    _webNotifyStartMs = millis();
    if (_webNotifyStartMs == 0) _webNotifyStartMs = 1;
}


/* =========================================================================== */
/*                   STATUS DO SISTEMA EM TEMPO REAL                         */
/* =========================================================================== */


void DisplayManager::showSettingsLicense() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_LICENSE;
    _licensePage = 0;
    _forceSettingsRedraw = true;
    _repaintSettings = true;
    mutex_exit(&_stateMutex);
}


void DisplayManager::drawSettingsLicense() {
    bool fullRedraw = _forceSettingsRedraw;

    /* REF/F17: licText agora vem de _licenseBuf (carregado em setLanguage
     * pelo Core 0). Fallback é gerado no próprio _licenseBuf se FS missing. */
    const char* licText = _licenseBuf;

    const int MAX_COLS  = 50;
    const int LINE_H    = 9;
    const int TEXT_Y0    = 36;
    const int MAX_VIS    = 17;

    /* Contar linhas totais (licença + acknowledgments já integrados) */
    int totalLines = wrapLineCount(licText, MAX_COLS);

    /* Calcular total de páginas */
    _licenseTotalPages = (totalLines + MAX_VIS - 1) / MAX_VIS;
    if (_licenseTotalPages < 1) _licenseTotalPages = 1;
    if (_licensePage >= _licenseTotalPages) _licensePage = _licenseTotalPages - 1;
    if (_licensePage < 0) _licensePage = 0;

    if (fullRedraw) {
        _tft->fillScreen(C_BG_MAIN);

        /* Header com título e contador de páginas */
        _tft->fillRect(4, 4, 312, 32, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_MAIN);
        _tft->setCursor(10, 22); _tft->print(tr(TR_LICENSE_TITLE));

        char pgBuf[8];
        snprintf(pgBuf, sizeof(pgBuf), "%d/%d", _licensePage + 1, _licenseTotalPages);
        int16_t px, py; uint16_t pw, ph;
        _tft->getTextBounds(pgBuf, 0, 0, &px, &py, &pw, &ph);
        _tft->setTextColor(C_TEXT_SUB);
        _tft->setCursor(310 - (int)pw, 22); _tft->print(pgBuf);

        /* Botões inferiores */
        int btnY = 195; int btnH = 40; int16_t bx, by; uint16_t bw, bh;

        _tft->fillRoundRect(5, btnY, 100, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(55, btnY + 12, 45, btnY + 26, 65, btnY + 26, C_TEXT_MAIN);

        _tft->fillRoundRect(110, btnY, 100, btnH, 8, C_CARD_BG);
        _tft->fillTriangle(160, btnY + 26, 150, btnY + 12, 170, btnY + 12, C_TEXT_MAIN);

        _tft->fillRoundRect(215, btnY, 100, btnH, 8, C_ACCENT);
        _tft->setTextColor(C_BG_MAIN);
        String backTxt = tr(TR_BACK);
        _tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
        _tft->setCursor(215 + (100 - bw) / 2, btnY + 25); _tft->print(backTxt);
    }

    /* Limpar área de texto */
    _tft->fillRect(0, TEXT_Y0, 320, MAX_VIS * LINE_H, C_BG_MAIN);
    _tft->setFont(NULL); _tft->setTextSize(1);
    _tft->setTextColor(C_TEXT_SUB);

    /* Renderizar página atual */
    int startLine = _licensePage * MAX_VIS;
    renderWrapped(_tft, licText, 10, TEXT_Y0, MAX_COLS, LINE_H,
                  startLine, MAX_VIS);

    /* Indicador de páginas (dots) */
    {
        int dotY = TEXT_Y0 + MAX_VIS * LINE_H + 2;
        int dotSpacing = 10;
        int dotsWidth  = (_licenseTotalPages - 1) * dotSpacing;
        int dotX0      = (320 - dotsWidth) / 2;
        for (int i = 0; i < _licenseTotalPages; i++) {
            int cx = dotX0 + i * dotSpacing;
            if (i == _licensePage) {
                _tft->fillCircle(cx, dotY, 3, C_ACCENT);
            } else {
                _tft->fillCircle(cx, dotY, 2, C_TEXT_OFF);
            }
        }
    }

    /* Atualizar contador no header (sem redesenhar tudo) */
    if (!fullRedraw) {
        _tft->fillRect(240, 6, 75, 22, C_CARD_BG);
        _tft->setFont(&simutFont9pt); _tft->setTextColor(C_TEXT_SUB);
        char pgBuf[8];
        snprintf(pgBuf, sizeof(pgBuf), "%d/%d", _licensePage + 1, _licenseTotalPages);
        int16_t px, py; uint16_t pw, ph;
        _tft->getTextBounds(pgBuf, 0, 0, &px, &py, &pw, &ph);
        _tft->setCursor(310 - (int)pw, 22); _tft->print(pgBuf);
    }

    _forceSettingsRedraw = false;
}
