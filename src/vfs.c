/* vfs.c - input containers (spec chapter 13) */
#include "vfs.h"
#include "util.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

VSource *g_data_src;

enum { K_FOLDER, K_ISO, K_RAW, K_CUE_ISO, K_CUE_RAW, K_ZIP };

typedef struct Ext { uint64_t sector; uint64_t len; } Ext;
typedef struct Ent {
    char name[256];
    int isdir;
    uint64_t size;
    Ext *ext;
    int next;
    char *path;
    int zip;
} Ent;

typedef struct VFile {
    char *name;
    uint64_t size;
    Ent ent;
    const uint8_t *data;
    size_t dlen;
    int loaded, owned, mapped;
} VFile;

typedef struct ZEnt { char *name; unsigned flags, method; uint32_t crc, csize, usize; uint64_t lho; } ZEnt;

struct VSource {
    int kind;
    char *path;
    uint8_t *map;
    size_t msize;
    int raw;
    char folder[1024];
    int folders_found;
    char warning[1000];
    VFile *files;
    int nfiles, cap;
    ZEnt *z;
    int nz;
    long delta;
    Ext root;
};

static int fail(VErr *e, const char *code, const char *fmt, ...) {
    if (e) {
        snprintf(e->code, sizeof e->code, "%s", code);
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e->detail, sizeof e->detail, fmt, ap);
        va_end(ap);
    }
    return -1;
}

uint32_t vs_crc32(const uint8_t *d, size_t n) { return (uint32_t)crc32(0, d, (uInt)n); }

static void upcase(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
static int cmp_upper(const void *a, const void *b) {
    char x[512], y[512];
    snprintf(x, sizeof x, "%s", ((const Ent *)a)->name);
    snprintf(y, sizeof y, "%s", ((const Ent *)b)->name);
    upcase(x); upcase(y);
    return strcmp(x, y);
}

static uint8_t *map_file(const char *path, size_t *size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
    *size = (size_t)st.st_size;
    if (!st.st_size) { close(fd); return (uint8_t *)xcalloc(1, 1); }
    void *m = mmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return m == MAP_FAILED ? NULL : (uint8_t *)m;
}

/* ------------------------------------------------------------------ folder access */
static int fs_children(const char *path, Ent **out, int *n) {
    DIR *d = opendir(path);
    if (!d) return -1;
    Ent *a = NULL;
    int cnt = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[2048];
        snprintf(full, sizeof full, "%s/%s", path, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        a = xrealloc(a, sizeof(Ent) * (size_t)(cnt + 1));
        Ent *e = &a[cnt++];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
        e->isdir = S_ISDIR(st.st_mode);
        e->size = (uint64_t)st.st_size;
        e->path = xstrdup(full);
    }
    closedir(d);
    *out = a;
    *n = cnt;
    return 0;
}

/* ------------------------------------------------------------------ ISO 9660 / raw image */
static int iso_sec(VSource *s, uint64_t n, const uint8_t **p, VErr *e) {
    uint64_t ss = s->raw ? 2352 : 2048;
    if ((n + 1) * ss > s->msize) return fail(e, "truncated", "sector %llu is past the end of the image", (unsigned long long)n);
    const uint8_t *b = s->map + n * ss;
    if (!s->raw) { *p = b; return 0; }
    static const uint8_t sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
    if (memcmp(b, sync, 12)) return fail(e, "bad_image", "raw sector %llu has no sync pattern", (unsigned long long)n);
    if (b[15] == 1) *p = b + 16;
    else if (b[15] == 2) *p = b + 24;
    else return fail(e, "bad_image", "raw sector %llu has unsupported mode %d", (unsigned long long)n, b[15]);
    return 0;
}

static uint32_t le32(const uint8_t *p) { return rd32(p); }

/* read `len` bytes starting at sector `sector` into a new buffer */
static int iso_read(VSource *s, uint64_t sector, uint64_t len, uint8_t **buf, VErr *e) {
    uint8_t *b = xmalloc((size_t)len + 1);
    uint64_t done = 0;
    for (uint64_t k = 0; done < len; k++) {
        const uint8_t *p;
        if (iso_sec(s, sector + k, &p, e) != 0) { free(b); return -1; }
        uint64_t n = len - done < 2048 ? len - done : 2048;
        memcpy(b + done, p, (size_t)n);
        done += n;
    }
    *buf = b;
    return 0;
}

static void iso_name(const uint8_t *id, int len, char *out, size_t n) {
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < n; i++) out[o++] = (char)id[i];
    out[o] = 0;
    char *semi = strrchr(out, ';');
    if (semi) *semi = 0;
    size_t l = strlen(out);
    if (l && out[l - 1] == '.') out[l - 1] = 0;
}

static int iso_children(VSource *s, const Ent *dir, Ent **out, int *n, VErr *e) {
    uint8_t *buf;
    uint64_t total = dir->ext[0].len;
    if (iso_read(s, dir->ext[0].sector, total, &buf, e) != 0) return -1;
    Ent *a = NULL;
    int cnt = 0;
    uint64_t p = 0;
    int rc = 0;
    Ent *pending = NULL;           /* multi-extent run in progress */
    while (p < total) {
        int len_dr = buf[p];
        if (len_dr == 0) { p = (p / 2048 + 1) * 2048; continue; }
        if (len_dr < 34 || (p % 2048) + (uint64_t)len_dr > 2048 || p + (uint64_t)len_dr > total || 33 + buf[p + 32] > len_dr) {
            rc = fail(e, "bad_image", "bad directory record at byte %llu", (unsigned long long)p);
            break;
        }
        const uint8_t *r = buf + p;
        int xar = r[1], flags = r[25], lfi = r[32];
        p += (uint64_t)len_dr;
        if (lfi == 1 && (r[33] == 0 || r[33] == 1)) continue;
        if (flags & 4) continue;
        if (r[26] || r[27]) { rc = fail(e, "bad_image", "interleaved files are not supported"); break; }
        char name[256];
        iso_name(r + 33, lfi, name, sizeof name);
        uint64_t sector = (uint64_t)le32(r + 2) + (uint64_t)xar, dlen = le32(r + 10);
        if (pending) {
            if (strcasecmp(pending->name, name)) { rc = fail(e, "bad_image", "multi-extent file '%s' is incomplete", pending->name); break; }
            pending->ext = xrealloc(pending->ext, sizeof(Ext) * (size_t)(pending->next + 1));
            pending->ext[pending->next++] = (Ext){sector, dlen};
            pending->size += dlen;
            if (!(flags & 0x80)) pending = NULL;
            continue;
        }
        int dup = 0;
        for (int i = 0; i < cnt; i++) if (!strcasecmp(a[i].name, name)) dup = 1;
        a = xrealloc(a, sizeof(Ent) * (size_t)(cnt + 1));
        Ent *en = &a[cnt++];
        memset(en, 0, sizeof *en);
        snprintf(en->name, sizeof en->name, "%s", name);
        en->isdir = (flags & 2) != 0;
        en->size = dlen;
        en->ext = xmalloc(sizeof(Ext));
        en->ext[0] = (Ext){sector, dlen};
        en->next = 1;
        if (dup) { cnt--; free(en->ext); continue; }     /* the first record of a name wins */
        if ((flags & 0x80) && !en->isdir) pending = en;
    }
    free(buf);
    if (rc == 0 && pending) rc = fail(e, "bad_image", "multi-extent file '%s' is incomplete", pending->name);
    if (rc) { free(a); return -1; }
    *out = a;
    *n = cnt;
    return 0;
}

static int iso_open_volume(VSource *s, VErr *e) {
    for (int k = 0; k < 64; k++) {
        const uint8_t *p;
        if (iso_sec(s, 16 + (uint64_t)k, &p, e) != 0) return -1;
        if (memcmp(p + 1, "CD001", 5)) return fail(e, "bad_image", "volume descriptor %d has no CD001 signature", k);
        if (p[0] == 255) break;
        if (p[0] == 1) {
            if (rd16(p + 128) != 2048) return fail(e, "bad_image", "logical block size is %d, not 2048", rd16(p + 128));
            s->root = (Ext){le32(p + 156 + 2), le32(p + 156 + 10)};
            return 0;
        }
        if (k == 63) return fail(e, "bad_image", "no volume descriptor terminator");
    }
    return fail(e, "bad_image", "no primary volume descriptor");
}

/* ------------------------------------------------------------------ zip */
static int name_norm(const char *in, char *out, size_t n) {
    size_t o = 0;
    for (; *in && o + 1 < n; in++) out[o++] = *in == '\\' ? '/' : *in;
    out[o] = 0;
    char *p = out;
    for (;;) {
        if (*p == '/') p++;
        else if (p[0] == '.' && p[1] == '/') p += 2;
        else break;
    }
    memmove(out, p, strlen(p) + 1);
    return (int)strlen(out);
}

static int zip_parse(VSource *s, VErr *e) {
    const uint8_t *m = s->map;
    size_t size = s->msize;
    long pos = -1;
    if (size >= 22) {
        long lo = (long)size - 22 - 65535;
        if (lo < 0) lo = 0;
        for (long q = (long)size - 22; q >= lo; q--)
            if (m[q] == 0x50 && m[q + 1] == 0x4B && m[q + 2] == 5 && m[q + 3] == 6 && (size_t)q + 22 + rd16(m + q + 20) == size) { pos = q; break; }
        if (pos < 0)
            for (long q = (long)size - 22; q >= lo; q--)
                if (m[q] == 0x50 && m[q + 1] == 0x4B && m[q + 2] == 5 && m[q + 3] == 6 && (size_t)q + 22 + rd16(m + q + 20) <= size) { pos = q; break; }
    }
    if (pos < 0) {
        if (size >= 4 && m[0] == 0x50 && m[1] == 0x4B && m[2] == 3 && m[3] == 4) return fail(e, "truncated", "no end-of-central-directory record");
        return fail(e, "unrecognised", "not a zip archive");
    }
    const uint8_t *r = m + pos;
    if ((pos >= 20 && m[pos - 20] == 0x50 && m[pos - 19] == 0x4B && m[pos - 18] == 6 && m[pos - 17] == 7) ||
        rd16(r + 10) == 0xFFFF || rd32(r + 12) == 0xFFFFFFFFu || rd32(r + 16) == 0xFFFFFFFFu) return fail(e, "zip64", "ZIP64");
    if (rd16(r + 4) != 0 || rd16(r + 6) != 0 || rd16(r + 8) != rd16(r + 10)) return fail(e, "bad_zip", "multi-part archive");
    long cd_size = (long)rd32(r + 12), cd_off = (long)rd32(r + 16);
    long delta = pos - (cd_off + cd_size);
    if (delta < 0) return fail(e, "bad_zip", "central directory overlaps the end record");
    s->delta = delta;
    int total = rd16(r + 10);
    size_t p = (size_t)(cd_off + delta);
    for (int i = 0; i < total; i++) {
        if (p + 46 > size || memcmp(m + p, "PK\1\2", 4)) return fail(e, "bad_zip", "bad central directory entry %d", i);
        const uint8_t *c = m + p;
        unsigned nl = rd16(c + 28), xl = rd16(c + 30), cl = rd16(c + 32);
        if (p + 46 + nl > size) return fail(e, "bad_zip", "central directory entry %d is cut off", i);
        char raw[1024], nm[1024];
        size_t cp = nl < sizeof raw - 1 ? nl : sizeof raw - 1;
        memcpy(raw, c + 46, cp);
        raw[cp] = 0;
        int ln = name_norm(raw, nm, sizeof nm);
        p += 46 + nl + xl + cl;
        if (ln == 0 || nm[ln - 1] == '/') continue;
        s->z = xrealloc(s->z, sizeof(ZEnt) * (size_t)(s->nz + 1));
        ZEnt *z = &s->z[s->nz++];
        z->name = xstrdup(nm);
        z->flags = rd16(c + 8);
        z->method = rd16(c + 10);
        z->crc = rd32(c + 16);
        z->csize = rd32(c + 20);
        z->usize = rd32(c + 24);
        z->lho = rd32(c + 42);
        if (rd32(c + 42) == 0xFFFFFFFFu) z->lho = 0xFFFFFFFFull;
        else z->lho += (uint64_t)delta;
    }
    return 0;
}

static int zip_load(VSource *s, int zi, uint8_t **out, size_t *outn, VErr *e) {
    ZEnt *z = &s->z[zi];
    if (z->csize == 0xFFFFFFFFu || z->usize == 0xFFFFFFFFu || z->lho == 0xFFFFFFFFull) return fail(e, "zip64", "%s", z->name);
    if (z->flags & 1) return fail(e, "encrypted", "%s", z->name);
    const uint8_t *m = s->map;
    if (z->lho + 30 > s->msize || memcmp(m + z->lho, "PK\3\4", 4)) return fail(e, "bad_zip", "bad local header of %s", z->name);
    uint64_t ds = z->lho + 30 + rd16(m + z->lho + 26) + rd16(m + z->lho + 28);
    if (ds + z->csize > s->msize) return fail(e, "truncated", "%s", z->name);
    uint8_t *buf = xmalloc((size_t)z->usize + 1);
    if (z->method == 0) {
        if (z->csize != z->usize) { free(buf); return fail(e, "bad_zip", "stored size mismatch in %s", z->name); }
        memcpy(buf, m + ds, z->usize);
    } else if (z->method == 8) {
        z_stream zs;
        memset(&zs, 0, sizeof zs);
        if (inflateInit2(&zs, -15) != Z_OK) { free(buf); return fail(e, "bad_zip", "inflate init failed"); }
        zs.next_in = (Bytef *)(m + ds);
        zs.avail_in = z->csize;
        zs.next_out = buf;
        zs.avail_out = z->usize;
        int r = inflate(&zs, Z_FINISH);
        uLong produced = zs.total_out;
        inflateEnd(&zs);
        if (r != Z_STREAM_END || produced != z->usize) { free(buf); return fail(e, "bad_zip", "deflate data of %s is damaged", z->name); }
    } else {
        free(buf);
        return fail(e, "unsupported_method", "%s\n%u", z->name, z->method);
    }
    if (vs_crc32(buf, z->usize) != z->crc) { free(buf); return fail(e, "bad_crc", "%s", z->name); }
    buf[z->usize] = 0;
    *out = buf;
    *outn = z->usize;
    return 0;
}

/* ------------------------------------------------------------------ source construction */
static void add_file(VSource *s, const char *name, const Ent *en) {
    if (s->nfiles == s->cap) { s->cap = s->cap ? s->cap * 2 : 64; s->files = xrealloc(s->files, sizeof(VFile) * (size_t)s->cap); }
    VFile *f = &s->files[s->nfiles++];
    memset(f, 0, sizeof *f);
    f->name = xstrdup(name);
    f->size = en->size;
    f->ent = *en;
    f->ent.ext = NULL;
    f->ent.path = en->path ? xstrdup(en->path) : NULL;
    if (en->ext) { f->ent.ext = xmalloc(sizeof(Ext) * (size_t)en->next); memcpy(f->ent.ext, en->ext, sizeof(Ext) * (size_t)en->next); }
}

static void free_ents(Ent *a, int n) {
    for (int i = 0; i < n; i++) { free(a[i].ext); free(a[i].path); }
    free(a);
}

static int children(VSource *s, const Ent *dir, Ent **out, int *n, VErr *e) {
    if (s->kind == K_FOLDER) {
        if (fs_children(dir->path, out, n) != 0) return fail(e, "not_found", "cannot read folder %s", dir->path);
        return 0;
    }
    return iso_children(s, dir, out, n, e);
}

static int walk(VSource *s, const Ent *dir, const char *prefix, int depth, VErr *e) {
    Ent *a;
    int n;
    if (children(s, dir, &a, &n, e) != 0) return -1;
    qsort(a, (size_t)n, sizeof(Ent), cmp_upper);
    int rc = 0;
    for (int i = 0; i < n && !rc; i++) {
        char nm[1024];
        snprintf(nm, sizeof nm, "%s%s%s", prefix, prefix[0] ? "/" : "", a[i].name);
        if (a[i].isdir) { if (depth < 8) rc = walk(s, &a[i], nm, depth + 1, e); }
        else add_file(s, nm, &a[i]);
    }
    free_ents(a, n);
    return rc;
}

static int has_file(const Ent *a, int n, const char *name) {
    for (int i = 0; i < n; i++) if (!a[i].isdir && !strcasecmp(a[i].name, name)) return 1;
    return 0;
}

/* find the game folder (13 section 3.1) in a source whose root Entry is given, then collect its files */
static int find_game(VSource *s, const Ent *root, VErr *e) {
    Ent *top;
    int nt;
    if (children(s, root, &top, &nt, e) != 0) return -1;
    int rc = 0;
    if (has_file(top, nt, "SSOWINCD.EXE")) {
        s->folders_found = 1;
        s->folder[0] = 0;
        rc = walk(s, root, "", 0, e);
    } else {
        qsort(top, (size_t)nt, sizeof(Ent), cmp_upper);
        int found = 0, first = -1;
        char names[4][64] = {{0}};
        for (int i = 0; i < nt; i++) {
            if (!top[i].isdir) continue;
            Ent *sub;
            int ns;
            if (children(s, &top[i], &sub, &ns, e) != 0) { rc = -1; break; }
            int q = has_file(sub, ns, "SSOWINCD.EXE");
            free_ents(sub, ns);
            if (!q) continue;
            if (found < 4) snprintf(names[found], sizeof names[found], "%s", top[i].name);
            found++;
            if (first < 0) first = i;
        }
        if (!rc && first < 0) rc = fail(e, "no_game_folder", "no SSOWINCD.EXE in the root or a first-level folder");
        if (!rc) {
            s->folders_found = found;
            snprintf(s->folder, sizeof s->folder, "%s", top[first].name);
            upcase(s->folder);
            if (found > 1) {
                char list[300] = "";
                for (int k = 0; k < found && k < 4; k++) { strncat(list, names[k], sizeof list - strlen(list) - 1); if (k + 1 < found) strncat(list, ", ", sizeof list - strlen(list) - 1); }
                snprintf(s->warning, sizeof s->warning, "several OutNumbered! folders in '%%s' (%s); using %s.", list, names[0]);
            }
            rc = walk(s, &top[first], "", 0, e);
        }
    }
    free_ents(top, nt);
    return rc;
}

static VSource *new_source(int kind, const char *path) {
    VSource *s = xcalloc(1, sizeof *s);
    s->kind = kind;
    s->path = xstrdup(path);
    return s;
}

void vs_close(VSource *s) {
    if (!s) return;
    for (int i = 0; i < s->nfiles; i++) {
        VFile *f = &s->files[i];
        if (f->owned) free((void *)f->data);
        free(f->name); free(f->ent.ext); free(f->ent.path);
    }
    free(s->files);
    for (int i = 0; i < s->nz; i++) free(s->z[i].name);
    free(s->z);
    free(s->path);
    free(s);
}

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int ends_ci(const char *s, const char *ext) { size_t a = strlen(s), b = strlen(ext); return a >= b && !strcasecmp(s + a - b, ext); }

/* open an image file by content (13 section 1.1 step 4) */
static int open_image(const char *path, int via_cue, VSource **out, VErr *e) {
    size_t size;
    uint8_t *m = map_file(path, &size);
    if (!m) return fail(e, "not_found", "cannot read %s", path);
    static const uint8_t sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
    int raw = -1;
    if (size >= 17u * 2048 && !memcmp(m + 32769, "CD001", 5)) raw = 0;
    else if (size >= 17u * 2352 && !memcmp(m, sync, 12) && !memcmp(m + 37632, sync, 12)) {
        int mode = m[37632 + 15];
        size_t off = mode == 1 ? 16 : mode == 2 ? 24 : 0;
        if (off && !memcmp(m + 37632 + off + 1, "CD001", 5)) raw = 1;
    }
    if (raw < 0) { munmap(m, size); return fail(e, "unrecognised", "not an image"); }
    VSource *s = new_source(raw ? (via_cue ? K_CUE_RAW : K_RAW) : (via_cue ? K_CUE_ISO : K_ISO), path);
    s->map = m;
    s->msize = size;
    s->raw = raw;
    if (iso_open_volume(s, e) != 0) { vs_close(s); return -1; }
    Ent root;
    memset(&root, 0, sizeof root);
    root.isdir = 1;
    root.ext = xmalloc(sizeof(Ext));
    root.ext[0] = s->root;
    root.next = 1;
    int rc = find_game(s, &root, e);
    free(root.ext);
    if (rc) { vs_close(s); return -1; }
    *out = s;
    return 0;
}

static int open_cue(const char *path, VSource **out, VErr *e) {
    size_t n;
    FILE *f = fopen(path, "rb");
    if (!f) return fail(e, "not_found", "cannot read %s", path);
    fseek(f, 0, SEEK_END);
    n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *txt = xmalloc(n + 1);
    n = fread(txt, 1, n, f);
    txt[n] = 0;
    fclose(f);
    char fname[512] = "", mode[64] = "";
    int have_file = 0, have_track = 0;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        while (*line == ' ' || *line == '\t') line++;
        if (!have_file && !strncasecmp(line, "FILE", 4) && isspace((unsigned char)line[4])) {
            char *p = line + 4;
            while (isspace((unsigned char)*p)) p++;
            if (*p == '"') { p++; char *q = strchr(p, '"'); if (!q) continue; *q = 0; snprintf(fname, sizeof fname, "%s", p); }
            else { char *q = p; while (*q && !isspace((unsigned char)*q)) q++; *q = 0; snprintf(fname, sizeof fname, "%s", p); }
            have_file = 1;
        } else if (have_file && !have_track && !strncasecmp(line, "TRACK", 5) && isspace((unsigned char)line[5])) {
            char nb[16];
            if (sscanf(line + 5, "%15s %63s", nb, mode) == 2) have_track = 1;
        }
    }
    free(txt);
    if (!have_file || !have_track) return fail(e, "bad_cue", "no FILE or TRACK line");
    if (strcasecmp(mode, "MODE1/2048") && strcasecmp(mode, "MODE1/2352") && strcasecmp(mode, "MODE2/2352"))
        return fail(e, "bad_cue", "first track is %s, not a data track", mode);
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (sl) *sl = 0; else snprintf(dir, sizeof dir, ".");
    char full[2048];
    snprintf(full, sizeof full, "%s/%s", dir, fname);
    if (!exists(full)) {
        DIR *d = opendir(dir);
        int found = 0;
        if (d) {
            struct dirent *de;
            while ((de = readdir(d))) if (!strcasecmp(de->d_name, fname)) { snprintf(full, sizeof full, "%s/%s", dir, de->d_name); found = 1; break; }
            closedir(d);
        }
        if (!found) return fail(e, "not_found", "%s named by the cue sheet", fname);
    }
    return open_image(full, 1, out, e);
}

int vs_open_data(const char *path, VSource **out, VErr *e) {
    *out = NULL;
    if (!exists(path)) return fail(e, "not_found", "%s", path);
    if (is_dir(path)) {
        VSource *s = new_source(K_FOLDER, path);
        Ent root;
        memset(&root, 0, sizeof root);
        root.isdir = 1;
        root.path = xstrdup(path);
        int rc = find_game(s, &root, e);
        free(root.path);
        if (rc) { vs_close(s); return -1; }
        *out = s;
        return 0;
    }
    if (ends_ci(path, ".cue")) return open_cue(path, out, e);
    return open_image(path, 0, out, e);
}

/* Legacy: a plain folder with no SSOWINCD.EXE anywhere is used as the game folder itself (old flat test folders). */
int vs_open_folder_as_game(const char *path, VSource **out, VErr *e) {
    *out = NULL;
    if (!is_dir(path)) return fail(e, "not_found", "%s", path);
    VSource *s = new_source(K_FOLDER, path);
    Ent root;
    memset(&root, 0, sizeof root);
    root.isdir = 1;
    root.path = xstrdup(path);
    s->folders_found = 1;
    int rc = walk(s, &root, "", 0, e);
    free(root.path);
    if (rc) { vs_close(s); return -1; }
    *out = s;
    return 0;
}

/* ------------------------------------------------------------------ DOS role */
static int dos_zip(const char *path, VSource **out, VErr *e) {
    size_t size;
    uint8_t *m = map_file(path, &size);
    if (!m) return fail(e, "not_found", "cannot read %s", path);
    VSource *s = new_source(K_ZIP, path);
    s->map = m;
    s->msize = size;
    if (zip_parse(s, e) != 0) { vs_close(s); return -1; }
    int best = -1, best_depth = 0;
    char best_dir[1024] = "";
    int count_at_best = 0;
    for (int i = 0; i < s->nz; i++) {
        const char *nm = s->z[i].name;
        const char *sl = strrchr(nm, '/');
        const char *base = sl ? sl + 1 : nm;
        if (strcasecmp(base, "SSO.EXE")) continue;
        char dir[1024];
        size_t dl = sl ? (size_t)(sl - nm) : 0;
        memcpy(dir, nm, dl);
        dir[dl] = 0;
        char want[1100];
        snprintf(want, sizeof want, "%s%sMOCKET.DAT", dir, dl ? "/" : "");
        int mi = -1;
        for (int k = 0; k < s->nz; k++) if (!strcasecmp(s->z[k].name, want)) { mi = k; break; }
        if (mi < 0) continue;
        int depth = 0;
        for (const char *c = dir; *c; c++) if (*c == '/') depth++;
        if (dl) depth++;
        char ud[1024], bd[1024];
        snprintf(ud, sizeof ud, "%s", dir); upcase(ud);
        snprintf(bd, sizeof bd, "%s", best_dir); upcase(bd);
        if (best < 0 || depth < best_depth || (depth == best_depth && strcmp(ud, bd) < 0)) {
            best = i; best_depth = depth; snprintf(best_dir, sizeof best_dir, "%s", dir); count_at_best = 0;
        }
        (void)count_at_best;
    }
    if (best < 0) { vs_close(s); return fail(e, "dos_files_not_found", "no folder with SSO.EXE and MOCKET.DAT in the archive"); }
    char ub[1024];
    snprintf(ub, sizeof ub, "%s", best_dir); upcase(ub);
    snprintf(s->folder, sizeof s->folder, "%s", ub);
    int qual = 0;
    for (int i = 0; i < s->nz; i++) {
        const char *sl = strrchr(s->z[i].name, '/');
        if (strcasecmp(sl ? sl + 1 : s->z[i].name, "SSO.EXE")) continue;
        char dir[1024];
        size_t dl = sl ? (size_t)(sl - s->z[i].name) : 0;
        memcpy(dir, s->z[i].name, dl); dir[dl] = 0;
        int d2 = 0;
        for (const char *c = dir; *c; c++) if (*c == '/') d2++;
        if (dl) d2++;
        if (d2 != best_depth) continue;
        char want[1100];
        snprintf(want, sizeof want, "%s%sMOCKET.DAT", dir, dl ? "/" : "");
        for (int k = 0; k < s->nz; k++) if (!strcasecmp(s->z[k].name, want)) { qual++; break; }
    }
    s->folders_found = qual;
    /* files: MOCKET.DAT and SSO.EXE of the chosen folder, names as in the DOS folder */
    const char *names[2] = {"MOCKET.DAT", "SSO.EXE"};
    for (int f = 0; f < 2; f++) {
        char want[1100];
        snprintf(want, sizeof want, "%s%s%s", best_dir, best_dir[0] ? "/" : "", names[f]);
        for (int k = 0; k < s->nz; k++)
            if (!strcasecmp(s->z[k].name, want)) {
                Ent en;
                memset(&en, 0, sizeof en);
                en.size = s->z[k].usize;
                en.zip = k;
                add_file(s, names[f], &en);
                break;
            }
    }
    *out = s;
    return 0;
}

static int dos_folder(const char *path, VSource **out, VErr *e) {
    typedef struct DirEnt { char rel[1024]; char full[2048]; } DirEnt;
    DirEnt *cur = xcalloc(1, sizeof(DirEnt));
    int ncur = 1;
    snprintf(cur[0].full, sizeof cur[0].full, "%s", path);
    for (int depth = 0; depth <= 2; depth++) {
        /* sort this level by upper-cased relative path */
        for (int i = 0; i < ncur; i++)
            for (int j = i + 1; j < ncur; j++) {
                char a[1024], b[1024];
                snprintf(a, sizeof a, "%s", cur[i].rel); upcase(a);
                snprintf(b, sizeof b, "%s", cur[j].rel); upcase(b);
                if (strcmp(b, a) < 0) { DirEnt t = cur[i]; cur[i] = cur[j]; cur[j] = t; }
            }
        int qual = 0, first = -1;
        for (int i = 0; i < ncur; i++) {
            Ent *a;
            int n;
            if (fs_children(cur[i].full, &a, &n) != 0) continue;
            int ok = has_file(a, n, "SSO.EXE") && has_file(a, n, "MOCKET.DAT");
            free_ents(a, n);
            if (ok) { qual++; if (first < 0) first = i; }
        }
        if (first >= 0) {
            VSource *s = new_source(K_FOLDER, path);
            s->folders_found = qual;
            snprintf(s->folder, sizeof s->folder, "%s", cur[first].rel);
            upcase(s->folder);
            Ent *a;
            int n;
            fs_children(cur[first].full, &a, &n);
            static const char *names[2] = {"MOCKET.DAT", "SSO.EXE"};
            for (int f = 0; f < 2; f++)
                for (int i = 0; i < n; i++) if (!a[i].isdir && !strcasecmp(a[i].name, names[f])) { add_file(s, names[f], &a[i]); break; }
            free_ents(a, n);
            free(cur);
            *out = s;
            return 0;
        }
        if (depth == 2) break;
        DirEnt *next = NULL;
        int nn = 0;
        for (int i = 0; i < ncur; i++) {
            Ent *a;
            int n;
            if (fs_children(cur[i].full, &a, &n) != 0) continue;
            for (int k = 0; k < n; k++) {
                if (!a[k].isdir) continue;
                next = xrealloc(next, sizeof(DirEnt) * (size_t)(nn + 1));
                snprintf(next[nn].rel, sizeof next[nn].rel, "%s%s%s", cur[i].rel, cur[i].rel[0] ? "/" : "", a[k].name);
                snprintf(next[nn].full, sizeof next[nn].full, "%s", a[k].path);
                nn++;
            }
            free_ents(a, n);
        }
        free(cur);
        cur = next;
        ncur = nn;
        if (!ncur) break;
    }
    free(cur);
    return fail(e, "dos_files_not_found", "no folder with SSO.EXE and MOCKET.DAT in %s", path);
}

int vs_open_dos(const char *path, VSource **out, VErr *e) {
    *out = NULL;
    if (!exists(path)) return fail(e, "not_found", "%s", path);
    if (is_dir(path)) return dos_folder(path, out, e);
    return dos_zip(path, out, e);
}

/* ------------------------------------------------------------------ queries */
const char *vs_kind(const VSource *s) {
    switch (s->kind) {
    case K_FOLDER: return "folder"; case K_ISO: return "iso2048"; case K_RAW: return "raw2352";
    case K_CUE_ISO: return "cue+iso2048"; case K_CUE_RAW: return "cue+raw2352"; default: return "zip";
    }
}
const char *vs_folder(const VSource *s) { return s->folder; }
int vs_folders_found(const VSource *s) { return s->folders_found; }
const char *vs_warning(const VSource *s) { return s->warning; }
int vs_nfiles(const VSource *s) { return s->nfiles; }
const char *vs_file_name(const VSource *s, int i) { return s->files[i].name; }
uint64_t vs_file_size(const VSource *s, int i) { return s->files[i].size; }

int vs_find(const VSource *s, const char *name) {
    for (int i = 0; i < s->nfiles; i++) if (!strcasecmp(s->files[i].name, name)) return i;
    return -1;
}

int vs_file_load(VSource *s, int i, const uint8_t **data, size_t *len, VErr *e) {
    VFile *f = &s->files[i];
    if (!f->loaded) {
        if (s->kind == K_ZIP) {
            uint8_t *b;
            size_t n;
            if (zip_load(s, f->ent.zip, &b, &n, e) != 0) return -1;
            f->data = b; f->dlen = n; f->owned = 1;
        } else if (s->kind == K_FOLDER) {
            size_t n;
            uint8_t *m = map_file(f->ent.path, &n);
            if (!m) return fail(e, "not_found", "cannot read %s", f->ent.path);
            f->data = m; f->dlen = n;                                     /* mapped; kept for the life of the process */
        } else if (!s->raw && f->ent.next == 1) {
            uint64_t start = f->ent.ext[0].sector * 2048, n = f->ent.ext[0].len;
            if (start + n > s->msize) return fail(e, "truncated", "sector %llu is past the end of the image", (unsigned long long)(s->msize / 2048));
            f->data = s->map + start; f->dlen = (size_t)n;                /* zero copy */
        } else {
            uint64_t total = 0;
            for (int k = 0; k < f->ent.next; k++) total += f->ent.ext[k].len;
            uint8_t *b = xmalloc((size_t)total + 1), *w = b;
            for (int k = 0; k < f->ent.next; k++) {
                uint8_t *part;
                if (iso_read(s, f->ent.ext[k].sector, f->ent.ext[k].len, &part, e) != 0) { free(b); return -1; }
                memcpy(w, part, (size_t)f->ent.ext[k].len);
                w += f->ent.ext[k].len;
                free(part);
            }
            f->data = b; f->dlen = (size_t)total; f->owned = 1;
        }
        f->loaded = 1;
    }
    *data = f->data;
    *len = f->dlen;
    return 0;
}

const uint8_t *vs_read(VSource *s, const char *name, size_t *len) {
    int i = vs_find(s, name);
    if (i < 0) return NULL;
    const uint8_t *d;
    size_t n;
    VErr e;
    if (vs_file_load(s, i, &d, &n, &e) != 0) return NULL;
    if (len) *len = n;
    return d;
}

void vs_format_error(const char *role, const char *p, const VErr *e, char *out, size_t n) {
    const char *c = e->code, *d = e->detail;
    if (!strcmp(role, "dos")) {
        if (!strcmp(c, "not_found")) snprintf(out, n, "%s does not exist", p);
        else if (!strcmp(c, "unrecognised")) snprintf(out, n, "%s is not a folder or a .zip archive", p);
        else if (!strcmp(c, "dos_files_not_found")) snprintf(out, n, "SSO.EXE and MOCKET.DAT not found in %s", p);
        else if (!strcmp(c, "truncated")) snprintf(out, n, "%s: the zip archive is incomplete", p);
        else if (!strcmp(c, "bad_zip")) snprintf(out, n, "%s: damaged zip archive (%s)", p, d);
        else if (!strcmp(c, "zip64")) snprintf(out, n, "%s: ZIP64 archives are not supported", p);
        else if (!strcmp(c, "encrypted")) snprintf(out, n, "%s: %s is encrypted", p, d);
        else if (!strcmp(c, "unsupported_method")) {
            const char *nl = strchr(d, '\n');
            snprintf(out, n, "%s: %.*s uses an unsupported compression method (%s)", p, nl ? (int)(nl - d) : (int)strlen(d), d, nl ? nl + 1 : "?");
        }
        else if (!strcmp(c, "bad_crc")) snprintf(out, n, "%s: CRC error in %s", p, d);
        else snprintf(out, n, "%s: %s", p, d);
        return;
    }
    if (!strcmp(c, "not_found")) snprintf(out, n, "engine: --data '%s': no such file or folder.", p);
    else if (!strcmp(c, "unrecognised")) snprintf(out, n, "engine: '%s' is not a folder, an ISO 9660 CD image (.iso), a raw CD image (.bin/.img, 2352-byte sectors) or a .cue sheet.", p);
    else if (!strcmp(c, "bad_cue")) snprintf(out, n, "engine: cannot use the cue sheet '%s': %s.", p, d);
    else if (!strcmp(c, "truncated")) snprintf(out, n, "engine: the CD image '%s' is incomplete (%s). Copy the image again.", p, d);
    else if (!strcmp(c, "bad_image")) snprintf(out, n, "engine: the CD image '%s' is damaged or uses an unsupported ISO 9660 feature: %s.", p, d);
    else if (!strcmp(c, "no_game_folder")) snprintf(out, n, "engine: the OutNumbered! CD files were not found in '%s' (no SSOWINCD.EXE at its top or in a top-level folder).", p);
    else snprintf(out, n, "engine: '%s': %s", p, d);
}
