/* ui.h - drawing helpers shared by all screens (spec 07 part A section 0) */
#ifndef UI_H
#define UI_H
#include "aseq.h"

enum { ST_MIRROR = 1, ST_BACK = 2, ST_BACKONLY = 4 };

/* colours (palette indices) */
enum { C_BLACK = 0, C_MSGBLUE = 4, C_LGRAY = 7, C_SCREENBLACK = 88, C_BROWN = 211, C_RED = 249, C_YELLOW = 251,
       C_CYAN = 254, C_WHITE = 255, C_DKBLUE = 4 };

/* fonts */
#define F_SMALL 28625   /* F0 default */
#define F_LARGE 28568   /* FL */
#define F_SERIF 28558   /* FS */
#define F_KEYS 15001
#define F_TITLE 15052

void ui_stamp(Aseq *a, int cel, int x, int y, int flags);
void ui_stamp_picture(int aseq_id, int x, int y, int flags);   /* cel 0 of any ASEQ, flags as above */
void ui_stamp_hall(int cel, int x, int y, int flags);           /* hall set: <10000 ASEQ 200, else ASEQ 210 */
void ui_fill(int x1, int y1, int x2, int y2, int colour, int to_back);   /* inclusive */
void ui_recolour(int x1, int y1, int x2, int y2, int from, int to);
void ui_restore_rect(int x, int y, int w, int h);                        /* work <- back */
void ui_copy_to_back(int x, int y, int w, int h);                        /* back <- work */

/* optional clip rectangle for ui_text (NULL = none) */
void ui_set_clip(const Rect *r);
/* text: y is the TOP of the text line (spec 07 A section 0). Returns the pen x after the text. */
int ui_text(int font, int x, int y, const char *s, int colour);
int ui_text_w(int font, const char *s);
int ui_text_h(int font);
void ui_text_center(int font, int cx, int y, const char *s, int colour);
void ui_text_right(int font, int rx, int y, const char *s, int colour);
/* word-wrapped; returns y after last line. line_gap = extra pixels between lines */
int ui_text_wrap(int font, int x, int y, int right, const char *s, int colour, int line_gap);
void ui_commas(char *dst, size_t n, long v);

/* HUD */
void hud_rebuild(void);        /* fill 0, stamp panel 1000, redraw clock/energy/score */
void hud_clock(void);
void hud_energy(void);
void hud_score(void);
void hud_msg_clear(void);      /* clear the message area with colour 4 (drill) */

/* panel buttons: ASEQ 1007 style (cel 0 normal, cel 1 pressed) */
#endif
