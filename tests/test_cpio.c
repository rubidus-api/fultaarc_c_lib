/* test_cpio.c - cpio reader specifics not covered by the 7z-validated manifest (cpio.txt): the symlink path
 * (S_IFLNK target surfaced as the entry data) and format rejection. See D-020, T020. */
#include "fulta/arc.h"
#include "testlib.h"

#include <string.h>
#include <stdio.h>

typedef struct { char buf[256]; size_t n; } cap_t;
static fulta_arc_err_t cap_write(void *ctx, const void *d, size_t n) {
    cap_t *c = ctx;
    if (c->n + n < sizeof c->buf) { memcpy(c->buf + c->n, d, n); c->n += n; }
    return FULTA_ARC_OK;
}

/* Append a newc (070701) member; returns the new length. 110-byte header of 8-hex fields, then the NUL-terminated
 * name padded to 4, then the data padded to 4. */
static size_t newc(unsigned char *p, size_t off, unsigned ino, unsigned mode,
                   const char *name, const unsigned char *data, unsigned dlen) {
    size_t nl = strlen(name) + 1;      /* includes the NUL */
    char h[111];
    snprintf(h, sizeof h, "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
             ino, mode, 0u, 0u, 1u, 0u, dlen, 0u, 0u, 0u, 0u, (unsigned)nl, 0u);
    memcpy(p + off, h, 110); off += 110;
    memcpy(p + off, name, nl); off += nl;
    while ((off) & 3) p[off++] = 0;    /* pad header+name to 4 (header is 110, so align on total) */
    memcpy(p + off, data, dlen); off += dlen;
    while (off & 3) p[off++] = 0;
    return off;
}

static void test_symlink(void) {
    unsigned char ar[1024];
    size_t off = 0;
    off = newc(ar, off, 1, 0100644, "real.txt", (const unsigned char *)"the target body\n", 16);
    off = newc(ar, off, 2, 0120777, "link.txt", (const unsigned char *)"real.txt", 8);   /* S_IFLNK, data=target */
    off = newc(ar, off, 3, 0, "TRAILER!!!", (const unsigned char *)"", 0);

    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(ar, off, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "cpio(newc) opens (%s)", fulta_arc_strerror(e));
    if (e) return;
    CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_CPIO, "detected as cpio");
    CHECK(fulta_arc_count(arc) == 2, "two members (TRAILER not listed)");
    size_t li = SIZE_MAX;
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (strcmp(fulta_arc_entry(arc, i)->name, "link.txt") == 0) li = i;
    CHECK(li != SIZE_MAX, "link.txt listed");
    if (li != SIZE_MAX) {
        CHECK(fulta_arc_entry(arc, li)->flags & FULTA_ARC_ENTRY_SYMLINK, "link.txt flagged SYMLINK");
        cap_t c = {0};
        fulta_arc_sink_t sink = {.ctx = &c, .write = cap_write};
        e = fulta_arc_extract(arc, li, &sink);
        CHECK(e == FULTA_ARC_OK && c.n == 8 && memcmp(c.buf, "real.txt", 8) == 0, "symlink target is the data");
    }
    fulta_arc_close(arc);
}

static void test_reject(void) {
    unsigned char junk[64];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = (unsigned char)(i * 3 + 1);
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(junk, sizeof junk, NULL, &arc);
    CHECK(e != FULTA_ARC_OK, "non-cpio junk not opened as cpio (%s)", fulta_arc_strerror(e));
    if (e == FULTA_ARC_OK) fulta_arc_close(arc);
}

int main(void) {
    test_symlink();
    test_reject();
    return t_report("test_cpio");
}
