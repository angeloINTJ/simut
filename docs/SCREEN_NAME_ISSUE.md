# Issue: `screen <NAME>` CLI não muda display TFT (alpha15-19)

## Comportamento observado em HW (2026-05-09 alpha19)

```
SIMUT> screen dash
OK: dash
SIMUT> screen set
OK: set
SIMUT> screen thm
OK: thm
```

CLI responde "OK" para todos os screen names válidos. **MAS o display TFT não muda** — continua exibindo Dashboard.

Comprovação via captures:
- 6 telas capturadas via /api/screenshot_chunk (alpha19 multi-sample, CRC32 verified)
- Diff entre `screen dash` capture vs `screen set` capture = **2.8% pixels** apenas
- Esses 2.8% são na área central (animation do relógio + sensor data)
- Se telas fossem realmente diferentes (settings menu vs dashboard panels) → ~50%+ diff

## Implementação atual (AppManager_Commands.cpp::CMD_GOTO_SCREEN)

```cpp
case CMD_GOTO_SCREEN: {
    const char* n = cmd.strVal1;
    if (!strcmp(n, "dash"))     _displayMgr->forceDashboard();
    else if (!strcmp(n, "set")) _displayMgr->showSettingsMain();
    else if (!strcmp(n, "thm")) _displayMgr->showSettingsThemes(...);
    // ...
}
```

show*Screen methods (e.g., showSettingsMain):
```cpp
void DisplayManager::showSettingsMain() {
    mutex_enter_blocking(&_stateMutex);
    _uiMode = MODE_SETTINGS_MAIN; _menuSelection = 0;
    _forceSettingsRedraw = true; _repaintSettings = true;
    mutex_exit(&_stateMutex);
}
```

DisplayManager.cpp loopCore1 dispatch (line 820):
```cpp
else if (_uiMode == MODE_SETTINGS_MAIN) {
    if (_repaintSettings) { drawSettingsMain(); _repaintSettings = false; }
}
```

Logica está correta. Por que não funciona?

## Hipóteses

### A) Mutex contention entre Core 0 (CLI) e Core 1 (render)

CLI executa em Core 0 (commandManager loop). Settings show*Screen entra `_stateMutex`. Core 1 loopCore1 também usa `_stateMutex` em outros pontos. Se Core 1 está segurando por longo tempo, Core 0 espera + show*Screen demora.

### B) loopCore1 dispatch path skipping settings branches

Olhando linha 690-810: `if (_uiMode == MODE_DASHBOARD) { ... grande bloco ... }` — esta é a primeira branch e ativa sempre que MODE_DASHBOARD. Se for outra mode, pula pra `else if` chain das demais.

Mas dentro do dashboard branch tem código que inclui `pullSnapshot + render`. Esse render é bom — chama drawDashboard etc.

Se `_uiMode == MODE_SETTINGS_MAIN`, dashboard branch é skipped → vai pra `else if MODE_SETTINGS_MAIN` → drawSettingsMain. Deveria funcionar.

**Possível bug:** pullSnapshot/render no dashboard branch pode estar ainda em execução quando _uiMode muda. Race condition entre setUiMode (Core 0) e render frame em curso (Core 1).

### C) screenshot endpoint pauseRendering interfere

Cada captura chunked = 15 pause/unpause cycles em Core 1. Se isso atrapalha Core 1 de processar a mudança de _uiMode, ele renderizá só durante GAPS entre captures.

**Teste pra próxima sessão:**
1. Sem capturar nada, fazer `screen set` via CLI
2. Esperar 10s
3. Olhar fisicamente a tela (não capturar)
4. Se mudou para settings → problema é capture-side
5. Se NÃO mudou → problema é show*Screen ou loopCore1

### D) Touch event override

Touch handler tem código que reseta _uiMode para MODE_DASHBOARD em certas condições (timeSince(_lastTouchTime, 30000) → forceDashboard). Se isso dispara entre show*Screen e Core 1 render, reset para dashboard.

### E) Boot screen / isBooting flag

No DisplayManager `_sharedState.isBooting` controla algumas paths. Pode estar travado em true.

## Fix candidates (próxima sessão)

### Fix 1: Add Serial debug
```cpp
case CMD_GOTO_SCREEN: {
    // ... show*Screen call ...
    delay(100);  // give Core 0 time
    Serial.printf("[DBG] _uiMode=%d after show*Screen\n", _displayMgr->getCurrentMode());
    break;
}
```
Verifica se _uiMode realmente mudou.

### Fix 2: Force Core 1 reset before show*Screen
```cpp
_displayMgr->requestQuietMode();
delay(100);
_displayMgr->showSettingsMain();
_displayMgr->releaseQuietMode();  // relaunch Core 1 com novo _uiMode
```

### Fix 3: Skip the screen NAME approach, use HTTP API directly
Maybe SIMUT has API endpoints that change UI mode. /api/touch?x=X&y=Y with simulated touch coords would invoke the same handler that touch does. **Possible novo endpoint** /api/setscreen?name=X.

### Fix 4: Change capture script to NOT use /api/screenshot_chunk during test
Use existing /api/screenshot (simpler), to rule out chunk endpoint interfering with Core 1 render between calls.

## Status

Capture pipeline funciona PERFEITAMENTE — alpha19 multi-sample resolve corruption. Mas screen change não funciona, então todas as captures são da mesma tela (dashboard com diferentes momentos do clock).

🚧 Pendente para alpha20+ próxima sessão.
