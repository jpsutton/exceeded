/* levels.c - runtime volume state and per-asset gains (spec chapter 15) */
#include "levels.h"
#include "levelmeasure.h"
#include "res.h"
#include "util.h"
#include "vfs.h"
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <strings.h>

volatile int lv_vol[3] = {100, 100, 100};
volatile int lv_even = 1;

void levels_set(int vm, int ve, int vs, int even) {
    lv_vol[0] = vm < 0 ? 0 : vm > 100 ? 100 : vm;
    lv_vol[1] = ve < 0 ? 0 : ve > 100 ? 100 : ve;
    lv_vol[2] = vs < 0 ? 0 : vs > 100 ? 100 : vs;
    lv_even = even != 0;
}

double levels_slider_lin(int bus) {
    int muted;
    double db = level_slider_db(lv_vol[bus], &muted);
    return muted ? 0.0 : level_db_to_lin(db);
}

/* ---------------------------------------------------------------- WAVEs */
typedef struct WG { int bus, id; float on, off; } WG;
static WG *wg;
static int nwg, cwg;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

void levels_wave(int bus, int id, const uint8_t *riff, size_t len, float *db_on, float *db_off) {
    pthread_mutex_lock(&mu);
    for (int i = 0; i < nwg; i++) if (wg[i].bus == bus && wg[i].id == id) { *db_on = wg[i].on; *db_off = wg[i].off; pthread_mutex_unlock(&mu); return; }
    pthread_mutex_unlock(&mu);
    Loud l;
    if (measure_wave(riff, len, &l) != 0) { l.silent = 1; l.has_peak = 0; }
    float on = (float)level_asset_gain(LS_WINDOWS, bus, &l, 1, 1), off = (float)level_asset_gain(LS_WINDOWS, bus, &l, 1, 0);
    pthread_mutex_lock(&mu);
    if (nwg == cwg) { cwg = cwg ? cwg * 2 : 128; wg = xrealloc(wg, sizeof(WG) * (size_t)cwg); }
    wg[nwg++] = (WG){bus, id, on, off};
    pthread_mutex_unlock(&mu);
    *db_on = on;
    *db_off = off;
}

/* ---------------------------------------------------------------- MIDI: background worker, and on demand (spec 15 section 4.2) */
typedef struct MJob { char name[64]; int state; float on, off; } MJob;     /* state 0 new, 1 being measured, 2 done */
static MJob mj[16];
static int nmj;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int mstarted;
static char msf[1100];

static MJob *mjob_locked(const char *key) {
    for (int i = 0; i < nmj; i++) if (!strcmp(mj[i].name, key)) return &mj[i];
    if (nmj >= 16) return NULL;
    snprintf(mj[nmj].name, sizeof mj[nmj].name, "%s", key);
    mj[nmj].state = 0;
    return &mj[nmj++];
}

static void lower(const char *in, char *out, size_t n) {
    snprintf(out, n, "%s", in);
    for (char *c = out; *c; c++) *c = (char)tolower((unsigned char)*c);
}

static void measure_into(MJob *j, const uint8_t *smf, size_t len) {
    Loud l;
    int ok = measure_midi(msf, smf, len, &l) == 0;
    float on, off;
    if (ok) { on = (float)level_asset_gain(LS_WINDOWS, LB_MUSIC, &l, 0, 1); off = (float)level_asset_gain(LS_WINDOWS, LB_MUSIC, &l, 0, 0); }
    else on = off = (float)level_style_gain(LS_WINDOWS, LB_MUSIC);
    pthread_mutex_lock(&mu);
    j->on = on; j->off = off; j->state = 2;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

typedef struct MArg { MJob *j; const uint8_t *smf; size_t len; } MArg;
static void *midi_demand_thread(void *p) { MArg *a = p; measure_into(a->j, a->smf, a->len); free(a); return NULL; }

static void *midi_worker(void *arg) {
    (void)arg;
    static const int slots[8] = {1717, 1724, 1728, 1729, 1735, 1745, 1746, 1757};
    for (int s = 0; s < 8; s++) {
        size_t tl;
        const uint8_t *t = res_get("TEXT", slots[s], &tl);
        if (!t || !g_data_src) continue;
        char name[64];
        size_t i = 0;
        while (i < tl && i < 63 && t[i] && t[i] != '\r' && t[i] != '\n') { name[i] = (char)t[i]; i++; }
        name[i] = 0;
        if (!name[0]) continue;
        char fn[96];
        snprintf(fn, sizeof fn, "MIDI/%s", name);
        fn[5] = 'e';
        size_t ml;
        const uint8_t *mb = vs_read(g_data_src, fn, &ml);
        if (!mb) continue;
        char key[64];
        lower(name + 1, key, sizeof key);
        pthread_mutex_lock(&mu);
        MJob *j = mjob_locked(key);
        int mine = j && j->state == 0;
        if (mine) j->state = 1;
        pthread_mutex_unlock(&mu);
        if (mine) measure_into(j, mb, ml);                  /* a song already taken by an on-demand request is skipped */
    }
    return NULL;
}

void levels_midi_start(const char *soundfont) {
    if (mstarted || !soundfont) return;
    mstarted = 1;
    snprintf(msf, sizeof msf, "%s", soundfont);
    pthread_t th;
    pthread_create(&th, NULL, midi_worker, NULL);
    pthread_detach(th);
}

/* A song requested before its measurement is done is measured at once (or its running measurement awaited): the start is delayed by at most
 * 1.5 s; past the cap this play uses the style gain and the measured gain applies from the next start. */
void levels_midi(const char *name, const uint8_t *smf, size_t len, float *db_on, float *db_off) {
    char key[64];
    lower(name, key, sizeof key);
    float G = (float)level_style_gain(LS_WINDOWS, LB_MUSIC);
    *db_on = *db_off = G;
    if (!msf[0]) return;                                    /* no SoundFont: nothing to measure with */
    pthread_mutex_lock(&mu);
    MJob *j = mjob_locked(key);
    if (!j) { pthread_mutex_unlock(&mu); return; }
    if (j->state == 0 && smf) {
        j->state = 1;
        MArg *a = xmalloc(sizeof *a);
        a->j = j; a->smf = smf; a->len = len;
        pthread_t th;
        pthread_create(&th, NULL, midi_demand_thread, a);
        pthread_detach(th);
    }
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 1;
    dl.tv_nsec += 500000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    while (j->state != 2) if (pthread_cond_timedwait(&cv, &mu, &dl) != 0) break;
    if (j->state == 2) { *db_on = j->on; *db_off = j->off; }
    pthread_mutex_unlock(&mu);
}

/* ---------------------------------------------------------------- DOS songs */
static struct { float on[DOS_SONGS], off[DOS_SONGS]; int state[DOS_SONGS]; int started; uint8_t sha[32]; } dg[2];

typedef struct DosJob { DosGame g; int variant; } DosJob;

static void dos_measure_song(const DosGame *g, int variant, int s) {
    int style = variant == DOS_TANDY ? LS_TANDY : LS_SPEAKER;
    int bus = (s <= 5 || s == 40 || s == 49) ? LB_MUSIC : LB_EFFECTS;
    Loud l;
    measure_dos_song(g, s, variant, &l);
    float on = (float)level_asset_gain(style, bus, &l, 0, 1), off = (float)level_asset_gain(style, bus, &l, 0, 0);
    pthread_mutex_lock(&mu);
    if (!memcmp(dg[variant].sha, g->sha, 32)) { dg[variant].on[s] = on; dg[variant].off[s] = off; dg[variant].state[s] = 2; }
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

static void *dos_worker(void *p) {
    DosJob *j = p;
    for (int s = 0; s < DOS_SONGS; s++) {
        pthread_mutex_lock(&mu);
        int mine = !memcmp(dg[j->variant].sha, j->g.sha, 32) && dg[j->variant].state[s] == 0;
        if (mine) dg[j->variant].state[s] = 1;
        pthread_mutex_unlock(&mu);
        if (mine) dos_measure_song(&j->g, j->variant, s);
    }
    dos_game_free(&j->g);
    free(j);
    return NULL;
}

/* all songs of the variant in the background (about a second of CPU); songs requested earlier are measured on demand (levels_dos_ensure) */
void levels_dos_measure(const DosGame *g, int variant) {
    pthread_mutex_lock(&mu);
    int skip = dg[variant].started && !memcmp(dg[variant].sha, g->sha, 32);
    if (!skip) { memset(&dg[variant], 0, sizeof dg[variant]); memcpy(dg[variant].sha, g->sha, 32); dg[variant].started = 1; }
    pthread_mutex_unlock(&mu);
    if (skip) return;
    DosJob *j = xcalloc(1, sizeof *j);
    j->g = *g;
    j->g.img = xmalloc(g->img_n + 1);
    memcpy(j->g.img, g->img, g->img_n);
    j->g.moc = xmalloc(g->moc_n + 1);
    memcpy(j->g.moc, g->moc, g->moc_n);
    j->variant = variant;
    pthread_t th;
    pthread_create(&th, NULL, dos_worker, j);
    pthread_detach(th);
}

/* a DOS song about to start: measure it now if nobody has, or wait (at most 1.5 s) for the running measurement (spec 15 section 4.2) */
void levels_dos_ensure(const DosGame *g, int variant, int song) {
    if (song < 0 || song >= DOS_SONGS) return;
    pthread_mutex_lock(&mu);
    if (memcmp(dg[variant].sha, g->sha, 32) || dg[variant].state[song] == 2) { pthread_mutex_unlock(&mu); return; }
    if (dg[variant].state[song] == 0) {
        dg[variant].state[song] = 1;
        pthread_mutex_unlock(&mu);
        dos_measure_song(g, variant, song);
        return;
    }
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 1;
    dl.tv_nsec += 500000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    while (dg[variant].state[song] != 2) if (pthread_cond_timedwait(&cv, &mu, &dl) != 0) break;
    pthread_mutex_unlock(&mu);
}

void levels_dos(int variant, int song, float *db_on, float *db_off) {
    int style = variant == DOS_TANDY ? LS_TANDY : LS_SPEAKER;
    int bus = (song <= 5 || song == 40 || song == 49) ? LB_MUSIC : LB_EFFECTS;
    pthread_mutex_lock(&mu);
    int have = song >= 0 && song < DOS_SONGS && dg[variant].state[song] == 2;
    *db_on = have ? dg[variant].on[song] : (float)level_style_gain(style, bus);
    *db_off = have ? dg[variant].off[song] : (float)level_style_gain(style, bus);
    pthread_mutex_unlock(&mu);
}
