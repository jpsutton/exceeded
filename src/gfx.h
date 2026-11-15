/* gfx.h - palette, cel decoding, NFNT bitmap fonts, 8-bit canvas (spec 02) */
#ifndef GFX_H
#define GFX_H
#include "util.h"

#define CANVAS_W 512
#define CANVAS_H 384

/* ---- palette ---- */
extern uint8_t g_pal[256][3];
/* Load RRGB `id` (default 128) into g_pal (high byte of each big-endian u16 channel). 0 on success. */
int pal_load(int id);

/* ---- cels ---- */
typedef struct Cel {
    int w, h;
    uint8_t *pix;   /* w*h palette indices */
    uint8_t *mask;  /* w*h: 1 = opaque, 0 = transparent */
} Cel;
/* Decode one RLE cel (spec 02 2.2) from `p` (points at the cel's u16 type word). */
int cel_decode(const uint8_t *p, size_t avail, Cel *out);
void cel_free(Cel *c);

/* ---- 8-bit canvas ---- */
typedef struct Canvas {
    int w, h;
    uint8_t *px;
} Canvas;
Canvas *canvas_new(int w, int h);
void canvas_free(Canvas *c);
void canvas_fill(Canvas *c, int colour);
typedef struct Rect { int x, y, w, h; } Rect;
/* Blit cel with optional horizontal mirror, clipped to canvas and to `clip` (may be NULL). */
void canvas_blit_cel(Canvas *c, const Cel *cel, int x, int y, int mirror, const Rect *clip);
void canvas_fill_rect(Canvas *c, Rect r, int colour);
void canvas_copy_rect(Canvas *dst, const Canvas *src, Rect r);

/* ---- NFNT bitmap fonts ---- */
typedef struct Font {
    int id;
    const uint8_t *d;
    size_t len;
    int first, last, widMax, kernMax, nDescent, fRectW, fRectH, owTLoc, ascent, descent, leading, rowWords;
    const uint8_t *strike;
    const uint8_t *loc;   /* u16 LE table */
    const uint8_t *ow;    /* 2 bytes per glyph */
} Font;
Font *font_load(int id);   /* NFNT id, searched in all open files; cached; NULL if missing */
/* Glyph metrics for byte `ch`: returns glyph slot (after missing-glyph substitution). */
int font_slot(const Font *f, int ch);
int font_advance(const Font *f, int ch);
int font_text_width(const Font *f, const char *s);
/* Draw text with pen at (x, baseline y). colour = palette index. Returns pen x after the text.
 * If `clip` is non-NULL pixels outside it are not drawn. */
int font_draw(const Font *f, Canvas *c, int x, int y, const char *s, int colour, const Rect *clip);
/* Render a glyph bitmask row-by-row for tests: writes into mask (w*h) at origin. Internal helper. */
void font_draw_mask(const Font *f, uint8_t *mask, int mw, int mh, int penx, int baseline, const char *s, int *pen_end);


/* ---- Windows FNT bitmap font (SSO.FON "TLC Keycaps", spec 02 section 4.2) ---- */
typedef struct FntFont {
    const uint8_t *d;
    size_t len;
    int height, ascent, first, last, defchar;
    int widths[257];
    uint32_t offs[257];
} FntFont;
FntFont *fnt_load_first(void);           /* first FONT resource of any open file; NULL if none */
int fnt_char_width(const FntFont *f, int ch);
int fnt_text_width(const FntFont *f, const char *s);
/* draw with the top-left of the line at (x, y); returns pen x after the text */
int fnt_draw(const FntFont *f, Canvas *c, int x, int y, const char *s, int colour, const Rect *clip);

#endif
