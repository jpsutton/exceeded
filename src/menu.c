/* menu.c - in-canvas menu bar built from the EXE's MENU resources, and the command handlers (spec 03 section 3) */
#include "game.h"

typedef struct MItem { int id; char text[48]; int flags; } MItem;
typedef struct MPopup { char title[32]; MItem items[20]; int n; } MPopup;

static MPopup popups[4];
static int npopups;

static void menu_load(int res_id) {
    size_t len;
    const uint8_t *d = res_get("MENU", res_id, &len);
    if (!d || len < 4) return;
    size_t p = 4;
    MPopup *cur = NULL;
    while (p + 2 <= len) {
        int flags = rd16(d + p);
        p += 2;
        if (flags & 0x10) {
            if (npopups >= 4) return;
            cur = &popups[npopups++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->title, sizeof cur->title, "%s", (const char *)d + p);
            p += strlen((const char *)d + p) + 1;
        } else {
            int id = rd16(d + p);
            p += 2;
            if (cur && cur->n < 18) {
                MItem *it = &cur->items[cur->n++];
                it->id = id;
                it->flags = flags & ~0x80;
                snprintf(it->text, sizeof it->text, "%s", (const char *)d + p);
                /* "Audio Options..." follows Voice (spec 12 section 6.6): new ids 610 / 1110 / 1410 (drill has no Voice: after Music) */
                int after = id == 604 ? 610 : id == 1104 ? 1110 : id == 1401 ? 1410 : 0;
                if (after && cur->n < 19) {
                    MItem *ao = &cur->items[cur->n++];
                    ao->id = after;
                    ao->flags = 0;
                    snprintf(ao->text, sizeof ao->text, "Audio Options...");
                }
                if (flags & 0x80) cur->items[cur->n - 1].flags |= 0x80;
            }
            p += strlen((const char *)d + p) + 1;
        }
        if (flags & 0x80) {
            /* last item at this level: if we were inside a popup, the level ends */
            if (!(flags & 0x10)) { cur = NULL; if (p + 4 > len) break; }
        }
    }
}

static const char *clean(const char *s, char *buf, size_t n) {
    size_t o = 0;
    for (; *s && o + 1 < n; s++) { if (*s == '&') continue; if (*s == '\t') break; buf[o++] = *s; }
    buf[o] = 0;
    return buf;
}

/* ---- state helpers */
static int checked(int id) {
    switch (id) {
    case 600: case 1100: return ses.hints;
    case 602: case 1102: case 1400: return ses.sfx;
    case 603: case 1103: case 1401: return ses.music;
    case 604: case 1104: return ses.voice;
    case 606: case 1108: return ses.calc;
    case 607: case 1109: return ses.clock_on;
    }
    return 0;
}

/* ---- dialogs for commands */
static void show_rank(void) {
    Dlg *d = dlg_load(200);
    evlog("DIALOG", 200, "");
    if (!d) return;
    /* top 10 by lifetime score; statics 100/101/102 = Name / Rank / Score columns (placeholder text replaced);
     * header line first, every line ends with a single CR; players with an empty name are skipped */
    int n = savedb_count(&ses.db);
    int order[99];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (savedb_score(&ses.db, order[j]) > savedb_score(&ses.db, order[i])) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    static const char *hdr[3] = {"Name", "Rank", "Score"};
    for (int c = 0; c < 3; c++) {
        DlgItem *it = dlg_item(d, 100 + c);
        if (!it) continue;
        size_t o = (size_t)snprintf(it->text, sizeof it->text, "%s\r", hdr[c]);
        for (int r = 0; r < 10 && r < n; r++) {
            int pl = order[r];
            if (!savedb_name(&ses.db, pl)[0]) continue;
            char cell[48];
            if (c == 0) snprintf(cell, sizeof cell, "%s", savedb_name(&ses.db, pl));
            else if (c == 1) snprintf(cell, sizeof cell, "%s", rank_names[rank_for_score(savedb_score(&ses.db, pl))]);
            else ui_commas(cell, sizeof cell, savedb_score(&ses.db, pl));
            if (o + strlen(cell) + 2 >= sizeof it->text) break;
            o += (size_t)snprintf(it->text + o, sizeof it->text - o, "%s\r", cell);
        }
    }
    dlg_modal(d, NULL, NULL);
    dlg_free(d);
}

static int help_pages(const int *pages, int n) {
    int i = 0;
    for (;;) {
        Dlg *d = dlg_load(pages[i]);
        evlog("DIALOG", pages[i], "");
        if (!d) return 0;
        DlgItem *b = dlg_item(d, 100);
        if (b && i == 0) b->enabled = 0;
        b = dlg_item(d, 101);
        if (b && i == n - 1) b->enabled = 0;
        int r = dlg_modal(d, NULL, NULL);
        dlg_free(d);
        if (r == 101 && i + 1 < n) i++;
        else if (r == 100 && i > 0) i--;
        else if (r == 102 || r == 1 || r == 2 || r == 0 || r >= 200) return r;
    }
}

typedef struct CustCtx { int drill; } CustCtx;
static int cust_validate(Dlg *d, int button, void *ctx) {
    if (button != 1) return 0;
    int ops = 0, nums = 0, fmts = 0, only_op = -1, opcount = 0;
    for (int i = 0; i < 4; i++) if (dlg_get_check(d, 101 + i)) { ops++; only_op = i; opcount++; }
    for (int i = 0; i < 13; i++) if (dlg_get_check(d, 105 + i)) nums++;
    for (int i = 0; i < 3; i++) if (dlg_get_check(d, 118 + i)) fmts++;
    if (!ops || !nums || !fmts) { dlg_show(140, 0); return 1; }
    if (opcount == 1 && (only_op == OP_MUL || only_op == OP_DIV) && nums == 1 && dlg_get_check(d, 105)) { dlg_show(141, 0); return 1; }
    return 0;
}
static void cust_to_dlg(Dlg *d, const uint8_t *c) {
    for (int i = 0; i < 4; i++) dlg_set_check(d, 101 + i, (c[i] & 0x10) != 0);
    for (int i = 0; i < 13; i++) dlg_set_check(d, 105 + i, (c[i] & 0x04) != 0);
    for (int i = 0; i < 3; i++) dlg_set_check(d, 118 + i, (c[i] & 0x08) != 0);
    for (int i = 0; i < 4; i++) dlg_set_check(d, 121 + i, (c[0] & 3) == i);
}
static void dlg_to_cust(Dlg *d, uint8_t *c) {
    memset(c, 0, 13);
    for (int i = 0; i < 4; i++) if (dlg_get_check(d, 101 + i)) c[i] |= 0x10;
    for (int i = 0; i < 13; i++) if (dlg_get_check(d, 105 + i)) c[i] |= 0x04;
    for (int i = 0; i < 3; i++) if (dlg_get_check(d, 118 + i)) c[i] |= 0x08;
    for (int i = 0; i < 4; i++) if (dlg_get_check(d, 121 + i)) c[0] |= (uint8_t)i;
}

/* the fields the dialog edits, compared with the player's level defaults */
static int cust_is_default(const uint8_t *c) {
    for (int i = 0; i < 13; i++) {
        int mask = (i < 4 ? 0x10 : 0) | 0x04 | (i < 3 ? 0x08 : 0) | (i == 0 ? 3 : 0);
        if ((c[i] & mask) != (tier_cust[ses.tier][i] & mask)) return 0;
    }
    return 1;
}

static void customize(int drill) {
    if (!drill && ses.player >= 0 && !ses.drill_mode) hint_show(16, 0);   /* first Problem Customization during a game */
    int id = drill ? 220 : 221;
    Dlg *d = dlg_load(id);
    evlog("DIALOG", id, "");
    if (!d) return;
    uint8_t *cur = drill ? ses.cust_drill : ses.cust_game;
    cust_to_dlg(d, cur);
    int r;
    for (;;) {
        r = dlg_modal(d, cust_validate, NULL);
        if (r == 1) { dlg_to_cust(d, cur); break; }
        if (r == 2 || r == 0) break;
        if (r == 3) {   /* Reset: defaults for the player's level; clears the customised bit */
            cust_to_dlg(d, tier_cust[ses.tier]);
            uint8_t *rc = ses_rec();
            if (rc) rc[REC_FLAGS] &= (uint8_t)~(drill ? 1 : 2);
        }
    }
    uint8_t *rec = ses_rec();
    if (r == 1) {
        if (!drill) { ses.level_word = cur[0] & 3; if (gs_live) game_redraw_questions(); }
        if (rec && !cust_is_default(cur)) { rec[REC_FLAGS] |= drill ? 1 : 2; ses_write_record(); }   /* only a setting that differs from the level's defaults is stored */
        if (!drill && !ses.drill_mode) dlg_show(20108, 0);
    }
    dlg_free(d);
}

/* the HUD (clock) is on screen in the hall, rooms, challenges and the decoder; not on the title, sign-in, drill or ending screens */
static int hud_on_screen(void) {
    extern const char *dbg_screen;
    return !strcmp(dbg_screen, "hall") || !strcmp(dbg_screen, "room") || !strcmp(dbg_screen, "challenge") || !strcmp(dbg_screen, "decoder");
}

void menu_exec(int id) {
    int in_game = !ses.drill_mode && ses.player >= 0;
    switch (id) {
    case 500: case 1002: show_rank(); break;
    case 502: case 1004: case 1302:
        if (dlg_yesno(game_signed_in() ? 23008 : 23013, 0)) game_quit();
        break;
    case 1000: case 1300:
        if (game_signed_in()) { if (dlg_yesno(23006, 0)) { save_game(); game_abort_to_signin(); } }
        else if (dlg_yesno(23007, 0)) game_abort_to_signin();
        break;
    case 600: case 1100:
        if (dlg_yesno(ses.hints ? 22012 : 22013, 0)) ses.hints = !ses.hints;
        break;
    case 602: case 1102: case 1400:
        if (dlg_yesno(ses.sfx ? 22000 : 22001, 0)) { ses.sfx = !ses.sfx; audio_set_switches(ses.sfx, ses.music, ses.voice); }
        break;
    case 603: case 1103: case 1401:
        if (dlg_yesno(ses.music ? 22002 : 22003, 0)) { ses.music = !ses.music; audio_set_switches(ses.sfx, ses.music, ses.voice); }
        break;
    case 610: case 1110: case 1410: audio_options_dialog(); break;
    case 604: case 1104:
        if (dlg_yesno(ses.voice ? 22004 : 22005, 0)) { ses.voice = !ses.voice; audio_set_switches(ses.sfx, ses.music, ses.voice); }
        break;
    case 606: case 1108:
        if (dlg_yesno(ses.calc ? 22010 : 22011, 0)) { ses.calc = !ses.calc; if (g_calc_redraw) g_calc_redraw(); }
        break;
    case 607: case 1109:
        if (ses.clock_on && dlg_yesno(22016, 0)) { ses.clock_on = 0; if (hud_on_screen()) hud_clock(); }
        break;
    case 1106: customize(0); break;
    case 1403: customize(1); break;
    case 1200: { int p[1] = {12500}; help_pages(p, 1); break; }
    case 1201: { int p[2] = {12400, 12401}; help_pages(p, 2); break; }
    case 1202: { int p[1] = {12300}; help_pages(p, 1); break; }
    case 1203: { int p[1] = {12200}; help_pages(p, 1); break; }
    case 1204: { int p[1] = {12100}; help_pages(p, 1); break; }
    case 1205: { int p[5] = {12000, 12001, 12002, 12003, 12004}; help_pages(p, 5); break; }
    case 1500: { int p[1] = {12600}; help_pages(p, 1); break; }
    default: break;
    }
    (void)in_game;
}

/* ctrl+S: toggle all sound */
void menu_toggle_all_sound(void) {
    int allon = ses.sfx && ses.music && ses.voice;
    ses.sfx = ses.music = ses.voice = !allon;
    audio_set_switches(ses.sfx, ses.music, ses.voice);
}

/* ------------------------------------------------------------------ the menu bar (spec 03 section 3.0) */
static int cur_set = 0;                               /* 0 = A sign-in, 1 = game, 2 = drill, -1 = none */
static uint32_t ov[FRAME_W * FRAME_H];                /* ARGB overlay: the strip, and an open drop-down */
static int tx0[4], tw[4];                             /* title rectangles: x start and width (text width + 24) */
#define COL_GREY 0xFFC0C0C0u
#define COL_DGREY 0xFF808080u
#define COL_WHITE 0xFFFFFFFFu
#define COL_BLACK 0xFF000000u
#define COL_NAVY 0xFF000080u

static const int SETS[3][3] = {{500, 600, 0}, {1000, 1100, 1200}, {1300, 1400, 1500}};

static void px_fill(int x0, int y0, int x1, int y1, uint32_t c) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (x >= 0 && x < FRAME_W && y >= 0 && y < FRAME_H) ov[y * FRAME_W + x] = c;
}

/* label with '&' mnemonic: clean text, index of the underlined character (-1 none), the part after a tab (accelerator) */
static void label_parts(const char *raw, char *txt, size_t n, int *ul, char *accel, size_t an) {
    size_t o = 0;
    *ul = -1;
    accel[0] = 0;
    for (const char *s = raw; *s && o + 1 < n; s++) {
        if (*s == '\t') { snprintf(accel, an, "%s", s + 1); break; }
        if (*s == '&') { if (s[1] == '&') { txt[o++] = '&'; s++; } else if (*ul < 0) *ul = (int)o; continue; }
        txt[o++] = *s;
    }
    txt[o] = 0;
}

static int text_w(const char *s) { Font *f = font_load(F_SMALL); return f ? font_text_width(f, s) : (int)strlen(s) * 6; }

/* draw `s` with its top at y; underline the character `ul` */
static void rgb_text(int x, int y, const char *s, uint32_t col, int ul) {
    Font *f = font_load(F_SMALL);
    if (!f) return;
    static uint8_t mask[FRAME_W * 24];
    memset(mask, 0, sizeof mask);
    int base = f->ascent - f->descent + 2;
    font_draw_mask(f, mask, FRAME_W, 24, 0, base, s, NULL);
    for (int yy = 0; yy < 24; yy++)
        for (int xx = 0; xx < FRAME_W; xx++) {
            if (!mask[yy * FRAME_W + xx]) continue;
            int px = x + xx, py = y + yy - 2;
            if (px >= 0 && px < FRAME_W && py >= 0 && py < FRAME_H) ov[py * FRAME_W + px] = col;
        }
    if (ul >= 0 && (size_t)ul < strlen(s)) {
        char pre[96];
        snprintf(pre, sizeof pre, "%.*s", ul, s);
        char one[2] = {s[ul], 0};
        int ux = x + text_w(pre), uw = text_w(one);
        px_fill(ux, y + base - 2 + 1, ux + uw - 1, y + base - 2 + 1, col);
    }
}

static void check_mark(int x, int y, uint32_t col) {                 /* 7x7 check mark with its top-left at (x, y) */
    static const int pts[][2] = {{0, 3}, {1, 4}, {2, 5}, {3, 4}, {4, 3}, {5, 2}, {6, 1}, {0, 4}, {1, 5}, {2, 6}, {3, 5}, {4, 4}, {5, 3}, {6, 2}};
    for (int i = 0; i < 14; i++) px_fill(x + pts[i][0], y + pts[i][1], x + pts[i][0], y + pts[i][1], col);
}

int g_menu_grey;                                  /* phases in which every item is greyed (03 section 3.0a C); the titles still open */
static int item_disabled(const MItem *it) { return g_menu_grey || (it->flags & 3) != 0; }
static int item_sep(const MItem *it) { return !it->text[0]; }

static void layout_titles(void) {
    int x = 6;
    for (int i = 0; i < npopups; i++) {
        char t[64], ac[8];
        int ul;
        label_parts(popups[i].title, t, sizeof t, &ul, ac, sizeof ac);
        tx0[i] = x;
        tw[i] = text_w(t) + 24;
        x += tw[i];
    }
}

static int drop_width(const MPopup *p) {
    int w = 80;
    for (int i = 0; i < p->n; i++) {
        char t[64], ac[32];
        int ul;
        label_parts(p->items[i].text, t, sizeof t, &ul, ac, sizeof ac);
        int tw_ = 18 + text_w(t) + (ac[0] ? 24 + text_w(ac) : 0) + 14;
        if (tw_ > w) w = tw_;
    }
    return w;
}

/* Draw the strip (and the open drop-down) into the overlay and hand it to the platform layer. */
static void bar_draw(int sel, int open, int hot) {
    memset(ov, 0, sizeof ov);
    px_fill(0, 0, FRAME_W - 1, BAR_H - 1, COL_GREY);
    for (int i = 0; i < npopups; i++) {
        char t[64], ac[8];
        int ul;
        label_parts(popups[i].title, t, sizeof t, &ul, ac, sizeof ac);
        int x0 = tx0[i], x1 = tx0[i] + tw[i] - 1;
        uint32_t col = COL_BLACK;
        if (i == sel && open) {                                       /* sunken */
            px_fill(x0, 1, x1, BAR_H - 2, COL_GREY);
            px_fill(x0, 1, x1, 1, COL_DGREY); px_fill(x0, 1, x0, BAR_H - 2, COL_DGREY);
            px_fill(x0, BAR_H - 2, x1, BAR_H - 2, COL_WHITE); px_fill(x1, 1, x1, BAR_H - 2, COL_WHITE);
        } else if (i == sel) { px_fill(x0, 1, x1, BAR_H - 2, COL_NAVY); col = COL_WHITE; }
        rgb_text(x0 + 12 + (i == sel && open ? 1 : 0), 4 + (i == sel && open ? 1 : 0), t, col, ul);
    }
    px_fill(0, BAR_H - 1, FRAME_W - 1, BAR_H - 1, COL_DGREY);
    if (open && sel >= 0 && sel < npopups) {
        MPopup *p = &popups[sel];
        int w = drop_width(p), h = 4 + p->n * 18, x0 = tx0[sel], y0 = BAR_H;
        if (x0 + w > FRAME_W) x0 = FRAME_W - w;
        px_fill(x0, y0, x0 + w - 1, y0 + h - 1, COL_GREY);
        px_fill(x0, y0, x0 + w - 1, y0, COL_WHITE); px_fill(x0, y0, x0, y0 + h - 1, COL_WHITE);                  /* top / left */
        px_fill(x0, y0 + h - 1, x0 + w - 1, y0 + h - 1, COL_BLACK); px_fill(x0 + w - 1, y0, x0 + w - 1, y0 + h - 1, COL_BLACK);   /* bottom / right */
        px_fill(x0 + 1, y0 + h - 2, x0 + w - 2, y0 + h - 2, COL_DGREY); px_fill(x0 + w - 2, y0 + 1, x0 + w - 2, y0 + h - 2, COL_DGREY);
        for (int i = 0; i < p->n; i++) {
            MItem *it = &p->items[i];
            int y = y0 + 2 + i * 18;
            if (item_sep(it)) { px_fill(x0 + 3, y + 8, x0 + w - 4, y + 8, COL_DGREY); px_fill(x0 + 3, y + 9, x0 + w - 4, y + 9, COL_WHITE); continue; }
            char t[64], ac[32];
            int ul;
            label_parts(it->text, t, sizeof t, &ul, ac, sizeof ac);
            int selected = i == hot && !item_disabled(it);
            if (selected) px_fill(x0 + 2, y, x0 + w - 3, y + 17, COL_NAVY);
            uint32_t col = selected ? COL_WHITE : item_disabled(it) ? COL_DGREY : COL_BLACK;
            if (item_disabled(it) && !selected) { rgb_text(x0 + 2 + 18 + 1, y + 3, t, COL_WHITE, ul); }          /* emboss */
            rgb_text(x0 + 2 + 18, y + 2, t, col, ul);
            if (ac[0]) rgb_text(x0 + w - 12 - text_w(ac), y + 2, ac, col, -1);
            if (checked(it->id)) check_mark(x0 + 2 + 5, y + 5, selected ? COL_WHITE : COL_BLACK);
        }
    }
    plat_set_overlay(ov);
}

static int title_at(int fx, int fy) {
    if (fy < 0 || fy >= BAR_H) return -1;
    for (int i = 0; i < npopups; i++) if (fx >= tx0[i] && fx < tx0[i] + tw[i]) return i;
    return -1;
}
/* item index under (fx, fy) in the open drop-down of `sel`, -1 none; *inside = 1 if the point is in the drop-down rectangle */
static int item_at(int sel, int fx, int fy, int *inside) {
    *inside = 0;
    if (sel < 0 || sel >= npopups) return -1;
    MPopup *p = &popups[sel];
    int w = drop_width(p), h = 4 + p->n * 18, x0 = tx0[sel];
    if (x0 + w > FRAME_W) x0 = FRAME_W - w;
    if (fx < x0 || fx >= x0 + w || fy < BAR_H || fy >= BAR_H + h) return -1;
    *inside = 1;
    int i = (fy - BAR_H - 2) / 18;
    return (i >= 0 && i < p->n) ? i : -1;
}
static int hit_hook(int fx, int fy) { return title_at(fx, fy); }

static int menu_service(int kind, int arg);
/* load the titles of a menu set and show the strip */
void menu_set_current(int set) {
    g_menu_service = menu_service;                   /* every wait on every screen serves the bar */
    g_menu_grey = 0;
    cur_set = set;
    npopups = 0;
    if (set >= 0 && set < 3 && res_find("MENU", SETS[set][0], NULL)) for (int i = 0; i < 3; i++) if (SETS[set][i]) menu_load(SETS[set][i]);
    layout_titles();
    g_menu_hit = hit_hook;
    bar_draw(-1, 0, -1);
}

/* the title whose '&' letter is `letter` (lower case), or -1 */
static int title_by_letter(int letter) {
    for (int i = 0; i < npopups; i++) {
        char t[64], ac[8];
        int ul;
        label_parts(popups[i].title, t, sizeof t, &ul, ac, sizeof ac);
        if (ul >= 0 && (t[ul] | 0x20) == letter) return i;
    }
    return -1;
}

static int next_item(const MPopup *p, int from, int dir) {                      /* skip separators and disabled items */
    for (int k = 1; k <= p->n; k++) {
        int i = ((from + dir * k) % p->n + p->n) % p->n;
        if (!item_sep(&p->items[i]) && !item_disabled(&p->items[i])) return i;
    }
    return -1;
}

/* Run the menu bar for `set` until a command ran or the bar was left. The game clock and music wait while it is open. */
static void menu_open(int req, int arg) {
    int set = cur_set;
    if (npopups == 0 && set >= 0) menu_set_current(set);
    if (!npopups) return;
    int sel = 0, open = 0, hot = -1, cmd = 0;
    if (req == MR_OPEN_TITLE) { sel = arg; open = 1; }
    else if (req == MR_ALTLETTER) { int t = title_by_letter(arg); if (t < 0) return; sel = t; open = 1; }
    if (open) hot = -1;
    g_menu_active = 1;
    plat_set_cursor(0);                    /* the arrow */
    audio_menu_pause(1);                   /* a menu is open: every wave channel, speech and the song wait (WM_INITMENU) */
    dosint_pause(1);                       /* ... and the DOS drivers */
    if (set != 0) clock_pause();           /* ... and so does the game clock */
    int fx, fy, left;
    plat_frame_mouse(&fx, &fy, &left);
    int prev_left = left, pressed_item = 0;
    plat_key_flush();
    int leave = 0;
    while (!leave) {
        bar_draw(sel, open, hot);
        plat_present();
        int k = plat_key_pop(), cx, cy;
        int progressed = 0;
        if (k) {
            progressed = 1;
            MPopup *p = &popups[sel];
            if (k == K_ESC) { if (open) { open = 0; hot = -1; } else leave = 1; }
            else if (k == 0x179 || k == K_ALT) leave = 1;
            else if (k == K_LEFT || k == K_RIGHT) { sel = (sel + (k == K_LEFT ? npopups - 1 : 1)) % npopups; hot = open ? next_item(&popups[sel], -1, 1) : -1; }
            else if (k == K_DOWN) { if (!open) { open = 1; hot = next_item(p, -1, 1); } else hot = next_item(p, hot < 0 ? -1 : hot, 1); }
            else if (k == K_UP) { if (open) hot = next_item(p, hot < 0 ? 0 : hot, -1); }
            else if (k == K_ENTER) {
                if (!open) { open = 1; hot = next_item(p, -1, 1); }
                else if (hot >= 0) { cmd = p->items[hot].id; leave = 1; }
            } else if (k >= (K_ALTLETTER | 'a') && k <= (K_ALTLETTER | 'z')) { int t = title_by_letter(k & 0xFF); if (t >= 0) { sel = t; open = 1; hot = next_item(&popups[sel], -1, 1); } }
            else if (open && k >= 0x20 && k < 0x7F) {                               /* a letter runs the item with that mnemonic */
                for (int i = 0; i < p->n; i++) {
                    char t[64], ac[32];
                    int ul;
                    label_parts(p->items[i].text, t, sizeof t, &ul, ac, sizeof ac);
                    if (ul >= 0 && !item_disabled(&p->items[i]) && (t[ul] | 0x20) == (k | 0x20)) { cmd = p->items[i].id; leave = 1; break; }
                }
            }
        } else if (plat_fclick_pop(&cx, &cy)) {                                  /* scripted click = press and release */
            progressed = 1;
            int t = title_at(cx, cy), inside, i = open ? item_at(sel, cx, cy, &inside) : -1;
            if (t >= 0) { if (open && t == sel) leave = 1; else { sel = t; open = 1; hot = -1; } }
            else if (i >= 0 && !item_sep(&popups[sel].items[i]) && !item_disabled(&popups[sel].items[i])) { cmd = popups[sel].items[i].id; leave = 1; }
            else { int ins = 0; if (open) item_at(sel, cx, cy, &ins); if (!ins) leave = 1; }
        } else if (plat_mode() == PLAT_SDL) {                                    /* real mouse: press, drag, release */
            plat_frame_mouse(&fx, &fy, &left);
            int t = title_at(fx, fy), inside, i = open ? item_at(sel, fx, fy, &inside) : -1;
            if (open && i >= 0 && i != hot && !item_sep(&popups[sel].items[i])) hot = i;                  /* the item under the pointer is selected */
            if (left && !prev_left) {                                             /* press */
                if (t >= 0) { if (open && t == sel) leave = 1; else { sel = t; open = 1; hot = -1; } }
                else if (i >= 0) pressed_item = 1;
                else { int ins = 0; if (open) item_at(sel, fx, fy, &ins); if (!ins) leave = 1; }
            } else if (left && prev_left && t >= 0 && t != sel && open) { sel = t; hot = -1; }          /* moving across titles switches */
            else if (!left && prev_left) {                                       /* release */
                if (i >= 0 && !item_sep(&popups[sel].items[i]) && !item_disabled(&popups[sel].items[i])) { cmd = popups[sel].items[i].id; leave = 1; }
                else if (t < 0) { int ins = 0; if (open) item_at(sel, fx, fy, &ins); if (!ins) leave = 1; }
            }
            (void)pressed_item;
            prev_left = left;
        }
        if (!progressed && !leave) plat_wait(2);
    }
    g_menu_active = 0;
    bar_draw(-1, 0, -1);                  /* back to the plain strip */
    dosint_pause(0);
    audio_menu_pause(0);                  /* the chosen command runs after the menu closed, sound already resumed */
    plat_present();
    if (cmd) menu_exec(cmd);
    if (set != 0) clock_resume();
}

/* what a queued bar request does (plat_menu_poll, called from every wait). A screen without titles drops it. */
/* the window close box: the Exit confirmation with the same save behaviour (03 section 3.3). Without the dialog resource it quits at once. */
static void window_close(void) {
    int id = game_signed_in() ? 23008 : 23013;
    if (!res_find("DIALOG", id, NULL)) game_quit();
    if (cur_set != 0) clock_pause();
    if (dlg_yesno(id, 0)) game_quit();
    if (cur_set != 0) clock_resume();
}
static int menu_service(int kind, int arg) {
    if (kind == MR_CLOSE) { window_close(); return 1; }
    if (!npopups) return 0;
    if (kind == MR_COMMAND) {                            /* scripted `menu ID`: runs at once, inside whatever the game was doing */
        if (g_menu_grey) return 0;
        if (cur_set != 0) clock_pause();
        menu_exec(arg);
        if (cur_set != 0) clock_resume();
        return 1;
    }
    menu_open(kind, arg);
    return 1;
}
void menu_reset_service(void) { g_calc_redraw = NULL; g_menu_grey = 0; plat_menu_reset(); plat_modal_reset(); }
