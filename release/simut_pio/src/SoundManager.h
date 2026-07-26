/**
 * @file SoundManager.h
 * @brief Sound event manager using BuzzerPIO_RP2040 library for PIO-driven audio.
 * @details Manages sound events (touch click, confirmation, error, alarm) with
 * 6 melody variants per type, independent system/alarm volume control,
 * event queue, mute/unmute, and persistent configuration stored in
 * SystemConfig::reserved[10..15]. Uses dual-SM PIO architecture
 * (PWM amplitude + frequency gate) via the BuzzerPIO_RP2040 library.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once


#include <Arduino.h>
#include <BuzzerPIO_RP2040.h>
#include "SystemDefs.h"


/* BUZZER_PIN → see src/simut_config.h */


#define BUZZER_PIO_BLOCK pio1


#define SND_MELODY_VARIANTS 6


enum SoundEvent {
 SND_NONE = 0,
 SND_TOUCH_CLICK,
 SND_CONFIRM,
 SND_ERROR,
 SND_ALARM_START,
 SND_ALARM_STOP,
 SND_ATTENTION /**< Sound for attention/confirmation screens */
};


struct MelodyDef {
 const BuzzerNote* notes;
 uint8_t len;
};


struct __attribute__((packed)) SoundConfigData {
 uint8_t magic;
 uint8_t flags;
 uint8_t volume;
 uint8_t melLow;
 uint8_t melHigh;
 uint8_t alarmVolume;
};
static_assert(sizeof(SoundConfigData) <= 6, "SoundConfigData exceeds the 6 reserved bytes!");


#define SND_FLAG_TOUCH 0x01
#define SND_FLAG_CONFIRM 0x02
#define SND_FLAG_ERROR 0x04
#define SND_FLAG_ALARM 0x08
#define SND_FLAG_MUTE 0x10
#define SND_FLAG_WEB 0x20
#define SND_FLAG_ATTENTION 0x40


struct SoundSettingsState {
 bool touchEnabled;
 bool confirmEnabled;
 bool errorEnabled;
 bool alarmEnabled;
 bool webEnabled;
 bool muted;
 bool attentionEnabled;
 uint8_t volume;
 uint8_t alarmVolume;
 uint8_t touchMelody;
 uint8_t confirmMelody;
 uint8_t errorMelody;
 uint8_t alarmMelody;
 uint8_t attentionMelody;
};


class SoundManager {
public:
 SoundManager( );


 void begin( );
 void update( );


 void play(SoundEvent event);


 void startAlarm( );
 void stopAlarm( );
 bool isAlarming( ) const { return _alarming; }


 void loadConfig(const SoundConfigData* data);
 void fillConfig(SoundConfigData* data) const;


 SoundSettingsState getSettingsState( ) const;
 void applySettingsState(const SoundSettingsState& state);


 void setEnabled(SoundEvent event, bool enabled);
 bool isEnabled(SoundEvent event) const;


 void setVolume(uint8_t vol);
 uint8_t getVolume( ) const { return _volume; }
 void setAlarmVolume(uint8_t vol);
 uint8_t getAlarmVolume( ) const { return _alarmVolume; }
 void setMuted(bool muted);
 bool isMuted( ) const { return _muted; }


 void setMelodyIndex(SoundEvent event, uint8_t idx);
 uint8_t getMelodyIndex(SoundEvent event) const;


 void playPreview(SoundEvent event, uint8_t melodyIdx);


 bool isWebSoundsEnabled( ) const { return _enableWeb; }

private:

 BuzzerPIO _buzzer;


 bool _alarming = false;


 bool _alarmSuspended = false;
 uint32_t _alarmResumeTime = 0;


 void playSystemSound(const MelodyDef& m);


 static uint16_t melodyDurationMs(const MelodyDef& m);


 static constexpr uint8_t QUEUE_SIZE = 4;
 SoundEvent _queue[QUEUE_SIZE];
 uint8_t _queueHead = 0;
 uint8_t _queueTail = 0;

 bool enqueue(SoundEvent ev);
 bool dequeue(SoundEvent& ev);


 const MelodyDef& resolveMelody(SoundEvent event) const;


 uint8_t _volume = 70;
 uint8_t _alarmVolume = 70;
 bool _muted = false;
 bool _enableTouch = true;
 bool _enableConfirm = true;
 bool _enableError = true;
 bool _enableAlarm = true;
 bool _enableWeb = true;
 bool _enableAttention = true;


 uint8_t _melTouch = 0;
 uint8_t _melConfirm = 0;
 uint8_t _melError = 0;
 uint8_t _melAlarm = 0;
 uint8_t _melAttention = 0;


 static const MelodyDef MELODIES[5][SND_MELODY_VARIANTS];


 static const BuzzerNote MEL_TOUCH_0[], MEL_TOUCH_1[], MEL_TOUCH_2[];
 static const BuzzerNote MEL_TOUCH_3[], MEL_TOUCH_4[], MEL_TOUCH_5[];
 static const BuzzerNote MEL_CONFIRM_0[], MEL_CONFIRM_1[], MEL_CONFIRM_2[];
 static const BuzzerNote MEL_CONFIRM_3[], MEL_CONFIRM_4[], MEL_CONFIRM_5[];
 static const BuzzerNote MEL_ERROR_0[], MEL_ERROR_1[], MEL_ERROR_2[];
 static const BuzzerNote MEL_ERROR_3[], MEL_ERROR_4[], MEL_ERROR_5[];
 static const BuzzerNote MEL_ALARM_0[], MEL_ALARM_1[], MEL_ALARM_2[];
 static const BuzzerNote MEL_ALARM_3[], MEL_ALARM_4[], MEL_ALARM_5[];
 static const BuzzerNote MEL_ATTENTION_0[], MEL_ATTENTION_1[], MEL_ATTENTION_2[];
 static const BuzzerNote MEL_ATTENTION_3[], MEL_ATTENTION_4[], MEL_ATTENTION_5[];
};
