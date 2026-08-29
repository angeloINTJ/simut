#include "DisplayManager.h"
#include "LogManager.h"
#include "display/HD44780_16x2.h"
#include "display/BigFont_HD44780.h"
#include "sensors/SensorHelpers.h"
#include <LittleFS.h>
#include <string.h>
extern DisplayManager* _instance;  /* defined in DisplayManager.cpp */
static Hd44780_16x2   _lcd;
static BigFont_HD44780 _big;
static SystemStatusData _netStatus;
static uint32_t _lt = 0;

/* Alpha: lightweight .lng locator (web translations only). No TFT UI means
 * @DICT/@HELP/@LICENSE/@LOGCODES/@TRL are never needed in RAM — only the file
 * path (so GET /api/lang can stream @WEBDICT to the browser) and @NAME/@CODE. */
static char _alphaLangPath[40] = {0};
static char _alphaLangName[16] = {0};
static char _alphaLangCode[8] = {0};
/* @HELP body, read from LittleFS on demand (the pack is not resident). */
static char _alphaHelpBuf[2048];

/* ── Alpha multi-slot cycling state (Core 1) ─────────────────────── */
static int8_t  _cycleSlot = -1;       /* slot currently on screen */
static uint8_t _cycleCh   = CH_TEMP;  /* channel currently on screen */

/* First channel a sensor type reports (every current type: CH_TEMP). */
static uint8_t alphaFirstChannel(SensorType t) {
	for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++)
		if (sensorHasChannel(t, c)) return c;
	return CH_TEMP;
}

/* Advance to the next (slot, channel) across active slots, in slot order. */
static void alphaCycleNext(const SlotSnapshot* snap, const int8_t* active, uint8_t n,
                           int8_t &slot, uint8_t &ch) {
	if (n == 0) { slot = -1; ch = CH_TEMP; return; }
	if (slot < 0) { slot = active[0]; ch = alphaFirstChannel(snap[slot].type); return; }
	SensorType t = snap[slot].type;
	for (uint8_t c = ch + 1; c < MAX_SENSOR_CHANNELS; c++) {
		if (sensorHasChannel(t, c)) { ch = c; return; }
	}
	int pos = 0;
	for (uint8_t i = 0; i < n; i++) if (active[i] == slot) { pos = i; break; }
	slot = active[(pos + 1) % n];
	ch = alphaFirstChannel(snap[slot].type);
}

/* Value of a snapshot for a given channel id. */
static float alphaChannelValue(const SlotSnapshot& s, uint8_t ch) {
	switch (ch) {
		case CH_TEMP:  return s.temp;
		case CH_HUM:   return s.hum;
		case CH_PRESS: return s.pres;
		default:       return NAN;
	}
}

void DisplayManager::core1Entry( ){ if (_instance) _instance->loopCore1(); }
void DisplayManager::loopCore1( ) {
	_lcd.begin( );
	_big.begin(_lcd);  /* load CGRAM custom characters */
	_lcd.clear( );

	/* Splash screen */
	_lcd.setCursor(0, 0);
	_lcd.print("SIMUT " SIMUT_VERSION);
	_lcd.setCursor(0, 1);
	_lcd.print("  Inicializando ");
	_lcd.blit( );
	delay(1200);

	multicore_lockout_victim_init( );
	_sharedState.slotTemp = NAN;
	_sharedState.slotHum  = NAN;
	_core1Ready = true;

	/* ── Boot progress state ─────────────────────────────────────── */
	uint8_t  bootBar    = 0;   /* 0–14 bar fill */
	uint32_t barTimer   = 0;   /* throttle bar advance */
	int16_t  lastLogKey[5] = { -1, -1, -1, -1, -1 };
	bool     bootDone   = false;
	uint8_t  fullBarCnt = 0;
	uint8_t  showNetCnt = 0;
	bool     showNetwork = false;

	while (true) {
		TRACE_MOD(1, MOD_DISPLAY);
		TRACE_BEAT(1);
		_lastHeartbeat = millis( );

		/* Update WiFi signal icon (slot 7) based on RSSI */
		_big.showWiFi(_lcd, _sharedState.wifiRssi);


		if (_sharedState.isBooting) {
			/* ── BOOT SCREEN ──────────────────────────────────── */
			_lcd.setCursor(0, 0);
			_lcd.print("SIMUT " SIMUT_VERSION);

			/* Detect new boot stages → advance bar */
			bool newStage = false;
			for (int i = 0; i < 5; i++) {
				if (_sharedState.bootLogs[i].key != lastLogKey[i]) {
					lastLogKey[i] = _sharedState.bootLogs[i].key;
					newStage = true;
				}
			}
			uint32_t now = millis( );
			if (newStage || (now - barTimer >= 800)) {
				if (bootBar < 14) bootBar++;
				barTimer = now;
			}

			/* Render bar: [############      ] */
			_lcd.setCursor(0, 1);
			_lcd.write('[');
			for (uint8_t i = 0; i < 14; i++)
				_lcd.write(i < bootBar ? '#' : ' ');
			_lcd.write(']');

		} else if (!bootDone) {
			/* ── BOOT → NETWORK → SENSOR TRANSITION ──────────────── */
			if (!showNetwork) {
				if (bootBar < 14 && fullBarCnt < 6) {
					/* Fill remaining bar segments */
					if (fullBarCnt == 0) { bootBar = 14; }
					_lcd.setCursor(0, 0);
					_lcd.print("SIMUT " SIMUT_VERSION);
					_lcd.setCursor(0, 1);
					_lcd.write('[');
					for (uint8_t i = 0; i < 14; i++) _lcd.write('#');
					_lcd.write(']');
					fullBarCnt++;
				} else {
					showNetwork = true;
					showNetCnt = 0;
				}
			} else {
				/* ── Show WiFi / IP ────────────────────────────── */
				bool hasIp  = (_netStatus.ip[0] != '\0');
				bool online = (_sharedState.wifiRssi > -100);

				/* Reset counter when IP first appears so user
				   sees it for at least 3 s.                      */
				static bool hadIp = false;
				if (hasIp && !hadIp) { showNetCnt = 0; hadIp = true; }

				_lcd.setCursor(0, 0);
				if (online && hasIp) {
					_lcd.print(" Conectado!     ");
					_lcd.setCursor(0, 1);
					_lcd.print("                ");
					_lcd.setCursor(0, 1);
					_lcd.print(_netStatus.ip);
				} else if (online && !hasIp) {
					_lcd.print(" Conectado!     ");
					_lcd.setCursor(0, 1);
					_lcd.print("  Obtendo IP... ");
				} else {
					_lcd.print(" Sem WiFi       ");
					_lcd.setCursor(0, 1);
					_lcd.print("    Offline     ");
				}

				showNetCnt++;
				/* 3 s once IP visible, 10 s max if waiting,
				   3 s for offline */
				uint8_t maxCnt = (online && !hasIp) ? 20u : 6u;
				if (showNetCnt >= maxCnt) {
					bootDone = true;
					_lcd.clear( );
					_lt  = millis( );
					_cycleSlot = -1;
				}
			}

		} else {
			/* ── SENSOR VALUES (cycle active slots × channels) ───── */
			SlotSnapshot snap[MAX_SENSORS];
			int8_t active[MAX_SENSORS];
			uint8_t n = 0;
			{
				mutex_enter_blocking(&_stateMutex);
				for (int i = 0; i < MAX_SENSORS; i++) {
					snap[i] = _slotSnapshots[i];
					if (_slotSnapshots[i].type != TYPE_NONE) active[n++] = (int8_t)i;
				}
				mutex_exit(&_stateMutex);
			}

			/* Re-anchor if no slots, or if the current slot vanished. */
			if (n == 0) {
				_cycleSlot = -1; _cycleCh = CH_TEMP;
			} else if (_cycleSlot < 0 || _cycleSlot >= MAX_SENSORS ||
			           snap[_cycleSlot].type == TYPE_NONE) {
				_cycleSlot = active[0];
				_cycleCh = alphaFirstChannel(snap[_cycleSlot].type);
			}

			if (millis( ) - _lt >= 3000) {
				_lt = millis( );
				alphaCycleNext(snap, active, n, _cycleSlot, _cycleCh);
			}

			_lcd.clear( );
			/* Status top-left: "AP" in Access Point mode, else WiFi icon. */
			_lcd.setCursor(0, 0);
			if (_sharedState.apMode) {
				_lcd.print("AP");
			} else {
				_lcd.write('W');
				_lcd.write(BFS_WIFI);
			}

			if (n == 0) {
				/* No active sensors — show big ERRO. */
				_big.showError(_lcd);
			} else {
				/* Identification on line 1, col 0 when >1 slot is active. */
				if (n > 1) {
					char id[8];
					snprintf(id, sizeof(id), "S%d", (int)_cycleSlot);
					_lcd.setCursor(0, 1);
					_lcd.print(id);
				}

				const SlotSnapshot &cs = snap[_cycleSlot];
				float v = alphaChannelValue(cs, _cycleCh);

				if (!cs.valid || isnan(v)) {
					/* Sensor in error — big ERRO (avgValue may be stale). */
					_big.showError(_lcd);
				} else switch (_cycleCh) {
				case CH_TEMP: {
					int raw = (int)(v * 10.0f);
					_big.showNumber(_lcd, raw);
					_lcd.setCursor(14, 0); _lcd.write('o');
					_lcd.setCursor(15, 1); _lcd.write('C');
					break;
				}
				case CH_HUM: {
					int hum = (int)v;
					_big.showInteger(_lcd, hum, 13, '%');
					_lcd.setCursor(14, 1); _lcd.print("UR");
					break;
				}
				case CH_PRESS: {
					/* Pressão em caracteres normais, montada com aritmética
					 * inteira — o %f do printf corrompia o LCD no Core 1.
					 * Dígitos grandes ficam para uma etapa futura. */
					char b[16];
					int tenths = (int)(v * 10.0f + 0.5f);
					int ip = tenths / 10;
					int dp = tenths % 10;
					snprintf(b, sizeof(b), "%d.%d hPa", ip, dp);
					_lcd.setCursor(3, 0);
					_lcd.print(b);
					break;
				}
				default:
					_big.showError(_lcd);
					break;
				}
			}
		}

		_lcd.blit( );
		delay(500);
	}
}
void DisplayManager::handleTouch( ){}
void DisplayManager::render(const SystemState&){}
void DisplayManager::drawSlotPanel(float,float,SensorType,bool,int,const char*,bool,DashPanel&,float){}
void DisplayManager::drawBottomButtons(int){}
void DisplayManager::drawInterfaceFixed( ){}
void DisplayManager::drawTopBar(const SystemState&){}
void DisplayManager::redrawAlarmFlash( ){}
bool DisplayManager::isSlotAlarming(int)const{return false;}
uint16_t DisplayManager::slotAlarmBg(int)const{return 0;}
bool DisplayManager::isAnyAlarmActive( )const{return false;}
void DisplayManager::showAlarmAction(int8_t){}
void DisplayManager::drawAlarmAction( ){}
void DisplayManager::setAlarmState(uint16_t,int8_t){}
void DisplayManager::setAlarmErrState(uint16_t errMask) {
	_alarmErrMask = errMask;
}
bool DisplayManager::isSlotErrAlarming(int slotIdx) const {
	return (slotIdx >= 0 && slotIdx < 16) && (_alarmErrMask & (1 << slotIdx));
}
void DisplayManager::setAlarmSilenced(bool,uint32_t){}
void DisplayManager::setAlarmErrMuted(int8_t,bool){}
bool DisplayManager::isAlarmErrMuted(int8_t)const{return false;}
GFXcanvas16* DisplayManager::beginScreenRender( ){return nullptr;}
void DisplayManager::commitScreenStrip(int16_t){}
void DisplayManager::endScreenRender( ){}
void DisplayManager::drawSystemStatus( ){}
int DisplayManager::buildDashLayout(DashBtn*,int*,bool*){return 0;}
void DisplayManager::drawLoadingScreen( ){}
void DisplayManager::drawGraphScreen( ){}
void DisplayManager::drawGraphDetailScreen( ){}
void DisplayManager::drawStatsScreen( ){}
void DisplayManager::drawPeriodButtons( ){}
void DisplayManager::drawCalendarScreen( ){}
void DisplayManager::drawGraphHeaderBar(bool){}
void DisplayManager::drawGraphIcon(int16_t,int16_t,uint16_t){}
void DisplayManager::drawSettingsMain( ){}
void DisplayManager::drawSettingsThemes( ){}
void DisplayManager::drawSettingsAlarms( ){}
void DisplayManager::renderAlarmRow(int,int16_t&){}
void DisplayManager::drawAlarmStatusOnly( ){}
void DisplayManager::refreshAlarmStatus( ){}
void DisplayManager::drawSettingsSounds( ){}
void DisplayManager::drawSettingsPassword( ){}
void DisplayManager::drawSettingsLang( ){}
void DisplayManager::drawSettingsLicense( ){}
void DisplayManager::drawSettingsDisplayOffset( ){}
void DisplayManager::showSettingsMain( ){}
void DisplayManager::showGraphPlot(const GraphDataPackage&,float,float){}
void DisplayManager::fixCardCorners(int16_t,int16_t,int16_t,int16_t,int16_t,uint16_t){}
void DisplayManager::maskStripCorners(GFXcanvas16*,int16_t,int16_t,int16_t,int16_t,int16_t,uint16_t,uint16_t){}
void DisplayManager::blitCanvas(GFXcanvas16*,int16_t,int16_t,int16_t,int16_t,int16_t){}
void DisplayManager::loadDisplayOffset(const DisplayOffsetData*){}
/* ── Stubs for guarded/excluded TFT methods ─────────────────────── */
bool DisplayManager::getUiEvent(UiEvent&){return false;}
void DisplayManager::setWebBusy(bool,const char*){}
void DisplayManager::injectTouch(int16_t,int16_t){}
void DisplayManager::setSlotData(float t, float h, float p, SensorType type, bool isValid, int slotIdx, String name) {
	_sharedState.slotTemp = t;
	_sharedState.slotHum  = h;
  _sharedState.slotPres = p;
	_sharedState.slotValid = isValid;
	_sharedState.slotType  = type;
	_sharedState.selectedSlotIdx = slotIdx;
	safeCopy(_sharedState.slotName, name.c_str( ), sizeof(_sharedState.slotName));
}
void DisplayManager::showCalendar(int,int,uint32_t){}
void DisplayManager::setSlotMinMax(float,float,float,float){}
void DisplayManager::setTopSlotData(float,float,float,SensorType,bool,int,String){}
void DisplayManager::showAuthScreen(String){}
bool DisplayManager::isScreenTouched( ){return false;}
void DisplayManager::setSystemStatus(int rssi, bool bt, String timeStr) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.wifiRssi = rssi;
	_sharedState.btActive  = bt;
	safeCopy(_sharedState.timeString, timeStr.c_str( ), sizeof(_sharedState.timeString));
	mutex_exit(&_stateMutex);
}
bool DisplayManager::getActiveWebDictSource(const char** path, uint32_t* offset, uint32_t* len) {
	if (_alphaLangPath[0] == '\0') return false;
	if (path) *path = _alphaLangPath;
	/* The web handler re-scans the file for the exact @WEBDICT range, so
	 * these are advisory: 0 tells it to locate the blob itself. */
	if (offset) *offset = 0;
	if (len) *len = 0;
	return true;
}
void DisplayManager::releaseQuietMode( ){}
bool DisplayManager::requestQuietMode(uint32_t){return true;}
void DisplayManager::setTopSlotMinMax(float,float,float,float){}
void DisplayManager::showSettingsLang(int){}
void DisplayManager::showSystemStatus( ){}
bool DisplayManager::consumeErrorSound( ){return false;}
bool DisplayManager::consumeTouchSound( ){return false;}
const char* DisplayManager::getActiveHelpText( ) {
	if (_alphaLangPath[0] == '\0') return nullptr;
	File f = LittleFS.open(_alphaLangPath, "r");
	if (!f) return nullptr;

	/* Locate @HELP at column 0, then its body (up to the next column-0 '@',
	 * which is @LICENSE). Byte-wise walk mirrors scanWebDictRange. */
	static const char kDir[] = "@HELP";
	const size_t kDirLen = sizeof(kDir) - 1;
	uint8_t scan[128];
	uint32_t pos = 0;
	size_t match = 0;
	bool atLineStart = true;
	bool skippingDirLine = false;
	bool inBlock = false;
	bool found = false;
	uint32_t bodyStart = 0, bodyEnd = 0;

	f.seek(0);
	while (!found) {
		int n = f.read(scan, sizeof(scan));
		if (n <= 0) break;
		for (int i = 0; i < n; i++, pos++) {
			const char c = (char)scan[i];
			if (skippingDirLine) {
				if (c == '\n') { skippingDirLine = false; inBlock = true; bodyStart = pos + 1; }
				continue;
			}
			if (inBlock) {
				if (atLineStart && c == '@') { bodyEnd = pos; found = true; break; }
				atLineStart = (c == '\n');
				continue;
			}
			if (atLineStart && c == '@') match = 1;
			else if (match > 0 && match < kDirLen && c == kDir[match]) match++;
			else match = 0;
			if (match == kDirLen) { skippingDirLine = true; match = 0; continue; }
			atLineStart = (c == '\n');
		}
	}
	if (inBlock && !found) bodyEnd = pos; /* @HELP runs to EOF (no @LICENSE) */

	if (!inBlock || bodyEnd <= bodyStart) { f.close( ); return nullptr; }
	size_t want = bodyEnd - bodyStart;
	if (want > sizeof(_alphaHelpBuf) - 1) want = sizeof(_alphaHelpBuf) - 1;
	f.seek(bodyStart);
	size_t got = f.readBytes(_alphaHelpBuf, want);
	f.close( );
	if (got == 0) return nullptr;
	_alphaHelpBuf[got] = '\0';
	return _alphaHelpBuf;
}
const char* DisplayManager::getActiveLangCode( ){return _alphaLangCode;}
const char* DisplayManager::getActiveLangName( ){return _alphaLangName;}
void DisplayManager::setGraphNavOffset(int){}
void DisplayManager::setWebNotification(const char*){}
void DisplayManager::showSettingsAlarms(SystemConfig*){}
void DisplayManager::showSettingsSounds(const SoundSettingsState&){}
void DisplayManager::showSettingsThemes(int){}
void DisplayManager::updateSystemStatus(const SystemStatusData& d) {
	_netStatus = d;
}
void DisplayManager::setTelemetryPending(uint16_t){}
void DisplayManager::showSettingsLicense( ){}
const char* DisplayManager::getActiveLicenseText( ){return "";}
void DisplayManager::loadTouchCalibration(const TouchCalData*){}
void DisplayManager::requestLoadingScreen( ){}
void DisplayManager::showSettingsPassword( ){}
void DisplayManager::showTouchCalibration( ){}
/* Arrived with the touch-sensitivity screen and never got its stub here, so
 * pico_w_alpha stopped linking the moment `screen touchsens` was wired into
 * AppManager_Commands — a variant with no touchscreen at all failing on the
 * one screen it can never show. The zip built from this tree is a release
 * asset, so the break shipped as source nobody could compile. */
void DisplayManager::showTouchSensitivity( ){}
void DisplayManager::resetTouchCalibration( ){}
void DisplayManager::setTelemetrySendStatus(bool){}
void DisplayManager::showSettingsDisplayOffset( ){}
const char* DisplayManager::tr(LangKey){return "";}
const char* DisplayManager::channelLabel(uint8_t){return "";}
void DisplayManager::readRow(int16_t,uint16_t*,int16_t){}
void DisplayManager::unaccent(const char* utf8, char* out, size_t outSize) {
	if (!out || outSize == 0) return;
	if (!utf8) { out[0] = '\0'; return; }

	size_t o = 0;
	const unsigned char* p = (const unsigned char*)utf8;
	while (*p && o + 1 < outSize) {
		unsigned char c = *p;
		if (c < 0x80) { out[o++] = (char)c; p++; continue; }
		unsigned char c2 = p[1];
		char repl = '?';
		if (c == 0xC3) {
			switch (c2) {
			case 0x80: case 0x81: case 0x82: case 0x83:
			case 0x84: case 0x85: repl = 'A'; break;
			case 0x86: repl = 'A'; break;
			case 0x87: repl = 'C'; break;
			case 0x88: case 0x89: case 0x8A:
			case 0x8B: repl = 'E'; break;
			case 0x8C: case 0x8D: case 0x8E:
			case 0x8F: repl = 'I'; break;
			case 0x91: repl = 'N'; break;
			case 0x92: case 0x93: case 0x94:
			case 0x95: case 0x96: case 0x98: repl = 'O'; break;
			case 0x99: case 0x9A: case 0x9B:
			case 0x9C: repl = 'U'; break;
			case 0x9D: repl = 'Y'; break;
			case 0xA0: case 0xA1: case 0xA2: case 0xA3:
			case 0xA4: case 0xA5: repl = 'a'; break;
			case 0xA6: repl = 'a'; break;
			case 0xA7: repl = 'c'; break;
			case 0xA8: case 0xA9: case 0xAA:
			case 0xAB: repl = 'e'; break;
			case 0xAC: case 0xAD: case 0xAE:
			case 0xAF: repl = 'i'; break;
			case 0xB1: repl = 'n'; break;
			case 0xB2: case 0xB3: case 0xB4:
			case 0xB5: case 0xB6: case 0xB8: repl = 'o'; break;
			case 0xB9: case 0xBA: case 0xBB:
			case 0xBC: repl = 'u'; break;
			case 0xBD: case 0xBF: repl = 'y'; break;
			default: repl = '?'; break;
			}
			out[o++] = repl; p += 2;
		} else if (c == 0xC2) {
			if (c2 == 0xA1 || c2 == 0xBF) { p += 2; continue; }
			switch (c2) {
			case 0xA9: repl = 'C'; break;
			case 0xAE: repl = 'R'; break;
			case 0xB0: repl = 'o'; break;
			case 0xB1: repl = '+'; break;
			case 0xB2: repl = '2'; break;
			case 0xB3: repl = '3'; break;
			default: repl = '?'; break;
			}
			out[o++] = repl; p += 2;
		} else {
			out[o++] = '?'; p++;
		}
	}
	out[o] = '\0';
}
void DisplayManager::showStats(const GraphDataPackage&,float,float){}
const char* DisplayManager::trlLookup(const char*){return "";}
void DisplayManager::fillCalData(TouchCalData*)const{}
void DisplayManager::getNewPassword(char*,unsigned)const{}
const char* DisplayManager::logcodeLookup(uint16_t){return "";}
bool DisplayManager::consumePreviewSound(SoundEvent&,uint8_t&){return false;}
bool DisplayManager::findAndLoadLangFile( ) {
	char firstName[40] = {0};
	int count = 0;
	Dir dir = LittleFS.openDir("/lang");
	while (dir.next( )) {
		String fn = dir.fileName( );
		if (!fn.startsWith("language_") || !fn.endsWith(".lng")) continue;
		count++;
		if (count == 1 || strcmp(fn.c_str( ), firstName) < 0) {
			strncpy(firstName, fn.c_str( ), sizeof(firstName) - 1);
		}
	}
	if (count == 0) return false;

	char path[48];
	snprintf(path, sizeof(path), "/lang/%s", firstName);
	strncpy(_alphaLangPath, path, sizeof(_alphaLangPath) - 1);
	_alphaLangPath[sizeof(_alphaLangPath) - 1] = '\0';

	/* Read only the leading bytes (header + @NAME + @CODE). The rest of the
	 * pack (@DICT/@HELP/@LICENSE/@LOGCODES/@TRL/@WEBDICT) is never loaded. */
	_alphaLangName[0] = '\0';
	_alphaLangCode[0] = '\0';
	File f = LittleFS.open(path, "r");
	if (f) {
		char head[256];
		int n = f.readBytes(head, sizeof(head) - 1);
		f.close( );
		if (n > 0) {
			head[n] = '\0';
			/* Bounded copy, no in-place mutation of head: nulling the @NAME
			 * newline would otherwise hide @CODE from strstr and leave the web
			 * selector EN-only. */
			const char* p = strstr(head, "@NAME ");
			if (p) {
				p += 6;
				const char* e = strpbrk(p, "\r\n");
				size_t len = e ? (size_t)(e - p) : strlen(p);
				if (len >= sizeof(_alphaLangName)) len = sizeof(_alphaLangName) - 1;
				memcpy(_alphaLangName, p, len);
				_alphaLangName[len] = '\0';
			}
			p = strstr(head, "@CODE ");
			if (p) {
				p += 6;
				const char* e = strpbrk(p, "\r\n");
				size_t len = e ? (size_t)(e - p) : strlen(p);
				if (len >= sizeof(_alphaLangCode)) len = sizeof(_alphaLangCode) - 1;
				memcpy(_alphaLangCode, p, len);
				_alphaLangCode[len] = '\0';
			}
		}
	}
	return true;
}
bool DisplayManager::consumeVolumePreview(uint8_t&){return false;}
bool DisplayManager::consumeAlarmVolumePreview(uint8_t&){return false;}
