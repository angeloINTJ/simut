/**
 * @file SoundConfigPack.h
 * @brief The six bytes the sound settings are stored as, in
 *        SystemConfig::reserved[12..17], and the one function that packs them.
 *
 * @details Split out of SoundManager.h on 2026-10-02 so the commit's parser can
 * write a settings state into the configuration it was handed — the dry run's
 * copy, or the try's — without going through a running SoundManager. It used
 * to call applySettingsState( ) and then fillConfig( ), which serialised the
 * result: the page sends a dry run 600 ms after every staged edit, so staging
 * the global mute muted the device, and setMuted( ) stops an alarm that is
 * sounding (findings 6 and 38 of docs/analysis/PLANO_REVISAO_EXTERNA.md).
 *
 * Plain types and one inline function, so the native suite tests the packing
 * (test/test_alarm_queue). SoundManager::fillConfig( ) packs through the same
 * function, so the two cannot drift.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stdint.h>

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

/* Melodies per event: each is a 3-bit field of melLow/melHigh, six used. */
#define SND_MELODY_VARIANTS 6

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

/**
 * The bytes a settings state is stored as: what applySettingsState( ) followed
 * by fillConfig( ) leaves in SoundConfigData — the same clamps (a melody past
 * the sixth is the first, a volume past 100 is 100) and the same layout, magic
 * 0xAC — without touching anything else.
 */
/* noinline: one copy, shared by fillConfig( ) and the commit. */
inline __attribute__((noinline)) void soundStateToConfig(const SoundSettingsState& s, SoundConfigData* data) {
 if (!data) return;

 /* Schema gained ATTENTION bit (flags) + 3 bits melAttention
 * (mp 12..14). Magic bumped 0xAB → 0xAC to distinguish layouts. */
 data->magic = 0xAC;

 data->flags = 0;
 if (s.touchEnabled) data->flags |= SND_FLAG_TOUCH;
 if (s.confirmEnabled) data->flags |= SND_FLAG_CONFIRM;
 if (s.errorEnabled) data->flags |= SND_FLAG_ERROR;
 if (s.alarmEnabled) data->flags |= SND_FLAG_ALARM;
 if (s.muted) data->flags |= SND_FLAG_MUTE;
 if (s.webEnabled) data->flags |= SND_FLAG_WEB;
 if (s.attentionEnabled) data->flags |= SND_FLAG_ATTENTION;

 data->volume = (s.volume <= 100) ? s.volume : 100;

 auto mel = [](uint8_t m) -> uint16_t { return (m < SND_MELODY_VARIANTS) ? m : 0; };
 uint16_t mp = mel(s.touchMelody)
 | (uint16_t)(mel(s.confirmMelody) << 3)
 | (uint16_t)(mel(s.errorMelody) << 6)
 | (uint16_t)(mel(s.alarmMelody) << 9)
 | (uint16_t)(mel(s.attentionMelody) << 12);
 data->melLow = (uint8_t)(mp & 0xFF);
 data->melHigh = (uint8_t)((mp >> 8) & 0xFF);

 data->alarmVolume = (s.alarmVolume <= 100) ? s.alarmVolume : 100;
}
