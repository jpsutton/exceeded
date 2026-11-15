#include "res.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAXFILES 32
static ResFile *files[MAXFILES];
static int nfiles;

static const char *std_type_name(int t) {
    switch (t) {
    case 1: return "CURSOR";
    case 2: return "BITMAP";
    case 3: return "ICON";
    case 4: return "MENU";
    case 5: return "DIALOG";
    case 6: return "STRING";
    case 7: return "FONTDIR";
    case 8: return "FONT";
    case 9: return "ACCELERATOR";
    case 10: return "RCDATA";
    case 12: return "GROUP_CURSOR";
    case 14: return "GROUP_ICON";
    case 15: return "NAMETABLE";
    case 16: return "VERSION";
    }
    return NULL;
}

uint8_t *file_read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = xmalloc((size_t)n + 1);
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    if (len) *len = got;
    return b;
}

char *path_find_ci(const char *dir, const char *name) {
    DIR *d = opendir(dir);
    if (!d) return NULL;
    struct dirent *e;
    char *res = NULL;
    while ((e = readdir(d))) {
        if (strcasecmp(e->d_name, name) == 0) {
            size_t n = strlen(dir) + strlen(e->d_name) + 2;
            res = xmalloc(n);
            snprintf(res, n, "%s/%s", dir, e->d_name);
            break;
        }
    }
    closedir(d);
    return res;
}

ResFile *res_open_mem(const char *name, const uint8_t *d, size_t size);

ResFile *res_open_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0x40 || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
    size_t size = (size_t)st.st_size;
    const uint8_t *d = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (d == MAP_FAILED) return NULL;
    ResFile *rf = res_open_mem(path, d, size);
    if (!rf) munmap((void *)d, size);
    return rf;
}

/* NE file already in memory (not owned by the reader); `name` may be a path or a bare file name */
ResFile *res_open_mem(const char *path, const uint8_t *d, size_t size) {
    if (size < 0x40) return NULL;
    uint32_t ne = rd32(d + 0x3C);
    if (d[0] != 'M' || d[1] != 'Z' || (size_t)ne + 0x40 > size || d[ne] != 'N' || d[ne + 1] != 'E') return NULL;
    uint32_t rt = ne + rd16(d + ne + 0x24);
    if ((size_t)rt + 2 > size) return NULL;
    ResFile *rf = xcalloc(1, sizeof *rf);
    rf->path = xstrdup(path);
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    rf->base = xstrdup(b);
    for (char *c = rf->base; *c; c++) *c = (char)toupper((unsigned char)*c);
    rf->data = d;
    rf->size = size;
    rf->shift = rd16(d + rt);
    /* first pass: count */
    int cap = 0;
    size_t p = rt + 2;
    while (p + 2 <= size && rd16(d + p) != 0) {
        int cnt = rd16(d + p + 2);
        cap += cnt;
        p += 8 + (size_t)cnt * 12;
    }
    rf->res = xcalloc((size_t)cap + 1, sizeof(Res));
    p = rt + 2;
    int n = 0;
    while (p + 2 <= size && rd16(d + p) != 0) {
        uint16_t tid = rd16(d + p);
        int cnt = rd16(d + p + 2);
        p += 8;
        for (int i = 0; i < cnt && p + 12 <= size; i++, p += 12) {
            Res *r = &rf->res[n++];
            if (tid & 0x8000) { r->type = tid & 0x7FFF; }
            else {
                r->type = -1;
                size_t so = rt + tid;
                if (so < size) {
                    int l = d[so];
                    if (l > 23) l = 23;
                    memcpy(r->tname, d + so + 1, (size_t)l);
                    r->tname[l] = 0;
                }
            }
            uint16_t rid = rd16(d + p + 6);
            if (rid & 0x8000) r->id = rid & 0x7FFF;
            else {
                r->id = -1;
                size_t so = rt + rid;
                if (so < size) {
                    int l = d[so];
                    if (l > 23) l = 23;
                    memcpy(r->name, d + so + 1, (size_t)l);
                    r->name[l] = 0;
                }
            }
            r->off = (uint32_t)rd16(d + p) << rf->shift;
            r->len = (uint32_t)rd16(d + p + 2) << rf->shift;
            if ((size_t)r->off > size) r->off = (uint32_t)size;
            if ((size_t)r->off + r->len > size) r->len = (uint32_t)(size - r->off);
        }
    }
    rf->nres = n;
    /* resolve custom type names from RT_NAMETABLE (type 15) */
    char *names[0x80] = {0};
    for (int i = 0; i < rf->nres; i++) {
        Res *r = &rf->res[i];
        if (r->type != 15) continue;
        const uint8_t *t = d + r->off;
        uint32_t len = r->len, q = 0;
        while (q + 6 <= len) {
            uint16_t el = rd16(t + q);
            if (el == 0 || q + el > len) break;
            uint16_t to = rd16(t + q + 2);
            if ((to & 0xFF00) == 0xFF00 || (to & 0x8000)) {
                int ty = to & 0x7FFF;
                if (ty >= 0x7F00 && ty < 0x7F80 && !names[ty - 0x7F00]) {
                    const char *nm = (const char *)(t + q + 6);
                    size_t nl = strnlen(nm, el - 6);
                    names[ty - 0x7F00] = xmalloc(nl + 1);
                    memcpy(names[ty - 0x7F00], nm, nl);
                    names[ty - 0x7F00][nl] = 0;
                }
            }
            q += el;
        }
    }
    for (int i = 0; i < rf->nres; i++) {
        Res *r = &rf->res[i];
        if (r->type < 0) continue;
        const char *nm = std_type_name(r->type);
        if (nm) snprintf(r->tname, sizeof r->tname, "%s", nm);
        else if (r->type >= 0x7F00 && r->type < 0x7F80 && names[r->type - 0x7F00])
            snprintf(r->tname, sizeof r->tname, "%s", names[r->type - 0x7F00]);
        else snprintf(r->tname, sizeof r->tname, "TYPE_%X", r->type);
    }
    for (int i = 0; i < 0x80; i++) free(names[i]);
    if (nfiles < MAXFILES) files[nfiles++] = rf;
    return rf;
}

static int has_ext(const char *n, const char *ext) {
    size_t a = strlen(n), b = strlen(ext);
    return a > b && strcasecmp(n + a - b, ext) == 0;
}

static int cmp_names(const void *a, const void *b) {
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

int res_open_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    char *names[256];
    int nn = 0;
    struct dirent *e;
    while ((e = readdir(d)) && nn < 256) {
        if (has_ext(e->d_name, ".DAT") || has_ext(e->d_name, ".EXE") || has_ext(e->d_name, ".FON"))
            names[nn++] = xstrdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)nn, sizeof(char *), cmp_names);
    int opened = 0;
    /* open in a stable order: SSO1, SSO2, SSO3, SND, SPEECH, EXE, then others */
    static const char *pref[] = {"SSO1.DAT", "SSO2.DAT", "SSO3.DAT", "SND.DAT", "SPEECH.DAT", "SSOWINCD.EXE", "SSO.FON"};
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; pass == 0 ? k < ARRAY_LEN(pref) : k < nn; k++) {
            for (int i = 0; i < nn; i++) {
                if (!names[i]) continue;
                if (pass == 0 && strcasecmp(names[i], pref[k]) != 0) continue;
                if (pass == 1 && i != k) continue;
                char *path = path_find_ci(dir, names[i]);
                if (path) {
                    if (res_open_file(path)) opened++;
                    free(path);
                }
                free(names[i]);
                names[i] = NULL;
                if (pass == 0) break;
            }
        }
    }
    for (int i = 0; i < nn; i++) free(names[i]);
    return opened;
}

/* Open the NE files of a data source (13 section 3.2): the files at the top of the game folder. */
int res_open_source(VSource *src) {
    static const char *pref[] = {"SSO1.DAT", "SSO2.DAT", "SSO3.DAT", "SND.DAT", "SPEECH.DAT", "SSOWINCD.EXE", "SSO.FON"};
    int opened = 0, n = vs_nfiles(src);
    char *done = xcalloc((size_t)n + 1, 1);
    for (int pass = 0; pass < 2; pass++) {
        int lim = pass == 0 ? ARRAY_LEN(pref) : n;
        for (int k = 0; k < lim; k++) {
            int i = pass == 0 ? vs_find(src, pref[k]) : k;
            if (i < 0 || done[i]) continue;
            const char *nm = vs_file_name(src, i);
            if (strchr(nm, '/')) continue;
            if (!has_ext(nm, ".DAT") && !has_ext(nm, ".EXE") && !has_ext(nm, ".FON")) continue;
            done[i] = 1;
            const uint8_t *d;
            size_t len;
            VErr e;
            if (vs_file_load(src, i, &d, &len, &e) != 0) continue;
            if (res_open_mem(nm, d, len)) opened++;
        }
    }
    free(done);
    return opened;
}

/* drop every opened file (the mappings stay: data pointers may still be referenced) */
void res_close_all(void) { nfiles = 0; }

int res_file_count(void) { return nfiles; }
ResFile *res_file(int i) { return files[i]; }

const Res *res_find(const char *type, int id, const ResFile **file) {
    for (int f = 0; f < nfiles; f++) {
        ResFile *rf = files[f];
        for (int i = 0; i < rf->nres; i++) {
            const Res *r = &rf->res[i];
            if (r->id == id && strcmp(r->tname, type) == 0) {
                if (file) *file = rf;
                return r;
            }
        }
    }
    return NULL;
}

const uint8_t *res_get(const char *type, int id, size_t *len) {
    const ResFile *rf;
    const Res *r = res_find(type, id, &rf);
    if (!r) return NULL;
    if (len) *len = r->len;
    return rf->data + r->off;
}
