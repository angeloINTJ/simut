#include <Arduino.h>
#include "AppManager.h"
#include "SystemDefs.h"
#include "LogManager.h"
#include <hardware/watchdog.h>

AppManager app;

void setup( ) { app.setup( ); }

void loop( ) {
#ifndef SIMUT_WDT_DISABLED
 static bool _wdtStarted = false;
 if (!_wdtStarted) {
  watchdog_enable(WATCHDOG_TIMEOUT_MS, 1);
  LogManager::markWdtActive( );
  _wdtStarted = true;
 }
 watchdog_update( );
#endif
 app.loop( );
#ifndef SIMUT_WDT_DISABLED
 watchdog_update( );
#endif
}
