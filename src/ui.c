#include "ui.h"
#include "game.h"
#include "res.h"

static Aseq *hall(void) { return aseq_load(200); }

void ui_stamp(Aseq *a, int cel, int x, int y, int flags) {
    if (!a) return;
    const Cel *c = aseq_cel(a, cel);
    if (!c) return;
    Rect clip = {0, 0, CANVAS_W, CANVAS_H};
    if (!(flags & ST_BACKONLY)) canvas_blit_cel(g_eng.work, c, x, y, flags & ST_MIRROR, &clip);
    if (flags & (ST_BACK | ST_BACKONLY)) canvas_blit_cel(g_eng.back, c, x, y, flags & ST_MIRROR, &clip);
}
void ui_stamp_picture(int aseq_id, int x, int y, int flags) { ui_stamp(aseq_load(aseq_id), 0, x, y, flags); }
void ui_stamp_hall(int cel, int x, int y, int flags) {
    if (cel >= 10000) ui_stamp(aseq_load(210), cel - 10000, x, y, flags);
    else ui_stamp(hall(), cel, x, y, flags);
}
void ui_fill(int x1, int y1, int x2, int y2, int colour, int to_back) {
    Rect r = {x1, y1, x2 - x1 + 1, y2 - y1 + 1};
    if (r.w <= 0 || r.h <= 0) return;
    canvas_fill_rect(g_eng.work, r, colour);
    if (to_back) canvas_fill_rect(g_eng.back, r, colour);
}
void ui_recolour(int x1, int y1, int x2, int y2, int from, int to) {
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            if (x >= 0 && y >= 0 && x < CANVAS_W && y < CANVAS_H && g_eng.work->px[y * CANVAS_W + x] == from)
                g_eng.work->px[y * CANVAS_W + x] = (uint8_t)to;
}
void ui_restore_rect(int x, int y, int w, int h) { canvas_copy_rect(g_eng.work, g_eng.back, (Rect){x, y, w, h}); }
void ui_copy_to_back(int x, int y, int w, int h) { canvas_copy_rect(g_eng.back, g_eng.work, (Rect){x, y, w, h}); }

/* ---------------------------------------------------------------- text */
static const Rect *g_clip;
void ui_set_clip(const Rect *r) { g_clip = r; }

int ui_text_h(int font) { Font *f = font_load(font); return f ? f->fRectH : 0; }

/* '^' prints glyph 0xA1 */
static void xlat(const char *s, char *d, size_t n) {
    size_t i = 0;
    for (; *s && i + 1 < n; s++) d[i++] = (*s == '^') ? (char)0xA1 : *s;
    d[i] = 0;
}
int ui_text_w(int font, const char *s) {
    Font *f = font_load(font);
    if (!f) return 0;
    char t[512];
    xlat(s, t, sizeof t);
    int w = 0;
    for (char *p = t; *p; p++) if (*p != '\r' && *p != '\n') w += font_advance(f, (uint8_t)*p);
    return w;
}
int ui_text(int font, int x, int y, const char *s, int colour) {
    Font *f = font_load(font);
    if (!f) return x;
    char t[512];
    xlat(s, t, sizeof t);
    int pen = x, line_y = y;
    char *p = t;
    char seg[512];
    while (*p) {
        size_t n = strcspn(p, "\r\n");
        memcpy(seg, p, n);
        seg[n] = 0;
        pen = font_draw(f, g_eng.work, x, line_y + f->ascent - f->descent, seg, colour, g_clip);   /* baseline = y + ascent - descent (spec 07 section 0, v1.9) */
        p += n;
        if (*p == '\r' || *p == '\n') {
            if (*p == '\r' && p[1] == '\n') p++;
            p++;
            line_y += f->fRectH;
            pen = x;
        }
    }
    return pen;
}
void ui_text_center(int font, int cx, int y, const char *s, int colour) { ui_text(font, cx - ui_text_w(font, s) / 2, y, s, colour); }
void ui_text_right(int font, int rx, int y, const char *s, int colour) { ui_text(font, rx - ui_text_w(font, s), y, s, colour); }

int ui_text_wrap(int font, int x, int y, int right, const char *s, int colour, int line_gap) {
    Font *f = font_load(font);
    if (!f) return y;
    char line[512] = "";
    const char *p = s;
    while (*p) {
        /* next word */
        const char *q = p;
        while (*q && *q != ' ' && *q != '\n' && *q != '\r') q++;
        char word[256];
        size_t wl = (size_t)(q - p);
        if (wl > 255) wl = 255;
        memcpy(word, p, wl);
        word[wl] = 0;
        char cand[768];
        snprintf(cand, sizeof cand, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && x + ui_text_w(font, cand) > right) {
            ui_text(font, x, y, line, colour);
            y += f->fRectH + line_gap;
            snprintf(line, sizeof line, "%s", word);
        } else snprintf(line, sizeof line, "%s", cand);
        p = q;
        if (*p == '\n' || *p == '\r') {
            ui_text(font, x, y, line, colour);
            y += f->fRectH + line_gap;
            line[0] = 0;
            if (*p == '\r' && p[1] == '\n') p++;
        }
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
    }
    if (line[0]) { ui_text(font, x, y, line, colour); y += f->fRectH + line_gap; }
    return y;
}
void ui_commas(char *dst, size_t n, long v) {
    char t[32];
    snprintf(t, sizeof t, "%ld", v);
    int l = (int)strlen(t), o = 0;
    char out[48];
    for (int i = 0; i < l; i++) {
        if (i && t[0] != '-' && (l - i) % 3 == 0) out[o++] = ',';
        out[o++] = t[i];
    }
    out[o] = 0;
    snprintf(dst, n, "%s", out);
}

/* ------------------------------------------------------------------ HUD */
void hud_clock(void) {
    Aseq *h = hall();
    if (gs_clock_off_display()) { ui_stamp(h, 76, 402, 303, 0); return; }
    int c = gs.clock;
    if (c >= 1200) { ui_stamp(h, 75, 402, 304, 0); return; }
    ui_stamp(h, 77, 402, 303, 0);
    int hh = c / 100, mm = c % 100;
    if (hh >= 10) ui_stamp(h, 64 + hh / 10, 407, 308, 0);
    ui_stamp(h, 64 + hh % 10, 418, 308, 0);
    ui_stamp(h, 64 + mm / 10, 434, 308, 0);
    ui_stamp(h, 64 + mm % 10, 445, 308, 0);
}
void hud_energy(void) {
    ui_fill(403, 327, 480, 345, 0, 0);
    int e = gs.energy;
    if (e > 0) ui_fill(403, 327, 402 + 3 * (e > 24 ? 24 : e), 345, C_RED, 0);
}
void hud_score(void) {
    Aseq *h = hall();
    int s = gs.score;
    int x = 467;
    for (int i = 0; i < 6; i++) {
        ui_stamp(h, 64 + s % 10, x, 353, 0);
        s /= 10;
        x -= 12;
    }
}
void hud_rebuild(void) {
    canvas_fill(g_eng.work, 0);
    canvas_fill(g_eng.back, 0);
    ui_stamp_picture(1000, 0, 0, ST_BACK);
    hud_clock();
    hud_energy();
    hud_score();
}
void hud_msg_clear(void) { ui_fill(17, 300, 326, 374, C_MSGBLUE, 0); }
