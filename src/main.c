/* OutNumbered! clean-room engine - entry point */
#include "conform.h"
#include "setup.h"
#include "game.h"
#include "util.h"
#if defined(_WIN32) && !defined(NO_SDL)
#include <SDL.h>   /* SDL_main: the arguments arrive as UTF-8 */
#endif

const char *g_opt_dos_game, *g_opt_dos_audio, *g_opt_music_style, *g_opt_sfx_style;
int g_opt_vol[3] = {-1, -1, -1}, g_opt_even = -1;
const char *g_opt_soundfont;
const char *g_opt_wav[64][2];
int g_opt_nwav;   /* command-line audio options (spec 12 section 1.2) */

/* --name=VALUE or --name VALUE (like --dos-game): the number of argv entries used, 0 if argv[i] is not this option. The value must be a
 * style; anything else is an error, not silently ignored. */
static int style_option(int argc, char **argv, int i, const char *name, const char **dst, int apply) {
    size_t n = strlen(name);
    const char *v = NULL;
    int used = 0;
    if (!strncmp(argv[i], name, n) && argv[i][n] == '=') { v = argv[i] + n + 1; used = 1; }
    else if (!strcmp(argv[i], name) && i + 1 < argc) { v = argv[i + 1]; used = 2; }
    else if (!strcmp(argv[i], name)) { fprintf(stderr, "engine: %s needs a value: windows, tandy or speaker.\n", name); exit(1); }
    if (!used) return 0;
    if (strcasecmp(v, "windows") && strcasecmp(v, "tandy") && strcasecmp(v, "speaker")) {
        if (apply) { fprintf(stderr, "engine: %s: expected windows, tandy or speaker (got '%s').\n", name, v); exit(1); }
        return used;
    }
    if (apply) *dst = v;
    return used;
}

/* returns the number of argv entries used (0 = not an audio / setup option) */
static int audio_option(int argc, char **argv, int i, int apply) {
    if (!strcmp(argv[i], "--soundfont") && i + 1 < argc) { if (apply) { g_opt_soundfont = argv[i + 1]; } return 2; }
    if (!strcmp(argv[i], "--volumes") && i + 1 < argc) { if (apply) { sscanf(argv[i + 1], "%d,%d,%d", &g_opt_vol[0], &g_opt_vol[1], &g_opt_vol[2]); } return 2; }
    if (!strcmp(argv[i], "--wav") && i + 2 < argc && g_opt_nwav < 64) { if (apply) { g_opt_wav[g_opt_nwav][0] = argv[i + 1]; g_opt_wav[g_opt_nwav][1] = argv[i + 2]; g_opt_nwav++; } return 3; }
    if (!strcmp(argv[i], "--music-volume") && i + 1 < argc) { if (apply) { g_opt_vol[0] = atoi(argv[i + 1]); } return 2; }
    if (!strcmp(argv[i], "--effects-volume") && i + 1 < argc) { if (apply) { g_opt_vol[1] = atoi(argv[i + 1]); } return 2; }
    if (!strcmp(argv[i], "--speech-volume") && i + 1 < argc) { if (apply) { g_opt_vol[2] = atoi(argv[i + 1]); } return 2; }
    if (!strcmp(argv[i], "--even-out") && i + 1 < argc) { if (apply) { g_opt_even = atoi(argv[i + 1]); } return 2; }
    if (!strcmp(argv[i], "--home") && i + 1 < argc) { if (apply) { g_opts.home = argv[i + 1]; } return 2; }
    if (!strcmp(argv[i], "--setup")) { if (apply) { g_opts.setup = 1; } return 1; }
    if (!strcmp(argv[i], "--verify-install")) { if (apply) { g_opts.verify = 1; } return 1; }
    if (!strcmp(argv[i], "--dos-game") && i + 1 < argc) { if (apply) { g_opt_dos_game = argv[i + 1]; } return 2; }
    { int u = style_option(argc, argv, i, "--dos-audio", &g_opt_dos_audio, apply); if (u) return u; }
    { int u = style_option(argc, argv, i, "--music-style", &g_opt_music_style, apply); if (u) return u; }
    { int u = style_option(argc, argv, i, "--sfx-style", &g_opt_sfx_style, apply); if (u) return u; }
    return 0;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    for (int i = 1; i < argc; i++) for (char *c = argv[i]; *c; c++) if (*c == '\\') *c = '/';      /* the engine keeps '/' separators */
#endif
    const char *data = NULL, *save = NULL, *out = NULL;
    int conform_at = -1;
    for (int i = 1; i < argc; i++) { int u = audio_option(argc, argv, i, 1); if (u) i += u - 1; }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--save-dir") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--conform") && i + 1 < argc) { conform_at = i + 1; break; }
    }
    if (conform_at >= 0) {
        /* conform args: the command and its positional arguments, then every other option wherever it stood (before or after the command).
         * Options we know ourselves (--out / --data / --save-dir pairs, audio, setup, volume, --wav) are taken out. */
        char *cargs[256];
        int n = 0;
        cargs[n++] = argv[conform_at];
        for (int pass = 0; pass < 2; pass++) {
            int from = pass == 0 ? conform_at + 1 : 1, to = pass == 0 ? argc : conform_at - 1;
            for (int i = from; i < to && n < 256; i++) {
                { int u = audio_option(argc, argv, i, 0); if (u) { i += u - 1; continue; } }
                if ((!strcmp(argv[i], "--out") || !strcmp(argv[i], "--data") || !strcmp(argv[i], "--save-dir")) && i + 1 < argc) {
                    if (!strcmp(argv[i], "--out")) out = argv[i + 1];
                    else if (!strcmp(argv[i], "--data")) data = argv[i + 1];
                    else save = argv[i + 1];
                    i++;
                    continue;
                }
                cargs[n++] = argv[i];
            }
        }
        g_opts.data = data; g_opts.dos_game = g_opt_dos_game; g_opts.save_dir = save;
        if (g_opts.setup && (g_opts.data || g_opts.dos_game)) { fprintf(stderr, "engine: --setup cannot be combined with --data or --dos-game.\n"); return 1; }
        return conform_main(data, save, n, cargs, out);
    }
    const char *sf = g_opt_soundfont;
    int fs = 0;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--fullscreen")) fs = 1;
    g_opts.data = data; g_opts.dos_game = g_opt_dos_game; g_opts.save_dir = save;
    if (g_opts.setup && (g_opts.data || g_opts.dos_game)) { fprintf(stderr, "engine: --setup cannot be combined with --data or --dos-game.\n"); return 1; }
    if (0) {
        fprintf(stderr, "usage: engine --data DIR [--save-dir DIR] [--soundfont FILE] [--fullscreen]\n"
                        "              [--dos-audio=tandy|speaker] [--music-style=S] [--sfx-style=S] [--dos-game DIR]\n"
                        "       engine [--data DIR] --conform CMD ... --out FILE\n");
        return 2;
    }
#ifdef NO_SDL
    fprintf(stderr, "engine: built without SDL2; only --conform commands are available\n");
    return 2;
#else
    shell_main(data, save, sf, fs);
    return 0;
#endif
}
