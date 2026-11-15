/* game.h - shared game declarations (state block, session, world, action loop) */
#ifndef GAME_H
#define GAME_H
#include "aseq.h"
#include "audio.h"
#include "dialog.h"
#include "dosint.h"
#include "gfx.h"
#include "mtht.h"
#include "plat.h"
#include "res.h"
#include "rng.h"
#include "rules.h"
#include "save.h"
#include "ui.h"
#include <setjmp.h>

/* ------------------------------------------------------------ game state block (03 section 4.4) */
typedef struct GameState {
    uint16_t seed;
    uint16_t score;
    uint16_t clock;           /* hours*100+minutes, 900..1200 */
    uint8_t level;
    uint8_t energy;
    uint8_t credit;           /* pending energy credit */
    uint8_t obj_state[10];    /* 0 none, 1 has challenge, 2 solved, 3 failed */
    uint8_t obj_op[10];       /* 0 add, 1 divide, 2 multiply, 3 subtract */
    uint8_t obj_q[10];
    uint8_t obj_wrong[10];
    uint8_t clue[5][4];       /* picture id, bit 7 = not yet revealed */
    uint8_t code[4];          /* picture id, bit 7 = not yet captured */
    uint16_t pieces;
    uint16_t rule;
    uint8_t hints[18];        /* 0 not triggered, 1 pending, 0xFF shown */
    uint16_t unused5f;
    uint16_t hideout;
} GameState;
extern GameState gs;
void gs_pack(uint8_t out[STATE_SIZE]);
void gs_unpack(const uint8_t in[STATE_SIZE]);
int gs_clock_off_display(void);

/* ------------------------------------------------------------------------ session */
typedef struct Session {
    SaveDB db;
    char save_path[1024];
    int player;               /* list index, -1 = nobody signed in */
    int rank, tier, rank_at_start;
    int sfx, music, voice, calc, hints, clock_on;
    uint8_t cust_game[13], cust_drill[13];
    int16_t math_seed[10];
    int resume;               /* a saved game is to be continued */
    int level_word;           /* word-problem level */
    int drill_mode;
    int time_seed_valid, time_seed;
    int betaless;
    int fix_puzzle;           /* re-roll ambiguous puzzles (open-questions D7/D8); on in the interactive game, off in play mode */
} Session;
extern Session ses;
uint8_t *ses_rec(void);
void ses_write_record(void);                     /* sync options into the record and flush */
uint32_t ses_lifetime(void);

/* ------------------------------------------------------------------------ restart points */
extern jmp_buf jb_signin, jb_newgame;
void game_abort_to_signin(void) __attribute__((noreturn));
void game_abort_to_newgame(void) __attribute__((noreturn));

/* ------------------------------------------------------------------------ clock / hints */
extern int g_clock_box_live;
void clock_pause_reset(void);
void clock_pause(void);
void clock_resume(void);
int clock_paused(void);
void clock_add_minutes(int n);          /* addMinutes: carries, clamps to 1200 and redraws; plays chime */
void clock_reset_deadline(void);
void clock_show_pause(int v);
void clock_freeze(int v);
void clock_check(void);                 /* called per frame in the action loop */
int hint_show(int n, int armed);        /* New Player Hint n (dialog 1000+n) */
void hint_poll_queue(void);             /* show at most one pending hint */
void out_of_time(void) __attribute__((noreturn));

/* ------------------------------------------------------------------------ world */
typedef struct Hot { int type, col, y, f3, cel, f5, f6; } Hot;
typedef struct AnimDef { int n, cur, cel[9], tick, ticks; } AnimDef;
typedef struct Obj {
    int x, y, cel, f6, f8, f10, active, hit_off, hit_w;
    int anim, frame, tick;
} Obj;
extern Hot hot[16][4];
extern AnimDef animdef[35];
extern Obj obj[6];
enum { OB_BOLT, OB_WIRE, OB_PROP, OB_PLAYER, OB_TELLY, OB_TRICK };
extern const int COLPX[41];
int px_x(int col);                       /* COLPX with negative mirroring */
int px_y(int du);                        /* round(du*1.92) */
int world_load(void);                    /* BINA 5000 */
void world_reset_objects(void);
extern const int ROOM_AREA[5];
extern const int ROOM_DOOR_AREA[5];

/* ------------------------------------------------------------------------ action game */
typedef struct Action {
    int in_room;
    int area;                 /* current room area (rooms) */
    int scroll;               /* hall scroll in columns (world column of screen column 0) */
    int hall_scroll_saved, hall_x_saved;
    int pstate, premaining, pdx, pdy, facing;
    int moved;
    int telly_state, telly_dir, telly_lit, telly_speed_cnt, telly_throw_type;
    int trick_type, trick_dir;
    int wire_dir;
    int zap_frame, zap_x0, zap_left;
    int em_area, em_slot;     /* energy machine location */
    int hall_music;
    int normal_play;          /* AI enabled */
    int splat_cel, splat_until, splat_x, splat_y;
    int in_decide;
    int frame_count;
    int last_hint9;
    int telly_belly_ok;       /* 0 on the frame the last materialise cel is drawn (belly from the first walking frame) */
    int telly_cnt;            /* frame counter for flash / dematerialise */
    int telly_mirror;         /* current mirror state of Telly's art (art faces left natively) */
    int music_off;            /* music updates disabled (elevator / room entry sequences) */
} Action;
extern Action ag;

void action_reset_for_new_game(void);
void action_dump_state(FILE *f);
void game_quit(void) __attribute__((noreturn));
extern int gs_live;
int game_signed_in(void);               /* a player is signed in with a live game: the 23008 / 23006 variants and Save Game */
void save_game(void);
void game_start_new(void);               /* generate scenario (new game) */
void game_continue_saved(void);
extern void (*g_calc_redraw)(void);
void game_redraw_questions(void);        /* new customisation during a game: re-draw question variants and unsolved operations (03 section 3.0a E) */
void game_enter_hall_view(void);         /* draw hud + hall at current scroll, place objects */
void action_loop(void) __attribute__((noreturn));
void action_frame(int key);
void action_screen_rebuild(void);
int player_world_col(void);
int hot_current(int *slot_out);          /* hot spot type (or -1) for the player's position */
int hot_type_at(int area, int slot);

/* world.c drawing */
void hall_draw_full(int scroll);
void hall_scroll_by(int n);               /* positive = view moves right */
void room_draw(int area);
void edge_strips(void);

/* sub-games (other modules) */
int drill_run_telly(void);               /* returns 1 if code piece won */
void drill_for_skill(void);
void drill_pause_hook(int on);   /* Drill for Skill: PAUSE cel while paused */
void decoder_run(void);                  /* decoder screen incl. Decide */
void math_challenge(int obj_idx);        /* returns when finished or aborted */
void puzzle_new_game(int tier);

/* endings (ending.c) */
void ending_decide(int room);            /* noreturn via restart points */
void midnight_sequence(void);
void continue_prompt(int x, int y);
void intro_run(void);
int signin_run(void);                    /* returns 1 game, 2 drill, 0 quit */
void shell_play(const char *data_dir, const char *save_dir, const char *script, const char *outdir);
void shell_main(const char *data_dir, const char *save_dir, const char *soundfont, int fullscreen);

extern int g_menu_grey;              /* every menu item greyed (intro, ending sequences, midnight): titles still open */
void menu_reset_service(void);       /* after a longjmp out of a menu command */
void menu_set_current(int set);      /* the menu bar of a menu set: 0 sign-in, 1 game, 2 drill, -1 none (03 section 3.0/3.1) */
void menu_exec(int id);
void menu_toggle_all_sound(void);
/* debug/state-dump info for the conformance `state` command */
extern const char *dbg_screen;
extern struct DbgDrill { char display[40]; int answer, asked, correct, required; } dbg_drill;
extern struct DbgChal { int object, mtht_id, seed, qnum, wrong; char entry[8]; } dbg_chal;
extern struct DbgDec { int focus, row; } dbg_dec;
/* constants */
extern const int CLUES_PER[4];
void play_random_praise(int use_rngb, char *out, size_t n);
#endif
