/* core.c - game state block, session, clock, hints, world tables */
#include "game.h"

GameState gs;
Session ses;
int gs_live;            /* this game's live state is initialised and the game is not finished: Save Game works, Exit / Return mention saving (03 section 4.3, v1.31) */
int game_signed_in(void) { return ses.player >= 0 && !ses.drill_mode && gs_live; }
jmp_buf jb_signin, jb_newgame;

void gs_pack(uint8_t o[STATE_SIZE]) {
    memset(o, 0, STATE_SIZE);
    o[0] = (uint8_t)gs.seed; o[1] = (uint8_t)(gs.seed >> 8);
    o[2] = (uint8_t)gs.score; o[3] = (uint8_t)(gs.score >> 8);
    o[4] = (uint8_t)gs.clock; o[5] = (uint8_t)(gs.clock >> 8);
    o[6] = gs.level; o[7] = gs.energy; o[8] = gs.credit;
    memcpy(o + 0x09, gs.obj_state, 10);
    memcpy(o + 0x13, gs.obj_op, 10);
    memcpy(o + 0x1D, gs.obj_q, 10);
    memcpy(o + 0x27, gs.obj_wrong, 10);
    memcpy(o + 0x31, gs.clue, 20);
    memcpy(o + 0x45, gs.code, 4);
    o[0x49] = (uint8_t)gs.pieces; o[0x4A] = (uint8_t)(gs.pieces >> 8);
    o[0x4B] = (uint8_t)gs.rule; o[0x4C] = (uint8_t)(gs.rule >> 8);
    memcpy(o + 0x4D, gs.hints, 18);
    o[0x5F] = (uint8_t)gs.unused5f; o[0x60] = (uint8_t)(gs.unused5f >> 8);
    o[0x61] = (uint8_t)gs.hideout; o[0x62] = (uint8_t)(gs.hideout >> 8);
}
void gs_unpack(const uint8_t *i) {
    memset(&gs, 0, sizeof gs);
    gs.seed = rd16(i); gs.score = rd16(i + 2); gs.clock = rd16(i + 4);
    gs.level = i[6]; gs.energy = i[7]; gs.credit = i[8];
    memcpy(gs.obj_state, i + 0x09, 10);
    memcpy(gs.obj_op, i + 0x13, 10);
    memcpy(gs.obj_q, i + 0x1D, 10);
    memcpy(gs.obj_wrong, i + 0x27, 10);
    memcpy(gs.clue, i + 0x31, 20);
    memcpy(gs.code, i + 0x45, 4);
    gs.pieces = rd16(i + 0x49); gs.rule = rd16(i + 0x4B);
    memcpy(gs.hints, i + 0x4D, 18);
    gs.unused5f = rd16(i + 0x5F); gs.hideout = rd16(i + 0x61);
}

/* ------------------------------------------------------------------ session */
uint8_t *ses_rec(void) { return ses.player >= 0 ? savedb_player(&ses.db, ses.player) : NULL; }
uint32_t ses_lifetime(void) { return ses.player >= 0 ? savedb_score(&ses.db, ses.player) : 0; }
void ses_write_record(void) {
    uint8_t *r = ses_rec();
    if (!r) return;
    r[REC_SFX] = (uint8_t)ses.sfx; r[REC_MUSIC] = (uint8_t)ses.music; r[REC_VOICE] = (uint8_t)ses.voice;
    r[REC_OPTIONS] = (uint8_t)((ses.calc ? 1 : 0) | (ses.hints ? 2 : 0) | (ses.clock_on ? 4 : 0));
    memcpy(r + REC_GAME_CUST, ses.cust_game, 13);
    memcpy(r + REC_DRILL_CUST, ses.cust_drill, 13);
    for (int i = 0; i < 10; i++) { r[REC_SEEDS + i * 2] = (uint8_t)ses.math_seed[i]; r[REC_SEEDS + i * 2 + 1] = (uint8_t)(ses.math_seed[i] >> 8); }
    savedb_sync_entry(&ses.db, ses.player);
    savedb_flush(&ses.db);
}

void game_abort_to_signin(void) { gs_live = 0; clock_pause_reset(); menu_reset_service(); plat_hold_present(0); menu_set_current(-1); longjmp(jb_signin, 1); }        /* "none": the bar is empty until the next screen sets its menu */
void game_abort_to_newgame(void) { gs_live = 0; clock_pause_reset(); menu_reset_service(); plat_hold_present(0); menu_set_current(-1); longjmp(jb_newgame, 1); }

/* -------------------------------------------------------------------- clock */
static int pause_count;
int g_clock_box_live = 1;   /* the clock box shows the live clock: opening a menu stamps PAUSE there (03 section 3.0a A/C). 0 from the score summary / midnight / Continue prompt */
void clock_pause_reset(void) { pause_count = 0; }   /* after a longjmp out of a menu command: the original's leaked pause counter is not reproduced (3.0a D) */
static int64_t clock_deadline, clock_saved_remaining;
static int clock_frozen;
void clock_freeze(int v) { clock_frozen = v; }

int clock_paused(void) { return pause_count > 0; }
void clock_reset_deadline(void) { clock_deadline = plat_now() + 328; }
void clock_show_pause(int v) { (void)v; }
int gs_clock_off_display(void) { return !ses.clock_on || (pause_count > 0 && !ses.drill_mode); }

void clock_pause(void) {
    if (pause_count++ == 0) {
        clock_saved_remaining = clock_deadline - plat_now();
        if (!ses.drill_mode && ses.player >= 0 && g_clock_box_live) hud_clock();   /* PAUSE cel while paused */
    }
    audio_pause();
    if (ses.drill_mode) drill_pause_hook(1);
}
void clock_resume(void) {
    if (pause_count <= 0) return;
    audio_resume();
    if (ses.drill_mode) drill_pause_hook(0);
    if (--pause_count == 0) {
        clock_deadline = plat_now() + (clock_saved_remaining > 0 ? clock_saved_remaining : 0);
        if (!ses.drill_mode && ses.player >= 0 && g_clock_box_live) hud_clock();
    }
}

void clock_add_minutes(int n) {
    if (!ses.clock_on) return;
    int old = gs.clock;
    int c = gs.clock + n;
    int h = c / 100, m = c % 100;
    while (m >= 60) { m -= 60; h++; }
    c = h * 100 + m;
    if (c > 1159) c = 1200;
    gs.clock = (uint16_t)c;
    hud_clock();
    if (c / 100 != old / 100 || c == 1150) snd_play(1725, 3, 0);
}

void clock_check(void) {
    if (clock_paused() || clock_frozen || !ses.clock_on) return;
    if (plat_now() > clock_deadline) {
        clock_add_minutes(1);
        clock_deadline = plat_now() + 328;
        if (gs.clock >= 1200) out_of_time();
        if (gs.clock == 902 && !ag.moved) hint_show(0, 0);
    }
}

/* -------------------------------------------------------------------- hints */
int hint_show(int n, int armed) {
    if (n < 0 || n >= 18) return 0;
    if (gs.hints[n] == 0xFF) return 0;
    if (armed) { if (gs.hints[n] == 0) gs.hints[n] = 1; return 0; }
    gs.hints[n] = 0xFF;
    if (!ses.hints) return 0;
    clock_pause();
    dlg_show(1000 + n, 1000 + n);
    clock_resume();
    return 1;
}
void hint_poll_queue(void) {
    if (ag.telly_state || obj[OB_TRICK].active || ag.pstate > 2) return;
    for (int i = 0; i < 18; i++)
        if (gs.hints[i] == 1) { hint_show(i, 0); return; }
}

/* -------------------------------------------------------------------- world */
Hot hot[16][4];
static Hot hot0[16][4];
AnimDef animdef[35];
Obj obj[6];
static Obj obj0[6];

const int COLPX[41] = {0, 13, 25, 38, 51, 64, 77, 89, 102, 115, 128, 141, 153, 166, 179, 192, 205, 217, 230, 243, 256,
                       269, 281, 294, 307, 320, 333, 345, 358, 371, 384, 397, 409, 422, 435, 448, 461, 473, 486, 499, 512};
const int ROOM_AREA[5] = {10, 12, 13, 14, 15};
const int ROOM_DOOR_AREA[5] = {1, 3, 6, 7, 8};
const int CLUES_PER[4] = {4, 4, 2, 2};

int px_x(int col) { if (col < 0) return -COLPX[-col > 40 ? 40 : -col]; return COLPX[col > 40 ? 40 : col]; }
int px_y(int du) { return (du * 192 + 50) / 100; }

int world_load(void) {
    size_t len;
    const uint8_t *b = res_get("BINA", 5000, &len);
    if (!b || len < 0x70E + 6 * 18) return -1;
    for (int a = 0; a < 16; a++)
        for (int s = 0; s < 4; s++) {
            const uint8_t *p = b + (a * 4 + s) * 14;
            hot0[a][s] = (Hot){rds16(p), rds16(p + 2), rds16(p + 4), rds16(p + 6), rds16(p + 8), rds16(p + 10), rds16(p + 12)};
        }
    for (int i = 0; i < 35; i++) {
        const uint8_t *p = b + 0x380 + i * 26;
        animdef[i].n = rds16(p);
        animdef[i].cur = rds16(p + 2);
        for (int k = 0; k < 9; k++) animdef[i].cel[k] = rds16(p + 4 + k * 2);
        animdef[i].tick = rds16(p + 22);
        animdef[i].ticks = rds16(p + 24);
    }
    for (int i = 0; i < 6; i++) {
        const uint8_t *p = b + 0x70E + i * 18;
        obj0[i] = (Obj){rds16(p), rds16(p + 2), rds16(p + 4), rds16(p + 6), rds16(p + 8), rds16(p + 10), rds16(p + 12),
                        rds16(p + 14), rds16(p + 16), 0, 0, 0};
    }
    memcpy(hot, hot0, sizeof hot);
    memcpy(obj, obj0, sizeof obj);
    return 0;
}
void world_reset_objects(void) {
    memcpy(obj, obj0, sizeof obj);
    memcpy(hot, hot0, sizeof hot);
}
int hot_type_at(int area, int slot) { return hot[area][slot].type; }
