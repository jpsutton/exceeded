/* dosaudio.c - DOS music driver model, file loading and synthesis (spec chapter 12) */
#include "dosaudio.h"
#include "res.h"
#include "vfs.h"
#include "util.h"
#include <math.h>
#include <strings.h>

static const uint16_t SPK_SIG[12] = {0x8E88, 0x8689, 0x7EFC, 0x77DB, 0x7121, 0x6AC8, 0x64C9, 0x5F21, 0x59CB, 0x54C0, 0x4FFF, 0x4B81};
static const uint16_t TANDY_SIG[12] = {0x06B8, 0x0658, 0x05FD, 0x05A7, 0x0555, 0x0509, 0x04C0, 0x047C, 0x043C, 0x03FF, 0x03C5, 0x038F};

static void seterr(char *e, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static void seterr(char *e, size_t n, const char *fmt, ...) {
    if (!e || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e, n, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ SHA-256 */
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25), ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22), mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
void sha256(const uint8_t *d, size_t n, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha_block(h, d + i);
    uint8_t t[128];
    size_t r = n - i;
    memset(t, 0, sizeof t);
    memcpy(t, d + i, r);
    t[r] = 0x80;
    size_t tl = r + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) t[tl - 1 - k] = (uint8_t)(bits >> (8 * k));
    sha_block(h, t);
    if (tl == 128) sha_block(h, t + 64);
    for (int k = 0; k < 8; k++) { out[k * 4] = (uint8_t)(h[k] >> 24); out[k * 4 + 1] = (uint8_t)(h[k] >> 16); out[k * 4 + 2] = (uint8_t)(h[k] >> 8); out[k * 4 + 3] = (uint8_t)h[k]; }
}

/* ------------------------------------------------------------------ EXEPACK (section 2.1-2.2) */
int dos_unpack_exe(const uint8_t *f, size_t n, uint8_t **out, size_t *outn, char *err, size_t errn) {
    *out = NULL; *outn = 0;
    if (n < 28 || f[0] != 'M' || f[1] != 'Z') { seterr(err, errn, "not an MZ executable"); return -1; }
    unsigned cblp = rd16(f + 2), cp = rd16(f + 4), cparhdr = rd16(f + 8), cs = rd16(f + 22);
    size_t isz = (size_t)cp * 512 - (cblp ? 512 - cblp : 0);
    if (isz > n) isz = n;
    size_t hdr = (size_t)cparhdr * 16;
    if (hdr > isz) { seterr(err, errn, "bad MZ header size"); return -1; }
    const uint8_t *li = f + hdr;
    size_t lin = isz - hdr;
    size_t eh = (size_t)cs * 16;
    int skip_len;
    if (eh + 18 <= lin && li[eh + 16] == 'R' && li[eh + 17] == 'B') skip_len = rd16(li + eh + 14);
    else if (eh + 16 <= lin && li[eh + 14] == 'R' && li[eh + 15] == 'B') skip_len = 1;
    else { seterr(err, errn, "no EXEPACK signature"); return -1; }
    size_t dest_len = (size_t)rd16(li + eh + 12) * 16;
    long srcn = (long)eh - (long)(skip_len - 1) * 16;
    if (skip_len < 1 || srcn <= 0 || (size_t)srcn > lin) { seterr(err, errn, "bad EXEPACK skip length"); return -1; }
    if ((size_t)srcn > dest_len) { seterr(err, errn, "EXEPACK compressed area larger than output"); return -1; }
    uint8_t *o = xcalloc(dest_len ? dest_len : 1, 1);
    memcpy(o, li, (size_t)srcn);
    const uint8_t *src = li;
    long s = srcn - 1;
    for (int k = 0; k < 16 && s >= 0 && src[s] == 0xFF; k++) s--;
    long d = (long)dest_len;
    for (;;) {
        if (s < 2) { seterr(err, errn, "EXEPACK data ends early"); free(o); return -1; }
        int cmd = src[s];
        long len = src[s - 1] * 256 + src[s - 2];
        s -= 3;
        if ((cmd & 0xFE) == 0xB0) {
            if (s < 0) { seterr(err, errn, "EXEPACK data ends early"); free(o); return -1; }
            uint8_t v = src[s--];
            if (len > d) { seterr(err, errn, "EXEPACK output overflow"); free(o); return -1; }
            while (len--) o[--d] = v;
        } else if ((cmd & 0xFE) == 0xB2) {
            if (len > d || len > s + 1) { seterr(err, errn, "EXEPACK copy out of range"); free(o); return -1; }
            while (len--) o[--d] = src[s--];
        } else { seterr(err, errn, "bad EXEPACK command"); free(o); return -1; }
        if (cmd & 1) break;
    }
    *out = o;
    *outn = dest_len;
    return 0;
}

/* ------------------------------------------------------------------ table location and validation */
static int check_tables(const uint8_t *img, size_t n, long ds, char *err, size_t errn) {
    if (ds < 0 || (size_t)(ds + 0x4506 + DOS_SONGS * 2) > n || (size_t)(ds + 0x456C + DOS_ENVS * 2) > n) { seterr(err, errn, "driver tables not found"); return -1; }
    for (int i = 0; i < 12; i++) {
        if (rd16(img + ds + 0x44D2 + i * 2) != SPK_SIG[i]) { seterr(err, errn, "PC-speaker divisor table mismatch"); return -1; }
        if (rd16(img + ds + 0x44EA + i * 2) != TANDY_SIG[i]) { seterr(err, errn, "Tandy divisor table mismatch"); return -1; }
    }
    if (rd16(img + ds + 0x4502) != 0x44D2 || rd16(img + ds + 0x4504) != 0x44EA) { seterr(err, errn, "E-event base pointers wrong"); return -1; }
    for (int i = 0; i < DOS_ENVS; i++) {
        long p = ds + rd16(img + ds + 0x456C + i * 2);
        if (p < 0 || (size_t)p >= n) { seterr(err, errn, "envelope %d pointer outside image", i); return -1; }
        int ok = 0;
        for (int k = 0; k < 1024 && (size_t)(p + k) < n; k++) if (img[p + k] == 0xFF) { ok = 1; break; }
        if (!ok) { seterr(err, errn, "envelope %d has no terminator", i); return -1; }
    }
    for (int i = 0; i < DOS_SONGS - 1; i++) {
        int pr = rds16(img + ds + 0x4506 + i * 2);
        if (pr < 0 || pr > 255) { seterr(err, errn, "song %d priority out of range", i); return -1; }
    }
    return 0;
}

static int locate_tables(DosGame *g, char *err, size_t errn) {
    char e1[160] = "";
    if (check_tables(g->img, g->img_n, 0x13F30, e1, sizeof e1) == 0) { g->ds = 0x13F30; g->scanned = 0; return 0; }
    uint8_t sig[48];
    for (int i = 0; i < 12; i++) { sig[i * 2] = (uint8_t)(SPK_SIG[i] & 255); sig[i * 2 + 1] = (uint8_t)(SPK_SIG[i] >> 8); sig[24 + i * 2] = (uint8_t)(TANDY_SIG[i] & 255); sig[24 + i * 2 + 1] = (uint8_t)(TANDY_SIG[i] >> 8); }
    char e2[160] = "";
    int hits = 0;
    for (size_t p = 0; p + 48 <= g->img_n; p++) {
        if (g->img[p] != sig[0] || memcmp(g->img + p, sig, 48)) continue;
        hits++;
        long ds = (long)p - 0x44D2;
        if (check_tables(g->img, g->img_n, ds, e2, sizeof e2) == 0) { g->ds = ds; g->scanned = 1; return 0; }
    }
    if (hits) seterr(err, errn, "%s", e2);
    else if (!strstr(e1, "not found") && !strstr(e1, "outside")) seterr(err, errn, "driver tables not found");
    else seterr(err, errn, "driver tables not found");
    return -1;
}

static int parse_mocket(DosGame *g, char *err, size_t errn) {
    const uint8_t *m = g->moc;
    size_t n = g->moc_n;
    if (n < 2) { seterr(err, errn, "MOCKET.DAT too small"); return -1; }
    unsigned cnt = rd16(m);
    if (2 + (size_t)cnt * 6 > n || cnt < 1) { seterr(err, errn, "MOCKET.DAT directory truncated"); return -1; }
    if (rd16(m + 2 + (cnt - 1) * 6) != 0xFFFE) { seterr(err, errn, "MOCKET.DAT has no end marker"); return -1; }
    uint32_t prev = 0;
    for (unsigned i = 0; i < cnt; i++) {
        uint32_t off = rd32(m + 2 + i * 6 + 2);
        if (off > n || off < prev) { seterr(err, errn, "MOCKET.DAT offsets invalid"); return -1; }
        prev = off;
    }
    static const unsigned ids[2] = {0x15E0, 0x15E1};
    for (int b = 0; b < 2; b++) {
        int found = 0;
        for (unsigned i = 0; i < cnt; i++) {
            if (rd16(m + 2 + i * 6) != ids[b]) continue;
            uint32_t off = rd32(m + 2 + i * 6 + 2);
            size_t end = n;
            for (unsigned k = 0; k < cnt; k++) { uint32_t o2 = rd32(m + 2 + k * 6 + 2); if (o2 > off && o2 < end) end = o2; }
            g->blk[b].off = off;
            g->blk[b].size = end - off;
            found = 1;
            break;
        }
        if (!found || g->blk[b].size < 2) { seterr(err, errn, "MOCKET.DAT block 0x%X missing", ids[b]); return -1; }
    }
    for (int b = 0; b < 2; b++) {
        const uint8_t *bk = m + g->blk[b].off;
        size_t sz = g->blk[b].size;
        int es = b == DOS_TANDY ? 8 : 2;
        unsigned w0 = rd16(bk);
        const char *nm = b == DOS_TANDY ? "Tandy" : "speaker";
        if (w0 % (unsigned)es || w0 / (unsigned)es < DOS_SONGS) { seterr(err, errn, "%s music block song table too short", nm); return -1; }
        g->blk[b].entries = (int)(w0 / (unsigned)es);
        for (int s = 0; s < DOS_SONGS; s++)
            for (int c = 0; c < (b == DOS_TANDY ? 4 : 1); c++) {
                unsigned off = rd16(bk + (size_t)s * es + c * 2);
                size_t p = off;
                int ok = 0;
                if (p + 2 <= sz) {
                    for (; p + 2 <= sz; p += 2) if ((bk[p] & 0x0F) == 0x0F) { ok = 1; break; }
                }
                if (!ok) { seterr(err, errn, "%s song %d channel %d has no end", nm, s, c); return -1; }
            }
    }
    return 0;
}

void dos_game_free(DosGame *g) {
    free(g->img); free(g->moc);
    memset(g, 0, sizeof *g);
}

/* validate and index the two files (spec 12 section 2.6 items 2-8). 0 = ok, 2 = invalid (err filled). */
static int dos_game_build(DosGame *g, const uint8_t *eb, size_t en, const uint8_t *mb, size_t mn, char *err, size_t errn) {
    if (dos_unpack_exe(eb, en, &g->img, &g->img_n, err, errn) != 0) return 2;
    g->moc = xmalloc(mn + 1);
    memcpy(g->moc, mb, mn);
    g->moc_n = mn;
    if (locate_tables(g, err, errn) != 0) return 2;
    for (int i = 0; i < 12; i++) { g->spk[i] = rd16(g->img + g->ds + 0x44D2 + i * 2); g->tandy[i] = rd16(g->img + g->ds + 0x44EA + i * 2); }
    for (int i = 0; i < DOS_SONGS; i++) g->prio[i] = rds16(g->img + g->ds + 0x4506 + i * 2);
    for (int i = 0; i < DOS_ENVS; i++) g->envptr[i] = rd16(g->img + g->ds + 0x456C + i * 2);
    if (parse_mocket(g, err, errn) != 0) return 2;
    sha256(g->img, g->img_n, g->sha);
    return 0;
}

int dos_validate_bytes(const uint8_t *eb, size_t en, const uint8_t *mb, size_t mn, char *err, size_t errn) {
    DosGame g;
    memset(&g, 0, sizeof g);
    int rc = dos_game_build(&g, eb, en, mb, mn, err, errn);
    dos_game_free(&g);
    return rc;
}

int dos_game_load(const char *dir, DosGame *g, char *err, size_t errn) {
    memset(g, 0, sizeof *g);
    if (!dir || !*dir) { seterr(err, errn, "no DOS game folder set"); return 1; }
    snprintf(g->dir, sizeof g->dir, "%s", dir);
    VSource *vs;
    VErr ve;
    if (vs_open_dos(dir, &vs, &ve) != 0) {
        char msg[500];
        vs_format_error("dos", dir, &ve, msg, sizeof msg);
        seterr(err, errn, "%s", msg);
        return !strcmp(ve.code, "not_found") || !strcmp(ve.code, "dos_files_not_found") ? 1 : 2;
    }
    snprintf(g->inner, sizeof g->inner, "%s", vs_folder(vs));
    const uint8_t *eb = NULL, *mb = NULL;
    size_t en = 0, mn = 0;
    int ie = vs_find(vs, "SSO.EXE"), im = vs_find(vs, "MOCKET.DAT");
    int rc = 2;
    if (ie < 0 || im < 0 || vs_file_load(vs, ie, &eb, &en, &ve) != 0 || vs_file_load(vs, im, &mb, &mn, &ve) != 0) {
        char msg[500];
        if (ie < 0 || im < 0) snprintf(ve.code, sizeof ve.code, "dos_files_not_found");
        vs_format_error("dos", dir, &ve, msg, sizeof msg);
        seterr(err, errn, "%s", msg);
        rc = !strcmp(ve.code, "dos_files_not_found") ? 1 : 2;
    } else rc = dos_game_build(g, eb, en, mb, mn, err, errn);
    vs_close(vs);
    if (rc) { char keep[1024], keep2[1024]; snprintf(keep, sizeof keep, "%s", g->dir); snprintf(keep2, sizeof keep2, "%s", g->inner); dos_game_free(g); snprintf(g->dir, sizeof g->dir, "%s", keep); snprintf(g->inner, sizeof g->inner, "%s", keep2); }
    return rc;
}

int dos_game_probe(const char *dir, char *err, size_t errn) {
    DosGame g;
    int rc = dos_game_load(dir, &g, err, errn);
    if (rc == 0) dos_game_free(&g);
    return rc;
}

/* ------------------------------------------------------------------ driver */
const char *dos_result_text(int r) {
    switch (r) {
    case DOS_ACCEPTED: return "accepted";
    case DOS_IGN_PRIO: return "ignored (priority)";
    case DOS_IGN_MUSICOFF: return "ignored (music off)";
    default: return "ignored (sound off)";
    }
}

static void L(DosDrv *d, int ch, int kind, int v) { if (d->log) d->log(d->ud, ch, kind, v); }

void dosdrv_init(DosDrv *d, const DosGame *g, int variant) {
    DosLogFn lf = d->log;
    void *ud = d->ud;
    memset(d, 0, sizeof *d);
    d->log = lf; d->ud = ud;
    d->g = g;
    d->variant = variant;
    d->nch = variant == DOS_TANDY ? 4 : 1;
    d->state = DOS_IDLE;
    d->current = -1;
    d->sound_on = 1;
    for (int c = 0; c < 4; c++) {
        d->chip.att[c] = 15;
        d->c[c].env_sel = d->c[c].env_pos = g->envptr[0];
    }
}

static const uint8_t *blk_bytes(const DosDrv *d, size_t *sz) {
    const DosBlock *b = &d->g->blk[d->variant];
    *sz = b->size;
    return d->g->moc + b->off;
}

static void silence(DosDrv *d, int c, int logit) {
    if (d->variant == DOS_TANDY) {
        d->chip.att[c] = 15;
        d->chip.div[c] = 0;
        if (c == 3) d->chip.noise_serial++;
    } else d->chip.spk_on = 0;
    if (logit) L(d, c, DK_OFF, 0);
}

static void write_att(DosDrv *d, int c, int a) {
    if (d->chip.att[c] != (a & 15)) { d->chip.att[c] = a & 15; L(d, c, DK_V, a); }
}

static void write_tone(DosDrv *d, int c, int v) {
    if (d->variant == DOS_TANDY) {
        L(d, c, DK_F, v);
        d->chip.div[c] = v;
        if (c == 3) d->chip.noise_serial++;
    } else {
        L(d, c, DK_F, v);
        d->chip.spk_div = v;
        d->chip.spk_on = 1;
        L(d, c, DK_ON, 0);
    }
}

static void duration(DosDrv *d, DosChan *ch, int b1) {
    int len = ((d->base + d->adj) & 0xFFFF) >> (b1 & 7);
    if (b1 & 8) len = (len + (len >> 1)) & 0xFFFF;
    if (b1 & 0x10) {
        if (ch->tie == 3) {}
        else if (ch->tie == 1) ch->tie = 3;
        else ch->tie++;
    } else if (ch->tie != 0) ch->tie++;
    ch->cnt = len;
    ch->newnote = 1;
}

static void note(DosDrv *d, int c, int n, int o) {
    if (d->variant == DOS_TANDY) {
        if (c == 3) {
            L(d, c, DK_NOISE, n - 1);
            d->chip.div[3] = n - 1;
            d->chip.noise_serial++;
        } else {
            int sh = o == 0 ? 0 : o - 1;
            int v = d->g->tandy[n - 1] >> sh;
            if (v > 0x3FF) v >>= 1;
            write_tone(d, c, v);
        }
    } else {
        write_tone(d, c, d->g->spk[n - 1] >> o);
    }
}

static void e_event(DosDrv *d, int c, int o, int b1) {
    DosChan *ch = &d->c[c];
    if (d->variant == DOS_TANDY) {
        if (b1 == 0) { silence(d, c, 1); ch->rest = 1; }
        else {
            int x = ((3 * b1) >> 2) + 1;
            int sh = o == 0 ? 1 : o;
            int v = 0xD70 >> sh;
            if (sh < 4) x = (x << (4 - sh)) & 0xFFFF;
            if (sh > 4) x = x >> (sh - 4);
            v = (v - x) & 0xFFFF;
            if ((int16_t)v > 0x400) v >>= 1;
            write_tone(d, c, v);
            ch->newnote = 1;
            ch->rest = 0;
        }
    } else {
        if (b1 == 0) { silence(d, c, 1); ch->rest = 1; }
        else write_tone(d, c, ((d->g->spk[0] - 128 * b1) & 0xFFFF) >> o);
    }
    ch->cnt = ch->d3;
}

static void command(DosDrv *d, DosChan *ch, int o, int a) {
    switch (o) {
    case 0: if (a == 0) ch->nogate = 1; else { ch->nogate = 0; ch->gate = a; } break;
    case 1: case 2:
        if (a == 0) d->adj = 0;
        else {
            int q = d->base * a / 100, r = d->base * a % 100;
            if (r > 49) q++;
            d->adj = (o == 1) ? q : -q;
        }
        break;
    case 3: ch->d3 = a; break;
    case 4: d->base = a * 4; break;
    case 5: if (a < DOS_ENVS) ch->env_sel = d->g->envptr[a]; break;
    case 6: ch->sus = a; break;
    default: break;
    }
}

static void fetch(DosDrv *d, int c) {
    DosChan *ch = &d->c[c];
    size_t sz;
    const uint8_t *bk = blk_bytes(d, &sz);
    for (;;) {
        if (ch->ptr < 0 || (size_t)ch->ptr + 2 > sz) { ch->active = 0; silence(d, c, 1); return; }   /* ran off the block: treat as end */
        int b0 = bk[ch->ptr], b1 = bk[ch->ptr + 1];
        int n = b0 & 15, o = b0 >> 4;
        if (n == 0) { silence(d, c, 1); duration(d, ch, b1); ch->rest = 1; }
        else if (n == 0xD) command(d, ch, o, b1);
        else if (n == 0xE) e_event(d, c, o, b1);
        else if (n == 0xF) { ch->active = 0; silence(d, c, 1); return; }
        else { note(d, c, n, o); duration(d, ch, b1); ch->rest = 0; }
        ch->ptr += 2;
        if (ch->cnt != 0) return;
    }
}

static void init_song(DosDrv *d) {
    d->state = DOS_RUNNING;
    d->adj = 0;
    size_t sz;
    const uint8_t *bk = blk_bytes(d, &sz);
    for (int c = 0; c < d->nch; c++) {
        DosChan *ch = &d->c[c];
        ch->active = (ch->ptr >= 0 && (size_t)ch->ptr < sz) ? bk[ch->ptr] != 0xFF : 0;
        if (ch->active) { ch->cnt = 0; ch->d3 = 1; ch->nogate = 0; ch->rest = 0; ch->tie = 0; ch->gate = 6; }
    }
}

static int env_byte(const DosDrv *d, int pos) {
    long p = d->g->ds + pos;
    if (p < 0 || (size_t)p >= d->g->img_n) return 0xFF;
    return d->g->img[p];
}

static void volume_pass(DosDrv *d) {
    for (int c = 0; c < 4; c++) {
        DosChan *ch = &d->c[c];
        if (!ch->active) continue;
        int goto_env = 0;
        if (ch->newnote) {
            ch->newnote = 0;
            if (ch->tie >= 2) { if (ch->tie != 3) ch->tie = 0; }
            else { ch->env_done = 0; ch->env_pos = ch->env_sel; }
            goto_env = 1;
        }
        if (!goto_env) {
            if (ch->rest == 1) { write_att(d, c, 15); continue; }
            if (ch->tie == 1 || ch->tie == 3 || ch->nogate == 1) goto_env = 1;
            else if ((int16_t)ch->cnt <= ch->gate) { write_att(d, c, 15); continue; }
        }
        if (ch->env_done == 0) {
            int a = env_byte(d, ch->env_pos);
            ch->env_pos++;
            if (a == 0xFF) { ch->env_done = 1; continue; }
            write_att(d, c, a);
        } else if (ch->env_done == 1) write_att(d, c, ch->sus);
        else write_att(d, c, 15);
    }
}

static void step(DosDrv *d) {
    int any = 0;
    for (int c = 0; c < d->nch; c++) {
        DosChan *ch = &d->c[c];
        if (!ch->active) continue;
        if (ch->cnt == 0) fetch(d, c);
        if (!ch->active) continue;
        ch->cnt = (ch->cnt - 1) & 0xFFFF;
        if (d->variant == DOS_SPEAKER && ch->nogate != 1 && ch->cnt == ch->gate - 1) silence(d, c, 1);
    }
    for (int c = 0; c < d->nch; c++) if (d->c[c].active) any = 1;
    if (!any) {
        d->state = DOS_IDLE;
        d->current = -1;
        for (int c = 0; c < d->nch; c++) silence(d, c, 0);
    } else if (d->variant == DOS_TANDY) volume_pass(d);
}

void dosdrv_tick(DosDrv *d) {
    if (d->state == DOS_IDLE) return;
    if (d->state == DOS_START) init_song(d);
    step(d);
    int guard = 0;
    while (d->state == DOS_IDLE && d->loop_flag) {
        if (++guard > 16) break;                 /* a song that never waits would hang the original; stop it */
        for (int c = 0; c < d->nch; c++) d->c[c].ptr = d->c[c].loop_ptr;
        init_song(d);
        step(d);
    }
}

void dosdrv_stop(DosDrv *d) {
    d->state = DOS_IDLE;
    d->current = -1;
    for (int c = 0; c < d->nch; c++) silence(d, c, 1);
}

int dosdrv_play(DosDrv *d, int song) {
    if (!d->sound_on) return DOS_IGN_SOUNDOFF;
    if (song < 0 || song >= DOS_SONGS) return DOS_IGN_PRIO;
    if (d->current >= 0 && d->g->prio[d->current] >= d->g->prio[song]) return DOS_IGN_PRIO;
    if (song < 6 && d->music_off) return DOS_IGN_MUSICOFF;
    size_t sz;
    const uint8_t *bk = blk_bytes(d, &sz);
    d->current = song;
    for (int c = 0; c < d->nch; c++) {
        size_t e = d->variant == DOS_TANDY ? (size_t)song * 8 + (size_t)c * 2 : (size_t)song * 2;
        long p = e + 2 <= sz ? (long)rd16(bk + e) : -1;
        d->c[c].ptr = d->c[c].loop_ptr = p;
    }
    d->state = DOS_START;
    return DOS_ACCEPTED;
}

int dosdrv_force(DosDrv *d, int song) {
    dosdrv_stop(d);
    return dosdrv_play(d, song);
}

void dosdrv_set_loop(DosDrv *d, int flag) { d->loop_flag = flag; }

/* ------------------------------------------------------------------ synthesis */
#define OVS DOS_OVS
#define FIR_N 65      /* odd: the group delay is DOS_FIR_DELAY output samples exactly */
#define HIST_N 128
static float fir[FIR_N];
static int fir_ready;
static void fir_build(void) {
    double fc = 0.45 / OVS, sum = 0;
    for (int k = 0; k < FIR_N; k++) {
        double x = k - (FIR_N - 1) / 2.0;
        double sv = x == 0 ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
        double w = 0.42 - 0.5 * cos(2 * M_PI * k / (FIR_N - 1)) + 0.08 * cos(4 * M_PI * k / (FIR_N - 1));
        fir[k] = (float)(sv * w);
        sum += sv * w;
    }
    for (int k = 0; k < FIR_N; k++) fir[k] = (float)(fir[k] / sum);
    fir_ready = 1;
}

static const double TANDY_CLK = 3579545.0, PIT_CLK = 1193182.0;

void dossynth_init(DosSynth *s, int variant, double rate, int voicing) {
    memset(s, 0, sizeof *s);
    s->variant = variant;
    s->rate = rate;
    s->voicing = voicing;
    s->lfsr = 0x4000;
    s->dt = 1.0 / (rate * OVS);
    s->a_lp = exp(-2 * M_PI * 5000.0 / rate);
    s->a_hp = exp(-2 * M_PI * 250.0 / rate);
    if (!fir_ready) fir_build();
}

/* Take over the chip registers (call whenever the driver has written them). */
void dossynth_latch(DosSynth *s, const DosChip *chip) {
    for (int c = 0; c < 4; c++) s->amp[c] = chip->att[c] >= 15 ? 0.0 : 0.25 * pow(10.0, -2.0 * chip->att[c] / 20.0);
    for (int c = 0; c < 3; c++) { int nn = chip->div[c] & 0x3FF; if (!nn) nn = 1024; s->freq[c] = TANDY_CLK / (32.0 * nn); }
    if (s->serial_seen != chip->noise_serial) { s->serial_seen = chip->noise_serial; s->lfsr = 0x4000; }
    s->nc = chip->div[3] & 7;
    switch (s->nc & 3) {
    case 0: s->nfreq = TANDY_CLK / 512; break;
    case 1: s->nfreq = TANDY_CLK / 1024; break;
    case 2: s->nfreq = TANDY_CLK / 2048; break;
    default: s->nfreq = s->freq[2]; break;
    }
    int nn = chip->spk_div & 0xFFFF;
    if (!nn) nn = 65536;
    s->spf = PIT_CLK / nn;
    s->spk_on = chip->spk_on;
}

/* one oversampled step */
void dossynth_step(DosSynth *s) {
    double v = 0, dt = s->dt;
    if (s->variant == DOS_TANDY) {
        for (int c = 0; c < 3; c++) {
            s->phase[c] += s->freq[c] * dt;
            s->phase[c] -= floor(s->phase[c]);
            if (s->amp[c] > 0) v += s->phase[c] < 0.5 ? s->amp[c] : -s->amp[c];
        }
        s->nphase += s->nfreq * dt;
        while (s->nphase >= 1.0) {
            s->nphase -= 1.0;
            unsigned fb = (s->nc & 4) ? ((s->lfsr ^ (s->lfsr >> 1)) & 1) : (s->lfsr & 1);
            s->lfsr = (s->lfsr >> 1) | (fb << 14);
        }
        if (s->amp[3] > 0) v += (s->lfsr & 1) ? s->amp[3] : -s->amp[3];
    } else {
        /* the PIT counter runs freely; the speaker gate only switches the output */
        s->sp_phase += s->spf * dt;
        s->sp_phase -= floor(s->sp_phase);
        if (s->spk_on) v = s->sp_phase < 0.5 ? 0.5 : -0.5;
    }
    s->hpos = (s->hpos + 1) & (HIST_N - 1);
    s->hist[s->hpos] = (float)v;
}

/* decimated (and voiced) output sample, after OVS steps */
float dossynth_out(DosSynth *s) {
    double acc = 0;
    for (int k = 0; k < FIR_N; k++) acc += fir[k] * s->hist[(s->hpos - k) & (HIST_N - 1)];
    if (s->variant == DOS_SPEAKER && s->voicing) {
        s->hp_lp = (1 - s->a_hp) * acc + s->a_hp * s->hp_lp;
        double hp = acc - s->hp_lp;
        s->lp_y = (1 - s->a_lp) * hp + s->a_lp * s->lp_y;
        acc = s->lp_y;
    }
    return (float)acc;
}

void dossynth_render(DosSynth *s, const DosChip *chip, float *out, int n) {
    dossynth_latch(s, chip);
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < OVS; k++) dossynth_step(s);
        out[i] = dossynth_out(s);
    }
}

/* oversampled step index at which driver tick k starts */
int64_t dos_tick_ovs(int64_t k, int rate) {
    int64_t num = k * 5041 * rate * OVS, den = 1193182;
    return (num + den - 1) / den;
}
int64_t dos_tick_sample(int64_t k, int rate) { return (dos_tick_ovs(k, rate) + OVS - 1) / OVS; }

/* One pass of a song from a reset driver at the reference levels (spec 12 section 7.5, 15 section 2.2): float samples,
 * no clipping. The decimator delay is removed and the length is ceil(T * tick * rate). max_ticks caps the run. */
float *dos_render_float(const DosGame *g, int song, int variant, int rate, int max_ticks, size_t *nout) {
    DosDrv dr;
    memset(&dr, 0, sizeof dr);
    dosdrv_init(&dr, g, variant);
    DosSynth sy;
    dossynth_init(&sy, variant, rate, 1);
    dosdrv_play(&dr, song);
    float *pcm = NULL;
    size_t total = 0, cap = 0;
    int64_t nextb = 0, k = 0, ovs_pos = 0;
    int ended = 0;
    dossynth_latch(&sy, &dr.chip);
    for (;;) {
        while (!ended && k < max_ticks && ovs_pos >= nextb) {
            dosdrv_tick(&dr);
            if (dr.state == DOS_IDLE) { ended = 1; break; }
            dossynth_latch(&sy, &dr.chip);
            k++;
            nextb = dos_tick_ovs(k, rate);
        }
        if (ended || (k >= max_ticks && ovs_pos >= nextb)) break;
        dossynth_step(&sy);
        ovs_pos++;
        if (ovs_pos % DOS_OVS == 0) {
            if (total == cap) { cap = cap ? cap * 2 : 65536; pcm = realloc(pcm, cap * sizeof(float)); }
            pcm[total++] = dossynth_out(&sy);
        }
    }
    dossynth_latch(&sy, &dr.chip);
    size_t want = (size_t)dos_tick_sample(k, rate) + DOS_FIR_DELAY;
    while (total < want) {
        for (int q = 0; q < DOS_OVS; q++) dossynth_step(&sy);
        if (total == cap) { cap = cap ? cap * 2 : 65536; pcm = realloc(pcm, cap * sizeof(float)); }
        pcm[total++] = dossynth_out(&sy);
    }
    if (total > DOS_FIR_DELAY) { total -= DOS_FIR_DELAY; memmove(pcm, pcm + DOS_FIR_DELAY, total * sizeof(float)); } else total = 0;
    *nout = total;
    return pcm;
}
