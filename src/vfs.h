/* vfs.h - input containers (spec chapter 13): folder / ISO 9660 / raw CD image / cue for --data,
 * folder / zip for --dos-game. Read-only. */
#ifndef VFS_H
#define VFS_H
#include <stddef.h>
#include <stdint.h>

typedef struct VErr { char code[24]; char detail[400]; } VErr;   /* codes of spec 13 section 7 */

typedef struct VSource VSource;

/* Open a --data source and find its game folder (13 section 3). 0 = ok; on error e is filled. */
int vs_open_data(const char *path, VSource **out, VErr *e);
/* Open a --dos-game source and find the DOS folder (13 section 5). */
int vs_open_folder_as_game(const char *path, VSource **out, VErr *e);   /* legacy: the folder itself is the game folder */
int vs_open_dos(const char *path, VSource **out, VErr *e);
void vs_close(VSource *s);

const char *vs_kind(const VSource *s);        /* folder, iso2048, raw2352, cue+iso2048, cue+raw2352, zip */
const char *vs_folder(const VSource *s);      /* chosen folder inside the source ("" = top), "/"-separated, original case */
int vs_folders_found(const VSource *s);
const char *vs_warning(const VSource *s);     /* several game folders: stderr text, or "" */
int vs_nfiles(const VSource *s);
const char *vs_file_name(const VSource *s, int i);   /* logical name */
uint64_t vs_file_size(const VSource *s, int i);
/* Load a file completely (cached). 0 = ok; else e is filled (truncated, bad_crc, ...). */
int vs_file_load(VSource *s, int i, const uint8_t **data, size_t *len, VErr *e);
int vs_find(const VSource *s, const char *logical_name);     /* case-insensitive; -1 = none */
/* convenience: bytes of a logical file or NULL (errors ignored) */
const uint8_t *vs_read(VSource *s, const char *logical_name, size_t *len);

/* The message for an error as printed by the engine. role is "data" or "dos". */
void vs_format_error(const char *role, const char *path, const VErr *e, char *out, size_t n);
uint32_t vs_crc32(const uint8_t *d, size_t n);

int data_source_init(const char *path, int legacy_folder_ok);   /* opens --data, sets g_data_src, opens the NE files; -1 = error (message printed), else the number of NE files */

void data_source_reset(void);

/* the data source in use (set by the start-up code; NULL when --data is a plain legacy directory is not used any more) */
extern VSource *g_data_src;
#endif
