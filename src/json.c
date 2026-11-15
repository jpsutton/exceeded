/* json.c - tiny JSON reader */
#include "json.h"
#include "util.h"
#include <ctype.h>
#include <math.h>

typedef struct P { const char *p, *e; int depth; } P;

static void ws(P *s) { while (s->p < s->e && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')) s->p++; }
static J *node(int t) { J *j = xcalloc(1, sizeof *j); j->t = t; return j; }
static J *parse_value(P *s);

static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int c = (unsigned char)p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

static char *parse_string(P *s) {
    if (s->p >= s->e || *s->p != '"') return NULL;
    s->p++;
    size_t cap = 32, n = 0;
    char *b = xmalloc(cap);
    while (s->p < s->e && *s->p != '"') {
        unsigned c = (unsigned char)*s->p++;
        if (c < 0x20) { free(b); return NULL; }
        if (c == '\\') {
            if (s->p >= s->e) { free(b); return NULL; }
            c = (unsigned char)*s->p++;
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = 8; break; case 'f': c = 12; break; case 'n': c = 10; break; case 'r': c = 13; break; case 't': c = 9; break;
            case 'u': {
                unsigned u;
                if (s->e - s->p < 4 || hex4(s->p, &u) != 0) { free(b); return NULL; }
                s->p += 4;
                if (u >= 0xD800 && u < 0xDC00 && s->e - s->p >= 6 && s->p[0] == '\\' && s->p[1] == 'u') {
                    unsigned lo;
                    if (hex4(s->p + 2, &lo) == 0 && lo >= 0xDC00 && lo < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); s->p += 6; }
                }
                if (n + 5 >= cap) { cap *= 2; b = xrealloc(b, cap); }
                if (u < 0x80) b[n++] = (char)u;
                else if (u < 0x800) { b[n++] = (char)(0xC0 | (u >> 6)); b[n++] = (char)(0x80 | (u & 63)); }
                else if (u < 0x10000) { b[n++] = (char)(0xE0 | (u >> 12)); b[n++] = (char)(0x80 | ((u >> 6) & 63)); b[n++] = (char)(0x80 | (u & 63)); }
                else { b[n++] = (char)(0xF0 | (u >> 18)); b[n++] = (char)(0x80 | ((u >> 12) & 63)); b[n++] = (char)(0x80 | ((u >> 6) & 63)); b[n++] = (char)(0x80 | (u & 63)); }
                continue;
            }
            default: free(b); return NULL;
            }
        }
        if (n + 2 >= cap) { cap *= 2; b = xrealloc(b, cap); }
        b[n++] = (char)c;
    }
    if (s->p >= s->e) { free(b); return NULL; }
    s->p++;
    b[n] = 0;
    return b;
}

static J *parse_value(P *s) {
    ws(s);
    if (s->p >= s->e || s->depth > 64) return NULL;
    char c = *s->p;
    if (c == '{') {
        s->p++;
        s->depth++;
        J *o = node(J_OBJ);
        ws(s);
        if (s->p < s->e && *s->p == '}') { s->p++; s->depth--; return o; }
        for (;;) {
            ws(s);
            char *k = parse_string(s);
            if (!k) { json_free(o); return NULL; }
            ws(s);
            if (s->p >= s->e || *s->p != ':') { free(k); json_free(o); return NULL; }
            s->p++;
            J *v = parse_value(s);
            if (!v) { free(k); json_free(o); return NULL; }
            o->kid = xrealloc(o->kid, sizeof(J *) * (size_t)(o->n + 1));
            o->key = xrealloc(o->key, sizeof(char *) * (size_t)(o->n + 1));
            o->kid[o->n] = v;
            o->key[o->n++] = k;
            ws(s);
            if (s->p < s->e && *s->p == ',') { s->p++; continue; }
            if (s->p < s->e && *s->p == '}') { s->p++; break; }
            json_free(o);
            return NULL;
        }
        s->depth--;
        return o;
    }
    if (c == '[') {
        s->p++;
        s->depth++;
        J *a = node(J_ARR);
        ws(s);
        if (s->p < s->e && *s->p == ']') { s->p++; s->depth--; return a; }
        for (;;) {
            J *v = parse_value(s);
            if (!v) { json_free(a); return NULL; }
            a->kid = xrealloc(a->kid, sizeof(J *) * (size_t)(a->n + 1));
            a->kid[a->n++] = v;
            ws(s);
            if (s->p < s->e && *s->p == ',') { s->p++; continue; }
            if (s->p < s->e && *s->p == ']') { s->p++; break; }
            json_free(a);
            return NULL;
        }
        s->depth--;
        return a;
    }
    if (c == '"') {
        char *str = parse_string(s);
        if (!str) return NULL;
        J *j = node(J_STR);
        j->s = str;
        return j;
    }
    if (s->e - s->p >= 4 && !strncmp(s->p, "true", 4)) { s->p += 4; J *j = node(J_BOOL); j->i = 1; return j; }
    if (s->e - s->p >= 5 && !strncmp(s->p, "false", 5)) { s->p += 5; return node(J_BOOL); }
    if (s->e - s->p >= 4 && !strncmp(s->p, "null", 4)) { s->p += 4; return node(J_NULL); }
    if (c == '-' || isdigit((unsigned char)c)) {
        const char *q = s->p;
        if (*q == '-') q++;
        if (q >= s->e || !isdigit((unsigned char)*q)) return NULL;
        if (*q == '0' && q + 1 < s->e && isdigit((unsigned char)q[1])) return NULL;
        int isint = 1;
        while (q < s->e && isdigit((unsigned char)*q)) q++;
        if (q < s->e && *q == '.') { isint = 0; q++; if (q >= s->e || !isdigit((unsigned char)*q)) return NULL; while (q < s->e && isdigit((unsigned char)*q)) q++; }
        if (q < s->e && (*q == 'e' || *q == 'E')) {
            isint = 0;
            q++;
            if (q < s->e && (*q == '+' || *q == '-')) q++;
            if (q >= s->e || !isdigit((unsigned char)*q)) return NULL;
            while (q < s->e && isdigit((unsigned char)*q)) q++;
        }
        char tmp[64];
        size_t l = (size_t)(q - s->p);
        if (l >= sizeof tmp) return NULL;
        memcpy(tmp, s->p, l);
        tmp[l] = 0;
        s->p = q;
        J *j = node(isint ? J_INT : J_REAL);
        j->d = strtod(tmp, NULL);
        j->i = isint ? strtoll(tmp, NULL, 10) : (int64_t)j->d;
        return j;
    }
    return NULL;
}

J *json_parse(const char *txt, size_t n) {
    P s = {txt, txt + n, 0};
    J *j = parse_value(&s);
    if (!j) return NULL;
    ws(&s);
    if (s.p != s.e) { json_free(j); return NULL; }
    return j;
}

void json_free(J *j) {
    if (!j) return;
    for (int i = 0; i < j->n; i++) { json_free(j->kid[i]); if (j->key) free(j->key[i]); }
    free(j->kid);
    free(j->key);
    free(j->s);
    free(j);
}

const J *json_get(const J *o, const char *key) {
    if (!o || o->t != J_OBJ) return NULL;
    for (int i = 0; i < o->n; i++) if (!strcmp(o->key[i], key)) return o->kid[i];
    return NULL;
}
