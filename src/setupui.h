/* setupui.h - the setup model (rows, messages, buttons, phases) and the setup screen (spec chapter 14 sections 4, 7) */
#ifndef SETUPUI_H
#define SETUPUI_H
#include "setup.h"

enum { PH_READY, PH_INSTALLING, PH_COMPLETE };
enum { MK_NONE, MK_INFO, MK_SUCCESS, MK_ERROR };
enum { FOCUS_FIELD, FOCUS_B1, FOCUS_B2 };

typedef struct Msg { int kind; char code[32]; char text[900]; } Msg;

typedef struct SetupModel {
    char home[1024];
    Row row[2];                        /* 0 windows, 1 dos */
    int phase;
    char field[4200];
    int caret;                         /* in code points */
    int focus;
    Msg msg;
    char last_result[32];
    Installer *inst;
    Manifest man;
    int countdown;                     /* ticks until the automatic start (-1 = none) */
    char (*drop_msgs)[32];
    int ndrop_msgs;
    int classifying;
    uint64_t units_per_tick;
    int installed_parts[2];            /* parts copied by the installs of this model */
    char install_err[32];
    int blink;                         /* tick counter for the caret */
    int want_start, want_quit;
    /* asynchronous classification (interactive build) */
    struct AsyncJob *async;
    int async_submit;                  /* the job came from Enter in the field */
} SetupModel;

void model_init(SetupModel *m, const char *home, int load_failed);
void model_free(SetupModel *m);
void model_reload_manifest(SetupModel *m);
void model_drop(SetupModel *m, char **paths, int n);              /* one drop (14 section 4.3), synchronous */
void model_submit_text(SetupModel *m, const char *text);
/* interactive: stage 1 now, stage 2 on a worker thread; model_poll_async applies the results */
void model_drop_async(SetupModel *m, char **paths, int n, int from_field);
void model_poll_async(SetupModel *m);          /* Enter in the field with text */
const char *model_b1_label(const SetupModel *m);
int model_b1_enabled(const SetupModel *m);
void model_activate_b1(SetupModel *m);                            /* Install / Retry / Play */
void model_start_install(SetupModel *m);
void model_cancel(SetupModel *m);
void model_tick(SetupModel *m);
void model_row_status(const SetupModel *m, int which, char *out, size_t n);   /* the status text of section 7.3 */
void model_state_json(const SetupModel *m, FILE *f);              /* the "setup" object of 14 section 10.3 */
void model_row_json(const Row *r, FILE *f, int with_text, const char *text);
char *normalize_input(const char *in, const char *script_dir);    /* 14 section 4.2 -> malloc'd absolute path */

/* the screen: returns 1 = start the game (resolve again), 0 = quit */
int setup_screen_run(const SetupOpts *opts, int load_failed);
int setup_screen_active(void);
void setup_script_event(int kind, const char *arg);               /* script commands drop / type (handled at once) */

int conform_install(int argc, char **argv, const char *out);       /* engine --conform install --home DIR [SRC...] --out f */
int conform_resolve(int argc, char **argv, const char *out);
#endif
