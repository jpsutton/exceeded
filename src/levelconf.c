/* levelconf.c - the audio-levels conformance command (spec 15 section 7.1) */
#include "levelmeasure.h"
#include "dosint.h"
#include "res.h"
#include "soundfont.h"
#include "util.h"
#include <ctype.h>
#include <math.h>
#include <strings.h>

extern const char *g_opt_dos_game;
extern int g_opt_even;

static void jnum(FILE *f, double v) { if (isnan(v)) fprintf(f, "null"); else fprintf(f, "%.2f", floor(v * 100.0 + 0.5) / 100.0); }

typedef struct Out { FILE *f; int first; int even; int vol[3]; } Out;

static void emit(Out *o, const char *source, const char *bus, const char *style, const char *id_str, int id_num, const Loud *l, int is_wave, int bi, int si) {
    double g = level_asset_gain(si, bi, l, is_wave, o->even);
    int muted;
    double sl = level_slider_db(o->vol[bi], &muted);
    fprintf(o->f, "%s  {\"source\": \"%s\", \"bus\": \"%s\", \"style\": \"%s\", \"id\": ", o->first ? "" : ",\n", source, bus, style);
    if (id_str) fprintf(o->f, "\"%s\"", id_str); else fprintf(o->f, "%d", id_num);
    fprintf(o->f, ", \"lufs\": ");
    if (l->silent) fprintf(o->f, "null"); else jnum(o->f, l->lufs);
    fprintf(o->f, ", \"peak_dbfs\": ");
    if (!l->has_peak) fprintf(o->f, "null"); else jnum(o->f, l->peak_db);
    fprintf(o->f, ", \"gain_db\": %.1f, \"total_db\": ", g);
    if (muted) fprintf(o->f, "null"); else fprintf(o->f, "%.2f", floor((g + sl) * 100.0 + 0.5) / 100.0);
    fprintf(o->f, "}");
    o->first = 0;
}

static const char *STYLE[3] = {"windows", "tandy", "speaker"};
static const char *BUS[3] = {"music", "effects", "speech"};

int cmd_audio_levels(int argc, char **argv, const char *out) {
    extern int g_opt_vol[3], g_opt_nwav;
    extern const char *g_opt_soundfont, *g_opt_wav[64][2];
    Out o = {NULL, 1, g_opt_even >= 0 ? g_opt_even != 0 : 1, {100, 100, 100}};
    const char *sf_cli = g_opt_soundfont;
    const char *(*wavs)[2] = g_opt_wav;
    int nw = g_opt_nwav;
    (void)argc; (void)argv;
    for (int i = 0; i < 3; i++) if (g_opt_vol[i] >= 0) o.vol[i] = g_opt_vol[i];        /* --volumes and the single options: the later one wins; the INI is not read */
    for (int i = 0; i < 3; i++) { if (o.vol[i] < 0) o.vol[i] = 0; if (o.vol[i] > 100) o.vol[i] = 100; }
    o.f = fopen(out, "w");
    if (!o.f) return 1;
    FILE *f = o.f;
    fprintf(f, "{\"targets\": {\"music\": -20, \"effects\": -16, \"speech\": -15},\n \"even_out\": %s, \"volumes\": {\"music\": %d, \"effects\": %d, \"speech\": %d},\n",
            o.even ? "true" : "false", o.vol[0], o.vol[1], o.vol[2]);
    int mu;
    fprintf(f, " \"slider_db\": {");
    for (int b = 0; b < 3; b++) {
        double s = level_slider_db(o.vol[b], &mu);
        fprintf(f, "%s\"%s\": ", b ? ", " : "", BUS[b]);
        if (mu) fprintf(f, "null"); else fprintf(f, "%.2f", floor(s * 100.0 + 0.5) / 100.0);
    }
    fprintf(f, "},\n \"style_gain_db\": {\"windows\": {\"music\": %.1f, \"effects\": %.1f, \"speech\": %.1f}, \"tandy\": {\"music\": %.1f, \"effects\": %.1f}, \"speaker\": {\"music\": %.1f, \"effects\": %.1f}},\n",
            level_style_gain(0, 0), level_style_gain(0, 1), level_style_gain(0, 2), level_style_gain(1, 0), level_style_gain(1, 1), level_style_gain(2, 0), level_style_gain(2, 1));
    char sfbuf[1100];
    const char *sf = nw ? NULL : soundfont_find(sf_cli, NULL, sfbuf, sizeof sfbuf);
    fprintf(f, " \"midi_set\": \"e\", \"soundfont\": ");
    if (sf) fprintf(f, "\"%s\"", sf); else fprintf(f, "null");
    fprintf(f, ",\n \"assets\": [\n");
    if (nw) {
        for (int i = 0; i < nw; i++) {
            int bi = !strcasecmp(wavs[i][1], "music") ? LB_MUSIC : !strcasecmp(wavs[i][1], "speech") ? LB_SPEECH : LB_EFFECTS;
            size_t n;
            uint8_t *b = file_read_all(wavs[i][0], &n);
            Loud l;
            memset(&l, 0, sizeof l);
            l.silent = 1;
            if (b) { measure_wave(b, n, &l); free(b); }
            const char *base = strrchr(wavs[i][0], '/');
            emit(&o, "file", BUS[bi], "windows", base ? base + 1 : wavs[i][0], 0, &l, 1, bi, LS_WINDOWS);
        }
    } else {
        static const char *files[3] = {"SND.DAT", "SSO3.DAT", "SPEECH.DAT"};
        for (int fi = 0; fi < 3; fi++)
            for (int k = 0; k < res_file_count(); k++) {
                ResFile *rf = res_file(k);
                if (strcasecmp(rf->base, files[fi])) continue;
                int ids[4096], nid = 0;
                for (int r = 0; r < rf->nres && nid < 4096; r++) if (!strcmp(rf->res[r].tname, "WAVE") && rf->res[r].id >= 0) ids[nid++] = rf->res[r].id;
                for (int a = 0; a < nid; a++) for (int c = a + 1; c < nid; c++) if (ids[c] < ids[a]) { int t = ids[a]; ids[a] = ids[c]; ids[c] = t; }
                for (int a = 0; a < nid; a++) {
                    size_t len;
                    const uint8_t *w = NULL;
                    for (int r = 0; r < rf->nres; r++) if (!strcmp(rf->res[r].tname, "WAVE") && rf->res[r].id == ids[a]) { w = rf->data + rf->res[r].off; len = rf->res[r].len; break; }
                    Loud l;
                    if (!w || measure_wave(w, len, &l) != 0) { memset(&l, 0, sizeof l); l.silent = 1; }
                    int bi = fi == 2 ? LB_SPEECH : LB_EFFECTS;
                    emit(&o, files[fi], BUS[bi], "windows", NULL, ids[a], &l, 1, bi, LS_WINDOWS);
                }
            }
        /* the 8 MIDI files of the active (extended) set */
        if (sf) {
            static const int slots[8] = {1717, 1724, 1728, 1729, 1735, 1745, 1746, 1757};
            for (int s = 0; s < 8; s++) {
                size_t tl;
                const uint8_t *t = res_get("TEXT", slots[s], &tl);
                if (!t) continue;
                char name[64];
                size_t i = 0;
                while (i < tl && i < 63 && t[i] && t[i] != '\r' && t[i] != '\n') { name[i] = (char)t[i]; i++; }
                name[i] = 0;
                if (!name[0] || !g_data_src) continue;
                char fn[96];
                snprintf(fn, sizeof fn, "MIDI/%s", name);
                fn[5] = 'e';
                size_t ml;
                const uint8_t *mb = vs_read(g_data_src, fn, &ml);
                if (!mb) continue;
                Loud l;
                if (measure_midi(sf, mb, ml, &l) != 0) continue;
                char id[64];
                snprintf(id, sizeof id, "%s", name + 1);
                for (char *c = id; *c; c++) *c = (char)tolower((unsigned char)*c);
                emit(&o, "MIDI", "music", "windows", id, 0, &l, 0, LB_MUSIC, LS_WINDOWS);
            }
        }
        /* DOS songs */
        if (g_opt_dos_game && *g_opt_dos_game) {
            DosGame g;
            char err[300];
            if (dos_game_load(g_opt_dos_game, &g, err, sizeof err) == 0) {
                for (int v = 0; v < 2; v++)
                    for (int s = 0; s < DOS_SONGS; s++) {
                        if (s == 21 || s == 41 || s == 44) continue;
                        Loud l;
                        measure_dos_song(&g, s, v, &l);
                        int bi = (s <= 5 || s == 40 || s == 49) ? LB_MUSIC : LB_EFFECTS;
                        emit(&o, "DOS", BUS[bi], STYLE[v == DOS_TANDY ? LS_TANDY : LS_SPEAKER], NULL, s, &l, 0, bi, v == DOS_TANDY ? LS_TANDY : LS_SPEAKER);
                    }
                dos_game_free(&g);
            }
        }
    }
    fprintf(f, "\n ]}\n");
    fclose(f);
    return 0;
}
