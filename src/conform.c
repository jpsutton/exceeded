#include "conform.h"
#include "vfs.h"
#include "setupui.h"
#include "aseq.h"
#include "gfx.h"
#include "mtht.h"
#include "png.h"
#include "res.h"
#include "rng.h"
#include "rules.h"
#include "save.h"
#include "game.h"

/* ------------------------------------------------------------ list-resources */
static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20 || c >= 0x7F) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static int cmd_list_resources(const char *out) {
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    fprintf(f, "{\n \"files\": [\n");
    for (int i = 0; i < res_file_count(); i++) {
        ResFile *rf = res_file(i);
        fprintf(f, "  {\"file\": ");
        json_str(f, rf->base);
        fprintf(f, ", \"align_shift\": %d, \"count\": %d, \"resources\": [\n", rf->shift, rf->nres);
        for (int k = 0; k < rf->nres; k++) {
            const Res *r = &rf->res[k];
            fprintf(f, "   {\"type\": ");
            json_str(f, r->tname);
            if (r->id >= 0) fprintf(f, ", \"id\": %d", r->id);
            else { fprintf(f, ", \"name\": "); json_str(f, r->name); }
            fprintf(f, ", \"offset\": %u, \"length\": %u}%s\n", r->off, r->len, k + 1 < rf->nres ? "," : "");
        }
        fprintf(f, "  ]}%s\n", i + 1 < res_file_count() ? "," : "");
    }
    fprintf(f, " ]\n}\n");
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- render-cel */
static int cmd_render_cel(int aseq_id, int celno, const char *out) {
    if (pal_load(128) != 0) { fprintf(stderr, "RRGB 128 not found\n"); return 1; }
    Aseq *a = aseq_load(aseq_id);
    if (!a) { fprintf(stderr, "ASEQ %d not found\n", aseq_id); return 1; }
    const Cel *c = aseq_cel(a, celno);
    if (!c) { fprintf(stderr, "ASEQ %d has no cel %d\n", aseq_id, celno); return 1; }
    int w = c->w, h = c->h;
    uint8_t *rgba = xcalloc((size_t)(w ? w : 1) * (size_t)(h ? h : 1), 4);
    for (int i = 0; i < w * h; i++) {
        if (!c->mask[i]) continue;
        rgba[i * 4 + 0] = g_pal[c->pix[i]][0];
        rgba[i * 4 + 1] = g_pal[c->pix[i]][1];
        rgba[i * 4 + 2] = g_pal[c->pix[i]][2];
        rgba[i * 4 + 3] = 255;
    }
    int rc = png_write_rgba(out, w, h, rgba);
    free(rgba);
    return rc ? 1 : 0;
}

/* --------------------------------------------------------------- aseq-trace */
static int cmd_aseq_trace(int aseq_id, int seq, int n, const char *out) {
    eng_init();
    Aseq *a = aseq_load(aseq_id);
    if (!a) { fprintf(stderr, "ASEQ %d not found\n", aseq_id); return 1; }
    if (!aseq_get_seq(a, seq)) { fprintf(stderr, "ASEQ %d has no sequence %d\n", aseq_id, seq); return 1; }
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    int period = 100 / a->fps;
    if (period < 1) period = 1;
    fprintf(f, "{\n \"resource\": \"ASEQ %d\",\n \"nseq\": %d,\n \"ncel\": %d,\n \"fps\": %d,\n \"frame_period_ticks\": %d,\n",
            aseq_id, a->nseq, a->ncel, a->fps, period);
    fprintf(f, " \"cels\": [\n");
    for (int i = 0; i < a->ncel; i++) {
        const Cel *c = aseq_cel(a, i);
        fprintf(f, "  {\"w\": %d, \"h\": %d", c ? c->w : 0, c ? c->h : 0);
        if (c && c->w * c->h <= 256) {
            fprintf(f, ", \"rows\": [");
            for (int y = 0; y < c->h; y++) {
                fprintf(f, "%s[", y ? ", " : "");
                for (int x = 0; x < c->w; x++)
                    fprintf(f, "%s%d", x ? ", " : "", c->mask[y * c->w + x] ? c->pix[y * c->w + x] : -1);
                fprintf(f, "]");
            }
            fprintf(f, "]");
        }
        fprintf(f, "}%s\n", i + 1 < a->ncel ? "," : "");
    }
    fprintf(f, " ],\n \"trace_seq%d\": [\n", seq);
    AO *ao = ao_create(aseq_id);
    ao_start(ao, seq);
    for (int i = 0; i < n; i++) {
        g_eng.now = (int64_t)i * period;
        eng_run_aos();
        fprintf(f, "  ");
        if (ao->state == AO_STOPPED) fprintf(f, "{\"tick\": %d, \"state\": \"stopped\"}", i);
        else {
            fprintf(f, "{\"tick\": %d, \"z\": %d, \"cels\": [", i, ao->z);
            for (int k = 0; k < ao->nse; k++) {
                SE *se = ao->se[k];
                int cn = (int)(se->cel - a->cel);
                fprintf(f, "%s{\"cel\": %d, \"x\": %d, \"y\": %d, \"mirror\": %s}", k ? ", " : "", cn, se->x, se->y,
                        (se->flags & SE_MIRROR) ? "true" : "false");
            }
            fprintf(f, "]}");
        }
        fprintf(f, "%s\n", i + 1 < n ? "," : "");
    }
    fprintf(f, " ]\n}\n");
    fclose(f);
    ao_destroy(ao);
    eng_shutdown();
    return 0;
}

/* --------------------------------------------------------------------- text */
static int cmd_text(int font_id, const char *text, const char *out) {
    Font *fnt = font_load(font_id);
    if (!fnt) { fprintf(stderr, "NFNT %d not found\n", font_id); return 1; }
    int penx = 1, base = fnt->ascent;
    int pen_end = penx + font_text_width(fnt, text);
    int h = fnt->fRectH;
    /* width = max(pen_end_x, rightmost set pixel + 1) + 1 (README 3.1): render wide, then measure */
    int w = pen_end + fnt->widMax + 16;
    uint8_t *mask = xcalloc((size_t)w * h, 1);
    font_draw_mask(fnt, mask, w, h, penx, base, text, NULL);
    int right = pen_end;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (mask[y * w + x] && x + 1 > right) right = x + 1;
    {
        int nw = right + 1;
        uint8_t *m2 = xcalloc((size_t)nw * h, 1);
        for (int y = 0; y < h; y++) memcpy(m2 + y * nw, mask + y * w, (size_t)nw);
        free(mask);
        mask = m2;
        w = nw;
    }
    uint8_t *rgba = xmalloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        uint8_t v = mask[i] ? 0 : 255;
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = 255;
    }
    int rc = png_write_rgba(out, w, h, rgba);
    free(mask);
    free(rgba);
    return rc ? 1 : 0;
}

/* --------------------------------------------------------------------- mtht */
static int cmd_mtht(int id, int seed, int qnum, const char *out) {
    MthtInst m;
    if (mtht_run(id, seed, qnum, &m) != 0) { fprintf(stderr, "MTHT %d not found\n", id); return 1; }
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    fprintf(f, "{\"resource\": \"MTHT %d\", \"seed\": %d, \"question_number\": %d, \"answer\": %d,\n \"question\": ", id, seed, qnum, m.answer);
    json_str(f, m.question);
    fprintf(f, ",\n \"hint\": ");
    json_str(f, m.hint);
    fprintf(f, ",\n \"buffer_hex\": \"");
    for (int i = 0; i < m.bufsize; i++) fprintf(f, "%02x", m.buf[i]);
    fprintf(f, "\"}\n");
    fclose(f);
    mtht_free(&m);
    return 0;
}

/* --------------------------------------------------------------- rules */
static void jhex_cust(FILE *f, const uint8_t *c) {
    for (int i = 0; i < 13; i++) fprintf(f, "%02x", c[i]);
}

static int cmd_drill(int tier, int seed, int n, const char *out) {
    if (tier < 0 || tier > 3) return 2;
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    DrillDeck *d = xmalloc(sizeof *d);
    deck_build(d, tier_cust[tier]);
    rnga_seed((uint16_t)seed);
    fprintf(f, "{\"tier\": %d, \"srand\": %d, \"customisation\": \"", tier, seed);
    jhex_cust(f, tier_cust[tier]);
    fprintf(f, "\", \"deck_size\": %d,\n \"questions\": [\n", d->n);
    for (int i = 0; i < n; i++) {
        Question q;
        question_make(&q, d, tier_cust[tier]);
        fprintf(f, "  {\"fact\": [%d, %d, %d], \"format\": %d, \"display\": \"%s\", \"answer\": %d}%s\n", q.fact.op,
                q.fact.lo, q.fact.hi, q.format, q.display, q.answer, i + 1 < n ? "," : "");
    }
    fprintf(f, " ]}\n");
    fclose(f);
    free(d);
    return 0;
}

static int cmd_puzzle(int tier, int seed, int games, const char *out) {
    if (tier < 0 || tier > 3) return 2;
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    puzzle_reset();
    rnga_seed((uint16_t)seed);
    fprintf(f, "{\"tier\": %d, \"srand\": %d, \"games\": [\n", tier, seed);
    for (int g = 0; g < games; g++) {
        Puzzle p;
        puzzle_generate(tier, 0, &p);   /* original behaviour, defects included */
        fprintf(f, "  {\"code\": [%d, %d, %d, %d], \"rooms\": [", p.code[0], p.code[1], p.code[2], p.code[3]);
        for (int r = 0; r < 5; r++)
            fprintf(f, "%s[%d, %d, %d, %d]", r ? ", " : "", p.room[r][0], p.room[r][1], p.room[r][2], p.room[r][3]);
        fprintf(f, "], \"hideout\": %d, \"rule\": %d}%s\n", p.hideout, p.rule, g + 1 < games ? "," : "");
    }
    fprintf(f, " ]}\n");
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- dispatch */
int conform_main(const char *data_dir, const char *save_dir, int argc, char **argv, const char *out) {
    if (argc >= 2 && !strcmp(argv[0], "save-dump")) {
        if (!out) { fprintf(stderr, "--out is required\n"); return 2; }
        return savedb_dump_json(argv[1], out);
    }
    if (argc < 1) { fprintf(stderr, "--conform needs a command\n"); return 2; }
    if (!strcmp(argv[0], "install") || !strcmp(argv[0], "resolve")) {
        if (!out) { fprintf(stderr, "--out is required\n"); return 2; }
        return !strcmp(argv[0], "install") ? conform_install(argc, argv, out) : conform_resolve(argc, argv, out);
    }
    if (!strcmp(argv[0], "list-files")) {
        extern int cmd_list_files(int, char **, const char *);
        if (!out) { fprintf(stderr, "--out is required\n"); return 2; }
        return cmd_list_files(argc, argv, out);
    }
    if (!strcmp(argv[0], "selftest-panel")) {
        if (data_dir && data_source_init(data_dir, 1) < 0) return 2;
        extern void ending_selftest_panel(const char *);
        plat_init(PLAT_SCRIPT);
        if (script_load(argv[1], out) != 0) return 2;
        eng_init();
        pal_load(128);
        world_load();
        audio_init(data_dir);
        ScriptStart st;
        script_get_start(&st);
        ending_selftest_panel(NULL);
        script_finish(0);
    }
    if (argc >= 2 && !strcmp(argv[0], "play")) {
        if (data_dir && data_source_init(data_dir, 1) < 0) return 2;
        if (!out) { fprintf(stderr, "--out is required\n"); return 2; }
        shell_play(data_dir, save_dir, argv[1], out);
        return 0;
    }
    const char *cmd = argv[0];
    {
        extern const char *g_opt_dos_game;
        extern int dosconf_main(const char *, int, char **, const char *);
        int r = dosconf_main(g_opt_dos_game, argc, argv, out);
        if (r >= 0) return r;
    }
    if (data_dir && data_source_init(data_dir, 1) < 0) return 2;
    if (!out) { fprintf(stderr, "--out is required\n"); return 2; }

    if (!strcmp(cmd, "audio-levels")) { extern int cmd_audio_levels(int, char **, const char *); return cmd_audio_levels(argc, argv, out); }
    if (!strcmp(cmd, "list-resources")) return cmd_list_resources(out);
    if (!strcmp(cmd, "render-cel") && argc >= 3) return cmd_render_cel(atoi(argv[1]), atoi(argv[2]), out);
    if (!strcmp(cmd, "aseq-trace") && argc >= 4) return cmd_aseq_trace(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), out);
    if (!strcmp(cmd, "mtht") && argc >= 4) return cmd_mtht(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), out);
    if (!strcmp(cmd, "drill") && argc >= 4) return cmd_drill(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), out);
    if (!strcmp(cmd, "puzzle") && argc >= 4) return cmd_puzzle(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), out);
    if (!strcmp(cmd, "text") && argc >= 3) return cmd_text(atoi(argv[1]), argv[2], out);
    fprintf(stderr, "conform command '%s' not implemented (or bad arguments)\n", cmd);
    return 2;
}

