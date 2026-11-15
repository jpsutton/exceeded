#include "aseq.h"
#include "res.h"
#include "rng.h"

Engine g_eng;

#define MAX_AO 512
#define MAX_SE 1024
static AO *ao_list[MAX_AO];
static int n_ao;
static SE *se_list[MAX_SE];
static int n_se;
static int ao_serial;

/* ---------------------------------------------------------------- ASEQ */
#define MAX_ASEQ 256
static Aseq *aseq_cache[MAX_ASEQ];
static int n_aseq;

static int parse_seqs(const uint8_t *d, size_t len, size_t *pos, int nseq, Seq *seq) {
    size_t p = *pos;
    for (int i = 0; i < nseq; i++) {
        if (p + 2 > len) return -1;
        int n = rd16(d + p);
        p += 2;
        if (p + (size_t)n * 8 > len) return -1;
        seq[i].n = n;
        seq[i].e = xmalloc(sizeof(SeqEnt) * (size_t)(n ? n : 1));
        for (int k = 0; k < n; k++, p += 8) {
            seq[i].e[k].a = rds16(d + p);
            seq[i].e[k].b = rds16(d + p + 2);
            seq[i].e[k].c = rds16(d + p + 4);
            seq[i].e[k].d = rds16(d + p + 6);
        }
    }
    *pos = p;
    return 0;
}

Aseq *aseq_load(int id) {
    for (int i = 0; i < n_aseq; i++)
        if (aseq_cache[i]->id == id) return aseq_cache[i];
    size_t len;
    const uint8_t *d = res_get("ASEQ", id, &len);
    if (!d || len < 10 || n_aseq >= MAX_ASEQ) return NULL;
    Aseq *a = xcalloc(1, sizeof *a);
    a->id = id;
    a->data = d;
    a->len = len;
    a->nseq = rd16(d);
    a->ncel = rd16(d + 2);
    a->fps = rd16(d + 4);
    a->w3 = rd16(d + 6);
    a->w4 = rd16(d + 8);
    if (a->fps == 0) a->fps = 10;
    size_t p = 10;
    if (p + (size_t)a->ncel * 4 + (size_t)a->nseq * 4 > len) { free(a); return NULL; }
    a->celoff = xmalloc(sizeof(uint32_t) * (size_t)(a->ncel ? a->ncel : 1));
    for (int i = 0; i < a->ncel; i++) a->celoff[i] = rd32(d + p + (size_t)i * 4);
    p += (size_t)a->ncel * 4 + (size_t)a->nseq * 4;
    a->seq = xcalloc((size_t)(a->nseq ? a->nseq : 1), sizeof(Seq));
    if (parse_seqs(d, len, &p, a->nseq, a->seq) != 0) { free(a); return NULL; }
    a->celbase = p;
    a->cel = xcalloc((size_t)(a->ncel ? a->ncel : 1), sizeof(Cel));
    a->cel_ok = xcalloc((size_t)(a->ncel ? a->ncel : 1), 1);
    aseq_cache[n_aseq++] = a;
    return a;
}

const Cel *aseq_cel(Aseq *a, int n) {
    if (!a || n < 0 || n >= a->ncel) return NULL;
    if (!a->cel_ok[n]) {
        size_t off = a->celbase + a->celoff[n];
        if (off >= a->len) a->cel_ok[n] = 2;
        else a->cel_ok[n] = cel_decode(a->data + off, a->len - off, &a->cel[n]) == 0 ? 1 : 2;
        /* a failed decode still leaves a usable (partial) cel */
        if (a->cel_ok[n] == 2 && !a->cel[n].pix) return NULL;
    }
    return &a->cel[n];
}

int aseq_attach_eseq(Aseq *a, int eseq_id) {
    if (!a) return -1;
    if (a->eseq) {
        for (int i = 0; i < a->neseq; i++) free(a->eseq[i].e);
        free(a->eseq);
        a->eseq = NULL;
        a->neseq = 0;
    }
    a->eseq_id = 0;
    if (eseq_id == 0) return 0;
    size_t len;
    const uint8_t *d = res_get("ESEQ", eseq_id, &len);
    if (!d || len < 2) return -1;
    int n = rd16(d);
    size_t p = 2 + (size_t)n * 4;
    Seq *s = xcalloc((size_t)(n ? n : 1), sizeof(Seq));
    if (parse_seqs(d, len, &p, n, s) != 0) { free(s); return -1; }
    a->eseq = s;
    a->neseq = n;
    a->eseq_id = eseq_id;
    return 0;
}

Seq *aseq_get_seq(Aseq *a, int n) {
    if (n >= 0) return n < a->nseq ? &a->seq[n] : NULL;
    int k = -n - 1;
    return (a->eseq && k < a->neseq) ? &a->eseq[k] : NULL;
}

/* ---------------------------------------------------------------- engine */
void eng_init(void) {
    memset(&g_eng, 0, sizeof g_eng);
    g_eng.work = canvas_new(CANVAS_W, CANVAS_H);
    g_eng.back = canvas_new(CANVAS_W, CANVAS_H);
    g_eng.viewport = (Rect){8, 35, 496, 249};
}
void eng_shutdown(void) {
    for (int i = n_ao - 1; i >= 0; i--) ao_destroy(ao_list[i]);
    canvas_free(g_eng.work);
    canvas_free(g_eng.back);
}

int eng_se_count(void) { return n_se; }

static void se_insert(SE *se) {
    if (n_se >= MAX_SE) { free(se); return; }
    int i = n_se;
    while (i > 0 && se_list[i - 1]->z > se->z) { se_list[i] = se_list[i - 1]; i--; }
    se_list[i] = se;
    n_se++;
}
static void se_remove(SE *se) {
    for (int i = 0; i < n_se; i++)
        if (se_list[i] == se) {
            memmove(&se_list[i], &se_list[i + 1], sizeof(SE *) * (size_t)(n_se - i - 1));
            n_se--;
            break;
        }
}

static int rect_hit(Rect a, Rect b) {
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
static Rect rect_clip(Rect r, Rect c) {
    int x0 = r.x > c.x ? r.x : c.x, y0 = r.y > c.y ? r.y : c.y;
    int x1 = r.x + r.w < c.x + c.w ? r.x + r.w : c.x + c.w;
    int y1 = r.y + r.h < c.y + c.h ? r.y + r.h : c.y + c.h;
    Rect o = {x0, y0, x1 - x0, y1 - y0};
    if (o.w < 0) o.w = 0;
    if (o.h < 0) o.h = 0;
    return o;
}
/* restore work from back under an SE and flag every overlapping SE for redraw (spec 02 5.3) */
static void se_restore(SE *se) {
    if (se->r.w > 0 && se->r.h > 0) canvas_copy_rect(g_eng.work, g_eng.back, se->r);
    for (int i = 0; i < n_se; i++) {
        SE *o = se_list[i];
        if (o != se && rect_hit(o->r, se->r)) o->flags |= SE_OVERLAP;
    }
}
void ao_erase(AO *ao) {
    for (int i = 0; i < ao->nse; i++) {
        se_restore(ao->se[i]);
        se_remove(ao->se[i]);
        free(ao->se[i]);
    }
    ao->nse = 0;
}

static void ao_list_remove(AO *ao) {
    for (int i = 0; i < n_ao; i++)
        if (ao_list[i] == ao) {
            memmove(&ao_list[i], &ao_list[i + 1], sizeof(AO *) * (size_t)(n_ao - i - 1));
            n_ao--;
            break;
        }
    ao->in_list = 0;
}
static void ao_list_insert(AO *ao) {
    if (ao->in_list) ao_list_remove(ao);
    if (n_ao >= MAX_AO) return;
    int i = n_ao;
    while (i > 0 && ao_list[i - 1]->z > ao->z) { ao_list[i] = ao_list[i - 1]; i--; }
    ao_list[i] = ao;
    n_ao++;
    ao->in_list = 1;
}

AO *ao_create(int aseq_id) {
    Aseq *a = aseq_load(aseq_id);
    if (!a) return NULL;
    AO *ao = xcalloc(1, sizeof *ao);
    ao->as = a;
    ao->serial = ++ao_serial;
    ao->period = 100 / a->fps;
    if (ao->period < 1) ao->period = 1;
    ao->label = -1;
    ao->labelpos = -1;
    ao->loops = 1;
    ao->seq = &a->seq[0];
    return ao;
}

void ao_destroy(AO *ao) {
    if (!ao) return;
    ao_erase(ao);
    if (ao->in_list) ao_list_remove(ao);
    for (int i = 0; i < n_ao; i++)
        if (ao_list[i]->link == ao) ao_list[i]->link = NULL;
    free(ao);
}

void ao_destroy_level(int level) {
    for (int i = n_ao - 1; i >= 0; i--)
        if (ao_list[i]->level == level) ao_destroy(ao_list[i]);
}

void ao_set_seq(AO *ao, int seq) {
    Seq *s = aseq_get_seq(ao->as, seq);
    if (!s) return;
    ao->seqno = seq;
    ao->seq = s;
}

void ao_start(AO *ao, int seq) {
    ao_set_seq(ao, seq);
    ao->idx = 0;
    ao->state = AO_RUNNING;
    ao->next_time = 0;
    ao->rcount = 0;
    ao->label = -1;
    ao->labelpos = -1;
    ao->hidden = 0;
    /* leading SETPOS entries also set z (spec 02 2.3) */
    for (int i = 0; i < ao->seq->n && ao->seq->e[i].c == -6; i++) ao->z = ao->seq->e[i].d > 0 ? ao->seq->e[i].d : 0;
    ao_list_insert(ao);
}

void ao_force_redraw(AO *ao) { for (int i = 0; i < ao->nse; i++) ao->se[i]->flags |= SE_REDRAW; }

static void se_place(SE *se, int x, int y) {
    se->x = x;
    se->y = y;
    Rect full = {0, 0, CANVAS_W, CANVAS_H};
    Rect cl = (se->flags & SE_VIEWPORT) ? g_eng.viewport : full;
    se->r = rect_clip((Rect){x, y, se->cel->w, se->cel->h}, cl);
}
void ao_set_pos(AO *ao, int x, int y) {
    int ddx = x - ao->x, ddy = y - ao->y;
    ao->x = x;
    ao->y = y;
    for (int i = 0; i < ao->nse; i++) {
        SE *se = ao->se[i];
        se_restore(se);
        se_place(se, se->x + ddx, se->y + ddy);
        se->flags |= SE_DRAW;
    }
}
void ao_set_z(AO *ao, int z) {
    ao->z = z;
    for (int i = 0; i < ao->nse; i++) {
        se_restore(ao->se[i]);
        ao->se[i]->flags |= SE_DRAW;
        se_remove(ao->se[i]);
        ao->se[i]->z = z;
        se_insert(ao->se[i]);
    }
    if (ao->in_list) ao_list_insert(ao);
}
void ao_set_fps(AO *ao, int fps) {
    if (fps < 1) fps = 1;
    ao->period = 100 / fps;
    if (ao->period < 1) ao->period = 1;
}
/* a missing resource gives a NULL AO (the engine then runs the sequence without it): these accept it */
void ao_stop(AO *ao) { if (!ao) return; ao->state = AO_STOPPED; ao_erase(ao); }
void ao_hide(AO *ao) { if (!ao) return; ao->hidden = 1; ao_erase(ao); }
void ao_show(AO *ao) { if (!ao) return; ao->hidden = 0; }
void ao_pause(AO *ao) { if (!ao) return; ao->state = AO_PAUSED; }
void ao_resume(AO *ao) { if (ao && ao->state == AO_PAUSED) ao->state = AO_RUNNING; }

void eng_stamp_cel(Aseq *as, int cel, int x, int y, int into_back) {
    const Cel *c = aseq_cel(as, cel);
    if (!c) return;
    canvas_blit_cel(g_eng.back, c, x, y, 0, NULL);
    if (!into_back) canvas_blit_cel(g_eng.work, c, x, y, 0, NULL);
}

/* Build SEs for the AO's current frame (entry idx, count cels) */
static void ao_build_frame(AO *ao, int count) {
    for (int k = 0; k < count && ao->nse < AO_MAX_SE; k++) {
        int ei = ao->idx + k;
        if (ei >= ao->seq->n) break;
        const SeqEnt *e = &ao->seq->e[ei];
        if (e->c < 0) break;
        const Cel *c = aseq_cel(ao->as, e->c);
        if (!c) continue;
        SE *se = xcalloc(1, sizeof *se);
        se->owner = ao;
        se->cel = c;
        se->dx = e->a;
        se->dy = e->b;
        se->z = ao->z;
        se->flags = SE_DRAW | ((e->d & 1) ? SE_MIRROR : 0) | (ao->use_viewport ? SE_VIEWPORT : 0);
        se_place(se, ao->x + e->a, ao->y + e->b);
        if (se->r.w <= 0 || se->r.h <= 0) { free(se); continue; }   /* fully clipped */
        if (ao->stamp_next) {
            /* STAMP: drawn permanently into both buffers, never erased */
            const Rect *cl = (se->flags & SE_VIEWPORT) ? &g_eng.viewport : NULL;
            canvas_blit_cel(g_eng.back, c, se->x, se->y, e->d & 1, cl);
            canvas_blit_cel(g_eng.work, c, se->x, se->y, e->d & 1, cl);
            free(se);
            continue;
        }
        se_insert(se);
        ao->se[ao->nse++] = se;
    }
    ao->stamp_next = 0;
}

static int link_valid(AO *ao) { return ao->link != NULL; }

/* Execute script for one AO whose timer is due. Returns 1 if a frame was built. */
int ao_update(AO *ao) {
    int64_t now = g_eng.now;
    if (now < ao->next_time) return 0;
    if (ao->next_time == 0) ao->next_time = now;
    int64_t late = now - ao->next_time;
    ao->next_time = now + ao->period;
    int catchup = (ao->allow_catchup && late >= ao->period) ? 1 : 0;
    if (ao->state != AO_RUNNING) return 0;
    ao_erase(ao);
    if (ao->hidden) return 0;
    int count = 1;
    int start_idx = ao->idx;
    int guard = 0;
    for (;;) {
        if (++guard > 4096) return 0;  /* script without frames */
        if (ao->idx >= ao->seq->n) { ao_stop(ao); return 0; }
        SeqEnt e = ao->seq->e[ao->idx];
        if (e.c >= 0) {
            if (catchup) { ao->idx += count; catchup--; count = 1; continue; }
            break;
        }
        int c = e.c;
        if (c <= -500) {
            int r = g_eng.user_hook ? g_eng.user_hook(ao, c, e.a, e.b, e.d) : 0;
            if (r == 0) ao->idx++;
            if (r & 1) ao->idx = start_idx;
            continue;   /* interpretation continues within the same tick (spec v1.1) */
        }
        switch (c) {
        case -1:  /* END */
            ao->state = AO_STOPPED;
            ao_erase(ao);
            ao->loops = 1;
            return 0;
        case -2:  /* LOOP */
            ao->loops++;
            ao->idx = 0;
            break;
        case -3:  /* MULTI */
            count = e.a > 0 ? e.a : 1;
            ao->idx++;
            break;
        case -4:  /* PAUSE */
            ao->state = AO_PAUSED;
            ao->idx++;
            break;
        case -5: { /* GOTO-LABEL (literal behaviour, see spec 02 2.3) */
            if (ao->label == e.a && ao->labelpos >= 0) { ao->idx = ao->labelpos; break; }
            int found = -1;
            for (int i = 0; i < ao->seq->n; i++) {
                const SeqEnt *s = &ao->seq->e[i];
                if (s->c == -1) break;
                if (s->c == -7 || s->a == e.a) found = i;
            }
            ao->idx = found >= 0 ? found + 1 : ao->idx + 1;
            break;
        }
        case -6:  /* SETPOS */
            if ((uint16_t)e.a != 0x8000) ao->x = e.a;
            if ((uint16_t)e.b != 0x8000) ao->y = e.b;
            ao->idx++;
            break;
        case -7:  /* LABEL */
            ao->label = e.a;
            ao->labelpos = ao->idx + 1;
            ao->idx++;
            break;
        case -8: ao->x += e.a; ao->idx++; break;
        case -9: ao->y += e.a; ao->idx++; break;
        case -10: { /* SELECT-SEQ */
            int ns = ao->as->nseq, target = ao->seqno;
            switch (e.a) {
            case 1: target = (int)(((int64_t)rnga_rand() * ns) / 32768); break;
            case 2: target = (ao->seqno + 1) % (ns ? ns : 1); break;
            case 3: target = e.b; break;
            case 4: target = e.b + (int)(((int64_t)rnga_rand() * e.d) / 32768); break;
            }
            Seq *s = aseq_get_seq(ao->as, target);
            if (s) { ao->seqno = target; ao->seq = s; }
            ao->idx = 0;
            break;
        }
        case -11: ao->idx = e.a; break;
        case -12:  /* CALLBACK */
            ao->idx++;
            if (ao->callback) ao->callback(ao);
            return 0;
        case -13:  /* LINK-SHOW */
            if (link_valid(ao)) {
                AO *l = ao->link;
                if (l->state == AO_STOPPED) ao_start(l, l->seqno);
                l->hidden = 0;
            }
            ao->idx++;
            break;
        case -14:  /* LINK-STOP */
            if (link_valid(ao)) ao_stop(ao->link);
            ao->idx++;
            break;
        case -15: ao->idx++; break;
        case -16:  /* LINK-HIDE */
            if (link_valid(ao)) ao_hide(ao->link);
            ao->idx++;
            break;
        case -17:  /* LINK-POS */
            if (link_valid(ao)) ao_set_pos(ao->link, ao->x + e.a, ao->y + e.b);
            ao->idx++;
            break;
        case -18:  /* TEXT */
            if (ao->text_hook) ao->text_hook(ao, e.a, 0);
            ao->idx++;
            break;
        case -19: ao->rcount = e.a; ao->idx++; break;
        case -20:  /* REPEAT */
            ao->rcount--;
            if (ao->rcount != 0 && ao->labelpos >= 0) ao->idx = ao->labelpos;
            else ao->idx++;
            break;
        case -21: { /* DELAY */
            int span = e.b - e.a;
            ao->next_time += e.a + (int)(((int64_t)rnga_rand() * span) / 32768);
            ao->idx++;
            return 0;
        }
        case -22:  /* STAMP */
            ao->stamp_next = 1;
            if (e.a && ao->text_hook) ao->text_hook(ao, e.a, 1);
            ao->idx++;
            break;
        default:
            ao->idx++;  /* unknown opcode: skip */
            break;
        }
    }
    ao->frame_idx = ao->idx;
    ao->frame_count = count;
    ao_build_frame(ao, count);
    ao->idx += count;
    return 1;
}

void eng_run_aos(void) {
    if (g_eng.pre_anim) g_eng.pre_anim();
    /* AOs may start/stop others while running: iterate over a snapshot */
    AO *snap[MAX_AO];
    int n = n_ao;
    memcpy(snap, ao_list, sizeof(AO *) * (size_t)n);
    for (int i = 0; i < n; i++) {
        AO *ao = snap[i];
        int alive = 0;
        for (int k = 0; k < n_ao; k++) if (ao_list[k] == ao) { alive = 1; break; }
        if (alive) ao_update(ao);
    }
    if (g_eng.post_anim) g_eng.post_anim();
}

void eng_render(void) {
    for (int i = 0; i < n_se; i++) {
        SE *se = se_list[i];
        if (!(se->flags & (SE_DRAW | SE_REDRAW | SE_OVERLAP))) continue;
        const Rect *cl = (se->flags & SE_VIEWPORT) ? &g_eng.viewport : NULL;
        canvas_blit_cel(g_eng.work, se->cel, se->x, se->y, (se->flags & SE_MIRROR) != 0, cl);
        if (se->flags & SE_STAMP) canvas_blit_cel(g_eng.back, se->cel, se->x, se->y, (se->flags & SE_MIRROR) != 0, cl);
        if (se->owner && se->owner->post_draw) se->owner->post_draw(se->owner);
        if (!(se->flags & SE_NOPROP))
            for (int j = i + 1; j < n_se; j++)
                if (rect_hit(se_list[j]->r, se->r)) se_list[j]->flags |= SE_OVERLAP;
        se->flags &= ~(SE_DRAW | SE_REDRAW | SE_OVERLAP);
    }
}
