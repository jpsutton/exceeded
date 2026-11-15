/* soundfont.c - SoundFont lookup (spec README, v1.25) */
#include "soundfont.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int file_ok(const char *p) { return p && *p && access(p, R_OK) == 0; }

const char *soundfont_find(const char *cli, const char *ini_value, char *buf, size_t n) {
    if (file_ok(cli)) return cli;
    if (file_ok(ini_value)) return ini_value;
    const char *env = getenv("SSO_SOUNDFONT");
    if (file_ok(env)) return env;
    char exe[1024];
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (l > 0) {
        exe[l] = 0;
        char *sl = strrchr(exe, '/');
        if (sl) *sl = 0;
        snprintf(buf, n, "%s/soundfont/GeneralUser-GS.sf2", exe);
        if (file_ok(buf)) return buf;
        char dir[1100];
        snprintf(dir, sizeof dir, "%s/soundfont", exe);
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *de;
            char best[1100] = "";
            while ((de = readdir(d))) {
                size_t nl = strlen(de->d_name);
                if (nl > 4 && !strcasecmp(de->d_name + nl - 4, ".sf2") && (!best[0] || strcmp(de->d_name, best) < 0)) snprintf(best, sizeof best, "%s", de->d_name);
            }
            closedir(d);
            if (best[0]) { snprintf(buf, n, "%s/%s", dir, best); return buf; }
        }
    }
    static const char *cand[] = {"/usr/share/soundfonts/default.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2",
                                 "/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/sounds/sf2/default.sf2", NULL};
    for (int i = 0; cand[i]; i++) if (file_ok(cand[i])) return cand[i];
    return NULL;
}
