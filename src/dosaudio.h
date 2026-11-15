/* dosaudio.h - optional DOS (1990) music driver model and synthesis (spec chapter 12 sections 2-4).
 * Reads the user's own SSO.EXE (EXEPACK) and MOCKET.DAT at run time. Nothing is executed. */
#ifndef DOSAUDIO_H
#define DOSAUDIO_H
#include <stddef.h>
#include <stdint.h>

#define DOS_SONGS 52
#define DOS_ENVS 36

enum { DOS_TANDY = 0, DOS_SPEAKER = 1 };

/* ------------------------------------------------------------ files and tables (section 2) */
typedef struct DosBlock { size_t off, size; int entries; } DosBlock;

typedef struct DosGame {
    uint8_t *img;  size_t img_n;         /* unpacked load image */
    uint8_t *moc;  size_t moc_n;         /* MOCKET.DAT */
    long ds;                             /* data segment offset in img */
    int scanned;                         /* tables found by scan rather than at 0x13F30 */
    uint16_t spk[12], tandy[12];
    int prio[DOS_SONGS];
    int envptr[DOS_ENVS];
    DosBlock blk[2];                     /* [DOS_TANDY], [DOS_SPEAKER] */
    uint8_t sha[32];
    char dir[1024];                      /* the path as given (folder or .zip) */
    char inner[1024];                    /* folder inside the source that holds the files (empty = top) */
} DosGame;

/* EXEPACK unpack of a whole SSO.EXE file image. 0 = ok, else err filled. Caller frees *out. */
int dos_unpack_exe(const uint8_t *file, size_t n, uint8_t **out, size_t *outn, char *err, size_t errn);
/* Find SSO.EXE/MOCKET.DAT in dir (or dir/SSOUTN), load and validate (section 2.6). 0 = ok. */
int dos_game_load(const char *dir, DosGame *g, char *err, size_t errn);
void dos_game_free(DosGame *g);
/* validate the two files in memory (12 section 2.6). 0 = ok, else err = the first failed check */
int dos_validate_bytes(const uint8_t *exe, size_t en, const uint8_t *moc, size_t mn, char *err, size_t errn);
/* Status for the UI/state: 0 ok, 1 missing files, 2 invalid. Fills err with the reason. */
int dos_game_probe(const char *dir, char *err, size_t errn);
void sha256(const uint8_t *d, size_t n, uint8_t out[32]);

/* ------------------------------------------------------------ driver (section 3) */
typedef struct DosChan {
    long ptr, loop_ptr;
    int active, cnt, d3, nogate, gate, rest, tie, newnote;
    int env_sel, env_pos, env_done, sus;
} DosChan;

typedef struct DosChip {
    int div[4];            /* tone divisors (0-2); [3] = noise control */
    int att[4];            /* 0..15, 15 = silent */
    unsigned noise_serial; /* increments on every noise-register write */
    int spk_on, spk_div;
} DosChip;

enum { DK_F, DK_NOISE, DK_V, DK_ON, DK_OFF };
typedef void (*DosLogFn)(void *ud, int ch, int kind, int value);

enum { DOS_IDLE = 0, DOS_START = 1, DOS_RUNNING = 2 };
enum { DOS_ACCEPTED = 0, DOS_IGN_PRIO = 1, DOS_IGN_MUSICOFF = 2, DOS_IGN_SOUNDOFF = 3 };

typedef struct DosDrv {
    const DosGame *g;
    int variant, nch;
    int state, current;                  /* current = -1 for NONE */
    int base, adj;
    int loop_flag, sound_on, music_off;
    DosChan c[4];
    DosChip chip;
    DosLogFn log;
    void *ud;
} DosDrv;

void dosdrv_init(DosDrv *d, const DosGame *g, int variant);
int dosdrv_play(DosDrv *d, int song);    /* returns DOS_ACCEPTED or a DOS_IGN_* code */
int dosdrv_force(DosDrv *d, int song);
void dosdrv_stop(DosDrv *d);
void dosdrv_set_loop(DosDrv *d, int flag);
void dosdrv_tick(DosDrv *d);
const char *dos_result_text(int r);

/* ------------------------------------------------------------ synthesis (section 4) */
#define DOS_OVS 4
#define DOS_FIR_DELAY 8   /* output samples of decimator delay (offline renders skip them) */
typedef struct DosSynth {
    int variant;
    double rate, dt;
    int voicing;                         /* speaker voicing (HP 250 / LP 5000) */
    double phase[4], sp_phase;           /* oscillator phases (cycles) */
    double nphase;
    unsigned lfsr, serial_seen;
    double amp[4], freq[3], nfreq, spf, a_lp, a_hp;
    int nc, spk_on;
    float hist[128];
    int hpos;
    double hp_lp, lp_y;
} DosSynth;

void dossynth_init(DosSynth *s, int variant, double rate, int voicing);
void dossynth_latch(DosSynth *s, const DosChip *chip);
void dossynth_step(DosSynth *s);          /* one oversampled step (DOS_OVS per output sample) */
float dossynth_out(DosSynth *s);          /* output sample, after DOS_OVS steps */
/* Render n samples at the reference levels with the chip state held constant. */
void dossynth_render(DosSynth *s, const DosChip *chip, float *out, int n);

/* oversampled step / output sample index at which driver tick k starts, for a given output rate */
int64_t dos_tick_ovs(int64_t k, int rate);
/* float render of one pass of a song at the reference levels (caller frees); see dosaudio.c */
float *dos_render_float(const DosGame *g, int song, int variant, int rate, int max_ticks, size_t *nout);

/* sample index at which driver tick k starts, for a given output rate */
int64_t dos_tick_sample(int64_t k, int rate);
#endif
