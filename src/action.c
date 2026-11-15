/* action.c - the real-time game: hall, rooms, player, Telly, LiveWire, zapper, energy (spec 04) */
#include "game.h"

Action ag;
const char *dbg_screen = "hall";
struct DbgDrill dbg_drill;
struct DbgChal dbg_chal;
struct DbgDec dbg_dec;
static int64_t frame_deadline;

#define TELLY_WALK 3
#define TELLY_STOP 4
#define TELLY_THROW 5
#define TELLY_ZAPPED 7
#define TELLY_MATERIALISE 8
#define TELLY_DEMAT 9

static const int STOP_PCT[4] = {60, 50, 40, 30};
static const int RESUME_PCT[4] = {15, 12, 11, 9};
static const int RELEASE[4] = {2, 0, 3, 3};
static const int TRICK_Y[4] = {81, 91, 115, 118};
static const int TRICK_W[4] = {5, 2, 4, 3};
static const int TRICK_SND[4] = {1700, 1727, 1706, 1750};
static const int TRICK_SPLAT[4] = {210, 212, 209, 211};
static const int EM_CAND[9][2] = {{0, 2}, {1, 2}, {3, 2}, {4, 0}, {5, 2}, {6, 0}, {7, 0}, {8, 2}, {9, 0}};

static int tp(int field) { return tier_params[field][ses.tier]; }

/* ------------------------------------------------------------ rng helpers */
static int rnd(int lo, int hi) { return rnga_range(lo, hi); }

/* ------------------------------------------------------------ animation objects */
static void obj_set_anim(Obj *o, int st) {          /* restarts the shared animation (player, LiveWire, trick) */
    o->anim = st;
    o->frame = 0;
    animdef[st].cur = 0;
    animdef[st].tick = animdef[st].ticks > 0 ? animdef[st].ticks : 1;
}
static void obj_use_anim(Obj *o, int st) { o->anim = st; }   /* Telly: the phase is global and never reset (spec 04 6.3) */
/* x += dx; y += dy; record the current cel; step the shared frame counter */
static void obj_advance(Obj *o, int dx, int dy) {
    o->x += dx;
    o->y += dy;
    AnimDef *a = &animdef[o->anim];
    if (a->n > 0) o->cel = a->cel[a->cur % a->n];
    if (--a->tick <= 0) {
        a->tick = a->ticks > 0 ? a->ticks : 1;
        if (a->n > 0) a->cur = (a->cur + 1) % a->n;
    }
    o->frame = a->cur;
}

int player_world_col(void) { return ag.in_room ? obj[OB_PLAYER].x : ag.scroll + obj[OB_PLAYER].x; }
static int cur_area(void) { return ag.in_room ? ag.area : player_world_col() / 38; }

/* ------------------------------------------------------------ collision */
static int gap(Obj *a, Obj *b) {
    if (!a->active || !b->active) return 39;
    int ax = a->x + a->hit_off, bx = b->x + b->hit_off;
    return (ax <= bx) ? bx - (ax + a->hit_w) : ax - (bx + b->hit_w);
}
static int touching(Obj *a, Obj *b) { return gap(a, b) <= 0; }

/* ------------------------------------------------------------ hot spots */
int hot_current(int *slot_out) {
    int area = cur_area();
    if (area < 0 || area > 15) return -1;
    int px = player_world_col();
    int type = -1, slot = -1;
    for (int s = 0; s < 4; s++) {
        Hot *h = &hot[area][s];
        if (h->type == -1) continue;
        int start = ag.in_room ? h->col : area * 38 + h->col;
        int width = h->f3 - h->col;
        if ((start <= px && px < start + width) || (start > px && px + 6 > start)) { type = h->type; slot = s; }
    }
    if (slot_out) *slot_out = slot;
    return type;
}

/* ------------------------------------------------------------ player state */
static void set_state(int s, int dur, int dx, int dy) {
    if (ag.pstate > 2 && ag.premaining > 0 && s < 9) return;
    ag.pstate = s;
    ag.premaining = dur < 0 ? animdef[s].n : dur;
    ag.pdx = dx;
    ag.pdy = dy;
    obj_set_anim(&obj[OB_PLAYER], s);
    if (s != 1) ag.moved = 1;
}

/* ------------------------------------------------------------ scrolling */
static int scroll_min(void) { return (player_world_col() / 38 >= 5 || ag.scroll >= 190) ? 190 : 0; }
static int scroll_max(void) { return (ag.scroll >= 190) ? 9 * 38 : 4 * 38; }

static void shift_objects(int n) {  /* view moved right by n: screen x decreases */
    obj[OB_PLAYER].x -= n;
    if (ag.telly_state) obj[OB_TELLY].x -= n;
    if (obj[OB_TRICK].active) obj[OB_TRICK].x -= n;
}

static void hall_scroll_check(void) {
    if (ag.in_room || !ag.pstate) return;
    Obj *p = &obj[OB_PLAYER];
    int n = ag.pdx < 0 ? -ag.pdx : ag.pdx;
    if (n == 0) return;
    int mv = ag.pdx * ag.facing;
    if (mv < 0 && p->x - n < 6) {
        int ex = 6 - (p->x - n);
        int room = ag.scroll - scroll_min();
        if (ex > room) ex = room;
        if (ex > 0) { ag.scroll -= ex; shift_objects(-ex); hall_draw_full(ag.scroll); }
    } else if (mv > 0 && p->x + n >= 26) {
        int ex = p->x + n - 26;
        int room = scroll_max() - ag.scroll;
        if (ex > room) ex = room;
        if (ex > 0) { ag.scroll += ex; shift_objects(ex); hall_draw_full(ag.scroll); }
    }
}

/* ------------------------------------------------------------ player update */
static void player_update(void) {
    if (!ag.pstate) return;
    Obj *p = &obj[OB_PLAYER];
    int target = p->x + ag.pdx * ag.facing;
    if (target <= 1) ag.pdx = p->x - 1;
    else if (target >= 33) ag.pdx = 33 - p->x;
    if (ag.pdx == 0 && ag.pstate == 2) {
        set_state(9, -1, 0, 0);
        ag.premaining--;
        snd_play(1720, 4, 0);   /* bump */
    }
    if (ag.pstate == 8 && ag.premaining == animdef[8].n / 2) ag.pdy = -ag.pdy;
    obj_advance(p, ag.pdx * ag.facing, ag.pdy);
    if (ag.pstate > 2) {
        ag.premaining--;
        if (ag.pstate == 9 && ag.premaining == 3) snd_play(1720, 3, 0);   /* thud */
        if (ag.premaining < 1) {
            if (ag.pstate == 9 || ag.pstate == 27) {
                snd_play(1719, 5, 0);
                snd_play(1721, 4, 0);
                set_state(28, -1, 0, 0);
            } else {
                if (ag.pstate == 26 || ag.pstate == 28 || ag.pstate == 8) snd_play(1726, 2, 0);   /* landing */
                set_state(1, 1, 0, 0);
            }
        }
    }
}

/* ------------------------------------------------------------ Telly */
static void telly_remove(void) {
    if (ag.telly_state) { snd_stop(1754); snd_stop(1753); }
    dosint_flag(DF_TELLY_FLASH, 0);
    ag.telly_state = 0;
    obj[OB_TELLY].active = 0;
}
static void trick_remove(void) {
    if (obj[OB_TRICK].active && ag.trick_type) snd_stop(TRICK_SND[ag.trick_type - 22]);
    obj[OB_TRICK].active = 0;
    ag.trick_type = 0;
}

static void telly_spawn_check(void) {
    if (ag.in_room || ag.telly_state || !ag.normal_play || !ag.moved || obj[OB_TRICK].active) return;
    if (!(rnd(1, 1000) < tp(TP_TELLY_SPAWN))) return;
    Obj *t = &obj[OB_TELLY], *p = &obj[OB_PLAYER];
    ag.telly_lit = (gs.pieces < 4) && rnd(1, 100) > tp(TP_TELLY_LIT);
    if (p->x + 3 > 20) { t->x = -3; ag.telly_dir = 1; }
    else { t->x = 35; ag.telly_dir = -1; }
    ag.telly_mirror = ag.telly_dir > 0;
    int r = rnd(0, 100);
    int area = ag.scroll / 38;
    t->active = 1;
    t->y = 77;
    if (r < 50 || (area % 5 == 0 && ag.telly_dir == 1) || (area % 5 == 4 && ag.telly_dir == -1)) {
        ag.telly_state = TELLY_MATERIALISE;
        t->x = p->x - (tp(TP_TELLY_POPIN) + 8) * ag.telly_dir;
        obj_use_anim(t, 33);
        snd_play(1753, 2, 0);                   /* pop-in sound on the spawn frame only; the hum starts next frame */
    } else {
        ag.telly_state = TELLY_WALK;
        obj_use_anim(t, 10);
        snd_play(1754, 2, 0);                   /* walk-in: the hum starts on the spawn frame */
    }
    ag.telly_belly_ok = (ag.telly_state == TELLY_WALK);
    obj_advance(t, 0, 0);                       /* spawn frame: first cel drawn, no movement */
}

static void trick_launch(void) {
    Obj *t = &obj[OB_TELLY], *k = &obj[OB_TRICK];
    int ty = ag.telly_throw_type;       /* 22..25 */
    int idx = ty - 22;
    k->active = 1;
    k->y = TRICK_Y[idx];
    k->hit_off = 0;
    k->hit_w = TRICK_W[idx];
    if (ag.telly_dir < 0) k->x = (ty == 22 || ty == 24) ? t->x - 4 : t->x;
    else k->x = t->x + 4;
    ag.trick_type = ty;
    ag.trick_dir = ag.telly_dir;
    k->f8 = rnd(tp(TP_TELLY_SPEED), tp(TP_TELLY_SPEED) + 1) * ag.telly_dir;   /* signed speed, fixed for the flight */
    k->f6 = 0;
    obj_set_anim(k, ty);
    snd_play(TRICK_SND[idx], 3, SND_LOOP);
}

static void telly_update(void) {
    if (!ag.telly_state) return;
    Obj *t = &obj[OB_TELLY], *p = &obj[OB_PLAYER];
    int g = ses.tier;
    ag.telly_belly_ok = 1;
    if (ag.telly_state != TELLY_ZAPPED) snd_play(1754, 2, 0);       /* hum, every update except while flashing */
    if (!ag.in_room && ag.telly_state >= 3) {
        if (ag.telly_dir == -1 && t->x + 8 < p->x) ag.telly_dir = 1;
        else if (ag.telly_dir == 1 && t->x - 8 > p->x) ag.telly_dir = -1;
    }
    ag.telly_mirror = ag.telly_dir > 0;                             /* art faces left natively */
    if (ag.telly_state < 5) {
        if (ag.telly_state == TELLY_STOP && rnd(0, 100) < RESUME_PCT[g]) { ag.telly_state = TELLY_WALK; }
        else if (rnd(1, 100) < STOP_PCT[g] && t->x >= 6 && t->x <= 18) { ag.telly_state = TELLY_STOP; }
    }
    if (ag.telly_state == TELLY_STOP && rnd(1, 100) < tp(TP_TELLY_THROW) && !obj[OB_TRICK].active) {
        ag.telly_state = TELLY_THROW;
        ag.telly_throw_type = rnd(22, 25);
    }
    int speed = (ag.telly_state == TELLY_WALK) ? tp(TP_TELLY_SPEED) : 0;
    switch (ag.telly_state) {
    case TELLY_WALK:
        obj_use_anim(t, 10);
        obj_advance(t, ag.telly_dir * speed, 0);
        break;
    case TELLY_STOP:
        obj_use_anim(t, 11);
        obj_advance(t, 0, 0);
        break;
    case TELLY_THROW: {
        int idx = ag.telly_throw_type - 22;
        obj_use_anim(t, ag.telly_throw_type - 5);
        obj_advance(t, 0, 0);
        int i = animdef[t->anim].cur;                  /* index after advancing */
        if (i == 0) ag.telly_state = TELLY_STOP;       /* throw ends; belly shown again on this frame */
        if (i == RELEASE[idx] && !obj[OB_TRICK].active) trick_launch();
        break;
    }
    case TELLY_ZAPPED:                                   /* flashing: 9 frames */
        obj_use_anim(t, 34);
        obj_advance(t, 0, 0);
        if (--ag.telly_cnt <= 0) { ag.telly_state = TELLY_DEMAT; ag.telly_cnt = 5; dosint_flag(DF_TELLY_FLASH, 0); }
        break;
    case TELLY_DEMAT:                                    /* 5 frames drawn, removed on the 6th */
        obj_use_anim(t, 32);
        if (ag.telly_cnt <= 0) { telly_remove(); return; }
        obj_advance(t, 0, 0);
        ag.telly_cnt--;
        break;
    case TELLY_MATERIALISE:
        obj_use_anim(t, 33);
        obj_advance(t, 0, 0);
        if (animdef[33].cur == 0) { ag.telly_state = TELLY_WALK; ag.telly_belly_ok = 0; }   /* belly only from the first walking frame */
        break;
    }
    if (ag.telly_state >= 3 && (t->x < -3 || t->x > 35)) telly_remove();
}

static void trick_update(void) {
    Obj *k = &obj[OB_TRICK];
    if (!k->active) return;
    if (k->f6) { trick_remove(); return; }           /* drawn at the edge, erased on the next frame */
    int v = k->f8;
    if (v > 0 && 33 - k->x < v) v = k->f8 = 2;        /* edge slow-down only for rightward tricks */
    obj_advance(k, v, 0);
    if (k->x <= 1 || k->x >= 33) k->f6 = 1;
}

/* ------------------------------------------------------------ LiveWire */
static void wire_spawn_check(void) {
    Obj *w = &obj[OB_WIRE];
    if (w->active) return;
    if (!(rnd(1, 100) < tp(TP_LIVEWIRE))) return;
    w->active = 1;
    w->y = 113;
    if (obj[OB_PLAYER].x > 20) { w->x = 3; ag.wire_dir = 1; }
    else { w->x = 30; ag.wire_dir = -1; }
    obj_set_anim(w, 13);
}
static void wire_update(void) {
    Obj *w = &obj[OB_WIRE];
    if (!w->active) return;
    if (w->anim == 31) {                         /* vanish cels 10010-10014, one per frame, then removed */
        if (ag.zap_frame <= 0) { w->active = 0; return; }
        obj_advance(w, 0, 0);
        ag.zap_frame--;
        return;
    }
    if (ag.wire_dir > 0 && w->x > 25) ag.wire_dir = -1;
    else if (ag.wire_dir < 0 && w->x < 3) ag.wire_dir = 1;
    obj_advance(w, ag.wire_dir, 0);
    snd_play(1731, 2, 0);
}

/* ------------------------------------------------------------ drawing */

typedef struct Overlay { int ase, cel, x, y, mirror; } Overlay;
static Overlay ovl[4];
static int n_ovl;
static void ov_clear(void) { n_ovl = 0; }
static void ov_add(int ase, int cel, int x, int y, int mirror) {
    if (n_ovl < 4) ovl[n_ovl++] = (Overlay){ase, cel, x, y, mirror};
}
static void stop_everything(void) { snd_stop_all(); music_stop(); }

static void draw_scene(void) {
    /* restore the playfield from the background, then draw the objects: 0,1,2,3,4, belly overlay, 5 (spec 04 section 14.7) */
    ui_restore_rect(9, 35, 494, 249);
    Rect clip = {9, 35, 494, 249};
    for (int i = 0; i < 6; i++) {
        Obj *o = &obj[i];
        if (!o->active) continue;
        int cel = o->cel;
        Aseq *a = cel >= 10000 ? aseq_load(210) : aseq_load(200);
        const Cel *c = aseq_cel(a, cel >= 10000 ? cel - 10000 : cel);
        if (!c) continue;
        int mirror = 0, dxp = 0;
        if (i == OB_PLAYER) { mirror = ag.facing < 0; if (ag.pstate == 7 && ag.facing < 0) dxp = px_x(o->x - 4) - px_x(o->x); }
        else if (i == OB_BOLT) mirror = ag.facing < 0;
        else if (i == OB_TELLY) mirror = ag.telly_mirror;       /* art faces left natively */
        else if (i == OB_TRICK) mirror = ag.telly_mirror;       /* tricks copy Telly's current mirroring */
        else if (i == OB_WIRE) mirror = ag.wire_dir > 0;
        canvas_blit_cel(g_eng.work, c, px_x(o->x) + dxp, px_y(o->y), mirror, &clip);
        if (i == OB_TELLY && ag.telly_lit && ag.telly_belly_ok && (ag.telly_state == TELLY_WALK || ag.telly_state == TELLY_STOP)) {
            int bc = (ag.telly_state == TELLY_WALK) ? 204 + (o->cel - 100 >= 0 && o->cel - 100 < 4 ? o->cel - 100 : 0) : 208;
            const Cel *lc = aseq_cel(aseq_load(200), bc);
            if (lc) canvas_blit_cel(g_eng.work, lc, px_x(o->x), px_y(o->y + 20), mirror, &clip);
        }
    }
    for (int i = 0; i < n_ovl; i++) {
        const Cel *c = aseq_cel(aseq_load(ovl[i].ase), ovl[i].cel);
        if (c) canvas_blit_cel(g_eng.work, c, ovl[i].x, ovl[i].y, ovl[i].mirror, &clip);
    }
}

/* ------------------------------------------------------------ one frame of object updates */
static void update_objects(int spawn) {
    hall_scroll_check();
    telly_update();
    if (spawn && !ag.in_room) telly_spawn_check();
    player_update();
    trick_update();
    wire_update();
    if (ag.in_room && obj[OB_PROP].active) obj_advance(&obj[OB_PROP], 0, 0);
    draw_scene();
}

static void music_for_area(void) {
    if (ag.music_off) return;
    int want;
    if (ag.in_room) want = ((ag.hall_scroll_saved + ag.hall_x_saved) / 38 < 5) ? 1745 : 1746;
    else want = (player_world_col() / 38 < 5) ? 1728 : 1729;
    if (music_current() != want || !music_playing()) { music_stop(); music_play(want, 1); }
}

static void frame_tail(int collide);

/* A scripted animation step: normal update, no spawning, no input, no collisions. */
static void seq_frame(void) {
    music_for_area();
    update_objects(0);
    plat_hold_present(1);
    plat_wait_until(frame_deadline);
    plat_hold_present(0);
    plat_present();
    if (!clock_paused()) clock_check();
    frame_deadline = plat_now() + 7;
}

static void run_state(int s, int dur, int dx, int dy) {
    set_state(s, dur, dx, dy);
    int guard = 0;
    while (ag.pstate == s && guard++ < 64) seq_frame();
}

static void stand(void) { ag.pstate = 1; ag.premaining = 0; ag.pdx = ag.pdy = 0; obj_set_anim(&obj[OB_PLAYER], 1); }

/* ------------------------------------------------------------ energy */
static void add_energy(int d) {
    if ((int)gs.energy + d < 0) return;
    gs.energy = (uint8_t)(gs.energy + d);
    hud_energy();
    if (gs.energy <= 5) hint_show(12, 1);
}

/* ------------------------------------------------------------ view setup */
static void place_prop(void) {
    obj[OB_PROP].active = ag.in_room;
    if (ag.in_room) obj_set_anim(&obj[OB_PROP], 14);
}

void action_screen_rebuild(void) {
    dbg_screen = ag.in_room ? "room" : "hall";
    hud_rebuild();
    if (ag.in_room) room_draw(ag.area);
    else hall_draw_full(ag.scroll);
    ui_stamp_picture(1007, 33, 312, ST_BACK);
    place_prop();
    draw_scene();
    plat_present();
}

void game_enter_hall_view(void) {
    ag.in_room = 0;
    action_screen_rebuild();
}

/* ------------------------------------------------------------ door / room / elevator sequences */
/* room door animation DA(dir, playerCel): 3 steps, the door cel is stamped into the scene, the player keeps a fixed cel on top,
 * Telly/trick take one AI step and are drawn over everything (spec 04 section 5.4) */
static void door_anim(int px_door_x, int open, int player_cel) {
    for (int s2 = 0; s2 < 3; s2++) {
        int cel = open ? 134 + s2 : 136 - s2;
        const Cel *c = aseq_cel(aseq_load(200), cel);
        if (c) canvas_blit_cel(g_eng.back, c, px_door_x, 101, 0, &(Rect){0, 0, CANVAS_W, CANVAS_H});
        telly_update();
        trick_update();
        obj[OB_PLAYER].cel = player_cel;
        draw_scene();
        plat_present();
        plat_wait(7);
    }
}

static int local_col(void) { return player_world_col() - (player_world_col() / 38) * 38; }

/* auto-walk (hall) until the player's column within its area equals `target_local` */
static void auto_walk_hall(int target_local) {
    int guard = 0;
    while (local_col() != target_local && guard++ < 400) {
        int dir = (local_col() < target_local) ? 1 : -1;
        ag.facing = dir;
        if (ag.pstate != 2) set_state(2, 1, 1, 0);
        int before = player_world_col();
        seq_frame();
        if (player_world_col() == before && ag.pstate != 2) set_state(2, 1, 1, 0);
        if (player_world_col() == before && guard > 3) break;
    }
    stand();
}
static void auto_walk_room(int target_col) {
    int guard = 0;
    while (obj[OB_PLAYER].x != target_col && guard++ < 100) {
        int dir = obj[OB_PLAYER].x < target_col ? 1 : -1;
        int rel = dir * ag.facing;               /* facing unchanged: backwards when facing away */
        if (ag.pstate != 2) set_state(2, 1, rel, 0);
        ag.pdx = rel;
        seq_frame();
    }
    stand();
}

static void auto_walk_keep_facing(int target_local);

static void enter_room(int slot) {
    int area = cur_area();
    Hot *h = &hot[area][slot];
    int door_world = area * 38 + h->col;
    stop_everything();
    ag.music_off = 1;
    music_play(1717, 0);                    /* enter.mid once, started before the auto-walk */
    auto_walk_keep_facing(h->col + 2);
    ag.facing = 1;
    run_state(3, -1, 0, -2);
    int door_x = 9 + 13 * (door_world - ag.scroll);
    door_anim(door_x, 1, 13);               /* player stays in back view cel 13 on top of the door */
    run_state(5, -1, 0, -4);                /* walks into the open doorway: cel 22 at y 55 */
    while (music_playing()) plat_wait(1);   /* frozen screen until enter.mid has finished */
    telly_remove();
    trick_remove();
    ag.hall_scroll_saved = ag.scroll;
    ag.hall_x_saved = obj[OB_PLAYER].x;
    int room_area = area + (area < 5 ? 9 : 7);
    snd_stop_all();
    ag.in_room = 1;
    ag.area = room_area;
    obj[OB_PLAYER].x = hot[room_area][0].col + 1;
    obj[OB_PLAYER].y = 55;
    obj[OB_PLAYER].cel = 23;
    ag.facing = 1;
    action_screen_rebuild();
    wire_spawn_check();                     /* the LiveWire roll happens before the arrival steps */
    run_state(6, -1, 0, 4);
    run_state(4, -1, 0, 2);
    stand();
    music_stop();
    hint_show(5, 0);
    if (obj[OB_WIRE].active) snd_play(1731, 2, 0);
    ag.music_off = 0;
}

static void exit_room(void) {
    int r = 0;
    for (int i = 0; i < 5; i++) if (ROOM_AREA[i] == ag.area) r = i;
    int harea = ROOM_DOOR_AREA[r];
    int hslot = 1;
    for (int sl = 0; sl < 4; sl++) if (hot[harea][sl].type == 1) hslot = sl;
    int door_world = harea * 38 + hot[harea][hslot].col;
    auto_walk_room(hot[ag.area][0].col + 1);
    ag.facing = 1;
    run_state(3, -1, 0, -2);
    run_state(5, -1, 0, -4);
    stop_everything();                      /* immediately: all sounds and the music stop */
    obj[OB_WIRE].active = 0;
    obj[OB_PROP].active = 0;
    ag.in_room = 0;
    ag.scroll = ag.hall_scroll_saved;
    obj[OB_PLAYER].x = ag.hall_x_saved;
    obj[OB_PLAYER].y = 55;
    obj[OB_PLAYER].cel = 23;
    ag.facing = 1;
    action_screen_rebuild();
    int door_x = 9 + 13 * (door_world - ag.scroll);
    {   /* the hall door is first shown open (136) */
        const Cel *c = aseq_cel(aseq_load(200), 136);
        if (c) canvas_blit_cel(g_eng.back, c, door_x, 101, 0, &(Rect){0, 0, CANVAS_W, CANVAS_H});
        draw_scene();
        plat_present();
    }
    run_state(6, -1, 0, 4);
    door_anim(door_x, 0, 18);               /* the door closes 136 -> 135 -> 134 behind him (cel 18 on top) */
    run_state(4, -1, 0, 2);
    obj[OB_PLAYER].y = 81;
    stand();
}

/* ---- elevator ride (spec 04 section 5.5, v1.11) ---- */
/* walk (state 2) until the local column equals target, without changing facing: backwards when facing away */
static void auto_walk_keep_facing(int target_local) {
    int guard = 0;
    while (local_col() != target_local && guard++ < 400) {
        int dir = (local_col() < target_local) ? 1 : -1;
        int rel = dir * ag.facing;            /* +1 forwards, -1 backwards (mirrored walk cels) */
        int before = player_world_col();
        set_state(2, 1, rel, 0);
        ag.pdx = rel;
        seq_frame();
        if (player_world_col() == before && guard > 3) break;
    }
    stand();
}

/* one door-animation sequence D(dir, playerCel, playerOnTop): 4 steps of 9 cs; door cels go into the background */
static void elev_D(int slot_col, int opening, int player_cel, int player_on_top) {
    int a = player_world_col() / 38;
    int L = ag.scroll - a * 38;
    int doorX = 9 + 13 * (slot_col - L) + 37, doorY = 105;
    Aseq *h = aseq_load(200);
    Rect cv = {0, 0, CANVAS_W, CANVAS_H};
    for (int s = 0; s < 4; s++) {
        int64_t t0 = plat_now();
        int panel = opening ? (s < 3 ? 177 - s : -1) : 175 + s;
        const Cel *cab = aseq_cel(h, 179), *pc = panel >= 0 ? aseq_cel(h, panel) : NULL;
        if (cab) canvas_blit_cel(g_eng.back, cab, doorX, doorY, 0, &cv);
        if (pc) canvas_blit_cel(g_eng.back, pc, doorX, doorY, 0, &cv);
        telly_update();
        trick_update();
        obj[OB_PLAYER].cel = player_cel;
        ov_clear();
        if (!player_on_top && pc) ov_add(200, panel, doorX, doorY, 0);
        draw_scene();
        ov_clear();
        plat_present();
        plat_wait_until(t0 + 9);
    }
}

static void elevator(int slot) {
    int area0 = cur_area();
    int slot_col = hot[area0][slot].col;
    clock_freeze(1);                          /* the clock is frozen during the ride */
    auto_walk_keep_facing(slot_col + 7);
    ag.facing = 1;
    run_state(3, -1, 0, -2);                  /* back view, y 81 -> 71 */
    stop_everything();
    ag.music_off = 1;
    snd_play(1713, 5, 0);
    elev_D(slot_col, 1, 13, 1);               /* departure open: player (back view cel 13) on top */
    run_state(5, -1, 0, -4);                  /* walks into the open cab, y 71 -> 55 */
    snd_play(1714, 6, 0);
    elev_D(slot_col, 0, 22, 0);               /* departure close: panels over the player */
    int up = (area0 == 0 || area0 == 4);
    snd_elevator_ride(up);                    /* DOS: the one sound of the ride, requested now (12 section 5.6a) */
    telly_remove();
    trick_remove();
    obj[OB_PLAYER].active = 0;                /* nobody visible during the slide */
    int newarea = up ? area0 + 5 : area0 - 5;
    /* snapshot the old floor, build the new one off-screen */
    draw_scene();
    Canvas *oldv = canvas_new(CANVAS_W, CANVAS_H);
    memcpy(oldv->px, g_eng.work->px, (size_t)CANVAS_W * CANVAS_H);
    ag.scroll = newarea * 38;
    hall_draw_full(ag.scroll);
    Canvas *newv = canvas_new(CANVAS_W, CANVAS_H);
    memcpy(newv->px, g_eng.work->px, (size_t)CANVAS_W * CANVAS_H);
    int off = 0;
    for (int step = 1; step <= 8; step++) {
        int64_t t0 = plat_now();
        int strip = up ? (step == 8 ? 32 : 31) : 31;
        off += strip;
        for (int y = 35; y < 284; y++) {
            const uint8_t *src = NULL;
            if (up) {          /* picture moves down; the new floor enters from the top */
                int oy = y - off, ny = y - (off - 249);
                if (oy >= 35 && oy < 284) src = oldv->px + oy * CANVAS_W;
                else if (ny >= 35 && ny < 284) src = newv->px + ny * CANVAS_W;
            } else {           /* picture moves up; the new floor enters from below */
                int oy = y + off, ny = y + off - 248 + 0;
                if (oy >= 35 && oy < 284) src = oldv->px + oy * CANVAS_W;
                else { ny = y - (248 - off) ; if (ny >= 35 && ny < 284) src = newv->px + ny * CANVAS_W; }
            }
            if (src) memcpy(g_eng.work->px + y * CANVAS_W + 9, src + 9, 494);
        }
        plat_present();
        plat_wait_until(t0 + 9);
    }
    canvas_free(oldv);
    canvas_free(newv);
    int left_end = (newarea == 0 || newarea == 5);
    ag.scroll = newarea * 38;
    obj[OB_PLAYER].active = 0;
    obj[OB_PLAYER].x = left_end ? 12 : 24;
    obj[OB_PLAYER].y = 55;
    ag.facing = 1;
    action_screen_rebuild();                  /* full hall draw at the new scroll, no player, no door cels yet */
    snd_stop_all_keep_ride();                 /* the arrival's "stop all sounds": the DOS ride song runs on (12 section 5.6) */
    music_stop();
    snd_play(1713, 5, 0);
    int new_slot_col = hot[newarea][1].col;
    obj[OB_PLAYER].active = 1;
    elev_D(new_slot_col, 1, 23, 0);           /* arrival open: player under the panels */
    run_state(6, -1, 0, 4);
    run_state(4, -1, 0, 2);
    snd_play(1714, 6, 0);
    elev_D(new_slot_col, 0, 18, 1);           /* arrival close: player on top, y 81 */
    obj[OB_PLAYER].y = 81;
    ag.facing = left_end ? 1 : -1;
    stand();
    ag.music_off = 0;
    clock_freeze(0);
}

static void energy_machine(int slot) {
    int area = cur_area();
    Hot *h = &hot[area][slot];
    int saved_facing = ag.facing;
    auto_walk_keep_facing(h->col + 3);       /* moonwalk if facing away */
    int ms = area * 38 + h->col - ag.scroll;
    int X = 9 + 13 * ms;
    ag.facing = 1;
    run_state(15, -1, 0, -2);                /* cels 9,10,11,12,203 at y 79..71; cel 203 stays */
    plat_wait_until(frame_deadline);         /* the last step (cel 203) is held for its full 7 cs before the HUD update and beam 0 */
    /* remember the scene rectangle X..X+131 x 91..130 (the beam frames are drawn on it) */
    int rx0 = X < 0 ? 0 : X, rx1 = X + 131 > CANVAS_W - 1 ? CANVAS_W - 1 : X + 131;
    int rw = rx1 - rx0 + 1;
    static uint8_t snap[40 * CANVAS_W];
    for (int y = 91; y <= 130; y++) memcpy(snap + (y - 91) * CANVAS_W, g_eng.work->px + y * CANVAS_W + rx0, (size_t)rw);
    int fill = gs.credit > 0;
    static const int fill_seq[9] = {196, 197, 196, 197, 196, 197, 196, 197, 196};
    static const int refuse_seq[4] = {199, 200, 201, 199};
    if (fill) {
        gs.energy = (uint8_t)(gs.energy + gs.credit);
        gs.credit = 0;
        hud_energy();                         /* redrawn and shown before the first beam frame */
        plat_present();
        if (gs.energy <= 5) hint_show(12, 1);
    } else hint_show(4, 1);
    int snd = fill ? 1707 : 1708;
    snd_play(snd, 2, SND_LOOP);
    int nf = fill ? 9 : 4;
    Aseq *hs = aseq_load(200);
    for (int f = 0; f < nf; f++) {
        int64_t t0 = plat_now();
        const Cel *c = aseq_cel(hs, fill ? fill_seq[f] : refuse_seq[f]);
        if (c) canvas_blit_cel(g_eng.work, c, X, 91, 0, &(Rect){0, 0, CANVAS_W, CANVAS_H});
        plat_present();
        for (int y = 91; y <= 130; y++) memcpy(g_eng.work->px + y * CANVAS_W + rx0, snap + (y - 91) * CANVAS_W, (size_t)rw);
        plat_hold_present(1);
        plat_wait_until(t0 + 12);
        plat_hold_present(0);
    }
    snd_stop(snd);
    run_state(29, -1, 0, 2);                 /* cels 14..18, y 73..81 */
    obj[OB_PLAYER].y = 81;
    ag.facing = saved_facing;
    stand();
}

/* ------------------------------------------------------------ zap */
static void do_zap_target_hit(void);

/* the zap: a synchronous, frozen sequence in the commands phase (spec 04 6.3.6): 3 pose steps + 6 bolt steps of 4 cs */
static void zap(void) {
    if (ag.pstate >= 3) return;
    Obj *p = &obj[OB_PLAYER], *t = &obj[OB_TELLY], *w = &obj[OB_WIRE];
    int e0 = gs.energy;
    ag.moved = 1;
    /* target fixed at the start: Telly if walking/stopped/throwing, otherwise the LiveWire */
    int target = 0, s0 = 0, e1 = 0;
    if (ag.telly_state >= 3 && ag.telly_state <= 5) { target = 1; s0 = t->x + 1; e1 = t->x + 7; }
    else if (w->active && w->anim != 31) { target = 2; s0 = w->x + 1; e1 = w->x + 10; }
    ag.pstate = 7;                                  /* pose drawn 4 columns left when facing left (draw_scene) */
    for (int i = 0; i < 3; i++) {
        int64_t t0 = plat_now();
        p->cel = 47 + i;
        ov_clear();
        draw_scene();
        plat_present();
        plat_wait_until(t0 + 4);
    }
    snd_play(e0 > 0 ? 1764 : 1765, 5, 0);
    int b = ag.facing > 0 ? p->x + 8 : p->x - 7;
    static const int shifts[6] = {5, 3, 0, 0, 0, 0};
    int hit = 0;
    for (int f = 0; f < 6; f++) {
        int64_t t0 = plat_now();
        ov_clear();
        if (e0 > 0 && b < 40) {
            if (target && ((s0 >= b && s0 <= b + 12) || (e1 >= b && e1 <= b + 12))) hit = 1;
            ov_add(200, 50 + f, px_x(b), px_y(p->y + 12), ag.facing < 0);
        }
        draw_scene();                                /* pose cel 49 stays under the bolt */
        plat_present();
        plat_wait_until(t0 + 4);
        if (ag.facing < 0) b -= shifts[f];
    }
    ov_clear();
    ag.pstate = 1; ag.premaining = 0; ag.pdx = ag.pdy = 0;
    obj_set_anim(p, 1);
    if (gs.energy > 0) add_energy(-1);
    if (gs.energy > 0 && hit) {
        if (target == 1) do_zap_target_hit();
        else if (target == 2 && w->active) {
            snd_stop(1731);
            snd_play(1733, 5, 0);
            obj_set_anim(w, 31);
            ag.zap_frame = 5;
        }
    }
}

static void do_zap_target_hit(void) {
    if (ag.telly_lit && ag.telly_state != TELLY_THROW) {
        stop_everything();                   /* music stops (remembered), all sounds stop, then the drill questions */
        snd_zap_lit_telly();                 /* DOS: the big zap song, after the stop-all (12 section 5.3) */
        clock_reset_deadline();
        int got = drill_run_telly();
        if (got) hint_show(3, 0);          /* over the still-visible drill screen */
        telly_remove();                      /* Telly and any trick in flight are removed silently */
        trick_remove();
        action_screen_rebuild();
    } else {
        snd_stop(1754);
        snd_play(1763, 2, 0);
        ag.telly_state = TELLY_ZAPPED;       /* flashes 9 frames starting with this frame's update; an unreleased trick is never thrown */
        ag.telly_cnt = 9;
    }
}

/* ------------------------------------------------------------ collisions */
static void splat_wait(int cel, int col) {
    /* frozen: objects redrawn exactly as presented, then the splat on top (clipped to the playfield), present, 1723, wait 83 */
    draw_scene();
    const Cel *c = aseq_cel(aseq_load(200), cel);
    if (c) canvas_blit_cel(g_eng.work, c, px_x(col), 100, 0, &(Rect){9, 35, 494, 249});
    plat_present();
    snd_play(1723, 3, 0);
    plat_wait(83);
    draw_scene();                            /* objects redrawn without presenting; the splat stays until the next present */
}

static void player_hit_by(int dirs_same, int fall, int crash) {
    if (fall) set_state(9, -1, -1, 0);
    else if (crash) set_state(dirs_same ? 26 : 27, -1, 2, 0);     /* dx +2 relative to the facing in both cases */
    else if (dirs_same) set_state(26, -1, 2, 0);
    else set_state(27, -1, -2, 0);
}

static void collisions(void) {
    Obj *p = &obj[OB_PLAYER];
    if (ag.pstate == 8) return;
    if (obj[OB_TRICK].active && touching(p, &obj[OB_TRICK])) {
        int idx = ag.trick_type - 22;
        int same = (ag.facing == ag.trick_dir);
        splat_wait(TRICK_SPLAT[idx], p->x);
        if (idx <= 1) player_hit_by(same, 0, 0); else player_hit_by(0, 1, 0);
        trick_remove();
        if (ag.telly_state && ag.telly_state != TELLY_ZAPPED && ag.telly_state != TELLY_DEMAT) telly_remove();
        clock_add_minutes(15);
        if (gs.clock >= 1200) out_of_time();
        hint_show(1, 1);
        return;
    }
    if (ag.telly_state >= 3 && ag.telly_state <= 5 && touching(p, &obj[OB_TELLY])) {
        snd_play(1704, 2, 0);
        int tx = obj[OB_TELLY].x;
        splat_wait(211, (tx < p->x ? tx : p->x) + 1);
        player_hit_by(ag.facing == ag.telly_dir, 0, 1);
        snd_play(1719, 3, 0);
        ag.telly_state = TELLY_DEMAT;
        ag.telly_cnt = 5;
        add_energy(-1);
        hint_show(2, 1);
        return;
    }
    if (obj[OB_WIRE].active && obj[OB_WIRE].anim != 31 && touching(p, &obj[OB_WIRE])) {
        obj[OB_WIRE].active = 0;
        snd_stop(1731);
        snd_play(1732, 5, 0);
        set_state(30, -1, -2, 0);
        add_energy(-1);
        hint_show(6, 1);
    }
}

/* ------------------------------------------------------------ Up action */
static void jump(void) { set_state(8, -1, 0, -12); }

static void action_up(void) {
    if (ag.pstate > 2) return;
    int slot;
    int spot = hot_current(&slot);
    int tact = ag.telly_state != 0, kact = obj[OB_TRICK].active;
    if (spot != 1 && spot != 2 && (tact || kact)) { jump(); return; }
    if ((spot == 1 || spot == 2) && ((tact && touching(&obj[OB_PLAYER], &obj[OB_TELLY])) || (kact && touching(&obj[OB_PLAYER], &obj[OB_TRICK])))) { jump(); return; }
    switch (spot) {
    case 2: elevator(slot); return;
    case 1:
        if (ag.in_room) exit_room(); else enter_room(slot);
        return;
    case 9: energy_machine(slot); return;
    default: break;
    }
    int saved_facing = ag.facing;
    ag.facing = 1;
    run_state(3, -1, 0, -2);
    if (ag.in_room && spot >= 11 && spot <= 20) {
        int idx = spot - 11;
        if (gs.obj_state[idx] == 1) {
            clock_pause();
            hint_show(ses.calc ? 11 : 10, 0);
            obj[OB_WIRE].active = 0;
            math_challenge(idx);
            action_screen_rebuild();
            clock_resume();
            wire_spawn_check();
        }
        if (gs.obj_state[idx] == 2) hint_show(7, 0);
        else if (gs.obj_state[idx] == 3) hint_show(8, 0);
    }
    run_state(4, -1, 0, 2);
    obj[OB_PLAYER].y = 81;
    stand();
    ag.facing = saved_facing;
}

/* ------------------------------------------------------------ input */
static void mouse_commands(int *left, int *right, int *flipl, int *flipr, int *up, int *zapc) {
    int mx, my, lb;
    plat_mouse_state(&mx, &my, &lb);
    if (!lb || mx < 9 || mx > 503 || my < 35 || my > 283) return;
    Obj *p = &obj[OB_PLAYER];
    int pl = px_x(p->x), pr = pl + 40;
    const Cel *c = aseq_cel(aseq_load(200), p->cel);
    if (c) pr = pl + c->w;
    int yT = 175;
    if (mx < pl) {
        if (my < yT) *flipl = 1;
        else if (my < yT + 50) *left = 1;
        else if (ag.facing > 0) *left = 1; else *zapc = 1;
    } else if (mx > pr) {
        if (my < yT) *flipr = 1;
        else if (my < yT + 50) *right = 1;
        else if (ag.facing < 0) *right = 1; else *zapc = 1;
    } else if (my < yT) *up = 1;
}

static void update_cursor(void) {
    if (plat_mode() != PLAT_SDL) return;
    int mx, my;
    plat_mouse_pos(&mx, &my);
    int id = 0;
    if (mx >= 9 && mx <= 503 && my >= 35 && my <= 283) {
        Obj *p = &obj[OB_PLAYER];
        int pl = px_x(p->x), pr = pl + 40;
        const Cel *c = aseq_cel(aseq_load(200), p->cel);
        if (c) pr = pl + c->w;
        int yT = 175;
        if (mx < pl) id = my < yT ? 131 : (my < yT + 50 ? 130 : (ag.facing > 0 ? 130 : 134));
        else if (mx > pr) id = my < yT ? 132 : (my < yT + 50 ? 128 : (ag.facing < 0 ? 128 : 134));
        else if (my < yT) id = 129;
    }
    plat_set_cursor(id);
}

static void apply_input(void) {
    int left = plat_key_held(K_LEFT), right = plat_key_held(K_RIGHT), up = plat_key_held(K_UP);
    int home = plat_key_held(K_HOME), space = plat_key_held(K_SPACE);
    int flipl = (up && left) || home, flipr = up && right, zapc = space;
    mouse_commands(&left, &right, &flipl, &flipr, &up, &zapc);
    if (left) { ag.facing = -1; if (ag.pstate != 2) set_state(2, 1, 1, 0); }
    if (right) { ag.facing = 1; if (ag.pstate != 2) set_state(2, 1, 1, 0); }
    if (flipr) { ag.facing = 1; set_state(8, -1, 2, -12); }
    if (flipl) { ag.facing = -1; set_state(8, -1, 2, -12); }
    if (up) action_up();
    if (zapc) zap();
    if (!left && !right && ag.pstate == 2) set_state(1, 1, 0, 0);
    (void)rnga_rand();
}

static void frame_tail(int collide) {
    plat_hold_present(1);
    dosint_action_loop(ag.pstate <= 2);     /* DOS mode: the hall/room song is re-requested while the frame loop runs and the player stands or walks (12 section 5.2) */
    plat_wait_until(frame_deadline);
    dosint_action_loop(0);
    plat_hold_present(0);
    plat_present();
    if (collide) collisions();
    clock_check();
    frame_deadline = plat_now() + 7;
}

void action_frame(int key) {
    ag.frame_count++;
    hint_poll_queue();
    update_cursor();
    if (key == K_ESC) {
        clock_pause();
        int yes = dlg_yesno(game_signed_in() ? 23008 : 23013, 0);
        if (yes) game_quit();
        clock_resume();
        return;
    }
    if (key == 0x5001 || key == 0x5002) { ending_decide(key == 0x5001 ? (int)gs.hideout : ((int)gs.hideout + 1) % 5); return; }   /* script hooks: `decide win|lose` */
    if (key == 0x5003) { out_of_time(); return; }                                        /* script hook: `midnight` */
    if (key == K_CTRL_S) { menu_toggle_all_sound(); return; }
    if (key == K_TAB) {
        clock_pause();
        hint_show(14, 0);
        decoder_run();
        action_screen_rebuild();
        clock_resume();
        return;
    }
    apply_input();
    music_for_area();
    update_objects(ag.normal_play);
    frame_tail(1);
}

void action_loop(void) {
    clock_reset_deadline();
    ag.normal_play = 1;
    for (;;) {
        int key = plat_key_pop();
        if (!key) { int cx, cy; if (plat_click_pop(&cx, &cy)) { if (cx >= 33 && cx < 99 && cy >= 312 && cy < 366) key = K_TAB; } }
        action_frame(key);
    }
}

/* ------------------------------------------------------------ new game / state dump */
void action_reset_for_new_game(void) {
    world_reset_objects();
    memset(&ag, 0, sizeof ag);
    ag.scroll = 76;
    ag.facing = 1;
    ag.pstate = 1;
    ag.premaining = 0;
    obj[OB_PLAYER].x = 18;
    obj[OB_PLAYER].y = 81;
    obj[OB_PLAYER].active = 1;
    obj_set_anim(&obj[OB_PLAYER], 1);
    obj[OB_PLAYER].cel = 0;
    /* energy machine: replaces a plain door at a random candidate slot */
    int k = rnd(0, 8);
    ag.em_area = EM_CAND[k][0];
    ag.em_slot = EM_CAND[k][1];
    Hot *h = &hot[ag.em_area][ag.em_slot];
    h->type = 9;
    h->y = 110;
    h->cel = 195;
    h->f3 = h->col + 19;                     /* hot-spot right end */
}

void action_dump_state(FILE *f) {
    uint8_t b[STATE_SIZE];
    gs_pack(b);
    fprintf(f, " \"game_state_hex\": \"");
    for (int i = 0; i < STATE_SIZE; i++) fprintf(f, "%02x", b[i]);
    fprintf(f, "\",\n \"state\": {\"seed\": %d, \"game_score\": %d, \"clock\": %d, \"level\": %d, \"energy\": %d, \"pending_energy\": %d,\n",
            gs.seed, gs.score, gs.clock, gs.level, gs.energy, gs.credit);
    fprintf(f, "  \"object_state\": [");
    for (int i = 0; i < 10; i++) fprintf(f, "%s%d", i ? ", " : "", gs.obj_state[i]);
    fprintf(f, "], \"object_op\": [");
    for (int i = 0; i < 10; i++) fprintf(f, "%s%d", i ? ", " : "", gs.obj_op[i]);
    fprintf(f, "], \"object_question\": [");
    for (int i = 0; i < 10; i++) fprintf(f, "%s%d", i ? ", " : "", gs.obj_q[i]);
    fprintf(f, "], \"object_wrong\": [");
    for (int i = 0; i < 10; i++) fprintf(f, "%s%d", i ? ", " : "", gs.obj_wrong[i]);
    fprintf(f, "],\n  \"room_clues\": [");
    for (int r = 0; r < 5; r++) {
        fprintf(f, "%s[", r ? ", " : "");
        for (int k = 0; k < 4; k++) fprintf(f, "%s{\"pic\": %d, \"revealed\": %s}", k ? ", " : "", gs.clue[r][k] & 0x7F, (gs.clue[r][k] & 0x80) ? "false" : "true");
        fprintf(f, "]");
    }
    fprintf(f, "],\n  \"code\": [");
    for (int k = 0; k < 4; k++) fprintf(f, "%s{\"pic\": %d, \"captured\": %s}", k ? ", " : "", gs.code[k] & 0x7F, (gs.code[k] & 0x80) ? "false" : "true");
    fprintf(f, "], \"code_pieces\": %d, \"match_rule\": %d, \"hint_flags\": [", gs.pieces, gs.rule);
    for (int i = 0; i < 18; i++) fprintf(f, "%s%d", i ? ", " : "", gs.hints[i]);
    fprintf(f, "], \"hideout\": %d},\n", gs.hideout);
    Obj *p = &obj[OB_PLAYER];
    fprintf(f, " \"player\": {\"x\": %d, \"y\": %d, \"world_col\": %d, \"state\": %d, \"remaining\": %d, \"dx\": %d, \"dy\": %d, \"facing\": %d, \"cel\": %d, \"frame\": %d, \"moved\": %d},\n",
            p->x, p->y, player_world_col(), ag.pstate, ag.premaining, ag.pdx, ag.pdy, ag.facing, p->cel, p->frame, ag.moved);
    fprintf(f, " \"scroll\": %d, \"area\": %d, \"in_room\": %s,\n", ag.scroll, ag.in_room ? ag.area : player_world_col() / 38, ag.in_room ? "true" : "false");
    Obj *t = &obj[OB_TELLY], *k = &obj[OB_TRICK], *w = &obj[OB_WIRE];
    fprintf(f, " \"telly\": {\"state\": %d, \"x\": %d, \"dir\": %d, \"lit\": %d},\n", ag.telly_state, t->x, ag.telly_dir, ag.telly_lit);
    fprintf(f, " \"trick\": {\"active\": %d, \"type\": %d, \"x\": %d},\n", k->active, ag.trick_type, k->x);
    fprintf(f, " \"livewire\": {\"active\": %d, \"x\": %d, \"dir\": %d},\n", w->active, w->x, ag.wire_dir);
    fprintf(f, " \"energy_machine\": {\"area\": %d, \"slot\": %d},\n", ag.em_area, ag.em_slot);
    fprintf(f, " \"energy_credit\": %d, \"clock\": %d, \"clock_paused\": %d, \"dialog\": %d,\n", gs.credit, gs.clock, clock_paused(), dlg_current_id());
    fprintf(f, " \"screen\": \"%s\", \"object_seeds\": [", dbg_screen);
    for (int i = 0; i < 10; i++) fprintf(f, "%s%d", i ? ", " : "", ses.math_seed[i]);
    fprintf(f, "],\n \"drill\": {\"display\": \"%s\", \"answer\": %d, \"asked\": %d, \"correct\": %d, \"required\": %d},\n",
            dbg_drill.display, dbg_drill.answer, dbg_drill.asked, dbg_drill.correct, dbg_drill.required);
    fprintf(f, " \"challenge\": {\"object\": %d, \"mtht_id\": %d, \"seed\": %d, \"question_number\": %d, \"wrong_tries\": %d, \"entry\": \"%s\"},\n",
            dbg_chal.object, dbg_chal.mtht_id, dbg_chal.seed, dbg_chal.qnum, dbg_chal.wrong, dbg_chal.entry);
    fprintf(f, " \"decoder\": {\"focus\": %d, \"highlighted_room\": %d}", dbg_dec.focus, dbg_dec.row);
}

/* Decide: scroll the hall to the door of room `room`, walk in through the door (spec 07 B 2.1 step 3) */
void action_decide_door(int room) {
    ag.music_off = 1;                        /* stays off until the next game starts (07 C.5) */
    int area = ROOM_DOOR_AREA[room];
    int slot = 1;
    for (int s = 0; s < 4; s++) if (hot[area][s].type == 1) slot = s;
    Hot *h = &hot[area][slot];
    ag.in_room = 0;
    telly_remove();
    trick_remove();
    obj[OB_WIRE].active = 0;
    obj[OB_PROP].active = 0;
    ag.scroll = area * 38;
    obj[OB_PLAYER].x = h->col + 2;
    obj[OB_PLAYER].y = 81;
    ag.facing = 1;
    stand();
    ag.normal_play = 0;
    action_screen_rebuild();
    music_stop();
    music_play(1717, 0);
    frame_deadline = plat_now() + 7;
    run_state(3, -1, 0, -2);
    door_anim(9 + 13 * (area * 38 + h->col - ag.scroll), 1, 13);
    run_state(5, -1, 0, -4);
    snd_play(1720, 5, 0);
}
