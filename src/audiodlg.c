/* audiodlg.c - the Audio Options dialog (NON-ORIGINAL enhancement, spec 12 section 6.3) */
#include "game.h"
#include "dosint.h"
#include "setup.h"
#include "fsx.h"
#include <strings.h>

#define AUDIO_DLG_ID 7000

typedef struct AdCtx {
    int status;
    char reason[500];
    char inner[1024];
    int tested;
    char checked_dir[1024];
    int orig_vol[3], orig_even;       /* restored by Cancel / Esc (spec 15 section 6.2) */
} AdCtx;

static void shorten(const char *src, char *dst, size_t n, int maxw) {
    FntFont *f = fnt_load_first();
    snprintf(dst, n, "%s", src);
    if (!f || fnt_text_width(f, dst) <= maxw) return;
    size_t len = strlen(src);
    /* keep a head and a tail, joined by an ellipsis, so that the result fits */
    for (size_t keep = len; keep > 4; keep--) {
        size_t head = keep / 3, tail = keep - head;
        if (head + tail >= len) continue;
        snprintf(dst, n, "%.*s...%s", (int)head, src, src + len - tail);
        if (fnt_text_width(f, dst) <= maxw) return;
    }
}

static void add(Dlg *d, int x, int y, int cx, int cy, int id, uint32_t style, int cls, const char *text) {
    DlgItem *it = &d->items[d->nitems++];
    memset(it, 0, sizeof *it);
    it->x = x; it->y = y; it->cx = cx; it->cy = cy; it->id = id; it->style = style; it->cls = cls;
    snprintf(it->text, sizeof it->text, "%s", text);
    it->enabled = !(style & 0x08000000);
}

static void add_scroll(Dlg *d, int x, int y, int cx, int cy, int id, int value) {
    add(d, x, y, cx, cy, id, 0x50010000, 0x84, "");
    DlgItem *it = &d->items[d->nitems - 1];
    it->vmin = 0; it->vmax = 100; it->value = value;
}

static void set_radio(Dlg *d, int base, int style) {
    for (int i = 0; i < 3; i++) dlg_set_check(d, base + i, i == style);
}
static int get_radio(Dlg *d, int base) {
    for (int i = 0; i < 3; i++) if (dlg_get_check(d, base + i)) return i;
    return 0;
}

/* the installation this run started from (14 section 9) */
static int from_installed(void) { return g_run_valid && g_run.data_from && !strcmp(g_run.data_from, "installed"); }

/* check the folder in the field and update the status line, the radios and the Install button */
static void recheck(Dlg *d, AdCtx *c) {
    DlgItem *ed = dlg_item(d, 320);
    char dir[1024], inst[1024];
    snprintf(dir, sizeof dir, "%s", ed->text);
    int have_inst = dosint_dos_installed(inst, sizeof inst);
    int use_inst = !dir[0] && have_inst;                       /* an empty field means the installed copy */
    c->status = dosint_check_dir(use_inst ? inst : dir, c->reason, sizeof c->reason, c->inner, sizeof c->inner);
    snprintf(c->checked_dir, sizeof c->checked_dir, "%s", use_inst ? inst : dir);
    char shown[1100], line[1300];
    shorten(use_inst ? inst : dir, shown, sizeof shown, 322);
    switch (c->status) {
    case DS_OK:
        if (use_inst) snprintf(line, sizeof line, "Using installed DOS files:\n%s", shown);
        else {
            if (c->inner[0]) { char t[1100]; snprintf(t, sizeof t, "%s [%s]", dir, c->inner); shorten(t, shown, sizeof shown, 322); }
            snprintf(line, sizeof line, "DOS files found:\n%s", shown);
        }
        break;
    case DS_MISSING: snprintf(line, sizeof line, "DOS files not found in:\n%s", shown); break;
    case DS_INVALID: snprintf(line, sizeof line, "DOS files not usable:\n%s", c->reason); break;
    default: snprintf(line, sizeof line, "No DOS game folder set."); break;
    }
    dlg_set_text(d, 321, line);
    int ok = c->status == DS_OK;
    static const int ids[4] = {302, 303, 312, 313};
    for (int i = 0; i < 4; i++) dlg_item(d, ids[i])->enabled = ok;
    if (!ok) {
        if (get_radio(d, 301) != 0) set_radio(d, 301, 0);
        if (get_radio(d, 311) != 0) set_radio(d, 311, 0);
    }
    /* Install: only for a run from the installation, with a checked path that is not <base>/dos itself */
    char own[1100] = "";
    if (g_run_valid) snprintf(own, sizeof own, "%s/dos", g_run.home);
    DlgItem *ib = dlg_item(d, 331);
    ib->enabled = from_installed() && !use_inst && c->status == DS_OK && strcmp(dir, own) != 0;
}

/* show the slider values and apply them (and Even out) at once */
static void sync_levels(Dlg *d) {
    int v[3];
    for (int i = 0; i < 3; i++) {
        v[i] = dlg_item(d, 340 + i)->value;
        char t[16];
        snprintf(t, sizeof t, "%d", v[i]);
        dlg_set_text(d, 343 + i, t);
    }
    dosint_set_levels_live(v, dlg_get_check(d, 346));
}

static int hook(Dlg *d, void *ctx, int ev) {
    AdCtx *c = ctx;
    if (ev == DH_CHANGE) { sync_levels(d); return 0; }
    if (ev == DH_OPEN) sync_levels(d);
    if (ev == DH_OPEN || ev == DH_BLUR) recheck(d, c);
    else if (ev == DH_INPUT) { if (dosint_test_running()) dosint_test_stop(); }
    else if (ev == DH_POLL) {
        char **paths;
        int n;
        if (plat_drop_pop(&paths, &n)) {                       /* the first path of a drop replaces the field and is checked at once */
            DlgItem *ed = dlg_item(d, 320);
            snprintf(ed->text, sizeof ed->text, "%s", paths[0]);
            for (int i = 0; i < n; i++) free(paths[i]);
            free(paths);
            recheck(d, c);
            return 1;
        }
        if (dosint_test_running()) return 0;
    }
    return 0;
}

static int validate(Dlg *d, int button, void *ctx) {
    AdCtx *c = ctx;
    if (button == 330) {                                     /* Test: plays a sample, changes nothing */
        recheck(d, c);
        dosint_test_start(get_radio(d, 301), get_radio(d, 311), c->checked_dir);
        c->tested = 1;
        return 1;
    }
    if (button == 331) {                                     /* Install the DOS files into <base>/dos (synchronous, 14 section 9) */
        dosint_test_stop();
        char code[32], msg[600], path[1100];
        snprintf(path, sizeof path, "%s", dlg_item(d, 320)->text);
        dlg_set_text(d, 321, "Installing...");
        if (install_dos_only(g_run.home, path, code, sizeof code, msg, sizeof msg) == 0) {
            char inst[1100];
            snprintf(inst, sizeof inst, "%s/dos", g_run.home);
            dlg_item(d, 320)->text[0] = 0;
            dosint_reload_dos(inst);
            snprintf(g_run.dos_installed, sizeof g_run.dos_installed, "ok");
            recheck(d, c);
            char st[1300];
            snprintf(st, sizeof st, "DOS files installed in:\n%s", inst);
            dlg_set_text(d, 321, st);
        } else {
            char st[1300];
            snprintf(st, sizeof st, "DOS files not installed:\n%s", msg);
            dlg_set_text(d, 321, st);
        }
        return 1;
    }
    dosint_test_stop();
    if (button != 1) { dosint_set_levels_live(c->orig_vol, c->orig_even); return 0; }     /* Cancel: no change, the levels come back */
    recheck(d, c);
    AudioSettings s;
    dosint_get_settings(&s);
    s.music = get_radio(d, 301);
    s.sfx = get_radio(d, 311);
    for (int i = 0; i < 3; i++) s.vol[i] = dlg_item(d, 340 + i)->value;
    s.even = dlg_get_check(d, 346);
    snprintf(s.dir, sizeof s.dir, "%s", dlg_item(d, 320)->text);
    dosint_apply(&s, 1);
    c->tested = 0;
    return 0;
}

void audio_options_dialog(void) {
    Dlg *d = xcalloc(1, sizeof *d);
    d->id = AUDIO_DLG_ID;
    d->style = 0x90C000C2;
    d->cx = 285; d->cy = 132;
    d->at_x = d->at_y = -1;
    snprintf(d->caption, sizeof d->caption, "Audio Options");
    d->use_fnt = 1;
    d->dlu_y = 2.125;
    d->dlu_x = 1.75;                          /* the non-original dialog keeps 1.75 px per unit (its 285 units fit the canvas) */
    d->kbd_nav = 1;
    d->hook = hook;
    d->default_id = 1;
    AudioSettings s;
    dosint_get_settings(&s);
    add(d, 5, 5, 90, 8, -1, 0x50000000, 0x82, "Music:");
    add(d, 10, 15, 80, 8, 301, 0x50030004, 0x80, "Windows");
    add(d, 10, 25, 80, 8, 302, 0x50000004, 0x80, "Tandy 1000");
    add(d, 10, 35, 80, 8, 303, 0x50000004, 0x80, "PC speaker");
    add(d, 105, 5, 90, 8, -1, 0x50000000, 0x82, "Sound effects:");
    add(d, 110, 15, 80, 8, 311, 0x50030004, 0x80, "Windows");
    add(d, 110, 25, 80, 8, 312, 0x50000004, 0x80, "Tandy 1000");
    add(d, 110, 35, 80, 8, 313, 0x50000004, 0x80, "PC speaker");
    add(d, 200, 5, 80, 8, -1, 0x50000000, 0x82, "Volume:");
    add(d, 205, 15, 28, 8, -1, 0x50000000, 0x82, "Music");
    add_scroll(d, 234, 15, 32, 8, 340, s.vol[0]);
    add(d, 268, 15, 14, 8, 343, 0x50000002, 0x82, "100");
    add(d, 205, 25, 28, 8, -1, 0x50000000, 0x82, "Effects");
    add_scroll(d, 234, 25, 32, 8, 341, s.vol[1]);
    add(d, 268, 25, 14, 8, 344, 0x50000002, 0x82, "100");
    add(d, 205, 35, 28, 8, -1, 0x50000000, 0x82, "Speech");
    add_scroll(d, 234, 35, 32, 8, 342, s.vol[2]);
    add(d, 268, 35, 14, 8, 345, 0x50000002, 0x82, "100");
    add(d, 205, 46, 78, 8, 346, 0x50010003, 0x80, "&Even out volume");
    dlg_set_check(d, 346, s.even);
    add(d, 5, 57, 275, 1, -1, 0x50000004, 0x82, "");
    add(d, 5, 62, 275, 8, -1, 0x50000000, 0x82, "DOS game folder or .zip file:");
    add(d, 5, 72, 225, 12, 320, 0x50810080, 0x81, s.dir);
    add(d, 235, 71, 45, 14, 331, 0x50010000, 0x80, "&Install");
    add(d, 5, 87, 275, 16, 321, 0x50000000, 0x82, "");
    add(d, 5, 106, 275, 1, -1, 0x50000004, 0x82, "");
    add(d, 5, 112, 50, 15, 1, 0x50030001, 0x80, "&OK");
    add(d, 117, 112, 50, 15, 330, 0x50010000, 0x80, "&Test");
    add(d, 230, 112, 50, 15, 2, 0x50010000, 0x80, "&Cancel");
    /* the radios show the stored choice; a choice whose files cannot be used is shown as Windows */
    set_radio(d, 301, s.music);
    set_radio(d, 311, s.sfx);
    AdCtx c;
    memset(&c, 0, sizeof c);
    for (int i = 0; i < 3; i++) c.orig_vol[i] = s.vol[i];
    c.orig_even = s.even;
    evlog("DIALOG", AUDIO_DLG_ID, "");
    plat_drops_accept(1);
    dlg_modal(d, validate, &c);
    plat_drops_accept(0);
    dosint_test_stop();
    if (c.tested) { audio_restyle_begin(); audio_restyle_end(); }   /* the test used the music player */
    dlg_free(d);
}
