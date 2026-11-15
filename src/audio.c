#include "audio.h"
#include "audio_hw.h"
#include "dosint.h"
#include "levels.h"
#include "plat.h"
#include "res.h"
#include "vfs.h"
#include "util.h"
#include <strings.h>

const AudioHw *g_audio_hw;
static int sw_sfx = 1, sw_music = 1, sw_voice = 1;
static int midi_extended = 1;

typedef struct Chan { int id, prio, loop; int64_t end; } Chan;
static Chan ch[4];
static struct { int id; int64_t end; } sp;
static struct { int id; int loop; int64_t end; int64_t start; int dur; } mu;
static int pause_depth;
static int mu_remember, mu_remember_loop;

void audio_init(const char *dir) {
    (void)dir;
    memset(ch, 0, sizeof ch);
    memset(&sp, 0, sizeof sp);
    memset(&mu, 0, sizeof mu);
}
void audio_set_switches(int sfx, int music, int voice) {
    sw_sfx = sfx; sw_voice = voice;
    if (!sfx) { for (int i = 0; i < 4; i++) ch[i].id = 0; dosint_effects_off(); }
    if (!voice) sp.id = 0;
    if (!music && sw_music) { int keep = mu.id ? mu.id : mu_remember, kl = mu.id ? mu.loop : mu_remember_loop; music_stop(); mu_remember = keep; mu_remember_loop = kl; }
    int was = sw_music;
    sw_music = music;
    if (music && !was && mu_remember) { int id = mu_remember, l = mu_remember_loop; mu_remember = 0; music_play(id, l); }
}
int audio_sfx_on(void) { return sw_sfx; }
int audio_music_on(void) { return sw_music; }
int audio_voice_on(void) { return sw_voice; }

int wave_duration_cs(int id, const char *type) {
    size_t len;
    const uint8_t *d = res_get(type, id, &len);
    if (!d || len < 44) return 0;
    /* find fmt and data chunks */
    int rate = 11025, bits = 8, chans = 1;
    size_t p = 12;
    long datalen = 0;
    while (p + 8 <= len) {
        uint32_t cl = rd32(d + p + 4);
        if (!memcmp(d + p, "fmt ", 4) && p + 24 <= len) {
            chans = rd16(d + p + 10);
            rate = (int)rd32(d + p + 12);
            bits = rd16(d + p + 22);
        } else if (!memcmp(d + p, "data", 4)) {
            datalen = cl;
            if (p + 8 + cl > len) datalen = (long)(len - p - 8);
            break;
        }
        p += 8 + cl + (cl & 1);
    }
    if (rate <= 0 || bits <= 0 || chans <= 0) return 0;
    long samples = datalen / (bits / 8) / chans;
    return (int)((int64_t)samples * 100 / rate);
}

static void expire(void) {
    int64_t now = plat_now();
    for (int i = 0; i < 4; i++) if (ch[i].id && !ch[i].loop && ch[i].end <= now) ch[i].id = 0;
    if (sp.id && sp.end <= now) sp.id = 0;
    if (mu.id && !mu.loop && mu.end <= now) mu.id = 0;
    if (mu.id && !mu.loop && dosint_music_dos() && !dosint_music_playing()) mu.id = 0;
}

static int dos_id(int id) {
    return dosint_sfx_dos() && id >= 1700 && id <= 1769 && id != 1741 && id != 1766 && id != 1767;
}
void snd_play(int id, int prio, int flags) { snd_play_ctx(id, prio, flags, 0); }
static void snd_play_win(int id, int prio, int flags);
void snd_play_ctx(int id, int prio, int flags, int ctx) {
    expire();
    if (!sw_sfx) return;
    snd_play_win(id, prio, flags);
    if (dos_id(id)) dosint_sfx(id, flags, ctx);   /* DOS driver request (also for calls the Windows rules ignore) */
}
static void snd_play_win(int id, int prio, int flags) {
    int dos = dos_id(id);
    for (int i = 0; i < 4; i++)
        if (ch[i].id == id) {
            if (!(flags & SND_RESTART)) return;
            ch[i].id = 0;
        }
    int slot = -1;
    for (int i = 0; i < 4; i++) if (!ch[i].id) { slot = i; break; }
    if (slot < 0) {
        int lo = -1;
        for (int i = 0; i < 4; i++) if (lo < 0 || ch[i].prio < ch[lo].prio) lo = i;
        if (ch[lo].prio < prio) slot = lo;
    }
    if (slot < 0) return;   /* dropped */
    ch[slot].id = id;
    ch[slot].prio = prio;
    ch[slot].loop = (flags & SND_LOOP) != 0;
    ch[slot].end = plat_now() + wave_duration_cs(id, "WAVE");
    evlog("SFX", id, "prio=%d flags=%d", prio, flags);
    if (g_audio_hw && !dos) { size_t l; const uint8_t *w = res_get("WAVE", id, &l); if (w) { float on, off; levels_wave(LB_EFFECTS, id, w, l, &on, &off); g_audio_hw->sfx_start(slot, w, (uint32_t)l, ch[slot].loop, on, off); } }
}
void snd_stop(int id) {
    dosint_sfx_stop(id);
    for (int i = 0; i < 4; i++) if (ch[i].id == id) { ch[i].id = 0; if (g_audio_hw) g_audio_hw->sfx_stop(i); }
}
void snd_stop_all(void) {
    dosint_stop_effects();
    for (int i = 0; i < 4; i++) { ch[i].id = 0; if (g_audio_hw) g_audio_hw->sfx_stop(i); }
    sp.id = 0;
    if (g_audio_hw) g_audio_hw->speech_stop();
}
/* the arrival's "stop all sounds" of an elevator ride: the DOS ride song keeps playing (12 section 5.6) */
void snd_stop_all_keep_ride(void) {
    dosint_stop_effects_keep_ride();
    for (int i = 0; i < 4; i++) { ch[i].id = 0; if (g_audio_hw) g_audio_hw->sfx_stop(i); }
    sp.id = 0;
    if (g_audio_hw) g_audio_hw->speech_stop();
}
/* the ride song in DOS effect style (the Windows doors-close WAVE is played by snd_play as before) */
void snd_elevator_ride(int up) {
    if (sw_sfx && dosint_sfx_dos()) dosint_elevator_ride(up);
}
int snd_playing(int id) { expire(); if (dos_id(id)) return dosint_sfx_playing(id); for (int i = 0; i < 4; i++) if (ch[i].id == id) return 1; return 0; }

void speech_play(int id) {
    if (!sw_voice) return;
    dosint_stop_effects();
    for (int i = 0; i < 4; i++) { ch[i].id = 0; if (g_audio_hw) g_audio_hw->sfx_stop(i); }   /* starting speech stops every playing wave */
    sp.id = id;
    sp.end = plat_now() + wave_duration_cs(id, "WAVE");
    evlog("SPEECH", id, "");
    if (g_audio_hw) { size_t l; const uint8_t *w = res_get("WAVE", id, &l); if (w) { float on, off; levels_wave(LB_SPEECH, id, w, l, &on, &off); g_audio_hw->speech_start(w, (uint32_t)l, on, off); } }
}
void speech_stop(void) { sp.id = 0; if (g_audio_hw) g_audio_hw->speech_stop(); }
int speech_playing(void) { expire(); return sp.id != 0; }

/* ----------------------------------------------------------------- music */
static int smf_duration_cs(const uint8_t *b, size_t n) {
    if (!b) return 0;
    int dur = 0;
    if (n >= 22 && !memcmp(b, "MThd", 4)) {
        int div = rd16be(b + 12);
        size_t p = 14;
        if (!memcmp(b + p, "MTrk", 4)) {
            uint32_t tl = (uint32_t)(b[p + 4] << 24 | b[p + 5] << 16 | b[p + 6] << 8 | b[p + 7]);
            size_t q = p + 8, end = q + tl;
            if (end > n) end = n;
            double us_per_q = 500000.0, total_us = 0;
            int run = 0;
            while (q < end) {
                uint32_t delta = 0;
                while (q < end) { int c = b[q++]; delta = (delta << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                total_us += (double)delta * us_per_q / (div ? div : 480);
                if (q >= end) break;
                int st = b[q];
                if (st == 0xFF) {
                    int type = b[q + 1];
                    q += 2;
                    uint32_t l = 0;
                    while (q < end) { int c = b[q++]; l = (l << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                    if (type == 0x51 && l == 3) us_per_q = (double)(b[q] << 16 | b[q + 1] << 8 | b[q + 2]);
                    q += l;
                } else if (st == 0xF0 || st == 0xF7) {
                    q++;
                    uint32_t l = 0;
                    while (q < end) { int c = b[q++]; l = (l << 7) | (c & 0x7F); if (!(c & 0x80)) break; }
                    q += l;
                } else {
                    if (st & 0x80) { run = st; q++; } else st = run;
                    int hi = st & 0xF0;
                    q += (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                }
            }
            dur = (int)(total_us / 10000.0);
        }
    }
    return dur;
}

/* bytes of MIDI/<name> from the data source (name starts with a space which is replaced by b / e, spec 01 section 4) */
static const uint8_t *midi_bytes(const char *name, size_t *len) {
    char fn[96];
    snprintf(fn, sizeof fn, "MIDI/%s", name);
    fn[5] = midi_extended ? 'e' : 'b';
    return g_data_src ? vs_read(g_data_src, fn, len) : NULL;
}

void music_play(int text_id, int loop) {
    expire();
    if (mu.id == text_id && (mu.loop || mu.end > plat_now())) { mu.loop = loop; return; }
    mu.id = 0;
    mu_remember = text_id;
    mu_remember_loop = loop;
    if (!sw_music) return;   /* remembered so that switching Music on resumes it */
    size_t len;
    const uint8_t *t = res_get("TEXT", text_id, &len);
    if (!t) return;
    char name[64];
    size_t i = 0;
    while (i < len && i < 63 && t[i] && t[i] != '\r' && t[i] != '\n') { name[i] = (char)t[i]; i++; }
    name[i] = 0;
    mu.id = text_id;
    mu.loop = loop;
    mu.start = plat_now();
    if (dosint_music_dos()) {
        mu.dur = 0;
        mu.end = mu.start + 100000000;         /* ends when the DOS song is no longer playing (expire) */
    } else {
        size_t ml = 0;
        const uint8_t *mb = name[0] ? midi_bytes(name, &ml) : NULL;
        mu.dur = mb ? smf_duration_cs(mb, ml) : 0;
        if (mb && g_audio_hw) { float on, off; levels_midi(name + 1, mb, ml, &on, &off); g_audio_hw->music_start(mb, ml, loop, on, off); }
        mu.end = mu.start + mu.dur;
    }
    evlog("MUSIC", text_id, "file=%c%s loop=%d", midi_extended ? 'e' : 'b', name + 1, loop);
    if (dosint_music_dos()) dosint_music_play(text_id, loop);      /* DOS song instead of the MIDI file */
}
void music_stop(void) { mu.id = 0; dosint_music_stop(); if (g_audio_hw) g_audio_hw->music_stop(); }
int music_playing(void) { expire(); return mu.id != 0; }
int music_current(void) { return mu.id; }

static int mpause_depth;
static int64_t mpause_t0;
void audio_menu_pause(int on) {
    if (on) {
        if (mpause_depth++ == 0) { mpause_t0 = plat_now(); if (g_audio_hw && g_audio_hw->pause) g_audio_hw->pause(1); }
        return;
    }
    if (mpause_depth <= 0 || --mpause_depth > 0) return;
    int64_t d = plat_now() - mpause_t0;              /* the logical end times move by the time spent paused */
    for (int i = 0; i < 4; i++) if (ch[i].id && !ch[i].loop) ch[i].end += d;
    if (sp.id) sp.end += d;
    if (mu.id && !mu.loop) { mu.end += d; mu.start += d; }
    if (g_audio_hw && g_audio_hw->pause) g_audio_hw->pause(0);
}
void audio_pause(void) { pause_depth++; }
void audio_resume(void) { if (pause_depth > 0) pause_depth--; }

/* ---------------------------------------------------------- Audio Options support (spec 12 section 6) */
static int rs_id, rs_loop;
void audio_restyle_begin(void) {
    expire();
    /* only a slot that is playing now, or held back by the Music switch, comes back */
    rs_id = mu.id ? mu.id : (!sw_music ? mu_remember : 0);
    rs_loop = mu.id ? mu.loop : mu_remember_loop;
    snd_stop_all();
    music_stop();
    mu_remember = 0;
}
void audio_restyle_end(void) {
    if (rs_id && sw_music) music_play(rs_id, rs_loop);
    else if (rs_id) { mu_remember = rs_id; mu_remember_loop = rs_loop; }
    rs_id = 0;
}
int audio_test_win_sfx(int id) {
    size_t l;
    const uint8_t *w = res_get("WAVE", id, &l);
    if (!w || !g_audio_hw) return 0;
    { float on, off; levels_wave(LB_EFFECTS, id, w, l, &on, &off); g_audio_hw->sfx_start(3, w, (uint32_t)l, 0, on, off); }
    return wave_duration_cs(id, "WAVE");
}
int audio_test_win_speech(int id) {
    size_t l;
    const uint8_t *w = res_get("WAVE", id, &l);
    if (!w || !g_audio_hw) return 0;
    float on, off;
    levels_wave(LB_SPEECH, id, w, l, &on, &off);
    g_audio_hw->speech_start(w, (uint32_t)l, on, off);
    return wave_duration_cs(id, "WAVE");
}
void audio_test_win_music(int text_id) {
    size_t len;
    const uint8_t *t = res_get("TEXT", text_id, &len);
    if (!t || !g_audio_hw) return;
    char name[64];
    size_t i = 0;
    while (i < len && i < 63 && t[i] && t[i] != '\r' && t[i] != '\n') { name[i] = (char)t[i]; i++; }
    name[i] = 0;
    size_t ml = 0;
    const uint8_t *mb = name[0] ? midi_bytes(name, &ml) : NULL;
    if (mb) { float on, off; levels_midi(name + 1, mb, ml, &on, &off); g_audio_hw->music_start(mb, ml, 0, on, off); }
}
void audio_test_win_stop(void) {
    if (!g_audio_hw) return;
    g_audio_hw->sfx_stop(3);
    g_audio_hw->speech_stop();
    g_audio_hw->music_stop();
}

void snd_wait(int id) { while (snd_playing(id)) plat_wait(1); }
void speech_wait(void) { while (speech_playing()) plat_wait(1); }
void music_wait(void) { while (music_playing()) plat_wait(1); }
