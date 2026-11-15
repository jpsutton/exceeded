/* rules.h - ranks, tiers, per-tier tables, drill deck, puzzle (spec 05) */
#ifndef RULES_H
#define RULES_H
#include "util.h"

extern const char *const rank_names[8];
extern const uint32_t rank_thresholds[8];
int rank_for_score(uint32_t lifetime);   /* 0..7 */
static inline int tier_for_rank(int rank) { return rank >> 1; }

/* per-tier parameter table 05 section 0.1, fields 0..12 */
extern const int tier_params[13][4];
enum { TP_MATH_BASE, TP_MATH_PENALTY, TP_TELLY_SPAWN, TP_TELLY_THROW, TP_TELLY_LIT, TP_TELLY_SPEED,
       TP_TELLY_POPIN, TP_LIVEWIRE, TP_WIN_BONUS, TP_ENERGY_BONUS, TP_MINUTE_BONUS, TP_UNKNOWN11, TP_ENERGY_CREDIT };


/* ---- customisation records (05 section 2.1) ---- */
extern const uint8_t tier_cust[4][13];
enum { OP_ADD = 0, OP_MUL = 1, OP_SUB = 2, OP_DIV = 3 };

/* ---- drill deck / questions (05 section 2.2, 2.3) ---- */
typedef struct Fact { uint8_t op, lo, hi; } Fact;
typedef struct DrillDeck {
    Fact f[676];
    int n;
    int remaining;
} DrillDeck;
void deck_build(DrillDeck *d, const uint8_t cust[13]);
void deck_reset_remaining(DrillDeck *d);
Fact deck_draw(DrillDeck *d);      /* RNG-A */

typedef struct Question {
    Fact fact;
    int format;          /* 0 hides C, 1 hides A, 2 hides B */
    int a, b, c;         /* underlying values (after the 0 / ? = 1 special case) */
    int answer;
    char display[32];    /* ASCII: "+", "x", "-", "/" and "?" */
} Question;
void question_make(Question *q, DrillDeck *d, const uint8_t cust[13]);
/* does `entry` count as correct? (includes the any-answer cases for multiplication) */
int question_check(const Question *q, int entry);
int cust_default_size_index(int tier);

/* ---- deduction puzzle (05 section 3) ---- */
typedef struct Puzzle {
    uint8_t code[4];
    uint8_t room[5][4];
    int hideout;
    int rule;
} Puzzle;
void puzzle_clear_code(void);   /* sign-in: zero the code (game-state block) */
void puzzle_reset(void);   /* program-start state of the persistent pool / patterns / candidates */
/* Generate a puzzle for `tier`. fix = 0: exact original generator (including its defects);
 * fix = 1: re-roll until the puzzle is unambiguous (open-questions D7, D8). */
void puzzle_generate(int tier, int fix, Puzzle *out);

#endif
