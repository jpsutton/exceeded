/* mathch.c - math challenges: screen, object displays, answer flow, calculator (spec 06) */
#include "game.h"

#define FC 19637
#define FM 25240
#define F0 28625
#define F1 28568
#define F2 28558

/* calculator / entry message dialogs (EXE DIALOG 20100..20107); mapping by message text is a guess */
enum { MSG_NEED_NUMBER = 20100, MSG_NEED_NEXT = 20101, MSG_NEED_EQUALS = 20102, MSG_DIV0 = 20104, MSG_REMAINDER = 20105,
       MSG_TOO_LARGE = 20106, MSG_NEGATIVE = 20107 };   /* no DIALOG 20103 exists; order follows the spec text */

static MthtInst mi;
static int obj_type;

/* ---- drawing helpers ---- */
/* line rasterisation (spec 06 section 8.8, v1.18): walk the major axis from the endpoint with the smaller major coordinate;
 * every plotted point draws the vertical run (x, y-t)..(x, y+t) */
static void dline(int x0, int y0, int x1, int y1, int colour, int t) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    if (dy <= dx) {
        if (x0 > x1) { int a = x0; x0 = x1; x1 = a; a = y0; y0 = y1; y1 = a; }
        int y = y0, d = 2 * dy - dx, step = y1 > y0 ? 1 : -1;
        ui_fill(x0, y - t, x0, y + t, colour, 0);
        for (int x = x0 + 1; x <= x1; x++) {
            if (d >= 0) { y += step; d += 2 * (dy - dx); } else d += 2 * dy;
            ui_fill(x, y - t, x, y + t, colour, 0);
        }
    } else {
        if (y0 > y1) { int a = x0; x0 = x1; x1 = a; a = y0; y0 = y1; y1 = a; }
        int x = x0, d = 2 * dx - dy, step = x1 > x0 ? 1 : -1;
        ui_fill(x, y0 - t, x, y0 + t, colour, 0);
        for (int y = y0 + 1; y <= y1; y++) {
            if (d >= 0) { x += step; d += 2 * (dx - dy); } else d += 2 * dx;
            ui_fill(x, y - t, x, y + t, colour, 0);
        }
    }
}
static const char *S(int n) { return mtht_flat_str(&mi, n); }
static int Wd(int n) { return mtht_flat_word(&mi, n); }
static void ctext(int font, int cx, int y, const char *s, int col) { ui_text(font, cx - ui_text_w(font, s) / 2, y, s, col); }
static void rtext(int font, int rx, int y, const char *s, int col) { int x = rx - ui_text_w(font, s); ui_text(font, x < 0 ? 0 : x, y, s, col); }
static void ditoa(int v, char *b, size_t n) { snprintf(b, n, "%d", v); }
/* vertical title: one character per step; spaces add `space` and draw nothing */
static void vtitle(int font, int cx, int y, const char *s, int step, int space) {
    for (const char *p = s; *p; p++) {
        if (*p == ' ') { y += space; continue; }
        char t[2] = {*p, 0};
        ui_text(font, cx - ui_text_w(font, t) / 2, y, t, 0);
        y += step;
    }
}
static int dly_cs(int n) { return (n * 4 / 13) * 100 / 73; }

/* ---- 8.0 map */
typedef struct City { const char *name; int lx, ly, dx, dy; } City;
static const City CITIES[18] = {
    {"Boston", 282, 100, 283, 133}, {"New York", 296, 111, 273, 144}, {"Dover", 246, 129, 264, 162}, {"Miami", 244, 221, 254, 258},
    {"Tampa", 235, 214, 246, 248}, {"Atlanta", 222, 184, 232, 220}, {"Chicago", 190, 97, 205, 128}, {"St. Louis", 181, 120, 189, 157},
    {"Detroit", 215, 92, 230, 124}, {"Dallas", 147, 177, 159, 211}, {"Houston", 158, 206, 164, 242}, {"Topeka", 159, 127, 168, 163},
    {"Seattle", 46, 88, 40, 81}, {"Oakland", 50, 134, 38, 165}, {"Fremont", 46, 174, 37, 170}, {"Pasadena", 50, 166, 55, 201},
    {"San Diego", 57, 167, 59, 207}, {"Phoenix", 81, 169, 88, 205},
};
static void disp_map(void) {
    ui_stamp_picture(8500, 13, 36, ST_BACK);
    ui_text(FC, 13 + (280 - ui_text_w(FC, S(0))) / 2, 42, S(0), 0);
    snd_play(1734, 10, 0);                       /* once, before the cities; then wait for it to end */
    for (int i = 0; i < 3; i++) {
        const char *nm = S(1 + i);
        const City *c = NULL;
        for (int k = 0; k < 18; k++) if (!strcmp(CITIES[k].name, nm)) c = &CITIES[k];
        int lx = c ? c->lx : 0, ly = c ? c->ly : 0;
        if (c) ui_fill(c->dx - 2, c->dy - 2, c->dx + 1, c->dy + 1, 1, 0);
        int h = ui_text_w(FM, nm) / 2;
        int x = lx + 5 - h;
        if (x < 17) x = 17;
        if (x > 288 - 2 * h) x = 288 - 2 * h;
        int y = ly < 53 ? 53 : ly > 228 ? 228 : ly;
        ui_text(FM, x, y, nm, 0);
        if (Wd(4) == 0) ui_text(FM, x + h - ui_text_w(FM, S(6 + i)) / 2, y + 15, S(6 + i), 0);
        plat_present();
    }
    snd_wait(1734);
}

/* ---- 8.1 bar graph */
static void disp_bar(void) {
    ui_stamp_picture(8501, 14, 36, ST_BACK);
    int n = Wd(2);
    if (n > 5) n = 5;
    int B = Wd(8), St = Wd(9) ? Wd(9) : 1;
    static const int barcol[5] = {13, 5, 2, 249, 13};
    int col = barcol[((Wd(18) % 5) + 5) % 5];
    int gap = n > 0 ? (188 - 17 * n) / (n + 1) : 0;
    int dollar = !strcmp(S(17), "Cassette Cost") || !strcmp(S(17), "Cost of Video Tape");
    for (int k = 0; k < 6; k++) {                                 /* y tick labels */
        char t[24];
        snprintf(t, sizeof t, "%s%d", dollar ? "$" : "", B + k * St);
        rtext(F2, 72, 210 - 26 * k, t, 0);
    }
    {   /* y-axis title: F0, vertical, centred on x 32 */
        int nc = (int)strlen(S(16));
        vtitle(F0, 32, 148 - (16 * nc - 3) / 2, S(16), 16, 13);
    }
    for (int i = 0; i < n; i++) ui_text(F2, 76 + gap + (gap + 17) * i + 8 - ui_text_w(F2, S(10 + i)) / 2, 219, S(10 + i), 0);
    ctext(F0, 170, 235, S(15), 0);
    {
        int tf = ui_text_w(F1, S(17)) > 188 ? FC : F1;
        ctext(tf, 150, 52, S(17), 0);
    }
    plat_present();
    for (int i = 0; i < n; i++) {
        int L = 76 + gap + (gap + 17) * i;
        int v = Wd(3 + i);
        int h = (v - B) * 26 / St;
        snd_play(1701, 10, 0);                   /* restarted for each bar; cut when the bar stops growing */
        for (int y = 213; y >= 215 - h; y--) {
            ui_fill(L - 1, y, L - 1, y, 0, 0);
            ui_fill(L + 18, y, L + 18, y, 0, 0);
            ui_fill(L, y, L + 17, y, col, 0);
            plat_present();
            plat_wait(1);
        }
        snd_stop(1701);
        plat_wait(dly_cs(50));
    }
}

/* ---- 8.2 prize board (spec 06 section 8.11) */
static void disp_prizes(void) {
    int64_t T0 = plat_now();
    ui_stamp_picture(8502, 13, 36, ST_BACK);
    static const int px[4] = {54, 103, 217, 209}, py[4] = {120, 43, 58, 143};
    static const int fx1[4] = {38, 136, 196, 188}, fy1[4] = {124, 47, 64, 147}, fx2[4] = {88, 186, 216, 238}, fy2[4] = {154, 77, 94, 177};
    static const int L[16] = {0x04, 0x00, 0x02, 0x0D, 0x01, 0x05, 0x10, 0x11, 0x07, 0xFC, 0xFA, 0xFE, 0xF9, 0xFD, 0xFB, 0xFF};
    char t[4][16];
    for (int i = 0; i < 4; i++) snprintf(t[i], sizeof t[i], "$%d", Wd(1 + i));
    int N = mtht_rand(5, 8);
    snd_play(1742, 10, 0);
    int prev = -1, k = 0;
    while (audio_sfx_on() ? snd_playing(1742) : k < N) {
        k++;
        if (k > 400) break;
        int p;
        do { p = mtht_rand(0, 3); } while (p == prev);
        prev = p;
        int c1;
        do { c1 = L[mtht_rand(0, 15)]; } while (c1 == 0xFF || c1 == 0xFB);
        ui_text(F2, px[p] + 13, py[p] + 39, t[p], c1);
        int c2;
        do { c2 = L[mtht_rand(0, 15)]; } while (!(c2 == 0x02 || c2 == 0x0D || c2 == 0x05 || c2 == 0x10 || c2 == 0x11 || c2 == 0xFE || c2 == 0xF9 || c2 == 0xFD));
        ui_recolour(fx1[p] + 10, fy1[p] + 39, fx2[p] + 13, fy2[p] + 39, 0xFB, c2);
        plat_present();
        while (plat_now() < T0 + 6 * k) plat_wait(1);
        ui_text(F2, px[p] + 13, py[p] + 39, t[p], 0xFF);
        ui_recolour(fx1[p] + 10, fy1[p] + 39, fx2[p] + 13, fy2[p] + 39, c2, 0xFB);
        plat_present();
    }
    int drawn[4] = {0, 0, 0, 0};
    for (int n = 0; n < 4; n++) {
        int p;
        do { p = mtht_rand(0, 3); } while (drawn[p] || p == prev);
        drawn[p] = 1;
        prev = -1;
        ui_text(F2, px[p] + 13, py[p] + 39, t[p], 0);
    }
    plat_present();
    snd_wait(1742);
}

/* ---- 8.3 spinner */
static const int SPIN_X[10] = {144, 171, 188, 188, 174, 141, 91, 61, 64, 90};
static const int SPIN_Y[10] = {65, 90, 124, 162, 190, 206, 186, 163, 128, 89};
static void spinner_texts(void) {
    static const int sx[10] = {139, 187, 208, 208, 187, 139, 91, 70, 70, 91};
    static const int sy[10] = {35, 53, 86, 130, 167, 185, 167, 130, 86, 53};
    if (S(17)[0] != ' ') {                                  /* a leading space: nothing at all in the centre */
        ctext(F2, 156, 142, S(17), 0);
        if (strcmp(S(17), "LAST")) ctext(F2, 156, 178, "SPIN", 0);
        if (S(18)[0] != ' ') ctext(F0, 156, 159, S(18), 0);
        else ui_fill(128, 156, 181, 171, 0xFD, 0);
    }
    for (int i = 0; i < 10; i++) {
        char t[16];
        ditoa(Wd(5 + i), t, sizeof t);
        ui_text(F0, sx[i] + 16 - ui_text_w(F0, t) / 2, sy[i] + 48, t, 0);    /* centred on X + 16 */
    }
}
static void disp_spinner(void) {
    dosint_flag(DF_SPIN_PLAYED, 0);                 /* a new spin: DOS sounds its first tick again */
    ui_stamp_picture(8505, 13, 36, ST_BACK);
    Aseq *a = aseq_load(8520);
    int target = ((Wd(2) % 10) + 10) % 10;
    int prevk = -1;
    for (int s = 0; s < 40; s++) {
        snd_play(1751, 2, 0);
        if (prevk >= 0 && a) {
            const Cel *pc = aseq_cel(a, prevk);
            if (pc) ui_restore_rect(SPIN_X[prevk], SPIN_Y[prevk], pc->w, pc->h);
        }
        int k = s % 10;
        if (a) ui_stamp(a, k, SPIN_X[k], SPIN_Y[k], 0);
        spinner_texts();                            /* centre text and the ten values over the pointer */
        plat_present();
        plat_wait(s);                               /* raw centiseconds */
        snd_stop(1751);
        prevk = k;
        if (s > 13 && s % 10 == target) break;
    }
}

/* ---- 8.4 Great 8 */
static void g8_header(int code, int col) {
    static const char *H[4][3] = {{"Last", "Wk's", "Pos."}, {"Weeks", "on", "Chart"}, {"Calls", "This", "Week"}, {"Calls", "Last", "Month"}};
    int h = code - 3;
    if (h < 0 || h > 3) h = 0;
    for (int l = 0; l < 3; l++) ui_text(F2, ((col == 1 ? 408 : 518) - ui_text_w(F2, H[h][l])) / 2, 67 + 12 * l, H[h][l], 0);
}
static void disp_great8(void) {
    ui_stamp_picture(8503, 13, 36, ST_BACK);
    g8_header(Wd(0), 1);
    g8_header(Wd(1), 2);
    int r1 = Wd(2), r2 = Wd(3);
    /* ranks and names */
    const char *names[8];
    int fill_idx = 14;
    for (int row = 0; row < 8; row++) {
        char t[16];
        snprintf(t, sizeof t, "%d.", row + 1);
        ui_text(F2, 19, 103 + 18 * (row + 1), t, 0);
        names[row] = (row == r1) ? S(12) : (row == r2) ? S(13) : S(fill_idx++);
        ui_text(F2, 32, 103 + 18 * (row + 1), names[row], 0);
    }
    int ov = Wd(20) > 7;
    char t[16];
    ditoa(Wd(9), t, sizeof t);
    int R1 = (ui_text_w(F2, t) + 408) / 2;
    ditoa(Wd(11), t, sizeof t);
    int R2 = (ui_text_w(F2, t) + 518) / 2;
    int excl[16], ne = 0;
    excl[ne++] = Wd(4); excl[ne++] = Wd(6);
    if (ov) excl[ne++] = Wd(22);
    for (int row = 0; row < 8; row++) {                 /* value column 1 */
        int v;
        if (row == r1) v = Wd(4);
        else if (row == r2) v = Wd(6);
        else if (row == 2 && ov) v = Wd(22);
        else {
            int tries = 0, dup;
            do {
                v = mtht_rand(Wd(8), Wd(9));
                dup = 0;
                for (int k = 0; k < ne; k++) if (excl[k] == v) dup = 1;
            } while (dup && ++tries < 200);
            if (ne < 16) excl[ne++] = v;
        }
        ditoa(v, t, sizeof t);
        rtext(F2, R1, 103 + 18 * (row + 1), t, 0);
    }
    for (int row = 0; row < 8; row++) {                 /* value column 2 */
        int v;
        if (row == r1) v = Wd(5);
        else if (row == r2) v = Wd(7);
        else if (row == 2 && ov) v = Wd(23);
        else v = mtht_rand(Wd(10), Wd(11));
        ditoa(v, t, sizeof t);
        rtext(F2, R2, 103 + 18 * (row + 1), t, 0);
    }
    plat_present();
    snd_play(1722, 10, 0);
    Aseq *an = aseq_load(8511);
    int frames = 0;
    while ((snd_playing(1722) || (!audio_sfx_on() && frames < 12)) && frames < 400) {
        if (an) ui_stamp(an, frames % 4, 18, 62, 0);
        plat_present();
        plat_wait(27);
        frames++;
        if (!audio_sfx_on() && frames >= 12) break;
    }
    snd_wait(1722);
}

/* ---- 8.5 volume */
static void disp_volume(void) {
    ui_stamp_picture(8504, 14, 36, ST_BACK);
    ui_text(F1, 157 - ui_text_w(F1, S(9)) / 2, 49, S(9), 0);
    plat_present();
    static const int kx[3] = {34, 125, 216};
    Aseq *a = aseq_load(8533);
    const Cel *kc = a ? aseq_cel(a, 0) : NULL;
    for (int i = 0; i < 3; i++) {
        int v = Wd(24 + i);
        if (!kc) continue;
        if (v != 0) snd_play(1736, 10, 0);
        for (int st = 0; st <= 17 * v; st++) {
            if (st > 0) ui_restore_rect(kx[i], 244 - (st - 1), kc->w, kc->h);
            ui_stamp(a, 0, kx[i], 244 - st, 0);
            plat_present();
            plat_wait(1);
        }
        if (v != 0) snd_stop(1736);
    }
}

/* ---- 8.6 monitor */
static void disp_monitor(void) {
    ui_stamp_picture(8506, 13, 36, ST_BACK);
    ui_text(F0, 13 + (274 - ui_text_w(F0, S(24))) / 2, 64, S(24), 0);
    for (int k = 0; k < 6; k++) {
        int y = 83 + 20 * k;
        char t[16];
        ui_text(F2, 57, y, S(5 + k), 0);
        ditoa(Wd(11 + k), t, sizeof t);
        rtext(F2, 200, y, t, 0);
        ui_text(F2, 206, y, S(17 + k), 0);
    }
    plat_present();
    /* TV static (spec 06 section 8.11): 50 - floor(f/4) blocks per frame, positions re-chosen on even frames only;
     * every block XORs its 2x2 pixels with 0xFF so each odd frame undoes the even one before it */
    snd_play(1740, 10, 0);
    int count = 51, f = 0;
    int xs[64], ys[64];
    while (audio_sfx_on() ? (snd_playing(1740) || (f & 1)) : f < 60) {
        if (f > 3000) break;
        int64_t t0 = plat_now();
        while (plat_now() < t0 + 1) plat_wait(1);
        if (f % 4 == 0) count--;
        if (count < 0) count = 0;
        if ((f & 1) == 0)
            for (int i = 0; i < count; i++) { xs[i] = 43 + mtht_rand(0, 220); ys[i] = 62 + mtht_rand(0, 142); }
        for (int i = 0; i < count; i++)
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) g_eng.work->px[(ys[i] + dy) * CANVAS_W + xs[i] + dx] ^= 0xFF;
        plat_present();
        f++;
    }
    snd_wait(1740);
}

/* ---- 8.7 equipment list */
static void disp_equipment(void) {
    ui_stamp_picture(8509, 13, 36, ST_BACK);
    snd_play(1718, 10, 0);
    int r = Wd(0);
    const char *subj = S(10);
    int split = strchr(subj, ' ') != NULL;
    /* names: left at 20, one string per line */
    char lines[10][48];
    int nl = 0, nextf = 11;
    for (int k = 0; k < 10 && nl < 8; ) {
        if (k == r) {
            if (split) {
                const char *p = subj;
                while (*p && nl < 8) {
                    const char *q = p + 1;
                    while (*q && *q != ' ') q++;
                    snprintf(lines[nl++], sizeof lines[0], "%.*s", (int)(q - p), p);
                    p = q;
                }
            } else snprintf(lines[nl++], sizeof lines[0], "%s", subj);
            k++;
            if (split) k++;                    /* the subject occupies two lines; later rows keep their names */
            continue;
        }
        snprintf(lines[nl++], sizeof lines[0], "%s", S(nextf++));
        k++;
    }
    /* value row plan */
    int rows_y[8], rows_subj[8], nr = 0;
    {
        int Y = 105;
        for (int i = 0; i < 8; i++) {
            if (i == r) {
                if (split) { Y += 9; Y += 18; rows_y[nr] = Y; rows_subj[nr++] = 1; Y += 9; i++; }
                else { Y += 18; rows_y[nr] = Y; rows_subj[nr++] = 1; }
            } else { Y += 18; rows_y[nr] = Y; rows_subj[nr++] = 0; }
        }
    }
    int R2 = (ui_text_w(F2, "180") + 426) / 2;
    int R3w = ui_text_w(F2, "$1500");
    int R3 = (R3w + 525) / 2;
    for (int i = 0; i < nr; i++) ui_text(F2, R3 - R3w, rows_y[i], "$", 0);
    for (int k = 0; k < nl; k++) ui_text(F2, 20, 123 + 18 * k, lines[k], 0);
    char t[48], u[16];
    int j = 0;
    for (int i = 0; i < nr; i++) {                         /* column 1 */
        if (rows_subj[i]) {
            char suffix[8];
            snprintf(suffix, sizeof suffix, "%s", S(18));
            if (strlen(suffix) == 1) suffix[0] = 0;
            snprintf(t, sizeof t, "%d%s", Wd(1), suffix);
        } else snprintf(t, sizeof t, "%d%s", mtht_rand(Wd(4), Wd(5)), S(19 + j++));
        ui_text(F2, (286 - ui_text_w(F2, t)) / 2, rows_y[i], t, 0);
    }
    for (int i = 0; i < nr; i++) {                         /* column 2 */
        ditoa(rows_subj[i] ? Wd(2) : mtht_rand(Wd(6), Wd(7)), u, sizeof u);
        rtext(F2, R2, rows_y[i], u, 0);
    }
    for (int i = 0; i < nr; i++) {                         /* column 3 */
        ditoa(rows_subj[i] ? Wd(3) : mtht_rand(Wd(8), Wd(9)), u, sizeof u);
        rtext(F2, R3, rows_y[i], u, 0);
    }
    plat_present();
    for (int rr = 0; rr < 8; rr++) plat_wait(dly_cs(50));    /* header band routine is disabled in the original */
    snd_wait(1718);
}

/* ---- 8.8 line graph */
static int line_y(int v, int Y0) {
    int St = Wd(5) ? Wd(5) : 1;
    int d = (v - Wd(4)) * 29;
    int q = d / St, rem = d - q * St;
    int y = Y0 - q;
    if (rem > St / 2) y--;
    return y;
}
static void disp_line(int hard) {
    ui_stamp_picture(hard ? 8508 : 8507, 14, 36, ST_BACK);
    ctext(FC, 154, 43, S(17), 0);
    {
        int nc = (int)strlen(S(18));
        int y0 = hard ? 129 - (18 * nc) / 2 : 145 - (17 * nc) / 2;
        if (y0 < (hard ? 72 : 73)) y0 = hard ? 72 : 73;
        vtitle(FC, 33, y0, S(18), 17, 14);     /* 17 px per character on both graphs */
    }
    ctext(FC, 177, hard ? 210 : 238, S(19), 0);
    int n = Wd(2);
    int Y0 = hard ? 171 : 201;
    int ticks = hard ? 4 : 5;
    for (int k = 0; k < ticks; k++) {
        char t[16];
        ditoa(Wd(4) + k * Wd(5), t, sizeof t);
        rtext(F2, 71, (hard ? 170 : 197) - 29 * k, t, 0);
    }
    for (int i = 0; i < n && i < 5; i++) ctext(F2, 104 + 36 * i, hard ? 198 : 226, S(20 + i), 0);
    int two = hard && Wd(3) > 1;
    if (two) {
        ui_text(F2, 76, 235, S(25), 0);
        ui_text(F2, 76, 251, S(26), 0);
        dline(21, 236, 72, 236, 0xF9, 1);
        dline(21, 252, 72, 252, 0, 0);
    }
    plat_present();
    for (int series = 0; series < (two ? 2 : 1); series++) {
        int px0 = -1, py0 = -1;
        int colour = series ? 0 : 0xF9, thick = series ? 0 : 1, mk = series ? 3 : 4;
        for (int i = 0; i < n && i < 5; i++) {
            int x = 104 + 36 * i;
            int y = line_y(Wd((series ? 11 : 6) + i), Y0);
            if (px0 >= 0) dline(px0, py0, x, y, colour, thick);
            ui_fill(x - mk, y - mk, x + mk, y + mk, colour, 0);
            plat_present();
            if (px0 >= 0) { snd_play(1730, 10, 0); plat_wait(dly_cs(150)); snd_wait(1730); }
            px0 = x; py0 = y;
        }
    }
}

/* ---- 8.9 VCR */
static void disp_vcr(void) {
    ui_stamp_picture(8510, 14, 36, ST_BACK);
    static const int ty[4] = {191, 155, 119, 83};
    static const char L[4] = {'D', 'C', 'B', 'A'};
    static const int delay[4] = {60, 50, 40, 30};
    Aseq *tape = aseq_load(8530);
    for (int i = 0; i < 4; i++) {
        if (tape) ui_stamp(tape, 0, 81, ty[i], 0);
        char t[8] = {L[i], 0};
        ui_text(F2, 124, ty[i] + 14, t, 0);
        char v[16];
        ditoa(Wd(3 - i), v, sizeof v);
        rtext(F2, 185, ty[i] + 15, v, 0);
        ui_text(F2, 190, ty[i] + 15, S(4), 0);
        plat_present();
        snd_play(1758, 10, 0);
        plat_wait(dly_cs(delay[i]));
        snd_wait(1758);
    }
}

static void draw_display(int type, int hard) {
    switch (type) {
    case 0: disp_map(); break;
    case 1: disp_bar(); break;
    case 2: disp_prizes(); break;
    case 3: disp_spinner(); break;
    case 4: disp_great8(); break;
    case 5: disp_volume(); break;
    case 6: disp_monitor(); break;
    case 7: disp_equipment(); break;
    case 8: disp_line(hard); break;
    case 9: disp_vcr(); break;
    }
}

/* ---- message box helpers */
static void msg_box_clear(void) {
    ui_fill(19, 300, 294, 374, C_WHITE, 0);
}
static void msg_lines(const char *a, const char *b, const char *c) {
    msg_box_clear();
    int y = 313;
    int fh = ui_text_h(F2) + 7;
    if (a) { ui_text(F2, 29, y, a, 0); y += fh; }
    if (b) { ui_text(F2, 29, y, b, 0); y += fh; }
    if (c) ui_text(F2, 29, y, c, 0);
}

/* ---- the challenge */
static const char *STUDY[10] = {"map", "graph", "prizes", "spinner", "chart", "settings", "list", "chart", "graph", "tapes"};

static void calc_message(int dlg) { dlg_show_at(dlg, 0, 16, 236); }

/* answer display (spec 06 section 7.1): blank 316..393 x 251..276 with palette 129; entry NFNT 28558 right-aligned to x 364
 * (baseline 267); the pending operator right-aligned to x 379 (baseline 265). Text y inputs 259 / 257 under the
 * ascent-descent baseline rule. */
static void find_display(void) {}
static void draw_entry(const char *entry, char op) {
    ui_fill(316, 251, 393, 276, 129, 0);
    if (entry[0]) ui_text_right(F2, 364, 259, entry, 0);
    if (op && ses.calc) {
        char o[2] = {op == '*' ? (char)0xC5 : op == '/' ? (char)0xD6 : op, 0};
        ui_text_right(F2, 379, 257, o, 0);
    }
}

typedef struct Calc { int state; long a, b; char op; char entry[24]; int len; } Calc;

static void calc_reset(Calc *c) { memset(c, 0, sizeof *c); }
static long calc_entry_val(Calc *c) { return atol(c->entry); }
static void calc_set_entry(Calc *c, long v) { snprintf(c->entry, sizeof c->entry, "%ld", v); c->len = (int)strlen(c->entry); }
static void calc_digit(Calc *c, int d) {
    if (c->len == 1 && c->entry[0] == '0') c->len = 0;
    if (c->len >= 4) return;   /* 5th digit just beeps */
    c->entry[c->len++] = (char)('0' + d);
    c->entry[c->len] = 0;
}

/* compute A op B; returns an error dialog id or 0 (for a remainder error *r holds the quotient) */
static int calc_apply(Calc *c, long *r) {
    c->b = calc_entry_val(c);
    int err = 0;
    *r = 0;
    if (c->op == '+') *r = c->a + c->b;
    else if (c->op == '-') { if (c->b > c->a) err = MSG_NEGATIVE; else *r = c->a - c->b; }
    else if (c->op == '*') *r = c->a * c->b;
    else {
        if (c->b == 0) err = MSG_DIV0;
        else if (c->a % c->b) { err = MSG_REMAINDER; *r = c->a / c->b; }
        else *r = c->a / c->b;
    }
    if (!err && *r > 9999) err = MSG_TOO_LARGE;
    return err;
}

/* calculator error: display/reset order per spec 06 section 7.1 */
static void calc_error(Calc *c, int msg, long quotient) {
    char q[24];
    if (msg == MSG_REMAINDER) { snprintf(q, sizeof q, "%ld", quotient); draw_entry(q, 'r'); plat_present(); calc_message(msg); calc_reset(c); }
    else if (msg == MSG_DIV0) { draw_entry(c->entry, c->op); plat_present(); calc_message(msg); calc_reset(c); }
    else { calc_reset(c); draw_entry("", 0); plat_present(); calc_message(msg); }
}

/* returns 1 when an answer is submitted (value in *out) */
static int calc_key(Calc *c, int k, long *out, int with_calc) {
    if (!with_calc) {
        if (k >= '0' && k <= '9') { calc_digit(c, k - '0'); if (c->state == 0) c->state = 1; }
        else if (k == K_BACKSPACE || k == K_DELETE) { if (c->len) c->entry[--c->len] = 0; }
        else if (k == 'c' || k == 'C') { c->len = 0; c->entry[0] = 0; }
        else if (k == K_ENTER) {
            if (!c->len) { calc_message(MSG_NEED_NUMBER); return 0; }
            *out = atol(c->entry);
            return 1;
        }
        return 0;
    }
    if (k >= '0' && k <= '9') {
        if (c->state == 0 || c->state == 4) { c->len = 0; c->entry[0] = 0; c->state = 1; }
        else if (c->state == 2) { c->len = 0; c->entry[0] = 0; c->state = 3; }
        calc_digit(c, k - '0');
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (c->state == 1 || c->state == 4) { c->a = calc_entry_val(c); c->op = (char)k; c->state = 2; }
        else if (c->state == 3) {
            long r = 0;
            int err = calc_apply(c, &r);
            if (err) {
                calc_error(c, err, r);
                /* quirk: a chained operator that errors leaves state 2 with this operator pending and A = 0 */
                c->state = 2; c->op = (char)k; c->a = 0;
                return 0;
            }
            c->a = r; calc_set_entry(c, r); c->op = (char)k; c->state = 2;
        }
    } else if (k == '=') {
        if (c->state == 3) {
            long r = 0;
            int err = calc_apply(c, &r);
            if (err) { calc_error(c, err, r); return 0; }
            calc_set_entry(c, r); c->state = 4; c->op = 0;
        }
    } else if (k == K_BACKSPACE || k == K_DELETE) {
        if (c->state == 1) { if (c->len) c->entry[--c->len] = 0; if (!c->len) { c->state = 0; } }
        else if (c->state == 2) { c->op = 0; c->state = 1; calc_set_entry(c, c->a); }
        else if (c->state == 3) { if (c->len) c->entry[--c->len] = 0; if (!c->len) { c->state = 2; calc_set_entry(c, c->a); } }
    } else if (k == 'c' || k == 'C') calc_reset(c);
    else if (k == K_ENTER) {
        if (c->state == 0) calc_message(MSG_NEED_NUMBER);
        else if (c->state == 2) calc_message(MSG_NEED_NEXT);
        else if (c->state == 3) calc_message(MSG_NEED_EQUALS);
        else if (c->state == 1 || c->state == 4) { *out = calc_entry_val(c); return 1; }
    }
    return 0;
}

/* calculator key faces (appendix C), relative to (299,235); the original hit-tests ~2 px larger */
static int calc_click_key(int cx, int cy) {
    static const struct { int x0, x1, y0, y1, key; } K[] = {
        {109, 120, 14, 26, '7'}, {130, 141, 14, 26, '8'}, {151, 162, 14, 26, '9'}, {172, 183, 14, 26, '/'},
        {109, 120, 36, 48, '4'}, {130, 141, 36, 48, '5'}, {151, 162, 36, 48, '6'}, {172, 183, 36, 48, '*'},
        {109, 120, 58, 70, '1'}, {130, 141, 58, 70, '2'}, {151, 162, 58, 70, '3'}, {172, 183, 58, 70, '-'},
        {109, 120, 80, 92, '0'}, {130, 162, 80, 92, '='}, {172, 183, 80, 92, '+'},
        {21, 32, 58, 70, 'c'}, {21, 69, 79, 91, K_ENTER},
    };
    int x = cx - 299, y = cy - 235;
    for (int i = 0; i < ARRAY_LEN(K); i++)
        if (x >= K[i].x0 - 2 && x <= K[i].x1 + 2 && y >= K[i].y0 - 2 && y <= K[i].y1 + 2) return K[i].key;
    return 0;
}

static void reveal_clues(void) {
    int r = -1;
    for (int i = 0; i < 5; i++) if (ROOM_AREA[i] == ag.area) r = i;
    if (r < 0) return;
    int n = CLUES_PER[ses.tier];
    for (int k = 0; k < 4 && n > 0; k++)
        if (gs.clue[r][k] & 0x80) { gs.clue[r][k] &= 0x7F; n--; }
}

static int pick_template(int type, int sub) {
    int L = gs.level, hard;
    if (L == 0) hard = 0;
    else if (L == 3) hard = 1;
    else hard = (sub == 0 || sub == 3);   /* L1/L2: hard for Add and Subtract */
    return type * 10 + sub + 4 * hard + 1;
}

void (*g_calc_redraw)(void);             /* set while a challenge is on screen: the Calculator option changed (03 section 3.0a F, recommended redraw) */
static Calc *cur_calc;
static void calc_art_redraw(void) {
    const Cel *old = aseq_cel(aseq_load(ses.calc ? 8534 : 8531), 0);     /* the art that was on screen */
    if (old) ui_restore_rect(299, 235, old->w, old->h);
    ui_stamp_picture(ses.calc ? 8531 : 8534, 299, 235, 0);
    if (cur_calc) draw_entry(cur_calc->entry, cur_calc->op);
    plat_present();
}

void math_challenge(int idx) {
    obj_type = idx;
    int sub = gs.obj_op[idx];
    int id = pick_template(idx, sub);
    int hard = (id - idx * 10 - 1) >= 4;
    if (mtht_run(id, ses.math_seed[idx], gs.obj_q[idx], &mi) != 0) return;
    snd_stop_all();
    music_stop();                                   /* opening stops all sounds and the background music */
    dbg_screen = "challenge";
    dbg_chal.object = idx; dbg_chal.mtht_id = id; dbg_chal.seed = ses.math_seed[idx]; dbg_chal.qnum = gs.obj_q[idx];
    dbg_chal.wrong = gs.obj_wrong[idx]; dbg_chal.entry[0] = 0;
    hud_rebuild();
    ui_stamp_picture(10000, 297, 35, 0);
    ui_stamp_picture(ses.calc ? 8531 : 8534, 299, 235, 0);
    ui_stamp(aseq_load(8551), 0, 212, 340, 0);
    draw_display(idx, hard);                       /* the display builds up with its own sound (06 section 8.10) */
    ui_text_wrap(F2, 313, 70, 481, mi.question, 0, 0);   /* question text only after the display finished */
    plat_present();
    msg_box_clear();
    find_display();
    Calc c;
    calc_reset(&c);
    cur_calc = &c;
    g_calc_redraw = calc_art_redraw;
    draw_entry(c.entry, c.op);
    plat_key_flush();
    plat_click_flush();
    int finished = 0, correct = 0, early_fail = 0;
    while (!finished) {
        int k = plat_key_pop();
        int cx, cy;
        if (!k && plat_click_pop(&cx, &cy)) {
            if (cx >= 212 && cx < 284 && cy >= 340 && cy < 362) { snd_play(1756, 2, 0); k = K_ESC; }
            else { k = calc_click_key(cx, cy); if (!k) continue; if (!ses.calc && !(k >= '0' && k <= '9') && k != K_ENTER && k != 'c') continue; snd_play(1756, 2, 0); }
        }
        if (!k) { plat_wait(2); continue; }
        if (k == K_ESC) break;   /* leave: challenge stays available, tries kept */
        long val;
        if (k == K_ENTER) ui_restore_rect(96, 300, 205, 75);   /* message area restored from the background first */
        if (calc_key(&c, k, &val, ses.calc)) {
            if (val == mi.answer) {
                static const char *P[5] = {"That's correct!", "That's the answer!", "That's right!", "You're right!", "Good job!"};
                snd_play(1743, 2, 0);
                msg_box_clear();
                ui_text(F2, 29, 313, P[rngb_range(0, 4)], 0);
                correct = 1;
                finished = 1;
                plat_present();
                snd_wait(1743);
            } else {
                gs.obj_wrong[idx]++;
                snd_play(1760, 2, 0);
                char b[640];
                if (gs.obj_wrong[idx] == 1) {
                    snprintf(b, sizeof b, "study the %s.", STUDY[idx]);
                    msg_lines("That's not correct.", "Read the question;", b);
                } else if (gs.obj_wrong[idx] == 2) {
                    snprintf(b, sizeof b, "HINT: %s", mi.hint);
                    msg_box_clear();
                    ui_text_wrap(F2, 106 - 77, 313, 295, b, 0, 7 - 0);
                } else {
                    snprintf(b, sizeof b, "That's not correct.  The correct answer is %d.", mi.answer);
                    msg_box_clear();
                    ui_text_wrap(F2, 29, 313, 289, b, 0, 7);
                    finished = 1;
                    gs.obj_state[idx] = 3;                      /* failed as soon as judged: a Return to Sign In during the feedback saves it failed (03 section 3.0a D) */
                    reveal_clues();
                    early_fail = 1;
                    plat_present();
                    snd_wait(1760);
                }
                calc_reset(&c);
            }
        }
        draw_entry(c.entry, c.op);
        snprintf(dbg_chal.entry, sizeof dbg_chal.entry, "%s", c.entry);
        dbg_chal.wrong = gs.obj_wrong[idx];
        plat_present();
    }
    if (finished) {
        if (correct) {
            gs.obj_state[idx] = 2;
            int pts = tier_params[TP_MATH_BASE][ses.tier] - tier_params[TP_MATH_PENALTY][ses.tier] * gs.obj_wrong[idx];
            gs.score = (uint16_t)(gs.score + pts);
            hud_score();
            plat_present();
            snd_play(1747, 2, 0);
            gs.credit = (uint8_t)(gs.credit + 2);
        } else if (!early_fail) { gs.obj_state[idx] = 3; reveal_clues(); }
        if (correct) reveal_clues();
    }
    g_calc_redraw = NULL;
    cur_calc = NULL;
    mtht_free(&mi);
}
