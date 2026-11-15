/* sys/statvfs.h for Windows: free space of a volume (compat_win.c). f_frsize is 1, f_bavail the free bytes. */
#ifndef COMPAT_STATVFS_H
#define COMPAT_STATVFS_H
#include <stdint.h>
struct statvfs { uint64_t f_bavail; uint64_t f_frsize; };
int statvfs(const char *path, struct statvfs *sv);
#endif
