#include "rules.h"

const char *const rank_names[8] = {"Trainee", "Junior", "Senior", "Pro", "Expert", "Master", "All-Star", "Champion"};
const uint32_t rank_thresholds[8] = {0, 5000, 30000, 60000, 120000, 200000, 300000, 500000};

int rank_for_score(uint32_t lifetime) {
    int r = 0;
    for (int i = 0; i < 8; i++) if (rank_thresholds[i] <= lifetime) r = i;
    return r;
}

const int tier_params[13][4] = {
    {300, 300, 600, 600}, {100, 100, 200, 200}, {10, 15, 30, 40}, {20, 25, 30, 35},
    {0, 30, 40, 50},      {1, 2, 2, 2},         {8, 8, 6, 4},     {20, 30, 40, 50},
    {1000, 2000, 3000, 5000}, {100, 150, 300, 400}, {25, 35, 50, 60}, {20, 30, 40, 60}, {2, 2, 2, 2},
};

#include "rng.h"

const uint8_t tier_cust[4][13] = {
    {0x1c, 0x04, 0x14, 0x04, 0x04, 0x04, 0, 0, 0, 0, 0, 0, 0},
    {0x1d, 0x0c, 0x1c, 0x04, 0x04, 0x04, 0x04, 0, 0x04, 0, 0x04, 0, 0},
    {0x1e, 0x14, 0x14, 0x14, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0, 0},
    {0x1f, 0x1c, 0x1c, 0x14, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
};

/* ------------------------------------------------------------------ drill */
void deck_build(DrillDeck *d, const uint8_t cust[13]) {
    d->n = 0;
    for (int op = 0; op < 4; op++) {
        if (!(cust[op] & 0x10)) continue;
        uint8_t used[13][13];
        memset(used, 0, sizeof used);
        for (int n = 0; n < 13; n++) {
            if (!(cust[n] & 0x04)) continue;
            for (int m = 0; m < 13; m++) {
                if (!used[n][m] && !(op == OP_DIV && n == 0)) {
                    d->f[d->n++] = (Fact){(uint8_t)op, (uint8_t)n, (uint8_t)m};
                    used[n][m] = 1;
                }
                if (!used[m][n] && !(op == OP_DIV && m == 0)) {
                    d->f[d->n++] = (Fact){(uint8_t)op, (uint8_t)m, (uint8_t)n};
                    used[m][n] = 1;
                }
            }
        }
    }
    d->remaining = d->n;
}
void deck_reset_remaining(DrillDeck *d) { d->remaining = d->n; }

Fact deck_draw(DrillDeck *d) {
    if (d->n == 0) return (Fact){0, 0, 0};
    if (d->remaining == 0) d->remaining = d->n;
    int j = rnga_scaled(d->remaining);
    Fact t = d->f[j];
    d->f[j] = d->f[d->remaining - 1];
    d->f[d->remaining - 1] = t;
    d->remaining--;
    return d->f[d->remaining];
}

void question_make(Question *q, DrillDeck *d, const uint8_t cust[13]) {
    q->fact = deck_draw(d);
    int k;
    int guard = 0;
    do { k = rnga_scaled(3); } while (!(cust[k] & 0x08) && ++guard < 1000);
    q->format = k;
    int lo = q->fact.lo, hi = q->fact.hi, a, b, c;
    switch (q->fact.op) {
    case OP_ADD: a = hi; b = lo; c = hi + lo; break;
    case OP_MUL: a = hi; b = lo; c = hi * lo; break;
    case OP_SUB: a = lo + hi; b = lo; c = hi; break;
    default: a = lo * hi; b = lo; c = hi; break;
    }
    int ans = k == 0 ? c : (k == 1 ? a : b);
    if (q->fact.op == OP_DIV && k == 2 && a == 0) {   /* 0 / ? = 0  ->  lo / ? = 1 */
        a = lo; c = 1; ans = lo;
    }
    q->a = a; q->b = b; q->c = c; q->answer = ans;
    static const char opch[4] = {'+', 'x', '-', '/'};
    char sa[8], sb[8], sc[8];
    snprintf(sa, sizeof sa, "%d", a); snprintf(sb, sizeof sb, "%d", b); snprintf(sc, sizeof sc, "%d", c);
    snprintf(q->display, sizeof q->display, "%s %c %s = %s", k == 1 ? "?" : sa, opch[q->fact.op], k == 2 ? "?" : sb,
             k == 0 ? "?" : sc);
}

int question_check(const Question *q, int entry) {
    if (q->fact.op == OP_MUL) {
        if (q->format == 1 && q->b == 0) return 1;   /* ? x 0 = 0 */
        if (q->format == 2 && q->a == 0) return 1;   /* 0 x ? = 0 */
    }
    return entry == q->answer;
}

/* ----------------------------------------------------------------- puzzle */
static const int PATTERN_INIT[10][4] = {
    {0, 0, 1, 1}, {0, 0, 1, 2}, {0, 1, 0, 1}, {0, 1, 0, 2}, {0, 1, 1, 0},
    {0, 1, 1, 2}, {0, 1, 2, 0}, {0, 1, 2, 1}, {0, 1, 2, 2}, {0, 1, 2, 3},
};
static int perm[24][4];
static int perm_ready;
static int pool[20];
static int patterns[10][4];
static uint8_t cand[5][4];
static uint8_t code_keep[4];

static void perm_init(void) {
    int n = 0;
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 4; c++)
                for (int d = 0; d < 4; d++) {
                    if (a == b || a == c || a == d || b == c || b == d || c == d) continue;
                    perm[n][0] = a; perm[n][1] = b; perm[n][2] = c; perm[n][3] = d;
                    n++;
                }
    perm_ready = 1;
}

/* Clear the secret code (part of the game-state block, zeroed at sign-in; kept across play-again games). */
void puzzle_clear_code(void) { memset(code_keep, 0, sizeof code_keep); }

void puzzle_reset(void) {
    if (!perm_ready) perm_init();
    for (int i = 0; i < 20; i++) pool[i] = i;
    memcpy(patterns, PATTERN_INIT, sizeof patterns);
    memset(cand, 0, sizeof cand);
    memset(code_keep, 0, sizeof code_keep);
}

static void swap_int(int *a, int *b) { int t = *a; *a = *b; *b = t; }
static void swap_u8(uint8_t *a, uint8_t *b) { uint8_t t = *a; *a = *b; *b = t; }

static void draw3(int out[3]) {
    int n = 20;
    for (int i = 0; i < 3; i++) {
        int j = rnga_scaled(n);
        swap_int(&pool[j], &pool[n - 1]);
        out[i] = pool[n - 1];
        n--;
    }
}

static void gen_once(int tier, Puzzle *out) {
    if (!perm_ready) perm_init();
    uint8_t *code = code_keep;
    /* step 1 */
    for (int n = 20; n >= 1; n--) { int j = rnga_scaled(n); swap_int(&pool[j], &pool[n - 1]); }
    /* step 2 */
    if (tier == 0) {
        for (int i = 0; i < 4; i++) code[i] = (uint8_t)pool[16 + i];
        for (int c = 0; c < 5; c++) memcpy(cand[c], code, 4);
        static const int ch[4][2] = {{1, 1}, {2, 2}, {3, 2}, {4, 3}};
        for (int t = 0; t < 4; t++) {
            int c = ch[t][0], changes = ch[t][1];
            int pos[4] = {0, 1, 2, 3}, n = 4;
            for (int r = 0; r < changes; r++) {
                int j = rnga_scaled(n);
                swap_int(&pos[j], &pos[n - 1]);
                int k = rnga_scaled(16);
                cand[c][pos[n - 1]] = (uint8_t)pool[k];
                n--;
            }
        }
    } else if (tier == 1) {
        int pics[4];
        for (int i = 0; i < 4; i++) pics[i] = pool[16 + i];
        int d;
        do { d = rnga_scaled(24); } while (d < 2 || d > 21);
        for (int i = 0; i < 4; i++) {
            code[i] = (uint8_t)pics[perm[d][i]];
            cand[0][i] = code[i];
            cand[1][i] = (uint8_t)pics[perm[d - 2][i]];
            cand[2][i] = (uint8_t)pics[perm[d - 1][i]];
            cand[3][i] = (uint8_t)pics[perm[d + 1][i]];
            cand[4][i] = (uint8_t)pics[perm[d + 2][i]];
        }
    } else if (tier == 2) {
        for (int i = 0; i < 4; i++) {
            code[i] = (uint8_t)pool[10 + i];
            cand[0][i] = (uint8_t)pool[10 + i];
            cand[1][i] = (uint8_t)pool[9 + i];
            cand[2][i] = (uint8_t)pool[11 + i];
            cand[3][i] = (uint8_t)pool[12 + i];
        }
        cand[4][0] = (uint8_t)pool[10]; cand[4][1] = (uint8_t)pool[9];
        cand[4][2] = (uint8_t)pool[14]; cand[4][3] = (uint8_t)pool[13];
        for (int c = 0; c < 5; c++)
            for (int n = 4; n >= 1; n--) { int j = rnga_scaled(n); swap_u8(&cand[c][j], &cand[c][n - 1]); }
    } else {
        int k = rnga_scaled(9);
        int pat[4];
        memcpy(pat, patterns[k], sizeof pat);
        { int t[4]; memcpy(t, patterns[k], sizeof t); memcpy(patterns[k], patterns[9], sizeof t); memcpy(patterns[9], t, sizeof t); }
        int picks[3];
        draw3(picks);
        for (int v = 0; v < 3; v++) for (int i = 0; i < 4; i++) if (pat[i] == v) code[i] = (uint8_t)picks[v];
        draw3(picks);
        for (int v = 0; v < 3; v++) for (int i = 0; i < 4; i++) if (pat[i] == v) cand[0][i] = (uint8_t)picks[v];
        for (int c = 1; c < 5; c++) {
            int kk = rnga_scaled(8);
            int p[4];
            memcpy(p, patterns[kk], sizeof p);
            draw3(picks);
            for (int v = 0; v < 3; v++) for (int i = 0; i < 4; i++) if (p[i] == v) cand[c][i] = (uint8_t)picks[v];
        }
    }
    /* step 4: deal candidates to rooms */
    int order[5] = {0, 1, 2, 3, 4}, n = 5;
    for (int r = 0; r < 5; r++) { int j = rnga_scaled(n); swap_int(&order[j], &order[n - 1]); n--; }
    memcpy(out->code, code, 4);
    out->hideout = 0;
    for (int r = 0; r < 5; r++) {
        memcpy(out->room[r], cand[order[r]], 4);
        if (order[r] == 0) out->hideout = r;
    }
    out->rule = tier == 0 ? 0 : tier == 1 ? 0 : tier == 2 ? 1 : 2;
}

static void shape(const uint8_t *p, int out[4]) {
    for (int i = 0; i < 4; i++) {
        out[i] = -1;
        for (int j = 0; j < i; j++) if (p[j] == p[i]) { out[i] = out[j]; break; }
        if (out[i] < 0) { int m = -1; for (int j = 0; j < i; j++) if (out[j] > m) m = out[j]; out[i] = m + 1; }
    }
}

static int puzzle_ok(int tier, const Puzzle *p) {
    if (tier == 2) return memcmp(p->room[p->hideout], p->code, 4) != 0;
    if (tier == 3) {
        int cs[4], rs[4], matches = 0;
        shape(p->code, cs);
        for (int r = 0; r < 5; r++) {
            shape(p->room[r], rs);
            if (memcmp(cs, rs, sizeof cs) == 0) { matches++; if (r != p->hideout) return 0; }
        }
        if (matches != 1) return 0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) if (p->room[p->hideout][i] == p->code[j]) return 0;
    }
    return 1;
}

void puzzle_generate(int tier, int fix, Puzzle *out) {
    if (!perm_ready) puzzle_reset();
    gen_once(tier, out);
    if (!fix) return;
    for (int tries = 0; tries < 1000 && !puzzle_ok(tier, out); tries++) gen_once(tier, out);
}
