/* plat.c - platform layer. Script (deterministic, headless) backend; SDL backend in plat_sdl.c */
#include "plat.h"
#include "aseq.h"
#include "gfx.h"
#include "png.h"
#include "res.h"
#include "fsx.h"
#include "util.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <strings.h>

static PlatMode g_mode = PLAT_SCRIPT;
static void kq_push(int k);
ScriptHooks g_script_hooks;
void (*g_quit_hook)(void);

/* ------------------------------------------------------------- input queues */
#define KQ 4096
static int kq[KQ], kq_h, kq_t;
static int held[0x2000];
static int release_after_poll[0x2000];
static int cq_x[16], cq_y[16], cq_h, cq_t;
static int mouse_x, mouse_y, mouse_left, mouse_release_pending;

PlatMode plat_mode(void) { return g_mode; }
static const PlatBackend *be;
void plat_set_backend(const PlatBackend *b) { be = b; }
void plat_push_key(int k) { kq_push(k); }
void plat_set_held(int key, int down) { if (key >= 0 && key < 0x2000) { held[key] = down; release_after_poll[key] = 0; } }
void plat_push_click(int x, int y) {
    int n = (cq_t + 1) % 16;
    if (n != cq_h) { cq_x[cq_t] = x; cq_y[cq_t] = y; cq_t = n; }
}
void plat_set_mouse(int x, int y, int left) { mouse_x = x; mouse_y = y; mouse_left = left; mouse_release_pending = 0; }

static void kq_push(int k) {
    int n = (kq_t + 1) % KQ;
    if (n == kq_h) return;
    kq[kq_t] = k;
    kq_t = n;
}
int plat_key_pop(void) {
    if (kq_h == kq_t) return 0;
    int k = kq[kq_h];
    kq_h = (kq_h + 1) % KQ;
    return k;
}
static int modal_depth;
void plat_modal_enter(void) { modal_depth++; }
void plat_modal_leave(void) { if (modal_depth > 0) modal_depth--; }
int plat_modal_depth(void) { return modal_depth; }
void plat_modal_reset(void) { modal_depth = 0; }
void plat_key_flush(void) { kq_h = kq_t = 0; }
int plat_key_held(int key) {
    if (key < 0 || key >= 0x2000) return 0;
    int v = held[key];
    if (v && release_after_poll[key]) { held[key] = 0; release_after_poll[key] = 0; }
    return v;
}
int plat_click_pop(int *x, int *y) {
    if (cq_h == cq_t) return 0;
    *x = cq_x[cq_h];
    *y = cq_y[cq_h];
    cq_h = (cq_h + 1) % 16;
    return 1;
}
void plat_click_flush(void) { cq_h = cq_t = 0; }
void plat_mouse_state(int *x, int *y, int *left) {
    *x = mouse_x; *y = mouse_y; *left = mouse_left;
    if (mouse_left && mouse_release_pending) { mouse_left = 0; mouse_release_pending = 0; }
}
void plat_mouse_pos(int *x, int *y) { *x = mouse_x; *y = mouse_y; }
void plat_set_cursor(int id) { if (be && be->set_cursor) be->set_cursor(id); }

/* ------------------------------------------------------------- the frame: menu bar strip + canvas */
const uint32_t *g_overlay;
int g_menu_active;
int (*g_menu_hit)(int fx, int fy);
static int fm_x, fm_y, fm_left;
static int fq_x[16], fq_y[16], fq_h, fq_t;

void plat_set_overlay(const uint32_t *argb) { g_overlay = argb; }

void plat_compose(uint32_t *out, const uint8_t *canvas, const uint32_t *overlay) {
    for (int i = 0; i < FRAME_W * BAR_H; i++) out[i] = 0xFFC0C0C0u;
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        uint8_t v = canvas[i];
        out[FRAME_W * BAR_H + i] = 0xFF000000u | ((uint32_t)g_pal[v][0] << 16) | ((uint32_t)g_pal[v][1] << 8) | g_pal[v][2];
    }
    if (overlay) for (int i = 0; i < FRAME_W * FRAME_H; i++) if (overlay[i] >> 24) out[i] = overlay[i];
}

typedef struct MEvt { int kind, arg; } MEvt;
static MEvt mq[16];
static int mq_h, mq_t, in_poll;
int (*g_menu_service)(int kind, int arg);
void plat_menu_request(int kind, int arg) {
    if (g_menu_active) { kq_push(0x179); if (kind != MR_CLOSE) return; }      /* F10 / Alt while the bar is open: the bar reads it (leave); a close request is also kept */
    int n = (mq_t + 1) % 16;
    if (n != mq_h) { mq[mq_t].kind = kind; mq[mq_t].arg = arg; mq_t = n; }
}
void plat_menu_reset(void) { mq_h = mq_t = 0; in_poll = 0; }
void plat_menu_poll(void) {
    if (g_menu_active || in_poll || mq_h == mq_t) return;
    if (plat_modal_depth()) { mq_h = mq_t = 0; return; }            /* a dialog is open: the bar is disabled */
    in_poll = 1;
    while (mq_h != mq_t && !g_menu_active) {
        MEvt e = mq[mq_h];
        mq_h = (mq_h + 1) % 16;
        if (g_menu_service) g_menu_service(e.kind, e.arg);
        if (plat_modal_depth()) { mq_h = mq_t = 0; break; }
    }
    in_poll = 0;
}
void plat_set_frame_mouse(int fx, int fy, int left) { fm_x = fx; fm_y = fy; fm_left = left; }
void plat_frame_mouse(int *fx, int *fy, int *left) { *fx = fm_x; *fy = fm_y; *left = fm_left; }
void plat_fclick_push(int fx, int fy) { int n = (fq_t + 1) % 16; if (n != fq_h) { fq_x[fq_t] = fx; fq_y[fq_t] = fy; fq_t = n; } }
int plat_fclick_pop(int *fx, int *fy) { if (fq_h == fq_t) return 0; *fx = fq_x[fq_h]; *fy = fq_y[fq_h]; fq_h = (fq_h + 1) % 16; return 1; }

/* ------------------------------------------------------------- file drops */
typedef struct Drop { char **p; int n; } Drop;
static Drop dq[16];
static int dq_h, dq_t;
static Drop cur_drop;
static int drops_on, drop_open;
static char script_dir_buf[1024];

const char *plat_script_dir(void) { return script_dir_buf; }
void plat_drops_accept(int on) {
    drops_on = on;
    if (!on) {
        while (dq_h != dq_t) { for (int i = 0; i < dq[dq_h].n; i++) free(dq[dq_h].p[i]); free(dq[dq_h].p); dq_h = (dq_h + 1) % 16; }
        for (int i = 0; i < cur_drop.n; i++) free(cur_drop.p[i]);
        free(cur_drop.p);
        memset(&cur_drop, 0, sizeof cur_drop);
        drop_open = 0;
    }
}
int plat_drops_accepting(void) { return drops_on; }
void plat_drop_begin(void) { if (!drops_on) return; plat_drop_complete(); drop_open = 1; }
void plat_drop_add(const char *path) {
    if (!drops_on) return;
    int single = !drop_open;
    cur_drop.p = xrealloc(cur_drop.p, sizeof(char *) * (size_t)(cur_drop.n + 1));
    cur_drop.p[cur_drop.n++] = xstrdup(path);
    if (single) plat_drop_complete();
}
void plat_drop_complete(void) {
    drop_open = 0;
    if (!cur_drop.n) return;
    int nx = (dq_t + 1) % 16;
    if (nx != dq_h) { dq[dq_t] = cur_drop; dq_t = nx; }
    else { for (int i = 0; i < cur_drop.n; i++) free(cur_drop.p[i]); free(cur_drop.p); }
    memset(&cur_drop, 0, sizeof cur_drop);
}
int plat_drop_pop(char ***paths, int *n) {
    if (dq_h == dq_t) return 0;
    *paths = dq[dq_h].p;
    *n = dq[dq_h].n;
    dq_h = (dq_h + 1) % 16;
    return 1;
}

/* -------------------------------------------------------------- event log */
static FILE *evf;
static int64_t vnow;
void evlog_open(const char *path) { evf = fopen(path, "w"); }
void (*g_evlog_pre)(void);
void evlog(const char *kind, int id, const char *fmt, ...) {
    if (!evf) return;
    if (g_evlog_pre) g_evlog_pre();
    fprintf(evf, "%lld %s %d", (long long)plat_now(), kind, id);
    if (fmt && *fmt) {
        va_list ap;
        va_start(ap, fmt);
        fputc(' ', evf);
        vfprintf(evf, fmt, ap);
        va_end(ap);
    }
    fputc('\n', evf);
    fflush(evf);
}
void evlog_raw(int64_t t, const char *text) {
    if (!evf) return;
    fprintf(evf, "%lld %s\n", (long long)t, text);
    fflush(evf);
}
void evlog_close(void) { if (evf) { fclose(evf); evf = NULL; } }

/* ---------------------------------------------------------------- script */
typedef struct Cmd { char *text; } Cmd;
static Cmd *cmds;
static int ncmds, cp;
static int64_t wait_left = -1;      /* remaining cs of the wait at cmds[cp], -1 = not started */
static char outdir[1024];

const char *script_outdir(void) { return outdir; }

static void mkdirs(const char *p) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", p);
    for (char *c = tmp + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(tmp, 0777); *c = '/'; }
    mkdir(tmp, 0777);
}

int script_load(const char *path, const char *dir) {
    size_t n;
    uint8_t *b = file_read_all(path, &n);
    if (!b) return -1;
    snprintf(outdir, sizeof outdir, "%s", dir);
    mkdirs(outdir);
    {
        char *abs = fsx_abspath(path);
        char *sl = strrchr(abs, '/');
        if (sl) *sl = 0;
        snprintf(script_dir_buf, sizeof script_dir_buf, "%s", abs);
        free(abs);
    }
    char p[1100];
    snprintf(p, sizeof p, "%s/events.log", outdir);
    evlog_open(p);
    char *s = (char *)b;
    char *line = strtok(s, "\n");
    while (line) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        size_t l = strlen(line);
        while (l && (line[l - 1] == ' ' || line[l - 1] == '\t' || line[l - 1] == '\r')) line[--l] = 0;
        if (l) {
            cmds = xrealloc(cmds, sizeof(Cmd) * (size_t)(ncmds + 1));
            cmds[ncmds++].text = xstrdup(line);
        }
        line = strtok(NULL, "\n");
    }
    free(b);
    return 0;
}

static int keycode(const char *name) {
    static const struct { const char *n; int k; } t[] = {
        {"LEFT", K_LEFT}, {"RIGHT", K_RIGHT}, {"UP", K_UP}, {"DOWN", K_DOWN}, {"HOME", K_HOME},
        {"SPACE", K_SPACE}, {"ENTER", K_ENTER}, {"ESC", K_ESC}, {"TAB", K_TAB}, {"BACKSPACE", K_BACKSPACE}, {"PGUP", K_PGUP}, {"PGDN", K_PGDN}, {"BACKTAB", K_BACKTAB}, {"SHIFT+TAB", K_BACKTAB}, {"DELETE", K_DELETE}, {"END", K_END},
    };
    for (int i = 0; i < ARRAY_LEN(t); i++)
        if (!strcasecmp(name, t[i].n)) return t[i].k;
    if (strlen(name) > 1) { fprintf(stderr, "script: unknown key name '%s'\n", name); return 0; }
    return (unsigned char)name[0];
}

static uint8_t shown[CANVAS_W * CANVAS_H];
static uint32_t shown_over[FRAME_W * FRAME_H];
static int shown_over_on;
static int no_autopresent;
void plat_hold_present(int on) { no_autopresent = on; }
static void present_copy(void) {
    memcpy(shown, g_eng.work->px, sizeof shown);
    shown_over_on = g_overlay != NULL;
    if (g_overlay) memcpy(shown_over, g_overlay, sizeof shown_over);
}

static void write_frame(const char *name, int full) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s.png", outdir, name);
    if (full) {                                   /* the whole 512x404 frame, menu bar included */
        static uint32_t fr[FRAME_W * FRAME_H];
        plat_compose(fr, shown, shown_over_on ? shown_over : NULL);
        uint8_t *rgba = xmalloc(FRAME_W * FRAME_H * 4);
        for (int i = 0; i < FRAME_W * FRAME_H; i++) {
            rgba[i * 4] = (uint8_t)(fr[i] >> 16); rgba[i * 4 + 1] = (uint8_t)(fr[i] >> 8); rgba[i * 4 + 2] = (uint8_t)fr[i]; rgba[i * 4 + 3] = 255;
        }
        png_write_rgba(p, FRAME_W, FRAME_H, rgba);
        free(rgba);
        return;
    }
    uint8_t *rgba = xmalloc(CANVAS_W * CANVAS_H * 4);
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
        uint8_t v = shown[i];
        rgba[i * 4] = g_pal[v][0]; rgba[i * 4 + 1] = g_pal[v][1]; rgba[i * 4 + 2] = g_pal[v][2]; rgba[i * 4 + 3] = 255;
    }
    png_write_rgba(p, CANVAS_W, CANVAS_H, rgba);
    free(rgba);
}

static void write_state(const char *name) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s.json", outdir, name);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "{\n \"tick\": %lld", (long long)vnow);
    if (g_script_hooks.dump_state) { fprintf(f, ",\n"); g_script_hooks.dump_state(f); }
    fprintf(f, "\n}\n");
    fclose(f);
}

static void key_down(int k, int queue) {
    if (k >= 0 && k < 0x2000) held[k] = 1;
    if (queue) kq_push(k);
}
static void key_up(int k) { if (k >= 0 && k < 0x2000) held[k] = 0; }

/* Execute the command at the head if it is not a wait. Returns 1 if a command ran. */
static int exec_head(void) {
    if (cp >= ncmds || g_script_hooks.hold) return 0;
    char *t = cmds[cp].text;
    char a[64] = "", b[64] = "", c[64] = "";
    sscanf(t, "%63s %63s %63s", a, b, c);
    if (!strcasecmp(a, "wait")) return 0;
    int input_cmd = !strcasecmp(a, "press") || !strcasecmp(a, "click") || !strcasecmp(a, "key");
    if (!strcasecmp(a, "key") && !strcasecmp(b, "down")) { int k = keycode(c); if (k) key_down(k, 1); }
    else if (!strcasecmp(a, "key") && !strcasecmp(b, "up")) { int k = keycode(c); if (k) key_up(k); }
    else if (!strcasecmp(a, "press")) { int k = keycode(b); if (k) { key_down(k, 1); release_after_poll[k] = 1; } }
    else if (!strcasecmp(a, "click")) {
        int x = atoi(b), y = atoi(c);
        mouse_x = x; mouse_y = y; mouse_left = 1; mouse_release_pending = 1;
        int n = (cq_t + 1) % 16;
        if (n != cq_h) { cq_x[cq_t] = x; cq_y[cq_t] = y; cq_t = n; }
    } else if (!strcasecmp(a, "close")) plat_menu_request(MR_CLOSE, 0);          /* the window close box */
    else if (!strcasecmp(a, "decide")) kq_push(!strcasecmp(b, "lose") ? 0x5002 : 0x5001);   /* conformance: the Decide ending, right (win) or wrong (lose) room, without the decoder */
    else if (!strcasecmp(a, "midnight")) kq_push(0x5003);                         /* conformance: the clock runs out (Out of Time) */
    else if (!strcasecmp(a, "menu")) plat_menu_request(MR_COMMAND, atoi(b));   /* menu command id (500..1500) */
    else if (!strcasecmp(a, "drop")) {
        const char *rest = t + 4;
        plat_drop_begin();
        while (*rest) {
            while (*rest == ' ' || *rest == '\t') rest++;
            if (!*rest) break;
            char tok[4096];
            size_t o = 0;
            if (*rest == '"') { rest++; while (*rest && *rest != '"' && o + 1 < sizeof tok) tok[o++] = *rest++; if (*rest == '"') rest++; }
            else while (*rest && *rest != ' ' && *rest != '\t' && o + 1 < sizeof tok) tok[o++] = *rest++;
            tok[o] = 0;
            char full[8192];
            if (PATH_IS_ABSOLUTE(tok) || !script_dir_buf[0]) snprintf(full, sizeof full, "%s", tok);
            else snprintf(full, sizeof full, "%s/%s", script_dir_buf, tok);
            plat_drop_add(full);
        }
        plat_drop_complete();
        input_cmd = 1;
    } else if (!strcasecmp(a, "type")) {
        const char *rest = t + 4;
        if (*rest == ' ') rest++;
        while (*rest) {
            const unsigned char *s = (const unsigned char *)rest;
            uint32_t cp = s[0];
            int n = cp < 0x80 ? 1 : (cp >> 5) == 6 ? 2 : (cp >> 4) == 14 ? 3 : (cp >> 3) == 30 ? 4 : 1;
            if (n > 1) { cp &= 0xFF >> (n + 1); for (int k = 1; k < n && s[k]; k++) cp = (cp << 6) | (s[k] & 0x3F); }
            rest += n;
            kq_push(cp < 0x80 ? (int)cp : (int)(K_TEXT_BASE | cp));
        }
        input_cmd = 1;
    }
    else if (!strcasecmp(a, "volume")) {
        int m = 100, e = 100, s = 100, ev = -1;
        sscanf(t + 6, "%d %d %d %d", &m, &e, &s, &ev);
        if (g_script_hooks.volume) g_script_hooks.volume(m, e, s, ev);
    }
    else if (!strcasecmp(a, "audio")) {
        char dir[1024] = "";
        const char *rest = t;
        for (int i = 0; i < 3 && *rest; i++) { while (*rest && *rest != ' ' && *rest != '\t') rest++; while (*rest == ' ' || *rest == '\t') rest++; }
        snprintf(dir, sizeof dir, "%s", rest);
        if (g_script_hooks.audio) g_script_hooks.audio(b, c, dir[0] ? dir : NULL);
    }
    else if (!strcasecmp(a, "frame")) write_frame(b, !strcasecmp(c, "full"));
    else if (!strcasecmp(a, "menuclick")) {
        int x = atoi(b), y = atoi(c);
        if (g_menu_active) plat_fclick_push(x, y);
        else if (g_menu_hit) { int t = g_menu_hit(x, y); if (t >= 0) plat_menu_request(MR_OPEN_TITLE, t); }
    }
    else if (!strcasecmp(a, "state")) write_state(b);
    else if (!strcasecmp(a, "seed") || !strcasecmp(a, "signin") || !strcasecmp(a, "drill") || !strcasecmp(a, "intro") || !strcasecmp(a, "setup")) { /* start-up commands: ignored here */ }
    else fprintf(stderr, "script: unknown command '%s'\n", t);
    cp++;
    if (input_cmd && g_script_hooks.pump) g_script_hooks.pump();
    return 1;
}

int script_get_start(ScriptStart *s) {
    memset(s, 0, sizeof *s);
    while (cp < ncmds) {
        char *t = cmds[cp].text;
        char a[64] = "", b[64] = "", c[64] = "";
        sscanf(t, "%63s %63s %63s", a, b, c);
        if (!strcasecmp(a, "seed") && !strcasecmp(b, "time")) { s->have_seed = 1; s->seed_time = atoi(c); cp++; }
        else if (!strcasecmp(a, "signin")) {
            snprintf(s->name, sizeof s->name, "%.8s", b);
            for (char *q = s->name; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
            s->score = c[0] ? atol(c) : -1;
            s->valid = 1;
            cp++;
            return 1;
        } else if (!strcasecmp(a, "drill")) { s->drill = 1; s->valid = 1; cp++; return 1; }
        else if (!strcasecmp(a, "intro")) { s->intro = 1; s->valid = 1; cp++; return 1; }
        else if (!strcasecmp(a, "setup")) { s->setup = 1; s->valid = 1; cp++; return 1; }
        else if (!strcasecmp(a, "volume")) {
            if (s->nvolume < 4) {
                int v[4] = {100, 100, 100, -1};
                sscanf(t + 6, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
                for (int i = 0; i < 4; i++) s->vol[s->nvolume][i] = v[i];
                s->nvolume++;
            }
            cp++;
        }
        else if (!strcasecmp(a, "audio")) {
            if (s->naudio < 4) {
                const char *rest = t;
                for (int i = 0; i < 3 && *rest; i++) { while (*rest && *rest != ' ' && *rest != '\t') rest++; while (*rest == ' ' || *rest == '\t') rest++; }
                snprintf(s->audio[s->naudio][0], 1024, "%s", b);
                snprintf(s->audio[s->naudio][1], 1024, "%s", c);
                snprintf(s->audio[s->naudio][2], 1024, "%s", rest);
                s->naudio++;
            }
            cp++;
        }
        else if (!strcasecmp(a, "wait") && !s->have_seed && cp == 0) cp++;   /* waits before sign-in are meaningless */
        else if (!strcasecmp(a, "wait")) cp++;
        else cp++;
    }
    return 0;
}

void script_finish(int code) {
    evlog_close();
    exit(code);
}

/* --------------------------------------------------------------- time */
int64_t plat_now(void) { return (g_mode == PLAT_SDL && be) ? be->now() : vnow; }

void plat_init(PlatMode mode) {
    g_mode = mode;
    vnow = 0;
}
void plat_present(void) { if (g_mode == PLAT_SDL && be) be->present(); else present_copy(); }
void plat_pump(void) {
    if (g_mode == PLAT_SDL && be) { be->pump(); plat_menu_poll(); return; }
    while (exec_head()) {}
    plat_menu_poll();
    if (cp >= ncmds) script_finish(0);
}

void plat_wait_until(int64_t t) {
    if (g_mode == PLAT_SDL && be) { be->wait_until(t); return; }
    if (!no_autopresent) present_copy();          /* what is drawn when the game starts waiting is on screen (frame commands capture it) */
    for (;;) {
        while (exec_head()) {}
        plat_menu_poll();
        if (g_script_hooks.hold) return;
        if (cp >= ncmds) script_finish(0);
        if (vnow >= t) return;
        /* head is a wait */
        if (wait_left < 0) {
            char a[32] = "";
            long n = 0;
            sscanf(cmds[cp].text, "%31s %ld", a, &n);
            wait_left = n < 0 ? 0 : n;
        }
        int64_t step = t - vnow;
        if (step > wait_left) step = wait_left;
        for (int64_t i = 0; i < step; i++) { vnow++; if (g_script_hooks.tick) g_script_hooks.tick(); }
        wait_left -= step;
        if (wait_left == 0) { cp++; wait_left = -1; }
    }
}
