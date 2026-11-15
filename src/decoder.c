/* decoder.c - the decoder screen and Decide (spec 05 section 3.7, 07 A section 5) */
#include "game.h"

static const int HL_Y[5] = {45, 90, 137, 184, 231};
static const int DECIDE_DLG[5] = {1017, 1018, 1019, 1020, 1021};
static const int DECIDE_SPEECH[5] = {1836, 1835, 1829, 1834, 1837};


static Aseq *H(void) { return aseq_load(200); }

static void draw_buttons(int focus) {
    Aseq *b = aseq_load(6002);
    static const int bx[3] = {33, 115, 198};
    static const int even[3] = {6, 2, 4};
    for (int i = 0; i < 3; i++) ui_stamp(b, even[i] + (i == focus ? 0 : 1), bx[i], 312, 0);
}

static void highlight(int row, int on) {
    if (on) ui_recolour(293, HL_Y[row], 470, HL_Y[row] + 41, C_SCREENBLACK, C_RED);
    else ui_recolour(293, HL_Y[row], 470, HL_Y[row] + 41, C_RED, C_SCREENBLACK);
}

static void animate_pieces(void) {
    for (int i = 0; i < 4; i++) {
        int x = 58 + 43 * i, y = 198;
        snd_play(1712, 5, SND_LOOP);                      /* looping; ignored if already playing */
        if (gs.code[i] & 0x80) continue;                  /* empty slot: nothing drawn */
        for (int c = 78; c <= 97; c++) { ui_stamp(H(), c, x, y, 0); plat_present(); plat_wait(1); }
        snd_stop_all();
        plat_wait(24);
        ui_stamp(H(), 97, x, y, 0);
        plat_wait(32);
        ui_stamp(H(), 78 + (gs.code[i] & 0x7F), x, y, 0);
        plat_present();
    }
}
static void animate_clues(void) {
    for (int r = 0; r < 5; r++) {
        if (!(gs.clue[r][0] & 0x80)) {
            snd_play(1712, 5, SND_LOOP);
            for (int s = 0; s < 20; s++) {
                for (int k = 0; k < 4; k++) {
                    if (gs.clue[r][k] & 0x80) continue;
                    ui_stamp(H(), 78 + (((gs.clue[r][k] & 0x7F) + s) % 20), 298 + 43 * k, 55 + 47 * r, 0);
                }
                plat_present();
                plat_wait(1);
            }
            plat_wait(12);
            for (int k = 0; k < 4; k++)
                if (!(gs.clue[r][k] & 0x80)) ui_stamp(H(), 78 + (gs.clue[r][k] & 0x7F), 298 + 43 * k, 55 + 47 * r, 0);
            plat_present();
        }
        snd_stop_all();
    }
}

static int decide_allowed(void) {
    int pieces = 0, rooms_with_all = 0, rooms_with_any = 0;
    for (int i = 0; i < 4; i++) if (!(gs.code[i] & 0x80)) pieces++;
    for (int r = 0; r < 5; r++) {
        int n = 0;
        for (int k = 0; k < 4; k++) if (!(gs.clue[r][k] & 0x80)) n++;
        if (n == 4) rooms_with_all++;
        if (n >= 1) rooms_with_any++;
    }
    if (pieces == 4 && rooms_with_all >= 1) return 1;
    if (pieces >= 1 && rooms_with_any >= 4) return 1;
    if (rooms_with_all == 5 && gs.energy == 0 && gs.credit == 0) return 1;
    return 0;
}

/* choose a room; returns row or -1 on cancel */
static int pick_room(int *row_io) {
    int row = *row_io;       /* -1 = nothing highlighted (right after the decoder opens) */
    ui_fill(19, 300, 294, 374, C_WHITE, 0);
    ui_text_wrap(F_SERIF, 29, 313, 289, "Click on the name of the room that you think is the Master of Mischief's hideout.", 0, 0);
    ui_stamp(aseq_load(8551), 0, 212, 340, 0);
    plat_present();
    plat_key_flush();
    plat_click_flush();
    for (;;) {
        int k = plat_key_pop(), cx, cy;
        if (!k && plat_click_pop(&cx, &cy)) {
            if (cx >= 212 && cx < 284 && cy >= 340 && cy < 362) return -1;
            for (int r = 0; r < 5; r++)
                if (cx >= 293 && cx < 470 && cy >= 43 + 47 * r && cy < 43 + 47 * r + 41) {
                    if (row >= 0) highlight(row, 0);
                    row = r;
                    highlight(row, 1);
                    *row_io = row;
                    return row;
                }
            continue;
        }
        if (!k) { plat_wait(2); continue; }
        if (k == K_ESC) { *row_io = row; return -1; }
        if (k == K_UP) { if (row >= 0) highlight(row, 0); row = row < 0 ? 4 : (row + 4) % 5; highlight(row, 1); plat_present(); }
        else if (k == K_DOWN) { if (row >= 0) highlight(row, 0); row = row < 0 ? 0 : (row + 1) % 5; highlight(row, 1); plat_present(); }
        else if (k == K_ENTER) { *row_io = row; return row >= 0 ? row : -1; }
    }
}

void decoder_run(void) {
    clock_show_pause(1);
    hud_rebuild();
    ui_stamp_picture(6000, 11, 35, ST_BACK);
    music_stop();                                  /* the caller's frame loop restarts it after the decoder closes */
    snd_stop_all();
    dbg_screen = "decoder";
    int focus = 0, row = -1;
    draw_buttons(focus);
    plat_present();
    animate_pieces();
    animate_clues();
    plat_present();
    plat_key_flush();
    plat_click_flush();
    for (;;) {
        dbg_dec.focus = focus; dbg_dec.row = row;
        int k = plat_key_pop(), cx, cy;
        int act = -1;
        if (!k && plat_click_pop(&cx, &cy)) {
            static const int bx[3] = {33, 115, 198};
            for (int i = 0; i < 3; i++)
                if (cx >= bx[i] && cx < bx[i] + 66 && cy >= 312 && cy < 366) { snd_play(1756, 5, 0); act = i; }
            if (act < 0) {
                for (int r = 0; r < 5; r++)
                    if (cx >= 293 && cx < 470 && cy >= 43 + 47 * r && cy < 43 + 47 * r + 41) { highlight(row, 0); row = r; highlight(row, 1); plat_present(); }
            }
        } else if (!k) { plat_wait(2); continue; }
        else if (k == K_ESC || k == 's' || k == 'S') act = 0;
        else if (k == 'm' || k == 'M') act = 1;
        else if (k == 'd' || k == 'D') act = 2;
        else if (k == K_UP) { if (row >= 0) highlight(row, 0); row = row < 0 ? 4 : (row + 4) % 5; highlight(row, 1); plat_present(); }
        else if (k == K_DOWN) { if (row >= 0) highlight(row, 0); row = row < 0 ? 0 : (row + 1) % 5; highlight(row, 1); plat_present(); }
        else if (k == K_TAB || k == K_RIGHT) { focus = (focus + 1) % 3; draw_buttons(focus); plat_present(); }
        else if (k == K_LEFT) { focus = (focus + 2) % 3; draw_buttons(focus); plat_present(); }
        else if (k == K_ENTER) act = focus;
        if (act == 0) break;
        if (act == 1) {
            dlg_show(25000 + gs.rule, 1800 + gs.rule);
            draw_buttons(focus);
            plat_present();
        } else if (act == 2) {
            if (!decide_allowed()) { dlg_show(25003, 0); draw_buttons(focus); plat_present(); continue; }
            int r = pick_room(&row);
            if (r >= 0) {
                if (dlg_yesno(DECIDE_DLG[r], DECIDE_SPEECH[r])) {
                    ending_decide(r);
                }
            }
            ui_fill(19, 300, 326, 374, C_MSGBLUE, 0);
            ui_stamp_picture(1000, 0, 0, 0);
            hud_clock(); hud_energy(); hud_score();
            draw_buttons(focus);
            plat_present();
        }
    }
    clock_show_pause(0);
    snd_stop(1712);
    snd_stop(1756);
    dbg_screen = "hall";
}
