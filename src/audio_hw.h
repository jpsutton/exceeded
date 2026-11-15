/* audio_hw.h - optional real audio output hooks (set by the SDL backend) */
#ifndef AUDIO_HW_H
#define AUDIO_HW_H
#include <stddef.h>
#include <stdint.h>
typedef struct AudioHw {
    void (*sfx_start)(int slot, const uint8_t *riff, uint32_t len, int loop, float db_on, float db_off);   /* gains in dB with Even out on / off (spec 15) */
    void (*sfx_stop)(int slot);
    void (*speech_start)(const uint8_t *riff, uint32_t len, float db_on, float db_off);
    void (*speech_stop)(void);
    void (*music_start)(const uint8_t *smf, size_t len, int loop, float db_on, float db_off);
    void (*music_stop)(void);
    void (*pause)(int on);
} AudioHw;
extern const AudioHw *g_audio_hw;
#endif
