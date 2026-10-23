/**
 * @file DisplayManager_Alarm.cpp
 * @brief Alarm state: the setters, the per-slot queries, the silence and the
 * entry to the alarm action screen.
 * @details The state both UIs read: which slots are in alarm or in sensor
 * error, the per-slot error mute and the silence. The original's draws of
 * the alarm (the action screen, the card background, the flash repaint)
 * are in DisplayManager_AlarmUi.cpp since S10c D5.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"

void DisplayManager::setAlarmState(uint16_t slotMask, int8_t navSlot) {
	_alarmSlotMask = slotMask;
	if (navSlot >= 0) _alarmNavPending = navSlot;
}

void DisplayManager::setAlarmErrState(uint16_t errMask) {
	_alarmErrMask = errMask;
}

bool DisplayManager::isSlotErrAlarming(int slotIdx) const {
	return (slotIdx >= 0 && slotIdx < MAX_SENSORS) && (_alarmErrMask & (1 << slotIdx));
}

/* Mute de ERRO por slot: desativar a falha de um sensor não toca o alarme
 * de LIMITE do mesmo slot — os dois domínios são independentes. */
void DisplayManager::setAlarmErrMuted(int8_t slotIdx, bool muted) {
	if (slotIdx < 0 || slotIdx >= MAX_SENSORS) return;
	if (muted) _alarmErrMuteMask |= (uint16_t)(1u << slotIdx);
	else       _alarmErrMuteMask &= (uint16_t)~(1u << slotIdx);
}

bool DisplayManager::isAlarmErrMuted(int8_t slotIdx) const {
	if (slotIdx < 0 || slotIdx >= MAX_SENSORS) return false;
	return (_alarmErrMuteMask & (1u << slotIdx)) != 0;
}


void DisplayManager::setAlarmSilenced(bool silenced, uint32_t endTime) {
	_alarmSilenced = silenced;
	_alarmSilenceEnd = endTime;
}


void DisplayManager::showAlarmAction(int8_t slotIdx) {
	mutex_enter_blocking(&_stateMutex);
	_alarmActionSlot = slotIdx;
	_uiMode = MODE_ALARM_ACTION;
	_forceSettingsRedraw = true;
	_repaintSettings = true;
	mutex_exit(&_stateMutex);
}


bool DisplayManager::isSlotAlarming(int slotIdx) const {
	return (slotIdx >= 0 && slotIdx < MAX_SENSORS) && (_alarmSlotMask & (1 << slotIdx));
}

bool DisplayManager::isAnyAlarmActive( ) const {
	return (_alarmSlotMask != 0) || (_alarmErrMask != 0);
}
