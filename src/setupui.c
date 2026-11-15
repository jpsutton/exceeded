/* setupui.c - the first-run setup screen (spec chapter 14 section 7) */
#include "setupui.h"
#include "aseq.h"
#include "fsx.h"
#include "gfx.h"
#include "plat.h"
#include "util.h"
#include <strings.h>
#include <unistd.h>

extern char font8x8_basic[128][8];
extern char font8x8_ext_latin[96][8];

static SetupModel *G;
static int g_active;
static int pressing;                  /* 0 none, 1 = button 1, 2 = button 2 */
static int blink0;
static int mouse_held_prev;

int setup_screen_active(void) { return g_active; }

/* ------------------------------------------------------------------ drawing */
static const uint8_t PAL[13][3] = {{0x14, 0x18, 0x2C}, {0x23, 0x2A, 0x45}, {0x3A, 0x45, 0x70}, {0xF0, 0xF0, 0xF0}, {0x9A, 0xA3, 0xC0}, {0xFF, 0xD3, 0x4E},
                                   {0x6B, 0xD4, 0x6B}, {0xFF, 0x6B, 0x6B}, {0x5F, 0xC8, 0xFF}, {0x3C, 0x4A, 0x80}, {0x2A, 0x35, 0x60}, {0x26, 0x2C, 0x44}, {0x0A, 0x0C, 0x18}};

static void fill(int x0, int y0, int x1, int y1, int c) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H) g_eng.work->px[y * CANVAS_W + x] = (uint8_t)c;
}
static void border(int x0, int y0, int x1, int y1, int c) {
    fill(x0, y0, x1, y0, c); fill(x0, y1, x1, y1, c); fill(x0, y0, x0, y1, c); fill(x1, y0, x1, y1, c);
}

/* decode one UTF-8 code point; invalid bytes give 0xFFFFFFFF (drawn as '?') */
static uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = s[0];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (n == 0) { *p += 1; return 0xFFFFFFFFu; }
    uint32_t v = n == 1 ? c : c & (0xFF >> (n + 1));
    for (int k = 1; k < n; k++) {
        if ((s[k] & 0xC0) != 0x80) { *p += 1; return 0xFFFFFFFFu; }
        v = (v << 6) | (s[k] & 0x3F);
    }
    *p += n;
    return v;
}

static void glyph(int x, int y, uint32_t cp, int color, int scale) {
    const char *g = NULL;
    if (cp >= 0x20 && cp < 0x7F) g = font8x8_basic[cp];
    else if (cp >= 0xA0 && cp <= 0xFF) g = font8x8_ext_latin[cp - 0xA0];
    else g = font8x8_basic['?'];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (((unsigned char)g[row] >> col) & 1) fill(x + col * scale, y + row * scale, x + col * scale + scale - 1, y + row * scale + scale - 1, color);
}

static void text(int x, int y, const char *s, int color, int scale) {
    while (*s) { uint32_t cp = utf8_next(&s); glyph(x, y, cp, color, scale); x += 8 * scale; }
}

static int ncp(const char *s) { int n = 0; while (*s) { utf8_next(&s); n++; } return n; }

/* elision to n characters (14 section 7.1) */
static void elide(const char *s, int n, char *out, size_t on) {
    int len = ncp(s);
    if (len <= n) { snprintf(out, on, "%s", s); return; }
    int head = (n - 3 + 1) / 2, tail = (n - 3) / 2;
    const char *p = s;
    for (int i = 0; i < head; i++) utf8_next(&p);
    size_t hb = (size_t)(p - s);
    const char *q = s;
    for (int i = 0; i < len - tail; i++) utf8_next(&q);
    snprintf(out, on, "%.*s...%s", (int)hb, s, q);
}

static int row_color(int state) {
    switch (state) { case ROW_NONE: return 4; case ROW_CHECKING: return 8; case ROW_OK: case ROW_INSTALLED: return 6; default: return 7; }
}

static void draw_button(const SetupModel *m, int which, const char *label, int enabled) {
    int x0 = which == 1 ? 136 : 276, y0 = 330, x1 = x0 + 99, y1 = 353;
    int focused = m->focus == (which == 1 ? FOCUS_B1 : FOCUS_B2);
    int face = !enabled ? 11 : (pressing == which && mouse_held_prev) ? 10 : 9;
    fill(x0, y0, x1, y1, face);
    if (focused) { border(x0, y0, x1, y1, 5); border(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 5); }
    else border(x0, y0, x1, y1, 2);
    int len = ncp(label);
    text(x0 + (100 - 8 * len) / 2, 338, label, enabled ? 3 : 4, 1);
}

static void draw(const SetupModel *m) {
    memset(g_eng.work->px, 0, (size_t)CANVAS_W * CANVAS_H);
    text(112, 12, "OutNumbered! Setup", 5, 2);
    text(16, 40, "Drag and drop your OutNumbered! CD image (.iso, .bin/.cue)", 3, 1);
    text(16, 50, "or its folder, and optionally the MS-DOS version's .zip or", 3, 1);
    text(16, 60, "folder, onto this window. You can also type a path below.", 3, 1);
    text(16, 74, "Use files from your own copy of the game.", 4, 1);
    static const char *labels[2] = {"Windows CD (required)", "MS-DOS game (optional: Tandy / PC-speaker sound)"};
    for (int r = 0; r < 2; r++) {
        int y0 = r == 0 ? 92 : 140;
        fill(16, y0, 495, y0 + 39, 1);
        border(16, y0, 495, y0 + 39, 2);
        static const char *marker[5] = {"[ ]", "[~]", "[+]", "[+]", "[!]"};
        int st = m->row[r].state;
        int col = row_color(st);
        text(24, y0 + 8, marker[st == ROW_NONE ? 0 : st == ROW_CHECKING ? 1 : st == ROW_OK ? 2 : st == ROW_INSTALLED ? 3 : 4], col, 1);
        text(56, y0 + 8, labels[r], 3, 1);
        char status[900], el[900];
        model_row_status(m, r, status, sizeof status);
        elide(status, 54, el, sizeof el);
        text(56, y0 + 22, el, col, 1);
    }
    text(16, 199, "Path:", 3, 1);
    fill(64, 194, 495, 211, 12);
    border(64, 194, 495, 211, m->focus == FOCUS_FIELD ? 5 : 2);
    if (!m->field[0]) text(68, 199, "type or paste a path, then press Enter", 4, 1);
    else {
        int caret = m->caret, start = caret > 53 ? caret - 53 : 0;
        const char *p = m->field;
        for (int i = 0; i < start; i++) utf8_next(&p);
        int x = 68;
        for (int i = 0; i < 53 && *p; i++) { uint32_t cp = utf8_next(&p); glyph(x, 199, cp, 3, 1); x += 8; }
    }
    if (m->focus == FOCUS_FIELD && ((m->blink - blink0) % 100) < 50) {
        int caret = m->caret, col = caret > 53 ? 53 : caret;
        fill(68 + 8 * col, 198, 68 + 8 * col, 207, 5);
    }
    if (m->msg.kind != MK_NONE) {
        char el[900];
        elide(m->msg.text, 60, el, sizeof el);
        text(16, 222, el, m->msg.kind == MK_INFO ? 3 : m->msg.kind == MK_SUCCESS ? 6 : 7, 1);
    }
    if (m->phase == PH_INSTALLING && m->inst) {
        InstProgress p;
        inst_progress(m->inst, &p);
        char t[1200], el[1200];
        unsigned pct = p.total ? (unsigned)(100 * p.done / p.total) : 100;
        if (!strcmp(p.step, "commit")) snprintf(t, sizeof t, "Finishing...");
        else snprintf(t, sizeof t, "%s %s (%d/%d)  %u%%", !strcmp(p.step, "copy") ? "Copying" : "Verifying", p.file, p.index, p.count, pct);
        elide(t, 60, el, sizeof el);
        text(16, 244, el, 3, 1);
        border(16, 258, 495, 273, 2);
        int w = p.total ? (int)(476 * p.done / p.total) : 476;
        if (w > 0) fill(18, 260, 17 + w, 271, 6);
    }
    text(16, 292, "Files will be copied to:", 4, 1);
    char el[1100];
    elide(m->home, 60, el, sizeof el);
    text(16, 304, el, 3, 1);
    draw_button(m, 1, model_b1_label(m), model_b1_enabled(m));
    draw_button(m, 2, m->phase == PH_INSTALLING ? "Cancel" : "Quit", 1);
    text(36, 368, "Tab: next  Enter: select  Esc: quit (or cancel copying)", 4, 1);
}

/* ------------------------------------------------------------------ input */
static void restart_blink(SetupModel *m) { blink0 = m->blink; }

static size_t cp_offset(const char *s, int n) { const char *p = s; for (int i = 0; i < n; i++) utf8_next(&p); return (size_t)(p - s); }

static void field_insert(SetupModel *m, uint32_t cp) {
    char enc[5];
    int n = 0;
    if (cp < 0x80) enc[n++] = (char)cp;
    else if (cp < 0x800) { enc[n++] = (char)(0xC0 | (cp >> 6)); enc[n++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { enc[n++] = (char)(0xE0 | (cp >> 12)); enc[n++] = (char)(0x80 | ((cp >> 6) & 63)); enc[n++] = (char)(0x80 | (cp & 63)); }
    else { enc[n++] = (char)(0xF0 | (cp >> 18)); enc[n++] = (char)(0x80 | ((cp >> 12) & 63)); enc[n++] = (char)(0x80 | ((cp >> 6) & 63)); enc[n++] = (char)(0x80 | (cp & 63)); }
    size_t len = strlen(m->field);
    if (len + (size_t)n > 4096) return;
    size_t at = cp_offset(m->field, m->caret);
    memmove(m->field + at + n, m->field + at, len - at + 1);
    memcpy(m->field + at, enc, (size_t)n);
    m->caret++;
}

static int focus_enabled(const SetupModel *m, int f) {
    if (f == FOCUS_FIELD) return 1;
    if (f == FOCUS_B1) return model_b1_enabled(m);
    return 1;
}
static void focus_move(SetupModel *m, int dir) {
    for (int i = 0; i < 3; i++) {
        m->focus = (m->focus + dir + 3) % 3;
        if (focus_enabled(m, m->focus)) break;
    }
    restart_blink(m);
}

static void activate_b2(SetupModel *m) {
    if (m->phase == PH_INSTALLING) model_cancel(m);
    else m->want_quit = 1;
}

static void submit(SetupModel *m) {
    char *p = normalize_input(m->field, plat_script_dir());
    if (plat_mode() == PLAT_SDL) model_drop_async(m, &p, 1, 1);
    else model_submit_text(m, p);
    free(p);
}

static void key(SetupModel *m, int k) {
    if (m->want_start || m->want_quit) return;
    if (k >= 0x100000) { if (m->focus == FOCUS_FIELD) { field_insert(m, (uint32_t)(k & 0xFFFFF)); restart_blink(m); } return; }
    if (k == K_TAB) { focus_move(m, 1); return; }
    if (k == K_BACKTAB) { focus_move(m, -1); return; }
    int infield = m->focus == FOCUS_FIELD;
    if (k == K_ESC) {
        if (m->phase == PH_INSTALLING) model_cancel(m);
        else if (infield && m->field[0]) { m->field[0] = 0; m->caret = 0; restart_blink(m); }
        else m->want_quit = 1;
        return;
    }
    if (k == K_ENTER) {
        if (infield) {
            if (m->field[0]) submit(m);
            else if (model_b1_enabled(m)) model_activate_b1(m);
        } else if (m->focus == FOCUS_B1) { if (model_b1_enabled(m)) model_activate_b1(m); }
        else activate_b2(m);
        return;
    }
    if (k == K_SPACE) {
        if (infield) { field_insert(m, ' '); restart_blink(m); }
        else if (m->focus == FOCUS_B1) { if (model_b1_enabled(m)) model_activate_b1(m); }
        else activate_b2(m);
        return;
    }
    if (!infield) return;
    int len = ncp(m->field);
    if (k > 0x20 && k < 0x7F) { field_insert(m, (uint32_t)k); restart_blink(m); }
    else if (k == K_LEFT) { if (m->caret > 0) m->caret--; restart_blink(m); }
    else if (k == K_RIGHT) { if (m->caret < len) m->caret++; restart_blink(m); }
    else if (k == K_HOME) { m->caret = 0; restart_blink(m); }
    else if (k == K_END) { m->caret = len; restart_blink(m); }
    else if (k == K_BACKSPACE && m->caret > 0) {
        size_t a = cp_offset(m->field, m->caret - 1), b = cp_offset(m->field, m->caret);
        memmove(m->field + a, m->field + b, strlen(m->field + b) + 1);
        m->caret--;
        restart_blink(m);
    } else if (k == K_DELETE && m->caret < len) {
        size_t a = cp_offset(m->field, m->caret), b = cp_offset(m->field, m->caret + 1);
        memmove(m->field + a, m->field + b, strlen(m->field + b) + 1);
        restart_blink(m);
    }
}

static int hit(int x, int y) {
    if (x >= 64 && x <= 495 && y >= 194 && y <= 211) return 0;
    if (x >= 136 && x <= 235 && y >= 330 && y <= 353) return 1;
    if (x >= 276 && x <= 375 && y >= 330 && y <= 353) return 2;
    return -1;
}

static void press_at(SetupModel *m, int x, int y) {
    int h = hit(x, y);
    if (h == 0) { m->focus = FOCUS_FIELD; m->caret = ncp(m->field); restart_blink(m); }
    else if (h == 1 && model_b1_enabled(m)) { m->focus = FOCUS_B1; pressing = 1; }
    else if (h == 2) { m->focus = FOCUS_B2; pressing = 2; }
    if (h >= 1 && plat_mode() == PLAT_SCRIPT && pressing) {          /* a scripted click is a press and a release */
        int b = pressing;
        pressing = 0;
        if (b == 1) model_activate_b1(m); else activate_b2(m);
    }
}

static void process_input(SetupModel *m) {
    int k;
    while ((k = plat_key_pop()) != 0) key(m, k);
    int x, y;
    while (plat_click_pop(&x, &y)) press_at(m, x, y);
    if (plat_mode() == PLAT_SDL) {
        int mx, my, left;
        plat_mouse_state(&mx, &my, &left);
        if (pressing && mouse_held_prev && !left) {
            int h = hit(mx, my), b = pressing;
            pressing = 0;
            if (h == b) { if (b == 1) { if (model_b1_enabled(m)) model_activate_b1(m); } else activate_b2(m); }
        }
        mouse_held_prev = left;
    }
    char **paths;
    int n;
    while (plat_drop_pop(&paths, &n)) {
        if (plat_mode() == PLAT_SDL) model_drop_async(m, paths, n, 0);
        else model_drop(m, paths, n);
        for (int i = 0; i < n; i++) free(paths[i]);
        free(paths);
    }
}

/* one 100 Hz step: input, install work, redraw */
static void screen_step(SetupModel *m) {
    process_input(m);
    model_poll_async(m);
    model_tick(m);
    draw(m);
    plat_present();
    if (m->want_start || m->want_quit) g_script_hooks.hold = 1;       /* script: the screen is leaving, later commands wait */
}

static void pump_hook(void) {
    if (!G) return;
    process_input(G);
    draw(G);
    plat_present();
}

static void tick_hook(void) { if (G) screen_step(G); }

static void dump_hook(FILE *f) {
    fprintf(f, " \"screen\": \"setup\",\n ");
    model_state_json(G, f);
}

/* ------------------------------------------------------------------ the screen */
int setup_screen_run(const SetupOpts *opts, int load_failed) {
    (void)opts;
    if (!g_eng.work) eng_init();
    for (int i = 0; i < 13; i++) memcpy(g_pal[i], PAL[i], 3);
    SetupModel *m = xcalloc(1, sizeof *m);
    model_init(m, g_run.home, load_failed);
    /* the reason the screen opened (14 section 7.3) */
    if (!load_failed && g_run_valid && (!strcmp(g_run.reason, "bad_manifest") || !strcmp(g_run.reason, "missing_file") || !strcmp(g_run.reason, "size_mismatch") || !strcmp(g_run.reason, "crc_mismatch"))) {
        m->msg.kind = MK_ERROR;
        snprintf(m->msg.code, sizeof m->msg.code, "%s", g_run.reason);
        snprintf(m->msg.text, sizeof m->msg.text, "The installed game files are damaged or incomplete (%s). Drop the CD again.", !strcmp(g_run.reason, "bad_manifest") ? "install.json" : g_run.detail);
    }
    G = m;
    g_active = 1;
    pressing = 0;
    mouse_held_prev = 0;
    blink0 = 0;
    plat_key_flush();
    plat_click_flush();
    plat_drops_accept(1);
    g_script_hooks.pump = pump_hook;
    g_script_hooks.tick = plat_mode() == PLAT_SCRIPT ? tick_hook : NULL;
    g_script_hooks.hold = 0;
    void (*old_dump)(FILE *) = g_script_hooks.dump_state;
    g_script_hooks.dump_state = dump_hook;
    if (plat_mode() == PLAT_SDL) { m->units_per_tick = 4u << 20; plat_sdl_set_title("OutNumbered! Setup"); }
    int rc = 0;
    draw(m);
    plat_present();
    for (;;) {
        if (plat_mode() == PLAT_SDL) screen_step(m);              /* script mode: the ticks run inside plat_wait (tick_hook) */
        if (m->want_start) { rc = 1; break; }
        if (m->want_quit) { rc = 0; break; }
        plat_wait(1);
        if (m->want_start) { rc = 1; break; }
        if (m->want_quit) { rc = 0; break; }
    }
    if (m->phase == PH_INSTALLING) model_cancel(m);
    plat_drops_accept(0);
    g_script_hooks.pump = NULL;
    g_script_hooks.tick = NULL;
    g_script_hooks.hold = 0;
    g_script_hooks.dump_state = old_dump;
    g_active = 0;
    G = NULL;
    model_free(m);
    free(m);
    return rc;
}
