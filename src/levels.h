/* levels.h - runtime volume state and per-asset gains (spec chapter 15) */
#ifndef LEVELS_H
#define LEVELS_H
#include "audiolevel.h"
#include "dosaudio.h"

extern volatile int lv_vol[3];       /* slider values 0..100: music, effects, speech */
extern volatile int lv_even;         /* "Even out volume" */
void levels_set(int vm, int ve, int vs, int even);
double levels_slider_lin(int bus);   /* 0 when muted */

/* WAVE gains in dB with Even out on / off (measured at first load, cached by (bus, id)) */
void levels_wave(int bus, int id, const uint8_t *riff, size_t len, float *db_on, float *db_off);
/* MIDI: measured offline on a worker thread once a SoundFont is known; until then both = the style gain */
void levels_midi_start(const char *soundfont);                  /* measure the 8 songs of the active set in the background */
void levels_midi(const char *name, const uint8_t *smf, size_t len, float *db_on, float *db_off);   /* measures on demand, waits at most 1.5 s */
/* DOS songs of a variant: measured when the DOS files are loaded */
void levels_dos_measure(const DosGame *g, int variant);
void levels_dos_ensure(const DosGame *g, int variant, int song);        /* before a DOS song starts: measure on demand / wait at most 1.5 s */
void levels_dos(int variant, int song, float *db_on, float *db_off);
#endif
