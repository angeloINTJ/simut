/**
 * @file    AppManager_Boot.cpp
 * @brief   Boot sequence: filesystem, sensors, network, web server initialization.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "CommandManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "SoundManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "src/ota/metadata.h"
#include "src/ota/config_snapshot.h"
#include "TelemetryManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include "WebManager.h"
#include <LittleFS.h>
#include <hardware/uart.h>
#include <hardware/gpio.h>
#include <time.h>

extern AppManager app;

/* v3.44.0-alpha27: BOOT_LOG fan-out — Serial USB CDC + UART1 raw.
 * Helpers static (file-scope) pra inline no callsite e economizar flash.
 * Externs adicionais (BootUart.cpp) inflavam o binário em ~3 KiB.
 * UART1 default arduino-pico Serial2 = GP8/GP9 (alpha25 fix). */
static inline void boot_uart_init() {
    uart_init(uart1, 115200);
    gpio_set_function(8, GPIO_FUNC_UART);  /* TX (UART1) */
    gpio_set_function(9, GPIO_FUNC_UART);  /* RX (UART1, idle pra futuro bidir) */
}
static void _bu_str(const char* s) {
    while (*s) uart_putc_raw(uart1, *s++);
}
static void _bu_u32(uint32_t v) {
    char buf[11]; int n = 0;
    if (!v) { uart_putc_raw(uart1, '0'); return; }
    while (v && n < 10) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n--) uart_putc_raw(uart1, buf[n]);
}
#define BLOG(s)   do { Serial.print(s); _bu_str(s); } while(0)
#define BLOG_U(v) do { uint32_t _vv=(uint32_t)(v); Serial.print(_vv); _bu_u32(_vv); } while(0)
#define BLOG_NL() do { Serial.print('\n'); uart_putc_raw(uart1, '\n'); } while(0)

void AppManager::setup() {
    Serial.begin(115200);

    /* v3.44.0-alpha10: ignoreFlowControl(true) — Arduino-Pico SerialUSB::write
     * silenciosamente DROPA writes se tud_cdc_connected() é false (host
     * não open port + não raised DTR). Pós-watchdog reset do applier,
     * USB CDC re-enumera mas host pode não conectar a tempo de capturar
     * os primeiros prints — boot output perdido = "USB enumera mas
     * serial mute" sintoma observado em residual OTA brick.
     * Com ignoreFlowControl, writes tentam por até 1s (timeout interno do
     * SerialUSB) — buffereados na FIFO TX da TinyUSB, host vê quando
     * conecta. Trade-off: cada Serial.println adiciona até 1s se host
     * não conectado. Aceitável pra debug; revisitar pós-v4 stable. */
    Serial.ignoreFlowControl(true);

    /* v3.44.0-alpha24: UART1 raw init — GP4 TX wired ao PicoHand GP5 RX.
     * Captura boot logs quando USB CDC fica mute por F-USB-CDC-DEAD pós-OTA. */
    boot_uart_init();

    delay(1000);

    /* Log da versão ANTES de qualquer init que possa travar — garante que
     * o user sempre saiba qual firmware está rodando, mesmo se o boot
     * trancar logo depois. F-OTA-BOOTLOOP boot tracing: BOOT_LOGF escreve
     * em USB CDC + UART1, snprintf-based. Permite localizar exatamente
     * onde o boot pós-apply trava sem precisar de reflash. */
    BLOG("\n==============================================\n");
    BLOG("  SIMUT firmware "); BLOG(SIMUT_VERSION); BLOG_NL();
    BLOG("==============================================\n");
    BLOG("[BOOT step] 1: pos-banner @ "); BLOG_U(millis()); BLOG_NL();

    /*
     * NÃO chamar TRACE_MOD aqui — scratch[4] precisa conter o módulo do
     * crash anterior até a autópsia ler (em LogManager::begin abaixo).
     * TRACE_BEAT(0) é OK: só mexe em RAM (_coreHeartbeat), não no scratch.
     */
    TRACE_BEAT(0);

    BLOG("[BOOT step] 2: _displayMgr->begin() @ "); BLOG_U(millis()); BLOG_NL();
    _displayMgr->begin();
    BLOG("[BOOT step] 3: _displayMgr->startCore1() @ "); BLOG_U(millis()); BLOG_NL();
    _displayMgr->startCore1();

    /* v3.44.0-alpha11: Achado #7 fix — aguardar Core 1 estar PRONTO antes
     * de prosseguir. _displayMgr->startCore1() retorna após
     * multicore_launch_core1, mas Core 1 ainda precisa rodar até
     * loopCore1::multicore_lockout_victim_init() pra setar _core1Ready.
     * Se Core 0 chamar enterFlashSafeMode (step 5+) antes desse setpoint,
     * multicore_lockout_start_timeout_us vai timeout 10s + recovery
     * (mensagem [DSP] Lockout stuck >10s) — observado empíricamente em
     * alpha10 captura HW 2026-05-08 (boot trava entre step 3 e 4).
     *
     * Wait com timeout de segurança (1500ms): victim_init é alguns
     * usegundos no caminho normal. Se não pronto em 1.5s, Core 1 está
     * defunct — boot continua sem ele (display fica off, mas firmware
     * não trava). */
    {
        unsigned long wait_start = millis();
        while (!_displayMgr->isCore1Ready() && millis() - wait_start < 1500) {
            tight_loop_contents();
        }
        BLOG("[BOOT step] 3.5: core1Ready="); BLOG_U(_displayMgr->isCore1Ready() ? 1 : 0);
        BLOG(" wait="); BLOG_U(millis() - wait_start);
        BLOG("ms @ "); BLOG_U(millis()); BLOG_NL();
    }

    BLOG("[BOOT step] 4: pos-startCore1 @ "); BLOG_U(millis()); BLOG_NL();
    LOG_CODE(LOG_INFO, "APP", APP_DISPLAY_LAUNCHED, 0, TRL("Display UI Launched on Core 1."));

    delay(BOOT_STEP_DELAY_MS);

    bool forceAP = false;
    _displayMgr->setBootStatusKey(TR_BOOT_HOLD_AP);

    /* Touch settle + sanity gate: alguns XPT2046 reportam touched()=true
     * permanentemente logo após boot (controller em estado indeterminado
     * antes do primeiro Z-axis sample, ou ruído elétrico no PENIRQ).
     * Sem este gate, o boot detecta esse stale-true como gesture de AP-hold
     * e o device cai em AP mode sozinho a cada reboot.
     *
     * Estratégia: aguardar 200ms de quiet consecutivo (até 1500ms cap).
     * - Se viu quiet → touch funcional → janela AP detect normal.
     * - Se NÃO viu quiet → touch stuck-true (bug HW/calib) → bypass janela.
     *   AP por touch fica indisponível enquanto stuck; rota é fix touch
     *   (`conf system touch reset` ou recalibrar) ou power cycle limpo. */
    bool touch_settled = false;
    {
        unsigned long settle_start = millis();
        unsigned long quiet_since = 0;
        while (millis() - settle_start < 1500) {
            TRACE_BEAT(0);
            if (_displayMgr->isScreenTouched()) {
                quiet_since = 0;
            } else {
                if (quiet_since == 0) quiet_since = millis();
                if (millis() - quiet_since >= 200) { touch_settled = true; break; }
            }
            delay(20);
        }
        BLOG("[BOOT] touch settle: quiet="); BLOG_U(touch_settled ? 1 : 0);
        BLOG(" ms="); BLOG_U(millis() - settle_start); BLOG_NL();
    }

    if (touch_settled) {
        unsigned long waitStart = millis();
        while (millis() - waitStart < AP_DETECT_WINDOW_MS) {
            TRACE_BEAT(0);

            if (_displayMgr->isScreenTouched()) {
                unsigned long holdStart = millis();
                bool held = true;
                int missedTouches = 0;

                while (millis() - holdStart < AP_HOLD_DURATION_MS) {
                    TRACE_BEAT(0);
                    if (!_displayMgr->isScreenTouched()) {
                        missedTouches++;
                        if (missedTouches > AP_HOLD_MAX_MISSED) {
                            held = false;
                            _displayMgr->setApProgress(-1);
                            _displayMgr->setBootStatusKey(TR_BOOT_AP_CANCELLED, nullptr, false);
                            delay(800);
                            break;
                        }
                    } else {
                        missedTouches = 0;
                    }
                    int pct = map(millis() - holdStart, 0, AP_HOLD_DURATION_MS, 0, 100);
                    _displayMgr->setApProgress(pct);
                    delay(50);
                }
                if (held) forceAP = true;
                break;
            }
            delay(50);
        }
    }
    BLOG("[BOOT] AP detect: forceAP="); BLOG_U(forceAP ? 1 : 0);
    BLOG(" (touch_settled="); BLOG_U(touch_settled ? 1 : 0); BLOG(")\n");

    _displayMgr->setApProgress(-1);

    _storageMgr->setLockCallback([](bool lock) {
        app.pauseDisplayForFlash(lock);
    });

    LogManager::instance().setLockCallback([](bool lock) {
        app.pauseDisplayForFlash(lock);
    });

    /* F-LOCKOUT-STUCK: wire quiet mode cooperativo para saveConfiguration.
     * Core 0 sinaliza, Core 1 congela em loop RAM-only, Core 0 faz flash
     * ops sem cascatas de lockout IRQ stuck. Retorna true só se Core 1 ACKed. */
    _storageMgr->setBigSaveQuietCallback([](bool enable) -> bool {
        return app.requestDisplayQuietMode(enable);
    });

    BLOG("[BOOT step] 5: pre _storageMgr->begin() @ "); BLOG_U(millis()); BLOG_NL();
    _displayMgr->setBootStatusKey(TR_BOOT_MOUNT_FS);
    bool fsOk = _storageMgr->begin();
    BLOG("[BOOT step] 6: pos _storageMgr->begin() fsOk="); BLOG_U(fsOk ? 1 : 0);
    BLOG(" @ "); BLOG_U(millis()); BLOG_NL();

    /* DisplayManager precisa do ponteiro pra config pra renderizar o dashboard
     * (buildDashLayout filtra slots inativos). Seta UMA vez no boot — o cfg
     * vive em BSS (membro de StorageManager) e nunca é realocado. */
    _displayMgr->setSysConfig(&_storageMgr->getConfig());

    _displayMgr->setBootStatusKey(TR_BOOT_START_LOG);
    LogManager::instance().begin(fsOk, LOG_DEBUG);

    /* Autópsia já leu scratch[4]. Agora pode setar MOD_BOOT para rastrear
     * estalls que aconteçam durante o restante do setup. */
    TRACE_MOD(0, MOD_BOOT);

    /* Fase 7 OTA: detecção pós-apply.
     *
     * Se metadata.state == APPLYING ou POST_BOOT, este boot ocorreu logo
     * após o orchestrator ter disparado watchdog_reboot. Em 7a (no-op):
     * nada precisa ser limpo no FS, só clear da metadata. Em 7b (real):
     * Fase 8 vai reformatar LFS aqui antes de subir o web em modo
     * "aguardando restore".
     *
     * Logamos via Serial + LOG_CODE pra rastreabilidade — esta linha só
     * deve aparecer em boot pós-apply, qualquer outra ocasião é bug.
     *
     * ota_metadata_clear faz flash_range_erase — exige Core 1 pausado
     * (já está rodando neste ponto via _displayMgr->startCore1). Wrap
     * com enterFlashSafeMode/exit. */
    {
        ota::UpdateMetadata m;
        if (ota::ota_metadata_read(m) &&
            (m.state == ota::STATE_APPLYING || m.state == ota::STATE_POST_BOOT)) {
            const bool snap_present = ota::ota_snapshot_present();
            BLOG("[BOOT] OTA post-apply detected: state="); BLOG_U(m.state);
            BLOG(" attempts="); BLOG_U(m.attempts);
            BLOG(" snapshot="); BLOG(snap_present ? "present" : "absent"); BLOG_NL();
            LOG_CODE(LOG_WARN, "OTA", SEC_CONFIG_CHANGED, 0,
                     String("post-apply boot, state=") + (int)m.state +
                     " attempts=" + (int)m.attempts +
                     (snap_present ? " snap=ok" : " snap=absent"));
            /* Snapshot já foi consumido pelo StorageManager::begin (restore
             * pré-loadConfiguration). Limpamos a metadata partition agora —
             * apaga UpdateMetadata + snapshot region juntos (factory state). */
            _storageMgr->enterFlashSafeMode();
            ota::ota_metadata_clear();
            _storageMgr->exitFlashSafeMode();
        }
    }

    BLOG("[BOOT step] 7: pos OTA detect @ "); BLOG_U(millis()); BLOG_NL();

    LogManager::instance().setHeavyTaskChecker([]() -> bool {
        return app._storageMgr->isHeavyTaskLocked();
    });


    /* REF-004: provider único em vez de 3 setters duplicados (Log/Storage/Web).
     * Setado aqui, antes que qualquer manager possa consultar via
     * TouchPriority::isActive() durante o boot. */
    TouchPriority::setProvider([]() -> bool {
        return app.isUserInteracting();
    });

    BLOG("[BOOT step] 8: pre _cmdMgr->begin @ "); BLOG_U(millis()); BLOG_NL();
    _displayMgr->setBootStatusKey(TR_BOOT_START_CMD);
    /* v3.33.1: nome do BT visível na rede agora vem do `cfg.deviceName`
     * (configurável em /config) em vez do default "PicoW Serial XX:XX:..."
     * da lib SerialBT. Mudanças via web exigem reboot (já é o fluxo do
     * "Salvar e Reiniciar"). */
    _cmdMgr->begin(_storageMgr->getConfig().deviceName);

    _cmdMgr->setBtValidator([this](String attempt) -> bool {
        SystemConfig &cfg = _storageMgr->getConfig();
        if (!cfg.users[0].active) return false;
        /* Frontend envia SHA256(plaintext) antes do hashPassword;
         * sha256Hex espelha esse comportamento (UTF-8 → Latin-1). */
        String preHash = _storageMgr->sha256Hex(attempt);
        String storedHash = String(cfg.users[0].password);
        /* SEC-007: suporta tanto legacy (30 chars) quanto v1 (32 chars). */
        if (cfg.users[0].hashVersion == 0 && storedHash.length() == 30) {
            String legacyHash = _storageMgr->hashPasswordLegacy(
                String(cfg.users[0].username), preHash);
            return (legacyHash == storedHash);
        } else {
            String hashed = _storageMgr->hashPasswordV1(
                String(cfg.users[0].username), preHash, cfg.users[0].salt);
            return (hashed == storedHash);
        }
    });

    if (!fsOk) LOG_CODE(LOG_ERROR, "APP", APP_STORAGE_CRITICAL, 0, TRL("Storage Critical Failure!"));

    /* SEC-003/F12.3: se o dispositivo subiu em factory defaults (config
     * inexistente ou corrompida nos dois bancos), exibe a senha inicial
     * aleatória no Serial — exige acesso físico USB. Também loga no FS via
     * LOG_CODE pra trilha de auditoria. Plaintext nunca persiste em flash. */
    if (_storageMgr->isFactoryDefaults()) {
        const char* pw = _storageMgr->getInitialAdminPassword();
        if (pw && pw[0] != '\0') {
            Serial.println(F("\n=============================================="));
            Serial.println(F("  SEC-003: FACTORY DEFAULTS ATIVADO"));
            Serial.print  (F("  Senha ADMIN inicial: "));
            Serial.println(pw);
            Serial.println(F("  Trocar no primeiro login (forcado)."));
            Serial.println(F("=============================================="));
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, 0,
                     TRL("Factory defaults active; initial admin pass on USB/serial."));
        } else {
            /* Caso raro: factory detectado mas plaintext não está em RAM
             * (loadConfiguration limpou após fallback). Avisa sem vazar. */
            LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, 0,
                     TRL("Factory defaults active; password regen required."));
        }
    }

    uint32_t lastTs = _storageMgr->getLastRecordedTimestamp();
    _netMgr->setProvisionalTime(lastTs);
    _netMgr->setTimeSyncCallback([](uint32_t bootTs, int32_t delta) {


        app._timeSyncBootTs = bootTs;
        app._timeSyncDelta = delta;
        __dmb();  /* Memory barrier: garante que bootTs/delta são visíveis antes da flag */
        app._pendingTimeSync = true;
    });

    SystemConfig &cfg = _storageMgr->getConfig();
    _displayMgr->setBootStatusKey(TR_BOOT_LOAD_THEME_LANG);
    scanCustomThemes();
    loadTheme(cfg.themeIndex);
    _displayMgr->refreshTheme();
    DisplayManager::findAndLoadLangFile();
    _displayMgr->setLanguage(cfg.displayLang);


    _soundMgr->begin();
    {
        const SoundConfigData* sndCfg = reinterpret_cast<const SoundConfigData*>(
            cfg.reserved + sizeof(TouchCalData));
        _soundMgr->loadConfig(sndCfg);
    }

    /* Offset de posicionamento do display — aplicado antes de qualquer tela
     * subsequente para que boot statuses já reflitam o alinhamento salvo. */
    {
        const DisplayOffsetData* ofs = reinterpret_cast<const DisplayOffsetData*>(
            cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData));
        _displayMgr->loadDisplayOffset(ofs);
    }

    /* B4: modo CLI (debug/config). Default = CONFIG (debug OFF) se magic inválido. */
    {
        const CliConfigData* cli = reinterpret_cast<const CliConfigData*>(
            cfg.reserved + CLI_CONFIG_OFFSET);
        bool debugOn = (cli->magic == CLI_CONFIG_MAGIC) && (cli->debugMode != 0);
        LogManager::instance().setConsoleStream(debugOn);
        _cmdMgr->setDebugMode(debugOn);
    }

    /* #2: idioma da CLI reutiliza cfg.displayLang (single source of truth).
     *     Propaga também para LogManager (labels de translateCode). */
    _cmdMgr->setCliLang(cfg.displayLang);
    LogManager::instance().setLanguage(cfg.displayLang);


    {
        const TouchCalData* cal = reinterpret_cast<const TouchCalData*>(cfg.reserved);
        _displayMgr->loadTouchCalibration(cal);
        if (!_displayMgr->isTouchCalibrated()) {
            /* Factory boot sem cal salva: aplica default seguro para destravar
             * o boot. Sem isso, o cal-screen-loop ficaria preso à espera de
             * 4 taps válidos — e em XPT2046 com touch stuck-true (este HW)
             * o loop é infinito ou produz cal degenerada com coordenadas
             * iguais. User recalibra depois via Settings → Touch Cal ou via
             * CLI ('conf system touch reset' não recalibra mas reseta defaults).
             * Default range 200..3900 cobre a maioria dos painéis XPT2046. */
            Serial.printf("[BOOT] no touch cal saved (touch_settled=%d) — applying default\n",
                          touch_settled ? 1 : 0);
            TouchCalData* calOut = reinterpret_cast<TouchCalData*>(cfg.reserved);
            calOut->magic      = 0xCA;
            calOut->flags      = 0;
            calOut->xMin       = 200;
            calOut->xMax       = 3900;
            calOut->yMin       = 200;
            calOut->yMax       = 3900;
            calOut->zThreshold = 400;
            _displayMgr->loadTouchCalibration(calOut);
            _storageMgr->saveConfiguration();
            LOG_CODE(LOG_WARN, "APP", APP_TOUCH_CAL_REQUIRED, 0,
                     TRL("Touch cal missing; default applied (recalibrate via Settings)"));
        }
    }

    _displayMgr->setBootStatusKey(TR_BOOT_LOAD_PERIPH);
    _sensorMgr->begin();
    loadAndCalibrateSensors();
    _sensorMgr->setDs18Resolution((DS18B20PIO::Resolution)cfg.ds18Resolution);

    BLOG("[BOOT step] 9: pre _netMgr (forceAP="); BLOG_U(forceAP ? 1 : 0);
    BLOG(") @ "); BLOG_U(millis()); BLOG_NL();
    if (forceAP) {
        LOG_CODE(LOG_WARN, "APP", APP_AP_MODE_TRIGGERED, 0, TRL("User triggered AP mode."));
        _displayMgr->setBootStatusKey(TR_BOOT_START_AP);
        _displayMgr->setBootStatusKey(TR_BOOT_AP_NETWORK);
        _displayMgr->setBootStatusKey(TR_BOOT_AP_IP);
        _netMgr->beginAP(cfg.deviceName);
        for (int i = 0; i < 35; i++) { delay(100); feedWdt(); }
    } else {
        _displayMgr->setBootStatusKey(TR_BOOT_START_WIFI);
        BLOG("[BOOT step] 10: pre _netMgr->begin() @ "); BLOG_U(millis()); BLOG_NL();
        _netMgr->begin(cfg,
                      _storageMgr->isDnsAuto(),
                      _storageMgr->isNtpEnabled(),
                      _storageMgr->getSecondaryDns());
        BLOG("[BOOT step] 11: pos _netMgr->begin() @ "); BLOG_U(millis()); BLOG_NL();

        unsigned long netWait = millis();
        unsigned long lastMsg = 0;
        bool skipped = false;

        int dotCount = 0;
        int waitState = 0;

        while (!_netMgr->isConnected() || !_netMgr->isTimeSynced()) {
            TRACE_BEAT(0);
            _netMgr->update();

            if (_displayMgr->isSkipPressed()) {
                _displayMgr->setBootStatusKey(TR_BOOT_WIFI_SKIPPED);
                skipped = true;
                delay(1000);
                break;
            }

            if (timeSince(lastMsg, BOOT_WAIT_DOT_INTERVAL_MS)) {
                dotCount++;
                if (dotCount > 4) dotCount = 0;
                String dots = "";
                for (int i = 0; i < dotCount; i++) dots += ".";

                if (!_netMgr->isConnected()) {
                    if (waitState != 1) {
                        waitState = 1; dotCount = 0;
                        _displayMgr->setBootStatusKey(TR_BOOT_WAITING_ROUTER, nullptr, true);
                    } else {
                        _displayMgr->replaceBootStatusKey(TR_BOOT_WAITING_ROUTER, dots.c_str(), true);
                    }
                } else if (!_netMgr->isTimeSynced()) {
                    if (waitState != 2) {
                        waitState = 2; dotCount = 0;
                        _displayMgr->setBootStatusKey(TR_BOOT_SYNC_NTP, nullptr, true);
                    } else {
                        _displayMgr->replaceBootStatusKey(TR_BOOT_SYNC_NTP, dots.c_str(), true);
                    }
                }
                lastMsg = millis();
            }

            if (timeSince(netWait, 30000)) {
                 _displayMgr->setBootStatusKey(TR_BOOT_NET_TIMEOUT);
                 delay(1000);
                 break;
            }
            delay(50);
        }

        if (!skipped && _netMgr->isConnected()) {
            _displayMgr->setBootStatusKey(TR_BOOT_NET_CONNECTED);
            delay(500);
        }
    }

    /* alpha28: SerialBT.begin movido pra DEPOIS de _netMgr->begin (ambas
     * branches AP e STA). WiFi.begin → cyw43_arch_init reseta o chip CYW43
     * em estado limpo. Antes (alpha27 e anteriores), SerialBT.begin rodava
     * em step 8 (_cmdMgr->begin) com CYW43 ainda em estado residual pós-OTA
     * → hardfault → reset spontaneo entre step 8 e 9. Diagnosticado via
     * UART bridge: boot 1 reach step 8 then BANNER (= reset), boot 2 OK.
     *
     * Cobertura: tanto AP como STA chamam algum WiFi.* primeiro, então
     * cyw43_arch_init já rodou aqui. */
    BLOG("[BOOT step] 11.5: cmdMgr->beginBluetooth @ "); BLOG_U(millis()); BLOG_NL();
    _cmdMgr->beginBluetooth(_storageMgr->getConfig().deviceName);
    BLOG("[BOOT step] 11.6: pos beginBluetooth @ "); BLOG_U(millis()); BLOG_NL();

    _displayMgr->setBootStatusKey(TR_BOOT_START_TEL);
    _telemetryMgr->begin(_storageMgr.get(), _netMgr.get());

    LogManager::instance().setEpochSource([]() -> time_t { return time(nullptr); });

    BLOG("[BOOT step] 12: pre _webMgr->begin() @ "); BLOG_U(millis()); BLOG_NL();
    _displayMgr->setBootStatusKey(TR_BOOT_START_WEB);
    _webMgr->begin(_storageMgr.get(), _sensorMgr.get(), _netMgr.get(), _displayMgr.get(), _telemetryMgr.get(), _soundMgr.get());
    BLOG("[BOOT step] 13: pos _webMgr->begin() @ "); BLOG_U(millis()); BLOG_NL();

    _displayMgr->setBootStatusKey(TR_BOOT_REG_CALLBACKS);
    _webMgr->setYieldCallback([this]() { this->core0Yield(); });
    _webMgr->setLightYieldCallback([this]() {
        feedWdt();


        static uint32_t lastLiveUpdate = 0;
        uint32_t now = millis();
        if (now - lastLiveUpdate > 3000) {
            lastLiveUpdate = now;
            _sensorMgr->update();
            updateLiveDisplay();
        }
    });


    /* REF-004: _webMgr->setTouchPriorityChecker removido — usa TouchPriority singleton. */

    if (forceAP) {
        _isApMode = true;
        _displayMgr->setBootStatusKey(TR_BOOT_AP_ACTIVE, nullptr, false);
        LOG_CODE(LOG_INFO, "APP", APP_READY_AP, 0, TRL("System ready (AP mode)."));
    } else {

        /* Carrega min/max do dia a partir do arquivo de histórico */
        _displayMgr->setBootStatusKey(TR_BOOT_LOAD_MINMAX);
        delay(80);
        preloadMinMax();

        _displayMgr->setBootStatusKey(TR_BOOT_WARMUP);
        {
            unsigned long warmStart = millis();


            while (millis() - warmStart < 2000) {
                feedWdt();
                _sensorMgr->update();


                if (timeSince(warmStart, 900)) break;

                delay(10);
            }


            updateLiveDisplay();
            refreshSelectedSlot();
        }


        if (_pendingTimeSync) {
            _displayMgr->setBootStatusKey(TR_BOOT_CORRECT_TS);
            delay(80);
            handleTimeSync(_timeSyncBootTs, _timeSyncDelta);

            /* Recarrega min/max com timestamps corrigidos */
            _displayMgr->setBootStatusKey(TR_BOOT_RELOAD_MINMAX);
            delay(80);
            for (int i = 0; i < MINMAX_SLOT_COUNT; i++) {
                _cachedMin[i] = 1000.0f; _cachedMax[i] = -1000.0f;
                _preloadMin[i] = 1000.0f; _preloadMax[i] = -1000.0f;
            }
            _cachedHumMin = 1000.0f; _cachedHumMax = -1000.0f;
            _preloadHumMin = 1000.0f; _preloadHumMax = -1000.0f;
            preloadMinMax();
        }


        _displayMgr->setBootStatusKey(TR_BOOT_PREP_DASH);
        _sensorMgr->update();
        updateLiveDisplay();
        refreshSelectedSlot();

        _displayMgr->setBootStatusKey(TR_BOOT_ALL_INIT);
        _displayMgr->setBootStatusKey(TR_BOOT_SYS_READY);
        delay(800);
        LOG_CODE(LOG_INFO, "APP", APP_READY, 0, TRL("System ready."));
        _displayMgr->endBoot();
        _bootCompletedAt = millis();
        BLOG("[BOOT step] 14: SYS READY @ "); BLOG_U(millis()); BLOG_NL();


        _soundMgr->play(SND_CONFIRM);
    }

    /*
     * Habilita monitoramento cross-core APÓS boot completo.
     * Força refresh de heartbeats de ambos os cores para evitar
     * detecção falsa de heartbeat estagnado durante o boot.
     * O grace period de 5s começa a contar a partir daqui.
     */
    LogManager::instance().enableHealthCheck();

    TRACE_MOD(0, MOD_IDLE);
    _cmdMgr->printPrompt();
}

/* =========================================================================== */
/*                MAIN LOOP — PRIORITY-BASED TASK SCHEDULING                 */
/* =========================================================================== */
/**
 * @brief Main application loop with cross-core health monitoring.
 *
 * Execution order (every cycle):
 *   1. Cross-core health check + pause watchdog
 *   2. CLI input processing
 *   3. Network keepalive (always runs)
 *   4. Web server request handling
 *   5. Telemetry upload (deferred during menu/touch/heavy tasks)
 *   6. Sensor auto-heal check (every 3s)
 *   7. NTP timestamp correction (if pending)
 *   8. History CSV logging (every 60s)
 *   9. UI event dispatch + sound processing
 */
