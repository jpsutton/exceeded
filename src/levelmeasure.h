/* levelmeasure.h - reference renders and loudness of assets (spec 15 sections 2.2, 4) */
#ifndef LEVELMEASURE_H
#define LEVELMEASURE_H
#include "audiolevel.h"
#include "dosaudio.h"
#define LEVEL_RATE 44100                    /* the mixer rate the music and DOS references are rendered at */
/* serialises FluidSynth settings / synth creation and deletion across threads (not thread-safe) */
void fluid_global_lock(void);
void fluid_global_unlock(void);
/* WAVE: 8-bit unsigned or 16-bit, mono or stereo (mean), at its own rate. 0 = ok. */
int measure_wave(const uint8_t *riff, size_t len, Loud *out);
int measure_dos_song(const DosGame *g, int song, int variant, Loud *out);
/* MIDI: FluidSynth offline (gain 0.2, default reverb/chorus, mono (L+R)/2, one pass, at most 120 s). 0 = ok, -1 = no FluidSynth / failed. */
int measure_midi(const char *soundfont, const uint8_t *smf, size_t len, Loud *out);
/* decode a WAVE to mono float samples at its own rate (caller frees). NULL on failure. */
float *wave_to_float(const uint8_t *riff, size_t len, size_t *n, int *rate);
#endif
