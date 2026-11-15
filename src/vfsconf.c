/* vfsconf.c - data source start-up and the list-files conformance command (spec 13 sections 3, 7, 8) */
#include "vfs.h"
#include "res.h"
#include "util.h"
#include <strings.h>
#include <ctype.h>

/* Open --data (folder, ISO, raw image, cue), make the NE files available to the resource reader.
 * Prints the message of 13 section 7.1 and returns -1 on failure; returns the number of NE files opened otherwise. */
int data_source_init(const char *path, int legacy_folder_ok) {
    VSource *s;
    VErr e;
    int rc = vs_open_data(path, &s, &e);
    /* conformance commands also accept an old flat folder (no SSOWINCD.EXE): the folder itself is then the game folder */
    if (rc != 0 && legacy_folder_ok && !strcmp(e.code, "no_game_folder")) {
        VErr e2;
        if (vs_open_folder_as_game(path, &s, &e2) == 0) rc = 0;     /* only for plain folders; an image keeps its error */
    }
    if (rc != 0) {
        char msg[1200];
        vs_format_error("data", path, &e, msg, sizeof msg);
        fprintf(stderr, "%s\n", msg);
        return -1;
    }
    if (vs_warning(s)[0]) {
        char w[1400];
        snprintf(w, sizeof w, vs_warning(s), path);
        fprintf(stderr, "engine: %s\n", w);
    }
    g_data_src = s;
    return res_open_source(s);
}

static void jstr(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20 || c >= 0x7F) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

typedef struct LF { char name[1024]; uint64_t size; uint32_t crc; } LF;
static int cmp_lf(const void *a, const void *b) { return strcmp(((const LF *)a)->name, ((const LF *)b)->name); }

/* engine --conform list-files PATH [--role data|dos] --out files.json */
int cmd_list_files(int argc, char **argv, const char *out) {
    const char *path = argc >= 2 ? argv[1] : NULL, *role = "data";
    for (int i = 2; i + 1 < argc; i++) if (!strcmp(argv[i], "--role")) role = argv[i + 1];
    if (!path || (strcmp(role, "data") && strcmp(role, "dos"))) { fprintf(stderr, "list-files PATH [--role data|dos]\n"); return 2; }
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    VSource *s = NULL;
    VErr e;
    LF *lf = NULL;
    int n = 0, rc = !strcmp(role, "dos") ? vs_open_dos(path, &s, &e) : vs_open_data(path, &s, &e);
    if (rc == 0) {
        n = vs_nfiles(s);
        lf = xcalloc((size_t)n + 1, sizeof(LF));
        for (int i = 0; i < n && rc == 0; i++) {
            const uint8_t *d;
            size_t len;
            if (vs_file_load(s, i, &d, &len, &e) != 0) { rc = -1; break; }
            snprintf(lf[i].name, sizeof lf[i].name, "%s", vs_file_name(s, i));
            for (char *c = lf[i].name; *c; c++) *c = (char)toupper((unsigned char)*c);
            lf[i].size = len;
            lf[i].crc = vs_crc32(d, len);
        }
    }
    if (rc != 0) {
        char msg[1200];
        vs_format_error(role, path, &e, msg, sizeof msg);
        fprintf(f, "{\"ok\": false, \"role\": \"%s\", \"error\": \"%s\", \"message\": ", role, e.code);
        jstr(f, msg);
        fprintf(f, "}\n");
        fclose(f);
        fprintf(stderr, "%s\n", msg);
        if (s) vs_close(s);
        free(lf);
        return 2;
    }
    qsort(lf, (size_t)n, sizeof(LF), cmp_lf);
    char fold[1100];
    snprintf(fold, sizeof fold, "%s", vs_folder(s));
    for (char *c = fold; *c; c++) *c = (char)toupper((unsigned char)*c);
    fprintf(f, "{\"ok\": true, \"role\": \"%s\", \"source\": \"%s\", \"folder\": ", role, vs_kind(s));
    jstr(f, fold);
    fprintf(f, ", \"folders_found\": %d,\n \"files\": [\n", vs_folders_found(s));
    for (int i = 0; i < n; i++) {
        fprintf(f, "  {\"name\": ");
        jstr(f, lf[i].name);
        fprintf(f, ", \"size\": %llu, \"crc32\": \"%08x\"}%s\n", (unsigned long long)lf[i].size, lf[i].crc, i + 1 < n ? "," : "");
    }
    fprintf(f, " ]}\n");
    fclose(f);
    if (vs_warning(s)[0]) { char w[1400]; snprintf(w, sizeof w, vs_warning(s), path); fprintf(stderr, "engine: %s\n", w); }
    vs_close(s);
    free(lf);
    return 0;
}

/* forget the data source and the NE files opened from it (a failed load from the installation) */
void data_source_reset(void) {
    if (g_data_src) { vs_close(g_data_src); g_data_src = NULL; }
    res_close_all();
}
