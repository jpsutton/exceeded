#include "dialog.h"
#include "audio.h"
#include "dosint.h"
#include "game.h"
#include "plat.h"
#include "res.h"
#include "ui.h"
#include <strings.h>

#define DLU_X 1.75
#define DLU_Y 2.125

Dlg *dlg_load(int id) {
    size_t len;
    const uint8_t *d = res_get("DIALOG", id, &len);
    if (!d || len < 12) return NULL;
    Dlg *dl = xcalloc(1, sizeof *dl);
    dl->id = id;
    dl->at_x = dl->at_y = -1;
    size_t p = 0;
    dl->style = rd32(d);
    int n = d[4];
    dl->x = rds16(d + 5); dl->y = rds16(d + 7); dl->cx = rds16(d + 9); dl->cy = rds16(d + 11);
    p = 13;
    if (d[p] == 0) p++;
    else if (d[p] == 0xFF) p += 3;
    else p += strlen((const char *)d + p) + 1;
    p += strlen((const char *)d + p) + 1;                       /* class */
    snprintf(dl->caption, sizeof dl->caption, "%s", (const char *)d + p);
    p += strlen((const char *)d + p) + 1;
    if (dl->style & 0x40) {
        p += 2;
        dl->use_fnt = strstr((const char *)d + p, "Keycaps") != NULL;
        if (dl->use_fnt) { dl->dlu_x = 2.0; dl->dlu_y = 2.125; }     /* Windows 95 base unit: (405 + 26) div 52 = 8 px per 4 units */
        p += strlen((const char *)d + p) + 1;
    }
    if (!dl->use_fnt) { dl->dlu_x = 1.5; dl->dlu_y = 1.625; }   /* MS Sans Serif 8 pt system dialogs (129, 30xx): 6 x 13 px base unit (03 section 1.2) */
    for (int i = 0; i < n && i < MAX_DLG_ITEMS && p + 14 < len; i++) {
        DlgItem *it = &dl->items[dl->nitems++];
        it->x = rds16(d + p); it->y = rds16(d + p + 2); it->cx = rds16(d + p + 4); it->cy = rds16(d + p + 6);
        it->id = rd16(d + p + 8);
        it->style = rd32(d + p + 10);
        p += 14;
        if (d[p] >= 0x80) { it->cls = d[p]; p++; }
        else { it->cls = 0; p += strlen((const char *)d + p) + 1; }
        if (d[p] == 0xFF) { p += 3; it->text[0] = 0; }
        else {
            snprintf(it->text, sizeof it->text, "%s", (const char *)d + p);
            p += strlen((const char *)d + p) + 1;
        }
        int extra = d[p++];
        p += (size_t)extra;
        it->enabled = !(it->style & 0x08000000);
        if (it->cls == 0x80 && (it->style & 0x0F) == 1 && it->enabled && !dl->default_id) dl->default_id = it->id;
    }
    return dl;
}
void dlg_free(Dlg *d) { free(d); }
DlgItem *dlg_item(Dlg *d, int ctl_id) {
    for (int i = 0; i < d->nitems; i++) if (d->items[i].id == ctl_id) return &d->items[i];
    return NULL;
}
void dlg_set_text(Dlg *d, int ctl_id, const char *t) {
    for (int i = 0; i < d->nitems; i++)
        if (d->items[i].id == ctl_id) snprintf(d->items[i].text, sizeof d->items[i].text, "%s", t);
}
void dlg_set_check(Dlg *d, int ctl_id, int v) { DlgItem *it = dlg_item(d, ctl_id); if (it) it->checked = v; }
int dlg_get_check(Dlg *d, int ctl_id) { DlgItem *it = dlg_item(d, ctl_id); return it ? it->checked : 0; }

static int g_use_fnt;
static const Rect *g_dlg_clip;
static FntFont *ff(void) { return g_use_fnt ? fnt_load_first() : NULL; }
static int lh(int font) { FntFont *f = ff(); return f ? f->height : ui_text_h(font); }

static double g_dlu_x = DLU_X, g_dlu_y = DLU_Y;
static int px(double dlu, int base) { return base + (int)(dlu * g_dlu_x + 0.5); }

static void layout(Dlg *d) {
    g_use_fnt = d->use_fnt;
    g_dlu_x = d->dlu_x > 0 ? d->dlu_x : DLU_X;
    g_dlu_y = d->dlu_y > 0 ? d->dlu_y : DLU_Y;
    d->pw = (int)(d->cx * g_dlu_x + 0.5);
    d->ph = (int)(d->cy * g_dlu_y + 0.5);
    int caption = (d->style & 0x00C00000) == 0x00C00000 ? 22 : 0;
    d->ph += caption;
    if (d->at_x >= 0) { d->px = d->at_x; d->py = d->at_y; }
    else { d->px = (CANVAS_W - d->pw) / 2; d->py = (CANVAS_H - d->ph) / 2; }
    if (d->px < 0) d->px = 0;
    if (d->py < 0) d->py = 0;
    for (int i = 0; i < d->nitems; i++) {
        DlgItem *it = &d->items[i];
        it->px = px(it->x, d->px);
        it->py = d->py + caption + (int)(it->y * g_dlu_y + 0.5);
        it->pw = (int)(it->cx * g_dlu_x + 0.5);
        it->ph = (int)(it->cy * g_dlu_y + 0.5);
    }
}

/* key-cap glyphs 0x7F..0x86 are drawn as short labels */
static const char *keycap_label(int c) {
    switch (c) {
    case 0x7F: return "<-"; case 0x80: return "Up"; case 0x81: return "->"; case 0x82: return "="; case 0x83: return "C";
    case 0x84: return "Spacebar"; case 0x85: return "Backspace"; case 0x86: return "Enter";
    }
    return NULL;
}
static void draw_label(int font, int x, int y, const char *s, int colour) {
    char buf[256];
    size_t o = 0;
    for (const char *p = s; *p && o < 250; p++) {
        unsigned char c = (unsigned char)*p;
        const char *k = (!ff() && c >= 0x7F && c <= 0x86) ? keycap_label(c) : NULL;
        if (k) { buf[o++] = '['; for (const char *q = k; *q && o < 250; q++) buf[o++] = *q; buf[o++] = ']'; }
        else if (c == '&') continue;
        else buf[o++] = (char)c;
    }
    buf[o] = 0;
    FntFont *f = ff();
    if (f) fnt_draw(f, g_eng.work, x, y, buf, colour, g_dlg_clip);
    else ui_text(font, x, y, buf, colour);
}
static int label_w(int font, const char *s) {
    char buf[256];
    size_t o = 0;
    for (const char *p = s; *p && o < 250; p++) {
        unsigned char c = (unsigned char)*p;
        const char *k = (!ff() && c >= 0x7F && c <= 0x86) ? keycap_label(c) : NULL;
        if (k) { buf[o++] = '['; for (const char *q = k; *q && o < 250; q++) buf[o++] = *q; buf[o++] = ']'; }
        else if (c == '&') continue;
        else buf[o++] = (char)c;
    }
    buf[o] = 0;
    FntFont *f = ff();
    return f ? fnt_text_width(f, buf) : ui_text_w(font, buf);
}

static void frame3d(int x, int y, int w, int h, int pressed) {
    ui_fill(x, y, x + w - 1, y + h - 1, C_LGRAY, 0);
    ui_fill(x, y, x + w - 1, y, pressed ? 0 : C_WHITE, 0);
    ui_fill(x, y, x, y + h - 1, pressed ? 0 : C_WHITE, 0);
    ui_fill(x, y + h - 1, x + w - 1, y + h - 1, pressed ? C_WHITE : 0, 0);
    ui_fill(x + w - 1, y, x + w - 1, y + h - 1, pressed ? C_WHITE : 0, 0);
}

static void draw_wrapped(int font, int x, int y, int w, int h, const char *text, int align) {
    /* split on '\n', wrap each paragraph inside width w */
    int fh = lh(font);
    char para[256];
    const char *p = text;
    int ly = y;
    while (*p && ly < y + h + fh) {
        size_t n = strcspn(p, "\r\n");
        if (n > 255) n = 255;
        memcpy(para, p, n);
        para[n] = 0;
        /* greedy word wrap */
        char line[256] = "";
        const char *q = para;
        for (;;) {
            while (*q == ' ') q++;
            if (!*q) break;
            const char *e = q;
            while (*e && *e != ' ') e++;
            char word[128];
            size_t wl = (size_t)(e - q);
            if (wl > 127) wl = 127;
            memcpy(word, q, wl);
            word[wl] = 0;
            char cand[400];
            snprintf(cand, sizeof cand, "%s%s%s", line, line[0] ? " " : "", word);
            if (line[0] && label_w(font, cand) > w) {
                int lw = label_w(font, line);
                draw_label(font, align == 1 ? x + (w - lw) / 2 : align == 2 ? x + w - lw : x, ly, line, 0);
                ly += fh;
                snprintf(line, sizeof line, "%s", word);
            } else snprintf(line, sizeof line, "%s", cand);
            q = e;
        }
        if (line[0]) {
            int lw = label_w(font, line);
            draw_label(font, align == 1 ? x + (w - lw) / 2 : align == 2 ? x + w - lw : x, ly, line, 0);
        }
        ly += fh;
        p += n;
        if (*p == '\r' && p[1] == '\n') p++;
        if (*p == '\n' || *p == '\r') p++;
    }
}

static int g_edit_focus;
static void draw_dlg(Dlg *d, int focus_id) {
    int caption = (d->style & 0x00C00000) == 0x00C00000 ? 22 : 0;
    frame3d(d->px, d->py, d->pw, d->ph, 0);
    ui_fill(d->px, d->py, d->px + d->pw - 1, d->py, 0, 0);
    ui_fill(d->px, d->py, d->px, d->py + d->ph - 1, 0, 0);
    ui_fill(d->px + d->pw - 1, d->py, d->px + d->pw - 1, d->py + d->ph - 1, 0, 0);
    ui_fill(d->px, d->py + d->ph - 1, d->px + d->pw - 1, d->py + d->ph - 1, 0, 0);
    if (caption) {
        ui_fill(d->px + 2, d->py + 2, d->px + d->pw - 3, d->py + caption - 2, C_DKBLUE, 0);
        ui_text_center(F_SMALL, d->px + d->pw / 2, d->py + 4, d->caption, C_WHITE);
    }
    int font = F_SMALL;
    for (int i = 0; i < d->nitems; i++) {
        DlgItem *it = &d->items[i];
        if (it->cls == 0x82) {
            int k = it->style & 0x1F;
            if (k <= 2 || k == 0x0C) {
                Rect cl = {it->px, it->py, it->pw, it->ph};   /* static text never spills outside its control */
                g_dlg_clip = &cl;
                ui_set_clip(&cl);
                draw_wrapped(font, it->px, it->py, it->pw, it->ph, it->text, k == 1 ? 1 : k == 2 ? 2 : 0);
                g_dlg_clip = NULL;
                ui_set_clip(NULL);
            }
            else if (k == 4 || k == 5) ui_fill(it->px, it->py, it->px + it->pw - 1, it->py + it->ph - 1, 0, 0);
            else if (k == 7 || k == 6 || k == 8 || k == 9) { /* frames */
                ui_fill(it->px, it->py, it->px + it->pw - 1, it->py, 0, 0);
                ui_fill(it->px, it->py + it->ph - 1, it->px + it->pw - 1, it->py + it->ph - 1, 0, 0);
                ui_fill(it->px, it->py, it->px, it->py + it->ph - 1, 0, 0);
                ui_fill(it->px + it->pw - 1, it->py, it->px + it->pw - 1, it->py + it->ph - 1, 0, 0);
            }
        } else if (it->cls == 0x84) {                              /* horizontal SCROLLBAR: arrows, trough, thumb */
            int x0 = it->px, y0 = it->py, x1 = it->px + it->pw - 1, y1 = it->py + it->ph - 1;
            ui_fill(x0 + 17, y0, x1 - 17, y1, C_LGRAY, 0);
            ui_fill(x0 + 17, y0, x1 - 17, y0, 0, 0);
            ui_fill(x0 + 17, y1, x1 - 17, y1, 0, 0);
            frame3d(x0, y0, 17, it->ph, 0);
            frame3d(x1 - 16, y0, 17, it->ph, 0);
            int my = y0 + it->ph / 2;
            for (int k = 0; k < 4; k++) { ui_fill(x0 + 10 - k, my - k, x0 + 10 - k, my + k, 0, 0); ui_fill(x1 - 10 + k, my - k, x1 - 10 + k, my + k, 0, 0); }
            int tw = 9, span = (it->pw - 34) - tw, range = it->vmax - it->vmin;
            int tx = x0 + 17 + (range > 0 ? span * (it->value - it->vmin) / range : 0);
            frame3d(tx, y0, tw, it->ph, 0);
            if (d->kbd_nav && it->id == focus_id) {
                ui_fill(x0 - 1, y0 - 1, x1 + 1, y0 - 1, 0, 0); ui_fill(x0 - 1, y1 + 1, x1 + 1, y1 + 1, 0, 0);
                ui_fill(x0 - 1, y0 - 1, x0 - 1, y1 + 1, 0, 0); ui_fill(x1 + 1, y0 - 1, x1 + 1, y1 + 1, 0, 0);
            }
        } else if (it->cls == 0x81) {                              /* EDIT: white box, text scrolls to its end */
            ui_fill(it->px, it->py, it->px + it->pw - 1, it->py + it->ph - 1, C_WHITE, 0);
            ui_fill(it->px, it->py, it->px + it->pw - 1, it->py, 0, 0);
            ui_fill(it->px, it->py + it->ph - 1, it->px + it->pw - 1, it->py + it->ph - 1, 0, 0);
            ui_fill(it->px, it->py, it->px, it->py + it->ph - 1, 0, 0);
            ui_fill(it->px + it->pw - 1, it->py, it->px + it->pw - 1, it->py + it->ph - 1, 0, 0);
            const char *t = it->text;
            while (*t && label_w(font, t) > it->pw - 8) t++;
            Rect cl = {it->px + 2, it->py + 1, it->pw - 4, it->ph - 2};
            g_dlg_clip = &cl;
            ui_set_clip(&cl);
            draw_label(font, it->px + 3, it->py + (it->ph - lh(font)) / 2, t, 0);
            g_dlg_clip = NULL;
            ui_set_clip(NULL);
            if (it->id == focus_id && g_edit_focus) {
                int cx = it->px + 3 + label_w(font, t) + 1;
                ui_fill(cx, it->py + 2, cx, it->py + it->ph - 3, 0, 0);
            }
        } else if (it->cls == 0x80) {
            int k = it->style & 0x0F;
            if (k == 0 || k == 1) {
                frame3d(it->px, it->py, it->pw, it->ph, 0);
                if (it->id == focus_id) {
                    ui_fill(it->px, it->py, it->px + it->pw - 1, it->py, 0, 0);
                    ui_fill(it->px, it->py + it->ph - 1, it->px + it->pw - 1, it->py + it->ph - 1, 0, 0);
                    ui_fill(it->px, it->py, it->px, it->py + it->ph - 1, 0, 0);
                    ui_fill(it->px + it->pw - 1, it->py, it->px + it->pw - 1, it->py + it->ph - 1, 0, 0);
                }
                int lw = label_w(font, it->text);
                draw_label(font, it->px + (it->pw - lw) / 2, it->py + (it->ph - lh(font)) / 2, it->text, 0);
            } else if (k == 7) {
                ui_fill(it->px, it->py + 6, it->px + it->pw - 1, it->py + 6, 0, 0);
                draw_label(font, it->px + 6, it->py, it->text, 0);
            } else {  /* check box / radio */
                int bx = it->px, by = it->py + (it->ph - 13) / 2;
                int col = it->enabled ? 0 : 8;                    /* disabled: dark grey */
                ui_fill(bx, by, bx + 12, by + 12, C_WHITE, 0);
                ui_fill(bx, by, bx + 12, by, col, 0); ui_fill(bx, by + 12, bx + 12, by + 12, col, 0);
                ui_fill(bx, by, bx, by + 12, col, 0); ui_fill(bx + 12, by, bx + 12, by + 12, col, 0);
                if (it->checked) ui_fill(bx + 3, by + 3, bx + 9, by + 9, col, 0);
                draw_label(font, it->px + 18, it->py + (it->ph - lh(font)) / 2, it->text, col);
                if (d->kbd_nav && it->id == focus_id) {            /* focus ring around the label */
                    int lx = it->px + 16, lw = label_w(font, it->text) + 4;
                    ui_fill(lx, it->py, lx + lw, it->py, 0, 0);
                    ui_fill(lx, it->py + it->ph - 1, lx + lw, it->py + it->ph - 1, 0, 0);
                    ui_fill(lx, it->py, lx, it->py + it->ph - 1, 0, 0);
                    ui_fill(lx + lw, it->py, lx + lw, it->py + it->ph - 1, 0, 0);
                }
            }
        }
    }
}

static int cur_dlg_id;
int dlg_current_id(void) { return cur_dlg_id; }

static int dlg_modal_nav(Dlg *d, DlgValidate validate, void *ctx);
int dlg_modal(Dlg *d, DlgValidate validate, void *ctx) {
    if (d->kbd_nav) return dlg_modal_nav(d, validate, ctx);
    layout(d);
    int prev_dlg = cur_dlg_id;
    cur_dlg_id = d->id;
    plat_modal_enter();
    /* save the area under the dialog */
    Canvas *save = canvas_new(CANVAS_W, CANVAS_H);
    memcpy(save->px, g_eng.work->px, (size_t)CANVAS_W * CANVAS_H);
    snd_stop_all();
    dosint_pause(1);
    if (d->speech) speech_play(d->speech);
    int result = 0;
    int focus = d->default_id;
    for (;;) {
        draw_dlg(d, focus);
        plat_present();
        int k, cx, cy;
        int pick = 0;
        while ((k = plat_key_pop()) == 0 && !plat_click_pop(&cx, &cy)) { plat_wait(2); }
        if (k) {
            if (k == K_ENTER || k == K_SPACE) {
                if (focus) pick = focus;
                else {
                    for (int i = 0; i < d->nitems; i++) if (d->items[i].cls == 0x80 && (d->items[i].style & 0x0F) <= 1) { pick = d->items[i].id; break; }
                }
            } else if (k == K_ESC) {
                pick = 2;   /* Esc always sends cancel (id 2), even without such a control */
            } else if (k > 32 && k < 127) {
                /* mnemonic: &Y activates the button whose label contains &Y */
                for (int i = 0; i < d->nitems && !pick; i++) {
                    DlgItem *it = &d->items[i];
                    if (it->cls != 0x80 || !it->enabled || (it->style & 0x0F) > 1) continue;
                    const char *am = strchr(it->text, '&');
                    if (am && (am[1] | 0x20) == (k | 0x20)) pick = it->id;
                }
            }
        } else {
            for (int i = 0; i < d->nitems; i++) {
                DlgItem *it = &d->items[i];
                if (it->cls != 0x80 || !it->enabled) continue;
                if (cx < it->px || cy < it->py || cx >= it->px + it->pw || cy >= it->py + it->ph) continue;
                int st = it->style & 0x0F;
                if (st <= 1) { pick = it->id; break; }
                if (st == 2 || st == 3) it->checked = !it->checked;
                else if (st == 4 || st == 9) {
                    /* radio: clear the group (items between WS_GROUP flags) */
                    int a = i, b = i;
                    while (a > 0 && !(d->items[a].style & 0x00020000)) a--;
                    while (b + 1 < d->nitems && !(d->items[b + 1].style & 0x00020000)) b++;
                    for (int j = a; j <= b; j++) if ((d->items[j].style & 0x0F) == 4 || (d->items[j].style & 0x0F) == 9) d->items[j].checked = 0;
                    it->checked = 1;
                }
                break;
            }
        }
        if (pick) {
            if (validate && validate(d, pick, ctx)) continue;
            result = pick;
            break;
        }
    }
    if (d->speech) speech_stop();
    dosint_pause(0);
    plat_modal_leave();
    cur_dlg_id = prev_dlg;
    memcpy(g_eng.work->px, save->px, (size_t)CANVAS_W * CANVAS_H);
    canvas_free(save);
    plat_present();
    return result;
}


/* ---- keyboard-navigable modal (engine-built dialogs): Tab / Shift+Tab, arrows in radio groups, EDIT controls ---- */
static int is_radio(const DlgItem *it) { return it->cls == 0x80 && ((it->style & 0x0F) == 4 || (it->style & 0x0F) == 9); }
static int is_push(const DlgItem *it) { return it->cls == 0x80 && (it->style & 0x0F) <= 1; }
static int is_check(const DlgItem *it) { return it->cls == 0x80 && ((it->style & 0x0F) == 2 || (it->style & 0x0F) == 3); }
static int is_scroll(const DlgItem *it) { return it->cls == 0x84; }
static int scroll_set(DlgItem *it, int v) {
    if (v < it->vmin) v = it->vmin;
    if (v > it->vmax) v = it->vmax;
    int ch = v != it->value;
    it->value = v;
    return ch;
}
static void thumb_geom(const DlgItem *it, int *x0, int *span, int *tw) {
    *tw = 9;
    *x0 = it->px + 17;
    *span = (it->pw - 34) - *tw;
}
/* press at x: arrows +-1, trough pages +-10; returns 1 if the press is on the thumb (drag) */
static int scroll_press(DlgItem *it, int x, int *changed) {
    int tx0, span, tw;
    thumb_geom(it, &tx0, &span, &tw);
    int range = it->vmax - it->vmin;
    int tx = tx0 + (range > 0 ? span * (it->value - it->vmin) / range : 0);
    *changed = 0;
    if (x < it->px + 17) *changed = scroll_set(it, it->value - 1);
    else if (x >= it->px + it->pw - 17) *changed = scroll_set(it, it->value + 1);
    else if (x < tx) *changed = scroll_set(it, it->value - 10);
    else if (x >= tx + tw) *changed = scroll_set(it, it->value + 10);
    else return 1;
    return 0;
}
static void radio_group(Dlg *d, int i, int *a, int *b) {
    *a = *b = i;
    while (*a > 0 && !(d->items[*a].style & 0x00020000)) (*a)--;
    while (*b + 1 < d->nitems && !(d->items[*b + 1].style & 0x00020000)) (*b)++;
}
static void radio_select(Dlg *d, int i) {
    int a, b;
    radio_group(d, i, &a, &b);
    for (int j = a; j <= b; j++) if (is_radio(&d->items[j])) d->items[j].checked = 0;
    d->items[i].checked = 1;
}
/* tab stops in order: one per radio group (the checked radio, else the first enabled), edits, push buttons */
static int tab_stops(Dlg *d, int *out) {
    int n = 0;
    for (int i = 0; i < d->nitems; i++) {
        DlgItem *it = &d->items[i];
        if (!it->enabled) continue;
        if (is_radio(it)) {
            int a, b;
            radio_group(d, i, &a, &b);
            int pick = -1;
            for (int j = a; j <= b; j++) if (is_radio(&d->items[j]) && d->items[j].enabled && d->items[j].checked) pick = j;
            for (int j = a; j <= b && pick < 0; j++) if (is_radio(&d->items[j]) && d->items[j].enabled) pick = j;
            if (pick == i) out[n++] = i;
        } else if (it->cls == 0x81 || is_push(it) || is_check(it) || is_scroll(it)) out[n++] = i;
    }
    return n;
}

static int dlg_modal_nav(Dlg *d, DlgValidate validate, void *ctx) {
    layout(d);
    int prev_dlg = cur_dlg_id;
    cur_dlg_id = d->id;
    plat_modal_enter();
    Canvas *save = canvas_new(CANVAS_W, CANVAS_H);
    memcpy(save->px, g_eng.work->px, (size_t)CANVAS_W * CANVAS_H);
    snd_stop_all();
    dosint_pause(1);                         /* DOS music/effect drivers stop ticking while a dialog is open */
    int result = 0;
    if (d->hook) d->hook(d, ctx, DH_OPEN);
    int stops[MAX_DLG_ITEMS];
    int ns = tab_stops(d, stops);
    int fi = ns ? stops[0] : -1;
    for (;;) {
        ns = tab_stops(d, stops);
        if (fi >= 0 && !d->items[fi].enabled) fi = ns ? stops[0] : -1;
        g_edit_focus = fi >= 0 && d->items[fi].cls == 0x81;
        draw_dlg(d, fi >= 0 ? d->items[fi].id : 0);
        plat_present();
        int k = 0, cx = 0, cy = 0, click = 0;
        for (;;) {
            if ((k = plat_key_pop()) != 0) break;
            if (plat_click_pop(&cx, &cy)) { click = 1; break; }
            plat_wait(2);
            if (d->hook && d->hook(d, ctx, DH_POLL)) break;
        }
        if (!k && !click) continue;
        if (d->hook) d->hook(d, ctx, DH_INPUT);
        int pick = 0, old = fi, changed = 0;
        if (click) {
            for (int i = 0; i < d->nitems; i++) {
                DlgItem *it = &d->items[i];
                if (!it->enabled || (it->cls != 0x80 && it->cls != 0x81 && it->cls != 0x84)) continue;
                if (cx < it->px || cy < it->py || cx >= it->px + it->pw || cy >= it->py + it->ph) continue;
                if (is_push(it)) pick = it->id;
                else if (is_radio(it)) { radio_select(d, i); fi = i; changed = 1; }
                else if (is_check(it)) { it->checked = !it->checked; fi = i; changed = 1; }
                else if (is_scroll(it)) {
                    int ch;
                    fi = i;
                    int onthumb = scroll_press(it, cx, &ch);
                    if (ch) changed = 1;
                    if (onthumb && plat_mode() == PLAT_SDL) {                    /* drag the thumb while the button is held */
                        int tx0, span, tw;
                        thumb_geom(it, &tx0, &span, &tw);
                        for (;;) {
                            int mx, my, left;
                            plat_mouse_state(&mx, &my, &left);
                            if (!left) break;
                            int v = it->vmin + (span > 0 ? (mx - tw / 2 - tx0) * (it->vmax - it->vmin) / span : 0);
                            if (scroll_set(it, v) && d->hook) d->hook(d, ctx, DH_CHANGE);
                            draw_dlg(d, d->items[fi].id);
                            plat_present();
                            plat_wait(1);
                        }
                    }
                }
                else fi = i;
                break;
            }
        } else {
            DlgItem *f = fi >= 0 ? &d->items[fi] : NULL;
            int pos = -1;
            for (int s = 0; s < ns; s++) if (stops[s] == fi) pos = s;
            if (k == K_ESC) pick = 2;
            else if (k == K_TAB && ns) fi = stops[(pos + 1) % ns];
            else if (k == K_BACKTAB && ns) fi = stops[(pos + ns - 1 + (pos < 0 ? 1 : 0)) % ns];
            else if (k == K_ENTER) pick = (f && is_push(f)) ? f->id : 1;
            else if (f && f->cls == 0x81) {
                size_t l = strlen(f->text);
                if (k == K_BACKSPACE) { if (l) f->text[l - 1] = 0; }
                else if (k >= 32 && k < 127 && l < 250) { f->text[l] = (char)k; f->text[l + 1] = 0; }
            } else if (f && is_scroll(f) && (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN || k == K_PGUP || k == K_PGDN || k == K_HOME || k == K_END)) {
                int v = f->value;
                if (k == K_LEFT || k == K_DOWN) v -= 1;
                else if (k == K_RIGHT || k == K_UP) v += 1;
                else if (k == K_PGDN) v -= 10;
                else if (k == K_PGUP) v += 10;
                else if (k == K_HOME) v = f->vmin;
                else v = f->vmax;
                if (scroll_set(f, v)) changed = 1;
            } else if (f && is_check(f) && k == K_SPACE) { f->checked = !f->checked; changed = 1;
            } else if (f && is_radio(f) && (k == K_UP || k == K_LEFT || k == K_DOWN || k == K_RIGHT)) {
                int a, b, dir = (k == K_UP || k == K_LEFT) ? -1 : 1;
                radio_group(d, fi, &a, &b);
                for (int j = fi + dir; j >= a && j <= b; j += dir)
                    if (is_radio(&d->items[j]) && d->items[j].enabled) { radio_select(d, j); fi = j; changed = 1; break; }
            } else if (f && is_push(f) && k == K_SPACE) pick = f->id;
            else if (k > 32 && k < 127) {
                for (int i = 0; i < d->nitems && !pick; i++) {
                    DlgItem *it = &d->items[i];
                    if ((!is_push(it) && !is_check(it)) || !it->enabled) continue;
                    const char *am = strchr(it->text, '&');
                    if (am && (am[1] | 0x20) == (k | 0x20)) { if (is_check(it)) { it->checked = !it->checked; changed = 1; fi = i; } else pick = it->id; }
                }
            }
        }
        if (changed && d->hook) d->hook(d, ctx, DH_CHANGE);
        if (old != fi && old >= 0 && d->items[old].cls == 0x81 && d->hook) d->hook(d, ctx, DH_BLUR);
        if (pick) {
            if (old >= 0 && d->items[old].cls == 0x81 && old == fi && d->hook && pick == 1) d->hook(d, ctx, DH_BLUR);
            if (validate && validate(d, pick, ctx)) continue;
            result = pick;
            break;
        }
    }
    dosint_pause(0);
    plat_modal_leave();
    cur_dlg_id = prev_dlg;
    memcpy(g_eng.work->px, save->px, (size_t)CANVAS_W * CANVAS_H);
    canvas_free(save);
    plat_present();
    return result;
}

int dlg_show_at(int id, int speech_id, int x, int y) {
    Dlg *d = dlg_load(id);
    evlog("DIALOG", id, "");
    if (!d) return 0;
    d->speech = speech_id;
    d->at_x = x;
    d->at_y = y;
    int r = dlg_modal(d, NULL, NULL);
    dlg_free(d);
    return r;
}
int dlg_show(int id, int speech_id) { return dlg_show_at(id, speech_id, -1, -1); }

int dlg_yesno(int id, int speech_id) {
    Dlg *d = dlg_load(id);
    evlog("DIALOG", id, "");
    if (!d) return 0;
    d->speech = speech_id;
    int r = dlg_modal(d, NULL, NULL);
    int yes = 0;
    yes = (r == 1);
    dlg_free(d);
    return yes;
}
