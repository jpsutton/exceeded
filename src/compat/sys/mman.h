/* sys/mman.h for Windows: read-only private file mappings (compat_win.c) */
#ifndef COMPAT_MMAN_H
#define COMPAT_MMAN_H
#include <stddef.h>
#define PROT_READ 1
#define MAP_PRIVATE 2
#define MAP_FAILED ((void *)-1)
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long long off);
int munmap(void *addr, size_t len);
#endif
