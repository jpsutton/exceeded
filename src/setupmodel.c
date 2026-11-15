/* setupmodel.c - the setup model: rows, messages, buttons, phases, state JSON, and the conformance
 * commands install / resolve (spec chapter 14 sections 4, 6, 7, 10) */
#include "setupui.h"
#include "fsx.h"
#include "res.h"
#include "util.h"
#include "vfs.h"
#include <ctype.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ texts */
static void mb_text(uint64_t bytes, char *out, size_t n) {
    uint64_t t = (bytes * 10 + 500000) / 1000000;
    snprintf(out, n, "%llu.%llu", (unsigned long long)(t / 10), (unsigned long long)(t % 10));
}

static void row_error_text(const Row *r, int which, char *out, size_t n) {
    const char *c = r->error, *nm = r->name, *d = r->detail;
    if (!strcmp(c, "missing_files")) snprintf(out, n, "Incomplete: %s lacks %s", nm, d);
    else if (!strcmp(c, "load_failed")) snprintf(out, n, "The installed game files could not be loaded. Drop the CD again.");
    else if (!strcmp(c, "damaged_install")) snprintf(out, n, "Installed files are damaged (%s). Drop the source again.", d);
    else if (which == 1 && !strcmp(c, "dos_invalid")) snprintf(out, n, "%s: not the expected DOS files (%s).", nm, d);
    else if (which == 1) {
        VErr e;
        snprintf(e.code, sizeof e.code, "%s", c);
        snprintf(e.detail, sizeof e.detail, "%s", d);
        char msg[600];
        vs_format_error("dos", nm, &e, msg, sizeof msg);
        snprintf(out, n, "%s", msg);
    } else if (!strcmp(c, "bad_cue")) snprintf(out, n, "Cannot use the cue sheet %s.", nm);
    else if (!strcmp(c, "not_found")) snprintf(out, n, "%s: the image named in the cue sheet is missing.", nm);
    else if (!strcmp(c, "truncated")) snprintf(out, n, "%s: the CD image is incomplete (%s).", nm, d);
    else if (!strcmp(c, "bad_image")) snprintf(out, n, "%s: the CD image is damaged or uses an unsupported feature (%s).", nm, d);
    else if (!strcmp(c, "no_game_folder")) snprintf(out, n, "%s: no OutNumbered! files (no SSOWINCD.EXE at its top or in a top-level folder).", nm);
    else if (!strcmp(c, "read_error")) snprintf(out, n, "Cannot read %s from %s.", d, nm);
    else snprintf(out, n, "%s: %s (%s)", nm, c, d);
}

void model_row_status(const SetupModel *m, int which, char *out, size_t n) {
    const Row *r = &m->row[which];
    char mb[32];
    switch (r->state) {
    case ROW_NONE: snprintf(out, n, which == 0 ? "Not provided yet." : "Not provided (optional)."); break;
    case ROW_CHECKING: snprintf(out, n, "Checking %s...", r->name); break;
    case ROW_OK:
        if (which == 0) {
            mb_text(r->bytes, mb, sizeof mb);
            if (r->folder[0]) snprintf(out, n, "OK: %s [%s], %d files, %s MB", r->name, r->folder, r->nfiles, mb);
            else snprintf(out, n, "OK: %s, %d files, %s MB", r->name, r->nfiles, mb);
        } else {
            if (r->folder[0]) snprintf(out, n, "OK: %s [%s]", r->name, r->folder);
            else snprintf(out, n, "OK: %s", r->name);
        }
        break;
    case ROW_INSTALLED: snprintf(out, n, "Installed (from %s).", r->name); break;
    default: row_error_text(r, which, out, n); break;
    }
}

static void set_msg(SetupModel *m, int kind, const char *code, const char *detail, const char *home) {
    m->msg.kind = kind;
    snprintf(m->msg.code, sizeof m->msg.code, "%s", code ? code : "");
    char *t = m->msg.text;
    size_t n = sizeof m->msg.text;
    const char *d = detail ? detail : "";
    if (!kind) { t[0] = 0; m->msg.code[0] = 0; return; }
    if (!strcmp(code, "not_found")) snprintf(t, n, "Not found: %s", d);
    else if (!strcmp(code, "unknown_file")) snprintf(t, n, "%s: not an OutNumbered! CD image, game folder or DOS .zip.", d);
    else if (!strcmp(code, "nothing_found")) snprintf(t, n, "No OutNumbered! files found in %s.", d);
    else if (!strcmp(code, "busy_drop")) snprintf(t, n, "Please wait until the files are copied.");
    else if (!strcmp(code, "bad_manifest") || !strcmp(code, "missing_file") || !strcmp(code, "size_mismatch") || !strcmp(code, "crc_mismatch"))
        snprintf(t, n, "The installed game files are damaged or incomplete (%s). Drop the CD again.", !strcmp(code, "bad_manifest") ? "install.json" : d);
    else if (!strcmp(code, "cancelled")) snprintf(t, n, "Installation cancelled. Nothing was changed.");
    else if (!strcmp(code, "installed")) snprintf(t, n, "Setup complete. Starting the game...");
    else if (!strcmp(code, "disk_full")) snprintf(t, n, "Not enough disk space in %s (%s MB needed).", home, d);
    else if (!strcmp(code, "permission")) snprintf(t, n, "Cannot write to %s: permission denied. Try --home DIR.", home);
    else if (!strcmp(code, "path_too_long")) snprintf(t, n, "The path is too long: %s", d);
    else if (!strcmp(code, "read_error")) snprintf(t, n, "Cannot read %s.", d);
    else if (!strcmp(code, "source_changed")) snprintf(t, n, "%s changed after it was checked. Drop it again.", d);
    else if (!strcmp(code, "verify_failed")) snprintf(t, n, "A copied file is damaged (%s). Check the disk and retry.", d);
    else if (!strcmp(code, "busy")) snprintf(t, n, "Another OutNumbered! window is installing. Try again later.");
    else snprintf(t, n, "Installation failed: %s.", d[0] ? d : code);
}

/* ------------------------------------------------------------------ model */
void model_reload_manifest(SetupModel *m) {
    manifest_free(&m->man);
    manifest_load(m->home, &m->man);
}

void model_init(SetupModel *m, const char *home, int load_failed) {
    memset(m, 0, sizeof *m);
    snprintf(m->home, sizeof m->home, "%s", home);
    snprintf(m->last_result, sizeof m->last_result, "none");
    m->countdown = -1;
    m->units_per_tick = 4096;
    model_reload_manifest(m);
    for (int p = 0; p < 2; p++) {
        if (!m->man.valid || !m->man.part[p].present) continue;
        char c[32] = "", d[512] = "";
        if (part_check(m->home, &m->man.part[p], 0, c, sizeof c, d, sizeof d) != 0) continue;      /* fails the fast check: none */
        if (part_check(m->home, &m->man.part[p], 1, c, sizeof c, d, sizeof d) == 0) row_from_part(&m->row[p], &m->man.part[p]);
        else {
            row_from_part(&m->row[p], &m->man.part[p]);
            m->row[p].state = ROW_ERROR;
            snprintf(m->row[p].error, sizeof m->row[p].error, "damaged_install");
            snprintf(m->row[p].detail, sizeof m->row[p].detail, "%s", d);
        }
    }
    if (load_failed) {
        if (m->row[0].state == ROW_NONE) path_last_component("installed game", m->row[0].name, sizeof m->row[0].name);
        m->row[0].state = ROW_ERROR;
        snprintf(m->row[0].error, sizeof m->row[0].error, "load_failed");
    }
}

void model_free(SetupModel *m) {
    for (int i = 0; i < 2; i++) row_clear(&m->row[i]);
    manifest_free(&m->man);
    if (m->inst) inst_free(m->inst);
    free(m->drop_msgs);
    memset(m, 0, sizeof *m);
}

static void log_drop_msg(SetupModel *m, const char *code) {
    m->drop_msgs = xrealloc(m->drop_msgs, sizeof(*m->drop_msgs) * (size_t)(m->ndrop_msgs + 1));
    snprintf(m->drop_msgs[m->ndrop_msgs++], 32, "%s", code);
}

/* ---- asynchronous classification */
typedef struct AsyncJob {
    pthread_t th;
    volatile int done;
    ClassJob *jobs;
    int n;
    Row prev[2];
} AsyncJob;

static void *async_worker(void *p) {
    AsyncJob *a = p;
    for (int i = 0; i < a->n; i++) classjob_stage2(&a->jobs[i]);
    a->done = 1;
    return NULL;
}

static void apply_result(SetupModel *m, ClassResult *r, char *lastmsg, size_t ln, char *lastdetail, size_t dn, int *rows) {
    if (r->win_set) { row_copy(&m->row[0], &r->win); *rows = 1; }
    if (r->dos_set) { row_copy(&m->row[1], &r->dos); *rows = 1; }
    if (r->msg[0]) {
        snprintf(lastmsg, ln, "%s", r->msg);
        snprintf(lastdetail, dn, "%s", r->msg_detail);
        m->drop_msgs = xrealloc(m->drop_msgs, sizeof(*m->drop_msgs) * (size_t)(m->ndrop_msgs + 1));
        snprintf(m->drop_msgs[m->ndrop_msgs++], 32, "%s", r->msg);
    }
}

void model_drop_async(SetupModel *m, char **paths, int n, int from_field) {
    if (m->phase == PH_INSTALLING) { set_msg(m, MK_ERROR, "busy_drop", NULL, m->home); return; }
    if (m->phase == PH_COMPLETE || m->classifying || n <= 0) return;
    AsyncJob *a = xcalloc(1, sizeof *a);
    a->jobs = xcalloc((size_t)n, sizeof(ClassJob));
    a->n = n;
    row_copy(&a->prev[0], &m->row[0]);
    row_copy(&a->prev[1], &m->row[1]);
    for (int i = 0; i < n; i++) {
        classjob_stage1(&a->jobs[i], paths[i]);
        for (int r = 0; r < 2; r++)
            if (r == 0 ? a->jobs[i].w1 : a->jobs[i].d1) {
                row_clear(&m->row[r]);
                m->row[r].state = ROW_CHECKING;
                path_last_component(paths[i], m->row[r].name, sizeof m->row[r].name);
            }
    }
    m->async = a;
    m->async_submit = from_field;
    m->classifying = 1;
    m->focus = from_field ? m->focus : FOCUS_FIELD;
    pthread_create(&a->th, NULL, async_worker, a);
}

void model_poll_async(SetupModel *m) {
    AsyncJob *a = m->async;
    if (!a || !a->done) return;
    pthread_join(a->th, NULL);
    char last[32] = "", lastd[512] = "";
    int rows = 0;
    int got[2] = {0, 0};
    for (int i = 0; i < a->n; i++) {
        ClassResult *r = &a->jobs[i].res;
        char msg[32] = "", d[512] = "";
        int rr = 0;
        apply_result(m, r, msg, sizeof msg, d, sizeof d, &rr);
        if (r->win_set) got[0] = 1;
        if (r->dos_set) got[1] = 1;
        rows |= rr;
        if (msg[0]) { snprintf(last, sizeof last, "%s", msg); snprintf(lastd, sizeof lastd, "%s", d); }
    }
    for (int r = 0; r < 2; r++) if (!got[r] && m->row[r].state == ROW_CHECKING) row_copy(&m->row[r], &a->prev[r]);   /* nothing came of it: back to what it was */
    if (last[0]) set_msg(m, MK_ERROR, last, lastd, m->home); else set_msg(m, MK_NONE, NULL, NULL, m->home);
    if (m->async_submit && rows) { m->field[0] = 0; m->caret = 0; }
    for (int i = 0; i < a->n; i++) classjob_free(&a->jobs[i]);
    free(a->jobs);
    row_clear(&a->prev[0]);
    row_clear(&a->prev[1]);
    free(a);
    m->async = NULL;
    m->classifying = 0;
}

/* returns 1 if the path gave a row result */
static int apply_path(SetupModel *m, const char *path, char *lastmsg, size_t ln, char *lastdetail, size_t dn) {
    ClassResult r;
    classify_path(path, &r);
    int rows = 0;
    if (r.win_set) { row_copy(&m->row[0], &r.win); rows = 1; }
    if (r.dos_set) { row_copy(&m->row[1], &r.dos); rows = 1; }
    if (r.msg[0]) {
        snprintf(lastmsg, ln, "%s", r.msg);
        snprintf(lastdetail, dn, "%s", r.msg_detail);
        log_drop_msg(m, r.msg);
    } else lastmsg[0] = 0;
    classresult_free(&r);
    return rows;
}

void model_drop(SetupModel *m, char **paths, int n) {
    if (m->phase == PH_INSTALLING) { set_msg(m, MK_ERROR, "busy_drop", NULL, m->home); return; }
    if (m->phase == PH_COMPLETE) return;
    char last[32] = "", lastd[512] = "";
    for (int i = 0; i < n; i++) {
        char msg[32], d[512];
        apply_path(m, paths[i], msg, sizeof msg, d, sizeof d);
        if (msg[0]) { snprintf(last, sizeof last, "%s", msg); snprintf(lastd, sizeof lastd, "%s", d); }
    }
    if (last[0]) set_msg(m, MK_ERROR, last, lastd, m->home); else set_msg(m, MK_NONE, NULL, NULL, m->home);
    m->focus = FOCUS_FIELD;
}

void model_submit_text(SetupModel *m, const char *text) {
    if (m->phase == PH_INSTALLING) { set_msg(m, MK_ERROR, "busy_drop", NULL, m->home); return; }
    if (m->phase == PH_COMPLETE) return;
    char msg[32], d[512];
    int rows = apply_path(m, text, msg, sizeof msg, d, sizeof d);
    if (msg[0]) set_msg(m, MK_ERROR, msg, d, m->home); else set_msg(m, MK_NONE, NULL, NULL, m->home);
    if (rows) { m->field[0] = 0; m->caret = 0; }
}

static int last_failed(const SetupModel *m) { return strcmp(m->last_result, "none") && strcmp(m->last_result, "ok") && strcmp(m->last_result, "cancelled"); }

const char *model_b1_label(const SetupModel *m) {
    if (m->row[0].state == ROW_INSTALLED && m->row[1].state != ROW_OK && m->phase != PH_INSTALLING) return "Play";
    if (last_failed(m)) return "Retry";
    return "Install";
}

int model_b1_enabled(const SetupModel *m) {
    if (!strcmp(model_b1_label(m), "Play")) return 1;
    if (m->phase != PH_READY || m->classifying) return 0;
    return m->row[0].state == ROW_OK || (m->row[0].state == ROW_INSTALLED && m->row[1].state == ROW_OK);
}

void model_start_install(SetupModel *m) {
    if (m->phase != PH_READY || !model_b1_enabled(m) || !strcmp(model_b1_label(m), "Play")) return;
    m->inst = inst_new(m->home, m->row[0].state == ROW_OK ? &m->row[0] : NULL, m->row[1].state == ROW_OK ? &m->row[1] : NULL, &m->man,
                       m->row[0].state == ROW_INSTALLED, m->row[1].state == ROW_INSTALLED);
    char code[32] = "";
    if (inst_begin(m->inst, code, sizeof code) != 0) {
        set_msg(m, MK_ERROR, code, inst_detail(m->inst), m->home);
        snprintf(m->last_result, sizeof m->last_result, "%s", code);
        snprintf(m->install_err, sizeof m->install_err, "%s", code);
        inst_free(m->inst);
        m->inst = NULL;
        return;
    }
    set_msg(m, MK_NONE, NULL, NULL, m->home);
    m->phase = PH_INSTALLING;
}

void model_activate_b1(SetupModel *m) {
    if (!model_b1_enabled(m)) return;
    if (!strcmp(model_b1_label(m), "Play")) m->want_start = 1;
    else model_start_install(m);
}

void model_cancel(SetupModel *m) {
    if (m->phase != PH_INSTALLING || !m->inst) return;
    inst_cancel(m->inst);
    inst_free(m->inst);
    m->inst = NULL;
    m->phase = PH_READY;
    set_msg(m, MK_INFO, "cancelled", NULL, m->home);
    snprintf(m->last_result, sizeof m->last_result, "cancelled");
}

void model_tick(SetupModel *m) {
    m->blink++;
    if (m->phase == PH_COMPLETE && m->countdown > 0) { if (--m->countdown == 0) m->want_start = 1; return; }
    if (m->phase != PH_INSTALLING || !m->inst) return;
    char code[32] = "";
    int rc = inst_tick(m->inst, m->units_per_tick, code, sizeof code);
    if (rc == 1) return;
    if (rc == 0) {
        int p0 = inst_parts(m->inst, 0), p1 = inst_parts(m->inst, 1);
        inst_free(m->inst);
        m->inst = NULL;
        model_reload_manifest(m);
        if (p0 && m->man.part[0].present) row_from_part(&m->row[0], &m->man.part[0]);
        if (p1 && m->man.part[1].present) row_from_part(&m->row[1], &m->man.part[1]);
        m->installed_parts[0] |= p0;
        m->installed_parts[1] |= p1;
        m->phase = PH_COMPLETE;
        set_msg(m, MK_SUCCESS, "installed", NULL, m->home);
        snprintf(m->last_result, sizeof m->last_result, "ok");
        m->focus = FOCUS_B1;
        m->countdown = 150;
        return;
    }
    char detail[600];
    snprintf(detail, sizeof detail, "%s", inst_detail(m->inst));
    inst_free(m->inst);
    m->inst = NULL;
    m->phase = PH_READY;
    set_msg(m, MK_ERROR, code, detail, m->home);
    snprintf(m->last_result, sizeof m->last_result, "%s", code);
    snprintf(m->install_err, sizeof m->install_err, "%s", code);
}

/* ------------------------------------------------------------------ JSON */
static void jstr(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static const char *state_name(int s) {
    switch (s) { case ROW_NONE: return "none"; case ROW_CHECKING: return "checking"; case ROW_OK: return "ok"; case ROW_INSTALLED: return "installed"; default: return "error"; }
}

void model_row_json(const Row *r, FILE *f, int with_text, const char *text) {
    int has = r->state != ROW_NONE;
    fprintf(f, "{\"state\": \"%s\", \"error\": ", state_name(r->state));
    if (r->state == ROW_ERROR) fprintf(f, "\"%s\"", r->error); else fprintf(f, "null");
    fprintf(f, ", \"name\": ");
    if (has && r->name[0]) jstr(f, r->name); else fprintf(f, "null");
    fprintf(f, ", \"source_kind\": ");
    if ((r->state == ROW_OK || r->state == ROW_INSTALLED) && r->kind[0]) fprintf(f, "\"%s\"", r->kind); else fprintf(f, "null");
    fprintf(f, ", \"folder\": ");
    if (r->state == ROW_OK || r->state == ROW_INSTALLED) jstr(f, r->folder); else fprintf(f, "null");
    int ok = r->state == ROW_OK || r->state == ROW_INSTALLED;
    fprintf(f, ", \"files\": %d, \"bytes\": %llu", ok ? r->nfiles : 0, (unsigned long long)(ok ? r->bytes : 0));
    if (with_text) { fprintf(f, ", \"text\": "); jstr(f, text); }
    fputc('}', f);
}

void model_state_json(const SetupModel *m, FILE *f) {
    char t0[900], t1[900];
    model_row_status(m, 0, t0, sizeof t0);
    model_row_status(m, 1, t1, sizeof t1);
    fprintf(f, "\"setup\": {\"phase\": \"%s\",\n  \"home\": ", m->phase == PH_READY ? "ready" : m->phase == PH_INSTALLING ? "installing" : "complete");
    jstr(f, m->home);
    fprintf(f, ",\n  \"rows\": {\"windows\": ");
    model_row_json(&m->row[0], f, 1, t0);
    fprintf(f, ", \"dos\": ");
    model_row_json(&m->row[1], f, 1, t1);
    fprintf(f, "},\n  \"field\": ");
    jstr(f, m->field);
    fprintf(f, ", \"caret\": %d, \"focus\": \"%s\",\n  \"button1\": {\"label\": \"%s\", \"enabled\": %s},\n  \"button2\": {\"label\": \"%s\"},\n",
            m->caret, m->focus == FOCUS_FIELD ? "field" : m->focus == FOCUS_B1 ? "button1" : "button2", model_b1_label(m), model_b1_enabled(m) ? "true" : "false",
            m->phase == PH_INSTALLING ? "Cancel" : "Quit");
    fprintf(f, "  \"message\": {\"kind\": \"%s\", \"code\": ", m->msg.kind == MK_NONE ? "none" : m->msg.kind == MK_INFO ? "info" : m->msg.kind == MK_SUCCESS ? "success" : "error");
    if (m->msg.code[0]) fprintf(f, "\"%s\"", m->msg.code); else fprintf(f, "null");
    fprintf(f, ", \"text\": ");
    jstr(f, m->msg.text);
    fprintf(f, "},\n  \"progress\": ");
    if (m->phase == PH_INSTALLING && m->inst) {
        InstProgress p;
        inst_progress(m->inst, &p);
        fprintf(f, "{\"done\": %llu, \"total\": %llu, \"index\": %d, \"count\": %d, \"file\": ", (unsigned long long)p.done, (unsigned long long)p.total, p.index, p.count);
        jstr(f, p.file);
        fprintf(f, ", \"step\": \"%s\"}", p.step);
    } else fprintf(f, "null");
    fprintf(f, ",\n  \"last_result\": \"%s\"}", m->last_result);
}

/* ------------------------------------------------------------------ input normalisation (14 section 4.2) */
char *normalize_input(const char *in, const char *script_dir) {
    char buf[4400];
    snprintf(buf, sizeof buf, "%s", in);
    char *s = buf;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t l = strlen(s);
    while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' || s[l - 1] == '\n')) s[--l] = 0;
    if (l >= 2 && ((s[0] == '"' && s[l - 1] == '"') || (s[0] == '\'' && s[l - 1] == '\''))) { s[l - 1] = 0; s++; l -= 2; }
    char out[4400];
    if (!strncmp(s, "file://", 7)) {
        const char *p = s + 7;
        if (*p != '/') { const char *sl = strchr(p, '/'); p = sl ? sl : p + strlen(p); }       /* host part */
        size_t o = 0;
        for (; *p && o + 1 < sizeof out; p++) {
            if (p[0] == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                char h[3] = {p[1], p[2], 0};
                out[o++] = (char)strtol(h, NULL, 16);
                p += 2;
            } else out[o++] = *p;
        }
        out[o] = 0;
        s = out;
    }
    char home[4400];
    if (s[0] == '~' && s[1] == '/') {
        const char *h = getenv("HOME");
        if (h) { snprintf(home, sizeof home, "%s%s", h, s + 1); s = home; }
    }
    char full[8800];
    if (s[0] == '/' || !script_dir || !*script_dir) { char *a = fsx_abspath(s); snprintf(full, sizeof full, "%s", a); free(a); }
    else snprintf(full, sizeof full, "%s/%s", script_dir, s);
    if (full[0] != '/') { char *a = fsx_abspath(full); snprintf(full, sizeof full, "%s", a); free(a); }
    return xstrdup(full);
}

/* ------------------------------------------------------------------ conformance: install */
static void tree_json(const char *home, FILE *f) {
    char **names;
    int n = fsx_list_files(home, &names);
    int first = 1;
    fprintf(f, "[");
    for (int i = 0; i < n; i++) {
        if (!strcmp(names[i], "install.json") || !strcmp(names[i], "install.lock")) { free(names[i]); continue; }
        char p[4400];
        snprintf(p, sizeof p, "%s/%s", home, names[i]);
        struct stat st;
        stat(p, &st);
        fprintf(f, "%s{\"path\": ", first ? "" : ", ");
        jstr(f, names[i]);
        fprintf(f, ", \"size\": %lld}", (long long)st.st_size);
        first = 0;
        free(names[i]);
    }
    fprintf(f, "]");
    free(names);
}

int conform_install(int argc, char **argv, const char *out) {
    if (!g_opts.home || !*g_opts.home) { fprintf(stderr, "engine: --conform install needs --home DIR.\n"); return 1; }
    char home[1024];
    if (fsx_base_dir(g_opts.home, home, sizeof home, NULL) != 0) return 1;
    SetupModel m;
    model_init(&m, home, 0);
    for (int i = 1; i < argc; i++) {
        char *p = normalize_input(argv[i], NULL);
        model_drop(&m, &p, 1);
        free(p);
    }
    /* snapshot of the rows when the install starts */
    Row snap[2];
    memset(snap, 0, sizeof snap);
    row_copy(&snap[0], &m.row[0]);
    row_copy(&snap[1], &m.row[1]);
    int ran = 0, success = 0;
    char err[32] = "";
    if (!strcmp(model_b1_label(&m), "Install") && model_b1_enabled(&m)) {
        ran = 1;
        m.units_per_tick = 1u << 20;
        model_start_install(&m);
        if (m.phase == PH_INSTALLING) {
            while (m.phase == PH_INSTALLING) model_tick(&m);
            success = m.phase == PH_COMPLETE;
        }
        if (!success) snprintf(err, sizeof err, "%s", m.install_err);
    }
    int ok;
    if (snap[0].state == ROW_ERROR) { ok = 0; snprintf(err, sizeof err, "%s", snap[0].error); }
    else if (ran) ok = success;
    else if (snap[0].state == ROW_INSTALLED) ok = 1;
    else { ok = 0; snprintf(err, sizeof err, "no_game_source"); }
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    fprintf(f, "{\"ok\": %s, \"error\": ", ok ? "true" : "false");
    if (!ok) fprintf(f, "\"%s\"", err); else fprintf(f, "null");
    fprintf(f, ",\n \"rows\": {\"windows\": ");
    model_row_json(&snap[0], f, 0, NULL);
    fprintf(f, ", \"dos\": ");
    model_row_json(&snap[1], f, 0, NULL);
    fprintf(f, "},\n \"messages\": [");
    for (int i = 0; i < m.ndrop_msgs; i++) fprintf(f, "%s\"%s\"", i ? ", " : "", m.drop_msgs[i]);
    fprintf(f, "],\n \"installed\": [");
    int first = 1;
    if (success && m.installed_parts[0]) { fprintf(f, "\"windows\""); first = 0; }
    if (success && m.installed_parts[1]) fprintf(f, "%s\"dos\"", first ? "" : ", ");
    fprintf(f, "],\n \"manifest\": ");
    char mp[1200];
    snprintf(mp, sizeof mp, "%s/install.json", home);
    size_t mn;
    uint8_t *mb = file_read_all(mp, &mn);
    if (mb) { fwrite(mb, 1, mn, f); free(mb); } else fprintf(f, "null");
    fprintf(f, ",\n \"tree\": ");
    tree_json(home, f);
    fprintf(f, "}\n");
    fclose(f);
    row_clear(&snap[0]);
    row_clear(&snap[1]);
    model_free(&m);
    return ok ? 0 : 2;
}

/* ------------------------------------------------------------------ conformance: resolve */
int conform_resolve(int argc, char **argv, const char *out) {
    (void)argc; (void)argv;
    Resolution r;
    char err[300];
    if (setup_resolve(&g_opts, &r, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); return 1; }
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    fprintf(f, "{\"home\": ");
    jstr(f, r.home);
    fprintf(f, ", \"home_from\": \"%s\", \"action\": \"%s\", \"reason\": \"%s\", \"detail\": ", r.home_from, r.setup ? "setup" : "run", r.reason);
    if (r.detail[0]) jstr(f, r.detail); else fprintf(f, "null");
    fprintf(f, ",\n \"data\": ");
    if (r.data[0]) jstr(f, r.data); else fprintf(f, "null");
    fprintf(f, ", \"data_from\": ");
    if (r.data_from) fprintf(f, "\"%s\"", r.data_from); else fprintf(f, "null");
    fprintf(f, ",\n \"dos\": ");
    if (r.dos[0]) jstr(f, r.dos); else fprintf(f, "null");
    fprintf(f, ", \"dos_from\": ");
    if (r.dos_from) fprintf(f, "\"%s\"", r.dos_from); else fprintf(f, "null");
    fprintf(f, ", \"dos_installed\": \"%s\",\n \"save_dir\": ", r.dos_installed);
    jstr(f, r.save_dir);
    fprintf(f, ", \"save_from\": \"%s\", \"home_created\": %s}\n", r.save_from, fsx_isdir(r.home) ? "true" : "false");
    fclose(f);
    return 0;
}
