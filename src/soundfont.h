/* soundfont.h - SoundFont lookup (spec README, v1.25) */
#ifndef SOUNDFONT_H
#define SOUNDFONT_H
#include <stddef.h>
/* --soundfont FILE, SoundFont= of outnumbered.ini, $SSO_SOUNDFONT, <exe dir>/soundfont/GeneralUser-GS.sf2, any *.sf2 there, system paths.
 * Returns the path (cli / ini / buf) or NULL. */
const char *soundfont_find(const char *cli, const char *ini_value, char *buf, size_t n);
#endif
