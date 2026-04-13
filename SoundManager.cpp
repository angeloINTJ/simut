/**
 * @file    SoundManager.cpp
 * @brief   Implementation of SoundManager — melody playback, alarm control, and persistence.
 * @details Contains 24 melody definitions (4 types x 6 variants), event queue
 * consumer with alarm suspend/resume for system sounds, preview
 * playback, bitfield-packed config serialization, and UI state
 * snapshot exchange. Alarm sounds use looping playback while system
 * sounds use one-shot with automatic alarm resumption.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.4.7
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "SoundManager.h"


/* =========================================================================== */
/*            MELODY DEFINITIONS — 24 VARIANTS (4 TYPES x 6 EACH)            */
/* =========================================================================== */

/* Touch click melodies */
const BuzzerNote SoundManager::MEL_TOUCH_0[] = { { 4000, 15 } };
const BuzzerNote SoundManager::MEL_TOUCH_1[] = { { 2800, 12 }, { 3500, 12 } };
const BuzzerNote SoundManager::MEL_TOUCH_2[] = { { 5500, 8 } };
const BuzzerNote SoundManager::MEL_TOUCH_3[] = { { 3200, 10 } };
const BuzzerNote SoundManager::MEL_TOUCH_4[] = { { 3800, 10 }, { 2800, 15 } };
const BuzzerNote SoundManager::MEL_TOUCH_5[] = { { 2500, 8 }, { 3200, 8 }, { 4200, 8 } };



/* Confirmation melodies */
const BuzzerNote SoundManager::MEL_CONFIRM_0[] = { { 880, 80 }, { 1100, 80 }, { 1320, 120 } };
const BuzzerNote SoundManager::MEL_CONFIRM_1[] = { { 1047, 90 }, { 1319, 90 }, { 1568, 140 } };
const BuzzerNote SoundManager::MEL_CONFIRM_2[] = { { 1200, 80 }, { 0, 60 }, { 1500, 120 } };
const BuzzerNote SoundManager::MEL_CONFIRM_3[] = { { 523, 70 }, { 659, 70 }, { 784, 70 }, { 1047, 130 } };
const BuzzerNote SoundManager::MEL_CONFIRM_4[] = { { 1800, 50 }, { 2400, 50 }, { 1800, 50 }, { 2400, 100 } };
const BuzzerNote SoundManager::MEL_CONFIRM_5[] = { { 784, 120 }, { 1047, 160 } };



/* Error melodies */
const BuzzerNote SoundManager::MEL_ERROR_0[] = { { 400, 150 }, { 250, 220 } };
const BuzzerNote SoundManager::MEL_ERROR_1[] = { { 300, 100 }, { 0, 80 }, { 300, 100 } };
const BuzzerNote SoundManager::MEL_ERROR_2[] = { { 200, 250 }, { 150, 300 } };
const BuzzerNote SoundManager::MEL_ERROR_3[] = { { 350, 120 }, { 370, 120 } };
const BuzzerNote SoundManager::MEL_ERROR_4[] = { { 500, 100 }, { 420, 100 }, { 340, 180 } };
const BuzzerNote SoundManager::MEL_ERROR_5[] = { { 180, 200 } };



/* Alarm melodies (looping) */
const BuzzerNote SoundManager::MEL_ALARM_0[] = { { 2200, 180 }, { 0, 100 }, { 2200, 180 }, { 0, 500 } };
const BuzzerNote SoundManager::MEL_ALARM_1[] = { { 1800, 200 }, { 2400, 200 }, { 1800, 200 }, { 0, 400 } };
const BuzzerNote SoundManager::MEL_ALARM_2[] = { { 3000, 100 }, { 0, 60 }, { 3000, 100 }, { 0, 400 } };
const BuzzerNote SoundManager::MEL_ALARM_3[] = { { 2600, 300 }, { 0, 600 } };
const BuzzerNote SoundManager::MEL_ALARM_4[] = { { 1600, 150 }, { 2000, 150 }, { 2400, 150 }, { 0, 450 } };
const BuzzerNote SoundManager::MEL_ALARM_5[] = { { 2800, 60 }, { 0, 40 }, { 2800, 60 }, { 0, 40 }, { 2800, 60 }, { 0, 400 } };


/* =========================================================================== */
/*                MELODY LOOKUP TABLE — [4 TYPES][6 VARIANTS]                */
/* =========================================================================== */

const MelodyDef SoundManager::MELODIES[4][SND_MELODY_VARIANTS] = {

    {
        { MEL_TOUCH_0, 1 }, { MEL_TOUCH_1, 2 }, { MEL_TOUCH_2, 1 },
        { MEL_TOUCH_3, 1 }, { MEL_TOUCH_4, 2 }, { MEL_TOUCH_5, 3 }
    },

    {
        { MEL_CONFIRM_0, 3 }, { MEL_CONFIRM_1, 3 }, { MEL_CONFIRM_2, 3 },
        { MEL_CONFIRM_3, 4 }, { MEL_CONFIRM_4, 4 }, { MEL_CONFIRM_5, 2 }
    },

    {
        { MEL_ERROR_0, 2 }, { MEL_ERROR_1, 3 }, { MEL_ERROR_2, 2 },
        { MEL_ERROR_3, 2 }, { MEL_ERROR_4, 3 }, { MEL_ERROR_5, 1 }
    },

    {
        { MEL_ALARM_0, 4 }, { MEL_ALARM_1, 4 }, { MEL_ALARM_2, 4 },
        { MEL_ALARM_3, 2 }, { MEL_ALARM_4, 4 }, { MEL_ALARM_5, 6 }
    }
};


SoundManager::SoundManager()
    : _buzzer(BUZZER_PIN, BUZZER_PIO_BLOCK) {
    memset(_queue, 0, sizeof(_queue));
}

void SoundManager::begin() {
    if (!_buzzer.begin()) {


        Serial.println(F("[SND] WARN: BuzzerPIO begin() failed — no sound."));
        Serial.println(F("[SND]   Neither PIO block has 4 free instr slots + 2 SMs."));
        return;
    }

    Serial.print(F("[SND] BuzzerPIO ready on pio"));


    _buzzer.setVolume(_volume);
}


const MelodyDef& SoundManager::resolveMelody(SoundEvent event) const {
    uint8_t typeIdx = 0;
    uint8_t varIdx  = 0;

    switch (event) {
        case SND_TOUCH_CLICK: typeIdx = 0; varIdx = _melTouch;   break;
        case SND_CONFIRM:     typeIdx = 1; varIdx = _melConfirm; break;
        case SND_ERROR:       typeIdx = 2; varIdx = _melError;   break;
        case SND_ALARM_START: typeIdx = 3; varIdx = _melAlarm;   break;
        default:              break;
    }

    if (varIdx >= SND_MELODY_VARIANTS) varIdx = 0;
    return MELODIES[typeIdx][varIdx];
}


/* =========================================================================== */
/*                       EVENT CONSUMER — UPDATE LOOP                        */
/* =========================================================================== */
/**
 * @brief Consume queued sound events and manage alarm suspend/resume.
 *
 * System sounds (touch/confirm/error) can play OVER an active alarm:
 *   1. Stop the alarm loop in hardware
 *   2. Play system sound at system volume
 *   3. Schedule alarm loop resumption after sound duration + 30ms margin
 */
void SoundManager::update() {


    if (_alarmSuspended && millis() >= _alarmResumeTime) {
        _alarmSuspended = false;
        if (_alarming && _enableAlarm && !_muted) {
            _buzzer.setVolume(_alarmVolume);
            const MelodyDef& m = resolveMelody(SND_ALARM_START);
            _buzzer.playMelodyLoop(m.notes, m.len);
        }
    }

    SoundEvent ev;

    while (dequeue(ev)) {
        switch (ev) {
            case SND_TOUCH_CLICK:

                if (_enableTouch && !_muted) {
                    const MelodyDef& m = resolveMelody(ev);
                    playSystemSound(m);
                }
                break;

            case SND_CONFIRM:

                if (_enableConfirm && !_muted) {
                    const MelodyDef& m = resolveMelody(ev);
                    playSystemSound(m);
                }
                break;

            case SND_ERROR:

                if (_enableError && !_muted) {
                    const MelodyDef& m = resolveMelody(ev);
                    playSystemSound(m);
                }
                break;

            case SND_ALARM_START:


                _alarming = true;
                _alarmSuspended = false;
                if (_enableAlarm && !_muted) {
                    _buzzer.setVolume(_alarmVolume);
                    const MelodyDef& m = resolveMelody(ev);
                    _buzzer.playMelodyLoop(m.notes, m.len);
                }
                break;

            case SND_ALARM_STOP:
                _alarming = false;
                _alarmSuspended = false;
                _buzzer.stopMelody();
                _buzzer.setVolume(_volume);
                break;

            default:
                break;
        }
    }
}


/**
 * @brief Play a system sound, suspending active alarm if necessary.
 * If called multiple times before resumption, the timer is extended
 * to always resume after the LAST system sound.
 */
void SoundManager::playSystemSound(const MelodyDef& m) {
    if (_alarming && !_alarmSuspended) {

        _buzzer.stopMelody();
    }


    _buzzer.setVolume(_volume);
    _buzzer.playMelody(m.notes, m.len);


    if (_alarming) {
        _alarmSuspended  = true;
        _alarmResumeTime = millis() + melodyDurationMs(m) + 30;
    }
}


uint16_t SoundManager::melodyDurationMs(const MelodyDef& m) {
    uint16_t total = 0;
    for (uint8_t i = 0; i < m.len; i++) {
        total += m.notes[i].durationMs + 2;
    }
    return total;
}


void SoundManager::play(SoundEvent event) {
    if (event == SND_NONE) return;


    if (event == SND_ALARM_STOP) {
        _alarming = false;
        _alarmSuspended = false;
        _buzzer.stopMelody();
        _queueHead = _queueTail = 0;
        return;
    }


    if (event == SND_ALARM_START) {
        _alarmSuspended = false;
        _buzzer.stopMelody();
        _queueHead = _queueTail = 0;
    }

    enqueue(event);
}

void SoundManager::startAlarm() {
    play(SND_ALARM_START);
}

void SoundManager::stopAlarm() {
    play(SND_ALARM_STOP);
}


void SoundManager::playPreview(SoundEvent event, uint8_t melodyIdx) {
    if (_muted || !_buzzer.isReady()) return;
    if (melodyIdx >= SND_MELODY_VARIANTS) melodyIdx = 0;

    uint8_t typeIdx = 0;
    switch (event) {
        case SND_TOUCH_CLICK: typeIdx = 0; break;
        case SND_CONFIRM:     typeIdx = 1; break;
        case SND_ERROR:       typeIdx = 2; break;
        case SND_ALARM_START: typeIdx = 3; break;
        default: return;
    }

    const MelodyDef& m = MELODIES[typeIdx][melodyIdx];


    _queueHead = _queueTail = 0;

    if (event == SND_ALARM_START) {


        _buzzer.setVolume(_alarmVolume);
        _buzzer.playMelody(m.notes, m.len);
    } else {

        playSystemSound(m);
    }
}


bool SoundManager::enqueue(SoundEvent ev) {
    uint8_t nextHead = (_queueHead + 1) % QUEUE_SIZE;
    if (nextHead == _queueTail) return false;


    if (ev == SND_TOUCH_CLICK) {
        for (uint8_t i = _queueTail; i != _queueHead; i = (i + 1) % QUEUE_SIZE) {
            if (_queue[i] == SND_TOUCH_CLICK) return false;
        }
    }

    _queue[_queueHead] = ev;
    _queueHead = nextHead;
    return true;
}

bool SoundManager::dequeue(SoundEvent& ev) {
    if (_queueHead == _queueTail) return false;

    ev = _queue[_queueTail];
    _queueTail = (_queueTail + 1) % QUEUE_SIZE;
    return true;
}


void SoundManager::setMelodyIndex(SoundEvent event, uint8_t idx) {
    if (idx >= SND_MELODY_VARIANTS) idx = 0;

    switch (event) {
        case SND_TOUCH_CLICK: _melTouch   = idx; break;
        case SND_CONFIRM:     _melConfirm = idx; break;
        case SND_ERROR:       _melError   = idx; break;
        case SND_ALARM_START: _melAlarm   = idx; break;
        default: break;
    }
}

uint8_t SoundManager::getMelodyIndex(SoundEvent event) const {
    switch (event) {
        case SND_TOUCH_CLICK: return _melTouch;
        case SND_CONFIRM:     return _melConfirm;
        case SND_ERROR:       return _melError;
        case SND_ALARM_START: return _melAlarm;
        default: return 0;
    }
}


void SoundManager::setVolume(uint8_t vol) {
    _volume = (vol <= 100) ? vol : 100;


    if (!_alarming || _alarmSuspended) {
        _buzzer.setVolume(_volume);
    }
}


void SoundManager::setAlarmVolume(uint8_t vol) {
    _alarmVolume = (vol <= 100) ? vol : 100;

    if (_alarming && !_alarmSuspended && _enableAlarm && !_muted) {
        _buzzer.setVolume(_alarmVolume);
    }
}

void SoundManager::setMuted(bool muted) {
    bool wasMuted = _muted;
    _muted = muted;

    if (_muted && _alarming) {

        _alarmSuspended = false;
        _buzzer.stopMelody();
        _queueHead = _queueTail = 0;
    }
    else if (!_muted && wasMuted && _alarming && _enableAlarm) {


        _alarmSuspended = false;
        _buzzer.setVolume(_alarmVolume);
        const MelodyDef& m = resolveMelody(SND_ALARM_START);
        _buzzer.playMelodyLoop(m.notes, m.len);
    }
}


/* =========================================================================== */
/*                   FLASH PERSISTENCE — reserved[10..15]                    */
/* =========================================================================== */
/**
 * @brief Load sound configuration from Flash (6-byte packed struct).
 * Handles migration from older firmware versions (missing fields default to 70%).
 */
void SoundManager::loadConfig(const SoundConfigData* data) {
    if (!data || data->magic != 0xAB) {
        _enableTouch   = true;
        _enableConfirm = true;
        _enableError   = true;
        _enableAlarm   = true;
        _enableWeb     = true;
        _muted         = false;
        _volume        = 70;
        _alarmVolume   = 70;
        _melTouch = _melConfirm = _melError = _melAlarm = 0;
        return;
    }

    _enableTouch   = (data->flags & SND_FLAG_TOUCH)   != 0;
    _enableConfirm = (data->flags & SND_FLAG_CONFIRM)  != 0;
    _enableError   = (data->flags & SND_FLAG_ERROR)    != 0;
    _enableAlarm   = (data->flags & SND_FLAG_ALARM)    != 0;
    _muted         = (data->flags & SND_FLAG_MUTE)     != 0;
    _enableWeb     = (data->flags & SND_FLAG_WEB)      != 0;

    _volume = (data->volume <= 100) ? data->volume : 70;


    _alarmVolume = (data->alarmVolume > 0 && data->alarmVolume <= 100)
                 ? data->alarmVolume : 70;


    uint16_t mp = (uint16_t)data->melLow | ((uint16_t)data->melHigh << 8);
    _melTouch   = (mp >> 0) & 0x07;
    _melConfirm = (mp >> 3) & 0x07;
    _melError   = (mp >> 6) & 0x07;
    _melAlarm   = (mp >> 9) & 0x07;

    if (_melTouch   >= SND_MELODY_VARIANTS) _melTouch   = 0;
    if (_melConfirm >= SND_MELODY_VARIANTS) _melConfirm = 0;
    if (_melError   >= SND_MELODY_VARIANTS) _melError   = 0;
    if (_melAlarm   >= SND_MELODY_VARIANTS) _melAlarm   = 0;

    _buzzer.setVolume(_volume);
}

void SoundManager::fillConfig(SoundConfigData* data) const {
    if (!data) return;

    data->magic = 0xAB;

    data->flags = 0;
    if (_enableTouch)   data->flags |= SND_FLAG_TOUCH;
    if (_enableConfirm) data->flags |= SND_FLAG_CONFIRM;
    if (_enableError)   data->flags |= SND_FLAG_ERROR;
    if (_enableAlarm)   data->flags |= SND_FLAG_ALARM;
    if (_muted)         data->flags |= SND_FLAG_MUTE;
    if (_enableWeb)     data->flags |= SND_FLAG_WEB;

    data->volume = _volume;


    uint16_t mp = ((uint16_t)(_melTouch   & 0x07))
                | ((uint16_t)(_melConfirm & 0x07) << 3)
                | ((uint16_t)(_melError   & 0x07) << 6)
                | ((uint16_t)(_melAlarm   & 0x07) << 9);
    data->melLow  = (uint8_t)(mp & 0xFF);
    data->melHigh = (uint8_t)((mp >> 8) & 0xFF);

    data->alarmVolume = _alarmVolume;
}


SoundSettingsState SoundManager::getSettingsState() const {
    SoundSettingsState s;
    s.touchEnabled   = _enableTouch;
    s.confirmEnabled = _enableConfirm;
    s.errorEnabled   = _enableError;
    s.alarmEnabled   = _enableAlarm;
    s.webEnabled     = _enableWeb;
    s.muted          = _muted;
    s.volume         = _volume;
    s.alarmVolume    = _alarmVolume;
    s.touchMelody    = _melTouch;
    s.confirmMelody  = _melConfirm;
    s.errorMelody    = _melError;
    s.alarmMelody    = _melAlarm;
    return s;
}

void SoundManager::applySettingsState(const SoundSettingsState& state) {
    _enableTouch   = state.touchEnabled;
    _enableConfirm = state.confirmEnabled;
    _enableError   = state.errorEnabled;
    _enableAlarm   = state.alarmEnabled;
    _enableWeb     = state.webEnabled;

    _melTouch   = (state.touchMelody   < SND_MELODY_VARIANTS) ? state.touchMelody   : 0;
    _melConfirm = (state.confirmMelody < SND_MELODY_VARIANTS) ? state.confirmMelody : 0;
    _melError   = (state.errorMelody   < SND_MELODY_VARIANTS) ? state.errorMelody   : 0;
    _melAlarm   = (state.alarmMelody   < SND_MELODY_VARIANTS) ? state.alarmMelody   : 0;

    setVolume(state.volume);
    setAlarmVolume(state.alarmVolume);


    setMuted(state.muted);
}


void SoundManager::setEnabled(SoundEvent event, bool enabled) {
    switch (event) {
        case SND_TOUCH_CLICK: _enableTouch   = enabled; break;
        case SND_CONFIRM:     _enableConfirm = enabled; break;
        case SND_ERROR:       _enableError   = enabled; break;
        case SND_ALARM_START: _enableAlarm   = enabled; break;
        default: break;
    }
}

bool SoundManager::isEnabled(SoundEvent event) const {
    switch (event) {
        case SND_TOUCH_CLICK: return _enableTouch;
        case SND_CONFIRM:     return _enableConfirm;
        case SND_ERROR:       return _enableError;
        case SND_ALARM_START: return _enableAlarm;
        default: return false;
    }
}
