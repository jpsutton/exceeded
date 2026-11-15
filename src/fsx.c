/* fsx.c - file system helpers for the per-user installation (spec chapter 14); POSIX */
#include "fsx.h"
#include "util.h"
#include <dirent.h>
#include <errno.h>
#ifndef _WIN32
#include <pwd.h>
#endif
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

char *fsx_abspath(const char *p) {
    if (PATH_IS_ABSOLUTE(p)) return xstrdup(p);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return xstrdup(p);
    size_t n = strlen(cwd) + strlen(p) + 2;
    char *r = xmalloc(n);
    snprintf(r, n, "%s/%s", cwd, p);
    return r;
}

int fsx_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
int fsx_isdir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
int fsx_isfile(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }

int fsx_mkdirs(const char *path, unsigned mode) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *c = tmp + 1; *c; c++)
        if (*c == '/') {
            *c = 0;
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return errno;
            *c = '/';
        }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return errno;
    return 0;
}

int fsx_rmtree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : errno;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                char sub[4096];
                snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
                fsx_rmtree(sub);
            }
            closedir(d);
        }
        return rmdir(path) == 0 ? 0 : errno;
    }
    return unlink(path) == 0 ? 0 : errno;
}

uint64_t fsx_free_space(const char *path) {
    char p[4096];
    snprintf(p, sizeof p, "%s", path);
    while (!fsx_exists(p)) {                        /* the nearest existing parent */
        char *sl = strrchr(p, '/');
        if (!sl || sl == p) { snprintf(p, sizeof p, "/"); break; }
        *sl = 0;
    }
    struct statvfs sv;
    if (statvfs(p, &sv) != 0) return UINT64_MAX;
    return (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
}

int fsx_base_dir(const char *opt, char *out, size_t n, const char **from) {
    const char *f = "option";
    char tmp[4096] = "";
    if (opt && *opt) snprintf(tmp, sizeof tmp, "%s", opt);
    else {
        const char *env = getenv("OUTNUMBERED_HOME");
        if (env && *env) { snprintf(tmp, sizeof tmp, "%s", env); f = "env"; }
        else {
            f = "default";
            const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
#ifdef _WIN32
            (void)xdg; (void)home;                          /* %LOCALAPPDATA%\OutNumbered (spec 14 section 1.1); the engine keeps '/' separators */
            const char *la = getenv("LOCALAPPDATA"), *up = getenv("USERPROFILE");
            if (la && *la) snprintf(tmp, sizeof tmp, "%s/OutNumbered", la);
            else if (up && *up) snprintf(tmp, sizeof tmp, "%s/AppData/Local/OutNumbered", up);
            else return -1;
            for (char *c = tmp; *c; c++) if (*c == '\\') *c = '/';
#elif defined(__APPLE__)
            (void)xdg;
            if (!home || !*home) { struct passwd *pw = getpwuid(getuid()); home = pw ? pw->pw_dir : NULL; }
            if (!home || !*home) return -1;
            snprintf(tmp, sizeof tmp, "%s/Library/Application Support/OutNumbered", home);
#else
            if (xdg && xdg[0] == '/') snprintf(tmp, sizeof tmp, "%s/outnumbered", xdg);
            else {
                if (!home || !*home) { struct passwd *pw = getpwuid(getuid()); home = pw ? pw->pw_dir : NULL; }
                if (!home || !*home) return -1;
                snprintf(tmp, sizeof tmp, "%s/.local/share/outnumbered", home);
            }
#endif
        }
    }
    char *abs = fsx_abspath(tmp);
    size_t l = strlen(abs);
    while (l > 1 && abs[l - 1] == '/' && !(l == 3 && abs[1] == ':')) abs[--l] = 0;
    snprintf(out, n, "%s", abs);
    free(abs);
    if (from) *from = f;
    return 0;
}

const char *fsx_error_code(int err) {
    switch (err) {
    case ENOSPC:
#ifdef EDQUOT
    case EDQUOT:
#endif
        return "disk_full";
    case EACCES: case EPERM: case EROFS: return "permission";
    case ENAMETOOLONG: return "path_too_long";
    }
    return "io_error";
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void list_rec(const char *root, const char *rel, char ***out, int *n) {
    char dir[4096];
    snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char r2[4096], full[8192];
        snprintf(r2, sizeof r2, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/%s", root, r2);
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) list_rec(root, r2, out, n);
        else if (S_ISREG(st.st_mode)) { *out = xrealloc(*out, sizeof(char *) * (size_t)(*n + 1)); (*out)[(*n)++] = xstrdup(r2); }
    }
    closedir(d);
}

int fsx_list_files(const char *dir, char ***out) {
    char **a = NULL;
    int n = 0;
    list_rec(dir, "", &a, &n);
    qsort(a, (size_t)n, sizeof(char *), cmp_str);
    *out = a;
    return n;
}
