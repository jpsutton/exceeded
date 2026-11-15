/* mtht.h - MTHT word-problem template interpreter (spec 06 sections 2-5) */
#ifndef MTHT_H
#define MTHT_H
#include "util.h"

typedef struct MthtVar { int count, size, offset; } MthtVar;

typedef struct MthtInst {
    int id, seed, qnum;
    int nvar;
    MthtVar *var;
    int answer_var, question_var, hint_var, title_var;   /* 0-based */
    uint8_t *buf;
    int bufsize;
    int answer;
    char question[1024];
    char hint[512];
} MthtInst;

/* Instantiate MTHT `id`. Seeds RNG-B with `seed` (spec 06 section 4), question number `qnum`.
 * Returns 0 on success. Free with mtht_free. */
int mtht_run(int id, int seed, int qnum, MthtInst *out);
void mtht_free(MthtInst *m);

/* Access by flat element number ("#n" in spec 06 section 8). Return -1/NULL when out of range. */
int mtht_flat_addr(const MthtInst *m, int n);
int mtht_flat_word(const MthtInst *m, int n);          /* int16 at element n */
const char *mtht_flat_str(const MthtInst *m, int n);   /* NUL-terminated string at element n */

/* exposed for tests */
int mtht_rand_step(int step, int lo, int hi);
int mtht_rand(int a, int b);

#endif
