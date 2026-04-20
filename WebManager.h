/**
 * @file    WebManager.h
 * @brief   Embedded web server with multi-user sessions, RBAC, and brute-force protection.
 * @details Provides a full web interface on the Pico W using the Arduino WebServer
 * library. Features multi-session authentication (3 simultaneous users),
 * role-based access control via permission bitmasks, challenge-response
 * login with HMAC nonces, per-IP rate limiting, SendGuard (hardware
 * timer for WDT during long sends), RAII guards for rendering and
 * flash access, and gzip-compressed asset delivery.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <functional>
#include "SystemDefs.h"
#include "StorageManager.h"
#include "SensorManager.h"
#include "NetworkManager.h"
#include "DisplayManager.h"
#include "TelemetryManager.h"
#include "SoundManager.h"
#include <bearssl/bearssl_hash.h>

class WebManager {
public:
    typedef std::function<void()> YieldCallback;
    typedef std::function<void()> LightYieldCallback;

    WebManager();
    void begin(StorageManager* storage, SensorManager* sensors,
               NetworkManager* net, DisplayManager* display,
               TelemetryManager* telemetry,
               SoundManager* sound);
    void update();
    void setYieldCallback(YieldCallback cb) { _yieldCb = cb; }
    void setLightYieldCallback(LightYieldCallback cb) { _lightYieldCb = cb; }
    uint32_t getCachedFlashUsed()  const { return _cachedFsUsedBytes; }
    uint32_t getCachedFlashTotal() const { return _cachedFsTotalBytes; }


    void setTouchPriorityChecker(bool (*fn)()) { _isTouchPriorityFn = fn; }

private:
    /** Responde 503 com Retry-After se user está interagindo com o display.
     *  @return true se 503 foi enviado (caller deve fazer early return). */
    bool rejectIfTouchPriority();

    WebServer _server;
    YieldCallback _yieldCb = nullptr;
    LightYieldCallback _lightYieldCb = nullptr;
    bool (*_isTouchPriorityFn)() = nullptr;

    StorageManager* _storageRef;
    SensorManager* _sensorRef;
    NetworkManager* _netRef;
    DisplayManager* _displayRef;


    struct RateEntry { uint32_t ip = 0; uint32_t lastReq = 0; uint8_t hits = 0; };
    RateEntry _rateLimits[RATE_LIMIT_SLOTS];
    File _uploadFile;


    uint32_t _cachedFsTotalBytes = 0;
    uint32_t _cachedFsUsedBytes = 0;
    uint32_t _lastFsInfoRefresh = 0;


    struct ActiveSession {
        String token;
        int userId;
        String username;
        uint16_t perms;
        uint32_t lastActivity;
    };
    ActiveSession _activeSessions[3];


    uint16_t _currentUserPerms;
    int _currentUserId;
    String _currentUserName;


    struct LoginState {
        uint32_t ip = 0;
        String nonce = "";
        uint32_t nonceCreatedAt = 0;
        uint8_t failCount = 0;
        uint32_t lockoutUntil = 0;
        uint32_t lastActivity = 0;
    };
    static const uint32_t NONCE_LIFETIME_MS = 60000;
    LoginState _loginStates[LOGIN_STATE_SLOTS];


    volatile bool _isProcessingScreenshot = false;
    volatile bool _cancelScreenshot = false;
    volatile bool _inHistoryHandler = false;


    inline bool isClientGone() {
        /* Wrap-safe: millis() wrap a cada ~49,7d quebraria este timeout. */
        if (_handlerDeadline > 0 && timeReached(_handlerDeadline)) {
            return true;
        }
        /* SendGuard atingiu o teto de alimentação do watchdog:
         * aborta handler limpa em vez de deixar o WDT disparar. */
        extern volatile bool _sendGuardExpired;
        if (_sendGuardExpired) {
            return true;
        }
        return !_server.client().connected();
    }


    void initSendGuardTimer();
    bool safeSend(const char* content);
    bool safeSend(const char* data, size_t len);
    bool safeSend(const String& content);
    bool safeSend_P(const char* content);
    bool safeSend_GZ(const uint8_t* gz_data, size_t gz_len);


    bool _clientAcceptsGzip = false;
    void detectGzipSupport();


    bool sendAuto_P(const char* raw, const uint8_t* gz, size_t gz_len);


    struct RenderGuard {
        DisplayManager* _dsp;
        RenderGuard(DisplayManager* d) : _dsp(d) { if (_dsp) _dsp->pauseRendering(true); }
        ~RenderGuard() { if (_dsp) _dsp->pauseRendering(false); }

        void release() { if (_dsp) _dsp->pauseRendering(false); _dsp = nullptr; }
    };


    struct ReadGuard {
        StorageManager* _sto;
        ReadGuard(StorageManager* s) : _sto(s) { if (_sto) _sto->enterFlashReadLock(); }
        ~ReadGuard() { if (_sto) _sto->exitFlashReadLock(); }
    };


    struct HeavyTaskGuard {
        StorageManager* _sto;
        bool _locked;
        HeavyTaskGuard(StorageManager* s) : _sto(s), _locked(false) {
            if (_sto) _locked = _sto->lockHeavyTask();
        }
        ~HeavyTaskGuard() { if (_sto && _locked) _sto->unlockHeavyTask(); }
        bool isLocked() const { return _locked; }
        void release() { if (_sto && _locked) _sto->unlockHeavyTask(); _locked = false; }
    };

    void clearStaleSessions();

    uint16_t getAuthPerms();
    bool isPasswordChangeRequired();


    bool serveProtectedPage(uint16_t requiredPerm, const uint8_t* gz_data, size_t gz_len);

    void handleLogin();
    void handleApiLoginInit();
    void handleApiLogin();
    void handleLogout();

    void handleForceChpass();
    void handleApiForceChpass();

    void handleRoot();
    void handleHistory();
    void handleFiles();
    void handleConfig();
    void handleNetwork();
    void handleUsers();

    void handleDownload();
    void handleDelete();
    void handleApiLs();
    void handleApiMkdir();
    void handleUploadComplete();
    void handleUploadData();

    void handleSaveSystem();    /**< U24: minimal — so theme-switch do dashboard */
    void handleApiCommitAll();  /**< U24: save-all + reboot */
    /* U24 Phase C: handleSaveNetwork substituido por handleApiCommitAll */
    void handleResetTouchCal();
    void handleApiStatus();
    void handleApiHistoryData();
    void handleApiLogs();
    void handleApiClearLogs();

    /* U24 Phase B: handleApiUserAdd/Del/Reset substituidos por handleApiCommitAll */

    void handleNotFound();
    void handleLangJs();

    void handleApiPerms();
    void handleApiNetwork();
    void handleApiConfig();
    void handleApiUsers();
    void handleApiThemes();


    void handleAlarms();
    void handleLicense();
    void handleApiAlarms();

    String getForceChpassHtml(bool isError);

    String getHistoryFileName(time_t date);
    String rgb565ToHex(uint16_t color);
    void feedWatchdog();
    bool isHandlerOvertime();
    bool isRateLimited(uint32_t minIntervalMs = 200);


    uint32_t _handlerDeadline = 0;
    void safeStreamFile(File& f, const String& contentType);
    void handleApiScreenshot();
    String getDynamicExpectedHash(String username);
    String jsonEscape(const char* src);
    void handleApiHistoryDays();
    void handleApiSecStatus();


    String generateSecureToken();


    bool secureCompare(const String& a, const String& b);

    TelemetryManager* _telemetryRef = nullptr;
    SoundManager*     _soundRef     = nullptr;
};
