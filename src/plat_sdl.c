/* plat_sdl.c - interactive backend: SDL2 window, input, audio mixer, optional FluidSynth music */
#ifndef NO_SDL
#include "plat.h"
#include "audio_hw.h"
#include "audio.h"
#include "dosint.h"
#include "levels.h"
#include "levelmeasure.h"
#include "soundfont.h"
#include <math.h>
#include "aseq.h"
#include "gfx.h"
#include "util.h"
#include "res.h"
#include <SDL.h>
#include <zlib.h>
#include "appicon.h"
#ifdef HAVE_FLUIDSYNTH
#include <fluidsynth.h>
#endif


static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t t0;
static uint32_t pix[FRAME_W * FRAME_H];
static int alt_pending;

/* ------------------------------------------------------------------ audio */
typedef struct Voice { int16_t *pcm; int len; double pos; double step; int loop; int active; float on, off, cur; } Voice;
static Voice sfx[4], speech;
static SDL_AudioDeviceID adev;
static const char *soundfont_path;
#define MIX_RATE 44100
#ifdef HAVE_FLUIDSYNTH
static fluid_settings_t *fs;
static fluid_synth_t *synth;
static fluid_player_t *player;
#endif

static int wav_to_s16(const uint8_t *d, uint32_t len, Voice *v) {
    int rate = 11025, bits = 8, chans = 1;
    uint32_t p = 12;
    const uint8_t *data = NULL;
    uint32_t dlen = 0;
    while (p + 8 <= len) {
        uint32_t cl = rd32(d + p + 4);
        if (!memcmp(d + p, "fmt ", 4)) { chans = rd16(d + p + 10); rate = (int)rd32(d + p + 12); bits = rd16(d + p + 22); }
        else if (!memcmp(d + p, "data", 4)) { data = d + p + 8; dlen = cl; if (p + 8 + cl > len) dlen = len - p - 8; break; }
        p += 8 + cl + (cl & 1);
    }
    if (!data || bits != 8 || chans != 1) return -1;
    v->pcm = xmalloc(sizeof(int16_t) * (dlen + 1));
    for (uint32_t i = 0; i < dlen; i++) v->pcm[i] = (int16_t)(((int)data[i] - 128) * 256);
    v->len = (int)dlen;
    v->step = (double)rate / MIX_RATE;
    v->pos = 0;
    return 0;
}

static void voice_free(Voice *v) { SDL_LockAudioDevice(adev); v->active = 0; free(v->pcm); v->pcm = NULL; SDL_UnlockAudioDevice(adev); }

/* linear gain of a voice: asset gain (Even out on / off) times the bus slider (spec 15 section 5) */
static float voice_target(const Voice *v, int bus) {
    return (float)(level_db_to_lin(lv_even ? v->on : v->off) * levels_slider_lin(bus));
}
static inline float ramp(float *cur, float tgt) {                  /* about 5 ms for a change of one unit of gain */
    float step = fmaxf(1.0f, tgt) / 220.0f, d = tgt - *cur;
    if (d > step) d = step; else if (d < -step) d = -step;
    *cur += d;
    return *cur;
}

static void mix_voice(Voice *v, float *acc, int n, int bus) {
    if (!v->active) return;
    float tgt = voice_target(v, bus);
    for (int i = 0; i < n; i++) {
        int ip = (int)v->pos;
        if (ip >= v->len) {
            if (v->loop) { v->pos = 0; ip = 0; } else { v->active = 0; return; }
        }
        acc[i] += (v->pcm[ip] / 32768.0f) * ramp(&v->cur, tgt);
        v->pos += v->step;
    }
}

#ifdef HAVE_FLUIDSYNTH
static void fl_del_player(fluid_player_t *p) { fluid_global_lock(); delete_fluid_player(p); fluid_global_unlock(); }
static fluid_player_t *fl_new_player(fluid_synth_t *sy) { fluid_global_lock(); fluid_player_t *p = new_fluid_player(sy); fluid_global_unlock(); return p; }
#endif
static float mu_on, mu_off, mu_cur;                                 /* the current MIDI song's gains */

static void audio_cb(void *ud, Uint8 *stream, int bytes) {
    int n = bytes / 2;
    int16_t *out = (int16_t *)stream;
    static float acc[8192];
    if (n > 8192) n = 8192;
    memset(acc, 0, sizeof(float) * (size_t)n);
    for (int i = 0; i < 4; i++) mix_voice(&sfx[i], acc, n, LB_EFFECTS);
    mix_voice(&speech, acc, n, LB_SPEECH);
#ifdef HAVE_FLUIDSYNTH
    if (synth && player) {
        static float l[8192], r[8192];
        fluid_synth_write_float(synth, n, l, 0, 1, r, 0, 1);
        float tgt = (float)(level_db_to_lin(lv_even ? mu_on : mu_off) * levels_slider_lin(LB_MUSIC));
        for (int i = 0; i < n; i++) acc[i] += (l[i] + r[i]) * 0.5f * ramp(&mu_cur, tgt);
    }
#endif
    dosint_render(acc, n);                       /* DOS music/effects (lock is held by SDL in the callback) */
    for (int i = 0; i < n; i++) {
        float v = level_softclip(acc[i], 0.8f, 1.0f) * 32767.0f;
        out[i] = (int16_t)lrintf(v);
    }
}

static void hw_sfx_start(int slot, const uint8_t *riff, uint32_t len, int loop, float db_on, float db_off) {
    Voice nv;
    memset(&nv, 0, sizeof nv);
    if (wav_to_s16(riff, len, &nv) != 0) return;
    nv.loop = loop;
    nv.on = db_on; nv.off = db_off;
    nv.cur = voice_target(&nv, LB_EFFECTS);
    SDL_LockAudioDevice(adev);
    free(sfx[slot].pcm);
    sfx[slot] = nv;
    sfx[slot].active = 1;
    SDL_UnlockAudioDevice(adev);
}
static void hw_sfx_stop(int slot) { if (sfx[slot].pcm) voice_free(&sfx[slot]); }
static void hw_speech_start(const uint8_t *riff, uint32_t len, float db_on, float db_off) {
    Voice nv;
    memset(&nv, 0, sizeof nv);
    if (wav_to_s16(riff, len, &nv) != 0) return;
    nv.on = db_on; nv.off = db_off;
    nv.cur = voice_target(&nv, LB_SPEECH);
    SDL_LockAudioDevice(adev);
    free(speech.pcm);
    speech = nv;
    speech.active = 1;
    SDL_UnlockAudioDevice(adev);
}
static void hw_speech_stop(void) { if (speech.pcm) voice_free(&speech); }
static void hw_music_start(const uint8_t *smf, size_t len, int loop, float db_on, float db_off) {
#ifdef HAVE_FLUIDSYNTH
    if (!synth) return;
    SDL_LockAudioDevice(adev);
    mu_on = db_on; mu_off = db_off;                     /* the level never changes inside a song: gains are taken at its start */
    mu_cur = (float)(level_db_to_lin(lv_even ? mu_on : mu_off) * levels_slider_lin(LB_MUSIC));
    if (player) { fluid_player_stop(player); fl_del_player(player); player = NULL; }
    fluid_synth_all_notes_off(synth, -1);
    player = fl_new_player(synth);
    if (player && fluid_player_add_mem(player, smf, len) == FLUID_OK) {
        fluid_player_set_loop(player, loop ? -1 : 1);
        fluid_player_play(player);
    } else if (player) { fl_del_player(player); player = NULL; }
    SDL_UnlockAudioDevice(adev);
#else
    (void)smf; (void)len; (void)loop; (void)db_on; (void)db_off;
#endif
}
static void hw_music_stop(void) {
#ifdef HAVE_FLUIDSYNTH
    if (!synth) return;
    SDL_LockAudioDevice(adev);
    if (player) { fluid_player_stop(player); fl_del_player(player); player = NULL; }
    fluid_synth_all_notes_off(synth, -1);
    SDL_UnlockAudioDevice(adev);
#endif
}
static void hw_pause(int on) { if (adev) SDL_PauseAudioDevice(adev, on); }

static const AudioHw hw = {hw_sfx_start, hw_sfx_stop, hw_speech_start, hw_speech_stop, hw_music_start, hw_music_stop, hw_pause};

static void dos_lock(void) { if (adev) SDL_LockAudioDevice(adev); }
static void dos_unlock(void) { if (adev) SDL_UnlockAudioDevice(adev); }

/* Create the FluidSynth player once the settings are known (the INI is in the save directory). */
void plat_sdl_load_soundfont(const char *ini_value) {
#ifdef HAVE_FLUIDSYNTH
    if (!adev || synth) return;
    char buf[1100];
    const char *sf = soundfont_find(soundfont_path, ini_value, buf, sizeof buf);
    if (!sf) { fprintf(stderr, "music: no SoundFont found (use --soundfont, SoundFont= in outnumbered.ini, $SSO_SOUNDFONT or put one in soundfont/ next to the program); music is silent\n"); return; }
    fluid_global_lock();
    fluid_settings_t *nfs = new_fluid_settings();
    fluid_settings_setnum(nfs, "synth.sample-rate", (double)MIX_RATE);
    fluid_synth_t *ns = new_fluid_synth(nfs);
    fluid_global_unlock();
    if (fluid_synth_sfload(ns, sf, 1) < 0) { fluid_global_lock(); delete_fluid_synth(ns); delete_fluid_settings(nfs); fluid_global_unlock(); fprintf(stderr, "music: cannot load the SoundFont '%s'; music is silent\n", sf); return; }
    SDL_LockAudioDevice(adev);
    fs = nfs;
    synth = ns;
    SDL_UnlockAudioDevice(adev);
    fprintf(stderr, "music: SoundFont %s\n", sf);
    levels_midi_start(sf);                          /* measure the 8 songs in the background (spec 15 section 4.2) */
#else
    (void)ini_value;
#endif
}

static void audio_open(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = MIX_RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return; }
    SDL_PauseAudioDevice(adev, 0);
    g_audio_hw = &hw;
    dosint_set_realtime(have.freq > 0 ? have.freq : MIX_RATE, dos_lock, dos_unlock);
}

/* ------------------------------------------------------------------ video + input */
static int64_t sdl_now(void) { return (int64_t)((SDL_GetTicks() - t0) / 10); }

static int map_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_LEFT: return K_LEFT; case SDLK_RIGHT: return K_RIGHT; case SDLK_UP: return K_UP; case SDLK_DOWN: return K_DOWN;
    case SDLK_HOME: return K_HOME; case SDLK_END: return K_END; case SDLK_PAGEUP: return K_PGUP; case SDLK_PAGEDOWN: return K_PGDN;
    case SDLK_DELETE: return K_DELETE; case SDLK_ESCAPE: return K_ESC; case SDLK_TAB: return K_TAB;   /* Shift+Tab is handled in handle_event */
    case SDLK_RETURN: case SDLK_KP_ENTER: return K_ENTER; case SDLK_BACKSPACE: return K_BACKSPACE; case SDLK_SPACE: return K_SPACE;
    case SDLK_KP_4: return K_LEFT; case SDLK_KP_6: return K_RIGHT; case SDLK_KP_8: return K_UP; case SDLK_KP_7: return K_HOME;
    case SDLK_KP_2: return K_DOWN;
    case SDLK_F10: return 0x179; case SDLK_F1: return 0x170;
    }
    return 0;
}

static void handle_event(SDL_Event *e) {
    switch (e->type) {
    case SDL_QUIT: if (g_quit_hook) g_quit_hook(); exit(0);
    case SDL_WINDOWEVENT: {
        static int inactive;
        if (e->window.event == SDL_WINDOWEVENT_CLOSE) {      /* the close box asks like Exit does (03 section 3.3); before the first screen is up it just quits */
            if (g_menu_service) plat_menu_request(MR_CLOSE, 0);
            else { if (g_quit_hook) g_quit_hook(); exit(0); }
            break;
        }
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST && !inactive) { inactive = 1; dosint_pause(1); }
        else if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED && inactive) { inactive = 0; dosint_pause(0); }
        break;
    }
    case SDL_KEYDOWN: {
        SDL_Keycode k = e->key.keysym.sym;
        if (k == SDLK_F10 && !e->key.repeat) { alt_pending = 0; plat_menu_request(MR_SELECT, 0); break; }      /* F10 selects the first menu title */
        if (k == SDLK_LALT || k == SDLK_RALT) { if (!e->key.repeat) alt_pending = 1; break; }
        if ((e->key.keysym.mod & KMOD_ALT) && k >= SDLK_a && k <= SDLK_z && !(e->key.keysym.mod & KMOD_CTRL)) {      /* Alt+letter opens a menu title */
            alt_pending = 0;
            plat_menu_request(MR_ALTLETTER, (int)k);
            break;
        }
        alt_pending = 0;
        if ((e->key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) && k == SDLK_v) {
            /* paste: the clipboard's first line as text input (setup screen field) */
            char *clip = SDL_GetClipboardText();
            if (clip) {
                for (const unsigned char *q = (const unsigned char *)clip; *q && *q != '\r' && *q != '\n';) {
                    uint32_t cp = *q;
                    int n = cp < 0x80 ? 1 : (cp >> 5) == 6 ? 2 : (cp >> 4) == 14 ? 3 : (cp >> 3) == 30 ? 4 : 1;
                    if (n > 1) { cp &= 0xFF >> (n + 1); for (int kk = 1; kk < n && q[kk]; kk++) cp = (cp << 6) | (q[kk] & 0x3F); }
                    q += n;
                    if (cp == ' ') plat_push_key(K_SPACE);
                    else if (cp > 0x20 && cp < 0x7F) plat_push_key((int)cp);
                    else if (cp > 0x7E) plat_push_key((int)(K_TEXT_BASE | cp));
                }
                SDL_free(clip);
            }
            return;
        }
        if ((e->key.keysym.mod & KMOD_CTRL)) {
            if (k == SDLK_r) { if (!e->key.repeat) plat_push_key(K_CTRL_R); return; }
            if (k == SDLK_s) { if (!e->key.repeat) plat_push_key(K_CTRL_S); return; }
        }
        int m = map_key(k);
        if (m == K_TAB && (e->key.keysym.mod & KMOD_SHIFT)) m = K_BACKTAB;
        if (!m) return;
        plat_set_held(m, 1);
        if (!e->key.repeat) plat_push_key(m);
        else if (m == K_UP || m == K_DOWN || m == K_BACKSPACE) plat_push_key(m);   /* list auto-repeat */
        break;
    }
    case SDL_KEYUP: {
        SDL_Keycode ku = e->key.keysym.sym;
        if ((ku == SDLK_LALT || ku == SDLK_RALT) && alt_pending) { alt_pending = 0; plat_menu_request(MR_SELECT, 0); break; }   /* Alt pressed and released alone */
        int m = map_key(ku);
        if (m) plat_set_held(m, 0);
        break;
    }
    case SDL_TEXTINPUT: {
        /* ASCII goes through as its code; code points above 0x7E (setup screen, dialogs) as K_TEXT_BASE | cp */
        const unsigned char *p = (const unsigned char *)e->text.text;
        while (*p) {
            uint32_t cp = *p;
            int n = cp < 0x80 ? 1 : (cp >> 5) == 6 ? 2 : (cp >> 4) == 14 ? 3 : (cp >> 3) == 30 ? 4 : 1;
            if (n > 1) { cp &= 0xFF >> (n + 1); for (int k = 1; k < n && p[k]; k++) cp = (cp << 6) | (p[k] & 0x3F); }
            p += n;
            if (cp > 0x20 && cp < 0x7F) plat_push_key((int)cp);
            else if (cp > 0x7E) plat_push_key((int)(K_TEXT_BASE | cp));
        }
        break;
    }
    case SDL_DROPBEGIN: plat_drop_begin(); break;
    case SDL_DROPCOMPLETE: plat_drop_complete(); break;
    case SDL_DROPFILE:
        if (e->drop.file) { plat_drop_add(e->drop.file); SDL_free(e->drop.file); }
        break;
    case SDL_DROPTEXT:
        if (e->drop.file) {
            if (plat_drops_accepting()) {                 /* every line is a path of the drop (14 section 4.2) */
                char *txt = e->drop.file, *save = NULL;
                for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                    extern char *normalize_input(const char *, const char *);
                    char *pth = normalize_input(line, NULL);
                    if (*line && *line != '\r') plat_drop_add(pth);
                    free(pth);
                }
            }
            SDL_free(e->drop.file);
        }
        break;
    /* mouse positions are frame coordinates (menu bar strip on top); the game sees canvas coordinates (y - BAR_H) */
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            plat_set_frame_mouse(e->button.x, e->button.y, 1);
            if (g_menu_active) break;                                  /* the open menu polls the frame mouse */
            if (e->button.y < BAR_H) { int t = g_menu_hit ? g_menu_hit(e->button.x, e->button.y) : -1; if (t >= 0) plat_menu_request(MR_OPEN_TITLE, t); }
            else { plat_set_mouse(e->button.x, e->button.y - BAR_H, 1); plat_push_click(e->button.x, e->button.y - BAR_H); }
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT) { plat_set_frame_mouse(e->button.x, e->button.y, 0); if (!g_menu_active) plat_set_mouse(e->button.x, e->button.y - BAR_H, 0); }
        break;
    case SDL_MOUSEMOTION: {
        int lb = (e->motion.state & SDL_BUTTON_LMASK) != 0;
        plat_set_frame_mouse(e->motion.x, e->motion.y, lb);
        if (!g_menu_active) plat_set_mouse(e->motion.x, e->motion.y - BAR_H, lb);
        break;
    }
    }
}

static void sdl_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(&e);
}
static void sdl_present(void) {
    plat_compose(pix, g_eng.work->px, g_overlay);
    SDL_UpdateTexture(tex, NULL, pix, FRAME_W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    sdl_pump();
}
static void sdl_wait_until(int64_t t) {
    for (;;) {
        sdl_pump();
        plat_menu_poll();
        if (sdl_now() >= t) return;
        SDL_Delay(1);
    }
}
static SDL_Cursor *curs[8];
static int curs_tried[8];
static SDL_Cursor *make_cursor(int image) {
    size_t len;
    const uint8_t *d = res_get("CURSOR", image, &len);
    if (!d || len < 4 + 40 + 8 + 256) return NULL;
    int hx = rd16(d), hy = rd16(d + 2);
    const uint8_t *xorb = d + 4 + 40 + 8, *andb = xorb + 128;
    uint8_t data[128], mask[128];
    for (int y = 0; y < 32; y++)
        for (int bx = 0; bx < 4; bx++) {
            uint8_t xv = xorb[(31 - y) * 4 + bx], av = andb[(31 - y) * 4 + bx];
            data[y * 4 + bx] = (uint8_t)(~av & ~xv);
            mask[y * 4 + bx] = (uint8_t)(~av);
        }
    return SDL_CreateCursor(data, mask, 32, 32, hx, hy);
}
static void sdl_cursor(int id) {
    static int cur = -1;
    if (id == cur) return;
    cur = id;
    if (id >= 128 && id <= 134) {
        int k = id - 127;
        if (!curs_tried[k]) { curs_tried[k] = 1; curs[k] = make_cursor(k); }
        if (curs[k]) { SDL_SetCursor(curs[k]); return; }
    }
    SDL_SetCursor(SDL_GetDefaultCursor());
}

static const PlatBackend be = {sdl_now, sdl_wait_until, sdl_present, sdl_pump, sdl_cursor};

void plat_sdl_set_title(const char *title) { if (win) SDL_SetWindowTitle(win, title); }

/* the window icon (assets/icon.svg, embedded by assets/make_icons.py) */
static void set_window_icon(SDL_Window *w) {
    uLongf raw = (uLongf)APPICON_RAW;
    uint8_t *px = malloc(raw);
    if (!px) return;
    if (uncompress(px, &raw, APPICON_Z, (uLong)APPICON_ZLEN) == Z_OK && raw == APPICON_RAW) {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, APPICON_WIDTH, APPICON_HEIGHT, 32, APPICON_WIDTH * 4, SDL_PIXELFORMAT_RGBA32);
        if (s) { SDL_SetWindowIcon(w, s); SDL_FreeSurface(s); }        /* SDL copies the pixels */
    }
    free(px);
}

int plat_sdl_start(const char *soundfont, int fullscreen) {
    soundfont_path = soundfont;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return -1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint("SDL_QUIT_ON_LAST_WINDOW_CLOSE", "0");        /* the close box arrives as a window event, not as SDL_QUIT (which stays an immediate quit: signals) */
    win = SDL_CreateWindow("OutNumbered! CD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 808,
                           SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) return -1;
    set_window_icon(win);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) return -1;
    SDL_RenderSetLogicalSize(ren, FRAME_W, FRAME_H);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, FRAME_W, FRAME_H);
    SDL_StartTextInput();
    t0 = SDL_GetTicks();
    plat_set_backend(&be);
    plat_init(PLAT_SDL);
    audio_open();
    return 0;
}
void plat_sdl_stop(void) {
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_Quit();
}
#else
void plat_sdl_set_title(const char *title) { (void)title; }
#endif
