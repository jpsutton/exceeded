/* shell.c - program flow: session preparation, new game generation, rank dialogs, play-mode driver */
#include "game.h"
#include "dosint.h"
#include "setup.h"
#include "setupui.h"
#include "res.h"
#include "vfs.h"
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

static const char *g_data_dir;

/* --------------------------------------------------------------- persistence */
void save_game(void) {
    uint8_t *r = ses_rec();
    if (!r || !gs_live) return;          /* v1.31: nothing to save before the state is initialised, or once the game is finished */
    r[REC_SAVED] = 1;
    gs_pack(r + REC_STATE);
    ses_write_record();
}

void game_quit(void) {
    if (game_signed_in()) save_game();
    evlog_close();
    exit(0);
}

/* --------------------------------------------------------------- seeding */
static void mkdirs_path(const char *p);
static void time_reseed(void) {
    if (ses.time_seed_valid) rnga_seed((uint16_t)ses.time_seed);
    else rnga_seed((uint16_t)time(NULL));
}

/* --------------------------------------------------------------- new game */
static const int OP_MAP[4] = {0, 2, 3, 1};   /* customisation order -> word-problem op code */

void game_start_new(void) {
    uint8_t keep_code[4];
    memcpy(keep_code, gs.code, 4);
    uint8_t hints_keep[18];
    memset(hints_keep, 0, sizeof hints_keep);
    memset(&gs, 0, sizeof gs);
    memcpy(gs.code, keep_code, 4);
    gs.clock = 900;
    gs.energy = 12;
    gs.level = (uint8_t)ses.level_word;
    /* seeds */
    int seed = rnga_rand();
    gs.seed = (uint16_t)seed;
    rnga_seed((uint16_t)seed);
    for (int i = 0; i < 10; i++) ses.math_seed[i] = (int16_t)(rnga_scaled(10) + 1);
    time_reseed();
    /* per-object variants */
    for (int i = 0; i < 10; i++) {
        gs.obj_q[i] = (uint8_t)rnga_scaled(4);
        int k, guard = 0;
        do { k = rngb_range(0, 3); } while (!(ses.cust_game[k] & 0x10) && ++guard < 1000);
        gs.obj_op[i] = (uint8_t)OP_MAP[k];
    }
    /* which objects hold challenges */
    if (ses.tier < 2) {
        for (int p = 0; p < 5; p++) gs.obj_state[2 * p + (rnga_range(1, 100) & 1)] = 1;
    } else {
        for (int i = 0; i < 10; i++) gs.obj_state[i] = 1;
    }
    /* deduction puzzle */
    Puzzle pz;
    puzzle_generate(ses.tier, ses.fix_puzzle, &pz);
    for (int i = 0; i < 4; i++) gs.code[i] = (uint8_t)(pz.code[i] | 0x80);
    for (int r = 0; r < 5; r++) for (int k = 0; k < 4; k++) gs.clue[r][k] = (uint8_t)(pz.room[r][k] | 0x80);
    gs.hideout = (uint16_t)pz.hideout;
    gs.rule = (uint16_t)pz.rule;
    gs.pieces = 0;
    action_reset_for_new_game();      /* energy machine slot etc. */
    gs_live = 1;
}

/* Problem Customization OK during a game (03 section 3.0a E): the question variant of every object and the operation of every
 * unsolved object are drawn again with the new settings. The challenge on screen keeps its problem. */
void game_redraw_questions(void) {
    for (int i = 0; i < 10; i++) {
        gs.obj_q[i] = (uint8_t)rnga_scaled(4);
        int k, guard = 0;
        do { k = rngb_range(0, 3); } while (!(ses.cust_game[k] & 0x10) && ++guard < 1000);   /* RNG-B is drawn for every object, as at a new game */
        if (gs.obj_state[i] < 2) gs.obj_op[i] = (uint8_t)OP_MAP[k];                          /* ... but only an unsolved object takes the new operation */
    }
}

void game_continue_saved(void) {
    uint8_t *r = ses_rec();
    gs_unpack(r + REC_STATE);
    ses.sfx = r[REC_SFX]; ses.music = r[REC_MUSIC]; ses.voice = r[REC_VOICE];
    ses.calc = r[REC_OPTIONS] & 1; ses.hints = (r[REC_OPTIONS] >> 1) & 1; ses.clock_on = (r[REC_OPTIONS] >> 2) & 1;
    audio_set_switches(ses.sfx, ses.music, ses.voice);
    r[REC_SAVED] = 0;
    rnga_seed(gs.seed);
    action_reset_for_new_game();
    gs_live = 1;
}

/* ------------------------------------------------------------------ session */
static void load_defaults_for_tier(void) {
    memcpy(ses.cust_game, tier_cust[ses.tier], 13);
    memcpy(ses.cust_drill, tier_cust[ses.tier], 13);
}

/* v1.31: clock 900..1200, the four code pictures in 0..19 and distinct, not all zero */
static int saved_state_valid(const uint8_t *st) {
    int z = 1;
    for (int i = 0; i < 99; i++) if (st[i]) { z = 0; break; }
    if (z) return 0;
    int clock = rd16(st + 4);
    if (clock < 900 || clock > 1200) return 0;
    for (int i = 0; i < 4; i++) {
        int p = st[0x45 + i] & 0x7F;
        if ((st[0x45 + i] & 0x7F) > 19) return 0;
        for (int j = 0; j < i; j++) if ((st[0x45 + j] & 0x7F) == p) return 0;
    }
    return 1;
}

static void prepare_session(void) {
    uint8_t *r = ses_rec();
    uint32_t life = ses_lifetime();
    ses.rank = rank_for_score(life);
    ses.tier = tier_for_rank(ses.rank);
    load_defaults_for_tier();
    ses.level_word = ses.tier;
    if (r) {
        if ((r[REC_FLAGS] & 2) && r[REC_GAME_CUST + 12] != 0xFF && (r[REC_GAME_CUST] & 0x10)) {
            memcpy(ses.cust_game, r + REC_GAME_CUST, 13);
            ses.level_word = ses.cust_game[0] & 3;
        }
        if ((r[REC_FLAGS] & 1) && r[REC_DRILL_CUST + 12] != 0xFF && (r[REC_DRILL_CUST] & 0x10)) memcpy(ses.cust_drill, r + REC_DRILL_CUST, 13);
        for (int i = 0; i < 10; i++) ses.math_seed[i] = rds16(r + REC_SEEDS + i * 2);
        ses.resume = r[REC_SAVED] == 1;
        if (ses.resume && !saved_state_valid(r + REC_STATE)) { ses.resume = 0; r[REC_SAVED] = 0; }   /* v1.31: an invalid saved state is discarded */
        ses_write_record();
    }
    ses.rank_at_start = ses.rank;
}

/* --------------------------------------------------------------- rank dialogs */
static void rank_dialog(void) {
    uint8_t *r = ses_rec();
    int stage = r ? r[REC_STAGE] : 2;
    int dlg = 0, sp = 0;
    switch (ses.rank) {
    case 0: if (stage == 0) { dlg = 9130; sp = 1830; if (r) r[REC_STAGE] = 1; } else { dlg = 132; sp = 1809; } break;
    case 1: if (stage == 1) { dlg = 9131; sp = 1831; if (r) r[REC_STAGE] = 2; } else { dlg = 144; sp = 1805; } break;
    case 2: dlg = 145; sp = 1808; break;
    case 3: dlg = 146; sp = 1807; break;
    case 4: dlg = 147; sp = 1804; break;
    case 5: dlg = 148; sp = 1806; break;
    case 6: dlg = 1; sp = 1803; break;       /* DIALOG 149 does not exist: DIALOG 1 has the text (D3) */
    default: dlg = 134; sp = 1833; break;
    }
    dlg_show(dlg, sp);
}

/* --------------------------------------------------------------- game flow */
static void start_game(void) {
    gs_live = 0;                       /* the rank dialog comes before the state exists */
    g_clock_box_live = 1;
    menu_set_current(1);
    ses.drill_mode = 0;
    audio_set_switches(ses.sfx, ses.music, ses.voice);
    dbg_screen = "start";
    hud_rebuild();
    rank_dialog();
    if (ses.resume) {
        game_continue_saved();
        ses.resume = 0;
    } else {
        ses.clock_on = 1;
        game_start_new();
    }
    memset(&ag.moved, 0, sizeof ag.moved);
    game_enter_hall_view();
}

void out_of_time(void) {
    gs.score = 0;
    gs.energy = 0;
    midnight_sequence();
    g_menu_grey = 0;                   /* enabled again when the midnight sequence ends, then the Out of Time dialog (modal) */
    uint8_t *r = ses_rec();
    if (r) { r[REC_SAVED] = 0; ses_write_record(); }
    dlg_show(9136, 0);
    game_abort_to_newgame();
}

static void dump_hook(FILE *f) {
    action_dump_state(f);
    fprintf(f, ",\n");
    dosint_state_json(f);
}

/* script command `volume M E S [even]`: as if set in Audio Options and confirmed with OK */
static void script_volume(int m, int e, int s, int even) {
    AudioSettings a;
    dosint_get_settings(&a);
    a.vol[0] = m; a.vol[1] = e; a.vol[2] = s;
    for (int i = 0; i < 3; i++) { if (a.vol[i] < 0) a.vol[i] = 0; if (a.vol[i] > 100) a.vol[i] = 100; }
    if (even >= 0) a.even = even != 0;
    dosint_apply(&a, 1);
}

static int style_arg(const char *v) { return v && !strcasecmp(v, "tandy") ? AS_TANDY : v && !strcasecmp(v, "speaker") ? AS_SPEAKER : AS_WINDOWS; }
/* script command `audio MUSIC SFX [DIR]`: as if chosen in Audio Options and confirmed with OK (spec 12 section 7.6) */
static void script_audio(const char *music, const char *sfx, const char *dir) {
    AudioSettings s;
    dosint_get_settings(&s);
    s.music = style_arg(music);
    s.sfx = style_arg(sfx);
    if (dir && *dir) snprintf(s.dir, sizeof s.dir, "%s", dir);
    dosint_apply(&s, 1);
}

/* --------------------------------------------------------------- common program flow */
static void main_flow(void) {
    menu_set_current(0);
    intro_run();
    setjmp(jb_signin);
    for (;;) {
        int r = signin_run();
        if (r == 0) {
            evlog_close();
            if (plat_mode() == PLAT_SDL) { plat_sdl_stop(); exit(0); }
            script_finish(0);
        }
        setjmp(jb_newgame);
        if (r == 2) {
            ses.drill_mode = 1;
            drill_for_skill();
            continue;
        }
        prepare_session();
        start_game();
        action_loop();
    }
}

/* --------------------------------------------------------------- play mode */
static int load_game_data(const Resolution *r, int explicit_data);
/* script command `setup` (14 section 10.3): the setup screen on the resolved base; after Play / the automatic start the
 * resolution runs again, and a load failure returns to a fresh setup screen */
static void play_setup(const char *save_dir, const char *outdir) {
    SetupOpts o = g_opts;
    char sd[1200];
    if (save_dir) snprintf(sd, sizeof sd, "%s", save_dir);
    else snprintf(sd, sizeof sd, "%s/save", outdir);
    o.save_dir = sd;
    o.data = NULL;
    o.setup = 1;
    int load_failed = 0;
    for (;;) {
        Resolution r;
        char err[300];
        if (setup_resolve(&o, &r, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); script_finish(1); }
        g_run = r;
        g_run_valid = 1;
        if (r.setup || load_failed) {
            int rc = setup_screen_run(&o, load_failed);
            if (rc == 0) script_finish(0);
            o.setup = 0;
            load_failed = 0;
            continue;
        }
        if (load_game_data(&r, 0) != 0) { load_failed = 1; continue; }
        return;                           /* the game data loaded: the game starts in this process (the script goes on) */
    }
}

void shell_play(const char *data_dir, const char *save_dir, const char *script, const char *outdir) {
    g_data_dir = data_dir;
    plat_init(PLAT_SCRIPT);
    if (script_load(script, outdir) != 0) die("cannot read script %s", script);
    g_script_hooks.dump_state = dump_hook;
    eng_init();
    if (data_dir) pal_load(128);
    audio_init(data_dir);
    if (data_dir) world_load();
    puzzle_reset();
    ScriptStart st;
    script_get_start(&st);
    if (st.setup) {
        play_setup(save_dir, outdir);
        /* the game starts: title and intro, as with the `intro` command; the rest of the script runs in it */
        if (g_eng.work) eng_shutdown();
        eng_init();
        pal_load(128);
        audio_init(g_run.data);
        world_load();
        puzzle_reset();
        st.intro = 1;
        st.valid = 1;
    }
    if (st.have_seed) { ses.time_seed_valid = 1; ses.time_seed = st.seed_time; rnga_seed((uint16_t)st.seed_time); }
    else rnga_seed((uint16_t)time(NULL));
    char sp[1200];
    if (save_dir) snprintf(sp, sizeof sp, "%s", save_dir);
    else snprintf(sp, sizeof sp, "%s/save", outdir);
    mkdirs_path(sp);
    {
        char *existing = path_find_ci(sp, "SSOWINCD.DAT");
        if (existing) { snprintf(ses.save_path, sizeof ses.save_path, "%s", existing); free(existing); }
        else snprintf(ses.save_path, sizeof ses.save_path, "%s/SSOWINCD.DAT", sp);
    }
    {
        size_t tl = 0;
        const uint8_t *tm = g_data_src ? vs_read(g_data_src, "SSOWINCD.DAT", &tl) : NULL;
        savedb_open_template(&ses.db, ses.save_path, tm, tl);   /* an existing database is used as is; else the data source's template; written at the first flush */
    }
    if (g_opts.home || (getenv("OUTNUMBERED_HOME") && *getenv("OUTNUMBERED_HOME"))) {      /* installed DOS files of a given base (14 section 2.3); the default profile is never touched in conformance runs */
        SetupOpts so = g_opts;
        so.save_dir = sp;
        Resolution rr;
        char er[300];
        if (setup_resolve(&so, &rr, er, sizeof er) == 0) { g_run = rr; g_run_valid = 1; if (rr.dos_from && !strcmp(rr.dos_from, "installed")) dosint_set_installed_dos(rr.dos); }
    }
    dosint_init(sp);
    g_script_hooks.audio = script_audio;
    g_script_hooks.volume = script_volume;
    for (int i = 0; i < st.nvolume; i++) script_volume(st.vol[i][0], st.vol[i][1], st.vol[i][2], st.vol[i][3]);
    for (int i = 0; i < st.naudio; i++) script_audio(st.audio[i][0], st.audio[i][1], st.audio[i][2][0] ? st.audio[i][2] : NULL);
    ses.player = -1;
    ses.sfx = ses.music = ses.voice = 1;
    ses.calc = 1; ses.clock_on = 1;
    if (!st.valid) script_finish(0);
    if (st.intro) { ses.fix_puzzle = 0; main_flow(); script_finish(0); }
    if (st.drill) {
        ses.drill_mode = 1;
        ses.tier = 0;
        memcpy(ses.cust_drill, tier_cust[0], 13);
        if (setjmp(jb_signin)) script_finish(0);     /* Return to Sign In: nothing to return to in script mode */
        drill_for_skill();
        script_finish(0);
    }
    int idx = savedb_find(&ses.db, st.name);
    int existing_player = idx >= 0;
    if (idx < 0) idx = savedb_add(&ses.db, st.name);
    ses.player = idx;
    if (st.score >= 0) savedb_set_score(&ses.db, idx, (uint32_t)st.score);   /* only the lifetime score is overwritten */
    if (existing_player) {
        uint8_t *r = ses_rec();
        ses.sfx = r[REC_SFX]; ses.music = r[REC_MUSIC]; ses.voice = r[REC_VOICE];
        ses.calc = r[REC_OPTIONS] & 1; ses.hints = (r[REC_OPTIONS] >> 1) & 1; ses.clock_on = (r[REC_OPTIONS] >> 2) & 1;
    }
    ses.rank = rank_for_score(ses_lifetime());
    ses.hints = (ses.rank == 0);                 /* New Player Hints = (rank == Trainee) when leaving sign-in */
    savedb_flush(&ses.db);
    ses.player = idx;
    /* sign-in: the game-state block is zeroed (the secret code with it) */
    memset(&gs, 0, sizeof gs);
    puzzle_clear_code();
    if (setjmp(jb_signin)) script_finish(0);
    setjmp(jb_newgame);
    ses.player = savedb_find(&ses.db, st.name);
    prepare_session();
    start_game();
    action_loop();
}

/* --------------------------------------------------------------- interactive */
static void quit_hook(void) { game_quit(); }

static void mkdirs_path(const char *p) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s", p);
    for (char *c = tmp + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(tmp, 0777); *c = '/'; }
    mkdir(tmp, 0777);
}

/* Try to make the game data of a resolution available (03 section 1.1 steps 6-7). 0 = ok. */
static int load_game_data(const Resolution *r, int explicit_data) {
    int nopened = data_source_init(r->data, 0);
    if (nopened < 0) return -1;
    int complete = nopened >= 1 && res_find("RRGB", 128, NULL) != NULL;
    static const char *need[6] = {"SSO1.DAT", "SSO2.DAT", "SSO3.DAT", "SND.DAT", "SPEECH.DAT", "SSOWINCD.EXE"};
    for (int i = 0; i < 6 && complete; i++) {
        int found = 0;
        for (int k = 0; k < res_file_count(); k++) if (!strcasecmp(res_file(k)->base, need[i])) found = 1;
        if (!found) complete = 0;
    }
    if (!complete) {
        if (explicit_data)
            fprintf(stderr, "engine: the OutNumbered! CD files were not found in '%s'.\n"
                            "Use --data DIR where DIR holds SSO1.DAT, SSO2.DAT, SSO3.DAT, SND.DAT, SPEECH.DAT and SSOWINCD.EXE.\n", r->data);
        data_source_reset();
        return -1;
    }
    return 0;
}

/* copy the player database and settings of the old default folder (<base>) into <base>/saves once (14 section 2.3, optional) */
static void migrate_old_saves(const Resolution *r) {
    if (strcmp(r->save_from, "default")) return;
    static const char *names[2] = {"SSOWINCD.DAT", "outnumbered.ini"};
    for (int i = 0; i < 2; i++) {
        char *have = path_find_ci(r->save_dir, names[i]);
        if (have) { free(have); return; }
    }
    for (int i = 0; i < 2; i++) {
        char *old = path_find_ci(r->home, names[i]);
        if (!old) continue;
        size_t n;
        uint8_t *b = file_read_all(old, &n);
        free(old);
        if (!b) continue;
        mkdirs_path(r->save_dir);
        char dst[1300];
        snprintf(dst, sizeof dst, "%s/%s", r->save_dir, names[i]);
        FILE *f = fopen(dst, "wb");
        if (f) { fwrite(b, 1, n, f); fclose(f); chmod(dst, 0600); }
        free(b);
    }
}

void shell_main(const char *data_dir, const char *save_dir, const char *soundfont, int fullscreen) {
    (void)data_dir; (void)save_dir;
    SetupOpts o = g_opts;
    Resolution r;
    char err[300];
    if (setup_resolve(&o, &r, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); exit(1); }
    g_run = r;
    g_run_valid = 1;
    g_data_dir = r.data;
    int ready = 0;
    if (!r.setup) {
        if (load_game_data(&r, !strcmp(r.data_from, "option")) == 0) ready = 1;
        else if (!strcmp(r.data_from, "option")) exit(1);                      /* explicit --data: a message and exit */
    }
    if (plat_sdl_start(soundfont, fullscreen) != 0) die("cannot open the display");
    int load_failed = !ready && !r.setup;
    while (!ready) {
        int rc = setup_screen_run(&o, load_failed);
        if (rc == 0) { plat_sdl_stop(); exit(0); }
        o.setup = 0;                                                          /* as if started with only --home, --save-dir and the audio options */
        o.data = NULL;
        if (setup_resolve(&o, &r, err, sizeof err) != 0) { fprintf(stderr, "%s\n", err); exit(1); }
        g_run = r;
        load_failed = 0;
        if (r.setup) continue;
        if (load_game_data(&r, 0) == 0) ready = 1; else load_failed = 1;
    }
    g_data_dir = r.data;
    plat_sdl_set_title("OutNumbered! CD");
    if (g_eng.work) eng_shutdown();
    eng_init();
    if (pal_load(128) != 0) die("palette RRGB 128 not found in the data files");
    audio_init(r.data);
    world_load();
    puzzle_reset();
    rnga_seed((uint16_t)time(NULL));
    migrate_old_saves(&r);
    const char *dir = r.save_dir;
    mkdirs_path(dir);
    snprintf(ses.save_path, sizeof ses.save_path, "%s/SSOWINCD.DAT", dir);
    {
        size_t tl = 0;
        const uint8_t *tm = g_data_src ? vs_read(g_data_src, "SSOWINCD.DAT", &tl) : NULL;
        char *existing = path_find_ci(dir, "SSOWINCD.DAT");
        if (existing) { snprintf(ses.save_path, sizeof ses.save_path, "%s", existing); free(existing); }
        savedb_open_template(&ses.db, ses.save_path, tm, tl);
    }
    dosint_set_installed_dos(r.dos_from && !strcmp(r.dos_from, "installed") ? r.dos : NULL);
    dosint_init(dir);
    {
        AudioSettings as;
        dosint_get_settings(&as);
        plat_sdl_load_soundfont(as.soundfont[0] ? as.soundfont : NULL);
    }
    ses.player = -1;
    ses.sfx = ses.music = ses.voice = 1;
    ses.calc = 1; ses.clock_on = 1;
    g_quit_hook = quit_hook;
    ses.fix_puzzle = 1;
    main_flow();
}
