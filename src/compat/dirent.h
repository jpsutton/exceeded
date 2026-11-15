/* dirent.h for Windows: opendir / readdir / closedir over FindFirstFileW (compat_win.c). Names are UTF-8. */
#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H
struct dirent { char d_name[1024]; };
typedef struct W_DIR DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
#endif
