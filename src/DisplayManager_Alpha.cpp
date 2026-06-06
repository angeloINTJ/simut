#include "DisplayManager.h"
#include "display/HD44780_16x2.h"
static Hd44780_16x2 _lcd;
static uint32_t _lt=0; static bool _sh=false;
void DisplayManager::loopCore1( ){
 _lcd.begin(); _lcd.clear(); _lcd.setCursor(0,0); _lcd.print("AMBIENTE");
 for(;;){
  if(millis()-_lt>=3000){_lt=millis();_sh=!_sh;}
  for(int i=0;i<16;i++) _lcd.line[1][i]=' ';
  char b[17];
  if(_sh&&!isnan(_sharedState.ambientHum)) snprintf(b,16,"Umid: %d%%      ",(int)_sharedState.ambientHum);
  else if(!isnan(_sharedState.ambientTemp)){int ti=(int)_sharedState.ambientTemp;snprintf(b,16,"Temp: %d.%d C   ",ti,abs((int)(_sharedState.ambientTemp*10)%10));}
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
void DisplayManager::drawAmbientPanel(float,float,SensorType,bool){}
void DisplayManager::redrawAlarmFlash( ){}
bool DisplayManager::isSlotAlarming(int)const{return false;}
uint16_t DisplayManager::slotAlarmBg(int)const{return 0;}
bool DisplayManager::isAnyAlarmActive( )const{return false;}
void DisplayManager::showAlarmAction(int8_t){}
void DisplayManager::drawAlarmAction( ){}
void DisplayManager::setAlarmState(uint16_t,int8_t,bool,bool){}
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
