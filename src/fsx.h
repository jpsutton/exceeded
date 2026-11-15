/* fsx.h - file system helpers for the per-user installation (spec chapter 14); POSIX */
#ifndef FSX_H
#define FSX_H
#include <stddef.h>
#include <stdint.h>

char *fsx_abspath(const char *p);                  /* malloc'd absolute path (relative to the current directory); no normalisation */
int fsx_exists(const char *p);
int fsx_isdir(const char *p);
int fsx_isfile(const char *p);                     /* regular file */
int fsx_mkdirs(const char *path, unsigned mode);   /* mkdir -p; 0 = ok, else errno */
int fsx_rmtree(const char *path);                  /* recursive delete (no symlink following); 0 = ok */
uint64_t fsx_free_space(const char *path);         /* free bytes for an unprivileged user, UINT64_MAX when unknown */
/* base directory (14 section 1.1). from = "option", "env" or "default". Returns 0, or -1 when none can be determined. */
int fsx_base_dir(const char *opt, char *out, size_t n, const char **from);
const char *fsx_error_code(int err);               /* disk_full, permission, path_too_long or io_error (14 section 6) */
/* list all regular files below dir, relative paths with '/', sorted by byte order; returns count, *out malloc'd array of malloc'd strings */
int fsx_list_files(const char *dir, char ***out);
#endif
