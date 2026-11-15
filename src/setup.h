/* setup.h - per-user installation: manifest, resolution, classification, installer (spec chapter 14) */
#ifndef SETUP_H
#define SETUP_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "vfs.h"

#define ENGINE_VERSION "1.15.0"

typedef struct SFile { char path[512]; uint64_t size; uint32_t crc; } SFile;   /* path relative to the base: game/MIDI/B00.MID */
typedef struct SPart {
    int present;
    char source[1024], kind[32], folder[512];
    SFile *files;
    int nfiles;
} SPart;
typedef struct Manifest {
    int valid;
    char err[32];                      /* no_manifest | bad_manifest when !valid */
    char engine[96], utc[40];
    SPart part[2];                     /* 0 = game, 1 = dos */
} Manifest;

void manifest_free(Manifest *m);
void manifest_load(const char *home, Manifest *m);
/* check one installed part: 0 = ok; else code (missing_file, size_mismatch, crc_mismatch) and detail (manifest path) */
int part_check(const char *home, const SPart *p, int full, char *code, size_t cn, char *detail, size_t dn);
uint64_t part_bytes(const SPart *p);

/* ---- start-up resolution (14 section 2) */
typedef struct SetupOpts {
    const char *home;                  /* --home (NULL = none) */
    const char *data, *dos_game, *save_dir;
    int setup, verify;
} SetupOpts;
typedef struct Resolution {
    char home[1024];
    const char *home_from;             /* option | env | default */
    int setup;                         /* action: 1 = setup screen, 0 = run */
    char reason[32], detail[512];
    char data[1024];                   /* "" = none */
    const char *data_from;             /* installed | option | NULL */
    char dos[1024];
    const char *dos_from;              /* option | ini | installed | NULL */
    char dos_installed[32];            /* none | ok | missing_file | size_mismatch | crc_mismatch */
    char save_dir[1024];
    const char *save_from;             /* option | default */
    int home_exists;
} Resolution;
/* 0 = ok, -1 = no base directory (message in err) */
int setup_resolve(const SetupOpts *o, Resolution *r, char *err, size_t en);
extern Resolution g_run;               /* the resolution this run started with (set by the start-up code) */
extern SetupOpts g_opts;
extern int g_run_valid;

/* ---- rows and classification (14 section 4) */
enum { ROW_NONE, ROW_CHECKING, ROW_OK, ROW_INSTALLED, ROW_ERROR };
typedef struct Row {
    int state;
    char error[32], detail[512];
    char name[256], source[1024], kind[32], folder[512];
    int nfiles;
    uint64_t bytes;
    SFile *files;                      /* ok rows: logical names (no game/ or dos/ prefix) with size and crc */
} Row;
void row_clear(Row *r);
void row_free(Row *r);
void row_copy(Row *dst, const Row *src);
void row_from_part(Row *r, const SPart *p);        /* state installed */

typedef struct ClassResult {
    int win_set, dos_set;              /* the role gave a result (ok or error) */
    Row win, dos;
    char msg[32], msg_detail[512];     /* message code ("" = none) */
} ClassResult;
void classify_path(const char *path, ClassResult *out);   /* stage 1 + 2, synchronous */
/* the two stages apart (the setup screen runs stage 2 on a worker thread) */
typedef struct ClassJob {
    char path[4400];
    VSource *ws, *ds;
    char wc[32], wd[512], dc[32], dd[512];
    int w1, d1;                        /* stage 1 found the Windows / DOS row */
    ClassResult res;
} ClassJob;
void classjob_stage1(ClassJob *j, const char *path);
void classjob_stage2(ClassJob *j);
void classjob_free(ClassJob *j);
void classresult_free(ClassResult *c);

/* ---- installer (14 section 6): step-wise so that the setup screen and --conform can drive it */
typedef struct Installer Installer;
Installer *inst_new(const char *home, const Row *win_ok, const Row *dos_ok, const Manifest *old, int win_installed_row, int dos_installed_row);
int inst_begin(Installer *in, char *code, size_t cn);      /* steps 1-3; 0 ok else error code */
/* do up to max_units of work; returns 1 = more to do, 0 = finished (committed), -1 = failed (code), -2 = cancelled */
int inst_tick(Installer *in, uint64_t max_units, char *code, size_t cn);
void inst_cancel(Installer *in);
void inst_free(Installer *in);
typedef struct InstProgress { uint64_t done, total; int index, count; char file[512]; const char *step; } InstProgress;
void inst_progress(const Installer *in, InstProgress *p);
int inst_parts(const Installer *in, int part);             /* 1 if the part (0 game, 1 dos) is being installed */
const char *inst_detail(const Installer *in);              /* detail text of the last error (file name ...) */
uint64_t inst_total_bytes(const Installer *in);

/* per-part helpers used by Audio Options: install only the DOS files from a folder/zip path. 0 = ok else code+message */
int install_dos_only(const char *home, const char *path, char *code, size_t cn, char *msg, size_t mn);

/* text helpers */
const char *path_last_component(const char *path, char *buf, size_t n);
#endif
