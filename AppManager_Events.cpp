/**
 * @file    AppManager_Events.cpp
 * @brief   UI event dispatch: touch, graph, calendar, settings, alarms.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "AppManager.h"
#include "LogManager.h"
#include "SystemDefs.h"
#include "Themes.h"
#include <time.h>

void AppManager::core0Yield() {
    static bool _isRenderingGraph = false;
    static bool _inYield = false;
    static uint32_t _yieldEntryTime = 0;

    /* Safety: reseta guard se preso há >10s (crash parcial) */
    if (_inYield && timeSince(_yieldEntryTime, 10000)) {
        _inYield = false;
        _isRenderingGraph = false;
        LOG_CODE(LOG_WARN, "APP", APP_YIELD_STUCK, 0, TRL("Yield stuck >10s, force reset."));
    }

    if (_inYield) return;
    _inYield = true;
    _yieldEntryTime = millis();

    /*
     * WdtWindow context-aware: core0Yield pode ser chamado de dentro de
     * web handlers (via _lightYieldCb). Processa UI events que podem
     * disparar graph preloads (5x renderGraphOptimized, cada um 6s budget).
     * Sem janela aqui, cumulativo cabe só em 15s default — insuficiente
     * para rajadas. 30s cobre sessão típica.
     */
    LogManager::WdtWindow _wdtYield(30000);

    /*
     * Prioridade máxima: processa som de toque ANTES de qualquer
     * outro processamento. Reduz latência do bip de ~50ms para ~5ms.
     */
    if (_displayMgr.consumeTouchSound()) {
        _soundMgr.play(SND_TOUCH_CLICK);
        _soundMgr.update();  /* Executa imediatamente, sem esperar fim do yield */
    }
    if (_displayMgr.consumeErrorSound()) {
        _soundMgr.play(SND_ERROR);
        _soundMgr.update();
    }

    UiEvent uiEv;
    if (!_isRenderingGraph) {
        while (_displayMgr.getUiEvent(uiEv)) {
            if (uiEv.type == UiEvent::EVT_SLOT_SELECT) { _currentSensorIdx = uiEv.id; refreshSelectedSlot(); }
            else if (uiEv.type == UiEvent::EVT_OPEN_GRAPH) {
                if (uiEv.param == 99) openStatsScreen(uiEv.id);
                else {
                    int sensorId = uiEv.id;
                    int range    = uiEv.param;

                    /*
                     * Preserva a âncora se já estamos navegando no passado.
                     * Zoom muda o intervalo mas mantém o ponto final fixo.
                     * Ex: dia 2 em 24H → zoom para 12H → mostra 12:00-23:59 do dia 2.
                     *
                     * Reseta apenas se abrindo um gráfico novo do dashboard
                     * (sensor diferente ou sem âncora prévia).
                     */
                    bool hasAnchor = (_graphAnchorEnd != 0);

                    if (!hasAnchor) {
                        _graphNavOffset = 0;
                        _displayMgr.setGraphNavOffset(0);
                    }

                    /* Sensor diferente do cacheado: invalida cache de ranges */
                    if (_sensorCacheId != sensorId) {
                        for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;
                        _sensorCacheId = sensorId;
                        /* Sensor novo = reset âncora */
                        _graphAnchorEnd = 0;
                        hasAnchor = false;
                        _graphNavOffset = 0;
                        _displayMgr.setGraphNavOffset(0);
                    }

                    _lastGraphRange = range;

                    if (hasAnchor) {
                        /*
                         * Zoom com âncora ativa: usa forceEndEpoch para manter
                         * o fim da janela no mesmo ponto. Não usa cache.
                         */
                        for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;

                        _isRenderingGraph = true;
                        renderGraphOptimized(sensorId, range, true, 0, _graphAnchorEnd);
                        _isRenderingGraph = false;
                    }
                    else if (_sensorCache[range].valid) {
                        /*
                         * Cache hit (sem âncora = visualização "agora").
                         * Verifica staleness para 7D.
                         */
                        bool stale = false;
                        if (range == 4) {
                            time_t age = time(nullptr) - _sensorCache[4].lastRefresh;
                            if (age > 1800) stale = true;
                        }

                        if (!stale) {
                            _displayMgr.showGraphPlot(
                                _sensorCache[range].pkg,
                                _sensorCache[range].humMin,
                                _sensorCache[range].humMax);
                        } else {
                            appendToGraphCache(_sensorCache[4], sensorId);
                            _displayMgr.showGraphPlot(
                                _sensorCache[4].pkg,
                                _sensorCache[4].humMin,
                                _sensorCache[4].humMax);
                        }
                    } else {
                        /*
                         * Cache miss: carrega do flash.
                         */
                        if (range == 4 && !_graphCache[graphCacheIdx(sensorId)].valid) {
                            _displayMgr.requestLoadingScreen();
                            uint32_t waitStart = millis();
                            while (!_displayMgr.isLoadingDrawn() && (millis() - waitStart < 500)) {
                                feedWdt();
                                delay(5);
                            }
                        }

                        _isRenderingGraph = true;
                        renderGraphOptimized(sensorId, range, true, 0);
                        _isRenderingGraph = false;

                        /*
                         * Pré-carrega ranges restantes apenas sem âncora.
                         */
                        if (_graphNavOffset == 0) {
                            _isRenderingGraph = true;
                            preloadSensorRanges(sensorId, range);
                            _isRenderingGraph = false;
                        }
                    }
                }
            }
            /* ── Navegação temporal do gráfico (setas ◀▶) ── */
            else if (uiEv.type == UiEvent::EVT_GRAPH_NAV) {
                static const time_t rangeDur[] = { 3600, 21600, 43200, 86400, 604800 };
                time_t step = (_lastGraphRange >= 0 && _lastGraphRange <= 4)
                              ? rangeDur[_lastGraphRange] : 86400;

                /*
                 * Se não há âncora (gráfico aberto sem calendário),
                 * ancora no now arredondado para o fim do step atual.
                 */
                if (_graphAnchorEnd == 0) {
                    _graphAnchorEnd = time(nullptr);
                }

                /* Desloca a âncora por exatamente 1 step */
                _graphAnchorEnd += (time_t)uiEv.param * step;

                /* Não permite ver o futuro */
                time_t now = time(nullptr);
                if (_graphAnchorEnd > now) _graphAnchorEnd = now;

                /* Offset derivado da posição: negativo = passado (▶ habilitado) */
                _graphNavOffset = (_graphAnchorEnd < now) ? -1 : 0;
                _displayMgr.setGraphNavOffset(_graphNavOffset);

                /* Invalida cache de ranges (dados com offset são únicos) */
                for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;

                _isRenderingGraph = true;
                renderGraphOptimized(uiEv.id, _lastGraphRange, true, 0, _graphAnchorEnd);
                _isRenderingGraph = false;
            }
            /* ── Abertura do calendário ── */
            else if (uiEv.type == UiEvent::EVT_OPEN_CALENDAR) {
                time_t now = _netMgr.getEpoch();
                struct tm nowTm;
                localtime_r(&now, &nowTm);
                int year  = nowTm.tm_year + 1900;
                int month = nowTm.tm_mon + 1;

                uint32_t mask = _storageMgr.getHistoryDaysMask(year, month);
                _displayMgr.showCalendar(year, month, mask);
            }
            /* ── Seleção de dia no calendário ── */
            else if (uiEv.type == UiEvent::EVT_CALENDAR_DAY) {
                int sensorId = uiEv.id;
                int dayNum   = uiEv.param;

                /* Meia-noite do dia selecionado */
                struct tm selTm = {};
                selTm.tm_year = _displayMgr.getCalYear() - 1900;
                selTm.tm_mon  = _displayMgr.getCalMonth() - 1;
                selTm.tm_mday = dayNum;
                time_t selMidnight = mktime(&selTm);

                /*
                 * Âncora = meia-noite do dia seguinte.
                 * Janela 24H será exatamente [00:00, 23:59] do dia selecionado.
                 * Navegação ◀▶ desloca a partir desta âncora, não de now.
                 */
                _graphAnchorEnd = selMidnight + 86400;

                /*
                 * Offset para controle do botão ▶:
                 * negativo = estamos no passado (▶ habilitado),
                 * zero = presente (▶ desabilitado).
                 */
                time_t now = time(nullptr);
                _graphNavOffset = (_graphAnchorEnd < now) ? -1 : 0;
                _displayMgr.setGraphNavOffset(_graphNavOffset);

                /* Invalida cache e carrega gráfico 24H com janela fixa */
                for (int r = 0; r < 5; r++) _sensorCache[r].valid = false;
                _sensorCacheId = sensorId;
                _lastGraphRange = RANGE_24H;

                _isRenderingGraph = true;
                renderGraphOptimized(sensorId, RANGE_24H, true, 0, _graphAnchorEnd);
                _isRenderingGraph = false;
            }
            /* ── Mudança de mês no calendário ── */
            else if (uiEv.type == UiEvent::EVT_CALENDAR_MONTH) {
                int newMonth = _displayMgr.getCalMonth() + uiEv.param;
                int newYear  = _displayMgr.getCalYear();

                if (newMonth < 1)  { newMonth = 12; newYear--; }
                if (newMonth > 12) { newMonth = 1;  newYear++; }

                uint32_t mask = _storageMgr.getHistoryDaysMask(newYear, newMonth);
                _displayMgr.showCalendar(newYear, newMonth, mask);
            }
            else if (uiEv.type == UiEvent::EVT_OPEN_SETTINGS) {
                SystemConfig &cfg = _storageMgr.getConfig();
                String authPin = String(cfg.displayPin);
                if (authPin.length() == 0) authPin = "1234";
                _displayMgr.showAuthScreen(authPin);
            }
            else if (uiEv.type == UiEvent::EVT_AUTH_SUCCESS) {
                _soundMgr.play(SND_CONFIRM);


                if (_pendingAlarmDeactivate) {
                    _pendingAlarmDeactivate = false;

                    SystemConfig &cfg = _storageMgr.getConfig();
                    cfg.ambientSensor.alarmsActive = false;
                    for (int i = 0; i < MAX_SENSORS; i++) {
                        cfg.sensors[i].alarmsActive = false;
                    }

                    _soundMgr.stopAlarm();
                    _displayMgr.setAlarmState(0, -1, false, false);
                    _displayMgr.setAlarmSilenced(false, 0);
                    _displayMgr.setAlarmDeactivated(true);
                    _displayMgr.forceDashboard();
                    LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_DEACTIVATED, 0, "");
                } else if (_storageMgr.mustChangePin()) {
                    /* SEC-004/F12.4: PIN ainda é o default factory "1234";
                     * força a tela de troca antes de permitir menu principal. */
                    _displayMgr.showSettingsPassword();
                    LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, 0,
                             TRL("Default PIN detected; forcing change."));
                } else {
                    _displayMgr.showSettingsMain();
                }
            }
            else if (uiEv.type == UiEvent::EVT_MENU_SELECT) {
                if (uiEv.id == 0) {
                    _displayMgr.showSettingsThemes(_storageMgr.getConfig().themeIndex);
                }
                else if (uiEv.id == 1) {
                    _displayMgr.showSettingsAlarms(&_storageMgr.getConfig());
                }
                else if (uiEv.id == 2) {

                    _displayMgr.showSettingsSounds(_soundMgr.getSettingsState());
                }
                else if (uiEv.id == 3) {
                    _displayMgr.showSettingsLang(_storageMgr.getConfig().displayLang);
                }
                else if (uiEv.id == 4) {
                    _displayMgr.showSettingsPassword();
                }
                else if (uiEv.id == 5) {
                    _displayMgr.showTouchCalibration();
                }
                else if (uiEv.id == 6) {
                    _displayMgr.showSettingsLicense();
                }
                else if (uiEv.id == 7) {
                    _displayMgr.showSystemStatus();
                }
                else if (uiEv.id == 8) {
                    _displayMgr.showSettingsDisplayOffset();
                }
            }
            else if (uiEv.type == UiEvent::EVT_APPLY_THEME) {
                SystemConfig &cfg = _storageMgr.getConfig();
                cfg.themeIndex = uiEv.id;
                loadTheme(cfg.themeIndex);
                _storageMgr.saveConfiguration();
                _displayMgr.refreshTheme();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_THEME_CHANGED, 0, "");
            }
            else if (uiEv.type == UiEvent::EVT_APPLY_LANG) {
                SystemConfig &cfg = _storageMgr.getConfig();
                cfg.displayLang = uiEv.id;
                _displayMgr.setLanguage(cfg.displayLang);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                _displayMgr.forceDashboard();
                LOG_CODE(LOG_INFO, "APP", APP_UI_LANG_CHANGED, 0, "");
            }
            else if (uiEv.type == UiEvent::EVT_SAVE_ALARMS) {
                _storageMgr.saveConfiguration();

                _sensorMgr.syncAlarmLimits(_storageMgr.getConfig());

                checkAlarmConditions();
                _soundMgr.play(SND_CONFIRM);
                _displayMgr.showSettingsAlarms(&_storageMgr.getConfig());
                LOG_CODE(LOG_INFO, "APP", APP_UI_ALARM_SAVED, 0, "");
            }

            else if (uiEv.type == UiEvent::EVT_APPLY_TOUCH_CAL) {
                SystemConfig &cfg = _storageMgr.getConfig();
                TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
                _displayMgr.fillCalData(cal);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_CAL_SAVED, 0, "");
            }

            else if (uiEv.type == UiEvent::EVT_SAVE_TOUCH_CAL) {
                /* Salva threshold de sensibilidade calibrado */
                SystemConfig &cfg = _storageMgr.getConfig();
                TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
                _displayMgr.fillCalData(cal);
                _storageMgr.saveConfiguration();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_SENS_SAVED, 0, "");
            }

            else if (uiEv.type == UiEvent::EVT_APPLY_DISPLAY_OFFSET) {
                /*
                 * Aplica o novo offset ao TFT (já foi aplicado pela tela em preview,
                 * mas os valores vêm do evento para garantir consistência), persiste
                 * em reserved[] e reinicia a calibração do touch: o mapeamento
                 * raw→pixel depende diretamente da posição da imagem no LCD, então
                 * não faz sentido manter a calibração antiga após o deslocamento.
                 */
                SystemConfig &cfg = _storageMgr.getConfig();

                int8_t ox = (int8_t)uiEv.id;
                int8_t oy = (int8_t)uiEv.param;
                DisplayOffsetData* ofs = reinterpret_cast<DisplayOffsetData*>(
                    cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData));
                ofs->magic    = 0xD0;
                ofs->offsetX  = ox;
                ofs->offsetY  = oy;
                ofs->reserved = 0;

                /* Invalida calibração de touch: magic=0 força recalibração; o restante
                 * do bloco pode permanecer como lixo — o magic é o único critério de
                 * validade em loadTouchCalibration(). */
                TouchCalData* cal = reinterpret_cast<TouchCalData*>(cfg.reserved);
                cal->magic = 0;

                _storageMgr.saveConfiguration();
                _displayMgr.resetTouchCalibration();
                _soundMgr.play(SND_CONFIRM);
                LOG_CODE(LOG_INFO, "APP", APP_UI_TOUCH_CAL_SAVED, 0,
                         "Display offset applied; touch calibration reset.");

                /* Abre imediatamente a calibração do touch para o usuário remapear
                 * o toque com a nova posição da imagem do LCD. */
                _displayMgr.showTouchCalibration();
            }

            else if (uiEv.type == UiEvent::EVT_SAVE_PASSWORD) {
                SystemConfig &cfg = _storageMgr.getConfig();
                char newPwd[9];
                _displayMgr.getNewPassword(newPwd, sizeof(newPwd));
                if (strlen(newPwd) >= 4 && strlen(newPwd) <= 7) {
                    /* SEC-004/F12.4: só limpa mustChangePin se o usuário
                     * escolheu PIN != default "1234". Se setou "1234" de novo,
                     * mantém flag ativa — não resolve nada trocar para o mesmo.
                     * Note: "1234" ainda é aceito como valor; a política só
                     * impede que conte como "troca real". */
                    safeCopy(cfg.displayPin, newPwd, sizeof(cfg.displayPin));
                    cfg.displayPin[7] = '\0';
                    if (strcmp(newPwd, "1234") != 0) {
                        _storageMgr.clearMustChangePin();
                    }
                    _storageMgr.saveConfiguration();
                    _soundMgr.play(SND_CONFIRM);
                    LOG_CODE(LOG_INFO, "APP", APP_UI_PIN_CHANGED, 0, "");
                } else {
                    _soundMgr.play(SND_ERROR);
                }
            }

            else if (uiEv.type == UiEvent::EVT_SAVE_SOUNDS) {
                SoundSettingsState sndState = _displayMgr.getSoundSettings();
                _soundMgr.applySettingsState(sndState);

                SystemConfig &cfg = _storageMgr.getConfig();
                SoundConfigData* sndCfg = reinterpret_cast<SoundConfigData*>(
                    cfg.reserved + sizeof(TouchCalData));
                _soundMgr.fillConfig(sndCfg);
                _storageMgr.saveConfiguration();

                _soundMgr.play(SND_CONFIRM);
                _displayMgr.showSettingsMain();
                LOG_CODE(LOG_INFO, "APP", APP_UI_SOUND_SAVED, 0, "");
            }


            else if (uiEv.type == UiEvent::EVT_ALARM_SILENCE) {
                uint32_t silenceSec = (uiEv.param > 0) ? uiEv.param : 120;
                _soundMgr.stopAlarm();
                _displayMgr.setAlarmSilenced(true, millis() + (silenceSec * 1000));
                _displayMgr.forceDashboard();
                LOG_CODE(LOG_WARN, "APP", APP_UI_ALARM_SILENCED, 120, "");
            }


            else if (uiEv.type == UiEvent::EVT_ALARM_DEACTIVATE) {

                _pendingAlarmDeactivate = true;
                SystemConfig &cfg = _storageMgr.getConfig();
                _displayMgr.showAuthScreen(String(cfg.displayPin));
            }
        }
    }


    {
        uint8_t volPreview;
        if (_displayMgr.consumeVolumePreview(volPreview)) {
            _soundMgr.setVolume(volPreview);
            _soundMgr.play(SND_TOUCH_CLICK);
            _displayMgr.consumeTouchSound();
        }
    }


    {
        uint8_t alarmVolPreview;
        if (_displayMgr.consumeAlarmVolumePreview(alarmVolPreview)) {
            _soundMgr.setAlarmVolume(alarmVolPreview);

            SoundSettingsState sndState = _displayMgr.getSoundSettings();
            _soundMgr.playPreview(SND_ALARM_START, sndState.alarmMelody);
            _displayMgr.consumeTouchSound();
        }
    }


    if (_displayMgr.consumeTouchSound()) {
        _soundMgr.play(SND_TOUCH_CLICK);
    }


    if (_displayMgr.consumeErrorSound()) {
        _soundMgr.play(SND_ERROR);
    }


    {
        SoundEvent prevEvt;
        uint8_t prevIdx;
        if (_displayMgr.consumePreviewSound(prevEvt, prevIdx)) {
            _soundMgr.playPreview(prevEvt, prevIdx);
        }
    }

    _soundMgr.update();

    _sensorMgr.update();
    updateLiveDisplay();


    if (_bootCompletedAt > 0 && timeSince(_bootCompletedAt, 5000)) {


        if (_displayMgr.isAlarmSilenced()) {
            uint32_t silEnd = _displayMgr.getAlarmSilenceEnd();
            if (silEnd > 0 && millis() >= silEnd) {
                _displayMgr.setAlarmSilenced(false, 0);
                LOG_CODE(LOG_INFO, "APP", APP_UI_ALARM_SILENCE_EXP, 0, "");
            }
        }

        checkAlarmConditions();
    }


    _soundMgr.update();

    _inYield = false;
}
