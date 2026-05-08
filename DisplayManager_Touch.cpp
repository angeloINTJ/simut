/**
 * @file    DisplayManager_Touch.cpp
 * @brief   Touch handling: handleTouch (giant switch on _uiMode) + accept helpers.
 * @details Sub-arquivo de DisplayManager.cpp (REF-001 / F17 etapa 8).
 *          handleTouch é chamado do loopCore1 a cada frame no Core 1.
 *          Despacha gestures por screen (dashboard / graph / settings /
 *          auth / calibration / alarm action). acceptTouch/Hold/Slide
 *          são gates de debounce + repeat-on-hold usados por handleTouch.
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "LogManager.h"

void DisplayManager::handleTouch() {
    /* v3.44.0-alpha14: use _rawTouchState (already OR'd with sim flag in
     * loopCore1) so simulated touches register as "touched". When sim is
     * active, _ts->getPoint() may return zeros, but mapTouchPoint() now
     * bypasses the ADC mapping and returns _simTouchX/Y directly. */
    if (!_rawTouchState) {
        /* Finger released — habilita próximo toque único */
        _touchReleased = true;

        /*
         * Detecção de release durante calibração hold-and-release.
         * Se o usuário segurou o ponto pelo tempo mínimo, registra a média
         * das amostras acumuladas ao soltar.
         */
        if (_uiMode == MODE_SETTINGS_TOUCH_CAL && _calHolding) {
            if (_calHoldReady && _calHoldSamples > 0 && _calStep < 8) {
                /* Registra ponto: média das amostras acumuladas durante o hold */
                _calRawX[_calStep] = (int16_t)(_calHoldSumX / _calHoldSamples);
                _calRawY[_calStep] = (int16_t)(_calHoldSumY / _calHoldSamples);
                _calStep++;

                if (_calStep < 8) {
                    /* Próximo ponto */
                    _repaintSettings = true;
                } else {
                    /* Todos os 8 pontos capturados — validar e calcular */
                    const int16_t TOLERANCE = 200;
                    bool rejected = false;

                    for (int i = 0; i < 4; i++) {
                        int16_t dx = abs(_calRawX[i] - _calRawX[i + 4]);
                        int16_t dy = abs(_calRawY[i] - _calRawY[i + 4]);
                        if (dx > TOLERANCE || dy > TOLERANCE) {
                            rejected = true;
                            break;
                        }
                    }

                    if (rejected) {
                        _calPhase = 1;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    } else {
                        float avgRawX[4], avgRawY[4];
                        for (int i = 0; i < 4; i++) {
                            avgRawX[i] = (_calRawX[i] + _calRawX[i + 4]) / 2.0f;
                            avgRawY[i] = (_calRawY[i] + _calRawY[i + 4]) / 2.0f;
                        }

                        float rawLeft_X  = (avgRawX[0] + avgRawX[2]) / 2.0f;
                        float rawRight_X = (avgRawX[1] + avgRawX[3]) / 2.0f;
                        float rawTop_Y   = (avgRawY[0] + avgRawY[1]) / 2.0f;
                        float rawBot_Y   = (avgRawY[2] + avgRawY[3]) / 2.0f;

                        float rawLeft_Y  = (avgRawY[0] + avgRawY[2]) / 2.0f;
                        float rawRight_Y = (avgRawY[1] + avgRawY[3]) / 2.0f;
                        float rawTop_X   = (avgRawX[0] + avgRawX[1]) / 2.0f;
                        float rawBot_X   = (avgRawX[2] + avgRawX[3]) / 2.0f;

                        float dxInRawX = fabsf(rawRight_X - rawLeft_X);
                        float dxInRawY = fabsf(rawRight_Y - rawLeft_Y);
                        _calSwapXY = (dxInRawY > dxInRawX);

                        if (_calSwapXY) {
                            float spanX = rawRight_Y - rawLeft_Y;
                            _calXMin = (int16_t)(rawLeft_Y  - 20.0f * spanX / 280.0f);
                            _calXMax = (int16_t)(rawRight_Y + 20.0f * spanX / 280.0f);
                            float spanY = rawBot_X - rawTop_X;
                            _calYMin = (int16_t)(rawTop_X   - 20.0f * spanY / 200.0f);
                            _calYMax = (int16_t)(rawBot_X   + 20.0f * spanY / 200.0f);
                        } else {
                            float spanX = rawRight_X - rawLeft_X;
                            _calXMin = (int16_t)(rawLeft_X  - 20.0f * spanX / 280.0f);
                            _calXMax = (int16_t)(rawRight_X + 20.0f * spanX / 280.0f);
                            float spanY = rawBot_Y - rawTop_Y;
                            _calYMin = (int16_t)(rawTop_Y   - 20.0f * spanY / 200.0f);
                            _calYMax = (int16_t)(rawBot_Y   + 20.0f * spanY / 200.0f);
                        }

                        _calValid = true;
                        UiEvent ev;
                        ev.type = UiEvent::EVT_APPLY_TOUCH_CAL;
                        queue_try_add(&_eventQueue, &ev);

                        _calPhase = 2;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    }
                }
            }
            _calHolding    = false;
            _calHoldReady  = false;
            _calHoldSamples = 0;
        }

        _btnHoldStartTime = 0;
        _lastPressedBtn = -1;
        if (_uiMode != MODE_DASHBOARD && !_sharedState.isBooting) {
            if (timeSince(_lastTouchTime, 30000)) forceDashboard();
        }
        return;
    }


    if (!timeSince(_lastTouchTime, 15)) return;
    TS_Point p = _ts->getPoint();

    /* ── Modo de calibração de sensibilidade: threshold mínimo ──
     * Usa p.z > 50 (ruído do ADC) em vez do threshold calibrado,
     * para capturar toda a faixa de pressão do usuário.            */
    if (_uiMode == MODE_SETTINGS_TOUCH_SENS) {
        /*
         * Calibração de sensibilidade baseada em HOLD contínuo.
         *
         * O usuário pressiona e segura o crosshair. O sistema amostra
         * p.z continuamente e calcula a estabilidade rolante. Quando
         * encontra a menor pressão com leitura estável (sem oscilar),
         * define o threshold e salva.
         *
         * _sensCount       — total de amostras coletadas
         * _sensSamples[30] — buffer circular de amostras recentes
         * _sensStability   — progresso visual da barra (0..1)
         * _sensThreshold   — menor p.z estável encontrado
         */

        /* Botão CANCEL: aceita toque em qualquer pressão */
        if (p.z >= 50) {
            int16_t sx, sy;
            mapTouchPoint(p, sx, sy);
            if (sy > 195 && sx < 125) {
                if (acceptTouch(0)) { showSettingsMain(); return; }
            }
        }

        /* Após concluído, ignora toques até auto-retorno */
        if (_sensDone) return;

        /* Precisa de pressão mínima para coletar (acima do ruído do ADC) */
        if (p.z < 30) return;

        /* Amostra contínua: coleta sem exigir release */
        uint8_t idx = _sensCount % 30;
        _sensSamples[idx] = p.z;
        _sensCount++;

        /* Precisa de pelo menos 10 amostras para análise */
        if (_sensCount < 10) {
            _sensStability = (float)_sensCount / 10.0f * 0.3f;
            _repaintSettings = true;
            return;
        }

        /*
         * Análise de estabilidade rolante (últimas 10 amostras).
         * Calcula stddev/mean dos últimos 10 valores. Se < 15%,
         * a pressão atual está estável.
         */
        int n = (_sensCount < 30) ? _sensCount : 30;
        if (n > 10) n = 10; /* Análise dos últimos 10 */

        float sum = 0;
        uint16_t minZ = 65535, maxZ = 0;
        int base = (int)((_sensCount - n) % 30);
        for (int i = 0; i < n; i++) {
            uint16_t v = _sensSamples[(base + i) % 30];
            sum += v;
            if (v < minZ) minZ = v;
            if (v > maxZ) maxZ = v;
        }
        float mean = sum / n;

        float varSum = 0;
        for (int i = 0; i < n; i++) {
            float d = _sensSamples[(base + i) % 30] - mean;
            varSum += d * d;
        }
        float stddev = sqrtf(varSum / n);
        float cv = (mean > 0) ? (stddev / mean) : 1.0f; /* coef. variação */

        /* Atualiza o threshold quando encontra zona estável */
        bool isStable = (cv < 0.15f) && (_sensCount >= 10);

        if (isStable) {
            /* Encontrou zona estável: threshold = menor valor estável × 0.8 */
            uint16_t candidate = (uint16_t)(minZ * 0.8f);
            if (candidate < 50) candidate = 50;

            /* Aceita se for melhor (menor) que o anterior, ou primeiro achado */
            if (_sensThreshold == 0 || candidate < _sensThreshold) {
                _sensThreshold = candidate;
            }

            /* Progresso: avança conforme tempo em zona estável */
            _sensStability += 0.02f;
            if (_sensStability > 1.0f) _sensStability = 1.0f;

            /* Após barra cheia (~2s estável): salva e conclui */
            if (_sensStability >= 1.0f) {
                _sensZThreshold = _sensThreshold;
                _sensDone = true;
                _sensDoneTime = millis();

                UiEvent ev;
                ev.type = UiEvent::EVT_SAVE_TOUCH_CAL;
                queue_try_add(&_eventQueue, &ev);
                _touchSoundPending = false;
            }
        } else {
            /* Zona instável: barra recua lentamente */
            if (_sensStability > 0.0f) _sensStability -= 0.005f;
            if (_sensStability < 0.0f) _sensStability = 0.0f;
        }

        _repaintSettings = true;
        return;
    }

    if (p.z < _sensZThreshold) return;


    if (_uiMode == MODE_SETTINGS_TOUCH_CAL) {
        _lastTouchTime = millis();


        if (_calPhase >= 1) {
            int16_t screenX, screenY;
            mapTouchPoint(p, screenX, screenY);
            if (screenY >= 185) {
                if (_calPhase == 2) {

                    if (_sharedState.isBooting) {
                        _uiMode = MODE_DASHBOARD;
                        _isDirty = true;
                        _forceFullRedraw = true;
                    } else {
                        showSettingsMain();
                    }
                } else {

                    _calStep = 0;
                    _calPhase = 0;
                    memset(_calRawX, 0, sizeof(_calRawX));
                    memset(_calRawY, 0, sizeof(_calRawY));
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
            }
            return;
        }


        if (_calStep < 8) {
            if (!_calHolding) {
                /* Início do hold: zera acumuladores */
                _calHolding     = true;
                _calHoldReady   = false;
                _calHoldStart   = millis();
                _calHoldSumX    = 0;
                _calHoldSumY    = 0;
                _calHoldSamples = 0;
            }

            /* Acumula amostras enquanto segura */
            _calHoldSumX += p.x;
            _calHoldSumY += p.y;
            _calHoldSamples++;

            /* Após tempo mínimo de hold, sinaliza que pode soltar */
            if (!_calHoldReady && timeSince(_calHoldStart, CAL_HOLD_MS)) {
                _calHoldReady = true;
                _repaintSettings = true; /* Redesenha crosshair verde */
            }
        }
        return;
    }


    int16_t x, y;
    mapTouchPoint(p, x, y);

    if (_sharedState.isBooting) {
        if (_sharedState.showSkipButton) {
            if (y > 190 && x > 80 && x < 240) _skipPressed = true;
        }
        return;
    }


    _lastTouchTime = millis();


    /* BUG-004: fallback para _lastWebBusy em vez de false quando
     * mutex_try_enter falha — evita processar toque como se não houvesse
     * overlay ativo (bypass visual do bloqueio). */
    bool webBusyNow = _lastWebBusy;
    if (mutex_try_enter(&_stateMutex, NULL)) {
        webBusyNow = _webBusy;
        _lastWebBusy = webBusyNow;
        mutex_exit(&_stateMutex);
    }


    if (webBusyNow && _uiMode == MODE_DASHBOARD) {
        if (!acceptTouch(0xF0)) return;
        if (!_webOverlayShown) {
            drawWebBusyOverlay();
        }
        _webOverlayPending = true;
        return;
    }

    if (_uiMode == MODE_DASHBOARD) {
        if (y > 35 && y < 110) {
            if (!acceptTouch(0)) return;

            /* Canto direito: botão de gráfico (prioridade sobre alarme) */
            if (_ambientShowMinMax && x > 266) {
                _ambientShowMinMax = false;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = -1; ev.param = 0;
                queue_try_add(&_eventQueue, &ev);
                return;
            }

            if (_alarmAmbientTemp || _alarmAmbientHum) {
                showAlarmAction(-1);
                return;
            }

            /* Alternar entre modo normal e modo min/max */
            _ambientShowMinMax = !_ambientShowMinMax;
            {
                SystemState snap;
                mutex_enter_blocking(&_stateMutex);
                snap = _sharedState;
                mutex_exit(&_stateMutex);
                drawAmbientPanel(snap.ambientTemp, snap.ambientHum, snap.ambientValid);
            }
            return;
        }
        if (y > 115 && y < 190) {
            if (!acceptTouch(1)) return;
            int sensorIdToGraph = -1;
            if (_sharedState.selectedSlotIdx >= 0 && _sharedState.selectedSlotIdx <= 10) sensorIdToGraph = _sharedState.selectedSlotIdx;

            /* Canto direito: botão de gráfico (prioridade sobre alarme) */
            if (_slotShowMinMax && x > 266) {
                _slotShowMinMax = false;
                if (sensorIdToGraph != -1) {
                    UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = sensorIdToGraph; ev.param = 0;
                    queue_try_add(&_eventQueue, &ev);
                }
                return;
            }

            if (sensorIdToGraph >= 0 && isSlotAlarming(sensorIdToGraph)) {
                showAlarmAction((int8_t)sensorIdToGraph);
                return;
            }

            /* Alternar entre modo normal e modo min/max */
            _slotShowMinMax = !_slotShowMinMax;
            {
                SystemState snap;
                mutex_enter_blocking(&_stateMutex);
                snap = _sharedState;
                mutex_exit(&_stateMutex);
                drawSlotPanel(snap.slotTemp, snap.slotValid,
                              snap.selectedSlotIdx, snap.slotName, true);
            }
            return;
        }
        if (y > 195) {
            const int btnW = 58, gap = 5, pitch = btnW + gap;
            int btnIdx = (x - 5) / pitch;
            DashBtn btns[5];
            int totalPages = 1; bool paging = false;
            (void)buildDashLayout(btns, &totalPages, &paging);
            if (btnIdx < 0 || btnIdx > 4) return;
            const DashBtn &b = btns[btnIdx];
            if (b.kind < 0) return;   /* toque em posição vazia (gap) — ignora */
            if (b.kind == 2) {  /* PAGE */
                if (!acceptTouch(14)) return;
                _currentPage++;
                if (_currentPage >= totalPages) _currentPage = 0;
                drawBottomButtons(_sharedState.selectedSlotIdx, true); return;
            }
            if (b.kind == 1) {  /* CFG */
                if (!acceptSlideTouch(20)) return;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_SETTINGS;
                queue_try_add(&_eventQueue, &ev); return;
            }
            /* SLOT */
            if (!acceptSlideTouch(10 + b.slotId)) return;
            _slotShowMinMax = false;
            drawBottomButtons(b.slotId, false);
            UiEvent ev; ev.type = UiEvent::EVT_SLOT_SELECT; ev.id = b.slotId;
            queue_try_add(&_eventQueue, &ev);
        }
    }
    else if (_uiMode == MODE_STATS_VIEW) {
        if (y < 40 && x > 270) { if (!acceptTouch(0)) return; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
        if (y > 170) {
            if (!acceptTouch(1)) return;
            UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = 3;
            queue_try_add(&_eventQueue, &ev); return;
        }
    }
    else if (_uiMode == MODE_GRAPH_VIEW) {
        /* Botão X (fechar) — canto superior direito */
        if (y < 40 && x > 284) { if (!acceptTouch(0)) return; _graphNavOffset = 0; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
        /* Toque no header — mostra nome do sensor por 3s */
        if (y < 28 && x < 284) {
            if (!acceptTouch(0)) return;
            _headerShowName = true;
            _headerNameTimer = millis();
            drawGraphHeaderBar();
            return;
        }
        /* ── Barra inferior: [◀Past][▶Fut][📅Cal][🔍+ZoomIn][🔍-ZoomOut] ── */
        if (y >= 195) {
            const int btnW = 60, gap = 4, startX = 2;
            int btn = -1;
            for (int i = 0; i < 5; i++) {
                int bx = startX + i * (btnW + gap);
                if (x >= bx && x <= bx + btnW) { btn = i; break; }
            }

            if (btn == 0) {
                /* Passado (◀) */
                if (!acceptHoldTouch(10)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = -1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 1 && _graphNavOffset < 0) {
                /* Futuro (▶) — só se offset < 0 */
                if (!acceptHoldTouch(11)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = +1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 2) {
                /* Calendário (📅) */
                if (!acceptTouch(0)) return;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_CALENDAR; ev.id = _graphData.sensorIdx; ev.param = 0;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 3 && _graphData.timeRange > 0) {
                /* Zoom In — range mais curto (mais detalhe) */
                if (!acceptHoldTouch(12)) return;
                int newRange = _graphData.timeRange - 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 4 && _graphData.timeRange < 4) {
                /* Zoom Out — range mais longo (menos detalhe) */
                if (!acceptHoldTouch(13)) return;
                int newRange = _graphData.timeRange + 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
        }
        /* Toque na zona central → detalhes de temperatura (página 0) */
        if (y >= 40 && y < 195) {
            if (!acceptTouch(10)) return;
            _detailPage = 0;
            _uiMode = MODE_GRAPH_DETAIL;
            _repaintGraph = true;
        }
    }
    else if (_uiMode == MODE_GRAPH_DETAIL) {
        /* Botão X — fechar para dashboard */
        if (y < 40 && x > 284) { if (!acceptTouch(0)) return; _graphNavOffset = 0; _uiMode = MODE_DASHBOARD; _isDirty = true; _forceFullRedraw = true; return; }
        /* Toque no header — mostra nome do sensor por 3s */
        if (y < 28 && x < 284) {
            if (!acceptTouch(0)) return;
            _headerShowName = true;
            _headerNameTimer = millis();
            drawGraphHeaderBar();
            return;
        }
        /* Barra inferior — mesma lógica do graph view */
        if (y >= 195) {
            const int btnW = 60, gap = 4, startX = 2;
            int btn = -1;
            for (int i = 0; i < 5; i++) {
                int bx = startX + i * (btnW + gap);
                if (x >= bx && x <= bx + btnW) { btn = i; break; }
            }

            if (btn == 0) {
                /* Passado (◀) */
                if (!acceptHoldTouch(10)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = -1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 1 && _graphNavOffset < 0) {
                /* Futuro (▶) — só se offset < 0 */
                if (!acceptHoldTouch(11)) return;
                UiEvent ev; ev.type = UiEvent::EVT_GRAPH_NAV; ev.id = _graphData.sensorIdx; ev.param = +1;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 2) {
                /* Calendário (📅) */
                if (!acceptTouch(0)) return;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_CALENDAR; ev.id = _graphData.sensorIdx; ev.param = 0;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 3 && _graphData.timeRange > 0) {
                /* Zoom In — range mais curto (mais detalhe) */
                if (!acceptHoldTouch(12)) return;
                int newRange = _graphData.timeRange - 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
            if (btn == 4 && _graphData.timeRange < 4) {
                /* Zoom Out — range mais longo (menos detalhe) */
                if (!acceptHoldTouch(13)) return;
                int newRange = _graphData.timeRange + 1;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = newRange;
                queue_try_add(&_eventQueue, &ev); return;
            }
        }
        /* Toque na zona central → próxima página ou voltar ao gráfico */
        if (y >= 40 && y < 195) {
            if (!acceptTouch(10)) return;
            bool hasHumNow = _graphData.hasHumidity && !isnan(_currentMinHum);
            if (_detailPage == 0 && hasHumNow) {
                /* Temperatura → Umidade */
                _detailPage = 1;
                _repaintGraph = true;
            } else {
                /* Umidade (ou temp sem hum) → voltar ao gráfico */
                _detailPage = 0;
                _uiMode = MODE_GRAPH_VIEW;
                _repaintGraph = true;
            }
        }
    }
    /* ── CALENDÁRIO ── */
    else if (_uiMode == MODE_CALENDAR) {
        /* Botão X (voltar ao gráfico) — canto superior direito */
        if (y < 28 && x >= 270) {
            if (!acceptTouch(0)) return;
            _uiMode = MODE_GRAPH_VIEW;
            _repaintGraph = true;
            return;
        }
        /* Seta ◀ mês — header esquerdo */
        if (y < 28 && x < 30) {
            if (!acceptSlideTouch(20)) return;
            UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = -1;
            queue_try_add(&_eventQueue, &ev); return;
        }
        /* Seta ▶ mês — header direito */
        if (y < 28 && x > 290) {
            if (!acceptSlideTouch(21)) return;
            UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = +1;
            queue_try_add(&_eventQueue, &ev); return;
        }
        /* ── Grade de dias (y=44..190) ── */
        if (y >= 44 && y < 190) {
            const int gridStartY = 46, cellW = 44, cellH = 24;
            int row = (y - gridStartY) / cellH;
            int col = x / cellW;
            if (col >= 0 && col < 7 && row >= 0 && row < 6) {
                /* Calcula primeiro dia da semana */
                struct tm firstTm = {};
                firstTm.tm_year = _calYear - 1900;
                firstTm.tm_mon  = _calMonth - 1;
                firstTm.tm_mday = 1;
                mktime(&firstTm);
                int firstDow = firstTm.tm_wday;

                int cell = row * 7 + col;
                int dayNum = cell - firstDow + 1;

                /* Verifica se é dia válido com dados */
                if (dayNum >= 1 && dayNum <= 31 && (_calDaysMask & (1UL << dayNum))) {
                    if (!acceptTouch(0)) return;
                    UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_DAY;
                    ev.id = _graphData.sensorIdx;
                    ev.param = dayNum;
                    queue_try_add(&_eventQueue, &ev);
                }
            }
        }
        /* ── Barra inferior: [◀ Mês] [Hoje] [Mês ▶] ── */
        if (y >= 195) {
            if (x < 106) {
                /* ◀ Mês */
                if (!acceptSlideTouch(20)) return;
                UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = -1;
                queue_try_add(&_eventQueue, &ev);
            } else if (x >= 108 && x < 212) {
                /* Hoje — volta ao gráfico com offset 0 */
                if (!acceptTouch(0)) return;
                _graphNavOffset = 0;
                UiEvent ev; ev.type = UiEvent::EVT_OPEN_GRAPH; ev.id = _graphData.sensorIdx; ev.param = _graphData.timeRange;
                queue_try_add(&_eventQueue, &ev);
            } else if (x >= 217) {
                /* Mês ▶ */
                if (!acceptSlideTouch(21)) return;
                UiEvent ev; ev.type = UiEvent::EVT_CALENDAR_MONTH; ev.id = _graphData.sensorIdx; ev.param = +1;
                queue_try_add(&_eventQueue, &ev);
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_THEMES) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int actualIndex = (_themePage * 4) + clickedIndex;
            if (actualIndex < getThemeCount() && actualIndex != _previewThemeIdx) {
                if (!acceptSlideTouch(clickedIndex)) return;
                _previewThemeIdx = actualIndex; _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_previewThemeIdx > 0) _previewThemeIdx--; else _previewThemeIdx = getThemeCount() - 1;
                _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            } else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_previewThemeIdx < getThemeCount() - 1) _previewThemeIdx++; else _previewThemeIdx = 0;
                _themePage = _previewThemeIdx / 4; _repaintSettings = true;
            } else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            } else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_APPLY_THEME; ev.id = _previewThemeIdx; queue_try_add(&_eventQueue, &ev);
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_ALARMS) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int mapIdx = (_alarmPage * 4) + clickedIndex;
            if (mapIdx < _activeSensorCount) {
                /*
                 * Zona de toque do SIM/NAO: lado direito do item.
                 * Items renderizados em x=10..295, SIM/NAO fica nos ~60px finais.
                 * Zona do toggle: x >= 230 (tela).
                 */
                bool touchOnStatus = (x >= 230);

                if (touchOnStatus && mapIdx == _alarmSelection) {
                    /* Toque no SIM/NAO do item selecionado: toggle ou edição */
                    if (!acceptTouch(clickedIndex + 4)) return;
                    int actualSensorId = _activeSensorsMap[_alarmSelection];
                    SensorRecord* rec = (actualSensorId == -1)
                        ? &_sysConfigPtr->ambientSensor
                        : &_sysConfigPtr->sensors[actualSensorId];

                    if (rec->alarmsActive) {
                        /* SIM → NAO: desativa e salva imediatamente */
                        rec->alarmsActive = false;
                        UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
                        ev.id = actualSensorId;
                        queue_try_add(&_eventQueue, &ev);
                        _repaintSettings = true;
                    } else {
                        /* NAO → entra na tela de edição de limites */
                        showAlarmEdit(actualSensorId);
                    }
                } else if (mapIdx != _alarmSelection) {
                    /* Toque no nome/barra: seleciona o item */
                    if (!acceptSlideTouch(clickedIndex)) return;
                    _alarmSelection = mapIdx; _alarmPage = _alarmSelection / 4; _repaintSettings = true;
                }
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_alarmSelection > 0) _alarmSelection--; else _alarmSelection = _activeSensorCount - 1;
                _alarmPage = _alarmSelection / 4; _repaintSettings = true;
            } else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_alarmSelection < _activeSensorCount - 1) _alarmSelection++; else _alarmSelection = 0;
                _alarmPage = _alarmSelection / 4; _repaintSettings = true;
            } else {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_ALARM_EDIT) {
        bool hasHum = (_editSensorIdx == -1 || _tempAlarmConfig.rom[0] != 0x28);


        if (y >= 50 && y <= 115) {
            uint8_t zone = (x < 160) ? 0 : 1;
            if (!acceptTouch(zone)) return;
            _editFieldFocus = zone;
            _repaintSettings = true;
        }

        else if (hasHum && y >= 125 && y <= 170) {
            uint8_t zone = (x < 160) ? 2 : 3;
            if (!acceptTouch(zone)) return;
            _editFieldFocus = zone;
            _repaintSettings = true;
        }

        else if (y >= 190) {
            auto adjustVal = [](float val, float step, float minV, float maxV) -> float {
                val += step; val = round(val * 10.0f) / 10.0f;
                if (val < minV) val = minV;
                if (val > maxV) val = maxV;
                return val;
            };

            auto enforceInterlock = [&]() {
                if (_tempAlarmConfig.tempMin >= _tempAlarmConfig.tempMax) {
                    if (_editFieldFocus == 0)
                        _tempAlarmConfig.tempMax = round((_tempAlarmConfig.tempMin + 0.1f) * 10.0f) / 10.0f;
                    else
                        _tempAlarmConfig.tempMin = round((_tempAlarmConfig.tempMax - 0.1f) * 10.0f) / 10.0f;
                }
                if (hasHum && _tempAlarmConfig.humMin >= _tempAlarmConfig.humMax) {
                    if (_editFieldFocus == 2)
                        _tempAlarmConfig.humMax = round((_tempAlarmConfig.humMin + 0.1f) * 10.0f) / 10.0f;
                    else
                        _tempAlarmConfig.humMin = round((_tempAlarmConfig.humMax - 0.1f) * 10.0f) / 10.0f;
                }
                if (_tempAlarmConfig.tempMax > 150.0f) _tempAlarmConfig.tempMax = 150.0f;
                if (_tempAlarmConfig.tempMin < -50.0f) _tempAlarmConfig.tempMin = -50.0f;
                if (hasHum) {
                    if (_tempAlarmConfig.humMax > 100.0f) _tempAlarmConfig.humMax = 100.0f;
                    if (_tempAlarmConfig.humMin < 0.0f) _tempAlarmConfig.humMin = 0.0f;
                }
            };

            if (x < 70) {
                /* Decremento com hold-repeat (300ms) e aceleração */
                if (!acceptHoldTouch(10)) return;
                if (_lastPressedBtn != 0) { _btnHoldStartTime = millis(); _lastPressedBtn = 0; }
                uint32_t holdTime = millis() - _btnHoldStartTime;
                float step = -0.1f; if (holdTime > 6000) step = -10.0f; else if (holdTime > 4000) step = -1.0f; else if (holdTime > 2000) step = -0.5f;
                if (_editFieldFocus == 0) _tempAlarmConfig.tempMin = adjustVal(_tempAlarmConfig.tempMin, step, -50.0f, 150.0f);
                if (_editFieldFocus == 1) _tempAlarmConfig.tempMax = adjustVal(_tempAlarmConfig.tempMax, step, -50.0f, 150.0f);
                if (_editFieldFocus == 2) _tempAlarmConfig.humMin = adjustVal(_tempAlarmConfig.humMin, step, 0.0f, 100.0f);
                if (_editFieldFocus == 3) _tempAlarmConfig.humMax = adjustVal(_tempAlarmConfig.humMax, step, 0.0f, 100.0f);
                enforceInterlock();
                _repaintSettings = true;
            }
            else if (x < 138) {
                /* Incremento com hold-repeat (300ms) e aceleração */
                if (!acceptHoldTouch(11)) return;
                if (_lastPressedBtn != 1) { _btnHoldStartTime = millis(); _lastPressedBtn = 1; }
                uint32_t holdTime = millis() - _btnHoldStartTime;
                float step = 0.1f; if (holdTime > 6000) step = 10.0f; else if (holdTime > 4000) step = 1.0f; else if (holdTime > 2000) step = 0.5f;
                if (_editFieldFocus == 0) _tempAlarmConfig.tempMin = adjustVal(_tempAlarmConfig.tempMin, step, -50.0f, 150.0f);
                if (_editFieldFocus == 1) _tempAlarmConfig.tempMax = adjustVal(_tempAlarmConfig.tempMax, step, -50.0f, 150.0f);
                if (_editFieldFocus == 2) _tempAlarmConfig.humMin = adjustVal(_tempAlarmConfig.humMin, step, 0.0f, 100.0f);
                if (_editFieldFocus == 3) _tempAlarmConfig.humMax = adjustVal(_tempAlarmConfig.humMax, step, 0.0f, 100.0f);
                enforceInterlock();
                _repaintSettings = true;
            }
            else if (x < 219) {
                /* BACK: desativa o alarme e salva */
                if (!acceptTouch(12)) return;
                _lastPressedBtn = -1;
                _tempAlarmConfig.alarmsActive = false;
                if (_editSensorIdx == -1) _sysConfigPtr->ambientSensor = _tempAlarmConfig;
                else                      _sysConfigPtr->sensors[_editSensorIdx] = _tempAlarmConfig;
                UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
                ev.id = _editSensorIdx; queue_try_add(&_eventQueue, &ev);
                showSettingsAlarms(_sysConfigPtr);
            }
            else {
                /* SAVE: ativa o alarme e salva */
                if (!acceptTouch(13)) return;
                _lastPressedBtn = -1;
                _tempAlarmConfig.alarmsActive = true;
                if (_editSensorIdx == -1) _sysConfigPtr->ambientSensor = _tempAlarmConfig;
                else                      _sysConfigPtr->sensors[_editSensorIdx] = _tempAlarmConfig;
                UiEvent ev; ev.type = UiEvent::EVT_SAVE_ALARMS;
                ev.id = _editSensorIdx; queue_try_add(&_eventQueue, &ev);
                showSettingsAlarms(_sysConfigPtr);
            }
        }
    }
    else if (_uiMode == MODE_AUTH) {
        if (y > 200 && x < 120) { if (!acceptTouch(0)) return; forceDashboard(); return; }
        /* Botão de licença — acessível mesmo em lockout */
        if (y > 200 && x > 195) { if (!acceptTouch(5)) return; _licenseFromAuth = true; showSettingsLicense(); return; }
        if (_permanentLockout || !timeReached(_lockoutUntil)) return;
        if (y >= 80 && y <= 185) {
            int row = (y < 135) ? 0 : 1; int col = (x > 160) ? 1 : 0; int btnIdx = (row * 2) + col;
            if (!acceptTouch(1 + btnIdx)) return;
            String clickedChars = String(_keypadChars[btnIdx]); char expected = _expectedPin[_authStep];
            if (clickedChars.indexOf(expected) < 0) _isCurrentAttemptValid = false;
            _authStep++; _authFailed = false;
            if ((size_t)_authStep >= _expectedPin.length()) {
                if (_isCurrentAttemptValid) {
                    _failedAttempts = 0; UiEvent ev; ev.type = UiEvent::EVT_AUTH_SUCCESS; queue_try_add(&_eventQueue, &ev); return;
                } else {
                    _authFailed = true; _failedAttempts++; _authStep = 0; _isCurrentAttemptValid = true;
                    _errorSoundPending = true;
                    if (_failedAttempts <= 2) _lockoutUntil = 0;
                    else if (_failedAttempts == 3) _lockoutUntil = millis() + 5000;
                    else if (_failedAttempts == 4) _lockoutUntil = millis() + 15000;
                    else if (_failedAttempts == 5) _lockoutUntil = millis() + 60000;
                    else { _permanentLockout = true; _lockoutUntil = millis() + 10000; }
                    _forceSettingsRedraw = true;
                }
            }
            scrambleKeys(); _repaintSettings = true;
        }
    }
    else if (_uiMode == MODE_SETTINGS_MAIN) {
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int mapIdx = (_mainMenuPage * 4) + clickedIndex;
            if (mapIdx < 9 && mapIdx != _menuSelection) {
                if (!acceptSlideTouch(clickedIndex)) return;
                _menuSelection = mapIdx; _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_menuSelection > 0) _menuSelection--; else _menuSelection = 8;
                _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_menuSelection < 8) _menuSelection++; else _menuSelection = 0;
                _mainMenuPage = _menuSelection / 4; _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                forceDashboard();
            }
            else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_MENU_SELECT; ev.id = _menuSelection; queue_try_add(&_eventQueue, &ev);
            }
        }
    }
    else if (_uiMode == MODE_SETTINGS_DISPLAY_OFFSET) {
        /*
         * Layout dos controles (coordenadas lógicas — o próprio offset aplicado
         * ao TFT ja desloca a imagem):
         *   Pad direcional centrado em (160, 120):
         *     ▲  (130..190, 55..95)   → Y -= 1
         *     ▼  (130..190, 145..185) → Y += 1
         *     ◀  (80..140, 100..140)  → X -= 1
         *     ▶  (180..240, 100..140) → X += 1
         *   Botão de reset central (148..172, 108..132) → zera ambos
         *   Rodapé:
         *     BACK  (10..130, 200..240)   → descarta e volta
         *     APPLY (190..310, 200..240)  → dispara EVT_APPLY_DISPLAY_OFFSET
         */
        /*
         * Cada ajuste é aplicado ao TFT em tempo real via setDisplayOffset(),
         * permitindo calibração visual imediata. BACK reverte ao offset salvo;
         * APPLY dispara EVT_APPLY_DISPLAY_OFFSET (Core 0 persiste + reseta touch).
         * Toda mudança força redraw completo para evitar artefatos do conteúdo
         * desenhado com o offset anterior na tela anterior.
         */
        bool changed = false;
        if (y >= 55 && y <= 95 && x >= 130 && x <= 190) {
            if (!acceptHoldTouch(20)) return;
            if (_offsetPreviewY > -4) { _offsetPreviewY--; changed = true; }
        }
        else if (y >= 145 && y <= 185 && x >= 130 && x <= 190) {
            if (!acceptHoldTouch(21)) return;
            if (_offsetPreviewY <  4) { _offsetPreviewY++; changed = true; }
        }
        else if (y >= 100 && y <= 140 && x >= 80 && x <= 140) {
            if (!acceptHoldTouch(22)) return;
            if (_offsetPreviewX > -4) { _offsetPreviewX--; changed = true; }
        }
        else if (y >= 100 && y <= 140 && x >= 180 && x <= 240) {
            if (!acceptHoldTouch(23)) return;
            if (_offsetPreviewX <  4) { _offsetPreviewX++; changed = true; }
        }
        else if (y >= 108 && y <= 132 && x >= 148 && x <= 172) {
            if (!acceptTouch(24)) return;
            if (_offsetPreviewX != 0 || _offsetPreviewY != 0) {
                _offsetPreviewX = 0; _offsetPreviewY = 0; changed = true;
            }
        }
        else if (y >= 200 && x <= 130) {
            if (!acceptTouch(25)) return;
            /* Descarta ajuste em preview: restaura offset salvo antes de sair. */
            _offsetPreviewX = _offsetSavedX;
            _offsetPreviewY = _offsetSavedY;
            if (_tft) _tft->setDisplayOffset(_offsetSavedX, _offsetSavedY);
            showSettingsMain();
            return;
        }
        else if (y >= 200 && x >= 190) {
            if (!acceptTouch(26)) return;
            UiEvent ev;
            ev.type  = UiEvent::EVT_APPLY_DISPLAY_OFFSET;
            ev.id    = _offsetPreviewX;
            ev.param = _offsetPreviewY;
            queue_try_add(&_eventQueue, &ev);
            return;
        }

        if (changed) {
            if (_tft) _tft->setDisplayOffset(_offsetPreviewX, _offsetPreviewY);
            /* Redraw completo: frame anterior foi desenhado com offset diferente,
             * pixels antigos permanecem fora da nova área e precisam ser limpos. */
            _forceSettingsRedraw = true;
            _repaintSettings = true;
        }
    }
    else if (_uiMode == MODE_SETTINGS_LANG) {
        /* F-LANGPACK Etapa 1: limita interação ao slot 0 quando nenhum
         * .lng carregado. Bound dinâmico evita "fantasma" do slot 1. */
        int activeSlots = _activeLangLoaded ? LANG_COUNT : 1;
        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1; else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int actualIndex = (_langPage * 4) + clickedIndex;
            if (actualIndex < activeSlots && actualIndex != _previewLangIdx) {
                if (!acceptSlideTouch(clickedIndex)) return;
                _previewLangIdx = actualIndex;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_previewLangIdx > 0) _previewLangIdx--; else _previewLangIdx = activeSlots - 1;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_previewLangIdx < activeSlots - 1) _previewLangIdx++; else _previewLangIdx = 0;
                _langPage = _previewLangIdx / 4;
                _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
            else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_APPLY_LANG; ev.id = _previewLangIdx; queue_try_add(&_eventQueue, &ev);
            }
        }
    }


    else if (_uiMode == MODE_SETTINGS_PASSWORD) {


        if (_kbPhase >= 2) {
            if (y >= 185) {
                if (!acceptTouch(0)) return;
                if (_kbPhase == 3) {
                    showSettingsMain();
                } else {
                    _kbPhase = 0;
                    _kbCursor = 0;
                    _kbShowRaw = false;
                    memset(_kbBuffer, 0, sizeof(_kbBuffer));
                    memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
            }
            return;
        }

        char* activeBuf = (_kbPhase == 0) ? _kbBuffer : _kbConfirmBuf;


        if (y < 28 && x > 280) {
            if (!acceptTouch(1)) return;
            showSettingsMain(); return;
        }


        if (y >= 33 && y < 66) {
            if (!acceptTouch(2)) return;
            _kbShowRaw = !_kbShowRaw;
            _repaintSettings = true;
            return;
        }


        if (y >= 72 && y < 168) {
            int row = (y - 72) / 32;
            int col = (x - 1) / 32;
            if (row < 0) row = 0;
            if (row > 2) row = 2;
            if (col < 0) col = 0;
            if (col > 9) col = 9;


            if (!acceptTouch((uint8_t)(row * 10 + col + 10))) return;

            /* Atualizar cursor de seleção visual */
            _kbSelRow = row;
            _kbSelCol = col;

            static const char layer0[3][10] = {
                {'q','w','e','r','t','y','u','i','o','p'},
                {'a','s','d','f','g','h','j','k','l','.'},
                {'z','x','c','v','b','n','m',',','!','?'}
            };
            static const char layer1[3][10] = {
                {'Q','W','E','R','T','Y','U','I','O','P'},
                {'A','S','D','F','G','H','J','K','L',':'},
                {'Z','X','C','V','B','N','M',';','"','\''}
            };
            static const char layer2[3][10] = {
                {'1','2','3','4','5','6','7','8','9','0'},
                {'@','#','$','%','&','*','-','+','=','~'},
                {'(',')','[',']','{','}','/','\\','^','_'}
            };

            const char (*active)[10] = (_kbLayer == 2) ? layer2
                                     : (_kbLayer == 1) ? layer1
                                     :                   layer0;

            if (_kbCursor < 7) {
                activeBuf[_kbCursor++] = active[row][col];
                activeBuf[_kbCursor] = '\0';
                if (_kbLayer == 1 && !_kbShiftLock) _kbLayer = 0;
            }
            _repaintSettings = true;
            return;
        }


        if (y >= 170 && y < 195) {
            /* Novas posições: Shift=1..49, 123=51..99, Espaço=101..219, Bksp=221..269, OK=271..319 */
            if (x < 49) {
                /* Shift */
                if (!acceptTouch(50)) return;
                if (_kbLayer == 1) {
                    _kbShiftLock = !_kbShiftLock;
                    if (!_kbShiftLock) _kbLayer = 0;
                } else {
                    _kbLayer = 1;
                    _kbShiftLock = false;
                }
                _repaintSettings = true;
            }
            else if (x < 99) {
                /* 123 */
                if (!acceptTouch(51)) return;
                _kbLayer = (_kbLayer == 2) ? 0 : 2;
                _kbShiftLock = false;
                _repaintSettings = true;
            }
            else if (x < 219) {
                /* Espaço */
                if (!acceptTouch(52)) return;
                if (_kbCursor < 7) {
                    activeBuf[_kbCursor++] = ' ';
                    activeBuf[_kbCursor] = '\0';
                }
                _repaintSettings = true;
            }
            else if (x < 269) {
                /* Backspace */
                if (!acceptTouch(53)) return;
                if (_kbCursor > 0) {
                    activeBuf[--_kbCursor] = '\0';
                }
                _repaintSettings = true;
            }
            else {
                /* OK — mesma lógica de confirmação */
                if (!acceptTouch(54)) return;
                if (_kbPhase == 0) {
                    if (_kbCursor < 4) {
                        _kbPhase = 2;
                        _kbMsgKey = TR_PWD_TOO_SHORT;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    } else {
                        _kbPhase = 1;
                        _kbCursor = 0;
                        _kbShowRaw = false;
                        memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
                        /* Redesenho parcial: título e boxes mudam, teclas não */
                        _repaintSettings = true;
                    }
                }
                else if (_kbPhase == 1) {
                    if (_kbCursor < 4) {
                        _kbPhase = 2;
                        _kbMsgKey = TR_PWD_TOO_SHORT;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    }
                    else if (strcmp(_kbBuffer, _kbConfirmBuf) != 0) {
                        _kbPhase = 2;
                        _kbMsgKey = TR_PWD_MISMATCH;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    }
                    else {
                        _kbPhase = 3;
                        _kbMsgKey = TR_PWD_SAVED;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;

                        UiEvent ev;
                        ev.type = UiEvent::EVT_SAVE_PASSWORD;
                        ev.id = 0;
                        ev.param = 0;
                        queue_try_add(&_eventQueue, &ev);
                    }
                }
            }
            return;
        }


        if (y >= 195) {
            int btnW = 58; int bGap = 5; int bStartX = 5;
            int btnIdx = (x - bStartX) / (btnW + bGap);
            if (btnIdx < 0) btnIdx = 0;
            if (btnIdx > 4) btnIdx = 4;
            /* Verificar se o toque está dentro do botão (não no gap) */
            int btnX = bStartX + btnIdx * (btnW + bGap);
            if (x < btnX || x > btnX + btnW) return;

            /* Limites de coluna: fila 3 (barra) tem 5 itens, filas 0-2 têm 10 */
            int maxCol = (_kbSelRow == 3) ? 4 : 9;

            if (btnIdx == 0) {
                /* ◄ Esquerda */
                if (!acceptTouch(60)) return;
                _kbSelCol--;
                if (_kbSelCol < 0) _kbSelCol = maxCol;
                _repaintSettings = true;
            }
            else if (btnIdx == 1) {
                /* ► Direita */
                if (!acceptTouch(61)) return;
                _kbSelCol++;
                if (_kbSelCol > maxCol) _kbSelCol = 0;
                _repaintSettings = true;
            }
            else if (btnIdx == 2) {
                /* ▲ Cima */
                if (!acceptTouch(62)) return;
                _kbSelRow--;
                if (_kbSelRow < 0) _kbSelRow = 3;
                /* Ajustar coluna ao trocar para/da barra */
                if (_kbSelRow == 3 && _kbSelCol > 4) _kbSelCol = 4;
                _repaintSettings = true;
            }
            else if (btnIdx == 3) {
                /* ▼ Baixo */
                if (!acceptTouch(63)) return;
                _kbSelRow++;
                if (_kbSelRow > 3) _kbSelRow = 0;
                /* Ajustar coluna ao trocar para/da barra */
                if (_kbSelRow == 3 && _kbSelCol > 4) _kbSelCol = 4;
                _repaintSettings = true;
            }
            else if (btnIdx == 4) {
                /* ✓ Confirma seleção */
                if (!acceptTouch(64)) return;

                if (_kbSelRow == 3) {
                    /*
                     * Barra de ações: executar a ação do item selecionado.
                     * 0=Shift, 1=123, 2=Espaço, 3=Backspace, 4=OK
                     */
                    if (_kbSelCol == 0) {
                        /* Shift */
                        if (_kbLayer == 1) {
                            _kbShiftLock = !_kbShiftLock;
                            if (!_kbShiftLock) _kbLayer = 0;
                        } else {
                            _kbLayer = 1;
                            _kbShiftLock = false;
                        }
                    }
                    else if (_kbSelCol == 1) {
                        /* 123 */
                        _kbLayer = (_kbLayer == 2) ? 0 : 2;
                        _kbShiftLock = false;
                    }
                    else if (_kbSelCol == 2) {
                        /* Espaço */
                        if (_kbCursor < 7) {
                            activeBuf[_kbCursor++] = ' ';
                            activeBuf[_kbCursor] = '\0';
                        }
                    }
                    else if (_kbSelCol == 3) {
                        /* Backspace */
                        if (_kbCursor > 0) {
                            activeBuf[--_kbCursor] = '\0';
                        }
                    }
                    else if (_kbSelCol == 4) {
                        /* OK — confirmação da senha */
                        if (_kbPhase == 0) {
                            if (_kbCursor < 4) {
                                _kbPhase = 2;
                                _kbMsgKey = TR_PWD_TOO_SHORT;
                                _forceSettingsRedraw = true;
                            } else {
                                _kbPhase = 1;
                                _kbCursor = 0;
                                _kbShowRaw = false;
                                memset(_kbConfirmBuf, 0, sizeof(_kbConfirmBuf));
                            }
                        }
                        else if (_kbPhase == 1) {
                            if (_kbCursor < 4) {
                                _kbPhase = 2;
                                _kbMsgKey = TR_PWD_TOO_SHORT;
                                _forceSettingsRedraw = true;
                            }
                            else if (strcmp(_kbBuffer, _kbConfirmBuf) != 0) {
                                _kbPhase = 2;
                                _kbMsgKey = TR_PWD_MISMATCH;
                                _forceSettingsRedraw = true;
                            }
                            else {
                                _kbPhase = 3;
                                _kbMsgKey = TR_PWD_SAVED;
                                _forceSettingsRedraw = true;
                                UiEvent ev;
                                ev.type = UiEvent::EVT_SAVE_PASSWORD;
                                ev.id = 0; ev.param = 0;
                                queue_try_add(&_eventQueue, &ev);
                            }
                        }
                    }
                } else {
                    /* Fila de teclas (0-2): insere o caractere selecionado */
                    static const char lay0[3][10] = {
                        {'q','w','e','r','t','y','u','i','o','p'},
                        {'a','s','d','f','g','h','j','k','l','.'},
                        {'z','x','c','v','b','n','m',',','!','?'}
                    };
                    static const char lay1[3][10] = {
                        {'Q','W','E','R','T','Y','U','I','O','P'},
                        {'A','S','D','F','G','H','J','K','L',':'},
                        {'Z','X','C','V','B','N','M',';','"','\''}
                    };
                    static const char lay2[3][10] = {
                        {'1','2','3','4','5','6','7','8','9','0'},
                        {'@','#','$','%','&','*','-','+','=','~'},
                        {'(',')','[',']','{','}','/','\\','^','_'}
                    };
                    const char (*sel)[10] = (_kbLayer == 2) ? lay2
                                          : (_kbLayer == 1) ? lay1
                                          :                   lay0;
                    if (_kbCursor < 7) {
                        activeBuf[_kbCursor++] = sel[_kbSelRow][_kbSelCol];
                        activeBuf[_kbCursor] = '\0';
                        if (_kbLayer == 1 && !_kbShiftLock) _kbLayer = 0;
                    }
                }
                _repaintSettings = true;
            }
            return;
        }
    }


    else if (_uiMode == MODE_SETTINGS_SOUNDS) {


        if (_inMelodySelect) {
            const int TOTAL_VARIANTS = 6;
            int melPage = _melSelectIdx / 4;

            if (y >= 40 && y <= 185) {
                int clickedIndex = 0;
                if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1;
                else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
                int mapIdx = (melPage * 4) + clickedIndex;
                if (mapIdx >= TOTAL_VARIANTS) return;
                if (!acceptSlideTouch(0x80 + clickedIndex)) return;

                _melSelectIdx = (uint8_t)mapIdx;
                SoundEvent evType = SND_NONE;
                switch (_melSelectType) {
                    case 0: evType = SND_TOUCH_CLICK; break;
                    case 1: evType = SND_CONFIRM;     break;
                    case 2: evType = SND_ERROR;       break;
                    case 3: evType = SND_ALARM_START; break;
                    case 4: evType = SND_ATTENTION;   break;
                }
                if (evType != SND_NONE) requestPreviewSound(evType, _melSelectIdx);
                _repaintSettings = true;
            }
            else if (y > 185) {
                if (x < 70) {
                    if (!acceptTouch(0x90)) return;
                    _melSelectIdx = (_melSelectIdx > 0) ? _melSelectIdx - 1 : TOTAL_VARIANTS - 1;
                    SoundEvent evType = SND_NONE;
                    switch (_melSelectType) {
                        case 0: evType = SND_TOUCH_CLICK; break;
                        case 1: evType = SND_CONFIRM;     break;
                        case 2: evType = SND_ERROR;        break;
                        case 3: evType = SND_ALARM_START; break;
                        case 4: evType = SND_ATTENTION;   break;
                    }
                    if (evType != SND_NONE) requestPreviewSound(evType, _melSelectIdx);
                    _repaintSettings = true;
                }
                else if (x < 138) {
                    if (!acceptTouch(0x91)) return;
                    _melSelectIdx = (_melSelectIdx < TOTAL_VARIANTS - 1) ? _melSelectIdx + 1 : 0;
                    SoundEvent evType = SND_NONE;
                    switch (_melSelectType) {
                        case 0: evType = SND_TOUCH_CLICK; break;
                        case 1: evType = SND_CONFIRM;     break;
                        case 2: evType = SND_ERROR;        break;
                        case 3: evType = SND_ALARM_START; break;
                        case 4: evType = SND_ATTENTION;   break;
                    }
                    if (evType != SND_NONE) requestPreviewSound(evType, _melSelectIdx);
                    _repaintSettings = true;
                }
                else if (x < 219) {
                    if (!acceptTouch(0x92)) return;
                    _inMelodySelect = false;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
                else {
                    if (!acceptTouch(0x93)) return;
                    switch (_melSelectType) {
                        case 0:
                            _soundSettings.touchEnabled    = true;
                            _soundSettings.touchMelody     = _melSelectIdx;
                            break;
                        case 1:
                            _soundSettings.confirmEnabled  = true;
                            _soundSettings.confirmMelody   = _melSelectIdx;
                            break;
                        case 2:
                            _soundSettings.errorEnabled    = true;
                            _soundSettings.errorMelody     = _melSelectIdx;
                            break;
                        case 3:
                            _soundSettings.alarmEnabled    = true;
                            _soundSettings.alarmMelody     = _melSelectIdx;
                            break;
                        case 4:
                            _soundSettings.attentionEnabled = true;
                            _soundSettings.attentionMelody  = _melSelectIdx;
                            break;
                    }
                    /* Paridade com web /alarms: ligar qualquer som individual desliga o mute global. */
                    _soundSettings.muted = false;
                    requestPreviewSound(SND_CONFIRM, _soundSettings.confirmMelody);

                    _inMelodySelect = false;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                }
            }
            return;
        }


        /* v3.32.3: 9 itens (Attention adicionado entre Web (4) e Mute (agora 6)). */
        const int TOTAL_SOUND_ITEMS = 9;
        int soundPage = _soundSelection / 4;

        if (y >= 40 && y <= 185) {
            int clickedIndex = 0;
            if (y < 80) clickedIndex = 0; else if (y < 118) clickedIndex = 1;
            else if (y < 156) clickedIndex = 2; else clickedIndex = 3;
            int mapIdx = (soundPage * 4) + clickedIndex;
            if (mapIdx >= TOTAL_SOUND_ITEMS) return;

            /* F13.3b: gate de toque MOVIDO para dentro dos branches.
             * O gate único no topo (acceptSlideTouch) seta _lastTouchRegion
             * para clickedIndex (0-3); o acceptHoldTouch(20/21) dos branches
             * de volume via a seguir falhava por mismatch de zoneId quando o
             * dedo continuava pressionado — bloqueava inc/dec. Agora cada
             * branch usa o accept apropriado ao seu modo de interação. */

            if (mapIdx != _soundSelection) {
                if (!acceptSlideTouch(clickedIndex)) return;
                _soundSelection = mapIdx;
                _repaintSettings = true;
            } else {
                /* v3.32.4 layout: 0=volume, 1=alarmVol, 2=touch, 3=confirm,
                 * 4=error, 5=alarm, 6=attention, 7=web, 8=mute. */
                if (mapIdx == 0) {
                    if (!acceptHoldTouch(20)) return;   /* único accept do caminho */
                    if (x < 160) { if (_soundSettings.volume >= 10) _soundSettings.volume -= 10; }
                    else          { if (_soundSettings.volume <= 90) _soundSettings.volume += 10; }
                    _touchSoundPending = false;   /* cancela bip para não sobrepor preview */
                    requestVolumePreview(_soundSettings.volume);
                    _repaintSettings = true;
                }
                else if (mapIdx == 1) {
                    if (!acceptHoldTouch(21)) return;
                    if (x < 160) { if (_soundSettings.alarmVolume >= 10) _soundSettings.alarmVolume -= 10; }
                    else          { if (_soundSettings.alarmVolume <= 90) _soundSettings.alarmVolume += 10; }
                    _touchSoundPending = false;
                    requestAlarmVolumePreview(_soundSettings.alarmVolume);
                    _repaintSettings = true;
                }
                else if (mapIdx >= 2 && mapIdx <= 6) {
                    /* Sons individuais com seleção de melodia: touch (2),
                     * confirm (3), error (4), alarm (5), attention (6). */
                    if (!acceptSlideTouch(clickedIndex)) return;
                    bool* enablePtr = nullptr;
                    uint8_t melType = 0;
                    uint8_t curMel  = 0;
                    switch (mapIdx) {
                        case 2: enablePtr = &_soundSettings.touchEnabled;
                                melType = 0; curMel = _soundSettings.touchMelody;     break;
                        case 3: enablePtr = &_soundSettings.confirmEnabled;
                                melType = 1; curMel = _soundSettings.confirmMelody;   break;
                        case 4: enablePtr = &_soundSettings.errorEnabled;
                                melType = 2; curMel = _soundSettings.errorMelody;     break;
                        case 5: enablePtr = &_soundSettings.alarmEnabled;
                                melType = 3; curMel = _soundSettings.alarmMelody;     break;
                        case 6: enablePtr = &_soundSettings.attentionEnabled;
                                melType = 4; curMel = _soundSettings.attentionMelody; break;
                    }

                    if (enablePtr && *enablePtr) {
                        *enablePtr = false;
                        _repaintSettings = true;
                    } else {
                        _inMelodySelect = true;
                        _melSelectType  = melType;
                        _melSelectIdx   = curMel;
                        _forceSettingsRedraw = true;
                        _repaintSettings = true;
                    }
                }
                else if (mapIdx == 7) {
                    if (!acceptSlideTouch(clickedIndex)) return;
                    _soundSettings.webEnabled = !_soundSettings.webEnabled;
                    /* Paridade com web /alarms: ligar Web desliga o mute global. */
                    if (_soundSettings.webEnabled) _soundSettings.muted = false;
                    _repaintSettings = true;
                }
                else if (mapIdx == 8) {
                    if (!acceptSlideTouch(clickedIndex)) return;
                    if (!_soundSettings.muted) {
                        /* Vai LIGAR Mudo Global → tela de confirmação. */
                        showMuteConfirm();
                    } else {
                        /* Desligar é direto, sem confirmação. */
                        _soundSettings.muted = false;
                        _repaintSettings = true;
                    }
                }
            }
        }
        else if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_soundSelection > 0) _soundSelection--; else _soundSelection = TOTAL_SOUND_ITEMS - 1;
                _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_soundSelection < TOTAL_SOUND_ITEMS - 1) _soundSelection++; else _soundSelection = 0;
                _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
            else {
                if (!acceptTouch(13)) return;
                UiEvent ev; ev.type = UiEvent::EVT_SAVE_SOUNDS; ev.id = 0; ev.param = 0;
                queue_try_add(&_eventQueue, &ev);
            }
        }
    }


    else if (_uiMode == MODE_SETTINGS_STATUS) {
        if (y > 185) {
            if (x < 70) {
                if (!acceptHoldTouch(10)) return;
                if (_statusPage > 0) _statusPage--; else _statusPage = STATUS_PAGES - 1;
                _forceSettingsRedraw = true; _repaintSettings = true;
            }
            else if (x < 138) {
                if (!acceptHoldTouch(11)) return;
                if (_statusPage < STATUS_PAGES - 1) _statusPage++; else _statusPage = 0;
                _forceSettingsRedraw = true; _repaintSettings = true;
            }
            else if (x < 219) {
                if (!acceptTouch(12)) return;
                showSettingsMain();
            }
        }
    }

    else if (_uiMode == MODE_SETTINGS_LICENSE) {

        if (y >= 32 && y <= 189) {
            /* Toque na área de texto: metade superior = pág anterior, inferior = próxima */
            if (y < 110) {
                if (!acceptTouch(0)) return;
                if (_licensePage > 0) _licensePage--;
            } else {
                if (!acceptTouch(1)) return;
                if (_licensePage < _licenseTotalPages - 1) _licensePage++;
            }
            _repaintSettings = true;
        }
        else if (y > 190) {
            if (x < 107) {
                if (!acceptHoldTouch(10)) return;
                if (_licensePage > 0) _licensePage--;
                _repaintSettings = true;
            }
            else if (x < 213) {
                if (!acceptHoldTouch(11)) return;
                if (_licensePage < _licenseTotalPages - 1) _licensePage++;
                _repaintSettings = true;
            }
            else {
                if (!acceptTouch(12)) return;
                if (_licenseFromAuth) {
                    _licenseFromAuth = false;
                    _uiMode = MODE_AUTH;
                    _forceSettingsRedraw = true;
                    _repaintSettings = true;
                    /* v3.37.7: licença cobriu o keypad com fillScreen — força
                     * repintar sem resetar o PIN parcial nem o estado de auth. */
                    requestAuthKeypadRedraw();
                } else {
                    showSettingsMain();
                }
            }
        }
    }


    else if (_uiMode == MODE_ALARM_ACTION) {
        if (y >= 60 && y <= 105) {
            if (!acceptTouch(0)) return;
            UiEvent ev;
            ev.type = UiEvent::EVT_ALARM_SILENCE;
            ev.id   = _alarmActionSlot;
            ev.param = 120;
            queue_try_add(&_eventQueue, &ev);
        }
        else if (y >= 115 && y <= 160) {
            if (!acceptTouch(1)) return;
            UiEvent ev;
            ev.type = UiEvent::EVT_ALARM_DEACTIVATE;
            ev.id   = _alarmActionSlot;
            ev.param = 0;
            queue_try_add(&_eventQueue, &ev);
        }
        else if (y >= 170 && y <= 215) {
            if (!acceptTouch(2)) return;
            /* Voltar ao dashboard com o painel em modo min/max */
            if (_alarmActionSlot < 0) {
                _ambientShowMinMax = true;
            } else {
                _slotShowMinMax = true;
            }
            _uiMode = MODE_DASHBOARD;
            _forceFullRedraw = true;
            mutex_enter_blocking(&_stateMutex);
            _isDirty = true;
            mutex_exit(&_stateMutex);
        }
    }
    else if (_uiMode == MODE_CONFIRM_MUTE_ALL) {
        /* 2 botões em y=190..230. Voltar (x=20..150), Confirmar (x=170..300). */
        if (y >= 190 && y <= 230) {
            if (x >= 20 && x <= 150) {
                if (!acceptTouch(0xC0)) return;
                /* Voltar — cancela, retorna ao menu Sons sem alterar nada. */
                _uiMode = MODE_SETTINGS_SOUNDS;
                _forceSettingsRedraw = true;
                _repaintSettings = true;
            }
            else if (x >= 170 && x <= 300) {
                /* v3.32.3: gate de aceitação manual (sem acceptTouch que disparava
                 * SND_TOUCH_CLICK). Tocamos SND_CONFIRM antes de aplicar mute para
                 * dar feedback de "ação confirmada" — o som chega no Core 0 e é
                 * processado antes do save. _muted no SoundManager só muda no save. */
                if (!_touchReleased) return;
                _touchReleased       = false;
                _lastTouchRegion     = 0xC1;
                _lastRegionTouchTime = millis();
                _lastTouchTimestamp  = millis();
                _touchSoundPending   = false;   /* sem bip de touch */
                requestPreviewSound(SND_CONFIRM, _soundSettings.confirmMelody);

                /* Confirmar — aplica mute=true e desliga todos os sons (paridade web). */
                _soundSettings.muted            = true;
                _soundSettings.touchEnabled     = false;
                _soundSettings.confirmEnabled   = false;
                _soundSettings.errorEnabled     = false;
                _soundSettings.alarmEnabled     = false;
                _soundSettings.webEnabled       = false;
                _soundSettings.attentionEnabled = false;
                _uiMode = MODE_SETTINGS_SOUNDS;
                _forceSettingsRedraw = true;
                _repaintSettings = true;
            }
        }
    }
}

bool DisplayManager::acceptTouch(uint8_t zoneId) {
    if (!_touchReleased) return false;

    _touchReleased       = false;
    _lastTouchRegion     = zoneId;
    _lastRegionTouchTime = millis();
    _lastTouchTimestamp  = millis();
    _touchSoundPending   = true;
    return true;
}

/**
 * @brief Aceita toque com repetição por segurar (hold-repeat).
 *
 * Primeiro toque: aceita imediatamente e toca o bip.
 * Enquanto segura: repete a cada HOLD_REPEAT_MS (300ms) com bip.
 * Usado para botões de navegação de lista e incremento/decremento.
 */
bool DisplayManager::acceptHoldTouch(uint8_t zoneId) {
    uint32_t now = millis();

    if (_touchReleased) {
        /* Primeiro toque: aceita e toca bip */
        _touchReleased       = false;
        _lastTouchRegion     = zoneId;
        _lastRegionTouchTime = now;
        _lastTouchTimestamp  = now;
        _holdRepeatLastFire  = now;
        _touchSoundPending   = true;
        return true;
    }

    /* Segurar: repete a cada 300ms com bip */
    if (zoneId == _lastTouchRegion && (now - _holdRepeatLastFire >= HOLD_REPEAT_MS)) {
        _holdRepeatLastFire = now;
        _lastTouchTimestamp = now;
        _touchSoundPending  = true;
        return true;
    }

    return false;
}

/**
 * @brief Aceita toque com deslizamento entre zonas.
 *
 * Primeiro toque: aceita imediatamente com bip.
 * Deslizar para zona diferente: aceita com bip (sem exigir release).
 * Manter na mesma zona: não repete.
 * Usado para slots, períodos de gráfico e listas de seleção.
 */
bool DisplayManager::acceptSlideTouch(uint8_t zoneId) {
    /* Primeiro toque ou deslizou para zona diferente */
    if (_touchReleased || zoneId != _lastTouchRegion) {
        _touchReleased       = false;
        _lastTouchRegion     = zoneId;
        _lastRegionTouchTime = millis();
        _lastTouchTimestamp  = millis();
        _touchSoundPending   = true;
        return true;
    }

    /* Mesma zona, segurando: não repete */
    return false;
}
