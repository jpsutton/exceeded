/* audiolevel.h - loudness measurement (ITU-R BS.1770-4), gains and slider law (spec chapter 15) */
#ifndef AUDIOLEVEL_H
#define AUDIOLEVEL_H
#include <stddef.h>
#include <stdint.h>

enum { LB_MUSIC = 0, LB_EFFECTS = 1, LB_SPEECH = 2 };
enum { LS_WINDOWS = 0, LS_TANDY = 1, LS_SPEAKER = 2 };     /* same numbering as AS_* in dosint.h */

typedef struct Loud {
    int silent;                  /* no block above the absolute gate (or no samples) */
    int has_peak;
    double lufs;                 /* integrated loudness */
    double peak_db;              /* sample peak in dBFS */
} Loud;

void loud_measure(const float *x, size_t n, int fs, Loud *out);
double level_target(int bus);                          /* -20 / -16 / -15 LUFS */
double level_style_gain(int style, int bus);           /* section 3 (dB); 0 for combinations that do not exist */
/* gain of one asset (15 section 4.3), 0.1 dB steps. is_wave applies the +3 dBFS peak cap. */
double level_asset_gain(int style, int bus, const Loud *l, int is_wave, int even_out);
double level_slider_db(int v, int *muted);             /* 40*log10(v/100); v = 0 -> muted */
double level_db_to_lin(double db);
float level_softclip(float x, float knee, float ceiling);
#endif
