/* compat_win.c - Windows (MSYS2 UCRT64): wide-API file operations on UTF-8 paths, and the POSIX calls the engine uses.
 * Spec 14 section 1.3. The whole file is empty on other systems. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef fopen
#undef stat
#undef lstat
#undef unlink
#undef rename
#undef mkdir
#undef rmdir
#undef access
#undef chmod
#undef open
#undef getenv
#undef getcwd
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "compat/dirent.h"
#include "compat/sys/mman.h"
#include "compat/sys/file.h"
#include "compat/sys/statvfs.h"

/* UTF-8 -> UTF-16 with '\' separators. An absolute path of 248 characters or more gets the \\?\ prefix. Caller frees. */
static wchar_t *wpath(const char *u8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = malloc(sizeof(wchar_t) * (size_t)(n + 16));
    if (!w) return NULL;
    wchar_t *p = w;
    int absolute = (((u8[0] >= 'A' && u8[0] <= 'Z') || (u8[0] >= 'a' && u8[0] <= 'z')) && u8[1] == ':' && (u8[2] == '/' || u8[2] == '\\'));
    int unc = (u8[0] == '/' || u8[0] == '\\') && (u8[1] == '/' || u8[1] == '\\') && u8[2] != '?';
    int prefixed = (u8[0] == '\\' && u8[1] == '\\' && u8[2] == '?');
    if (n - 1 >= 248 && !prefixed && (absolute || unc)) {
        if (unc) { wcscpy(p, L"\\\\?\\UNC\\"); p += 8; u8 += 2; n -= 2; }
        else { wcscpy(p, L"\\\\?\\"); p += 4; }
    }
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, p, n);
    for (wchar_t *c = p; *c; c++) if (*c == L'/') *c = L'\\';
    return w;
}

static char *to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = malloc((size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static int err_from_win(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME: return ENOENT;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return EEXIST;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: return EACCES;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return ENOSPC;
    case ERROR_FILENAME_EXCED_RANGE: return ENAMETOOLONG;
    case ERROR_DIR_NOT_EMPTY: return ENOTEMPTY;
    }
    return EIO;
}

FILE *w_fopen(const char *path, const char *mode) {
    wchar_t *w = wpath(path);
    wchar_t wm[16];
    int i = 0;
    for (; mode[i] && i < 15; i++) wm[i] = (wchar_t)(unsigned char)mode[i];
    wm[i] = 0;
    if (!w) { errno = EINVAL; return NULL; }
    FILE *f = _wfopen(w, wm);
    free(w);
    return f;
}

int w_stat(const char *path, struct stat *st) {
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    size_t l = wcslen(w);
    while (l > 3 && (w[l - 1] == L'\\')) w[--l] = 0;                 /* _wstat64 refuses a trailing separator (but not "C:\") */
    struct _stat64 s;
    int r = _wstat64(w, &s);
    free(w);
    if (r != 0) return -1;
    memset(st, 0, sizeof *st);
    st->st_mode = s.st_mode;
    st->st_size = (off_t)s.st_size;
    st->st_mtime = (time_t)s.st_mtime;
    return 0;
}

int w_unlink(const char *path) {
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    BOOL ok = DeleteFileW(w);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {                /* files copied off a CD are read-only */
        SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);
        ok = DeleteFileW(w);
    }
    DWORD e = GetLastError();
    free(w);
    if (!ok) { errno = err_from_win(e); return -1; }
    return 0;
}

int w_rename(const char *from, const char *to) {
    wchar_t *a = wpath(from), *b = wpath(to);
    if (!a || !b) { free(a); free(b); errno = EINVAL; return -1; }
    BOOL ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    DWORD e = GetLastError();
    free(a);
    free(b);
    if (!ok) { errno = err_from_win(e); return -1; }
    return 0;
}

int w_mkdir(const char *path) {
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    BOOL ok = CreateDirectoryW(w, NULL);
    DWORD e = GetLastError();
    if (!ok) {
        DWORD a = GetFileAttributesW(w);                              /* "C:" or an existing folder: already there */
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) e = ERROR_ALREADY_EXISTS;
    }
    free(w);
    if (!ok) { errno = err_from_win(e); return -1; }
    return 0;
}

int w_rmdir(const char *path) {
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    BOOL ok = RemoveDirectoryW(w);
    DWORD e = GetLastError();
    free(w);
    if (!ok) { errno = err_from_win(e); return -1; }
    return 0;
}

int w_access(const char *path, int mode) {
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    int r = _waccess(w, mode & 6);                                   /* no execute bit on Windows */
    free(w);
    return r;
}

int w_chmod(const char *path, int mode) {
    (void)mode;
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    int r = _wchmod(w, _S_IREAD | _S_IWRITE);                        /* the folder keeps the ACL of Local AppData; files stay writable */
    free(w);
    return r;
}

int w_open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & _O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap); }
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    int fd = _wopen(w, flags | _O_BINARY, mode ? mode : (_S_IREAD | _S_IWRITE));
    free(w);
    return fd;
}

/* an environment variable as UTF-8 with '/' separators (a few buffers in rotation) */
char *w_getenv(const char *name) {
    static char *ring[8];
    static int next;
    wchar_t wn[128];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wn, 128) <= 0) return NULL;
    DWORD n = GetEnvironmentVariableW(wn, NULL, 0);
    if (n == 0) return NULL;
    wchar_t *v = malloc(sizeof(wchar_t) * n);
    if (!v) return NULL;
    GetEnvironmentVariableW(wn, v, n);
    char *s = to_utf8(v);
    free(v);
    if (!s) return NULL;
    free(ring[next]);
    ring[next] = s;
    next = (next + 1) % 8;
    return s;
}

char *w_getcwd(char *buf, size_t n) {
    wchar_t w[4096];
    if (!_wgetcwd(w, 4096)) return NULL;
    char *s = to_utf8(w);
    if (!s || strlen(s) + 1 > n) { free(s); errno = ERANGE; return NULL; }
    for (char *c = s; *c; c++) if (*c == '\\') *c = '/';
    strcpy(buf, s);
    free(s);
    return buf;
}

/* ------------------------------------------------------------------ directories */
struct W_DIR { HANDLE h; WIN32_FIND_DATAW fd; int first; struct dirent ent; };

DIR *opendir(const char *path) {
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return NULL; }
    size_t l = wcslen(w);
    wchar_t *pat = malloc(sizeof(wchar_t) * (l + 3));
    if (!pat) { free(w); return NULL; }
    wcscpy(pat, w);
    if (l && pat[l - 1] != L'\\') wcscat(pat, L"\\");
    wcscat(pat, L"*");
    free(w);
    DIR *d = calloc(1, sizeof *d);
    if (!d) { free(pat); return NULL; }
    d->h = FindFirstFileW(pat, &d->fd);
    DWORD e = GetLastError();
    free(pat);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); errno = err_from_win(e); return NULL; }
    d->first = 1;
    return d;
}

struct dirent *readdir(DIR *d) {
    if (d->first) d->first = 0;
    else if (!FindNextFileW(d->h, &d->fd)) return NULL;
    char *s = to_utf8(d->fd.cFileName);
    if (!s) return NULL;
    snprintf(d->ent.d_name, sizeof d->ent.d_name, "%s", s);
    free(s);
    return &d->ent;
}

int closedir(DIR *d) {
    if (!d) return -1;
    FindClose(d->h);
    free(d);
    return 0;
}

/* ------------------------------------------------------------------ mmap, flock, statvfs */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long long off) {
    (void)addr; (void)prot; (void)flags; (void)len;
    if (off != 0) return MAP_FAILED;
    HANDLE f = (HANDLE)_get_osfhandle(fd);
    if (f == INVALID_HANDLE_VALUE) return MAP_FAILED;
    HANDLE m = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!m) return MAP_FAILED;
    void *v = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(m);                                                   /* the view keeps the mapping alive after the file is closed */
    return v ? v : MAP_FAILED;
}

int munmap(void *addr, size_t len) {
    (void)len;
    return UnmapViewOfFile(addr) ? 0 : -1;
}

int flock(int fd, int op) {
    HANDLE f = (HANDLE)_get_osfhandle(fd);
    if (f == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    if (op & LOCK_UN) return UnlockFileEx(f, 0, 1, 0, &ov) ? 0 : -1;
    DWORD fl = ((op & LOCK_EX) ? LOCKFILE_EXCLUSIVE_LOCK : 0) | ((op & LOCK_NB) ? LOCKFILE_FAIL_IMMEDIATELY : 0);
    if (LockFileEx(f, fl, 0, 1, 0, &ov)) return 0;
    errno = EWOULDBLOCK;
    return -1;
}

int statvfs(const char *path, struct statvfs *sv) {
    wchar_t *w = wpath(path);
    if (!w) { errno = EINVAL; return -1; }
    ULARGE_INTEGER avail, total, freeb;
    BOOL ok = GetDiskFreeSpaceExW(w, &avail, &total, &freeb);
    free(w);
    if (!ok) { errno = EIO; return -1; }
    sv->f_frsize = 1;
    sv->f_bavail = avail.QuadPart;
    return 0;
}

/* start-up: binary file mode (no CRLF translation), and, when built as a GUI program and started from a console, that console for
 * any standard stream that is not redirected (a pipe or file the parent set up is left alone) */
static int std_missing(DWORD which) {
    HANDLE h = GetStdHandle(which);
    return h == NULL || h == INVALID_HANDLE_VALUE;
}
__attribute__((constructor)) static void win_init(void) {
    _set_fmode(_O_BINARY);
    int o = std_missing(STD_OUTPUT_HANDLE), e = std_missing(STD_ERROR_HANDLE), i = std_missing(STD_INPUT_HANDLE);
    if ((o || e || i) && !GetConsoleWindow() && AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (o) freopen("CONOUT$", "w", stdout);
        if (e) freopen("CONOUT$", "w", stderr);
        if (i) freopen("CONIN$", "r", stdin);
    }
}
#endif
