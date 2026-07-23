/**
 * @file WebManager.h
 * @brief Embedded web server with multi-user sessions, RBAC, and brute-force protection.
 * @details Provides a full web interface on the Pico W using the Arduino WebServer
 * library. Features multi-session authentication (3 simultaneous users),
 * role-based access control via permission bitmasks, challenge-response
 * login with HMAC nonces, per-IP rate limiting, SendGuard (hardware
 * timer for WDT during long sends), RAII guards for rendering and
 * flash access, and gzip-compressed asset delivery.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <functional>
#include "SystemDefs.h"

/* SendGuard shared state — defined in WebManager_Core.cpp */
extern volatile bool _sendGuardExpired;
extern volatile bool _sendGuardActive;
extern volatile uint32_t _sendGuardStartMs;

/* Why a chunked response was cut short. safeSend( ) aborts the handler the
 * moment isClientGone( ) is true, which leaves the client holding a truncated
 * body — JSON that cannot parse. The three causes are indistinguishable in the
 * log (all surface as WEB_CLIENT_DISCONNECT), and they call for opposite fixes:
 * a real disconnect is the client's doing, a deadline hit means the handler is
 * too slow, and a guard hit means the abort latch tripped. Count them apart. */
extern volatile uint32_t _cgDeadlineHits;
extern volatile uint32_t _cgGuardHits;
extern volatile uint32_t _cgDisconnHits;

struct SendGuard {
	SendGuard( ) {
		_sendGuardStartMs = millis( );
		_sendGuardExpired = false;
		_sendGuardActive = true;
	}
	~SendGuard( ) { _sendGuardActive = false; }
};
#include "StorageManager.h"
#include "SensorManager.h"
#include "NetworkManager.h"
#include "DisplayManager.h"
#include "TelemetryManager.h"
#include "SoundManager.h"
#include "ota/restore.h"
#include "ota/firmware_stage.h"
#include <bearssl/bearssl_hash.h>

class WebManager {
public:
	typedef std::function<void( )> YieldCallback;
	typedef std::function<void( )> LightYieldCallback;

	WebManager( );
	void begin(StorageManager* storage, SensorManager* sensors,
	           NetworkManager* net, DisplayManager* display,
	           TelemetryManager* telemetry,
	           SoundManager* sound);
	void update( );
	void setYieldCallback(YieldCallback cb) { _yieldCb = cb; }
	void setLightYieldCallback(LightYieldCallback cb) { _lightYieldCb = cb; }
	uint32_t getCachedFlashUsed( ) const { return _cachedFsUsedBytes; }
	uint32_t getCachedFlashTotal( ) const { return _cachedFsTotalBytes; }


	 /* Touch priority is now checked via TouchPriority::isActive( ). */

private:
	/** Responds 503 with Retry-After if the user is interacting with the display.
	 * @return true if 503 was sent (caller should early-return). */
	bool rejectIfTouchPriority( );

	WebServer _server;
	YieldCallback _yieldCb = nullptr;
	LightYieldCallback _lightYieldCb = nullptr;
	/* Touch priority is now checked via TouchPriority::isActive( ). */

	StorageManager* _storageRef;
	SensorManager* _sensorRef;
	NetworkManager* _netRef;
	DisplayManager* _displayRef;


	struct RateEntry { uint32_t ip = 0; uint32_t lastReq = 0; uint8_t hits = 0; };
	RateEntry _rateLimits[RATE_LIMIT_SLOTS];
	File _uploadFile;
	uint8_t _uploadBatchBuf[8192]; /**< Upload batch buffer. */
	uint16_t _uploadBatchLen = 0; /**< Bytes accumulated in the batch. */
	/* Marks upload as rejected at START so that WRITE/END become no-ops
	 * and handleUploadComplete responds 400 instead of 200. */
	bool _uploadRejected = false;


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
		/* Fixed-size nonce char array instead of String — avoids heap
		 * allocation on each login_init (a latency-sensitive path) and
		 * zero fragmentation in the slot array. Size 65 = 64 hex chars
		 * from SHA-256 of generateSecureToken + terminator. */
		char nonce[65] = {0};
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
	volatile bool _inExportLogsHandler = false; /**< Separate guard for /api/export/logs.bin */


	inline bool isClientGone( ) {
		/* Wrap-safe: millis( ) wraps every ~49.7 days and would break this timeout. */
		if (_handlerDeadline > 0 && timeReached(_handlerDeadline)) {
			_cgDeadlineHits++;
			return true;
		}
		/* SendGuard hit the watchdog feed ceiling:
		 * abort handler cleanly instead of letting the WDT fire. */
		if (_sendGuardExpired) {
			_cgGuardHits++;
			return true;
		}
		if (!_server.client( ).connected( )) {
			_cgDisconnHits++;
			return true;
		}
		return false;
	}


	void initSendGuardTimer( );

	friend struct SendGuard;
	bool safeSend(const char* content);
	bool safeSend(const char* data, size_t len);
	bool safeSend(const String& content);
	bool safeSend_P(const char* content);
	bool safeSend_GZ(const uint8_t* gz_data, size_t gz_len);

	/* Broken-pipe observability. safeSend returning false is logged at most
	 * once every 5 seconds (throttle to avoid log spam when a handler
	 * sends many chunks after the client disconnects). */
	uint32_t _lastDisconnectLogMs = 0;
	void maybeLogClientDisconnect(const char* origin);

	bool _clientAcceptsGzip = false;
	void detectGzipSupport( );


	bool sendAuto_P(const char* raw, const uint8_t* gz, size_t gz_len);


	struct RenderGuard {
		DisplayManager* _dsp;
		RenderGuard(DisplayManager* d) : _dsp(d) { if (_dsp) _dsp->pauseRendering(true); }
		~RenderGuard( ) { if (_dsp) _dsp->pauseRendering(false); }

		void release( ) { if (_dsp) _dsp->pauseRendering(false); _dsp = nullptr; }
	};


	/* ReadGuard moved to StorageManager.h — now public. */

	struct HeavyTaskGuard {
		StorageManager* _sto;
		bool _locked;
		HeavyTaskGuard(StorageManager* s) : _sto(s), _locked(false) {
			if (_sto) _locked = _sto->lockHeavyTask( );
		}
		~HeavyTaskGuard( ) { if (_sto && _locked) _sto->unlockHeavyTask( ); }
		bool isLocked( ) const { return _locked; }
		void release( ) { if (_sto && _locked) _sto->unlockHeavyTask( ); _locked = false; }
	};

	void clearStaleSessions( );

	uint16_t getAuthPerms( );
	bool isPasswordChangeRequired( );


	bool serveProtectedPage(uint16_t requiredPerm, const uint8_t* gz_data, size_t gz_len);

	void handleLogin( );
	void handleApiLoginInit( );
	void handleApiLogin( );
	void handleLogout( );

	/* handleApiLogin decomposed into named helpers. */
	int findLoginStateForIp(uint32_t clientIP) const;
	bool respondIfLockedOut(int ls, int httpCode);
	bool validateNonceAndRespond(int ls);
	int verifyPasswordFor(const String& u, const String& p);
	int allocSessionSlot(int foundId);
	void completeLogin(int slot, int foundId, int ls, const String& u);
	uint32_t applyExponentialPenalty(int ls);

	void handleForceChpass( );
	void handleApiForceChpass( );
	void handleApiLoginChpass( );

	void handleRoot( );
	void handleHistory( );
	void handleFiles( );
	void handleConfig( );
	void handleNetwork( );
	void handleUsers( );

	void handleDownload( );
	void handleDelete( );
	void handleApiLs( );
	void handleApiMkdir( );
	void handleUploadComplete( );
	void handleUploadData( );
	void _flushUploadBatch( ); /**< Flush batch buffer to LittleFS. */

	void handleSaveSystem( ); /**< Minimal save — used by dashboard theme switch. */
	void handleApiCommitAll( ); /**< save-all + reboot */
	/* handleSaveNetwork replaced by handleApiCommitAll */
	void handleResetTouchCal( );
	void handleApiStatus( );
	void handleApiHistoryMulti( ); /**< History streaming for multiple sensors in one response. */
	void handleApiExportHistory( ); /**< Export history as .simx bundle (CRC32 trailer). */
	void handleApiExportLogs( ); /**< Export logs as .simx bundle kind='L' (CRC32 trailer). */
	void handleApiLogs( );
	void handleApiClearLogs( );
	void handleApiLang( ); /**< Serve @WEBDICT from .lng file as JSON. */

	/* handleApiUserAdd/Del/Reset replaced by handleApiCommitAll */

	void handleNotFound( );
	void handleLangJs( );
	void handleStyleCss( ); /**< Common cacheable CSS. */
	void handleFavicon( ); /**< Serve /favicon.ico from LittleFS with 7-day cache. */

	void handleApiPerms( );
	void handleApiNetwork( );
	void handleApiConfig( );
	void handleApiUsers( );
	void handleApiThemes( );


	void handleAlarms( );
	void handleLicense( );
	void handleApiAlarms( );

	String getForceChpassHtml(bool isError);

	String getHistoryFileName(time_t date);
	const char* getHistoryFileNameC(time_t date); /**< Reusable buffer version. */
	String rgb565ToHex(uint16_t color);
	void feedWatchdog( );
	bool isHandlerOvertime( );
	bool isRateLimited(uint32_t minIntervalMs = 200);


	uint32_t _handlerDeadline = 0;
	char _historyFnBuf[40]; /**< Reusable buffer for getHistoryFileNameC. */
	void safeStreamFile(File& f, const String& contentType);
	void handleApiScreenshot( );
	void handleApiScreenshotChunk( ); /**< /chunked with CRC32 */
	String getDynamicExpectedHash(String username);
	String jsonEscape(const char* src);
	void handleApiHistoryDays( );
	void handleApiSecStatus( );

	/* POST /api/set_time — applies manual RTC via
	 * NetworkManager::setManualTime. Immediate action (not via commit-all),
	 * so the user sees the updated time in the same response. */
	void handleApiSetTime( );

	/* Integrated into /dashboard. 2 endpoints.
	 * - GET /api/calib → state: NTP, current readings, offsets
	 * - POST /api/calib → applies refs/IDs/names; computes offsets; rewrites
	 * calib.csv with VERSION=epoch (NTP-gated). */
	void handleApiCalibGet( );
	void handleApiCalibPost( );

	/* OTA: full backup of LittleFS tied to chip_id (.bkp).
	 * Implementation in WebManager_Ota.cpp; format in src/ota/backup_format.h. */
	void handleApiBackup( );

	/* OTA: validation + restore of .bkp. Single handler for both
	 * endpoints (validate vs apply distinguished by URI path) — avoids
	 * std::function/std::bind duplication in .text. */
	void handleApiRestoreFinish( );
	void handleApiRestoreUploadData( );

	/* OTA: staging test handler stub (see WebManager_Core.cpp). */
	void handleApiOtaStagingTest( );

	/* OTA: triggers apply of pending update. Accepts ?test=1 which
	 * injects stub metadata and exercises the infrastructure path
	 * (tear down → IRQ off → SRAM applier → watchdog reboot) without
	 * destroying the app slot. */
	void handleApiOtaApply( );
	ota::RestoreSession _restoreSession;
	/* Core 1 paused during the entire restore upload session
	 * /api/restore?op=apply (1 lockout at START, 1 unlock at END/ABORTED).
	 * Previously RenderGuard was recreated per chunk, causing hundreds of
	 * multicore_lockout_start_timeout calls that occasionally deadlocked
	 * when Core 1 was in an IRQ-blocked state. */
	bool _restoreCorePaused = false;

	/* OTA: upload of firmware .bin.gz to staging via
	 * /api/restore?op=stage. Dedicated session (mutually exclusive with
	 * _restoreSession via the op= gate in the upload callback). */
	ota::StageSession _stageSession;
	/* Concurrency: we assume 1 admin web session at a time. HeavyTaskGuard
	 * on apply covers the pathological case of 2 sessions competing for LittleFS. */

	String generateSecureToken( );


	bool secureCompare(const String& a, const String& b);

	TelemetryManager* _telemetryRef = nullptr;
	SoundManager* _soundRef = nullptr;

	/* OTA: adapter that exposes safeSend for ota::backup_emit (Print&). */
	friend struct OtaBackupPrintAdapter;
};
