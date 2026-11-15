/* compat.h - Windows (MSYS2 UCRT64) only: force-included by the Makefile before every source file.
 * Every file operation on a UTF-8 path goes through the wide API (spec 14 section 1.3): plain fopen with a UTF-8 name
 * is not acceptable on Windows. The POSIX calls the engine uses are redirected to the wrappers in compat_win.c.
 * Missing POSIX headers (dirent.h, sys/mman.h, sys/file.h, sys/statvfs.h) are in src/compat/. */
#ifndef COMPAT_H
#define COMPAT_H
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

FILE *w_fopen(const char *path, const char *mode);
int w_stat(const char *path, struct stat *st);
int w_unlink(const char *path);
int w_rename(const char *from, const char *to);
int w_mkdir(const char *path);
int w_rmdir(const char *path);
int w_access(const char *path, int mode);
int w_chmod(const char *path, int mode);
int w_open(const char *path, int flags, ...);
char *w_getenv(const char *name);
char *w_getcwd(char *buf, size_t n);

#define fopen(p, m) w_fopen(p, m)
#define stat(p, s) w_stat(p, s)
#define lstat(p, s) w_stat(p, s)
#define unlink(p) w_unlink(p)
#define rename(a, b) w_rename(a, b)
#define mkdir(p, m) w_mkdir(p)
#define rmdir(p) w_rmdir(p)
#define access(p, m) w_access(p, m)
#define chmod(p, m) w_chmod(p, m)
#define open(...) w_open(__VA_ARGS__)
#define getenv(n) w_getenv(n)
#define getcwd(b, n) w_getcwd(b, n)
#define fsync(fd) _commit(fd)
#define gmtime_r(t, r) (gmtime_s(r, t) ? NULL : (r))

#endif
#endif
