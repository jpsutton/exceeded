/* dosint.h - game integration of the optional DOS audio (spec chapter 12 sections 1, 5, 6.5, 7.6) */
#ifndef DOSINT_H
#define DOSINT_H
#include <stdint.h>
#include <stdio.h>

enum { AS_WINDOWS = 0, AS_TANDY = 1, AS_SPEAKER = 2 };
enum { DS_OK = 0, DS_MISSING = 1, DS_INVALID = 2, DS_UNSET = 3 };

typedef struct AudioSettings {
    int music, sfx;            /* AS_* */
    char dir[1024];            /* DosGameDir ("" = not set) */
    int shared;                /* DosSharedVoice */
    char soundfont[1024];      /* SoundFont= (read only; the engine never writes it) */
    int vol[3];                /* MusicVolume, EffectsVolume, SpeechVolume: 0..100 (spec 15 section 6) */
    int even;                  /* EvenOutVolume */
} AudioSettings;

/* sound-effect request contexts (the same Windows id can mean different DOS songs, spec 12 section 5.3) */
enum { SC_DEFAULT = 0, SC_ENDING, SC_AWARD, SC_SCORE, SC_WRONGDEC, SC_INTRO };
/* condition keys for repeating requests */
enum { DF_TELLY_FLASH = 0x7001, DF_SPIN_PLAYED = 0x7002 };

/* start-up: read the INI in save_dir, apply the command-line options, load the DOS files */
void dosint_set_installed_dos(const char *dir);        /* <base>/dos when the installation holds valid DOS files (14 section 2.3), else NULL; call before dosint_init */
int dosint_dos_installed(char *buf, size_t n);          /* 1 if the installation's DOS files are in use as the fallback; buf = their path */
void dosint_reload_dos(const char *installed_dir);      /* the installed DOS files changed: reload */
void dosint_init(const char *save_dir);
void dosint_set_save_dir(const char *save_dir);
void dosint_peek_ini(const char *save_dir, AudioSettings *s);   /* read outnumbered.ini of a folder without changing state */
void dosint_get_settings(AudioSettings *s);           /* requested (stored) values */
int dosint_status(char *reason, size_t n);            /* DS_*; reason text for missing/invalid */
int dosint_check_dir(const char *dir, char *reason, size_t n, char *inner, size_t in_n);   /* DS_* for a folder or .zip; inner = folder inside it; no state change */
/* choose styles/folder as if confirmed with OK in Audio Options: validates, writes the INI, applies at once */
void dosint_apply(const AudioSettings *s, int write_ini);
void dosint_set_levels_live(const int vol[3], int even);   /* Audio Options sliders / check box, applied at once */
int dosint_music_dos(void);                           /* music goes through a DOS driver */
int dosint_sfx_dos(void);
int dosint_shared(void);

/* realtime mixing (interactive build) */
void dosint_set_realtime(int rate, void (*lock)(void), void (*unlock)(void));
void dosint_render(float *acc, int n);                /* called by the audio callback (lock held); adds the DOS buses (full scale 1.0) */

/* sound effects. dosint_sfx returns 1 when the DOS driver handled the request (or defined it as silent). */
int dosint_sfx(int id, int flags, int ctx);
void dosint_sfx_stop(int id);                         /* Windows StopSFX(id) */
void dosint_stop_effects(void);
void dosint_stop_effects_keep_ride(void);                /* ... except an elevator ride song (12 section 5.6a) */
void dosint_elevator_ride(int up);                       /* the ride song: 22 up, 21 down; once, when the departure doors have closed */
void dosint_effects_off(void);                        /* Sound Effects switched off */                       /* stop all sounds / speech / dialog */
int dosint_sfx_playing(int id);
void dosint_flag(int key, int on);
enum { TL_NONE = 0, TL_AWARD, TL_CHAMPION, TL_LOSE, TL_MIDNIGHT };
int dosint_timeline_begin(int kind);                  /* a DOS ending timeline starts now (12 section 5.5); 0 if that driver is not DOS */
int dosint_timeline_active(void);                     /* the timeline is not finished yet */
void dosint_zap_lit_telly(void);                      /* the zap that hits a lit Telly: force(16) after the Windows stop-all */
/* music slots */
void dosint_music_play(int text_id, int loop);
void dosint_music_stop(void);
int dosint_music_playing(void);                       /* the slot's DOS song is still playing */
void dosint_action_loop(int on);                      /* the action game's frame loop is waiting for its frame */
void dosint_pause(int on);
void dosint_sfx_keep(int on);                         /* do not stop DOS effects when Sound Effects is switched off */
int dosint_current_song(void);
void dosint_state_json(FILE *f);
/* Audio Options "Test" sample */
void dosint_test_start(int music_style, int sfx_style, const char *dir);
void dosint_test_stop(void);
int dosint_test_running(void);
void audio_options_dialog(void);                    /* audiodlg.c */
#endif
