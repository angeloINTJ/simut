#include "DisplayManager.h"
#include "display/HD44780_16x2.h"
extern DisplayManager* _instance;  /* defined in DisplayManager.cpp */
static Hd44780_16x2 _lcd;
static uint32_t _lt=0; static bool _sh=false;
void DisplayManager::core1Entry( ){ if (_instance) _instance->loopCore1(); }
void DisplayManager::loopCore1( ){
 _lcd.begin(); _lcd.clear(); _lcd.setCursor(0,0); _lcd.print("SIMUT v" SIMUT_VERSION);
 multicore_lockout_victim_init( );
 _core1Ready = true;
 for(;;){
  if(millis()-_lt>=3000){_lt=millis();_sh=!_sh;}
  for(int i=0;i<16;i++) _lcd.line[1][i]=' ';
  char b[17];
  if(_sh&&!isnan(_sharedState.slotHum)) snprintf(b,16,"Umid: %d%%      ",(int)_sharedState.slotHum);
  else if(!isnan(_sharedState.slotTemp)){int ti=(int)_sharedState.slotTemp;snprintf(b,16,"Temp: %d.%d C   ",ti,abs((int)(_sharedState.slotTemp*10)%10));}
  else snprintf(b,16,"--.- C / --%%    ");
  _lcd.setCursor(0,1); _lcd.print(b); _lcd.blit();
  delay(500);
 }}
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
void DisplayManager::setSlotData(float,float,SensorType,bool,int,String){}
void DisplayManager::showCalendar(int,int,uint32_t){}
void DisplayManager::setSlotMinMax(float,float,float,float){}
void DisplayManager::setTopSlotData(float,float,SensorType,bool,int,String){}
void DisplayManager::showAuthScreen(String){}
bool DisplayManager::isScreenTouched( ){return false;}
void DisplayManager::setSystemStatus(int,bool,String){}
const char* DisplayManager::getActiveWebDict( ){return "";}
void DisplayManager::releaseQuietMode( ){}
bool DisplayManager::requestQuietMode(uint32_t){return true;}
void DisplayManager::setTopSlotMinMax(float,float,float,float){}
void DisplayManager::showSettingsLang(int){}
void DisplayManager::showSystemStatus( ){}
bool DisplayManager::consumeErrorSound( ){return false;}
bool DisplayManager::consumeTouchSound( ){return false;}
const char* DisplayManager::getActiveHelpText( ){return "";}
const char* DisplayManager::getActiveLangCode( ){return "";}
const char* DisplayManager::getActiveLangName( ){return "";}
void DisplayManager::setGraphNavOffset(int){}
void DisplayManager::setWebNotification(const char*){}
void DisplayManager::showSettingsAlarms(SystemConfig*){}
void DisplayManager::showSettingsSounds(const SoundSettingsState&){}
void DisplayManager::showSettingsThemes(int){}
void DisplayManager::updateSystemStatus(const SystemStatusData&){}
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
