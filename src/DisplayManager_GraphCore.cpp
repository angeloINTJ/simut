/**
 * @file DisplayManager_GraphCore.cpp
 * @brief The graph screen's entry points: the core share of DisplayManager_Graph.cpp, moved word for word in S10a (docs/analysis/UI_DRIVERS.md).
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "DisplayManager_FmtFloat.h"
#include "UiWidgets.h"
#include "sensors/SensorChannels.h" /* CH_TEMP/CH_HUM/CH_PRESS for card units */

void DisplayManager::showGraphPlot(const GraphDataPackage& data, float minHum, float maxHum) {
 mutex_enter_blocking(&_stateMutex);
 _graphData = data; _currentMinHum = minHum; _currentMaxHum = maxHum;
 _uiMode = MODE_GRAPH_VIEW;
 _headerShowName = false;
 _headerNameTimer = 0;
 __dmb( );
 _repaintGraph = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::requestLoadingScreen( ) {
 _loadingDrawn = false;
 _repaintLoading = true;
 _uiMode = MODE_GRAPH_LOADING;
}
