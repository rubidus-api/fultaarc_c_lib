/* testlib.h - FultaArc's minimal test helpers. MIT. */
#ifndef FULTA_TESTLIB_H
#define FULTA_TESTLIB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int t_fail, t_pass, t_skip;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (cond) t_pass++;                                                 \
        else {                                                              \
            t_fail++;                                                       \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
        }                                                                   \
    } while (0)

#define SKIP(...)                                                           \
    do {                                                                    \
        t_skip++;                                                           \
        fprintf(stderr, "skip: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fputc('\n', stderr);                                                \
    } while (0)

static inline int t_report(const char *name) {
    printf("%s: %d passed, %d failed, %d skipped\n", name, t_pass, t_fail, t_skip);
    return t_fail ? 1 : 0;
}

/* The private fixtures root: $FULTA_ARC_FIXTURES, else ops/fixtures. */
static inline const char *t_fixtures(void) {
    const char *e = getenv("FULTA_ARC_FIXTURES");
    return e && *e ? e : "ops/fixtures";
}

static inline unsigned char *t_read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(sz > 0 ? (size_t)sz : 1);
    if (b && sz > 0 && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f);
    if (b) *n = (size_t)(sz > 0 ? sz : 0);
    return b;
}

#endif
