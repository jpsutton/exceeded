/* signin.c - the sign-in screen (spec 03 section 2.3) */
#include "game.h"

#define LIST_X 87
#define LIST_Y 108
#define LIST_W 312
#define ROW_H 22
#define ROWS 6

typedef struct Row { char name[10]; long score; int real; int idx; } Row;
static Row rows[100];
static int nrows;
static char typed[10];
static int prov;            /* 0 none, 1 provisional entry present */
static int sel, top;
static Aseq *btn[4];

/* The OPTIONS button (spec 12 section 6.1): the CANCEL cels with the label erased and "OPTIONS" drawn at run time. */
static void make_options_art(Aseq *a) {
    static int made;
    if (made || !a) return;
    Font *f = font_load(F_SMALL);
    if (!f) return;
    static const int letter[3] = {135, 137, 77}, shadow[3] = {42, 42, 67};
    for (int n = 0; n < 3; n++) {
        const Cel *cc = aseq_cel(a, n);
        if (!cc || cc->w < 67 || cc->h < 40) continue;
        Cel *c = (Cel *)cc;
        int w = c->w, h = c->h;
        for (int y = 16; y <= 27; y++)
            for (int x = 5; x <= 66; x++) c->pix[y * w + x] = c->pix[(y + 12) * w + x];
        uint8_t *mask = xcalloc((size_t)w * h, 1);
        font_draw_mask(f, mask, w, h, 10, 27, "OPTIONS", NULL);
        static const int off[4][2] = {{-1, 0}, {0, -1}, {-1, -1}, {0, 0}};
        for (int o = 0; o < 4; o++) {
            int col = o < 3 ? shadow[n] : letter[n];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    if (!mask[y * w + x]) continue;
                    int dx = x + off[o][0], dy = y + off[o][1];
                    if (dx >= 0 && dx < w && dy >= 0 && dy < h) c->pix[dy * w + dx] = (uint8_t)col;
                }
        }
        free(mask);
    }
    made = 1;
}

static int cmp_rows(const void *a, const void *b) { return strcmp(((const Row *)a)->name, ((const Row *)b)->name); }

static void rebuild(void) {
    nrows = 0;
    int n = savedb_count(&ses.db);
    for (int i = 0; i < n && nrows < 99; i++) {
        Row *r = &rows[nrows++];
        memset(r, 0, sizeof *r);
        memcpy(r->name, savedb_name(&ses.db, i), 9);
        r->name[9] = 0;
        r->score = (long)savedb_score(&ses.db, i);
        r->real = 1;
        r->idx = i;
    }
    if (prov) {
        Row *r = &rows[nrows++];
        memset(r, 0, sizeof *r);
        snprintf(r->name, sizeof r->name, "%s", typed);
        r->score = typed[0] ? -1 : -2;
        r->real = 0;
        r->idx = -1;
    }
    qsort(rows, (size_t)nrows, sizeof(Row), cmp_rows);
    /* the provisional entry sorts by its typed text; the blank one goes first */
}

/* CANCEL's old rule: the list is not empty and a provisional entry exists */
static int cancel_enabled(void) { return nrows > 0 && prov; }

static int sel_is_prov(void) { return sel >= 0 && sel < nrows && !rows[sel].real; }

static void select_prov(void) { for (int i = 0; i < nrows; i++) if (!rows[i].real) sel = i; }

static void keep_visible(void) {
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    if (top < 0) top = 0;
}

static int btn_enabled(int b) {
    switch (b) {
    case 0: return (sel >= 0 && sel < nrows && (rows[sel].real || typed[0]));   /* start */
    case 1: return !sel_is_prov();                                               /* new */
    case 2: return 1;                                                            /* OPTIONS: always enabled (replaces CANCEL) */
    default: return 1;                                                           /* drill */
    }
}

static void draw(int pressed) {
    canvas_fill(g_eng.work, 0);
    ui_stamp_picture(16000, 0, 0, 0);
    static const int bx[4] = {164, 241, 318, 395}, by[4] = {331, 331, 331, 330};
    for (int b = 0; b < 4; b++) ui_stamp(btn[b], b == pressed ? 1 : (btn_enabled(b) ? 0 : 2), bx[b], by[b], 0);
    int font = F_KEYS;
    for (int i = 0; i < ROWS && top + i < nrows; i++) {
        Row *r = &rows[top + i];
        int y = 108 + ROW_H * i;
        int on = (top + i == sel);
        if (on) ui_fill(87, y - 2, 399, y + 20, 0, 0);
        int col = on ? C_CYAN : C_WHITE;
        int n = (int)strlen(typed);
        if (on && n > 0) {
            char head[10];
            snprintf(head, sizeof head, "%.*s", n, r->name);
            int hx = ui_text(font, 97, y + 1, head, C_YELLOW);
            if ((int)strlen(r->name) > n) ui_text(font, hx, y + 1, r->name + n, C_CYAN);
        } else ui_text(font, 97, y + 1, r->name, col);
        if (r->score >= 0) {
            char t[24];
            ui_text(font, 227, y + 1, rank_names[rank_for_score((uint32_t)r->score)], col);
            ui_commas(t, sizeof t, r->score);
            ui_text_right(font, 387, y + 1, t, col);
        }
    }
    ui_stamp(aseq_load(16005), 0, 400, 108, 0);
    ui_stamp(aseq_load(16006), 0, 400, 218, 0);
    plat_present();
}

static void type_char(int c) {
    size_t l = strlen(typed);
    if (l >= 8) return;
    if (l == 0 && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return;
    if (c >= 'a' && c <= 'z') c -= 32;
    char t[10];
    snprintf(t, sizeof t, "%s%c", typed, c);
    if (ui_text_w(F_KEYS, t) >= 100) return;
    snprintf(typed, sizeof typed, "%s", t);
    /* jump to the first name that matches the typed prefix; else a provisional entry */
    int exact = 0;
    for (int i = 0; i < savedb_count(&ses.db); i++) if (!strcmp(savedb_name(&ses.db, i), typed)) exact = 1;
    prov = !exact;
    rebuild();
    for (int i = 0; i < nrows; i++) if (!strncmp(rows[i].name, typed, strlen(typed))) { sel = i; break; }
    if (prov) select_prov();
    keep_visible();
}
static void backspace(void) {
    size_t l = strlen(typed);
    if (!l) return;
    typed[l - 1] = 0;
    if (!typed[0]) { prov = 1; rebuild(); select_prov(); return; }   /* blank entry stays (NEW PLAYER) */
    int exact = 0;
    for (int i = 0; i < savedb_count(&ses.db); i++) if (!strcmp(savedb_name(&ses.db, i), typed)) exact = 1;
    prov = !exact;
    rebuild();
    for (int i = 0; i < nrows; i++) if (!strncmp(rows[i].name, typed, strlen(typed))) { sel = i; break; }
    if (prov) select_prov();
    keep_visible();
}

/* returns 1 if signed in */
static int do_start(void) {
    if (!btn_enabled(0)) return 0;
    if (sel_is_prov() || (typed[0] && !rows[sel].real)) {
        char nm[10];
        snprintf(nm, sizeof nm, "%s", typed);
        for (int l = (int)strlen(nm); l > 0 && nm[l - 1] == ' '; l--) nm[l - 1] = 0;
        if (!nm[0]) return 0;
        if (savedb_find(&ses.db, nm) >= 0) { dlg_show(11000, 0); return 0; }
        int idx = savedb_add(&ses.db, nm);
        if (idx < 0) { dlg_show(23012, 0); return 0; }
        ses.player = idx;
        ses.calc = 1; ses.hints = 1; ses.clock_on = 1; ses.sfx = ses.music = ses.voice = 1;
        ses_write_record();
        return 1;
    }
    ses.player = rows[sel].idx;
    uint8_t *r = ses_rec();
    ses.sfx = r[REC_SFX]; ses.music = r[REC_MUSIC]; ses.voice = r[REC_VOICE];
    ses.calc = r[REC_OPTIONS] & 1; ses.hints = (r[REC_OPTIONS] >> 1) & 1; ses.clock_on = (r[REC_OPTIONS] >> 2) & 1;
    return 1;
}

int signin_run(void) {
    dbg_screen = "signin";
    menu_set_current(0);
    memset(&gs, 0, sizeof gs);
    gs_live = 0;
    puzzle_clear_code();
    ses.player = -1;
    ses.drill_mode = 0;
    ses.sfx = ses.music = ses.voice = 1;
    static const int ids[4] = {16001, 16002, 16003, 16009};
    for (int i = 0; i < 4; i++) btn[i] = aseq_load(ids[i]);
    make_options_art(btn[2]);
    typed[0] = 0;
    prov = 0;
    sel = 0; top = 0;
    rebuild();
    if (nrows == 0) { prov = 1; rebuild(); select_prov(); }
    plat_key_flush();
    plat_click_flush();
    for (;;) {
        draw(-1);
        int k = plat_key_pop(), cx, cy;
        int action = -1;
        if (!k && plat_click_pop(&cx, &cy)) {
            static const int bx[4] = {164, 241, 318, 395}, by[4] = {331, 331, 331, 330}, bw[4] = {73, 73, 73, 85};
            for (int b = 0; b < 4; b++)
                if (cx >= bx[b] && cx < bx[b] + bw[b] && cy >= by[b] && cy < by[b] + 48 && btn_enabled(b)) {
                    snd_play(1756, 5, 0);
                    draw(b);
                    plat_wait(10);
                    action = b;
                }
            if (action < 0 && cx >= LIST_X && cx < LIST_X + LIST_W && cy >= LIST_Y && cy < LIST_Y + ROWS * ROW_H) {
                int i = top + (cy - LIST_Y) / ROW_H;
                if (i < nrows) { if (i == sel) action = 0; else { sel = i; } }
            } else if (action < 0 && cx >= 400 && cx < 431 && cy >= 108 && cy < 132) { if (top > 0) top--; }
            else if (action < 0 && cx >= 400 && cx < 431 && cy >= 218 && cy < 242) { if (top + ROWS < nrows) top++; }
        } else if (!k) { plat_wait(2); continue; }
        else if (k == K_ESC) {
            if (cancel_enabled()) action = 4;        /* Esc does what CANCEL did when CANCEL would have been enabled; else it quits */
            else return 0;
        }
        else if (k == K_ENTER) action = 0;
        else if (k == K_UP) { if (sel > 0) sel--; keep_visible(); }
        else if (k == K_DOWN) { if (sel + 1 < nrows) sel++; keep_visible(); }
        else if (k == K_BACKSPACE) backspace();
        else if (k == K_CTRL_R) {
            if (sel >= 0 && sel < nrows && rows[sel].real && dlg_yesno(15000, 0)) {
                savedb_remove(&ses.db, rows[sel].idx);
                savedb_flush(&ses.db);
                rebuild();
                if (sel >= nrows) sel = nrows ? nrows - 1 : 0;
            }
        } else if (k == K_CTRL_S) menu_toggle_all_sound();
        else if (k >= 0x20 && k < 0x7F && k != 0x20 + 0x100) { if (k == ' ' && !typed[0]) continue; type_char(k); }
        if (action == 0) { if (do_start()) { ses.rank = rank_for_score(ses_lifetime()); ses.hints = (ses.rank == 0); return 1; } }
        else if (action == 1) {
            if (savedb_count(&ses.db) >= 99) { dlg_show(23012, 0); continue; }
            typed[0] = 0; prov = 1; rebuild(); select_prov(); keep_visible();
        } else if (action == 2) audio_options_dialog();
        else if (action == 4) { typed[0] = 0; prov = 0; rebuild(); if (sel >= nrows) sel = nrows ? nrows - 1 : 0; }
        else if (action == 3) {
            ses.player = -1;
            ses.tier = 0;
            memcpy(ses.cust_drill, tier_cust[0], 13);
            return 2;
        }
    }
}
