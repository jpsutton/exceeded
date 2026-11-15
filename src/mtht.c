#include "mtht.h"
#include "res.h"
#include "rng.h"

typedef struct Interp {
    const uint8_t *s;
    size_t slen;
    size_t P;
    uint8_t *buf;
    int bufsize;
    int W;
    int qnum;
    MthtVar *var;
    int nvar;
    int excl[4096];
    int nexcl;
} Interp;

int mtht_rand(int a, int b) { return rngb_range(a, b); }
int mtht_rand_step(int step, int lo, int hi) {
    int n = (hi - lo + 1) / step;
    int base = (lo + step - 1) / step;
    return (mtht_rand(0, n) + base) * step;
}

static int sb(Interp *I, size_t at) { return at < I->slen ? I->s[at] : 0xFF; }
static int peek(Interp *I) { return sb(I, I->P); }
static int next(Interp *I) { int b = sb(I, I->P); if (I->P < I->slen) I->P++; return b; }
static int is_lparen(Interp *I) { return sb(I, I->P) == 0x10 && sb(I, I->P + 1) == '('; }

static int16_t rdw(const Interp *I, int addr) {
    if (addr < 0 || addr + 1 >= I->bufsize) return 0;
    return (int16_t)(I->buf[addr] | (I->buf[addr + 1] << 8));
}
static void wrw(Interp *I, int addr, int v) {
    if (addr < 0 || addr + 1 >= I->bufsize) return;
    I->buf[addr] = (uint8_t)v;
    I->buf[addr + 1] = (uint8_t)(v >> 8);
}
static void put(Interp *I, int b) {
    if (I->W >= 0 && I->W < I->bufsize) I->buf[I->W] = (uint8_t)b;
    I->W++;
}
static void emit(Interp *I, int v) { put(I, v & 0xFF); put(I, (v >> 8) & 0xFF); }

static int ref_addr(Interp *I);
static int item(Interp *I) {
    if (is_lparen(I)) return rdw(I, ref_addr(I));
    int b = next(I);
    if (b == 0x10) return 16;     /* \x10 followed by anything but ( or ) is the number 16 */
    return (int8_t)b;
}
/* REF = \x10( var idx \x10) ; returns buffer address or -1 */
static int ref_addr(Interp *I) {
    if (is_lparen(I)) I->P += 2;
    int v = item(I);
    int idx = item(I);
    if (sb(I, I->P) == 0x10 && sb(I, I->P + 1) == ')') I->P += 2;
    if (v < 1 || v > I->nvar) return -1;
    return I->var[v - 1].offset + idx * I->var[v - 1].size;
}
static int value(Interp *I) {
    if (is_lparen(I)) return rdw(I, ref_addr(I));
    int lo = next(I), hi = next(I);
    return (int16_t)(lo | (hi << 8));
}
static void skip_ff(Interp *I) {
    while (I->P < I->slen) if (I->s[I->P++] == 0xFF) break;
}
static const char *str_at(Interp *I, int addr, int *len) {
    if (addr < 0 || addr >= I->bufsize) { *len = 0; return ""; }
    int n = 0;
    while (addr + n < I->bufsize && I->buf[addr + n]) n++;
    *len = n;
    return (const char *)I->buf + addr;
}
static void copy_str(Interp *I, int addr, int with_nul) {
    int n;
    const char *p = str_at(I, addr, &n);
    for (int i = 0; i < n; i++) put(I, (uint8_t)p[i]);
    if (with_nul) put(I, 0);
}
/* read a literal NUL-terminated string from the stream, returning start index and length */
static size_t read_cstr(Interp *I, int *len) {
    size_t st = I->P;
    int n = 0;
    while (I->P < I->slen && I->s[I->P] != 0) { I->P++; n++; }
    if (I->P < I->slen) I->P++;
    *len = n;
    return st;
}

static int in_excl(Interp *I, int v) {
    for (int i = 0; i < I->nexcl; i++) if (I->excl[i] == v) return 1;
    return 0;
}
static void add_excl(Interp *I, int v) {
    if (!in_excl(I, v) && I->nexcl < 4096) I->excl[I->nexcl++] = v;
}

static void skip_group(Interp *I) {
    /* at \x10* : skip to just after the matching \x10+ */
    int depth = 0;
    while (I->P < I->slen) {
        int b = I->s[I->P];
        if (b == 0x10 && I->P + 1 < I->slen) {
            int c = I->s[I->P + 1];
            if (c == '*') { depth++; I->P += 2; continue; }
            if (c == '+') { depth--; I->P += 2; if (depth <= 0) return; continue; }
        }
        I->P++;
    }
}

/* Execute opcode `op` (already consumed). Opcodes that produce a number either emit it (line level)
 * or return it (in_expr). Returns nonzero if the opcode yielded a value in *val. */
static int exec_op(Interp *I, int op, int *val);

static int eval_expr(Interp *I) {
    int stack[64], sp = 0;
    for (;;) {
        if (I->P >= I->slen) break;
        int b = peek(I);
        if (b == 0xFF) { I->P++; break; }
        int v = 0, have = 0;
        switch (b) {
        case 0xDF: case 0xE1: case 0xE2: case 0xE9: case 0xEA: case 0xEB: case 0xF2: case 0xF5: case 0xF6:
            I->P++;
            have = exec_op(I, b, &v);
            if (have && sp < 64) stack[sp++] = v;
            continue;
        case 0x10: {
            int c = sb(I, I->P + 1);
            if (c >= 0x2C && c <= 0x2F) {
                I->P += 2;
                if (sp >= 2) {
                    int a = stack[--sp], bb = stack[--sp], r = 0;
                    switch (c) {
                    case 0x2C: r = bb + a; break;
                    case 0x2D: r = bb - a; break;
                    case 0x2E: r = bb * a; break;
                    case 0x2F: r = a ? bb / a : 0; break;
                    }
                    stack[sp++] = (int16_t)r;
                }
                continue;
            }
            break;
        }
        default: break;
        }
        /* literal int16 */
        int lo = next(I), hi = next(I);
        if (sp < 64) stack[sp++] = (int16_t)(lo | (hi << 8));
    }
    return sp ? stack[sp - 1] : 0;
}

static void exec_group(Interp *I, int n, int k) {
    /* n+1 groups follow; run group k only */
    for (int i = 0; i <= n; i++) {
        if (peek(I) == 0x10 && sb(I, I->P + 1) == '*') {
            size_t start = I->P;
            skip_group(I);
            size_t end = I->P;
            if (i == k) {
                I->P = start + 2;
                int v;
                int op = next(I);
                if (op >= 0x80) { if (exec_op(I, op, &v)) emit(I, v); }
                I->P = end;
            }
        } else break;
    }
    skip_ff(I);
}

static int exec_op(Interp *I, int op, int *val) {
    switch (op) {
    case 0xDF: {  /* word */
        int lo = next(I), hi = next(I);
        skip_ff(I);
        *val = (int16_t)(lo | (hi << 8));
        return 1;
    }
    case 0xE0: {
        if (is_lparen(I)) { int a = ref_addr(I); copy_str(I, a, 1); }
        else { int n; size_t st = read_cstr(I, &n); for (int i = 0; i < n; i++) put(I, I->s[st + i]); put(I, 0); }
        int lo = next(I), hi = next(I);
        emit(I, (int16_t)(lo | (hi << 8)));
        skip_ff(I);
        return 0;
    }
    case 0xE1: {
        int a = ref_addr(I);
        int n = value(I);
        int sum = 0;
        for (int i = 0; i < n; i++) sum += rdw(I, a + 2 * i);
        skip_ff(I);
        *val = (int16_t)sum;
        return 1;
    }
    case 0xE2: *val = (int16_t)eval_expr(I); return 1;
    case 0xE3: skip_ff(I); *val = I->qnum; return 1;
    case 0xE4: return 0;
    case 0xE5: {
        int a = ref_addr(I);
        char t[16];
        snprintf(t, sizeof t, "%d", rdw(I, a));
        for (char *c = t; *c; c++) put(I, (uint8_t)*c);
        skip_ff(I);
        return 0;
    }
    case 0xE6: {
        int a = ref_addr(I);
        copy_str(I, a, 0);
        skip_ff(I);
        return 0;
    }
    case 0xE7: case 0xE8: case 0xE9: case 0xEA: {
        int n = next(I);
        int best = (op == 0xE7 || op == 0xE9) ? 9999 : 0, bi = 0;
        for (int i = 0; i < n; i++) {
            int a = ref_addr(I);
            int v = rdw(I, a);
            if (op == 0xE7 || op == 0xE9) { if (v < best) { best = v; bi = i; } }
            else { if (v > best) { best = v; bi = i; } }
        }
        skip_ff(I);
        *val = (op == 0xE7 || op == 0xE8) ? bi : best;
        return 1;
    }
    case 0xEB: {
        int a = ref_addr(I);
        skip_ff(I);
        *val = rdw(I, a);
        return 1;
    }
    case 0xEC: {
        int v = value(I);
        add_excl(I, v);
        skip_ff(I);
        return 0;
    }
    case 0xED: {
        int d = ref_addr(I), s = ref_addr(I);
        int n;
        const char *p = str_at(I, s, &n);
        char tmp[512];
        if (n > 510) n = 510;
        memcpy(tmp, p, (size_t)n);
        for (int i = 0; i < n; i++) if (d + i >= 0 && d + i < I->bufsize) I->buf[d + i] = (uint8_t)tmp[i];
        if (d + n >= 0 && d + n < I->bufsize) I->buf[d + n] = 0;
        skip_ff(I);
        return 0;
    }
    case 0xEE: {
        int d = ref_addr(I);
        int n;
        size_t st = read_cstr(I, &n);
        for (int i = 0; i < n; i++) if (d + i >= 0 && d + i < I->bufsize) I->buf[d + i] = I->s[st + i];
        if (d + n >= 0 && d + n < I->bufsize) I->buf[d + n] = 0;
        skip_ff(I);
        return 0;
    }
    case 0xEF: {
        int d = ref_addr(I);
        int v = value(I);
        wrw(I, d, v);
        skip_ff(I);
        return 0;
    }
    case 0xF0: {
        int a = ref_addr(I);
        int k = rdw(I, a);
        int n = next(I);
        exec_group(I, n, k);
        return 0;
    }
    case 0xF1: {
        int a = ref_addr(I);
        int k = rdw(I, a), i = 0;
        while (I->P < I->slen && peek(I) != 0xFF) {
            int n;
            size_t st = read_cstr(I, &n);
            if (i == k) { for (int j = 0; j < n; j++) put(I, I->s[st + j]); put(I, 0); }
            i++;
        }
        skip_ff(I);
        return 0;
    }
    case 0xF2: {
        int a = ref_addr(I);
        int k = rdw(I, a), i = 0, r = 0;
        while (I->P < I->slen && peek(I) != 0xFF) {
            int lo = next(I), hi = next(I);
            if (i == k) r = (int16_t)(lo | (hi << 8));
            i++;
        }
        skip_ff(I);
        *val = r;
        return 1;
    }
    case 0xF3: {
        int n = next(I);
        int k = mtht_rand(0, n);
        exec_group(I, n, k);
        return 0;
    }
    case 0xF4: {
        int n = next(I);
        int k = mtht_rand(0, n);
        for (int i = 0; i <= n; i++) {
            int l;
            size_t st = read_cstr(I, &l);
            if (i == k) { for (int j = 0; j < l; j++) put(I, I->s[st + j]); put(I, 0); }
        }
        skip_ff(I);
        return 0;
    }
    case 0xF5: {
        int n = next(I);
        int k = mtht_rand(0, n), r = 0;
        for (int i = 0; i <= n; i++) {
            int lo = next(I), hi = next(I);
            if (i == k) r = (int16_t)(lo | (hi << 8));
        }
        skip_ff(I);
        *val = r;
        return 1;
    }
    case 0xF6: {
        int lo = value(I), hi = value(I);
        if (lo > hi) { int t = lo; lo = hi; hi = t; }
        int step = 0, mode = 0;
        while (I->P < I->slen) {
            int b = next(I);
            if (b == 0xFF) break;
            if (b == 0xCA) mode = 1;
            else if (b == 0xCB) mode = 2;
            else if (b == 0xCC) { int a = ref_addr(I); int v = rdw(I, a); if (v < hi) hi = v; }
            else if (b == 0xCD) { int a = ref_addr(I); int v = rdw(I, a); if (v > lo) lo = v; }
            else if (b == 0xCE) step = value(I);
        }
        int v;
        if (mode == 1) I->nexcl = 0;
        if (mode == 2) {
            int guard = 0;
            do { v = step ? mtht_rand_step(step, lo, hi) : mtht_rand(lo, hi); } while (in_excl(I, v) && ++guard < 10000);
        } else v = step ? mtht_rand_step(step, lo, hi) : mtht_rand(lo, hi);
        add_excl(I, v);
        *val = (int16_t)v;
        return 1;
    }
    default: break;
    }
    return 0;
}

int mtht_run(int id, int seed, int qnum, MthtInst *out) {
    memset(out, 0, sizeof *out);
    size_t len;
    const uint8_t *d = res_get("MTHT", id, &len);
    if (!d || len < 2) return -1;
    int n = rd16(d);
    size_t p = 2;
    if (p + (size_t)n * 6 + 8 > len) return -1;
    out->id = id; out->seed = seed; out->qnum = qnum; out->nvar = n;
    out->var = xcalloc((size_t)(n ? n : 1), sizeof(MthtVar));
    int bufsize = 0;
    for (int i = 0; i < n; i++, p += 6) {
        out->var[i].count = rd16(d + p);
        out->var[i].size = rd16(d + p + 2);
        out->var[i].offset = rd16(d + p + 4);
        int e = out->var[i].offset + out->var[i].count * out->var[i].size;
        if (e > bufsize) bufsize = e;
    }
    out->answer_var = rd16(d + p);
    out->question_var = rd16(d + p + 2);
    out->hint_var = rd16(d + p + 4);
    out->title_var = rd16(d + p + 6);
    p += 8;
    out->bufsize = bufsize;
    out->buf = xcalloc((size_t)(bufsize ? bufsize : 1), 1);

    Interp *I = xcalloc(1, sizeof *I);
    I->s = d + p; I->slen = len - p; I->buf = out->buf; I->bufsize = bufsize;
    I->qnum = qnum; I->var = out->var; I->nvar = n;
    rngb_seed((uint16_t)seed);
    for (int v = 0; v < n; v++) {
        for (int e = 0; e < out->var[v].count; e++) {
            I->W = out->var[v].offset + e * out->var[v].size;
            for (;;) {
                if (I->P >= I->slen) break;
                int b = next(I);
                if (b == 0x0A) break;
                if (b >= 0x80) {
                    int val;
                    if (exec_op(I, b, &val)) emit(I, val);
                } else put(I, b);
            }
        }
    }
    free(I);

    /* outputs (spec 06 section 5) */
    if (out->answer_var < n) out->answer = rdw(&(Interp){.buf = out->buf, .bufsize = bufsize}, out->var[out->answer_var].offset);
    if (out->question_var < n) {
        MthtVar *q = &out->var[out->question_var];
        int cnt = rdw(&(Interp){.buf = out->buf, .bufsize = bufsize}, q->offset);
        size_t ql = 0;
        for (int i = 1; i <= cnt && i < q->count; i++) {
            const char *s = (const char *)out->buf + q->offset + i * q->size;
            if (q->offset + i * q->size >= bufsize) break;
            size_t l = strnlen(s, (size_t)(bufsize - (q->offset + i * q->size)));
            if (ql + l >= sizeof out->question) l = sizeof out->question - 1 - ql;
            memcpy(out->question + ql, s, l);
            ql += l;
        }
        out->question[ql] = 0;
    }
    if (out->hint_var < n) {
        int o = out->var[out->hint_var].offset;
        size_t l = strnlen((const char *)out->buf + o, (size_t)(bufsize - o));
        if (l >= sizeof out->hint) l = sizeof out->hint - 1;
        memcpy(out->hint, out->buf + o, l);
        out->hint[l] = 0;
    }
    return 0;
}

void mtht_free(MthtInst *m) {
    free(m->var);
    free(m->buf);
    memset(m, 0, sizeof *m);
}

/* flat element access */
int mtht_flat_addr(const MthtInst *m, int n) {
    if (n < 0) return -1;
    for (int v = 0; v < m->nvar; v++) {
        if (n < m->var[v].count) return m->var[v].offset + n * m->var[v].size;
        n -= m->var[v].count;
    }
    return -1;
}
int mtht_flat_word(const MthtInst *m, int n) {
    int a = mtht_flat_addr(m, n);
    if (a < 0 || a + 1 >= m->bufsize) return 0;
    return (int16_t)(m->buf[a] | (m->buf[a + 1] << 8));
}
const char *mtht_flat_str(const MthtInst *m, int n) {
    int a = mtht_flat_addr(m, n);
    if (a < 0 || a >= m->bufsize) return "";
    return (const char *)m->buf + a;
}
