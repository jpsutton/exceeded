/* aseq.h - ASEQ/ESEQ animation resources, animated objects (AO), screen elements (SE),
 * compositor. Spec 02 sections 2 and 5. */
#ifndef ASEQ_H
#define ASEQ_H
#include "gfx.h"

typedef struct SeqEnt { int16_t a, b, c, d; } SeqEnt;
typedef struct Seq { int n; SeqEnt *e; } Seq;

typedef struct Aseq {
    int id;
    int nseq, ncel, fps, w3, w4;
    Seq *seq;
    Cel *cel;          /* lazily decoded */
    uint8_t *cel_ok;   /* 0 = not decoded, 1 = ok, 2 = failed */
    const uint8_t *data;
    size_t len;
    uint32_t *celoff;
    size_t celbase;
    Seq *eseq;         /* attached ESEQ sequences (negative sequence numbers) */
    int neseq;
    int eseq_id;
} Aseq;

Aseq *aseq_load(int id);                 /* cached; NULL if missing */
const Cel *aseq_cel(Aseq *a, int n);     /* decode on demand; NULL if invalid */
int aseq_attach_eseq(Aseq *a, int eseq_id);
Seq *aseq_get_seq(Aseq *a, int n);       /* n < 0 : ESEQ sequence -n-1 */

enum { AO_STOPPED = 0, AO_RUNNING = 1, AO_PAUSED = 0x10 };

#define SE_DRAW 0x0001      /* needs drawing */
#define SE_TEMP 0x0002
#define SE_REDRAW 0x0004    /* externally requested redraw */
#define SE_STAMP 0x0008     /* also draw into background */
#define SE_NOPROP 0x0010    /* do not propagate overlap */
#define SE_OVERLAP 0x0040   /* overlapped by a changed SE */
#define SE_MIRROR 0x80
#define SE_VIEWPORT 0x200

typedef struct AO AO;
typedef struct SE {
    AO *owner;
    const Cel *cel;
    Rect r;         /* clipped rectangle */
    int x, y;       /* absolute origin */
    int dx, dy;     /* offset from AO position when built */
    int z;
    int flags;
} SE;

#define AO_MAX_SE 16

typedef int (*AoUserHook)(AO *ao, int code, int a, int b, int d);
typedef void (*AoCallback)(AO *ao);

struct AO {
    Aseq *as;
    int serial;
    int level;            /* owner/level tag for bulk destroy */
    int seqno;
    Seq *seq;
    int idx;
    int state;
    int x, y, z;
    int rcount;           /* repeat counter */
    int loops;            /* loop counter */
    int label, labelpos;  /* cached label id and index after it (-1 = none) */
    int period;
    int64_t next_time;
    int allow_catchup;
    int hidden;
    int use_viewport;
    AO *link;
    AoCallback callback;
    void *user;
    int frame_idx, frame_count;   /* last built frame (entry index, number of cels) */
    SE *se[AO_MAX_SE];
    int nse;
    int stamp_next;
    int in_list;
    /* text hooks for opcodes -18 / -22 */
    void (*text_hook)(AO *ao, int a, int stamp);
    void (*post_draw)(AO *ao);      /* called after each of this AO's SEs is drawn (SE flag 0x800) */
};

/* ---- engine state ---- */
typedef struct Engine {
    int64_t now;                 /* ticks, 1/100 s */
    Canvas *work, *back;
    Rect viewport;               /* alternate clip rectangle */
    AoUserHook user_hook;
    void (*pre_anim)(void);
    void (*post_anim)(void);
} Engine;
extern Engine g_eng;

void eng_init(void);
void eng_shutdown(void);

AO *ao_create(int aseq_id);
void ao_destroy(AO *ao);
void ao_destroy_level(int level);
void ao_start(AO *ao, int seq);
void ao_set_seq(AO *ao, int seq);
void ao_set_pos(AO *ao, int x, int y);
void ao_set_z(AO *ao, int z);
void ao_set_fps(AO *ao, int fps);
void ao_stop(AO *ao);
void ao_hide(AO *ao);
void ao_show(AO *ao);
void ao_pause(AO *ao);
void ao_resume(AO *ao);
void ao_erase(AO *ao);
void ao_force_redraw(AO *ao);          /* drop all SEs */
/* Show one cel statically (no script): used for backgrounds/panels. */
void eng_stamp_cel(Aseq *as, int cel, int x, int y, int into_back);

/* Run all due AOs for the current g_eng.now. */
void eng_run_aos(void);
/* Compose back + SEs into work. */
void eng_render(void);
/* Number of live SEs (diagnostics). */
int eng_se_count(void);

/* single-AO step used by tests: returns 1 if a new frame was produced */
int ao_update(AO *ao);

#endif
