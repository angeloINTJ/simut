#include "DisplayManager.h"
#include "LogManager.h"
#include "display/HD44780_16x2.h"
#include "display/BigFont_HD44780.h"
extern DisplayManager* _instance;  /* defined in DisplayManager.cpp */
static Hd44780_16x2   _lcd;
static BigFont_HD44780 _big;
static SystemStatusData _netStatus;
static uint32_t _lt = 0;
static bool     _sh = false;

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
					_sh  = false;
				}
			}

		} else {
			/* ── SENSOR VALUES (big digits) ────────────────────── */
			if (millis( ) - _lt >= 3000) {
				_lt = millis( );
				_sh = !_sh;
			}

			_lcd.clear( );
			/* WiFi icon top-left */
			_lcd.setCursor(0, 0);
			_lcd.write('W');
			_lcd.write(BFS_WIFI);

			if (_sh && !isnan(_sharedState.slotHum)) {
				int hum = (int)_sharedState.slotHum;
				_big.showInteger(_lcd, hum, 13, '%');
				_lcd.setCursor(14, 1); _lcd.print("UR");
			} else if (!isnan(_sharedState.slotTemp)) {
				int raw = (int)(_sharedState.slotTemp * 10.0f);
				_big.showNumber(_lcd, raw);
				_lcd.setCursor(14, 0); _lcd.write('o');
				_lcd.setCursor(15, 1); _lcd.write('C');
			} else {
				_lcd.setCursor(0, 0);
				_lcd.print("SIMUT " SIMUT_VERSION);
				_lcd.setCursor(0, 1);
				_lcd.print("--.- C / --%%    ");
			}
		}

		_lcd.blit( );
		delay(500);
	}
}
void DisplayManager::handleTouch( ){}
void DisplayManager::render(const SystemState&){}
void DisplayManager::drawSlotPanel(float,float,SensorType,bool,int,const char*,bool,DashPanel&){}
void DisplayManager::drawBottomButtons(int,bool){}
void DisplayManager::drawInterfaceFixed( ){}
void DisplayManager::drawTopBar(const SystemState&){}
void DisplayManager::redrawAlarmFlash( ){}
bool DisplayManager::isSlotAlarming(int)const{return false;}
uint16_t DisplayManager::slotAlarmBg(int)const{return 0;}
bool DisplayManager::isAnyAlarmActive( )const{return false;}
void DisplayManager::showAlarmAction(int8_t){}
void DisplayManager::drawAlarmAction( ){}
void DisplayManager::setAlarmState(uint16_t,int8_t){}
void DisplayManager::setAlarmSilenced(bool,uint32_t){}
void DisplayManager::setAlarmDeactivated(bool){}
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
void DisplayManager::drawSettingsSounds( ){}
void DisplayManager::drawSettingsPassword( ){}
void DisplayManager::drawSettingsLang( ){}
void DisplayManager::drawSettingsLicense( ){}
void DisplayManager::drawSettingsDisplayOffset( ){}
void DisplayManager::showSettingsMain( ){}
void DisplayManager::showGraphPlot(const GraphDataPackage&,float,float){}
void DisplayManager::fixCardCorners(int16_t,int16_t,int16_t,int16_t,int16_t,uint16_t){}
void DisplayManager::maskStripCorners(GFXcanvas16*,int16_t,int16_t,int16_t,int16_t,int16_t,uint16_t,uint16_t){}
void DisplayManager::blitCanvas(GFXcanvas16*,int16_t,int16_t,int16_t,int16_t){}
void DisplayManager::loadDisplayOffset(const DisplayOffsetData*){}
/* ── Stubs for guarded/excluded TFT methods ─────────────────────── */
bool DisplayManager::getUiEvent(UiEvent&){return false;}
void DisplayManager::setWebBusy(bool,const char*){}
void DisplayManager::injectTouch(int16_t,int16_t){}
void DisplayManager::setSlotData(float t, float h, SensorType type, bool isValid, int slotIdx, String name) {
	_sharedState.slotTemp = t;
	_sharedState.slotHum  = h;
	_sharedState.slotValid = isValid;
	_sharedState.slotType  = type;
	_sharedState.selectedSlotIdx = slotIdx;
	safeCopy(_sharedState.slotName, name.c_str( ), sizeof(_sharedState.slotName));
}
void DisplayManager::showCalendar(int,int,uint32_t){}
void DisplayManager::setSlotMinMax(float,float,float,float){}
void DisplayManager::setTopSlotData(float,float,SensorType,bool,int,String){}
void DisplayManager::showAuthScreen(String){}
bool DisplayManager::isScreenTouched( ){return false;}
void DisplayManager::setSystemStatus(int rssi, bool bt, String timeStr) {
	mutex_enter_blocking(&_stateMutex);
	_sharedState.wifiRssi = rssi;
	_sharedState.btActive  = bt;
	safeCopy(_sharedState.timeString, timeStr.c_str( ), sizeof(_sharedState.timeString));
	mutex_exit(&_stateMutex);
}
const char* DisplayManager::getActiveWebDict( ){return nullptr;}
void DisplayManager::releaseQuietMode( ){}
bool DisplayManager::requestQuietMode(uint32_t){return true;}
void DisplayManager::setTopSlotMinMax(float,float,float,float){}
void DisplayManager::showSettingsLang(int){}
void DisplayManager::showSystemStatus( ){}
bool DisplayManager::consumeErrorSound( ){return false;}
bool DisplayManager::consumeTouchSound( ){return false;}
const char* DisplayManager::getActiveHelpText( ){return nullptr;}
const char* DisplayManager::getActiveLangCode( ){return nullptr;}
const char* DisplayManager::getActiveLangName( ){return nullptr;}
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
void DisplayManager::resetTouchCalibration( ){}
void DisplayManager::setTelemetrySendStatus(bool){}
void DisplayManager::showSettingsDisplayOffset( ){}
const char* DisplayManager::tr(LangKey){return "";}
void DisplayManager::readRow(int16_t,uint16_t*,int16_t){}
void DisplayManager::unaccent(const char*,char*,unsigned){}
void DisplayManager::showStats(const GraphDataPackage&,float,float){}
const char* DisplayManager::trlLookup(const char*){return "";}
void DisplayManager::fillCalData(TouchCalData*)const{}
void DisplayManager::getNewPassword(char*,unsigned)const{}
const char* DisplayManager::logcodeLookup(uint16_t){return "";}
bool DisplayManager::consumePreviewSound(SoundEvent&,uint8_t&){return false;}
bool DisplayManager::findAndLoadLangFile( ){return false;}
bool DisplayManager::consumeVolumePreview(uint8_t&){return false;}
bool DisplayManager::consumeAlarmVolumePreview(uint8_t&){return false;}
