/* ending.c - Decide sequences, bonus panel, score summary, midnight, continue prompt (spec 07 part B) */
#include "game.h"

static int dly(int n) { return (n * 4 / 13) * 100 / 73; }

/* ---------------------------------------------------------------- INTS lists + user hook */
static int ints_list[2][32];
static int ints_n[2];      /* 0 = ending list, 1 = hall list (INTS 1700) */

static void load_ints(int slot, int id) {
    size_t len;
    const uint8_t *d = res_get("INTS", id, &len);
    ints_n[slot] = 0;
    if (!d) return;
    for (size_t i = 0; i + 1 < len && ints_n[slot] < 32; i += 2) {
        int v = rd16(d + i);
        if (v == 0xFFFF) break;
        ints_list[slot][ints_n[slot]++] = v;
    }
}
static int ending_hook(AO *ao, int code, int a, int b, int d) {
    switch (code) {
    case -500: if (a >= 0 && a < ints_n[0]) snd_play_ctx(ints_list[0][a], b, 0, SC_ENDING); break;
    case -501: if (a >= 0 && a < ints_n[0]) snd_play_ctx(ints_list[0][a], b, SND_LOOP, SC_ENDING); break;
    case -502: if (a >= 0 && a < ints_n[0]) snd_stop(ints_list[0][a]); break;
    case -503: if (a >= 0 && a < ints_n[1]) snd_play_ctx(ints_list[1][a], b, 0, SC_AWARD); break;
    case -504: snd_play_ctx(a, b, 0, SC_ENDING); break;
    }
    return 0;
}

/* ---------------------------------------------------------------- clip running */
static void tick_aos(void) {
    g_eng.now = plat_now();
    eng_run_aos();
    eng_render();
    plat_present();
}
/* run until every given AO is paused or stopped */
static void wait_clips(AO **aos, int n) {
    int guard = 0;
    for (;;) {
        tick_aos();
        int busy = 0;
        for (int i = 0; i < n; i++) if (aos[i] && aos[i]->state == AO_RUNNING) busy = 1;
        if (!busy || ++guard > 60000) break;
        plat_wait(1);
    }
}
static void wait_clip(AO *a) { AO *l[1] = {a}; wait_clips(l, 1); }
static void wait_units(int n) { int cs = dly(n); for (int i = 0; i < cs; i++) { tick_aos(); plat_wait(1); } if (cs <= 0) tick_aos(); }

static AO *mkao(int aseq, int x, int y, int z) {
    AO *a = ao_create(aseq);
    if (!a) return NULL;
    a->x = x; a->y = y; a->z = z;
    return a;
}
static void startao(AO *a, int seq) { if (a) { a->x = a->x; ao_start(a, seq); } }
static void eseq_seq(AO *a, int k) { if (a) ao_start(a, -(k + 1)); }   /* ESEQ sequence k = negative number -(k+1) */

/* ---------------------------------------------------------------- text bubble */
static void bubble_text(const char *text, Rect box, int font) {
    /* every line centred, the block centred in the box */
    char lines[16][128];
    int n = 0;
    const char *p = text;
    while (*p && n < 16) {
        size_t l = strcspn(p, "\r\n");
        if (l > 127) l = 127;
        memcpy(lines[n], p, l);
        lines[n][l] = 0;
        n++;
        p += l;
        if (*p == '\r' && p[1] == '\n') p++;
        if (*p) p++;
    }
    int fh = ui_text_h(font);
    int y = box.y + (box.h - n * fh) / 2;
    for (int i = 0; i < n; i++) ui_text_center(font, box.x + box.w / 2, y + i * fh, lines[i], 0);
}
static void say_text(int text_id, Rect box) {
    size_t len;
    const uint8_t *t = res_get("TEXT", text_id, &len);
    if (!t) return;
    char buf[600];
    size_t l = strnlen((const char *)t, len < 599 ? len : 599);
    memcpy(buf, t, l);
    buf[l] = 0;
    bubble_text(buf, box, F_LARGE);
}

/* ---------------------------------------------------------------- score handling */
static void score_add(int n) {
    gs.score = (uint16_t)(gs.score + n);
    hud_score();
    snd_play(1747, 2, 0);
}

/* ---------------------------------------------------------------- midnight */
void midnight_sequence(void) {
    g_menu_grey = 1;                 /* the flashes: every item greyed, and the clock box is finished */
    g_clock_box_live = 0;
    music_stop();
    music_play(1735, 0);
    for (int i = 0; i < 10; i++) {
        ui_stamp(aseq_load(200), (i & 1) ? 75 : 74, 402, 304, 0);
        plat_present();
        plat_wait(dly(200));
    }
    music_wait();
}

void continue_prompt(int x, int y) {
    g_menu_grey = 0;                 /* the items are enabled again at the Continue prompt */
    g_clock_box_live = 0;
    ui_stamp_picture(1012, x, y, 0);
    plat_present();
    plat_key_flush();
    plat_click_flush();
    for (;;) {
        int k = plat_key_pop(), cx, cy;
        if (!k && plat_click_pop(&cx, &cy)) { if (cx >= x && cx < x + 114 && cy >= y && cy < y + 30) return; continue; }
        if (!k) { plat_wait(2); continue; }
        if (k == K_ENTER) return;
        if (k == K_ESC) {
            int yes = dlg_yesno(game_signed_in() ? 23008 : 23013, 0);
            if (yes) game_quit();
        }
    }
}

/* ---------------------------------------------------------------- bonus panel */
static const int RATE_E[4] = {100, 150, 300, 400}, RATE_T[4] = {25, 35, 50, 60}, WIN[4] = {1000, 2000, 3000, 5000},
                 WIN_CHUNK[4] = {50, 100, 150, 250};

static char panel_title_s[16], panel_line_s[64];
static int panel_shadow, panel_face;
static void panel_post(AO *ao) {
    if (getenv("SSO_DEBUG")) fprintf(stderr, "panel_post tick=%lld nse=%d y=%d state=%d line='%s'\n", (long long)plat_now(), ao->nse, ao->nse ? ao->se[0]->y : -1, ao->state, panel_line_s);
    if (ao->nse < 1 || ao->state == AO_RUNNING || !panel_line_s[0]) return;     /* text only while the panel holds (paused) */
    int font = F_LARGE;                       /* NFNT 28568 */
    int w = ui_text_w(font, panel_title_s);
    int x0 = 109 + (296 - w) / 2 + 1;
    for (int i = 0; i < 6; i++) ui_text(font, x0 + i, 71 - i, panel_title_s, panel_shadow);
    ui_text(font, x0 + 6, 65, panel_title_s, panel_face);
    int lw = ui_text_w(F_SMALL, panel_line_s);
    ui_text(F_SMALL, 109 + 29 + (235 - lw) / 2, 112, panel_line_s, C_WHITE);
}
static AO *panel_ao;
static void panel_text(const char *title, int shadow, int face, const char *line) {
    snprintf(panel_title_s, sizeof panel_title_s, "%s", title);
    snprintf(panel_line_s, sizeof panel_line_s, "%s", line);
    panel_shadow = shadow;
    panel_face = face;
    if (panel_ao) ao_force_redraw(panel_ao);
    tick_aos();
}

/* palette-index substitution on the whole panel image (ASEQ 1011 cel 0); persists through the slide-out */
static uint8_t *panel_backup;
static void panel_recolour(int a_from, int a_to, int b_from, int b_to) {
    Aseq *as = aseq_load(1011);
    Cel *c = (Cel *)aseq_cel(as, 0);
    if (!c) return;
    if (!panel_backup) { panel_backup = xmalloc((size_t)c->w * c->h); memcpy(panel_backup, c->pix, (size_t)c->w * c->h); }
    for (int i = 0; i < c->w * c->h; i++) {
        if (!c->mask[i]) continue;
        uint8_t v = c->pix[i];
        if (v == a_from) c->pix[i] = (uint8_t)a_to;
        else if (v == b_from) c->pix[i] = (uint8_t)b_to;
    }
    panel_line_s[0] = 0;     /* the redraw wipes the previous title and line */
    if (panel_ao) ao_force_redraw(panel_ao);
    tick_aos();
}
static void panel_restore(void) {
    Aseq *as = aseq_load(1011);
    Cel *c = (Cel *)aseq_cel(as, 0);
    if (c && panel_backup) memcpy(c->pix, panel_backup, (size_t)c->w * c->h);
    free(panel_backup);
    panel_backup = NULL;
}

static void bonus_panel(AO *mom) {
    int T = ses.tier;
    int energy = gs.energy;
    int minutes = ses.clock_on ? (11 - gs.clock / 100) * 60 + (60 - gs.clock % 100) : 0;
    if (gs.clock >= 1200) minutes = 0;
    AO *ban = ao_create(1011);
    if (ban) { ban->use_viewport = 1; ban->post_draw = panel_post; ao_set_fps(ban, 20); }   /* positions come from the script (absolute canvas pixels) */
    panel_ao = ban;
    panel_line_s[0] = 0;
    eseq_seq(mom, 5);
    wait_clip(mom);
    snd_play(1748, 5, 0);
    startao(ban, 0);
    if (ban) ao_set_z(ban, 60);
    wait_clip(ban);
    snd_stop(1748);
    char line[64], num[24];
    /* stage 1: Energy Left (no recolour) */
    ui_commas(num, sizeof num, (long)energy * RATE_E[T]);
    snprintf(line, sizeof line, "Energy Left: %s", num);
    panel_text("B O N U S", 0, C_WHITE, line);
    wait_units(240);
    while (gs.energy > 0) {
        gs.energy--;
        hud_energy();
        snd_play(1703, 2, 0);
        wait_units(10);
        score_add(RATE_E[T]);
        snd_play(1703, 2, 0);
        wait_units(10);
    }
    eseq_seq(mom, 4);
    wait_clip(mom);
    /* stage 2: Time Left (251 <-> 254) */
    panel_recolour(C_YELLOW, C_CYAN, C_CYAN, C_YELLOW);
    ui_commas(num, sizeof num, (long)minutes * RATE_T[T]);
    snprintf(line, sizeof line, "Time Left: %s", num);
    panel_text("B O N U S", C_WHITE, 0, line);
    wait_units(240);
    while (minutes > 0) {
        int s = minutes < 15 ? minutes : 15;
        int c = gs.clock + s;
        int h = c / 100, m = c % 100;
        while (m >= 60) { m -= 60; h++; }
        gs.clock = (uint16_t)(h * 100 + m > 1200 ? 1200 : h * 100 + m);
        hud_clock();
        snd_play(1703, 2, 0);
        wait_units(10);
        score_add(s * RATE_T[T]);
        snd_play(1703, 2, 0);
        wait_units(10);
        minutes -= s;
    }
    eseq_seq(mom, 4);
    wait_clip(mom);
    /* stage 3: Winning Game (current 254 -> 249, current 251 -> 0) */
    panel_recolour(C_CYAN, C_RED, C_YELLOW, 0);
    ui_commas(num, sizeof num, WIN[T]);
    snprintf(line, sizeof line, "Winning Game: %s", num);
    panel_text("B O N U S", 0, C_YELLOW, line);
    wait_units(240);
    for (int i = 0; i < 20; i++) {
        score_add(WIN_CHUNK[T]);
        snd_play(1703, 2, 0);
        wait_units(10);
    }
    snd_play(1749, 5, 0);
    if (ban) { startao(ban, 1); wait_clip(ban); }
    eseq_seq(mom, 6);
    wait_clip(mom);
    snd_stop(1749);
    panel_restore();
    panel_ao = NULL;
    panel_line_s[0] = 0;
    if (ban) ao_destroy(ban);
}

/* ---------------------------------------------------------------- score summary */
static void score_screen(void) {
    dbg_screen = "score";
    g_clock_box_live = 0;
    uint8_t *r = ses_rec();
    if (r) { r[REC_SAVED] = 0; }
    gs_live = 0;                       /* the game is recorded: the player counts as signed out (03 section 3.3c) */
    ui_fill(0, 0, 511, 383, 0, 1);
    ui_stamp_picture(225, 0, 0, ST_BACK);
    ui_fill(0, 336, 511, 337, C_RED, 0);
    plat_present();
    int wmax = ui_text_w(F_SMALL, "500,000");
    ui_text(F_SMALL, 400 - wmax, 0, "Points", C_WHITE);
    ui_text(F_SMALL, 427, 0, "Rank", C_YELLOW);
    ui_fill(306, 14, 511, 14, C_YELLOW, 0);
    static const int TY[8] = {306, 277, 247, 215, 173, 126, 78, 17};
    static const int MY[8] = {305, 276, 246, 214, 172, 125, 77, 16};
    for (int i = 0; i < 8; i++) {
        char t[16];
        ui_commas(t, sizeof t, rank_thresholds[i]);
        ui_text_right(F_SMALL, 400, TY[i], t, C_WHITE);
        ui_text(F_SMALL, 427, TY[i], rank_names[i], C_YELLOW);
    }
    char nm[10] = "";
    if (r) memcpy(nm, r, 9);
    ui_text_center(F_SMALL, (45 + 188) / 2, 217, nm, 0);
    ui_text(F_SMALL, 35, 248, "Game", 0);
    char t[24];
    ui_commas(t, sizeof t, gs.score);
    ui_text_right(F_SMALL, 197, 248, t, 0);
    ui_text(F_SMALL, 35, 271, "Total", 0);
    ui_commas(t, sizeof t, ses_lifetime());
    ui_text_right(F_SMALL, 197, 271, t, 0);
    plat_present();
    memcpy(g_eng.back->px, g_eng.work->px, (size_t)CANVAS_W * CANVAS_H);   /* steps 1-4 are the scene: every later erase restores them */
    snd_play_ctx(1764, 3, 0, SC_SCORE);
    Aseq *a225 = aseq_load(225);
    for (int c = 1; c <= 5; c++) {
        ui_stamp(a225, c, 192, 0, 0);
        plat_present();
        plat_wait(dly(30));
        ui_restore_rect(192, 0, 90, 35);
    }
    for (int seg = 0; seg < 7; seg++) {
        static const int Y0[7] = {37, 98, 146, 193, 235, 266, 297}, Y1[7] = {76, 124, 171, 213, 245, 275, 304};
        for (int y = Y0[seg]; y <= Y1[seg]; y++) { ui_fill(256, y, 257, y, C_RED, 0); plat_present(); plat_wait(1); }
    }
    int rank = rank_for_score(ses_lifetime());
    for (int i = 0; i <= rank; i++) {
        snd_play(1755, 2, 0);
        for (int b = 0; b < 3; b++) {
            ui_stamp(a225, 16, 237, MY[i], 0);
            plat_present();
            plat_wait(dly(30));
            if (b < 2) { ui_restore_rect(237, MY[i], 41, 21); plat_present(); plat_wait(dly(30)); }
        }
        snd_stop(1755);
    }
    if (rank == 7) {
        static const int cx[3] = {192, 243, 243};
        for (int rep = 0; rep < 2; rep++)
            for (int c = 6; c <= 8; c++) { ui_stamp(a225, c, cx[c - 6], 0, 0); plat_present(); plat_wait(dly(30)); }
        ui_stamp(a225, 9, 243, 0, 0); plat_present(); plat_wait(dly(30));
        snd_play(1727, 3, 0);
        ui_stamp(a225, 10, 243, 2, 0); plat_present(); plat_wait(dly(30));
        for (int c = 11; c <= 13; c++) { ui_stamp(a225, c, 128, 2, 0); plat_present(); plat_wait(dly(30)); }
        ui_stamp(a225, 14, 128, 0, 0); plat_present(); plat_wait(dly(30));
        ui_stamp(a225, 15, 128, 0, 0); plat_present();
        snd_play(1739, 2, 0);
    }
}

/* ---------------------------------------------------------------- Decide */
void action_decide_door(int room);


void ending_decide(int room) {
    int correct = (room == (int)gs.hideout);
    dbg_screen = "ending";
    g_menu_grey = 1;                 /* Decide .. score summary (win and lose): every item greyed */
    uint8_t *r = ses_rec();
    if (correct && r) { save_game(); }
    clock_freeze(1);
    action_decide_door(room);
    /* room interior */
    hud_rebuild();
    room_draw(ROOM_AREA[room]);
    ui_stamp_picture(7000, 243, 42, ST_BACK);
    plat_present();
    load_ints(1, 1700);
    load_ints(0, correct ? 1702 : 1703);
    g_eng.user_hook = ending_hook;
    Aseq *aset = aseq_load(correct ? 215 : 220);
    Aseq *hs = aseq_load(200);
    aseq_attach_eseq(hs, 1002);
    AO *pl = mkao(200, 0, 0, 20);
    startao(pl, -1);
    wait_clip(pl);
    music_stop();
    /* taunt */
    int t = rnga_scaled(8);
    if (!ses.voice) {
        ui_stamp(aset, correct ? 27 : 13, 154, 30, 0);
        say_text(500 + t, (Rect){188, 44, 118, 86});
        plat_present();
        wait_units(960);
        ui_restore_rect(154, 30, 192, 132);
    } else {
        speech_play(1838 + t);
        speech_wait();
    }
    aseq_attach_eseq(aset, correct ? 1000 : 1001);
    AO *mom = mkao(correct ? 215 : 220, 0, 0, 10);
    startao(mom, -1);
    wait_clip(mom);
    int win_result = 0;
    if (correct) {
        /* 2.2 */
        eseq_seq(mom, 2);
        wait_clip(mom);
        mom->use_viewport = 1;
        eseq_seq(mom, 1);
        wait_clip(mom);
        mom->use_viewport = 0;
        int m = rnga_scaled(8);
        if (!ses.voice) {
            ui_stamp(aset, 27, 307, 48, 0);
            say_text(508 + m, (Rect){341, 62, 118, 86});
            plat_present();
            wait_units(700);
            ui_restore_rect(307, 48, 192, 132);
        } else {
            ao_hide(mom);
            AO *head = mkao(1013 + m, 294, 159, 10);
            AO *body = mkao(1021, 305, 192, 10);
            startao(head, 0);
            startao(body, 0);
            speech_play(1846 + m);
            while (speech_playing() || (head && head->state == AO_RUNNING)) { tick_aos(); plat_wait(1); }
            if (head) ao_destroy(head);
            if (body) ao_destroy(body);
            ao_show(mom);
            eseq_seq(mom, 8);
            wait_clip(mom);
        }
        bonus_panel(mom);
        /* lifetime update */
        uint32_t life = ses_lifetime() + gs.score;
        if (life > 9999999) life = 9999999;
        int old_rank = ses.rank;
        (void)old_rank;
        savedb_set_score(&ses.db, ses.player, life);
        /* award animation */
        AO *plz = pl;      /* the hall player is one AO for the whole ending */
        if (life < 500000) {
            AO *both[2] = {plz, mom};
            dosint_award_begin();                   /* DOS effect style: the DOS award timeline starts with the player's zap */
            startao(plz, -4);
            startao(mom, -4);
            wait_clips(both, 2);
            for (int64_t w0 = plat_now(); dosint_award_active() && plat_now() - w0 < 1500;) plat_wait(1);   /* the last award frame stays until the DOS timeline is done (capped) */
        } else {
            startao(plz, -2);
            wait_clip(plz);
            startao(mom, -8);
            wait_clip(mom);
            startao(plz, -5);
            wait_clip(plz);
            startao(mom, -10);
            wait_clip(mom);
        }
        music_play(1757, 0);
        startao(plz, -3);
        wait_clip(plz);
        music_wait();
        win_result = 1;
    } else {
        /* 2.3 */
        snd_stop_all();
        AO *plz = pl;      /* the hall player is one AO for the whole ending */
        startao(plz, -6);
        startao(mom, -2);
        AO *both[2] = {plz, mom};
        wait_clips(both, 2);
        snd_play_ctx(1760, 5, 0, SC_WRONGDEC);
        int g = rnga_scaled(8);
        if (!ses.voice) {
            ui_stamp(aset, 10, 2, 88, 0);
            say_text(516 + g, (Rect){36, 113, 181, 55});
            plat_present();
        } else {
            snd_wait(1760);
            speech_play(1854 + g);
            speech_wait();
        }
        while (gs.energy > 0) { gs.energy--; hud_energy(); wait_units(10); }
        int c = gs.clock;
        while (c < 1200) {
            int h = c / 100, m = c % 100 + 10;
            while (m >= 60) { m -= 60; h++; }
            c = h * 100 + m;
            if (c > 1159) c = 1200;
            gs.clock = (uint16_t)c;
            hud_clock();
            wait_units(20);
        }
        gs.score = 0;
        hud_score();
        snd_play(1747, 2, 0);
        midnight_sequence();
    }
    /* clean up */
    ao_destroy(mom);
    ao_destroy(pl);
    aseq_attach_eseq(hs, 0);
    aseq_attach_eseq(aset, 0);
    g_eng.user_hook = NULL;
    if (win_result) {
        score_screen();
        if (ses.rank_at_start == 0 && rank_for_score(ses_lifetime()) >= 1) ses.hints = 0;
        if (ses_rec()) { ses_rec()[REC_SAVED] = 0; ses_write_record(); }
        continue_prompt(199, 346);
    } else {
        gs_live = 0;                         /* v1.31: a lost game is finished at the Continue prompt: no save, 23007 / 23013 */
        if (ses_rec()) { ses_rec()[REC_SAVED] = 0; ses_write_record(); }   /* D12: a loss clears the saved flag */
        continue_prompt(115, 322);
    }
    clock_freeze(0);
    game_abort_to_newgame();
}

/* conformance/debug: run just the bonus panel on a plain background (used by a hidden conform command) */
void ending_selftest_panel(const char *png_prefix) {
    ses.tier = 0;
    gs.energy = 2; gs.clock = 1150; gs.score = 0;
    ses.clock_on = 1;
    hud_rebuild();
    room_draw(10);
    g_eng.back->px[0] = g_eng.back->px[0];
    memcpy(g_eng.work->px, g_eng.back->px, (size_t)CANVAS_W * CANVAS_H);
    (void)png_prefix;
    bonus_panel(NULL);
}
