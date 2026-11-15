/* res.h - NE resource container reader (spec 01 section 2) */
#ifndef RES_H
#define RES_H
#include "util.h"
#include "vfs.h"

typedef struct Res {
    int type;           /* integer type id (bit 15 stripped), or -1 for a named type */
    char tname[24];     /* resolved type name, e.g. "ASEQ", "DIALOG", "NFNT" */
    int id;             /* integer id, or -1 if named */
    char name[24];      /* resource name when not integer */
    uint32_t off, len;  /* byte offset in file and (padded) length */
} Res;

typedef struct ResFile {
    char *path;
    char *base;          /* upper-case base name, e.g. SSO1.DAT */
    const uint8_t *data;
    size_t size;
    int shift;
    int nres;
    Res *res;
} ResFile;

/* Open every NE file in `dir` (case-insensitive: SSO1/2/3.DAT, SND.DAT, SPEECH.DAT,
 * SSOWINCD.EXE, any other *.DAT / *.EXE / *.FON that is a valid NE file). Returns number opened. */
int res_open_dir(const char *dir);
/* Open one NE file explicitly. Returns NULL on failure. */
ResFile *res_open_file(const char *path);
ResFile *res_open_mem(const char *name, const uint8_t *data, size_t size);
/* Open the NE files of a data source (vfs.h). Returns the number opened. */
int res_open_source(VSource *src);

void res_close_all(void);
int res_file_count(void);
ResFile *res_file(int i);

/* Look up (type name, integer id) across all open files. Returns pointer to the resource bytes
 * (padded length in *len, may be NULL) or NULL. */
const uint8_t *res_get(const char *type, int id, size_t *len);
const Res *res_find(const char *type, int id, const ResFile **file);

/* Case-insensitive path join/open helper: returns malloc'd path of file `name` inside `dir` or NULL. */
char *path_find_ci(const char *dir, const char *name);
/* Read a whole file. Returns malloc'd buffer or NULL. */
uint8_t *file_read_all(const char *path, size_t *len);

#endif
