/* plat.h - platform layer: virtual/real time, input, script driver, event log, presentation */
#ifndef PLAT_H
#define PLAT_H
#include <stdint.h>
#include <stdio.h>

/* ---- keys: ASCII for characters; high codes for navigation keys ---- */
enum {
    K_NONE = 0, K_BACKSPACE = 8, K_TAB = 9, K_ENTER = 13, K_ESC = 27, K_SPACE = 32,
    K_LEFT = 0x1025, K_UP = 0x1026, K_RIGHT = 0x1027, K_DOWN = 0x1028, K_HOME = 0x1024,
    K_END = 0x1023, K_PGUP = 0x1021, K_PGDN = 0x1022, K_DELETE = 0x102E,
    K_CTRL_R = 0x12, K_CTRL_S = 0x13, K_BACKTAB = 0x1009
};

typedef enum { PLAT_SCRIPT, PLAT_SDL } PlatMode;

/* backend hooks implemented by plat_sdl.c (interactive mode) */
typedef struct PlatBackend {
    int64_t (*now)(void);
    void (*wait_until)(int64_t t);
    void (*present)(void);
    void (*pump)(void);
    void (*set_cursor)(int id);
} PlatBackend;
void plat_set_backend(const PlatBackend *b);
/* input injection for backends */
void plat_push_key(int k);
void plat_set_held(int key, int down);
void plat_push_click(int x, int y);
void plat_set_mouse(int x, int y, int left);
extern void (*g_quit_hook)(void);
int plat_sdl_start(const char *soundfont, int fullscreen);
void plat_sdl_stop(void);
void plat_sdl_load_soundfont(const char *ini_value);   /* after the save dir is known; ini_value = SoundFont= of outnumbered.ini or NULL */
void plat_init(PlatMode mode);
PlatMode plat_mode(void);
int64_t plat_now(void);                /* centiseconds */
void plat_wait_until(int64_t t);       /* block (pumping input/script) until now >= t */
static inline void plat_wait(int cs) { plat_wait_until(plat_now() + cs); }
void plat_pump(void);                  /* process pending input without advancing time */
void plat_present(void);
void plat_hold_present(int on);   /* script mode: the next waits do not auto-present (the action frame loop presents after its deadline) */               /* show g_eng.work */

int plat_key_pop(void);                /* next queued key (0 = none) */
void plat_modal_enter(void);           /* a dialog is open: the menu bar is disabled (as in a Windows modal dialog) */
void plat_modal_leave(void);
int plat_modal_depth(void);
void plat_modal_reset(void);
void plat_key_flush(void);
int plat_key_held(int key);            /* polled state; marks the key as polled */
/* mouse: returns 1 and fills if a click was queued */
int plat_click_pop(int *x, int *y);
void plat_mouse_state(int *x, int *y, int *left);  /* polled state */
void plat_click_flush(void);
void plat_mouse_pos(int *x, int *y);          /* position only (does not consume the pending button release) */
void plat_set_cursor(int group_cursor_id);

/* file drops (14 section 4.3): only collected while a screen asks for them */
void plat_drops_accept(int on);
int plat_drops_accepting(void);
void plat_drop_begin(void);
void plat_drop_add(const char *path);          /* a path of the current drop (starts one if none is open) */
void plat_drop_complete(void);
int plat_drop_pop(char ***paths, int *n);      /* the next complete drop; caller frees the strings and the array */
const char *plat_script_dir(void);             /* folder of the script file ("" = none) */
void plat_sdl_set_title(const char *title);
#define K_TEXT_BASE 0x100000
#define K_ALT 0x1018                           /* Alt pressed and released alone */
#define K_ALTLETTER 0x200000                   /* Alt+letter: K_ALTLETTER | lower-case letter */

/* the logical frame: a 20-px menu bar strip above the 512x384 canvas (03 section 3.0) */
#define FRAME_W 512
#define BAR_H 20
#define FRAME_H 404
extern const uint32_t *g_overlay;              /* FRAME_W x FRAME_H ARGB drawn over the canvas (the bar, an open drop-down); NULL = a plain grey strip */
void plat_set_overlay(const uint32_t *argb);
void plat_compose(uint32_t *out, const uint8_t *canvas, const uint32_t *overlay);   /* canvas indices + palette + overlay -> ARGB frame */
enum { MR_NONE = 0, MR_SELECT, MR_OPEN_TITLE, MR_ALTLETTER, MR_COMMAND, MR_CLOSE };   /* MR_CLOSE: the window's close box (03 section 3.3) */
void plat_menu_request(int kind, int arg);     /* a menu bar request (F10 / Alt / title click), or MR_COMMAND = a scripted command id. Queued; served by plat_menu_poll() */
void plat_menu_poll(void);                     /* called by every wait: serves the queued requests through g_menu_service (the bar opens at any moment of any screen, 03 section 3.0a B) */
extern int (*g_menu_service)(int kind, int arg);   /* menu.c */
void plat_menu_reset(void);                    /* after a longjmp out of a menu command */
extern int g_menu_active;                      /* menu_run is running */
extern int (*g_menu_hit)(int fx, int fy);      /* title index under a bar position, or -1 (set by menu.c) */
void plat_set_frame_mouse(int fx, int fy, int left);
void plat_frame_mouse(int *fx, int *fy, int *left);
void plat_fclick_push(int fx, int fy);
int plat_fclick_pop(int *fx, int *fy);                   /* key code of a text-input code point above 0x7E: K_TEXT_BASE | cp */   /* 0 = default arrow */

/* ---- event log (conformance) ---- */
void evlog_open(const char *path);
void evlog(const char *kind, int id, const char *fmt, ...);
void evlog_close(void);
extern void (*g_evlog_pre)(void);           /* run before any line is written (the DOS drivers catch up first) */
void evlog_raw(int64_t t, const char *text);   /* writes "<t> <text>" */

/* ---- script driver ---- */
typedef struct ScriptHooks {
    void (*dump_state)(FILE *f);      /* write the body of the state JSON (without outer braces) */
    void (*audio)(const char *music, const char *sfx, const char *dir);   /* script command `audio` (dir may be NULL) */
    void (*volume)(int m, int e, int s, int even);                        /* script command `volume M E S [even]` (even = -1: unchanged) */
    void (*tick)(void);               /* after every centisecond of virtual time (setup screen: one 100 Hz tick, before the next command runs) */
    int hold;                         /* nonzero: run no more script commands until cleared (a screen is leaving) */
    void (*pump)(void);               /* after input commands (press, click, key, drop, type): the setup screen handles them at once */
} ScriptHooks;
extern ScriptHooks g_script_hooks;
int script_load(const char *path, const char *outdir);
const char *script_outdir(void);
/* Pre-game commands: returns next command line kind for the driver (signin/drill/seed). */
typedef struct ScriptStart {
    int have_seed;
    int seed_time;
    char name[16];
    long score;
    int drill;
    int intro;
    int setup;                        /* the `setup` command: start on the setup screen (14 section 10.3) */
    int valid;
    int nvolume;
    int vol[4][4];                    /* leading `volume M E S [even]` commands */
    int naudio;
    char audio[4][3][1024];           /* leading `audio MUSIC SFX [DIR]` commands (applied once the save dir is known) */
} ScriptStart;
int script_get_start(ScriptStart *s);   /* executes leading commands up to signin/drill */
void script_finish(int code);           /* flush logs and exit */

#endif
