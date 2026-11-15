/* save.h - SSOWINCD.DAT player database (spec 03 section 4) */
#ifndef SAVE_H
#define SAVE_H
#include "util.h"

#define SAVE_MAGIC_LEN 4
#define SAVE_HDR_SIZE 1508
#define SAVE_REC_SIZE 168
#define SAVE_NREC 99
#define SAVE_FILE_SIZE (SAVE_HDR_SIZE + SAVE_NREC * SAVE_REC_SIZE)
#define SAVE_ENTRY_SIZE 15
#define STATE_SIZE 99

/* record offsets */
enum {
    REC_NAME = 0x00, REC_SCORE = 0x09, REC_SLOT = 0x0D, REC_GAME_CUST = 0x0F, REC_DRILL_CUST = 0x1C,
    REC_FLAGS = 0x29, REC_SAVED = 0x2B, REC_STATE = 0x2C, REC_SEEDS = 0x8F, REC_SFX = 0xA3,
    REC_MUSIC = 0xA4, REC_VOICE = 0xA5, REC_STAGE = 0xA6, REC_OPTIONS = 0xA7
};

typedef struct SaveDB {
    char path[1024];
    uint8_t data[SAVE_FILE_SIZE];
    int loaded_from_file;
} SaveDB;

/* Load `path`; if it does not exist, a fresh database is created in memory (and written on flush). */
int savedb_open(SaveDB *db, const char *path);
int savedb_open_template(SaveDB *db, const char *path, const uint8_t *tmpl, size_t tlen);   /* spec 13 section 6.1 */
int savedb_flush(SaveDB *db);                     /* write whole file; 0 on success */
int savedb_count(const SaveDB *db);
const char *savedb_name(const SaveDB *db, int idx);
uint32_t savedb_score(const SaveDB *db, int idx);
int savedb_slot(const SaveDB *db, int idx);
uint8_t *savedb_record(SaveDB *db, int slot);
uint8_t *savedb_player(SaveDB *db, int idx);      /* record of list entry idx */
int savedb_find(const SaveDB *db, const char *name);   /* list index or -1 (case-insensitive) */
int savedb_add(SaveDB *db, const char *name);     /* returns list index of new player or -1 if full */
void savedb_remove(SaveDB *db, int idx);
void savedb_sync_entry(SaveDB *db, int idx);      /* copy first 15 bytes of record into list entry */
void savedb_set_score(SaveDB *db, int idx, uint32_t score);

/* conformance: decode FILE to JSON in the format of fixtures/save_expected.json */
int savedb_dump_json(const char *file, const char *out);

#endif
