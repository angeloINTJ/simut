/**
 * @file DisplayManager_CalendarCore.cpp
 * @brief The calendar's entry points: the core share of DisplayManager_Calendar.cpp, moved word for word in S10a (docs/analysis/UI_DRIVERS.md).
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "UiWidgets.h"

void DisplayManager::showCalendar(int year, int month, uint32_t daysMask) {
 mutex_enter_blocking(&_stateMutex);
 _calYear = year;
 _calMonth = month;
 _calDaysMask = daysMask;
 _uiMode = MODE_CALENDAR;
 __dmb( );
 _repaintCalendar = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::setCalendarDays(uint32_t daysMask) {
 mutex_enter_blocking(&_stateMutex);
 _calDaysMask = daysMask;
 _repaintCalendar = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::setGraphNavOffset(int offset) {
 mutex_enter_blocking(&_stateMutex);
 _graphNavOffset = offset;
 mutex_exit(&_stateMutex);
}
