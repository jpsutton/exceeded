/* dialog.h - modal dialogs built from the EXE's Win16 DIALOG resources (spec 01 section 5) */
#ifndef DIALOG_H
#define DIALOG_H
#include <stdint.h>

#define MAX_DLG_ITEMS 64
typedef struct DlgItem {
    int x, y, cx, cy;     /* dialog units */
    int id;
    uint32_t style;
    int cls;              /* 0x80 BUTTON, 0x81 EDIT, 0x82 STATIC, 0x83 LISTBOX, 0x84 SCROLLBAR, 0x85 COMBOBOX, 0 other */
    char text[256];
    int checked;
    int px, py, pw, ph;   /* canvas rect after layout */
    int enabled;
    int value, vmin, vmax; /* SCROLLBAR (engine-built dialogs) */
} DlgItem;

typedef struct Dlg {
    int id;
    uint32_t style;
    int x, y, cx, cy;
    char caption[128];
    int nitems;
    DlgItem items[MAX_DLG_ITEMS];
    int px, py, pw, ph;
    int speech;
    int at_x, at_y;       /* explicit canvas position (>= 0) instead of centring */
    int default_id;
    int use_fnt;          /* dialog font is TLC Keycaps (SSO.FON) */
    double dlu_y;         /* pixels per vertical unit: 2.125 (TLC Keycaps), 1.625 (MS Sans Serif 8 pt), 2.125 for Audio Options */
    double dlu_x;         /* pixels per horizontal dialog unit: 2.0 for the TLC Keycaps dialogs (03 section 1.2, v1.28), 1.75 otherwise and for Audio Options */
    /* extensions for engine-built dialogs (Audio Options): keyboard navigation, EDIT controls, hook */
    int kbd_nav;
    int (*hook)(struct Dlg *d, void *ctx, int event);   /* DH_* ; DH_POLL returns nonzero to request a redraw */
} Dlg;
enum { DH_OPEN = 1, DH_BLUR, DH_INPUT, DH_POLL, DH_CHANGE };   /* DH_CHANGE: a radio, check box or scroll bar value changed */

/* Load DIALOG resource `id`; NULL when it does not exist. */
Dlg *dlg_load(int id);
void dlg_free(Dlg *d);
DlgItem *dlg_item(Dlg *d, int ctl_id);
void dlg_set_text(Dlg *d, int ctl_id, const char *text);
void dlg_set_check(Dlg *d, int ctl_id, int v);
int dlg_get_check(Dlg *d, int ctl_id);
/* validate: called when a push button is chosen; return nonzero to keep the dialog open */
typedef int (*DlgValidate)(Dlg *d, int button_id, void *ctx);
int dlg_modal(Dlg *d, DlgValidate validate, void *ctx);

/* One-shot helper: show dialog `id` (with optional SPEECH id, 0 = none), return the chosen button id
 * (0 if the dialog does not exist). Logs a DIALOG event. */
int dlg_show(int id, int speech_id);
int dlg_show_at(int id, int speech_id, int x, int y);
/* Yes/No style dialog: returns 1 if the chosen button reads Yes/OK (by text), 0 otherwise. */
int dlg_yesno(int id, int speech_id);
int dlg_current_id(void);   /* id of the dialog currently open, 0 if none */
#endif
