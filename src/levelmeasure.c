/* levelmeasure.c - reference renders and loudness of assets (spec 15 sections 2.2, 4) */
#include "levelmeasure.h"
#include "util.h"
#include <pthread.h>
#ifdef HAVE_FLUIDSYNTH
#include <fluidsynth.h>
#endif

/* FluidSynth's settings / synth creation and deletion are not thread-safe (creating settings initialises the audio driver back-ends):
 * every thread of the engine serialises them behind this one lock. */
static pthread_mutex_t fl_mu = PTHREAD_MUTEX_INITIALIZER;
void fluid_global_lock(void) { pthread_mutex_lock(&fl_mu); }
void fluid_global_unlock(void) { pthread_mutex_unlock(&fl_mu); }

float *wave_to_float(const uint8_t *d, size_t len, size_t *n, int *rate) {
    if (len < 12) return NULL;
    int chans = 1, bits = 8, fs = 11025;
    size_t p = 12;
    const uint8_t *data = NULL;
    size_t dlen = 0;
    while (p + 8 <= len) {
        uint32_t cl = rd32(d + p + 4);
        if (!memcmp(d + p, "fmt ", 4) && p + 24 <= len) { chans = rd16(d + p + 10); fs = (int)rd32(d + p + 12); bits = rd16(d + p + 22); }
        else if (!memcmp(d + p, "data", 4)) { data = d + p + 8; dlen = cl; if (p + 8 + cl > len) dlen = len - p - 8; break; }
        p += 8 + cl + (cl & 1);
    }
    if (chans < 1) chans = 1;
    if (!data || (bits != 8 && bits != 16)) return NULL;
    size_t frames = dlen / (size_t)(bits / 8) / (size_t)chans;
    float *x = xmalloc(sizeof(float) * (frames + 1));
    for (size_t i = 0; i < frames; i++) {
        double s = 0;
        for (int c = 0; c < chans; c++) {
            size_t k = i * (size_t)chans + (size_t)c;
            s += bits == 8 ? ((int)data[k] - 128) / 128.0 : (int16_t)(data[k * 2] | (data[k * 2 + 1] << 8)) / 32768.0;
        }
        x[i] = (float)(s / chans);
    }
    *n = frames;
    *rate = fs;
    return x;
}

int measure_wave(const uint8_t *riff, size_t len, Loud *out) {
    size_t n;
    int fs;
    float *x = wave_to_float(riff, len, &n, &fs);
    if (!x) { memset(out, 0, sizeof *out); out->silent = 1; return -1; }
    loud_measure(x, n, fs, out);
    free(x);
    return 0;
}

int measure_dos_song(const DosGame *g, int song, int variant, Loud *out) {
    size_t n;
    float *x = dos_render_float(g, song, variant, LEVEL_RATE, 28402, &n);       /* 120 s of driver ticks */
    if (!x) { memset(out, 0, sizeof *out); out->silent = 1; return -1; }
    if (n > (size_t)LEVEL_RATE * 120) n = (size_t)LEVEL_RATE * 120;
    loud_measure(x, n, LEVEL_RATE, out);
    free(x);
    return 0;
}

int measure_midi(const char *soundfont, const uint8_t *smf, size_t len, Loud *out) {
#ifdef HAVE_FLUIDSYNTH
    static fluid_settings_t *st;                       /* one settings object for all measurements, made once (no audio driver is started with it) */
    fluid_global_lock();
    if (!st) {
        st = new_fluid_settings();
        fluid_settings_setnum(st, "synth.sample-rate", (double)LEVEL_RATE);
        fluid_settings_setnum(st, "synth.gain", 0.2);
    }
    fluid_synth_t *sy = new_fluid_synth(st);
    fluid_global_unlock();
    if (fluid_synth_sfload(sy, soundfont, 1) < 0) { fluid_global_lock(); delete_fluid_synth(sy); fluid_global_unlock(); return -1; }
    fluid_global_lock();
    fluid_player_t *pl = new_fluid_player(sy);
    fluid_global_unlock();
    if (fluid_player_add_mem(pl, smf, len) != FLUID_OK) { fluid_global_lock(); delete_fluid_player(pl); delete_fluid_synth(sy); fluid_global_unlock(); return -1; }
    fluid_player_play(pl);
    size_t cap = (size_t)LEVEL_RATE * 120, n = 0;
    float *x = xmalloc(sizeof(float) * cap);
    float l[512], r[512];
    while (n + 512 <= cap && fluid_player_get_status(pl) == FLUID_PLAYER_PLAYING) {
        fluid_synth_write_float(sy, 512, l, 0, 1, r, 0, 1);
        for (int i = 0; i < 512; i++) x[n + (size_t)i] = (l[i] + r[i]) * 0.5f;
        n += 512;
    }
    fluid_global_lock();
    delete_fluid_player(pl);
    delete_fluid_synth(sy);
    fluid_global_unlock();
    loud_measure(x, n, LEVEL_RATE, out);
    free(x);
    return 0;
#else
    (void)soundfont; (void)smf; (void)len; (void)out;
    return -1;
#endif
}
