/* intro.c - title screen, attract loop and the hideout narration (spec 07 part B section 1) */
#include "game.h"

static void tick_aos(void) {
    g_eng.now = plat_now();
    eng_run_aos();
    eng_render();
    plat_present();
}

static int intro_skip, intro_key;

static int intro_hook(AO *ao, int code, int a, int b, int d) {
    if (code == -500) snd_play_ctx(a, b, 0, SC_INTRO);
    else if (code == -501) snd_stop(a);
    return 0;
}

static void paint_title(void) {
    dbg_screen = "title";
    canvas_fill(g_eng.back, 0);
    canvas_fill(g_eng.work, 0);
    ui_stamp_picture(1700, 0, 0, ST_BACK);
    plat_present();
}

/* returns: 0 normal end, 1 key pressed (abort), -1 = quit */
static int poll_abort(void) {
    int k = plat_key_pop(), cx, cy;
    if (!k && plat_click_pop(&cx, &cy)) k = K_ENTER;
    if (!k) return 0;
    if (k == K_ESC) { evlog_close(); exit(0); }
    if (k == K_ENTER || k == K_SPACE) { snd_stop(1727); intro_skip = 1; return 1; }
    return 0;
}

/* run an attract clip (ASEQ 1700 sequence `seq` at fps 5); returns 1 if aborted */
static int attract_clip(int seq) {
    dbg_screen = "attract";
    AO *a = ao_create(1700);
    if (!a) return 0;
    ao_set_fps(a, 5);
    ao_start(a, seq);
    int aborted = 0;
    while (a->state == AO_RUNNING) {
        tick_aos();
        if (poll_abort()) { aborted = 1; break; }
        plat_wait(1);
    }
    ao_destroy(a);
    return aborted;
}

/* ---------------------------------------------------------------- narration */
static AO *bodies[5], *heads[11], *telly, *anten, *cur_body, *cur_head;
static int toggle8;

static void anten_cb(AO *ao) {
    if (cur_head) ao_set_fps(cur_head, toggle8 ? 9 : 8);
    toggle8 = !toggle8;
    if (telly) {
        const SeqEnt *e = (ao->frame_idx < ao->seq->n) ? &ao->seq->e[ao->frame_idx] : NULL;
        if (e && e->b) ao_set_pos(telly, ao->x + 19, ao->y + 3);
        else ao_set_pos(telly, ao->x + 20, ao->y);
    }
}

static AO *mk(int id, int x, int y, int z, int fps) {
    AO *a = ao_create(id);
    if (!a) return NULL;
    a->x = x; a->y = y; a->z = z;
    if (fps) ao_set_fps(a, fps);
    return a;
}

static void wait_ao(AO *a) {
    int guard = 0;
    while (a && a->state == AO_RUNNING && guard++ < 20000) { tick_aos(); plat_wait(1); }
}

static void narration(void) {
    dbg_screen = "narration";
    static const int BODY[11][4] = {{0, 0, 0, 0}, {2, 2, 2, 5}, {1, 2, 5, 0}, {1, 2, 2, 3}, {2, 2, 5, 0}, {1, 0, 0, 0},
                                    {5, 0, 0, 0}, {0, 0, 0, 0}, {1, 2, 2, 5}, {1, 1, 1, 5}, {1, 3, 0, 0}};
    static const int SPEECH[11] = {1862, 1863, 1864, 1865, 1866, 1867, 1868, 0, 1869, 1870, 0};
    canvas_fill(g_eng.back, 0);
    ui_stamp_picture(15000, 0, 0, ST_BACK);
    for (int i = 0; i < 5; i++) bodies[i] = mk(12001 + i, 189, 209, 20, 8);
    for (int i = 0; i < 11; i++) heads[i] = mk(11001 + i, 182, 153, 25, 9);
    telly = mk(10001, 420, 240, 20, 8);
    anten = mk(12008, 440, 240, 25, 0);
    if (anten) anten->callback = anten_cb;
    toggle8 = 0;
    cur_body = bodies[0];
    cur_head = heads[9];
    ao_start(cur_body, 0);
    ao_start(cur_head, 0);
    ao_start(telly, 0);
    ao_start(anten, 0);
    int s = 0, F = 0, k = -1, line_active = 1;
    int done = 0;
    plat_key_flush();
    plat_click_flush();
    while (!done) {
        /* head follows body */
        if (cur_body && cur_head && cur_body->frame_idx < cur_body->seq->n) {
            const SeqEnt *e = &cur_body->seq->e[cur_body->frame_idx];
            ao_set_pos(cur_head, cur_body->x + e->a - 7, cur_body->y + e->b - 56);
        }
        /* 1: body */
        if (cur_body && cur_body->state != AO_RUNNING) {
            k++;
            if (k < 4 && s < 11) {
                int b = BODY[s][k];
                if (b) {
                    ao_stop(cur_body);
                    cur_body = bodies[b - 1];
                    ao_start(cur_body, 0);
                }
            }
        }
        /* 2: speech */
        if (line_active && !speech_playing()) {
            line_active = 0;
            F = 1;
            ao_stop(cur_head);
            cur_head = heads[s < 8 ? 9 : 10];
            ao_start(cur_head, 0);
        }
        /* 3: next line when the idle head has stopped */
        if (F && cur_head->state != AO_RUNNING) {
            F = 0;
            k = -1;
            if (s == 10) { done = 1; break; }
            if (s == 7) {
                /* Telly interlude */
                ao_stop(telly); ao_stop(anten);
                ao_set_fps(telly, 10);
                ao_start(telly, 1);
                snd_play(11001, 30, 0);
                wait_ao(telly);
                AO *disc = mk(12007, 299, 261, 30, 8);
                ao_start(disc, 0);
                ao_stop(telly);
                ao_set_seq(telly, 0);
                ao_set_fps(telly, 8);
                ao_start(telly, 0);
                ao_start(anten, 0);
                wait_ao(disc);
                ao_destroy(disc);
                AO *st = mk(12006, 182, 153, 25, 10);
                ao_hide(cur_head); ao_hide(cur_body);
                ao_start(st, 0);
                snd_play(11000, 35, 0);
                wait_ao(st);
                ao_destroy(st);
                ao_show(cur_head); ao_show(cur_body);
                F = 1;
                ao_stop(cur_head);
                cur_head = heads[10];
                ao_start(cur_head, 0);
                s++;
                continue;
            }
            ao_stop(cur_head);
            cur_head = heads[s <= 6 ? s : (s == 8 ? 7 : 8)];
            ao_start(cur_head, 0);
            if (SPEECH[s]) { speech_play(SPEECH[s]); line_active = 1; }
            s++;
        }
        tick_aos();
        int kk = plat_key_pop(), cx, cy;
        if (!kk && plat_click_pop(&cx, &cy)) {
            speech_stop(); intro_key = 1; done = 1;
        } else if (kk == K_ESC) { evlog_close(); exit(0); }
        else if (kk == K_SPACE) { speech_stop(); intro_skip = 1; done = 1; }
        else if (kk == K_ENTER) {
            intro_key = 1;
            if (cur_head) ao_stop(cur_head);
            if (speech_playing()) { speech_stop(); done = 1; }
        }
        plat_wait(1);
    }
    speech_stop();
    ao_destroy(telly); ao_destroy(anten);
    for (int i = 0; i < 5; i++) ao_destroy(bodies[i]);
    for (int i = 0; i < 11; i++) ao_destroy(heads[i]);
    telly = anten = cur_body = cur_head = NULL;
}

void intro_run(void) {
    intro_skip = 0;
    intro_key = 0;
    g_menu_grey = 1;                 /* the title and attract loop: the titles open, every item is greyed (03 section 3.0a C) */
    g_eng.user_hook = intro_hook;
    for (;;) {
        paint_title();
        music_play(1724, 1);
        int64_t until = plat_now() + 3000;
        int got = 0;
        plat_key_flush();
        plat_click_flush();
        while (plat_now() < until && !got) {
            int k = plat_key_pop(), cx, cy;
            if (!k && plat_click_pop(&cx, &cy)) k = K_ENTER;
            if (k == K_ESC) { evlog_close(); exit(0); }
            else if (k == K_SPACE) { intro_skip = 1; got = 1; }
            else if (k) { intro_key = 1; got = 1; }
            else plat_wait(2);
        }
        music_stop();
        if (!intro_skip) narration();
        if (!intro_skip) {
            music_play(1724, 1);
            paint_title();
            int aborted = attract_clip(0);
            if (!aborted) {
                ui_stamp_picture(1700, 0, 0, ST_BACK);
                ui_stamp(aseq_load(1700), 27, 0, 0, ST_BACK);
                plat_present();
                attract_clip(1);
            }
            music_stop();
        }
        if (intro_skip || intro_key) break;
    }
    g_eng.user_hook = NULL;
    g_menu_grey = 0;
    /* free 1724/1727 : nothing to do */
}
