/* dosint.c - game integration of the optional DOS audio (spec chapter 12 sections 1, 5, 6.5, 7.6) */
#include "dosint.h"
#include "audio.h"
#include "audio_hw.h"
#include "dosaudio.h"
#include "levels.h"
#include "plat.h"
#include "res.h"
#include "util.h"
#include <ctype.h>
#include <math.h>
#include <strings.h>
#include <unistd.h>

extern const char *g_opt_dos_game, *g_opt_dos_audio, *g_opt_music_style, *g_opt_sfx_style;
extern int g_opt_vol[3], g_opt_even;      /* --music-volume / --effects-volume / --speech-volume / --even-out (-1 = not given) */

#define MAXBUS 4
#define MAXSCHED 64
#define MAXFLAG 16

enum { OP_PLAY, OP_FORCE, OP_STOP, OP_STOP_IF, OP_LOOP };
enum { KD_NONE, KD_MUSIC, KD_SFX };
enum { MODE_NONE, MODE_ONCE, MODE_BG, MODE_INTRO };

typedef struct Bus {
    int used, is_test, role_music, role_sfx;
    int pausable;                    /* music / shared driver: pauses with dialogs, menus, inactive window (spec 12 section 5.6) */
    int64_t ptotal;                  /* total time this driver has spent paused (cs) */
    DosDrv d;
    DosSynth sy;
    int variant;
    int64_t k, ovs, nextb;
    int cur_kind, cur_id, cur_stopyes;
    int64_t cur_dead;                 /* waits give up here (game time minus pauses, cs): the song's length plus a margin */
    int len[DOS_SONGS];               /* song lengths in ticks (dry run) */
    int mslot_song, mslot_text, mslot_mode;
    float g_on, g_off, g_cur;          /* gain of the song being played in dB (Even out on / off) and the ramped linear gain */
    int g_bus, g_init;
} Bus;

typedef struct Sched {
    int used;
    Bus *b;
    int64_t due;
    int op, song, period, remaining, while_key, tag, owner;
} Sched;

static struct {
    char save_dir[1024];
    AudioSettings want;                 /* stored/requested */
    int music_style, sfx_style;         /* effective */
    int status;
    char reason[500];
    DosGame game;
    int game_loaded;
    int shared;
    Bus bus[MAXBUS];
    Bus *bus_music, *bus_sfx;
    Sched sched[MAXSCHED];
    int flags[MAXFLAG];
    int action_loop;
    int paused, pause_depth;
    int64_t pause_at;
    int realtime, rate;
    void (*lock)(void), (*unlock)(void);
    int keep_sfx;
    int tl;                             /* the running DOS ending timeline (TL_*, spec 12 section 5.5), 0 = none */
    int tl_final;                       /* its last song: the timeline is finished when its last request is made and this song is no longer current */
    char installed_dos[1024];           /* <base>/dos when installed (spec 14): used when DosGameDir is empty */
    int inited;
    /* test sample */
    struct { int phase; int64_t t0; int msty, ssty; DosGame game; int loaded; Bus *bm, *bs; int win_sfx_end; int64_t win_end; } T;
} D;

#define LOCK() do { if (D.lock) D.lock(); } while (0)
#define UNLOCK() do { if (D.unlock) D.unlock(); } while (0)

/* ---------------------------------------------------------------- INI (spec 6.5) */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}
static int style_of(const char *v) {
    if (!v) return AS_WINDOWS;
    if (!strcasecmp(v, "tandy")) return AS_TANDY;
    if (!strcasecmp(v, "speaker")) return AS_SPEAKER;
    return AS_WINDOWS;
}
static const char *style_name(int s) { return s == AS_TANDY ? "tandy" : s == AS_SPEAKER ? "speaker" : "windows"; }

static void ini_path_in(const char *dir, char *out, size_t n) { snprintf(out, n, "%s/outnumbered.ini", dir && dir[0] ? dir : "."); }
static void ini_path(char *out, size_t n) { ini_path_in(D.save_dir, out, n); }

static void ini_load_from(const char *dir, AudioSettings *s) {
    s->music = s->sfx = AS_WINDOWS;
    s->dir[0] = 0;
    s->soundfont[0] = 0;
    s->shared = 1;
    s->vol[0] = s->vol[1] = s->vol[2] = 100;
    s->even = 1;
    char p[1200];
    ini_path_in(dir, p, sizeof p);
    size_t n;
    uint8_t *b = file_read_all(p, &n);
    if (!b) return;
    char *txt = xmalloc(n + 1);
    memcpy(txt, b, n);
    txt[n] = 0;
    free(b);
    int in_audio = 0;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *c = strpbrk(line, ";#");
        if (c) *c = 0;
        line = trim(line);
        if (!*line) continue;
        if (*line == '[') {
            char *e = strchr(line, ']');
            if (e) { *e = 0; in_audio = !strcasecmp(trim(line + 1), "Audio"); }
            continue;
        }
        if (!in_audio) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(line), *val = trim(eq + 1);
        if (!strcasecmp(key, "MusicStyle")) s->music = style_of(val);
        else if (!strcasecmp(key, "SfxStyle")) s->sfx = style_of(val);
        else if (!strcasecmp(key, "DosGameDir")) snprintf(s->dir, sizeof s->dir, "%s", val);
        else if (!strcasecmp(key, "MusicVolume") || !strcasecmp(key, "EffectsVolume") || !strcasecmp(key, "SpeechVolume")) {
            char *end;
            long v = strtol(val, &end, 10);
            if (*val && !*end) { if (v < 0) v = 0; if (v > 100) v = 100; s->vol[!strcasecmp(key, "MusicVolume") ? 0 : !strcasecmp(key, "EffectsVolume") ? 1 : 2] = (int)v; }
        }
        else if (!strcasecmp(key, "EvenOutVolume")) s->even = !(!strcasecmp(val, "0") || !strcasecmp(val, "no") || !strcasecmp(val, "false"));
        else if (!strcasecmp(key, "SoundFont")) snprintf(s->soundfont, sizeof s->soundfont, "%s", val);
        else if (!strcasecmp(key, "DosSharedVoice")) s->shared = !(!strcasecmp(val, "0") || !strcasecmp(val, "no") || !strcasecmp(val, "false"));
    }
    free(txt);
}

static void ini_load(AudioSettings *s) { ini_load_from(D.save_dir, s); }

/* the settings of the INI in `save_dir` (defaults when missing); changes no state */
void dosint_peek_ini(const char *save_dir, AudioSettings *s) { ini_load_from(save_dir, s); }

typedef struct IniLine { char *text; } IniLine;

static void ini_save(const AudioSettings *s) {
    char p[1200], tmp[1300];
    ini_path(p, sizeof p);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    size_t n = 0;
    uint8_t *b = file_read_all(p, &n);
    char *txt = xmalloc(n + 1);
    if (b) memcpy(txt, b, n);
    txt[n] = 0;
    free(b);
    /* split into lines (keeping them verbatim) */
    char **lines = NULL;
    int nl = 0;
    for (char *q = txt; *q;) {
        char *e = strchr(q, '\n');
        size_t len = e ? (size_t)(e - q) : strlen(q);
        if (len && q[len - 1] == '\r') len--;
        lines = xrealloc(lines, sizeof(char *) * (size_t)(nl + 1));
        lines[nl] = xmalloc(len + 1);
        memcpy(lines[nl], q, len);
        lines[nl][len] = 0;
        nl++;
        q = e ? e + 1 : q + strlen(q);
    }
    struct { const char *key; char val[1100]; int done, optional; } kv[8];
    memset(kv, 0, sizeof kv);
    kv[0].key = "MusicStyle"; snprintf(kv[0].val, sizeof kv[0].val, "%s", style_name(s->music));
    kv[1].key = "SfxStyle"; snprintf(kv[1].val, sizeof kv[1].val, "%s", style_name(s->sfx));
    kv[2].key = "DosGameDir"; snprintf(kv[2].val, sizeof kv[2].val, "%s", s->dir);
    kv[3].key = "DosSharedVoice"; snprintf(kv[3].val, sizeof kv[3].val, "%d", s->shared ? 1 : 0); kv[3].optional = 1;
    kv[4].key = "MusicVolume"; snprintf(kv[4].val, sizeof kv[4].val, "%d", s->vol[0]);
    kv[5].key = "EffectsVolume"; snprintf(kv[5].val, sizeof kv[5].val, "%d", s->vol[1]);
    kv[6].key = "SpeechVolume"; snprintf(kv[6].val, sizeof kv[6].val, "%d", s->vol[2]);
    kv[7].key = "EvenOutVolume"; snprintf(kv[7].val, sizeof kv[7].val, "%d", s->even ? 1 : 0);
    int in_audio = 0, audio_seen = 0, audio_end = -1;
    for (int i = 0; i < nl; i++) {
        char copy[1200];
        snprintf(copy, sizeof copy, "%s", lines[i]);
        char *t = trim(copy);
        if (*t == '[') {
            if (in_audio) audio_end = i;
            char *e = strchr(t, ']');
            in_audio = 0;
            if (e) { *e = 0; if (!strcasecmp(trim(t + 1), "Audio")) { in_audio = 1; audio_seen = 1; } }
            continue;
        }
        if (!in_audio) continue;
        char *cm = strpbrk(copy, ";#");
        char comment[300] = "";
        if (cm) { snprintf(comment, sizeof comment, "%s", cm); *cm = 0; }
        char *eq = strchr(copy, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(copy);
        for (int k = 0; k < 8; k++) {
            if (strcasecmp(key, kv[k].key)) continue;
            char nb[1500];
            if (comment[0]) snprintf(nb, sizeof nb, "%s=%s        %s", kv[k].key, kv[k].val, comment);
            else snprintf(nb, sizeof nb, "%s=%s", kv[k].key, kv[k].val);
            free(lines[i]);
            lines[i] = xstrdup(nb);
            kv[k].done = 1;
        }
    }
    if (in_audio) audio_end = nl;
    FILE *f = fopen(tmp, "w");
    if (!f) { for (int i = 0; i < nl; i++) free(lines[i]); free(lines); free(txt); return; }
    if (!audio_seen) {
        for (int i = 0; i < nl; i++) fprintf(f, "%s\n", lines[i]);
        if (nl && lines[nl - 1][0]) fprintf(f, "\n");
        fprintf(f, "[Audio]\n");
        for (int k = 0; k < 8; k++) if (!kv[k].optional || s->shared == 0) fprintf(f, "%s=%s\n", kv[k].key, kv[k].val);
    } else {
        /* insert missing keys after the last non-blank line of the section */
        int ins = audio_end;
        while (ins > 0 && !lines[ins - 1][0]) ins--;
        for (int i = 0; i < nl; i++) {
            if (i == ins) for (int k = 0; k < 8; k++) if (!kv[k].done && (!kv[k].optional || s->shared == 0)) fprintf(f, "%s=%s\n", kv[k].key, kv[k].val);
            fprintf(f, "%s\n", lines[i]);
        }
        if (ins >= nl) for (int k = 0; k < 8; k++) if (!kv[k].done && (!kv[k].optional || s->shared == 0)) fprintf(f, "%s=%s\n", kv[k].key, kv[k].val);
    }
    fclose(f);
    rename(tmp, p);
    for (int i = 0; i < nl; i++) free(lines[i]);
    free(lines);
    free(txt);
}

/* ---------------------------------------------------------------- bus basics */
static int64_t now_cs(void) { return plat_now(); }

static int song_len(const DosGame *g, int variant, int song) {
    DosDrv t;
    memset(&t, 0, sizeof t);
    dosdrv_init(&t, g, variant);
    dosdrv_play(&t, song);
    int k = 0;
    while (k < 100000) { dosdrv_tick(&t); k++; if (t.state == DOS_IDLE) break; }
    return k;
}

/* ticks whose start is before virtual time t (centiseconds): tick k starts at k*504100/1193182 cs */
static int64_t ticks_before(int64_t t) { return t <= 0 ? 0 : (t * 1193182 + 504099) / 504100; }

static void latch(Bus *b) { dossynth_latch(&b->sy, &b->d.chip); }

static void log_req(Bus *b, int64_t t_cs, const char *verb, int val, const char *res) {
    if (b->is_test) return;
    char line[160];
    if (!D.shared && (b->role_music || b->role_sfx))
        snprintf(line, sizeof line, "DOS %s %d %s %s", verb, val, res, b->role_music && !b->role_sfx ? "music" : "sfx");
    else snprintf(line, sizeof line, "DOS %s %d %s", verb, val, res);
    evlog_raw(t_cs, line);
}

/* the bus gain is that of the song it plays; it switches when a song is accepted, with a short ramp (spec 15 section 5) */
static void set_song_gain(Bus *b, int song) {
    levels_dos(b->variant, song, &b->g_on, &b->g_off);
    b->g_bus = (song <= 5 || song == 40 || song == 49) ? LB_MUSIC : LB_EFFECTS;
    if (!b->g_init) { b->g_cur = (float)(level_db_to_lin(lv_even ? b->g_on : b->g_off) * levels_slider_lin(b->g_bus)); b->g_init = 1; }
}

static int req(Bus *b, int op, int song, int kind, int id, int stopyes, int64_t t_cs) {
    int r = DOS_ACCEPTED;
    switch (op) {
    case OP_PLAY:
        r = dosdrv_play(&b->d, song);
        log_req(b, t_cs, "PLAY", song, r == DOS_ACCEPTED ? "accepted" : "ignored");
        break;
    case OP_FORCE:
        r = dosdrv_force(&b->d, song);
        log_req(b, t_cs, "FORCE", song, r == DOS_ACCEPTED ? "accepted" : "ignored");
        break;
    case OP_STOP: {
        int prev = b->d.current;
        dosdrv_stop(&b->d);
        log_req(b, t_cs, "STOP", prev, "accepted");
        break;
    }
    case OP_LOOP:
        dosdrv_set_loop(&b->d, song);
        log_req(b, t_cs, "LOOP", song, "accepted");
        break;
    }
    if ((op == OP_PLAY || op == OP_FORCE) && r == DOS_ACCEPTED) {
        b->cur_kind = kind; b->cur_id = id; b->cur_stopyes = stopyes;
        set_song_gain(b, song);
        /* a wait for this song ends at the latest after its length in game time, whatever the driver does (spec 5.2) */
        b->cur_dead = b->d.loop_flag ? INT64_MAX / 2 : t_cs + ((int64_t)b->len[song] * 504100 + 1193181) / 1193182;
    }
    latch(b);
    return r;
}

static int flag_get(int key) { for (int i = 0; i < MAXFLAG; i++) if (D.flags[i] == key) return 1; return 0; }
void dosint_flag(int key, int on) {
    LOCK();
    for (int i = 0; i < MAXFLAG; i++) if (D.flags[i] == key) { if (!on) D.flags[i] = 0; UNLOCK(); return; }
    if (on) for (int i = 0; i < MAXFLAG; i++) if (!D.flags[i]) { D.flags[i] = key; break; }
    UNLOCK();
}

static int g_owner;      /* Windows id of the request being translated */
static void sched_add(Bus *b, int64_t due, int op, int song, int period, int remaining, int while_key, int tag) {
    for (int i = 0; i < MAXSCHED; i++)
        if (!D.sched[i].used) { D.sched[i] = (Sched){1, b, due, op, song, period, remaining, while_key, tag, g_owner}; return; }
}
static void sched_cancel_tag(int tag) { for (int i = 0; i < MAXSCHED; i++) if (D.sched[i].used && D.sched[i].tag == tag) D.sched[i].used = 0; }

static void bus_tick(Bus *b) {
    int64_t t_cs = b->k * 504100 / 1193182 + b->ptotal;           /* game time: driver time plus the time this driver spent paused */
    for (int i = 0; i < MAXSCHED; i++) {
        Sched *e = &D.sched[i];
        if (!e->used || e->b != b || e->due > b->k) continue;
        if (e->while_key && !flag_get(e->while_key)) { e->used = 0; continue; }
        if (e->op == OP_STOP_IF) { if (b->d.current == e->song) req(b, OP_STOP, 0, 0, 0, 0, t_cs); e->used = 0; continue; }
        req(b, e->op, e->song, KD_SFX, e->owner, 0, t_cs);
        if (e->period > 0 && e->remaining != 0) { if (e->remaining > 0) e->remaining--; e->due += e->period; }
        else e->used = 0;
    }
    if (b->mslot_song >= 0 && !b->is_test) {
        if (b->mslot_mode == MODE_INTRO && b->d.current < 0) req(b, OP_PLAY, b->mslot_song, KD_MUSIC, 0, 0, t_cs);
        else if (b->mslot_mode == MODE_BG && D.action_loop && b->k % 24 == 0) req(b, OP_PLAY, b->mslot_song, KD_MUSIC, 0, 0, t_cs);
    }
    dosdrv_tick(&b->d);
    latch(b);
    b->k++;
}

static int bus_paused(const Bus *b) { return b->pausable && D.paused; }

/* run the due ticks of all drivers in game-time order, so that log lines never go backwards */
static void sync_virtual(void) {
    if (D.realtime) return;
    for (;;) {
        Bus *best = NULL;
        int64_t best_t = 0;
        for (int i = 0; i < MAXBUS; i++) {
            Bus *b = &D.bus[i];
            if (!b->used) continue;
            int64_t game_now = bus_paused(b) ? D.pause_at : now_cs();
            if (b->k >= ticks_before(game_now - b->ptotal)) continue;
            int64_t t = b->k * 504100 / 1193182 + b->ptotal;
            if (!best || t < best_t) { best = b; best_t = t; }
        }
        if (!best) break;
        bus_tick(best);
    }
}

/* ---------------------------------------------------------------- set-up */
static void free_buses(void) {
    memset(D.bus, 0, sizeof D.bus);
    memset(D.sched, 0, sizeof D.sched);
    memset(D.flags, 0, sizeof D.flags);
    D.bus_music = D.bus_sfx = NULL;
}

static Bus *new_bus(const DosGame *g, int variant, int music, int sfx, int test) {
    for (int i = 0; i < MAXBUS; i++) {
        Bus *b = &D.bus[i];
        if (b->used) continue;
        memset(b, 0, sizeof *b);
        b->used = 1;
        b->is_test = test;
        b->role_music = music;
        b->role_sfx = sfx;
        b->variant = variant;
        b->pausable = music && !test;
        b->mslot_song = -1;
        dosdrv_init(&b->d, g, variant);
        for (int s = 0; s < DOS_SONGS; s++) b->len[s] = song_len(g, variant, s);
        dossynth_init(&b->sy, variant, D.rate, 1);
        levels_dos_measure(g, variant);
        latch(b);
        if (!D.realtime) {
            b->k = ticks_before(now_cs());
        }
        b->nextb = dos_tick_ovs(b->k, D.rate);
        return b;
    }
    return NULL;
}

int dosint_check_dir(const char *dir, char *reason, size_t n, char *inner, size_t in_n) {
    if (inner && in_n) inner[0] = 0;
    if (!dir || !*dir) { snprintf(reason, n, "No DOS game folder set."); return DS_UNSET; }
    DosGame g;
    int rc = dos_game_load(dir, &g, reason, n);
    if (rc == 0) { if (inner && in_n) snprintf(inner, in_n, "%s", g.inner); dos_game_free(&g); }
    return rc == 0 ? DS_OK : rc == 1 ? DS_MISSING : DS_INVALID;
}

void dosint_set_installed_dos(const char *dir) { snprintf(D.installed_dos, sizeof D.installed_dos, "%s", dir ? dir : ""); }
int dosint_dos_installed(char *buf, size_t n) { if (buf && n) snprintf(buf, n, "%s", D.installed_dos); return D.installed_dos[0] != 0; }

static void rebuild(void) {
    free_buses();
    if (D.game_loaded) { dos_game_free(&D.game); D.game_loaded = 0; }
    D.status = DS_UNSET;
    D.reason[0] = 0;
    D.music_style = D.sfx_style = AS_WINDOWS;
    D.shared = 0;
    const char *eff = D.want.dir[0] ? D.want.dir : D.installed_dos;     /* --dos-game / INI, else the installed copy */
    if (eff[0]) {
        char err[500] = "";
        int rc = dos_game_load(eff, &D.game, err, sizeof err);
        if (rc == 0) { D.game_loaded = 1; D.status = DS_OK; }
        else { D.status = rc == 1 ? DS_MISSING : DS_INVALID; snprintf(D.reason, sizeof D.reason, "%s", err); }
    } else snprintf(D.reason, sizeof D.reason, "No DOS game folder set.");
    int wantdos = D.want.music != AS_WINDOWS || D.want.sfx != AS_WINDOWS;
    if (wantdos && D.status != DS_OK) {
        fprintf(stderr, "DOS audio unavailable: %s\n", D.reason);
        return;
    }
    if (!wantdos) return;
    D.music_style = D.want.music;
    D.sfx_style = D.want.sfx;
    D.shared = D.want.shared && D.music_style == D.sfx_style && D.music_style != AS_WINDOWS;
    if (D.shared) {
        D.bus_music = D.bus_sfx = new_bus(&D.game, D.music_style == AS_TANDY ? DOS_TANDY : DOS_SPEAKER, 1, 1, 0);
    } else {
        if (D.music_style != AS_WINDOWS) D.bus_music = new_bus(&D.game, D.music_style == AS_TANDY ? DOS_TANDY : DOS_SPEAKER, 1, 0, 0);
        if (D.sfx_style != AS_WINDOWS) D.bus_sfx = new_bus(&D.game, D.sfx_style == AS_TANDY ? DOS_TANDY : DOS_SPEAKER, 0, 1, 0);
    }
}

void dosint_set_save_dir(const char *dir) { snprintf(D.save_dir, sizeof D.save_dir, "%s", dir ? dir : ""); }

static void log_pre(void) {
    if (D.realtime || !D.inited) return;
    LOCK();
    sync_virtual();
    UNLOCK();
}

void dosint_init(const char *save_dir) {
    g_evlog_pre = log_pre;
    LOCK();
    if (!D.inited) D.rate = 44100;
    D.inited = 1;
    dosint_set_save_dir(save_dir);
    ini_load(&D.want);
    if (g_opt_dos_audio) D.want.music = D.want.sfx = style_of(g_opt_dos_audio);
    if (g_opt_music_style) D.want.music = style_of(g_opt_music_style);
    if (g_opt_sfx_style) D.want.sfx = style_of(g_opt_sfx_style);
    if (g_opt_dos_game) snprintf(D.want.dir, sizeof D.want.dir, "%s", g_opt_dos_game);
    for (int i = 0; i < 3; i++) if (g_opt_vol[i] >= 0) D.want.vol[i] = g_opt_vol[i] > 100 ? 100 : g_opt_vol[i];
    if (g_opt_even >= 0) D.want.even = g_opt_even != 0;
    levels_set(D.want.vol[0], D.want.vol[1], D.want.vol[2], D.want.even);
    D.paused = 0; D.pause_depth = 0;
    D.action_loop = 0;
    rebuild();
    UNLOCK();
}

void dosint_get_settings(AudioSettings *s) { *s = D.want; }

void dosint_reload_dos(const char *installed_dir) {
    LOCK();
    sync_virtual();
    dosint_set_installed_dos(installed_dir);
    rebuild();
    UNLOCK();
}
int dosint_status(char *reason, size_t n) { if (reason && n) snprintf(reason, n, "%s", D.reason); return D.status; }
int dosint_music_dos(void) { return D.bus_music != NULL; }
int dosint_sfx_dos(void) { return D.bus_sfx != NULL; }
int dosint_shared(void) { return D.shared; }

void dosint_apply(const AudioSettings *s, int write_ini) {
    /* styles, folder or voice mode unchanged: only the volumes / the INI change, the music keeps playing */
    int same = s->music == D.want.music && s->sfx == D.want.sfx && s->shared == D.want.shared && !strcmp(s->dir, D.want.dir);
    if (!same) audio_restyle_begin();
    LOCK();
    sync_virtual();
    D.want = *s;
    levels_set(s->vol[0], s->vol[1], s->vol[2], s->even);
    if (write_ini) ini_save(&D.want);
    if (!same) rebuild();
    UNLOCK();
    if (!same) audio_restyle_end();
}

/* live changes while Audio Options is open (spec 15 section 6.2): no INI write */
void dosint_set_levels_live(const int vol[3], int even) { levels_set(vol[0], vol[1], vol[2], even); }

void dosint_set_realtime(int rate, void (*lock)(void), void (*unlock)(void)) {
    D.realtime = 1;
    D.rate = rate;
    D.lock = lock;
    D.unlock = unlock;
}

/* ---------------------------------------------------------------- realtime render */
void dosint_render(float *acc, int n) {
    for (int bi = 0; bi < MAXBUS; bi++) {
        Bus *b = &D.bus[bi];
        if (!b->used || bus_paused(b)) continue;
        for (int i = 0; i < n; i++) {
            for (int q = 0; q < DOS_OVS; q++) {
                while (b->ovs >= b->nextb) { bus_tick(b); b->nextb = dos_tick_ovs(b->k, D.rate); }
                dossynth_step(&b->sy);
                b->ovs++;
            }
            float tgt = (float)(level_db_to_lin(lv_even ? b->g_on : b->g_off) * levels_slider_lin(b->g_bus));
            float step = fmaxf(1.0f, tgt) / 220.0f, dd = tgt - b->g_cur;
            if (dd > step) dd = step; else if (dd < -step) dd = -step;
            b->g_cur += dd;
            acc[i] += level_softclip(dossynth_out(&b->sy) * b->g_cur, 0.6f, 0.95f);
        }
    }
}

/* ---------------------------------------------------------------- music slots */
static int slot_song(int text_id) {
    switch (text_id) {
    case 1724: return 0; case 1728: return 1; case 1729: return 2; case 1745: return 4; case 1746: return 5;
    case 1717: return 3; case 1735: return 49; case 1757: return 40;
    }
    return -1;
}
static int is_music_song(int s) { return (s >= 0 && s <= 5) || s == 40 || s == 49; }

void dosint_music_play(int text_id, int loop) {
    {   /* a song that is not measured yet is measured now (at most 1.5 s), before it starts (spec 15 section 4.2) */
        int sg = slot_song(text_id);
        if (D.bus_music && sg >= 0) levels_dos_ensure(&D.game, D.bus_music->variant, sg);
    }
    LOCK();
    Bus *b = D.bus_music;
    int song = slot_song(text_id);
    if (!b || song < 0) { UNLOCK(); return; }
    sync_virtual();
    int64_t t = now_cs();
    b->mslot_song = song;
    b->mslot_text = text_id;
    b->mslot_mode = !loop ? MODE_ONCE : text_id == 1724 ? MODE_INTRO : MODE_BG;
    if (b->mslot_mode == MODE_ONCE) req(b, OP_FORCE, song, KD_MUSIC, 0, 0, t);
    else req(b, OP_PLAY, song, KD_MUSIC, 0, 0, t);
    UNLOCK();
}

void dosint_music_stop(void) {
    LOCK();
    Bus *b = D.bus_music;
    if (b) {
        sync_virtual();
        b->mslot_song = -1;
        b->mslot_mode = MODE_NONE;
        if (is_music_song(b->d.current)) req(b, OP_STOP, 0, 0, 0, 0, now_cs());
    }
    UNLOCK();
}

int dosint_music_playing(void) {
    LOCK();
    Bus *b = D.bus_music;
    int r = 0;
    if (b) {
        sync_virtual();
        if (b->mslot_song >= 0) r = b->mslot_mode != MODE_ONCE ? 1 : (b->d.current == b->mslot_song && b->cur_kind == KD_MUSIC && now_cs() <= b->cur_dead);
    }
    UNLOCK();
    return r;
}

void dosint_action_loop(int on) {
    LOCK();
    sync_virtual();
    D.action_loop = on;
    UNLOCK();
}

void dosint_pause(int on) {      /* nests: dialog, menu, inactive window (never the game-clock pause) */
    LOCK();
    sync_virtual();
    if (on) { if (D.pause_depth++ == 0) { D.paused = 1; D.pause_at = now_cs(); } }
    else if (D.pause_depth > 0 && --D.pause_depth == 0) {
        for (int i = 0; i < MAXBUS; i++) if (D.bus[i].used && D.bus[i].pausable) D.bus[i].ptotal += now_cs() - D.pause_at;
        D.paused = 0;
    }
    UNLOCK();
}
void dosint_sfx_keep(int on) { D.keep_sfx = on; }

int dosint_current_song(void) {
    Bus *b = D.bus_music ? D.bus_music : D.bus_sfx;
    if (!b) return -1;
    LOCK();
    sync_virtual();
    int c = b->d.current;
    UNLOCK();
    return c;
}

/* ---------------------------------------------------------------- sound effects (spec 5.3) */
/* the DOS song a Windows effect id starts first (-1 = none), for the on-demand measurement (spec 5.3) */
static int sfx_song(int id, int ctx, int flags) {
    (void)flags;
    switch (id) {
    case 1700: return 10; case 1727: return ctx == SC_SCORE ? 14 : 11; case 1706: return 12; case 1750: return 13;
    case 1701: case 1736: return 27; case 1702: return 26; case 1703: case 1747: return 37; case 1704: return 51; case 1705: return 24;
    case 1707: return 35; case 1708: return 36; case 1709: return 48; case 1711: return 9; case 1712: return 25;
    case 1718: return 34; case 1720: case 1723: return 20; case 1722: return 33; case 1726: return 38; case 1730: return 29; case 1731: return 18;
    case 1732: return 19; case 1734: return 28; case 1737: return 45; case 1739: return ctx == SC_SCORE ? 7 : -1; case 1753: return 50; case 1740: case 1752: return 23;
    case 1742: return 31; case 1743: return 7; case 1744: return 6; case 1748: return 46; case 1749: return 47; case 1754: return 17; case 1755: return 26;
    case 1758: return 32; case 1759: return 39; case 1760: return ctx == SC_WRONGDEC ? -1 : 9; case 1761: return 8; case 1763: return 42;
    case 1764: return ctx == SC_AWARD ? -1 : ctx == SC_SCORE ? 16 : 14; case 1765: return 15;
    }
    return -1;
}

#define STOPYES 1
int dosint_sfx(int id, int flags, int ctx) {
    Bus *b = D.bus_sfx;
    if (!b) return 0;
    switch (id) {
    case 1741: case 1766: case 1767: return 0;       /* keep the Windows WAVE */
    }
    { int sg = sfx_song(id, ctx, flags); if (sg >= 0) levels_dos_ensure(&D.game, b->variant, sg); }
    LOCK();
    sync_virtual();
    int64_t t = now_cs();
    int handled = 1;
    g_owner = id;
#define PLAY(s) req(b, OP_PLAY, (s), KD_SFX, id, 0, t)
#define PLAYS(s) req(b, OP_PLAY, (s), KD_SFX, id, STOPYES, t)
#define FORCE(s) req(b, OP_FORCE, (s), KD_SFX, id, 0, t)
#define STOPLATER(song, ticks) sched_add(b, b->k + (ticks), OP_STOP_IF, (song), 0, 0, 0, 0)
    if (id == 1705 || id == 1711 || id == 1709) {            /* an earlier drill TV static is cut by these */
        if (b->d.current == 23) req(b, OP_STOP, 0, 0, 0, 0, t);
        sched_cancel_tag(1752);
    }
    switch (id) {
    case 1700: case 1727: case 1706: case 1750: {
        int song = id == 1700 ? 10 : id == 1727 ? 11 : id == 1706 ? 12 : 13;
        if (id == 1727 && ctx == SC_INTRO) { req(b, OP_FORCE, 11, KD_SFX, id, STOPYES, t); break; }
        if (id == 1727 && ctx == SC_SCORE) { FORCE(14); break; }                      /* the score-screen Champion extra */
        if (flags & SND_LOOP) {
            dosint_flag(id, 1);
            PLAY(song);
            sched_add(b, b->k + 72, OP_PLAY, song, 72, -1, id, 0);
        } else PLAY(song);
        break;
    }
    case 1701: PLAY(27); break;
    case 1702: if (ctx == SC_ENDING) FORCE(26); else PLAY(26); break;
    case 1703: case 1747: PLAY(37); break;
    case 1704: PLAY(51); break;
    case 1705: PLAY(24); break;
    case 1707: PLAYS(35); break;
    case 1708: PLAYS(36); break;
    case 1709: PLAY(48); break;
    case 1710: case 1713: case 1719: case 1721: case 1725: case 1733: case 1756: break;   /* DOS is silent here */
    case 1711: PLAY(9); break;
    case 1712:
        req(b, OP_LOOP, 1, 0, 0, 0, t);
        PLAYS(25);
        break;
    case 1714: break;                                        /* the doors are silent in DOS; the ride has one sound, the ride song (dosint_elevator_ride) */
    case 1718: PLAY(34); break;
    case 1720: case 1723: PLAY(20); break;
    case 1722: PLAY(33); break;
    case 1726: PLAY(38); break;
    case 1730: PLAY(29); break;
    case 1731: PLAY(18); break;
    case 1732: PLAY(19); break;
    case 1734: PLAY(28); break;
    case 1736: PLAY(27); break;                              /* Volume display: the slider sound, once per moving slider */
    case 1751:                                               /* Spinner: only the first tick of a spin sounds */
        if (!flag_get(DF_SPIN_PLAYED)) { dosint_flag(DF_SPIN_PLAYED, 1); PLAY(30); }
        break;
    case 1737: FORCE(45); break;
    case 1738: break;                                       /* MoM zapped: the award and Champion endings play the DOS timelines (12 section 5.5) */
    case 1739: if (ctx == SC_SCORE) FORCE(7); break;        /* score-screen Champion extra; the endings' own cues are ignored */
    case 1740: PLAY(23); break;
    case 1752: {
        int pending = 0;                         /* the 160-tick cut-off counts from the first request of the static, even an ignored one (12 section 5.3) */
        for (int i = 0; i < MAXSCHED; i++) if (D.sched[i].used && D.sched[i].tag == 1752) pending = 1;
        if (!pending) sched_add(b, b->k + 160, OP_STOP_IF, 23, 0, 0, 0, 1752);
        PLAY(23);
        break;
    }
    case 1742: PLAY(31); break;
    case 1743: PLAY(7); break;
    case 1744: PLAY(6); break;
    case 1748: case 1749:
        PLAY(id == 1748 ? 46 : 47);
        sched_add(b, b->k + 5, OP_PLAY, id == 1748 ? 46 : 47, 5, 57, 0, 0);
        break;
    case 1753: PLAY(50); break;
    case 1754: PLAY(17); break;
    case 1755: PLAYS(26); break;
    case 1758: PLAY(32); break;
    case 1759:
        dosint_flag(1759, 1);
        FORCE(39);
        sched_add(b, b->k + 30, OP_FORCE, 39, 30, 18, 1759, 0);
        break;
    case 1760: if (ctx != SC_WRONGDEC) PLAY(9); break;       /* lose ending: ignored, the DOS lose timeline plays instead */
    case 1761: PLAY(8); break;
    case 1762: break;                                        /* lose ending lightning: part of the DOS lose timeline */
    case 1763:
        FORCE(42);
        dosint_flag(DF_TELLY_FLASH, 1);
        sched_add(b, b->k + 24, OP_PLAY, 17, 24, -1, DF_TELLY_FLASH, 0);
        break;
    case 1764:
        if (ctx == SC_AWARD) break;                          /* song 16 comes from the DOS award timeline */
        else if (ctx == SC_SCORE) PLAY(16);
        else if (PLAY(14) == DOS_ACCEPTED) STOPLATER(14, 60);
        break;
    case 1765: if (PLAY(15) == DOS_ACCEPTED) STOPLATER(15, 60); break;
    case 1768: case 1769: break;                             /* Champion ending: part of the DOS Champion timeline */
    default: handled = 0; break;
    }
#undef PLAY
#undef PLAYS
#undef FORCE
#undef STOPLATER
    UNLOCK();
    return handled;
}

void dosint_sfx_stop(int id) {
    Bus *b = D.bus_sfx;
    if (!b) return;
    LOCK();
    sync_virtual();
    dosint_flag(id, 0);
    if (id == 1712 && b->cur_kind == KD_SFX && b->cur_id == 1712) {
        req(b, OP_STOP, 0, 0, 0, 0, now_cs());
        req(b, OP_LOOP, 0, 0, 0, 0, now_cs());
    } else if (b->cur_kind == KD_SFX && b->cur_id == id && b->cur_stopyes && b->d.state != DOS_IDLE) req(b, OP_STOP, 0, 0, 0, 0, now_cs());
    UNLOCK();
}

/* the elevator ride song (spec 12 section 5.6a): a plain play, requested once when the departure doors have finished closing.
 * 22 going up (departure area 0 or 4), 21 going down (5 or 9). It runs to its natural end. */
void dosint_elevator_ride(int up) {
    Bus *b = D.bus_sfx;
    if (!b) return;
    int song = up ? 22 : 21;
    levels_dos_ensure(&D.game, b->variant, song);
    LOCK();
    sync_virtual();
    g_owner = 1714;
    req(b, OP_PLAY, song, KD_SFX, 1714, 0, now_cs());
    UNLOCK();
}

/* stop the effect driver's sound; keep_ride leaves the elevator ride song playing (the arrival's "stop all sounds", 12 section 5.6) */
static void stop_effects(int keep_ride) {
    Bus *b = D.bus_sfx;
    if (!b) return;
    LOCK();
    sync_virtual();
    int64_t t = now_cs();
    int ride = keep_ride && b->cur_kind == KD_SFX && b->cur_id == 1714;
    if (!ride && b->d.state != DOS_IDLE && (b->cur_kind == KD_SFX || !b->role_music)) req(b, OP_STOP, 0, 0, 0, 0, t);
    if (b->d.loop_flag) req(b, OP_LOOP, 0, 0, 0, 0, t);
    for (int i = 0; i < MAXFLAG; i++) D.flags[i] = 0;
    for (int i = 0; i < MAXSCHED; i++) if (D.sched[i].used && D.sched[i].b == b) D.sched[i].used = 0;
    D.tl = 0;
    UNLOCK();
}

void dosint_stop_effects(void) { stop_effects(0); }
void dosint_stop_effects_keep_ride(void) { stop_effects(1); }

/* called when Sound Effects is switched off */
void dosint_effects_off(void) { if (!D.keep_sfx) dosint_stop_effects(); }

int dosint_sfx_playing(int id) {
    Bus *b = D.bus_sfx;
    if (!b) return 0;
    LOCK();
    sync_virtual();
    int r = b->cur_kind == KD_SFX && b->cur_id == id && (b->d.current >= 0 || b->d.state != DOS_IDLE) && now_cs() <= b->cur_dead;
    UNLOCK();
    return r;
}

/* ---------------------------------------------------------------- ending timelines (spec 12 section 5.5) */
#define TAG_TL 9001
typedef struct TlEnt { int tick, op, song; } TlEnt;
#define TLF(t, s) {t, OP_FORCE, s}
#define TLP(t, s) {t, OP_PLAY, s}
static const TlEnt TL_AWARD_T[] = {TLF(90, 16), TLF(300, 10), TLF(360, 10), TLF(420, 10), TLF(510, 50), TLF(540, 50), TLF(570, 50)};
static const TlEnt TL_CHAMP_T[] = {TLF(60, 43), TLF(500, 22), TLF(820, 13), TLF(880, 13), TLF(940, 13), TLF(1000, 13), TLF(1060, 13), TLF(1120, 13), TLF(1450, 14), TLF(1750, 48)};
static const TlEnt TL_LOSE_T[] = {TLP(0, 17), TLF(60, 11), TLF(180, 20), TLF(300, 49)};      /* the +330 play(49) is always ignored: omitted */
static const TlEnt TL_MID_T[] = {TLP(0, 49), TLP(100, 49), TLP(200, 49), TLP(300, 49), TLP(400, 49), TLP(500, 49), TLP(600, 49), TLP(700, 49), TLP(800, 49), TLP(900, 49)};

/* Start a DOS ending timeline at the current driver time: the effect (or shared) driver, except the midnight flashes, which are the
 * music slot's MIDNITE and use the music driver. Returns 0 when that driver is not DOS (the Windows cues play as before). */
int dosint_timeline_begin(int kind) {
    const TlEnt *t = NULL;
    int n = 0, fin = 0;
    Bus *b = kind == TL_MIDNIGHT ? D.bus_music : D.bus_sfx;
    if (!b) return 0;
    switch (kind) {
    case TL_AWARD: t = TL_AWARD_T; n = (int)(sizeof TL_AWARD_T / sizeof *TL_AWARD_T); fin = 50; break;
    case TL_CHAMPION: t = TL_CHAMP_T; n = (int)(sizeof TL_CHAMP_T / sizeof *TL_CHAMP_T); fin = 48; break;
    case TL_LOSE: t = TL_LOSE_T; n = (int)(sizeof TL_LOSE_T / sizeof *TL_LOSE_T); fin = 49; break;
    case TL_MIDNIGHT: t = TL_MID_T; n = (int)(sizeof TL_MID_T / sizeof *TL_MID_T); fin = 49; break;
    default: return 0;
    }
    for (int i = 0; i < n; i++) levels_dos_ensure(&D.game, b->variant, t[i].song);
    LOCK();
    sync_virtual();
    D.tl = kind;
    D.tl_final = fin;
    g_owner = 0;
    int64_t now = now_cs();
    for (int i = 0; i < n; i++) {
        if (t[i].tick == 0) req(b, t[i].op, t[i].song, kind == TL_MIDNIGHT ? KD_MUSIC : KD_SFX, 0, 0, now);
        else sched_add(b, b->k + t[i].tick, t[i].op, t[i].song, 0, 0, 0, TAG_TL);
    }
    UNLOCK();
    return 1;
}

/* 1 while the timeline is not finished: its last request is made and its last song is no longer current */
int dosint_timeline_active(void) {
    if (!D.tl) return 0;
    Bus *b = D.tl == TL_MIDNIGHT ? D.bus_music : D.bus_sfx;
    if (!b) { D.tl = 0; return 0; }
    LOCK();
    sync_virtual();
    int active = 0;
    for (int i = 0; i < MAXSCHED; i++) if (D.sched[i].used && D.sched[i].tag == TAG_TL) active = 1;
    if (!active && b->d.current == D.tl_final) active = 1;
    if (!active) D.tl = 0;
    UNLOCK();
    return active;
}

/* the zap that hits a lit Telly: the big zap song after the Windows stop-all (12 section 5.3) */
void dosint_zap_lit_telly(void) {
    Bus *b = D.bus_sfx;
    if (!b) return;
    levels_dos_ensure(&D.game, b->variant, 16);
    LOCK();
    sync_virtual();
    g_owner = 0;
    req(b, OP_FORCE, 16, KD_SFX, 0, 0, now_cs());
    UNLOCK();
}

/* ---------------------------------------------------------------- state */
void dosint_state_json(FILE *f) {
    LOCK();
    sync_virtual();
    const char *st = D.status == DS_OK ? "ok" : D.status == DS_MISSING ? "missing" : D.status == DS_INVALID ? "invalid" : "unset";
    fprintf(f, " \"audio\": {\"music_style\": \"%s\", \"sfx_style\": \"%s\", \"dos_game_dir\": \"", style_name(D.music_style), style_name(D.sfx_style));
    for (const char *p = D.want.dir; *p; p++) { if (*p == '"' || *p == '\\') fputc('\\', f); fputc(*p, f); }
    Bus *b = D.bus_music ? D.bus_music : D.bus_sfx;
    fprintf(f, "\", \"volumes\": {\"music\": %d, \"effects\": %d, \"speech\": %d}, \"even_out\": %s, \"dos_status\": \"%s\", \"dos_installed\": %s, \"shared_voice\": %s, \"current_song\": %d}", lv_vol[0], lv_vol[1], lv_vol[2], lv_even ? "true" : "false", st, D.installed_dos[0] ? "true" : "false", D.shared ? "true" : "false", b ? b->d.current : -1);
    UNLOCK();
}

/* ---------------------------------------------------------------- Audio Options "Test" sample (spec 6.3) */
void dosint_test_stop(void) {
    LOCK();
    if (D.T.bm) D.T.bm->used = 0;
    if (D.T.bs) D.T.bs->used = 0;
    D.T.bm = D.T.bs = NULL;
    if (D.T.loaded) { dos_game_free(&D.T.game); D.T.loaded = 0; }
    D.T.phase = 0;
    UNLOCK();
    audio_test_win_stop();
}

static void test_start_music(int64_t now) {
    if (D.T.msty != AS_WINDOWS && D.T.loaded) {
        Bus *b = new_bus(&D.T.game, D.T.msty == AS_TANDY ? DOS_TANDY : DOS_SPEAKER, 1, 0, 1);
        D.T.bm = b;
        if (b) req(b, OP_PLAY, 1, KD_MUSIC, 0, 0, now);
    } else audio_test_win_music(1728);
    D.T.phase = 3;
    D.T.t0 = now;
}

/* phases: 1 effect, 2 speech 1862 (if Voice is on, at most 4 s), 3 the first 4 s of LEVEL1 (spec 15 section 6.2) */
int dosint_test_running(void) {
    if (!D.T.phase) return 0;
    LOCK();
    sync_virtual();
    int64_t now = now_cs();
    if (D.T.phase == 1) {
        int done = D.T.bs ? D.T.bs->d.state == DOS_IDLE : now >= D.T.win_end;
        if (done) {
            if (D.T.bs) { D.T.bs->used = 0; D.T.bs = NULL; }
            if (audio_voice_on()) {
                int dur = audio_test_win_speech(1862);
                if (dur > 400) dur = 400;
                D.T.win_end = now + dur;
                D.T.phase = 2;
            } else test_start_music(now);
        }
    } else if (D.T.phase == 2) {
        if (now >= D.T.win_end) test_start_music(now);
    } else if (now - D.T.t0 >= 400) {
        UNLOCK();
        dosint_test_stop();
        return 0;
    }
    int r = D.T.phase != 0;
    UNLOCK();
    return r;
}

void dosint_test_start(int music_style, int sfx_style, const char *dir) {
    dosint_test_stop();
    LOCK();
    sync_virtual();
    D.T.msty = music_style;
    D.T.ssty = sfx_style;
    char err[200];
    if ((music_style != AS_WINDOWS || sfx_style != AS_WINDOWS) && dir && dos_game_load(dir, &D.T.game, err, sizeof err) == 0) D.T.loaded = 1;
    if (!D.T.loaded) { D.T.msty = D.T.ssty = AS_WINDOWS; }
    D.T.phase = 1;
    D.T.t0 = now_cs();
    if (D.T.ssty != AS_WINDOWS) {
        Bus *b = new_bus(&D.T.game, D.T.ssty == AS_TANDY ? DOS_TANDY : DOS_SPEAKER, 0, 1, 1);
        D.T.bs = b;
        if (b) req(b, OP_PLAY, 7, KD_SFX, 0, 0, D.T.t0);
    } else D.T.win_end = D.T.t0 + audio_test_win_sfx(1743);
    UNLOCK();
}
