/* dosconf.c - conformance commands of spec chapter 12 section 7: dos-unpack, dos-check, dos-trace, dos-script, dos-render */
#include "dosaudio.h"
#include "res.h"
#include "util.h"
#include <math.h>
#include <strings.h>

typedef struct Ev { int64_t tick; int kind, val; } Ev;
typedef struct EvList { Ev *e; int n, cap; } EvList;
typedef struct Rec { EvList ch[4]; int64_t tick; } Rec;

static void rec_log(void *ud, int ch, int kind, int val) {
    Rec *r = ud;
    EvList *l = &r->ch[ch];
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->e = xrealloc(l->e, sizeof(Ev) * (size_t)l->cap); }
    l->e[l->n++] = (Ev){r->tick, kind, val};
}
static void rec_free(Rec *r) { for (int i = 0; i < 4; i++) free(r->ch[i].e); }

static const char *KIND[] = {"f", "noise", "v", "on", "off"};

static void write_channels(FILE *f, Rec *r, int nch) {
    fprintf(f, "\"channels\": [");
    for (int c = 0; c < nch; c++) {
        fprintf(f, "%s[", c ? ", " : "");
        for (int i = 0; i < r->ch[c].n; i++) {
            Ev *e = &r->ch[c].e[i];
            fprintf(f, "%s[%lld, \"%s\", %d]", i ? ", " : "", (long long)e->tick, KIND[e->kind], e->val);
        }
        fprintf(f, "]");
    }
    fprintf(f, "]");
}

static const char *opt(int argc, char **argv, const char *name) {
    for (int i = 0; i + 1 < argc; i++) if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}
static int variant_of(int argc, char **argv) {
    const char *v = opt(argc, argv, "--variant");
    if (!v) return -1;
    if (!strcasecmp(v, "tandy")) return DOS_TANDY;
    if (!strcasecmp(v, "speaker")) return DOS_SPEAKER;
    return -1;
}

/* ---------------------------------------------------------------- dos-unpack */
static int cmd_unpack(const char *exe, const char *out) {
    size_t n;
    uint8_t *b = file_read_all(exe, &n);
    if (!b) { fprintf(stderr, "cannot read %s\n", exe); return 2; }
    uint8_t *img;
    size_t in;
    char err[160];
    if (dos_unpack_exe(b, n, &img, &in, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); free(b); return 2; }
    FILE *f = fopen(out, "wb");
    if (!f) { free(b); free(img); return 1; }
    fwrite(img, 1, in, f);
    fclose(f);
    free(b); free(img);
    return 0;
}

/* ---------------------------------------------------------------- dos-check */
static int cmd_check(const char *dir, const char *out) {
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    DosGame g;
    char err[500] = "";
    int rc = dos_game_load(dir, &g, err, sizeof err);
    if (rc) {
        fprintf(f, "{\"ok\": false, \"reason\": \"");
        for (const char *p = err; *p; p++) { if (*p == '"' || *p == '\\') fputc('\\', f); fputc(*p, f); }
        fprintf(f, "\"}\n");
        fclose(f);
        return 0;
    }
    fprintf(f, "{\"ok\": true, \"tables_found\": \"%s\", \"data_segment\": %ld, \"unpacked_size\": %zu, \"unpacked_sha256\": \"", g.scanned ? "scan" : "fixed", g.ds, g.img_n);
    for (int i = 0; i < 32; i++) fprintf(f, "%02x", g.sha[i]);
    fprintf(f, "\",\n \"speaker_divisors\": [");
    for (int i = 0; i < 12; i++) fprintf(f, "%s%u", i ? ", " : "", g.spk[i]);
    fprintf(f, "],\n \"tandy_divisors\": [");
    for (int i = 0; i < 12; i++) fprintf(f, "%s%u", i ? ", " : "", g.tandy[i]);
    fprintf(f, "],\n \"priorities\": [");
    for (int i = 0; i < DOS_SONGS; i++) fprintf(f, "%s%d", i ? ", " : "", g.prio[i]);
    fprintf(f, "],\n \"envelope_pointers\": [");
    for (int i = 0; i < DOS_ENVS; i++) fprintf(f, "%s%d", i ? ", " : "", g.envptr[i]);
    fprintf(f, "],\n \"envelopes\": [");
    for (int i = 0; i < DOS_ENVS; i++) {
        fprintf(f, "%s[", i ? ", " : "");
        for (long p = g.ds + g.envptr[i]; (size_t)p < g.img_n; p++) {
            fprintf(f, "%s%d", p > g.ds + g.envptr[i] ? ", " : "", g.img[p]);
            if (g.img[p] == 0xFF) break;
        }
        fprintf(f, "]");
    }
    fprintf(f, "],\n \"block_tandy\": {\"size\": %zu, \"table_entries\": %d},\n \"block_speaker\": {\"size\": %zu, \"table_entries\": %d}}\n",
            g.blk[DOS_TANDY].size, g.blk[DOS_TANDY].entries, g.blk[DOS_SPEAKER].size, g.blk[DOS_SPEAKER].entries);
    fclose(f);
    dos_game_free(&g);
    return 0;
}

/* ---------------------------------------------------------------- dos-trace */
static int cmd_trace(const DosGame *g, int song, int variant, int ticks, const char *out) {
    Rec r;
    memset(&r, 0, sizeof r);
    DosDrv d;
    memset(&d, 0, sizeof d);
    d.log = rec_log; d.ud = &r;
    dosdrv_init(&d, g, variant);
    dosdrv_play(&d, song);
    long long end = -1;
    for (int k = 0; k < ticks; k++) {
        r.tick = k;
        dosdrv_tick(&d);
        if (d.state == DOS_IDLE) { end = k; break; }
    }
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    fprintf(f, "{\"variant\": \"%s\", \"song\": %d, \"ticks\": %d, \"end_tick\": ", variant == DOS_TANDY ? "tandy" : "speaker", song, ticks);
    if (end < 0) fprintf(f, "null"); else fprintf(f, "%lld", end);
    fprintf(f, ",\n ");
    write_channels(f, &r, d.nch);
    fprintf(f, "}\n");
    fclose(f);
    rec_free(&r);
    return 0;
}

/* ---------------------------------------------------------------- dos-script */
typedef struct SCmd { int tick; char op[16]; int arg; } SCmd;

static int cmd_script(const DosGame *g, const char *file, int variant, const char *out) {
    size_t n;
    uint8_t *b = file_read_all(file, &n);
    if (!b) { fprintf(stderr, "cannot read %s\n", file); return 2; }
    char *txt = xmalloc(n + 1);
    memcpy(txt, b, n);
    txt[n] = 0;
    free(b);
    SCmd *cmds = NULL;
    int nc = 0;
    for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        int t = 0, a = 0;
        char op[32] = "";
        int got = sscanf(line, "%d %31s %d", &t, op, &a);
        if (got < 2) continue;
        cmds = xrealloc(cmds, sizeof(SCmd) * (size_t)(nc + 1));
        cmds[nc].tick = t;
        snprintf(cmds[nc].op, sizeof cmds[nc].op, "%s", op);
        cmds[nc].arg = got >= 3 ? a : 0;
        nc++;
    }
    free(txt);
    int endtick = 0;
    for (int i = 0; i < nc; i++) if (!strcmp(cmds[i].op, "end")) { endtick = cmds[i].tick; break; }
    Rec r;
    memset(&r, 0, sizeof r);
    DosDrv d;
    memset(&d, 0, sizeof d);
    d.log = rec_log; d.ud = &r;
    dosdrv_init(&d, g, variant);
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    char *reqs = xcalloc(1, 1);
    size_t rl = 0;
    int first = 1;
    int ci = 0;
    for (int k = 0; k <= endtick; k++) {
        r.tick = k;
        for (; ci < nc && cmds[ci].tick <= k; ci++) {
            SCmd *c = &cmds[ci];
            char line[160];
            const char *op = c->op;
            if (!strcmp(op, "play") || !strcmp(op, "force")) {
                int res = !strcmp(op, "play") ? dosdrv_play(&d, c->arg) : dosdrv_force(&d, c->arg);
                snprintf(line, sizeof line, "[%d, \"%s\", %d, \"%s\"]", c->tick, op, c->arg, dos_result_text(res));
            } else if (!strcmp(op, "stop")) { dosdrv_stop(&d); snprintf(line, sizeof line, "[%d, \"stop\", \"done\"]", c->tick); }
            else if (!strcmp(op, "loop")) { dosdrv_set_loop(&d, c->arg); snprintf(line, sizeof line, "[%d, \"loop\", %d, \"done\"]", c->tick, c->arg); }
            else if (!strcmp(op, "musicoff")) { d.music_off = c->arg; snprintf(line, sizeof line, "[%d, \"musicoff\", %d, \"done\"]", c->tick, c->arg); }
            else if (!strcmp(op, "soundoff")) { d.sound_on = !c->arg; snprintf(line, sizeof line, "[%d, \"soundoff\", %d, \"done\"]", c->tick, c->arg); }
            else if (!strcmp(op, "end")) snprintf(line, sizeof line, "[%d, \"end\", \"done\"]", c->tick);
            else { fprintf(stderr, "dos-script: unknown command '%s'\n", op); continue; }
            size_t ln = strlen(line);
            reqs = xrealloc(reqs, rl + ln + 3);
            if (!first) { reqs[rl++] = ','; reqs[rl++] = ' '; }
            memcpy(reqs + rl, line, ln);
            rl += ln;
            reqs[rl] = 0;
            first = 0;
        }
        if (k < endtick) dosdrv_tick(&d);
    }
    fprintf(f, "{\"variant\": \"%s\", \"ticks\": %d, \"requests\": [%s],\n ", variant == DOS_TANDY ? "tandy" : "speaker", endtick, reqs);
    write_channels(f, &r, d.nch);
    fprintf(f, "}\n");
    fclose(f);
    free(reqs); free(cmds);
    rec_free(&r);
    return 0;
}

/* ---------------------------------------------------------------- dos-render */
static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, unsigned v) { put16(f, (int)(v & 0xFFFF)); put16(f, (int)(v >> 16)); }

static int cmd_render(const DosGame *g, int song, int variant, int ticks, int rate, const char *out) {
    size_t total;
    float *f = dos_render_float(g, song, variant, rate, ticks, &total);
    int16_t *pcm = xmalloc((total + 1) * sizeof(int16_t));
    for (size_t i = 0; i < total; i++) { float v = f[i]; if (v > 1) v = 1; if (v < -1) v = -1; pcm[i] = (int16_t)lrintf(v * 32767.0f); }
    free(f);
    FILE *fo = fopen(out, "wb");
    if (!fo) return 1;
    fwrite("RIFF", 1, 4, fo); put32(fo, (unsigned)(36 + total * 2)); fwrite("WAVEfmt ", 1, 8, fo);
    put32(fo, 16); put16(fo, 1); put16(fo, 1); put32(fo, (unsigned)rate); put32(fo, (unsigned)rate * 2); put16(fo, 2); put16(fo, 16);
    fwrite("data", 1, 4, fo); put32(fo, (unsigned)(total * 2));
    if (total) fwrite(pcm, 2, total, fo);
    fclose(fo);
    free(pcm);
    return 0;
}

/* returns -1 if cmd is not a DOS command */
int dosconf_main(const char *dosdir, int argc, char **argv, const char *out) {
    const char *cmd = argv[0];
    if (strncmp(cmd, "dos-", 4)) return -1;
    if (!out) { fprintf(stderr, "--out is required\n"); return 2; }
    if (!strcmp(cmd, "dos-unpack") && argc >= 2) return cmd_unpack(argv[1], out);
    if (!strcmp(cmd, "dos-check") && argc >= 2) return cmd_check(argv[1], out);
    int variant = variant_of(argc, argv);
    if (variant < 0) { fprintf(stderr, "--variant tandy|speaker is required\n"); return 2; }
    if (!dosdir) { fprintf(stderr, "--dos-game DIR is required\n"); return 2; }
    DosGame g;
    char err[500];
    if (dos_game_load(dosdir, &g, err, sizeof err) != 0) { fprintf(stderr, "DOS audio unavailable: %s\n", err); return 2; }
    int rc = 2;
    const char *tk = opt(argc, argv, "--ticks"), *rt = opt(argc, argv, "--rate");
    if (!strcmp(cmd, "dos-trace") && argc >= 2) rc = cmd_trace(&g, atoi(argv[1]), variant, tk ? atoi(tk) : 4000, out);
    else if (!strcmp(cmd, "dos-script") && argc >= 2) rc = cmd_script(&g, argv[1], variant, out);
    else if (!strcmp(cmd, "dos-render") && argc >= 2) rc = cmd_render(&g, atoi(argv[1]), variant, tk ? atoi(tk) : 60000, rt ? atoi(rt) : 44100, out);
    else fprintf(stderr, "conform command '%s' not implemented (or bad arguments)\n", cmd);
    dos_game_free(&g);
    return rc;
}
