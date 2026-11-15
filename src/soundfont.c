/* soundfont.c - SoundFont lookup (spec README, v1.25) */
#include "soundfont.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

static int file_ok(const char *p) { return p && *p && access(p, R_OK) == 0; }

/* the folder of the running program ("" when unknown) */
static int exe_dir(char *out, size_t n) {
    char exe[1024];
#ifdef _WIN32
    wchar_t wexe[1024];
    DWORD wl = GetModuleFileNameW(NULL, wexe, 1024);
    if (wl == 0 || wl >= 1024) return 0;
    if (WideCharToMultiByte(CP_UTF8, 0, wexe, -1, exe, (int)sizeof exe, NULL, NULL) <= 0) return 0;
    for (char *c = exe; *c; c++) if (*c == '\\') *c = '/';
#elif defined(__APPLE__)
    uint32_t sz = sizeof exe;
    char raw[1024];
    if (_NSGetExecutablePath(raw, &sz) != 0 || !realpath(raw, exe)) return 0;
#else
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (l <= 0) return 0;
    exe[l] = 0;
#endif
    char *sl = strrchr(exe, '/');
    if (sl) *sl = 0;
    snprintf(out, n, "%s", exe);
    return 1;
}

/* the first .sf2 file of a folder (by name), or 0 */
static int first_sf2(const char *dir, char *buf, size_t n) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    char best[1100] = "";
    while ((de = readdir(d))) {
        size_t nl = strlen(de->d_name);
        if (nl > 4 && !strcasecmp(de->d_name + nl - 4, ".sf2") && (!best[0] || strcmp(de->d_name, best) < 0)) snprintf(best, sizeof best, "%s", de->d_name);
    }
    closedir(d);
    if (!best[0]) return 0;
    snprintf(buf, n, "%s/%s", dir, best);
    return 1;
}

const char *soundfont_find(const char *cli, const char *ini_value, char *buf, size_t n) {
    if (file_ok(cli)) return cli;
    if (file_ok(ini_value)) return ini_value;
    const char *env = getenv("SSO_SOUNDFONT");
    if (file_ok(env)) return env;
    char exe[1024];
    if (exe_dir(exe, sizeof exe)) {
        /* next to the program (a download or `make dist`), in an installed package (../share/exceeded) or in a macOS app (../Resources) */
        static const char *sub[] = {"%s/soundfont", "%s/../share/exceeded/soundfont", "%s/../Resources/soundfont", NULL};
        for (int i = 0; sub[i]; i++) {
            char dir[1300];
            snprintf(dir, sizeof dir, sub[i], exe);
            snprintf(buf, n, "%s/GeneralUser-GS.sf2", dir);
            if (file_ok(buf)) return buf;
            if (first_sf2(dir, buf, n)) return buf;
        }
    }
    static const char *cand[] = {"/usr/share/soundfonts/default.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2",
                                 "/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/sounds/sf2/default.sf2", NULL};
    for (int i = 0; cand[i]; i++) if (file_ok(cand[i])) return cand[i];
    return NULL;
}
