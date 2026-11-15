#include "save.h"
#include "res.h"
#include "rules.h"
#include <strings.h>

static void fresh_record(uint8_t *r, int slot) {
    memset(r, 0, SAVE_REC_SIZE);
    r[REC_SLOT] = (uint8_t)slot;
    r[REC_SLOT + 1] = (uint8_t)(slot >> 8);
    r[REC_FLAGS] = 1;
    r[REC_SFX] = r[REC_MUSIC] = r[REC_VOICE] = 1;
    r[REC_OPTIONS] = 7;
}

static void fresh_db(SaveDB *db) {
    memset(db->data, 0, sizeof db->data);
    memcpy(db->data, "USR\x1a", 4);
    for (int i = 0; i < SAVE_NREC; i++) {
        uint8_t *e = db->data + 8 + i * SAVE_ENTRY_SIZE;
        e[13] = (uint8_t)i;
        fresh_record(db->data + SAVE_HDR_SIZE + i * SAVE_REC_SIZE, i);
    }
}

int savedb_open(SaveDB *db, const char *path) {
    memset(db, 0, sizeof *db);
    snprintf(db->path, sizeof db->path, "%s", path);
    size_t n = 0;
    uint8_t *b = file_read_all(path, &n);
    if (b && n == SAVE_FILE_SIZE && memcmp(b, "USR\x1a", 4) == 0) {
        memcpy(db->data, b, n);
        free(b);
        db->loaded_from_file = 1;
        if (savedb_count(db) > SAVE_NREC) { db->data[6] = SAVE_NREC; db->data[7] = 0; }
        return 0;
    }
    free(b);
    fresh_db(db);
    return 0;
}

/* Like savedb_open; when there is no database file yet, a valid template (the data source's SSOWINCD.DAT:
 * exactly SAVE_FILE_SIZE bytes, "USR\x1a") is the initial content. Nothing is written until the first flush. */
int savedb_open_template(SaveDB *db, const char *path, const uint8_t *tmpl, size_t tlen) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return savedb_open(db, path); }
    if (tmpl && tlen == SAVE_FILE_SIZE && !memcmp(tmpl, "USR\x1a", 4)) {
        memset(db, 0, sizeof *db);
        snprintf(db->path, sizeof db->path, "%s", path);
        memcpy(db->data, tmpl, tlen);
        if (savedb_count(db) > SAVE_NREC) { db->data[6] = SAVE_NREC; db->data[7] = 0; }
        return 0;
    }
    return savedb_open(db, path);
}

int savedb_flush(SaveDB *db) {
    FILE *f = fopen(db->path, "wb");
    if (!f) return -1;
    size_t w = fwrite(db->data, 1, sizeof db->data, f);
    fclose(f);
    return w == sizeof db->data ? 0 : -1;
}

int savedb_count(const SaveDB *db) { return rd16(db->data + 6); }
static const uint8_t *entry(const SaveDB *db, int idx) { return db->data + 8 + idx * SAVE_ENTRY_SIZE; }
const char *savedb_name(const SaveDB *db, int idx) { return (const char *)entry(db, idx); }
uint32_t savedb_score(const SaveDB *db, int idx) { return rd32(entry(db, idx) + 9); }
int savedb_slot(const SaveDB *db, int idx) { return rd16(entry(db, idx) + 13); }
uint8_t *savedb_record(SaveDB *db, int slot) {
    if (slot < 0 || slot >= SAVE_NREC) return NULL;
    return db->data + SAVE_HDR_SIZE + slot * SAVE_REC_SIZE;
}
uint8_t *savedb_player(SaveDB *db, int idx) { return savedb_record(db, savedb_slot(db, idx)); }

int savedb_find(const SaveDB *db, const char *name) {
    int n = savedb_count(db);
    for (int i = 0; i < n; i++)
        if (strncasecmp(savedb_name(db, i), name, 8) == 0 && strlen(name) <= 8) return i;
    return -1;
}

void savedb_sync_entry(SaveDB *db, int idx) {
    uint8_t *rec = savedb_player(db, idx);
    if (rec) memcpy(db->data + 8 + idx * SAVE_ENTRY_SIZE, rec, SAVE_ENTRY_SIZE);
}

void savedb_set_score(SaveDB *db, int idx, uint32_t score) {
    if (score > 9999999) score = 9999999;
    uint8_t *rec = savedb_player(db, idx);
    rec[REC_SCORE] = (uint8_t)score; rec[REC_SCORE + 1] = (uint8_t)(score >> 8);
    rec[REC_SCORE + 2] = (uint8_t)(score >> 16); rec[REC_SCORE + 3] = (uint8_t)(score >> 24);
    savedb_sync_entry(db, idx);
}

int savedb_add(SaveDB *db, const char *name) {
    int n = savedb_count(db);
    if (n >= SAVE_NREC) return -1;
    int pos = 0;
    while (pos < n && strcasecmp(savedb_name(db, pos), name) < 0) pos++;
    uint8_t *base = db->data + 8;
    /* free slots are kept at the end of the list: entry n carries the next free slot index */
    int new_slot = rd16(base + n * SAVE_ENTRY_SIZE + 13);
    memmove(base + (pos + 1) * SAVE_ENTRY_SIZE, base + pos * SAVE_ENTRY_SIZE, (size_t)(n - pos) * SAVE_ENTRY_SIZE);
    memset(base + pos * SAVE_ENTRY_SIZE, 0, SAVE_ENTRY_SIZE);
    base[pos * SAVE_ENTRY_SIZE + 13] = (uint8_t)new_slot;
    base[pos * SAVE_ENTRY_SIZE + 14] = (uint8_t)(new_slot >> 8);
    db->data[6] = (uint8_t)(n + 1);
    db->data[7] = 0;
    uint8_t *rec = savedb_record(db, new_slot);
    fresh_record(rec, new_slot);
    size_t l = strlen(name);
    if (l > 8) l = 8;
    for (size_t i = 0; i < l; i++) rec[i] = (uint8_t)name[i];
    savedb_sync_entry(db, pos);
    return pos;
}

void savedb_remove(SaveDB *db, int idx) {
    int n = savedb_count(db);
    if (idx < 0 || idx >= n) return;
    uint8_t *base = db->data + 8;
    uint8_t removed[SAVE_ENTRY_SIZE];
    memcpy(removed, base + idx * SAVE_ENTRY_SIZE, SAVE_ENTRY_SIZE);
    int slot = rd16(removed + 13);
    memmove(base + idx * SAVE_ENTRY_SIZE, base + (idx + 1) * SAVE_ENTRY_SIZE, (size_t)(n - 1 - idx) * SAVE_ENTRY_SIZE);
    /* free entry goes to position n-1 carrying the freed slot */
    uint8_t *e = base + (n - 1) * SAVE_ENTRY_SIZE;
    memset(e, 0, SAVE_ENTRY_SIZE);
    e[13] = (uint8_t)slot; e[14] = (uint8_t)(slot >> 8);
    fresh_record(savedb_record(db, slot), slot);
    db->data[6] = (uint8_t)(n - 1);
    db->data[7] = 0;
}

/* ------------------------------------------------------------------ conformance dump */
static void jstr(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 0x20 || c >= 0x7F) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}
static void jhex(FILE *f, const uint8_t *p, int n) {
    fputc('"', f);
    for (int i = 0; i < n; i++) fprintf(f, "%02x", p[i]);
    fputc('"', f);
}
static void jarr8(FILE *f, const uint8_t *p, int n) {
    fputc('[', f);
    for (int i = 0; i < n; i++) fprintf(f, "%s%d", i ? ", " : "", p[i]);
    fputc(']', f);
}

static void dump_state(FILE *f, const uint8_t *s) {
    fprintf(f, "{\"seed\": %d, \"game_score\": %d, \"clock\": %d, \"level\": %d, \"energy\": %d, \"pending_energy\": %d,\n",
            rd16(s + 0), rd16(s + 2), rd16(s + 4), s[6], s[7], s[8]);
    fprintf(f, "   \"object_state\": "); jarr8(f, s + 0x09, 10);
    fprintf(f, ", \"object_op\": "); jarr8(f, s + 0x13, 10);
    fprintf(f, ", \"object_question\": "); jarr8(f, s + 0x1D, 10);
    fprintf(f, ", \"object_wrong\": "); jarr8(f, s + 0x27, 10);
    fprintf(f, ",\n   \"room_clues\": [");
    for (int r = 0; r < 5; r++) {
        fprintf(f, "%s[", r ? ", " : "");
        for (int k = 0; k < 4; k++) {
            int b = s[0x31 + r * 4 + k];
            fprintf(f, "%s{\"pic\": %d, \"revealed\": %s}", k ? ", " : "", b & 0x7F, (b & 0x80) ? "false" : "true");
        }
        fprintf(f, "]");
    }
    fprintf(f, "],\n   \"code\": [");
    for (int k = 0; k < 4; k++) {
        int b = s[0x45 + k];
        fprintf(f, "%s{\"pic\": %d, \"captured\": %s}", k ? ", " : "", b & 0x7F, (b & 0x80) ? "false" : "true");
    }
    fprintf(f, "], \"code_pieces\": %d, \"match_rule\": %d,\n   \"hint_flags\": ", rd16(s + 0x49), rd16(s + 0x4B));
    jarr8(f, s + 0x4D, 18);
    fprintf(f, ", \"hideout\": %d}", rd16(s + 0x61));
}

static void dump_record(FILE *f, const uint8_t *r) {
    fprintf(f, "{\"slot\": %d, \"game_customisation\": ", rd16(r + REC_SLOT));
    jhex(f, r + REC_GAME_CUST, 13);
    fprintf(f, ", \"drill_customisation\": ");
    jhex(f, r + REC_DRILL_CUST, 13);
    fprintf(f, ", \"customised_flags\": %d, \"flags\": %d, \"saved_game_present\": %d,\n", r[REC_FLAGS], r[REC_FLAGS], r[REC_SAVED]);
    if (r[REC_SAVED]) {
        fprintf(f, "  \"state\": ");
        dump_state(f, r + REC_STATE);
        fprintf(f, ",\n");
    }
    fprintf(f, "  \"object_seeds\": [");
    for (int i = 0; i < 10; i++) fprintf(f, "%s%d", i ? ", " : "", rds16(r + REC_SEEDS + i * 2));
    fprintf(f, "], \"sfx\": %d, \"music\": %d, \"voice\": %d, \"rank_intro_stage\": %d,\n", r[REC_SFX], r[REC_MUSIC], r[REC_VOICE], r[REC_STAGE]);
    int o = r[REC_OPTIONS];
    fprintf(f, "  \"options_byte\": %d, \"options\": {\"calculator\": %s, \"hints\": %s, \"clock\": %s}}", o,
            (o & 1) ? "true" : "false", (o & 2) ? "true" : "false", (o & 4) ? "true" : "false");
}

int savedb_dump_json(const char *file, const char *out) {
    static SaveDB db;
    size_t n = 0;
    uint8_t *b = file_read_all(file, &n);
    if (!b) { fprintf(stderr, "cannot read %s\n", file); return 1; }
    if (n != SAVE_FILE_SIZE || memcmp(b, "USR\x1a", 4) != 0) {
        fprintf(stderr, "%s: not a player database (size %zu)\n", file, n);
        free(b);
        return 1;
    }
    memcpy(db.data, b, n);
    free(b);
    FILE *f = fopen(out, "w");
    if (!f) return 1;
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;
    int cnt = savedb_count(&db);
    fprintf(f, "{\"file\": "); jstr(f, base);
    fprintf(f, ", \"size\": %zu, \"player_count\": %d,\n \"header_list\": [", n, cnt);
    for (int i = 0; i < cnt; i++) {
        fprintf(f, "%s{\"name\": ", i ? ", " : "");
        char nm[10]; memcpy(nm, savedb_name(&db, i), 9); nm[9] = 0;
        jstr(f, nm);
        fprintf(f, ", \"lifetime\": %u, \"slot\": %d}", savedb_score(&db, i), savedb_slot(&db, i));
    }
    fprintf(f, "],\n \"rank_names\": {");
    for (int i = 0; i < cnt; i++) {
        char nm[10]; memcpy(nm, savedb_name(&db, i), 9); nm[9] = 0;
        fprintf(f, "%s", i ? ", " : "");
        jstr(f, nm);
        fprintf(f, ": \"%s\"", rank_names[rank_for_score(savedb_score(&db, i))]);
    }
    fprintf(f, "}");
    for (int i = 0; i < cnt; i++) {
        char nm[10]; memcpy(nm, savedb_name(&db, i), 9); nm[9] = 0;
        fprintf(f, ",\n ");
        jstr(f, nm);
        fprintf(f, ": ");
        dump_record(f, savedb_player(&db, i));
    }
    fprintf(f, ",\n \"untouched_record_example\": ");
    dump_record(f, savedb_record(&db, 50));
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}
