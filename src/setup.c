/* setup.c - per-user installation: manifest, resolution, classification, installer (spec chapter 14) */
#include "setup.h"
#include "dosaudio.h"
#include "dosint.h"
#include "fsx.h"
#include "json.h"
#include "res.h"
#include "util.h"
#include "vfs.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>
#include <unistd.h>

Resolution g_run;
SetupOpts g_opts;
int g_run_valid;

static void setcode(char *dst, size_t n, const char *s) { if (dst && n) snprintf(dst, n, "%s", s); }

/* ------------------------------------------------------------------ manifest */
void manifest_free(Manifest *m) {
    for (int i = 0; i < 2; i++) { free(m->part[i].files); m->part[i].files = NULL; m->part[i].nfiles = 0; m->part[i].present = 0; }
}

static int cmp_sfile(const void *a, const void *b) { return strcmp(((const SFile *)a)->path, ((const SFile *)b)->path); }

static int ok_rel_path(const char *p, const char *prefix) {
    size_t pl = strlen(prefix);
    if (strncmp(p, prefix, pl) || !p[pl]) return 0;
    if (strchr(p, '\\')) return 0;
    const char *c = p;
    while (*c) {
        const char *e = strchr(c, '/');
        size_t l = e ? (size_t)(e - c) : strlen(c);
        if (l == 0 || (l == 1 && c[0] == '.') || (l == 2 && c[0] == '.' && c[1] == '.')) return 0;
        if (!e) break;
        c = e + 1;
    }
    return 1;
}

static int parse_part(const J *o, int which, SPart *p) {
    static const char *prefix[2] = {"game/", "dos/"};
    if (!o || o->t == J_NULL) return o ? 0 : -1;          /* a missing key is invalid, null is "not installed" */
    if (o->t != J_OBJ) return -1;
    const J *src = json_get(o, "source"), *kind = json_get(o, "source_kind"), *fold = json_get(o, "folder"), *files = json_get(o, "files");
    if (!files || files->t != J_ARR) return -1;
    if (src && src->t == J_STR) snprintf(p->source, sizeof p->source, "%s", src->s);
    if (kind && kind->t == J_STR) snprintf(p->kind, sizeof p->kind, "%s", kind->s);
    if (fold && fold->t == J_STR) snprintf(p->folder, sizeof p->folder, "%s", fold->s);
    p->files = xcalloc((size_t)files->n + 1, sizeof(SFile));
    for (int i = 0; i < files->n; i++) {
        const J *e = files->kid[i];
        if (e->t != J_OBJ) return -1;
        const J *pa = json_get(e, "path"), *sz = json_get(e, "size"), *cr = json_get(e, "crc32");
        if (!pa || pa->t != J_STR || !ok_rel_path(pa->s, prefix[which]) || strlen(pa->s) >= sizeof p->files[i].path) return -1;
        if (!sz || sz->t != J_INT || sz->i < 0) return -1;
        if (!cr || cr->t != J_STR || strlen(cr->s) != 8) return -1;
        uint32_t v = 0;
        for (int k = 0; k < 8; k++) {
            int c = (unsigned char)cr->s[k];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return -1;
            v = (v << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        snprintf(p->files[i].path, sizeof p->files[i].path, "%s", pa->s);
        p->files[i].size = (uint64_t)sz->i;
        p->files[i].crc = v;
        p->nfiles++;
    }
    /* required files */
    static const char *req_game[7] = {"SSOWINCD.EXE", "SSO1.DAT", "SSO2.DAT", "SSO3.DAT", "SND.DAT", "SPEECH.DAT", "SSO.FON"};
    if (which == 0) {
        for (int r = 0; r < 7; r++) {
            char want[64];
            snprintf(want, sizeof want, "game/%s", req_game[r]);
            int found = 0;
            for (int i = 0; i < p->nfiles; i++) if (!strcmp(p->files[i].path, want)) found = 1;
            if (!found) return -1;
        }
    } else {
        int a = 0, b = 0;
        for (int i = 0; i < p->nfiles; i++) { if (!strcmp(p->files[i].path, "dos/MOCKET.DAT")) a++; else if (!strcmp(p->files[i].path, "dos/SSO.EXE")) b++; }
        if (p->nfiles != 2 || a != 1 || b != 1) return -1;
    }
    p->present = 1;
    return 1;
}

void manifest_load(const char *home, Manifest *m) {
    memset(m, 0, sizeof *m);
    char path[2048];
    snprintf(path, sizeof path, "%s/install.json", home);
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) { setcode(m->err, sizeof m->err, "no_manifest"); return; }
    setcode(m->err, sizeof m->err, "bad_manifest");
    if (st.st_size > 1024 * 1024) return;
    size_t n;
    uint8_t *b = file_read_all(path, &n);
    if (!b) return;
    J *j = json_parse((const char *)b, n);
    free(b);
    if (!j || j->t != J_OBJ) { json_free(j); return; }
    const J *fmt = json_get(j, "format");
    if (!fmt || fmt->t != J_INT || fmt->i != 1) { json_free(j); return; }
    int g = parse_part(json_get(j, "game"), 0, &m->part[0]);
    int d = g < 0 ? -1 : parse_part(json_get(j, "dos"), 1, &m->part[1]);
    if (g < 0 || d < 0) { manifest_free(m); json_free(j); return; }
    const J *e = json_get(j, "engine"), *u = json_get(j, "installed_utc");
    if (e && e->t == J_STR) snprintf(m->engine, sizeof m->engine, "%s", e->s);
    if (u && u->t == J_STR) snprintf(m->utc, sizeof m->utc, "%s", u->s);
    json_free(j);
    m->valid = 1;
    m->err[0] = 0;
}

uint64_t part_bytes(const SPart *p) { uint64_t t = 0; for (int i = 0; i < p->nfiles; i++) t += p->files[i].size; return t; }

int part_check(const char *home, const SPart *p, int full, char *code, size_t cn, char *detail, size_t dn) {
    for (int i = 0; i < p->nfiles; i++) {
        char path[2200];
        snprintf(path, sizeof path, "%s/%s", home, p->files[i].path);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) { setcode(code, cn, "missing_file"); setcode(detail, dn, p->files[i].path); return -1; }
        if ((uint64_t)st.st_size != p->files[i].size) { setcode(code, cn, "size_mismatch"); setcode(detail, dn, p->files[i].path); return -1; }
    }
    if (full)
        for (int i = 0; i < p->nfiles; i++) {
            char path[2200];
            snprintf(path, sizeof path, "%s/%s", home, p->files[i].path);
            size_t n;
            uint8_t *b = file_read_all(path, &n);
            if (!b) { setcode(code, cn, "missing_file"); setcode(detail, dn, p->files[i].path); return -1; }
            uint32_t c = vs_crc32(b, n);
            free(b);
            if (n != p->files[i].size) { setcode(code, cn, "size_mismatch"); setcode(detail, dn, p->files[i].path); return -1; }
            if (c != p->files[i].crc) { setcode(code, cn, "crc_mismatch"); setcode(detail, dn, p->files[i].path); return -1; }
        }
    return 0;
}

/* ------------------------------------------------------------------ resolution */
int setup_resolve(const SetupOpts *o, Resolution *r, char *err, size_t en) {
    memset(r, 0, sizeof *r);
    if (fsx_base_dir(o->home, r->home, sizeof r->home, &r->home_from) != 0) {
        snprintf(err, en, "engine: cannot find a per-user data folder; use --home DIR.");
        return -1;
    }
    r->home_exists = fsx_isdir(r->home);
    if (o->save_dir && *o->save_dir) { snprintf(r->save_dir, sizeof r->save_dir, "%s", o->save_dir); r->save_from = "option"; }
    else { snprintf(r->save_dir, sizeof r->save_dir, "%s/saves", r->home); r->save_from = "default"; }
    Manifest m;
    manifest_load(r->home, &m);
    char code[32] = "", detail[512] = "";
    int game_ok = 0;
    snprintf(r->dos_installed, sizeof r->dos_installed, "none");
    if (m.valid) {
        if (m.part[1].present) {
            char c2[32] = "", d2[512] = "";
            if (part_check(r->home, &m.part[1], o->verify, c2, sizeof c2, d2, sizeof d2) == 0) snprintf(r->dos_installed, sizeof r->dos_installed, "ok");
            else {
                snprintf(r->dos_installed, sizeof r->dos_installed, "%s", c2);
                fprintf(stderr, "engine: the installed DOS files in '%s/dos' are damaged (%s); not using them.\n", r->home, d2);
            }
        }
        if (m.part[0].present) game_ok = part_check(r->home, &m.part[0], o->verify, code, sizeof code, detail, sizeof detail) == 0;
        else snprintf(code, sizeof code, "no_manifest");
    } else snprintf(code, sizeof code, "%s", m.err);
    if (o->data && *o->data) {
        r->setup = 0;
        snprintf(r->reason, sizeof r->reason, "explicit_data");
        snprintf(r->data, sizeof r->data, "%s", o->data);
        r->data_from = "option";
    } else if (o->setup) {
        r->setup = 1;
        snprintf(r->reason, sizeof r->reason, "forced");
    } else if (game_ok) {
        r->setup = 0;
        snprintf(r->reason, sizeof r->reason, "installed");
        snprintf(r->data, sizeof r->data, "%s/game", r->home);
        r->data_from = "installed";
    } else {
        r->setup = 1;
        snprintf(r->reason, sizeof r->reason, "%s", code);
        snprintf(r->detail, sizeof r->detail, "%s", detail);
    }
    /* DOS folder: --dos-game > DosGameDir (INI of the save dir) > installed */
    AudioSettings as;
    dosint_peek_ini(r->save_dir, &as);
    if (o->dos_game && *o->dos_game) { snprintf(r->dos, sizeof r->dos, "%s", o->dos_game); r->dos_from = "option"; }
    else if (as.dir[0]) { snprintf(r->dos, sizeof r->dos, "%s", as.dir); r->dos_from = "ini"; }
    else if (!strcmp(r->dos_installed, "ok")) { snprintf(r->dos, sizeof r->dos, "%s/dos", r->home); r->dos_from = "installed"; }
    manifest_free(&m);
    return 0;
}

/* ------------------------------------------------------------------ rows */
void row_clear(Row *r) { free(r->files); memset(r, 0, sizeof *r); }
void row_free(Row *r) { row_clear(r); }
void row_copy(Row *dst, const Row *src) {
    row_clear(dst);
    *dst = *src;
    dst->files = NULL;
    if (src->files) { dst->files = xmalloc(sizeof(SFile) * (size_t)(src->nfiles + 1)); memcpy(dst->files, src->files, sizeof(SFile) * (size_t)src->nfiles); }
}

const char *path_last_component(const char *path, char *buf, size_t n) {
    size_t l = strlen(path);
    while (l > 1 && path[l - 1] == '/') l--;
    size_t s = l;
    while (s > 0 && path[s - 1] != '/') s--;
    if (s == 0 && l == 0) { snprintf(buf, n, "%s", path); return buf; }
    if (s == l) s = 0;                               /* the root itself */
    snprintf(buf, n, "%.*s", (int)(l - s), path + s);
    return buf;
}

void row_from_part(Row *r, const SPart *p) {
    row_clear(r);
    r->state = ROW_INSTALLED;
    snprintf(r->source, sizeof r->source, "%s", p->source);
    path_last_component(p->source, r->name, sizeof r->name);
    snprintf(r->kind, sizeof r->kind, "%s", p->kind);
    snprintf(r->folder, sizeof r->folder, "%s", p->folder);
    r->nfiles = p->nfiles;
    r->bytes = part_bytes(p);
}

/* ------------------------------------------------------------------ classification */
static int cmp_names(const void *a, const void *b) { return strcmp(((const SFile *)a)->path, ((const SFile *)b)->path); }
static void upcase(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }

static void fill_row_common(Row *r, const char *path, VSource *s) {
    snprintf(r->source, sizeof r->source, "%s", path);
    path_last_component(path, r->name, sizeof r->name);
    snprintf(r->kind, sizeof r->kind, "%s", vs_kind(s));
    snprintf(r->folder, sizeof r->folder, "%s", vs_folder(s));
    upcase(r->folder);
}

static const char *REQ[7] = {"SSOWINCD.EXE", "SSO1.DAT", "SSO2.DAT", "SSO3.DAT", "SND.DAT", "SPEECH.DAT", "SSO.FON"};

static void err_row(Row *r, const char *path, const char *code, const char *detail) {
    row_clear(r);
    r->state = ROW_ERROR;
    snprintf(r->error, sizeof r->error, "%s", code);
    snprintf(r->detail, sizeof r->detail, "%s", detail);
    path_last_component(path, r->name, sizeof r->name);
}

/* stage 1: find the rows (14 section 4.3 steps 1-2) */
void classjob_stage1(ClassJob *j, const char *path) {
    memset(j, 0, sizeof *j);
    snprintf(j->path, sizeof j->path, "%s", path);
    if (!fsx_exists(path)) { snprintf(j->res.msg, sizeof j->res.msg, "not_found"); snprintf(j->res.msg_detail, sizeof j->res.msg_detail, "%s", path); return; }
    VErr e;
    if (vs_open_data(path, &j->ws, &e) == 0) {
        char missing[600] = "";
        int nmiss = 0;
        const char *sorted[7];
        for (int i = 0; i < 7; i++) sorted[i] = REQ[i];
        for (int i = 0; i < 7; i++)
            for (int k = i + 1; k < 7; k++) if (strcmp(sorted[k], sorted[i]) < 0) { const char *t = sorted[i]; sorted[i] = sorted[k]; sorted[k] = t; }
        for (int i = 0; i < 7; i++)
            if (vs_find(j->ws, sorted[i]) < 0) { if (nmiss++) strncat(missing, ", ", sizeof missing - strlen(missing) - 1); strncat(missing, sorted[i], sizeof missing - strlen(missing) - 1); }
        if (nmiss) { snprintf(j->wc, sizeof j->wc, "missing_files"); snprintf(j->wd, sizeof j->wd, "%s", missing); vs_close(j->ws); j->ws = NULL; }
        else j->w1 = 1;
    } else { snprintf(j->wc, sizeof j->wc, "%s", e.code); snprintf(j->wd, sizeof j->wd, "%s", e.detail); j->ws = NULL; }
    if (vs_open_dos(path, &j->ds, &e) == 0) j->d1 = 1;
    else { snprintf(j->dc, sizeof j->dc, "%s", e.code); snprintf(j->dd, sizeof j->dd, "%s", e.detail); j->ds = NULL; }
}

/* stage 2: read and check (steps 3-5) */
void classjob_stage2(ClassJob *j) {
    ClassResult *out = &j->res;
    if (out->msg[0]) return;
    VErr e;
    if (j->w1) {
        VSource *s = j->ws;
        SFile *files = xcalloc((size_t)vs_nfiles(s) + 1, sizeof(SFile));
        int nf = 0, rc = 0;
        for (int i = 0; i < vs_nfiles(s) && !rc; i++) {
            char up[512];
            snprintf(up, sizeof up, "%s", vs_file_name(s, i));
            upcase(up);
            int want = 0;
            for (int k = 0; k < 7; k++) if (!strcmp(up, REQ[k])) want = 1;
            if (!strcmp(up, "SSOWINCD.DAT") || !strcmp(up, "SSOWINCD.INI")) want = 1;
            if (!strncmp(up, "MIDI/", 5) && !strchr(up + 5, '/')) { size_t l = strlen(up); if (l > 9 && !strcmp(up + l - 4, ".MID")) want = 1; }
            if (!want) continue;
            const uint8_t *d;
            size_t len;
            if (vs_file_load(s, i, &d, &len, &e) != 0) { snprintf(j->wc, sizeof j->wc, "%s", e.code); snprintf(j->wd, sizeof j->wd, "%s", e.detail); rc = -1; break; }
            snprintf(files[nf].path, sizeof files[nf].path, "%s", up);
            files[nf].size = len;
            files[nf].crc = vs_crc32(d, len);
            nf++;
        }
        if (rc == 0) {
            qsort(files, (size_t)nf, sizeof(SFile), cmp_names);
            Row *r = &out->win;
            r->state = ROW_OK;
            fill_row_common(r, j->path, s);
            r->files = files;
            r->nfiles = nf;
            for (int i = 0; i < nf; i++) r->bytes += files[i].size;
            out->win_set = 1;
        } else { free(files); j->w1 = 0; }
    }
    if (j->d1) {
        VSource *s = j->ds;
        const uint8_t *d[2];
        size_t len[2];
        static const char *names[2] = {"MOCKET.DAT", "SSO.EXE"};
        int bad = 0;
        for (int i = 0; i < 2 && !bad; i++) {
            int k = vs_find(s, names[i]);
            if (k < 0) { snprintf(j->dc, sizeof j->dc, "dos_files_not_found"); snprintf(j->dd, sizeof j->dd, "%s", names[i]); bad = 1; }
            else if (vs_file_load(s, k, &d[i], &len[i], &e) != 0) { snprintf(j->dc, sizeof j->dc, "%s", e.code); snprintf(j->dd, sizeof j->dd, "%s", e.detail); bad = 1; }
        }
        char err[300] = "";
        if (!bad && dos_validate_bytes(d[1], len[1], d[0], len[0], err, sizeof err) != 0) { snprintf(j->dc, sizeof j->dc, "dos_invalid"); snprintf(j->dd, sizeof j->dd, "%s", err); bad = 1; }
        if (!bad) {
            Row *r = &out->dos;
            r->state = ROW_OK;
            fill_row_common(r, j->path, s);
            r->files = xcalloc(3, sizeof(SFile));
            for (int i = 0; i < 2; i++) { snprintf(r->files[i].path, sizeof r->files[i].path, "%s", names[i]); r->files[i].size = len[i]; r->files[i].crc = vs_crc32(d[i], len[i]); r->bytes += len[i]; }
            r->nfiles = 2;
            out->dos_set = 1;
        } else j->d1 = 0;
    }
    if (out->win_set || out->dos_set) return;              /* step 5: the other role's error is ignored */
    int isdir = fsx_isdir(j->path);
    if (isdir) {
        if (strcmp(j->wc, "no_game_folder")) { out->win_set = 1; err_row(&out->win, j->path, j->wc, j->wd); }
        else if (strcmp(j->dc, "dos_files_not_found")) { out->dos_set = 1; err_row(&out->dos, j->path, j->dc, j->dd); }
        else { snprintf(out->msg, sizeof out->msg, "nothing_found"); path_last_component(j->path, out->msg_detail, sizeof out->msg_detail); }
    } else {
        if (strcmp(j->wc, "unrecognised")) { out->win_set = 1; err_row(&out->win, j->path, j->wc, j->wd); }
        else if (strcmp(j->dc, "unrecognised")) { out->dos_set = 1; err_row(&out->dos, j->path, j->dc, j->dd); }
        else { snprintf(out->msg, sizeof out->msg, "unknown_file"); path_last_component(j->path, out->msg_detail, sizeof out->msg_detail); }
    }
}

void classjob_free(ClassJob *j) {
    if (j->ws) vs_close(j->ws);
    if (j->ds) vs_close(j->ds);
    classresult_free(&j->res);
    j->ws = j->ds = NULL;
}

void classresult_free(ClassResult *c) { row_clear(&c->win); row_clear(&c->dos); }

void classify_path(const char *path, ClassResult *out) {
    ClassJob j;
    classjob_stage1(&j, path);
    classjob_stage2(&j);
    *out = j.res;                                   /* the rows now belong to the caller */
    memset(&j.res, 0, sizeof j.res);
    classjob_free(&j);
}

/* DOS role only (Audio Options install): 0 = ok (r filled) */
static int classify_dos(const char *path, Row *r, char *code, size_t cn, char *detail, size_t dn) {
    ClassJob j;
    classjob_stage1(&j, path);
    if (j.res.msg[0]) { setcode(code, cn, "not_found"); setcode(detail, dn, path); classjob_free(&j); return -1; }
    j.w1 = 0;
    if (j.ws) { vs_close(j.ws); j.ws = NULL; }
    classjob_stage2(&j);
    int rc;
    if (j.res.dos_set && j.res.dos.state == ROW_OK) { row_copy(r, &j.res.dos); rc = 0; }
    else { setcode(code, cn, j.dc); setcode(detail, dn, j.dd); rc = -1; }
    classjob_free(&j);
    return rc;
}

/* ------------------------------------------------------------------ installer (14 section 6) */
typedef struct WorkFile { char logical[512]; int part; uint64_t size; uint32_t crc; } WorkFile;

struct Installer {
    char home[1024], tmp[1100], old[1100];
    Row row[2];
    int do_part[2];
    Manifest oldm;
    int row_installed[2];
    WorkFile *w;
    int nw;
    uint64_t total, done;
    int lock_fd;
    int idx, step;                     /* current file, 0 = copy, 1 = verify */
    VSource *vs[2];
    const uint8_t *data;
    size_t dlen;
    uint64_t pos;
    FILE *fp;
    uint32_t crc;
    char detail[600];
    int finished;
};

Installer *inst_new(const char *home, const Row *win_ok, const Row *dos_ok, const Manifest *old, int win_installed_row, int dos_installed_row) {
    Installer *in = xcalloc(1, sizeof *in);
    snprintf(in->home, sizeof in->home, "%s", home);
    snprintf(in->tmp, sizeof in->tmp, "%s/.install-tmp", home);
    snprintf(in->old, sizeof in->old, "%s/.install-old", home);
    if (win_ok && win_ok->state == ROW_OK) { row_copy(&in->row[0], win_ok); in->do_part[0] = 1; }
    if (dos_ok && dos_ok->state == ROW_OK) { row_copy(&in->row[1], dos_ok); in->do_part[1] = 1; }
    if (old) {
        in->oldm = *old;
        for (int i = 0; i < 2; i++) {
            in->oldm.part[i].files = NULL;
            if (old->part[i].files) { in->oldm.part[i].files = xmalloc(sizeof(SFile) * (size_t)(old->part[i].nfiles + 1)); memcpy(in->oldm.part[i].files, old->part[i].files, sizeof(SFile) * (size_t)old->part[i].nfiles); }
        }
    }
    in->row_installed[0] = win_installed_row;
    in->row_installed[1] = dos_installed_row;
    in->lock_fd = -1;
    return in;
}

const char *inst_detail(const Installer *in) { return in->detail; }
uint64_t inst_total_bytes(const Installer *in) { return in->total / 2; }
int inst_parts(const Installer *in, int part) { return in->do_part[part]; }

static void inst_unlock(Installer *in) {
    if (in->lock_fd >= 0) {
        char p[1200];
        snprintf(p, sizeof p, "%s/install.lock", in->home);
        unlink(p);
        close(in->lock_fd);
        in->lock_fd = -1;
    }
}

static void inst_cleanup(Installer *in) {
    if (in->fp) { fclose(in->fp); in->fp = NULL; }
    for (int i = 0; i < 2; i++) if (in->vs[i]) { vs_close(in->vs[i]); in->vs[i] = NULL; }
    fsx_rmtree(in->tmp);
    inst_unlock(in);
}

void inst_free(Installer *in) {
    if (!in) return;
    inst_cleanup(in);
    for (int i = 0; i < 2; i++) { row_clear(&in->row[i]); free(in->oldm.part[i].files); }
    free(in->w);
    free(in);
}

void inst_cancel(Installer *in) { inst_cleanup(in); in->finished = 1; }

static int fail_errno(Installer *in, char *code, size_t cn, int err, const char *what) {
    setcode(code, cn, fsx_error_code(err));
    snprintf(in->detail, sizeof in->detail, "%s: %s", what, strerror(err));
    inst_cleanup(in);
    in->finished = 1;
    return -1;
}

static int fail_code(Installer *in, char *code, size_t cn, const char *c, const char *detail) {
    setcode(code, cn, c);
    snprintf(in->detail, sizeof in->detail, "%s", detail);
    inst_cleanup(in);
    in->finished = 1;
    return -1;
}

int inst_begin(Installer *in, char *code, size_t cn) {
    int e = fsx_mkdirs(in->home, 0700);
    if (e) { setcode(code, cn, fsx_error_code(e)); snprintf(in->detail, sizeof in->detail, "%s: %s", in->home, strerror(e)); return -1; }
    char lp[1200];
    snprintf(lp, sizeof lp, "%s/install.lock", in->home);
    in->lock_fd = open(lp, O_RDWR | O_CREAT, 0600);
    if (in->lock_fd < 0) { e = errno; setcode(code, cn, fsx_error_code(e)); snprintf(in->detail, sizeof in->detail, "%s: %s", lp, strerror(e)); return -1; }
    if (flock(in->lock_fd, LOCK_EX | LOCK_NB) != 0) { close(in->lock_fd); in->lock_fd = -1; setcode(code, cn, "busy"); return -1; }
    fsx_rmtree(in->tmp);
    fsx_rmtree(in->old);
    uint64_t S = 0;
    for (int p = 0; p < 2; p++) if (in->do_part[p]) S += in->row[p].bytes;
    uint64_t freeb = fsx_free_space(in->home);
    if (freeb != UINT64_MAX && freeb < S + 1048576) {
        snprintf(in->detail, sizeof in->detail, "%.1f", (double)(S + 1048576) / 1e6);
        setcode(code, cn, "disk_full");
        inst_unlock(in);
        return -1;
    }
    in->total = 2 * S;
    int n = 0;
    for (int p = 0; p < 2; p++) if (in->do_part[p]) n += in->row[p].nfiles;
    in->w = xcalloc((size_t)n + 1, sizeof(WorkFile));
    for (int p = 0; p < 2; p++) {
        if (!in->do_part[p]) continue;
        for (int i = 0; i < in->row[p].nfiles; i++) {
            WorkFile *w = &in->w[in->nw++];
            snprintf(w->logical, sizeof w->logical, "%s", in->row[p].files[i].path);
            w->part = p;
            w->size = in->row[p].files[i].size;
            w->crc = in->row[p].files[i].crc;
        }
        /* open the source again: a change since stage 2 must be noticed */
        VErr ve;
        int rc = p == 0 ? vs_open_data(in->row[p].source, &in->vs[p], &ve) : vs_open_dos(in->row[p].source, &in->vs[p], &ve);
        if (rc != 0) { in->vs[p] = NULL; return fail_code(in, code, cn, "source_changed", in->row[p].name); }
    }
    e = fsx_mkdirs(in->tmp, 0700);
    if (e) return fail_errno(in, code, cn, e, in->tmp);
    in->idx = 0;
    in->step = 0;
    return 0;
}

void inst_progress(const Installer *in, InstProgress *p) {
    p->done = in->done;
    p->total = in->total;
    p->count = in->nw;
    if (in->done >= in->total || in->idx >= in->nw) {
        p->step = "commit";
        p->index = in->nw;
        snprintf(p->file, sizeof p->file, "%s", in->nw ? in->w[in->nw - 1].logical : "");
        return;
    }
    p->index = in->idx + 1;
    snprintf(p->file, sizeof p->file, "%s", in->w[in->idx].logical);
    p->step = in->step == 0 ? "copy" : "verify";
}

static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p;
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        int ok = len > 0;
        for (int k = 1; k < len && ok; k++) if ((p[k] & 0xC0) != 0x80) ok = 0;
        if (len == 0 || !ok) { fputs("\xEF\xBF\xBD", f); p++; continue; }       /* invalid UTF-8 -> U+FFFD */
        if (c == '"' || c == '\\') { fputc('\\', f); fputc((int)c, f); }
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else for (int k = 0; k < len; k++) fputc(p[k], f);
        p += len;
    }
    fputc('"', f);
}

static void write_part(FILE *f, const char *key, const SPart *p) {
    if (!p->present) { fprintf(f, " \"%s\": null", key); return; }
    fprintf(f, " \"%s\": {\n  \"source\": ", key);
    write_json_string(f, p->source);
    fprintf(f, ",\n  \"source_kind\": \"%s\",\n  \"folder\": ", p->kind);
    write_json_string(f, p->folder);
    fprintf(f, ",\n  \"files\": [\n");
    for (int i = 0; i < p->nfiles; i++) {
        fprintf(f, "   {\"path\": ");
        write_json_string(f, p->files[i].path);
        fprintf(f, ", \"size\": %llu, \"crc32\": \"%08x\"}%s\n", (unsigned long long)p->files[i].size, p->files[i].crc, i + 1 < p->nfiles ? "," : "");
    }
    fprintf(f, "  ]\n }");
}

static int write_manifest(const char *home, const Manifest *m) {
    char tmp[1200], fin[1200];
    snprintf(tmp, sizeof tmp, "%s/install.json.tmp", home);
    snprintf(fin, sizeof fin, "%s/install.json", home);
    FILE *f = fopen(tmp, "wb");
    if (!f) return errno;
    int fd = fileno(f);
    (void)fd;
    fprintf(f, "{\n \"format\": 1,\n \"engine\": ");
    write_json_string(f, m->engine);
    fprintf(f, ",\n \"installed_utc\": \"%s\",\n", m->utc);
    write_part(f, "game", &m->part[0]);
    fprintf(f, ",\n");
    write_part(f, "dos", &m->part[1]);
    fprintf(f, "\n}\n");
    fflush(f);
    fsync(fileno(f));
    int err = ferror(f) ? EIO : 0;
    if (fclose(f) != 0 && !err) err = errno;
    if (err) return err;
    if (chmod(tmp, 0600) != 0) { /* best effort */ }
    if (rename(tmp, fin) != 0) return errno;
    return 0;
}

static void part_from_row(SPart *p, const Row *r, const char *prefix) {
    memset(p, 0, sizeof *p);
    p->present = 1;
    snprintf(p->source, sizeof p->source, "%s", r->source);
    snprintf(p->kind, sizeof p->kind, "%s", r->kind);
    snprintf(p->folder, sizeof p->folder, "%s", r->folder);
    p->files = xcalloc((size_t)r->nfiles + 1, sizeof(SFile));
    for (int i = 0; i < r->nfiles; i++) {
        snprintf(p->files[i].path, sizeof p->files[i].path, "%s%s", prefix, r->files[i].path);
        p->files[i].size = r->files[i].size;
        p->files[i].crc = r->files[i].crc;
    }
    p->nfiles = r->nfiles;
    qsort(p->files, (size_t)p->nfiles, sizeof(SFile), cmp_sfile);
}

static void part_copy(SPart *dst, const SPart *src) {
    *dst = *src;
    dst->files = xmalloc(sizeof(SFile) * (size_t)(src->nfiles + 1));
    memcpy(dst->files, src->files, sizeof(SFile) * (size_t)src->nfiles);
}

static int commit(Installer *in, char *code, size_t cn) {
    static const char *dirs[2] = {"game", "dos"};
    char p1[1300], p2[1300];
    if (in->do_part[0]) {                               /* an interruption from here on leaves no manifest */
        snprintf(p1, sizeof p1, "%s/install.json", in->home);
        unlink(p1);
    }
    for (int p = 0; p < 2; p++) {
        if (!in->do_part[p]) continue;
        snprintf(p1, sizeof p1, "%s/%s", in->home, dirs[p]);
        if (fsx_exists(p1)) {
            int e = fsx_mkdirs(in->old, 0700);
            if (e) return fail_errno(in, code, cn, e, in->old);
            snprintf(p2, sizeof p2, "%s/%s", in->old, dirs[p]);
            if (rename(p1, p2) != 0) return fail_errno(in, code, cn, errno, p1);
        }
        snprintf(p1, sizeof p1, "%s/%s", in->tmp, dirs[p]);
        snprintf(p2, sizeof p2, "%s/%s", in->home, dirs[p]);
        if (rename(p1, p2) != 0) return fail_errno(in, code, cn, errno, p2);
    }
    Manifest nm;
    memset(&nm, 0, sizeof nm);
    snprintf(nm.engine, sizeof nm.engine, "outnumbered-engine %s", ENGINE_VERSION);
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(nm.utc, sizeof nm.utc, "%Y-%m-%dT%H:%M:%SZ", &tm);
    if (in->do_part[0]) part_from_row(&nm.part[0], &in->row[0], "game/");
    else if (in->row_installed[0] && in->oldm.valid && in->oldm.part[0].present) part_copy(&nm.part[0], &in->oldm.part[0]);
    if (in->do_part[1]) part_from_row(&nm.part[1], &in->row[1], "dos/");
    else if (in->row_installed[1] && in->oldm.valid && in->oldm.part[1].present) part_copy(&nm.part[1], &in->oldm.part[1]);
    int e = write_manifest(in->home, &nm);
    manifest_free(&nm);
    if (e) return fail_errno(in, code, cn, e, "install.json");
    fsx_rmtree(in->old);
    fsx_rmtree(in->tmp);
    snprintf(p1, sizeof p1, "%s/saves", in->home);
    fsx_mkdirs(p1, 0700);
    inst_unlock(in);
    in->finished = 1;
    return 0;
}

static int src_data(Installer *in, WorkFile *w, char *code, size_t cn) {
    VSource *s = in->vs[w->part];
    int k = vs_find(s, w->logical);
    if (k < 0) return fail_code(in, code, cn, "source_changed", in->row[w->part].name);
    VErr e;
    if (vs_file_load(s, k, &in->data, &in->dlen, &e) != 0) {
        snprintf(in->detail, sizeof in->detail, "%s", w->logical);
        setcode(code, cn, !strcmp(e.code, "not_found") ? "read_error" : e.code);
        inst_cleanup(in);
        in->finished = 1;
        return -1;
    }
    if (in->dlen != w->size) return fail_code(in, code, cn, "source_changed", in->row[w->part].name);
    return 0;
}

int inst_tick(Installer *in, uint64_t max_units, char *code, size_t cn) {
    if (in->finished) return -2;
    if (in->idx >= in->nw) return commit(in, code, cn) == 0 ? 0 : -1;     /* the tick after the work reached its total */
    uint64_t budget = max_units;
    while (in->idx < in->nw) {
        WorkFile *w = &in->w[in->idx];
        static const char *dirs[2] = {"game", "dos"};
        char path[2300];
        snprintf(path, sizeof path, "%s/%s/%s", in->tmp, dirs[w->part], w->logical);
        if (in->step == 0) {
            if (!in->fp) {
                char dir[2300];
                snprintf(dir, sizeof dir, "%s", path);
                char *sl = strrchr(dir, '/');
                if (sl) { *sl = 0; int e = fsx_mkdirs(dir, 0700); if (e) return fail_errno(in, code, cn, e, dir); }
                if (src_data(in, w, code, cn) != 0) return -1;
                int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                in->fp = fd >= 0 ? fdopen(fd, "wb") : NULL;
                if (!in->fp) return fail_errno(in, code, cn, errno, path);
                in->pos = 0;
                in->crc = 0;
            }
            uint64_t left = w->size - in->pos, n = left < budget ? left : budget;
            if (n == 0 && left > 0) break;
            if (n) {
                if (fwrite(in->data + in->pos, 1, (size_t)n, in->fp) != n) return fail_errno(in, code, cn, errno ? errno : EIO, path);
                in->crc = (uint32_t)crc32(in->crc, in->data + in->pos, (size_t)n);
                in->pos += n; in->done += n; budget -= n;
            }
            if (in->pos == w->size) {
                fflush(in->fp);
                fsync(fileno(in->fp));
                if (fclose(in->fp) != 0) { in->fp = NULL; return fail_errno(in, code, cn, errno ? errno : EIO, path); }
                in->fp = NULL;
                if (in->crc != w->crc) return fail_code(in, code, cn, "source_changed", in->row[w->part].name);
                in->step = 1;
                in->pos = 0;
                in->crc = 0;
            }
        } else {
            if (!in->fp) {
                in->fp = fopen(path, "rb");
                if (!in->fp) return fail_errno(in, code, cn, errno, path);
                in->pos = 0;
                in->crc = 0;
            }
            uint64_t left = w->size - in->pos, n = left < budget ? left : budget;
            if (n == 0 && left > 0) break;
            while (n) {
                uint8_t buf[65536];
                size_t want = n < sizeof buf ? (size_t)n : sizeof buf;
                size_t got = fread(buf, 1, want, in->fp);
                if (got == 0) { in->pos = w->size + 1; break; }
                in->crc = (uint32_t)crc32(in->crc, buf, got);
                in->pos += got; in->done += got; budget -= got; n -= got;
            }
            if (in->pos >= w->size) {
                fclose(in->fp);
                in->fp = NULL;
                if (in->pos != w->size || in->crc != w->crc) return fail_code(in, code, cn, "verify_failed", w->logical);
                in->idx++;
                in->step = 0;
            }
        }
    }
    return 1;
}

/* Audio Options: install only the DOS files of a folder or zip (14 section 9). Synchronous. */
int install_dos_only(const char *home, const char *path, char *code, size_t cn, char *msg, size_t mn) {
    char *abs = fsx_abspath(path);
    Row dos;
    memset(&dos, 0, sizeof dos);
    char c[32] = "", d[512] = "";
    if (classify_dos(abs, &dos, c, sizeof c, d, sizeof d) != 0) {
        setcode(code, cn, c);
        VErr ve;
        snprintf(ve.code, sizeof ve.code, "%s", c);
        snprintf(ve.detail, sizeof ve.detail, "%s", d);
        if (!strcmp(c, "dos_invalid")) snprintf(msg, mn, "not the expected DOS files (%s)", d);
        else vs_format_error("dos", path, &ve, msg, mn);
        free(abs);
        return -1;
    }
    Manifest old;
    manifest_load(home, &old);
    Installer *in = inst_new(home, NULL, &dos, &old, old.valid && old.part[0].present, 0);
    int rc = inst_begin(in, code, cn);
    while (rc == 0) {
        rc = inst_tick(in, 1u << 20, code, cn);
        if (rc == 1) rc = 0;
        else if (rc == 0) { rc = 1; break; }
    }
    if (rc < 0) {
        if (!strcmp(code, "disk_full")) snprintf(msg, mn, "Not enough disk space in %s (%s MB needed).", home, inst_detail(in));
        else if (!strcmp(code, "permission")) snprintf(msg, mn, "Cannot write to %s: permission denied.", home);
        else snprintf(msg, mn, "%s", inst_detail(in)[0] ? inst_detail(in) : code);
    }
    inst_free(in);
    manifest_free(&old);
    row_clear(&dos);
    free(abs);
    return rc == 1 ? 0 : -1;
}
