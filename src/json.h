/* json.h - tiny JSON reader (for install.json) */
#ifndef JSON_H
#define JSON_H
#include <stddef.h>
#include <stdint.h>
enum { J_NULL, J_BOOL, J_INT, J_REAL, J_STR, J_ARR, J_OBJ };
typedef struct J {
    int t;
    int64_t i;
    double d;
    char *s;
    struct J **kid;
    char **key;
    int n;
} J;
J *json_parse(const char *txt, size_t n);          /* NULL on any syntax error */
void json_free(J *j);
const J *json_get(const J *o, const char *key);    /* object member or NULL */
#endif
