/* test_ar.c - ar reader specifics not covered by the GNU-ar manifest (ar.txt): the BSD "#1/<len>" inline-name
 * scheme (GNU ar does not produce it) and format detection. See D-020, T019. */
#include "fulta/arc.h"
#include "testlib.h"

#include <string.h>

typedef struct { char buf[256]; size_t n; } cap_t;
static fulta_arc_err_t cap_write(void *ctx, const void *d, size_t n) {
    cap_t *c = ctx;
    if (c->n + n < sizeof c->buf) { memcpy(c->buf + c->n, d, n); c->n += n; }
    return FULTA_ARC_OK;
}

/* Build a BSD-style ar with one "#1/<len>" member in a memory buffer. */
static void test_bsd_name(void) {
    const char *name = "bsdname.txt";          /* 11 bytes */
    const char *body = "hello bsd world\n";    /* 16 bytes */
    size_t nl = strlen(name), bl = strlen(body);

    unsigned char ar[256];
    size_t p = 0;
    memcpy(ar, "!<arch>\n", 8); p = 8;
    char hdr[60];
    memset(hdr, ' ', 60);
    int k = snprintf(hdr, 17, "#1/%zu", nl);   /* name field: "#1/11" */
    hdr[k] = ' ';                              /* undo snprintf's NUL, keep space padding */
    memset(hdr + 16, ' ', 60 - 16);
    char sz[16]; snprintf(sz, sizeof sz, "%zu", nl + bl);
    memcpy(hdr + 48, sz, strlen(sz));          /* size = namelen + body */
    hdr[58] = 0x60; hdr[59] = 0x0A;
    memcpy(ar + p, hdr, 60); p += 60;
    memcpy(ar + p, name, nl); p += nl;         /* inline name, then the content */
    memcpy(ar + p, body, bl); p += bl;
    if ((nl + bl) & 1) ar[p++] = '\n';         /* even padding */

    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(ar, p, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "bsd ar opens (%s)", fulta_arc_strerror(e));
    if (e) return;
    CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_AR, "detected as ar");
    CHECK(fulta_arc_count(arc) == 1, "one member");
    const fulta_arc_entry_t *en = fulta_arc_entry(arc, 0);
    CHECK(en && strcmp(en->name, "bsdname.txt") == 0, "BSD inline name resolved (got '%s')", en ? en->name : "");
    CHECK(en && en->size == bl, "size excludes the inline name (%llu want %zu)",
          en ? (unsigned long long)en->size : 0, bl);
    cap_t c = {0};
    fulta_arc_sink_t sink = {.ctx = &c, .write = cap_write};
    e = fulta_arc_extract(arc, 0, &sink);
    CHECK(e == FULTA_ARC_OK && c.n == bl && memcmp(c.buf, body, bl) == 0, "BSD member content extracts");
    fulta_arc_close(arc);
}

/* A GNU-ar fixture opens as ar with the long-name member resolved. */
static void test_gnu_open(void) {
    char path[4096];
    snprintf(path, sizeof path, "%s/ar/gnu.a", t_fixtures());
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("gnu.a fixture not present"); return; }
    fclose(f);
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_file(path, NULL, &arc) != FULTA_ARC_OK) { CHECK(0, "gnu.a open"); return; }
    CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_AR, "gnu.a is ar");
    int found = 0;
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (strcmp(fulta_arc_entry(arc, i)->name, "a_rather_long_member_name_over_15.o") == 0) found = 1;
    CHECK(found, "GNU // long name resolved");
    fulta_arc_close(arc);
}

/* A bare magic with trailing garbage must not crash and lists nothing extractable. */
static void test_truncated(void) {
    unsigned char ar[32];
    memcpy(ar, "!<arch>\n", 8);
    memset(ar + 8, 'x', sizeof ar - 8);   /* no valid 60-byte header */
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(ar, sizeof ar, NULL, &arc);
    /* either opens with zero members (the garbage is not a header) or is rejected; never crashes */
    if (e == FULTA_ARC_OK) { CHECK(fulta_arc_count(arc) == 0, "garbage after magic -> no members"); fulta_arc_close(arc); }
    else CHECK(1, "garbage after magic rejected (%s)", fulta_arc_strerror(e));
}

int main(void) {
    test_bsd_name();
    test_gnu_open();
    test_truncated();
    return t_report("test_ar");
}
