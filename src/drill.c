/* drill.c - drill questions inside Telly's belly and Drill for Skill (spec 05 section 2, 07 A section 4) */
#include "game.h"

#define ANCHOR_X 328
#define ANCHOR_Y 210

static int dly_cs(int n) { return (n * 4 / 13) * 100 / 73; }

static const int OFF8604[13][2] = {{0, 0}, {-1, -6}, {-73, -82}, {-68, -83}, {-75, -86}, {-74, -79}, {-77, -87},
                                   {-72, -85}, {-84, -95}, {-168, -138}, {-197, -148}, {-197, -148}, {-197, -148}};

typedef struct DrillCtx {
    int main_game;
    int64_t clock_dl;
    int64_t sec_dl;           /* DfS elapsed-time deadline */
    int dfs_seconds, dfs_minutes;
    int asked, correct;
    int correct_set;
    int aborted;
    int hint9_done;
} DrillCtx;
static DrillCtx dc;

static void d_stamp(int cel) {
    Aseq *a = aseq_load(8604);
    if (!a || cel < 0 || cel >= 13) return;
    ui_stamp(a, cel, ANCHOR_X + OFF8604[cel][0], ANCHOR_Y + OFF8604[cel][1], 0);
}

/* wait `cs` centiseconds; in the main game the HUD clock keeps running at 1 minute / 240 cs */
static void d_wait(int cs) {
    int64_t end = plat_now() + cs;
    while (plat_now() < end) {
        int64_t step = end - plat_now();
        if (dc.main_game && ses.clock_on) {
            int64_t to_clock = dc.clock_dl - plat_now();
            if (to_clock <= 0) {
                clock_add_minutes(1);
                dc.clock_dl = plat_now() + 240;
                if (gs.clock >= 1200) out_of_time();
                continue;
            }
            if (to_clock < step) step = to_clock;
        }
        plat_wait_until(plat_now() + (step > 0 ? step : 1));
    }
}
static void d_wait_units(int n) { d_wait(dly_cs(n)); }

static void add_score(int n) {
    gs.score = (uint16_t)(gs.score + n);
    hud_score();
    snd_play(1747, 2, 0);
}

static void tv_static(void) {
    for (int i = 0; i < 8; i++) {
        int64_t t0 = plat_now();
        snd_play(1752, 10, 0);                 /* requested on each frame; a request while it still plays is ignored */
        d_stamp(10 + (i & 1));
        plat_present();
        while (plat_now() < t0 + 8) plat_wait(1);
    }
    d_stamp(12);
    plat_present();
}

static int dfs_paused;
static int64_t last_tick, tick_dl;
static int dfs_sec, dfs_min;

static void draw_screen(void) {
    dbg_screen = "drill";
    hud_rebuild();
    ui_stamp_picture(8600, 13, 36, ST_BACK);
    ui_stamp_picture(8540, 140, 301, ST_BACK);          /* numeric keypad (both drills) */
    if (!dc.main_game) {
        ui_stamp_picture(8605, 326, 297, 0);
        ui_stamp(aseq_load(200), 64, 465, 331, 0);
        ui_stamp(aseq_load(200), 64, 465, 354, 0);
    } else {
        ui_stamp(aseq_load(6002), 7, 33, 312, ST_BACK);
    }
}

/* ---- DfS counters (digit cels 64..73) */
static void put_digits(int value, const int *xs, int n, int y, int always_first) {
    Aseq *h = aseq_load(200);
    char buf[16];
    snprintf(buf, sizeof buf, "%d", value);
    int len = (int)strlen(buf);
    for (int i = 0; i < n && i < len; i++) ui_stamp(h, 64 + (buf[len - 1 - i] - '0'), xs[i], y, 0);
    (void)always_first;
}
static void draw_problems(void) { static const int xs[4] = {465, 451, 437, 423}; put_digits(dc.asked % 10000, xs, 4, 331, 1); }
static void draw_correct(void) { static const int xs[4] = {465, 451, 437, 423}; put_digits(dc.correct % 10000, xs, 4, 354, 1); }
static void draw_time(void) {
    static const int xm[3] = {433, 419, 405};
    Aseq *h = aseq_load(200);
    if (dfs_min >= 1) put_digits(dfs_min, xm, 3, 308, 0);
    else ui_fill(405, 308, 441, 320, 0, 0);
    ui_stamp(h, 64 + dfs_sec / 10, 451, 308, 0);
    ui_stamp(h, 64 + dfs_sec % 10, 465, 308, 0);
    ui_fill(445, 311, 446, 312, 137, 0);
    ui_fill(445, 316, 446, 317, 137, 0);
}
static void dfs_tick(void) {
    last_tick = plat_now();
    tick_dl = last_tick + 101;
    if (++dfs_sec >= 60) { dfs_sec = 0; dfs_min = (dfs_min + 1) % 1000; }
    draw_time();
    plat_present();
}

/* PAUSE cel while paused (menus, dialogs, hints, Esc, end of set) */
void drill_pause_hook(int on) {
    if (dc.main_game) return;
    if (on) { if (dfs_paused++ == 0) { ui_stamp(aseq_load(200), 76, 402, 303, 0); plat_present(); } }
    else if (dfs_paused > 0 && --dfs_paused == 0) { ui_fill(403, 304, 480, 322, 0, 0); plat_present(); }
}

/* ---- question line (spec 07 A section 4, v1.17) ---- */
static int sx(int v) { return (v * 16 + 5) / 10; }
static int sy(int v) { return (v * 192 + 50) / 100; }
static void du_fill(int x1, int y1, int x2, int y2, int colour) {   /* d.u. fill: size converted, not the far edge */
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    int px = sx(x1), py = sy(y1);
    ui_fill(px, py, px + sx(w) - 1, py + sy(h) - 1, colour, 0);
}
static void clear_question(void) { ui_fill(145, 80, 364, 159, C_SCREENBLACK, 0); }

typedef struct QLayout { int x[5]; int wsp; int fx; } QLayout;
static void q_layout(const Question *q, const char *buf, QLayout *L) {
    int font = F_LARGE;
    char sa[8], sb[8], sc[8];
    snprintf(sa, sizeof sa, "%d", q->a); snprintf(sb, sizeof sb, "%d", q->b); snprintf(sc, sizeof sc, "%d", q->c);
    const char *p[3] = {sa, sb, sc};
    int ukpos = q->format == 1 ? 0 : q->format == 2 ? 1 : 2;
    int w[3];
    for (int i = 0; i < 3; i++) w[i] = ui_text_w(font, i == ukpos ? buf : p[i]);
    char opc[2] = {q->fact.op == OP_ADD ? '+' : q->fact.op == OP_MUL ? '*' : q->fact.op == OP_SUB ? '-' : '/', 0};
    int sp = ui_text_w(font, " "), wop = ui_text_w(font, opc);
    int W = w[0] + wop + w[1] + w[2] + 4 * sp + 7;      /* the "=" is not counted */
    int left = 164 - W / 2;
    L->wsp = sp;
    L->x[0] = left;                                     /* A */
    L->x[1] = left + w[0] + sp;                         /* op */
    L->x[2] = left + w[0] + wop + 2 * sp;               /* B */
    L->x[3] = left + w[0] + wop + w[1] + 3 * sp;        /* "=" */
    L->x[4] = left + w[0] + wop + w[1] + 4 * sp + 7;    /* C */
    L->fx = ukpos == 0 ? L->x[0] : ukpos == 1 ? L->x[2] : L->x[4];
}
/* buffer string: the entry padded with spaces to 3 characters */
static void pad3(const char *e, char *out) { snprintf(out, 8, "%-3s", e); }

/* redraw = question-area clear + the five parts */
static void draw_question(const Question *q, const char *buf, int unknown_color) {
    int font = F_LARGE;
    QLayout L;
    q_layout(q, buf, &L);
    clear_question();
    char sa[8], sb[8], sc[8];
    snprintf(sa, sizeof sa, "%d", q->a); snprintf(sb, sizeof sb, "%d", q->b); snprintf(sc, sizeof sc, "%d", q->c);
    const char *p[3] = {sa, sb, sc};
    int ukpos = q->format == 1 ? 0 : q->format == 2 ? 1 : 2;
    char opc[2] = {q->fact.op == OP_ADD ? '+' : q->fact.op == OP_MUL ? '*' : q->fact.op == OP_SUB ? '-' : '/', 0};
    int xa[3] = {L.x[0], L.x[2], L.x[4]};
    for (int i = 0; i < 3; i++) ui_text(font, sx(xa[i]), 144, i == ukpos ? buf : p[i], i == ukpos ? unknown_color : C_WHITE);
    ui_text(font, sx(L.x[1]), 144, opc, C_WHITE);
    ui_text(font, sx(L.x[3]), 144, "=", C_WHITE);
}

static void draw_cursor(const Question *q, const char *entry, int len, int colour) {
    char buf[8];
    pad3(len ? entry : "?", buf);
    QLayout L;
    q_layout(q, buf, &L);
    int wd = len ? ui_text_w(F_LARGE, entry) : 0;
    int dux = len ? L.fx + wd * 625 / 1000 + 1 : L.fx;
    int clen = sx(ui_text_w(F_LARGE, "?") * 625 / 1000);
    int px = sx(dux), py = sy(84);
    ui_fill(px, py, px + clen - 1, py, colour, 0);
}

/* keypad rectangles (07 D.6) */
static int keypad_key(int cx, int cy) {
    static const struct { int x, y, w, h, key; } K[] = {
        {204, 305, 15, 17, '7'}, {225, 305, 15, 17, '8'}, {246, 305, 15, 17, '9'},
        {204, 329, 15, 17, '4'}, {225, 329, 15, 17, '5'}, {246, 329, 15, 17, '6'},
        {204, 353, 15, 17, '1'}, {225, 353, 15, 17, '2'}, {246, 353, 15, 17, '3'},
        {183, 353, 15, 17, '0'}, {144, 330, 15, 17, 'c'}, {144, 305, 54, 18, K_ENTER},
    };
    for (int i = 0; i < ARRAY_LEN(K); i++)
        if (cx >= K[i].x && cx < K[i].x + K[i].w && cy >= K[i].y && cy < K[i].y + K[i].h) return K[i].key;
    return 0;
}

/* Run one set of n questions. Returns number correct; sets dc.aborted when left early (Telly drill). */
static int run_set(DrillDeck *deck, const uint8_t *cust, int n) {
    int correct = 0;
    deck_reset_remaining(deck);
    d_wait(49);                                   /* only before the first problem of the set */
    for (int qi = 0; qi < n; qi++) {
        /* button press: timer, cel 0 presented, sound, rest of the 16 cs, cel 1 */
        int64_t t0 = plat_now();
        d_stamp(0);
        plat_present();
        snd_play(1705, 2, 0);
        while (plat_now() < t0 + 16) d_wait(1);
        d_stamp(1);
        plat_present();
        Question q;
        question_make(&q, deck, cust);
        dc.asked++;
        if (!dc.main_game) { draw_problems(); plat_present(); }
        d_wait(49);
        snprintf(dbg_drill.display, sizeof dbg_drill.display, "%s", q.display);
        dbg_drill.answer = q.answer; dbg_drill.asked = dc.asked; dbg_drill.correct = dc.main_game ? correct : dc.correct;
        dbg_drill.required = dc.main_game ? n - 1 : n;
        char entry[8] = "";
        int len = 0;
        char buf[8];
        pad3("?", buf);
        draw_question(&q, buf, C_WHITE);
        int cursor_on = 1;
        int64_t cursor_t = plat_now();
        draw_cursor(&q, entry, len, C_WHITE);
        plat_present();
        plat_key_flush();
        plat_click_flush();
        int submitted = 0, typed = 0;
        int64_t start = plat_now();
        int hint_pending = (!dc.hint9_done);
        while (!submitted) {
            int k = plat_key_pop();
            int cx, cy;
            if (!k && plat_click_pop(&cx, &cy)) {
                if (dc.main_game && cx >= 33 && cx < 99 && cy >= 312 && cy < 366) { dc.aborted = 1; return correct; }
                int kk = keypad_key(cx, cy);
                if (!kk) continue;
                snd_play(1756, 2, 0);
                k = kk;
            }
            if (!k) {
                if (hint_pending && !typed && plat_now() - start >= 300) {
                    hint_pending = 0;
                    dc.hint9_done = 1;
                    if (gs.hints[9] != 0xFF) hint_show(9, 0);
                    plat_key_flush();
                }
                if (!dc.main_game && plat_now() - last_tick >= 74 && !dfs_paused) dfs_tick();
                if (plat_now() - cursor_t >= 50) {             /* blink every 50 cs */
                    cursor_on = !cursor_on;
                    cursor_t = plat_now();
                    draw_cursor(&q, entry, len, cursor_on ? C_WHITE : C_SCREENBLACK);
                    plat_present();
                }
                d_wait(1);
                continue;
            }
            if (cursor_on) draw_cursor(&q, entry, len, C_SCREENBLACK);   /* erased before the key is handled */
            cursor_on = 0;
            if (!dc.main_game && plat_now() >= tick_dl && !dfs_paused) dfs_tick();
            if (k == K_ESC) {
                if (dc.main_game) { dc.aborted = 1; return correct; }
                clock_pause();
                int yes = dlg_yesno(game_signed_in() ? 23008 : 23013, 0);
                if (yes) game_quit();
                clock_resume();
                continue;
            }
            if (k >= '0' && k <= '9') {
                if (hint_pending) { hint_pending = 0; dc.hint9_done = 1; gs.hints[9] = 0xFF; }
                typed = 1;
                if (len < 3) { entry[len++] = (char)k; entry[len] = 0; }
                pad3(len ? entry : "?", buf);
                draw_question(&q, buf, C_WHITE);
            } else if (k == K_BACKSPACE || k == K_DELETE) {
                QLayout L;
                pad3(len ? entry : "?", buf);
                q_layout(&q, buf, &L);
                du_fill(L.fx, 71, L.fx + L.wsp, 105, C_SCREENBLACK);      /* starts at the field's left edge */
                if (len > 0) entry[--len] = 0;
                pad3(len ? entry : "?", buf);
                draw_question(&q, buf, C_WHITE);
            } else if (k == 'c' || k == 'C') {
                QLayout L;
                pad3(len ? entry : "?", buf);
                q_layout(&q, buf, &L);
                for (int d = 0; d < len; d++) du_fill(L.fx, 71, L.fx + L.wsp, 105, C_SCREENBLACK);
                len = 0; entry[0] = 0;
                pad3("?", buf);
                draw_question(&q, buf, C_WHITE);
            } else if (k == K_ENTER || k == 3) {
                if (len == 0) continue;
                submitted = 1;
                break;
            } else continue;
            cursor_on = 1;
            cursor_t = plat_now();
            draw_cursor(&q, entry, len, C_WHITE);
            plat_present();
        }
        int val = atoi(entry);
        if (question_check(&q, val)) {
            correct++;
            dc.correct_set = correct;
            if (dc.main_game) dbg_drill.correct = correct;
            static const char *P[5] = {"That's correct!", "That's the answer!", "That's right!", "You're right!", "Good job!"};
            const char *praise = P[rnga_scaled(5)];
            int pw = ui_text_w(F_SERIF, praise), px0 = 256 - pw / 2;
            ui_text(F_SERIF, px0, 107, praise, C_WHITE);
            snd_play(1744, 2, 0);
            plat_present();
            d_wait(100);
            if (dc.main_game) add_score(50); else { dc.correct++; dbg_drill.correct = dc.correct; draw_correct(); snd_play(1747, 2, 0); plat_present(); }
            d_wait(100);
            clear_question();
            ui_fill(px0, 107, px0 + pw, 115, C_SCREENBLACK, 0);
            plat_present();
        } else {
            snd_play(1761, 2, 0);
            ui_text(F_SERIF, 200, 190, "That's incorrect.", C_WHITE);
            plat_present();
            d_wait(201);
            int mw = ui_text_w(F_SERIF, "That's incorrect.");
            ui_fill(200, 187, 200 + mw, 198, C_SCREENBLACK, 0);
            plat_present();
            /* without presenting in between: clear the answer slot area, redraw with the correct answer in red */
            char ab[8], tb[8];
            pad3(len ? entry : "?", ab);
            QLayout L;
            q_layout(&q, ab, &L);
            du_fill(L.fx, 72, L.fx + ui_text_w(F_LARGE, "000") + 2, 105, C_SCREENBLACK);
            snprintf(tb, sizeof tb, "%d", q.answer);
            draw_question(&q, tb, C_RED);               /* slot laid out with the width of the answer string itself */
            plat_present();
            d_wait(302);
        }
        clear_question();              /* not presented on its own; shown with the next press */
    }
    return correct;
}

int drill_run_telly(void) {
    memset(&dc, 0, sizeof dc);
    dc.main_game = 1;
    dc.clock_dl = plat_now() + 240;
    int n = 2 * ses.tier + 4;
    draw_screen();
    plat_present();
    tv_static();
    static DrillDeck deck;
    deck_build(&deck, ses.cust_game);
    dc.hint9_done = gs.hints[9] == 0xFF;
    int correct = run_set(&deck, ses.cust_game, n);
    int won = correct >= n - 1;
    if (won) {
        int slot = -1;
        for (int i = 0; i < 4; i++) if (gs.code[i] & 0x80) { slot = i; break; }
        if (slot >= 0) {
            clear_question();
            Aseq *a = aseq_load(8604);
            int prev = -1;
            for (int c = 3; c <= 9; c++) {
                int64_t t0 = plat_now();
                if (prev >= 0 && a) {
                    const Cel *pc = aseq_cel(a, prev);
                    if (pc) ui_restore_rect(ANCHOR_X + OFF8604[prev][0], ANCHOR_Y + OFF8604[prev][1], pc->w, pc->h);
                }
                d_stamp(c);
                plat_present();
                snd_play(1709, 2, 0);
                while (plat_now() < t0 + 20) d_wait(1);
                prev = c;
            }
            snd_stop(1709);
            snd_play(1710, 2, 0);
            gs.code[slot] &= 0x7F;
            gs.pieces++;
            ui_stamp(aseq_load(200), 78 + (gs.code[slot] & 0x7F), 242, 125, 0);
            plat_present();
            add_score(100);
            d_wait(302);
        }
    } else {
        tv_static();
        snd_play(1711, 2, 0);
        ui_text(F_SERIF, 163, 134, "No Code.   Catch me again!", C_WHITE);
        plat_present();
        d_wait(201);
    }
    return won;
}

/* ---- Drill for Skill ---- */
typedef struct Fr { int cel, col, hold; } Fr;

static void play_script(const Fr *f, int n, int quirk_erase_idx_for_50) {
    Aseq *h = aseq_load(200);
    int prev = -1;
    for (int i = 0; i < n; i++) {
        if (prev >= 0) {
            int e = prev;
            if (quirk_erase_idx_for_50 && i == 49) e = 33;      /* frame 50 erases frame 34's rectangle */
            const Cel *pc = aseq_cel(h, f[e].cel);
            if (pc) ui_restore_rect(px_x(f[e].col / 2), 79, pc->w, pc->h);
        }
        const Cel *c = aseq_cel(h, f[i].cel);
        if (c) canvas_blit_cel(g_eng.work, c, px_x(f[i].col / 2), 79, 0, NULL);
        plat_present();
        plat_wait(f[i].hold * 12);
        prev = i;
    }
}

static int build_script_P(Fr *f) {
    int n = 0;
    for (int i = 0; i < 15; i++) f[n++] = (Fr){i % 2, 20 + 2 * i, 1};
    f[n++] = (Fr){0, 48, 1};
    static const int flip[6] = {34, 35, 36, 37, 38, 39};
    for (int i = 0; i < 12; i++) f[n++] = (Fr){flip[i % 6], 46 - 2 * i, 1};
    static const int back[7] = {34, 35, 39, 38, 37, 36, 34};
    for (int i = 0; i < 7; i++) f[n++] = (Fr){back[i], 22 + 2 * i, 1};
    f[n++] = (Fr){0, 34, 1};
    static const int arms[5] = {47, 48, 49, 48, 47};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 28, 1};
    f[n++] = (Fr){0, 34, 1};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 28, 1};
    f[n++] = (Fr){0, 34, 1};
    f[n++] = (Fr){0, 34, 1};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 34, 1};
    f[n++] = (Fr){0, 34, 1};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 34, 1};
    f[n++] = (Fr){0, 34, 1};
    f[n++] = (Fr){18, 34, 10};
    return n;
}
static int build_script_A(Fr *f) {
    int n = 0;
    static const int flip[6] = {34, 35, 36, 37, 38, 39};
    for (int i = 0; i < 12; i++) f[n++] = (Fr){flip[i % 6], 20 + 2 * i, 1};
    f[n++] = (Fr){0, 42, 1};
    for (int i = 0; i < 6; i++) f[n++] = (Fr){flip[i], 42 - 2 * i, 1};
    f[n++] = (Fr){0, 32, 1};
    f[n++] = (Fr){18, 32, 10};
    return n;
}
static int build_script_B(Fr *f) {
    int n = 0;
    static const int arms[5] = {47, 48, 49, 48, 47};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 34, 1};
    f[n++] = (Fr){0, 34, 1};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 28, 1};
    f[n++] = (Fr){0, 34, 1};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 34, 1};
    f[n++] = (Fr){0, 34, 1};
    for (int i = 0; i < 5; i++) f[n++] = (Fr){arms[i], 28, 1};
    f[n++] = (Fr){0, 34, 1};
    f[n++] = (Fr){18, 34, 10};
    return n;
}

static void dfs_continue(void) {
    ui_stamp_picture(1012, 193, 180, 0);
    plat_present();
    for (;;) {
        int k = plat_key_pop(), cx, cy;
        if (!k && plat_click_pop(&cx, &cy)) return;               /* a click anywhere continues */
        if (!k) { plat_wait(2); continue; }
        if (k == K_ENTER || k == K_ESC) return;                  /* Esc continues here, no Exit confirmation */
    }
}

void drill_for_skill(void) {
    menu_set_current(2);
    memset(&dc, 0, sizeof dc);
    dc.main_game = 0;
    ses.drill_mode = 1;
    ses.player = -1;
    ses.tier = 0;
    memcpy(ses.cust_drill, tier_cust[0], 13);      /* tier defaults; the stored DfS block is never used */
    ses.hints = 1;                                   /* Trainee */
    memset(gs.hints, 0, sizeof gs.hints);
    dfs_paused = 0;
    dfs_sec = dfs_min = 0;
    last_tick = 0;
    tick_dl = 0;
    snd_stop_all();
    music_stop();
    draw_screen();
    plat_present();
    tv_static();
    static DrillDeck deck;
    for (;;) {
        int n = 5 * (ses.cust_drill[0] & 3) + 5;
        deck_build(&deck, ses.cust_drill);
        int correct = run_set(&deck, ses.cust_drill, n);
        /* end of set */
        drill_pause_hook(1);
        Fr fr[80];
        if (correct == n) {
            if (ses.music) music_play(1757, 0);
            int cnt = build_script_P(fr);
            play_script(fr, cnt, 1);
            if (ses.music) music_wait();
        } else {
            int k = rnga_scaled(2);
            int id = k == 0 ? 1734 : 1718;
            if (ses.music) {
                audio_set_switches(1, ses.music, ses.voice);     /* the jingle plays even with Sound Effects off */
                snd_play(id, 2, 0);
                dosint_sfx_keep(1);                              /* a DOS jingle keeps playing */
                audio_set_switches(ses.sfx, ses.music, ses.voice);
                dosint_sfx_keep(0);
            }
            int cnt = k == 0 ? build_script_A(fr) : build_script_B(fr);
            play_script(fr, cnt, 0);
            if (ses.music) snd_wait(id);
        }
        dfs_continue();
        tv_static();
        drill_pause_hook(0);
    }
}
