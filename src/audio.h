/* audio.h - sound manager model (spec 03 section 5): 4 SFX channels + speech + music.
 * In script mode nothing is mixed: playback state is simulated in virtual time from the data lengths
 * and every start is written to the event log. */
#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>

void audio_init(const char *data_dir);
void audio_set_switches(int sfx, int music, int voice);
int audio_sfx_on(void);
int audio_music_on(void);
int audio_voice_on(void);

void snd_play(int id, int prio, int flags);   /* flags bit0: loop; bit1: restart if already playing */
#define SND_LOOP 1
#define SND_RESTART 2
void snd_stop(int id);
void snd_stop_all(void);                      /* all SFX channels and speech */
int snd_playing(int id);
void speech_play(int id);
void speech_stop(void);
int speech_playing(void);
void music_play(int text_id, int loop);       /* SND TEXT id naming a MIDI file */
void music_stop(void);
int music_playing(void);
int music_current(void);
void audio_menu_pause(int on);   /* a menu bar drop-down is open: waves, speech and the song freeze; they continue from there (03 section 3.0a A) */
void audio_pause(void);
void audio_resume(void);
/* wait helpers (pump input) */
void snd_wait(int id);
void speech_wait(void);
void music_wait(void);
/* duration in cs of a WAVE resource (0 if unknown) */
int wave_duration_cs(int id, const char *type);

/* optional DOS audio hooks (dosint.c) */
void snd_play_ctx(int id, int prio, int flags, int ctx);   /* ctx: SC_* of dosint.h */
void audio_restyle_begin(void);                            /* stop everything before the audio styles change */
void audio_restyle_end(void);                              /* restart the current music slot in the new style */
int audio_test_win_sfx(int id);                            /* Audio Options test: Windows effect; returns duration in cs */
int audio_test_win_speech(int id);                         /* speech WAVE of the Audio Options test; returns its duration in cs */
void audio_test_win_music(int text_id);
void audio_test_win_stop(void);
#endif
