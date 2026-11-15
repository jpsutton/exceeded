#include "gfx.h"
#include "res.h"

uint8_t g_pal[256][3];

int pal_load(int id) {
    size_t len;
    const uint8_t *p = res_get("RRGB", id, &len);
    if (!p || len < 1536) return -1;
    for (int i = 0; i < 256; i++)
        for (int k = 0; k < 3; k++) g_pal[i][k] = p[i * 6 + k * 2];  /* high byte of BE u16 */
    return 0;
}

/* ------------------------------------------------------------------ cels */
int cel_decode(const uint8_t *p, size_t avail, Cel *out) {
    memset(out, 0, sizeof *out);
    if (avail < 6) return -1;
    int w = rd16(p + 2), h = rd16(p + 4);
    out->w = w;
    out->h = h;
    out->pix = xcalloc((size_t)w * h + 1, 1);
    out->mask = xcalloc((size_t)w * h + 1, 1);
    size_t q = 6;
    int err = 0;
    for (int y = 0; y < h && !err; y++) {
        int x = 0;
        while (x < w) {
            if (q >= avail) { err = 1; break; }
            int c = p[q++];
            if (c == 0x00) {
                if (q >= avail) { err = 1; break; }
                x += p[q++];
            } else if (c == 0xFF) {
                if (q + 1 >= avail) { err = 1; break; }
                int col = p[q++], n = p[q++];
                for (int i = 0; i < n && x + i < w; i++) {
                    out->pix[y * w + x + i] = (uint8_t)col;
                    out->mask[y * w + x + i] = 1;
                }
                x += n;
            } else {
                if (q + (size_t)c > avail) { err = 1; break; }
                for (int i = 0; i < c && x + i < w; i++) {
                    out->pix[y * w + x + i] = p[q + i];
                    out->mask[y * w + x + i] = 1;
                }
                q += (size_t)c;
                x += c;
            }
        }
    }
    return err ? -1 : 0;
}

void cel_free(Cel *c) {
    free(c->pix);
    free(c->mask);
    memset(c, 0, sizeof *c);
}

/* ---------------------------------------------------------------- canvas */
Canvas *canvas_new(int w, int h) {
    Canvas *c = xcalloc(1, sizeof *c);
    c->w = w;
    c->h = h;
    c->px = xcalloc((size_t)w * h, 1);
    return c;
}
void canvas_free(Canvas *c) {
    if (!c) return;
    free(c->px);
    free(c);
}
void canvas_fill(Canvas *c, int colour) { memset(c->px, colour, (size_t)c->w * c->h); }

static Rect clip_to(const Canvas *c, const Rect *clip) {
    Rect r = {0, 0, c->w, c->h};
    if (clip) {
        int x0 = clip->x > r.x ? clip->x : r.x;
        int y0 = clip->y > r.y ? clip->y : r.y;
        int x1 = (clip->x + clip->w < r.x + r.w) ? clip->x + clip->w : r.x + r.w;
        int y1 = (clip->y + clip->h < r.y + r.h) ? clip->y + clip->h : r.y + r.h;
        r.x = x0; r.y = y0; r.w = x1 - x0; r.h = y1 - y0;
    }
    return r;
}

void canvas_blit_cel(Canvas *c, const Cel *cel, int x, int y, int mirror, const Rect *clip) {
    Rect cl = clip_to(c, clip);
    if (cl.w <= 0 || cl.h <= 0) return;
    for (int row = 0; row < cel->h; row++) {
        int dy = y + row;
        if (dy < cl.y || dy >= cl.y + cl.h) continue;
        for (int col = 0; col < cel->w; col++) {
            int sx = mirror ? cel->w - 1 - col : col;
            int dx = x + col;
            if (dx < cl.x || dx >= cl.x + cl.w) continue;
            if (cel->mask[row * cel->w + sx]) c->px[dy * c->w + dx] = cel->pix[row * cel->w + sx];
        }
    }
}

void canvas_fill_rect(Canvas *c, Rect r, int colour) {
    Rect cl = clip_to(c, &r);
    for (int y = cl.y; y < cl.y + cl.h; y++) memset(c->px + y * c->w + cl.x, colour, (size_t)cl.w > 0 ? (size_t)cl.w : 0);
}
void canvas_copy_rect(Canvas *dst, const Canvas *src, Rect r) {
    Rect cl = clip_to(dst, &r);
    for (int y = cl.y; y < cl.y + cl.h && y < src->h; y++)
        for (int x = cl.x; x < cl.x + cl.w && x < src->w; x++) dst->px[y * dst->w + x] = src->px[y * src->w + x];
}

/* ----------------------------------------------------------------- fonts */
#define MAXFONTS 32
static Font *fonts[MAXFONTS];
static int nfonts;

Font *font_load(int id) {
    for (int i = 0; i < nfonts; i++)
        if (fonts[i]->id == id) return fonts[i];
    size_t len;
    const uint8_t *d = res_get("NFNT", id, &len);
    if (!d || len < 26) return NULL;
    Font *f = xcalloc(1, sizeof *f);
    f->id = id;
    f->d = d;
    f->len = len;
    f->first = rd16(d + 2);
    f->last = rd16(d + 4);
    f->widMax = rds16(d + 6);
    f->kernMax = rds16(d + 8);
    f->nDescent = rds16(d + 10);
    f->fRectW = rds16(d + 12);
    f->fRectH = rds16(d + 14);
    f->owTLoc = rd16(d + 16);
    f->ascent = rds16(d + 18);
    f->descent = rds16(d + 20);
    f->leading = rds16(d + 22);
    f->rowWords = rd16(d + 24);
    f->strike = d + 26;
    size_t strike_bytes = (size_t)f->fRectH * f->rowWords * 2;
    f->loc = d + 26 + strike_bytes;
    f->ow = d + 16 + 2 * (size_t)f->owTLoc;
    int nglyph = f->last - f->first + 3;
    if (26 + strike_bytes + (size_t)nglyph * 2 > len || (size_t)(16 + 2 * f->owTLoc) + (size_t)nglyph * 2 > len) {
        free(f);
        return NULL;
    }
    if (nfonts < MAXFONTS) fonts[nfonts++] = f;
    return f;
}

int font_slot(const Font *f, int ch) {
    int miss = f->last - f->first + 1;
    if (ch < f->first || ch > f->last) return miss;
    int s = ch - f->first;
    if (f->ow[s * 2] == 0xFF && f->ow[s * 2 + 1] == 0xFF) return miss;
    return s;
}
int font_advance(const Font *f, int ch) { return f->ow[font_slot(f, ch) * 2 + 1]; }
int font_text_width(const Font *f, const char *s) {
    int w = 0;
    for (; *s; s++) w += font_advance(f, (uint8_t)*s);
    return w;
}

static int glyph_x0(const Font *f, int slot) { return rd16(f->loc + slot * 2); }

/* Visit each set pixel of the text; fn(x, y, ctx). */
typedef void (*pixfn)(int x, int y, void *ctx);
static int font_walk(const Font *f, int penx, int baseline, const char *s, pixfn fn, void *ctx) {
    int maxbit = f->rowWords * 16;
    for (; *s; s++) {
        int slot = font_slot(f, (uint8_t)*s);
        int x0 = glyph_x0(f, slot), x1 = glyph_x0(f, slot + 1);
        if (x0 > maxbit) x0 = maxbit;
        if (x1 > maxbit) x1 = maxbit;
        int off = f->ow[slot * 2], adv = f->ow[slot * 2 + 1];
        int gx = penx + f->kernMax + off;
        for (int row = 0; row < f->fRectH; row++) {
            const uint8_t *r = f->strike + (size_t)row * f->rowWords * 2;
            for (int bx = x0; bx < x1; bx++) {
                if (r[bx >> 3] & (0x80 >> (bx & 7))) fn(gx + (bx - x0), baseline - f->ascent + row, ctx);
            }
        }
        penx += adv;
    }
    return penx;
}

typedef struct { Canvas *c; int colour; const Rect *clip; } DrawCtx;
static void draw_px(int x, int y, void *p) {
    DrawCtx *d = p;
    if (x < 0 || y < 0 || x >= d->c->w || y >= d->c->h) return;
    if (d->clip && (x < d->clip->x || y < d->clip->y || x >= d->clip->x + d->clip->w || y >= d->clip->y + d->clip->h)) return;
    d->c->px[y * d->c->w + x] = (uint8_t)d->colour;
}
int font_draw(const Font *f, Canvas *c, int x, int y, const char *s, int colour, const Rect *clip) {
    DrawCtx d = {c, colour, clip};
    return font_walk(f, x, y, s, draw_px, &d);
}

typedef struct { uint8_t *m; int w, h; } MaskCtx;
static void mask_px(int x, int y, void *p) {
    MaskCtx *d = p;
    if (x >= 0 && y >= 0 && x < d->w && y < d->h) d->m[y * d->w + x] = 1;
}
void font_draw_mask(const Font *f, uint8_t *mask, int mw, int mh, int penx, int baseline, const char *s, int *pen_end) {
    MaskCtx m = {mask, mw, mh};
    int e = font_walk(f, penx, baseline, s, mask_px, &m);
    if (pen_end) *pen_end = e;
}

/* ------------------------------------------------------------ Windows FNT (version 0x200/0x300) */
FntFont *fnt_load_first(void) {
    static FntFont *cached;
    static int tried;
    if (tried) return cached;
    tried = 1;
    for (int fi = 0; fi < res_file_count(); fi++) {
        ResFile *rf = res_file(fi);
        for (int i = 0; i < rf->nres; i++) {
            Res *r = &rf->res[i];
            if (strcmp(r->tname, "FONT") != 0) continue;
            const uint8_t *d = rf->data + r->off;
            if (r->len < 118) continue;
            int ver = rd16(d);
            FntFont *f = xcalloc(1, sizeof *f);
            f->d = d;
            f->len = r->len;
            f->ascent = rd16(d + 74);
            f->height = rd16(d + 88);
            f->first = d[95];
            f->last = d[96];
            f->defchar = d[97] + f->first;
            int n = f->last - f->first + 2;
            size_t tbl = ver >= 0x300 ? 148 : 118;
            size_t ent = ver >= 0x300 ? 6 : 4;
            if (n > 257 || tbl + (size_t)n * ent > r->len) { free(f); continue; }
            for (int k = 0; k < n; k++) {
                f->widths[k] = rd16(d + tbl + (size_t)k * ent);
                f->offs[k] = ver >= 0x300 ? rd32(d + tbl + (size_t)k * ent + 2) : rd16(d + tbl + (size_t)k * ent + 2);
            }
            cached = f;
            return f;
        }
    }
    return NULL;
}
static int fnt_slot(const FntFont *f, int ch) {
    if (ch < f->first || ch > f->last) ch = f->defchar;
    return ch - f->first;
}
int fnt_char_width(const FntFont *f, int ch) { return f->widths[fnt_slot(f, ch)]; }
int fnt_text_width(const FntFont *f, const char *s) {
    int w = 0;
    for (; *s; s++) w += fnt_char_width(f, (uint8_t)*s);
    return w;
}
int fnt_draw(const FntFont *f, Canvas *c, int x, int y, const char *s, int colour, const Rect *clip) {
    for (; *s; s++) {
        int slot = fnt_slot(f, (uint8_t)*s);
        int w = f->widths[slot];
        size_t off = f->offs[slot];
        int strips = (w + 7) / 8;
        for (int st = 0; st < strips; st++)
            for (int row = 0; row < f->height; row++) {
                size_t bo = off + (size_t)st * f->height + row;
                if (bo >= f->len) continue;
                uint8_t b = f->d[bo];
                for (int bit = 0; bit < 8; bit++) {
                    int px = st * 8 + bit;
                    if (px >= w) break;
                    if (b & (0x80 >> bit)) {
                        int dx = x + px, dy = y + row;
                        if (clip && (dx < clip->x || dy < clip->y || dx >= clip->x + clip->w || dy >= clip->y + clip->h)) continue;
                        if (dx >= 0 && dy >= 0 && dx < c->w && dy < c->h) c->px[dy * c->w + dx] = (uint8_t)colour;
                    }
                }
            }
        x += w;
    }
    return x;
}
