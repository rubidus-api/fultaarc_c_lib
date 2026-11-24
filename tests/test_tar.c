/* test_tar.c - TAR reader specifics not covered by the manifest round-trip (test_manifests/tar.txt): symlink
 * handling (the target is surfaced as the entry's data) and detection rejection of a non-tar block. See D-020. */
#include "fulta/arc.h"
#include "testlib.h"

#include <string.h>

typedef struct { char buf[256]; size_t n; } cap_t;
static fulta_arc_err_t cap_write(void *ctx, const void *d, size_t n) {
    cap_t *c = ctx;
    if (c->n + n < sizeof c->buf) { memcpy(c->buf + c->n, d, n); c->n += n; }
    return FULTA_ARC_OK;
}

static void test_symlink(void) {
    char path[4096];
    snprintf(path, sizeof path, "%s/tar/sym.tar", t_fixtures());
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("sym.tar fixture not present"); return; }
    fclose(f);

    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_file(path, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "sym.tar open: %s", fulta_arc_strerror(e));
    if (e) return;
    CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_TAR, "sym.tar detected as TAR");

    size_t link = SIZE_MAX, real = SIZE_MAX;
    for (size_t i = 0; i < fulta_arc_count(arc); i++) {
        const char *nm = fulta_arc_entry(arc, i)->name;
        if (strcmp(nm, "link.txt") == 0) link = i;
        if (strcmp(nm, "real.txt") == 0) real = i;
    }
    CHECK(real != SIZE_MAX && link != SIZE_MAX, "both entries listed");
    if (link != SIZE_MAX) {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, link);
        CHECK(en->flags & FULTA_ARC_ENTRY_SYMLINK, "link.txt flagged SYMLINK");
        cap_t c = {0};
        fulta_arc_sink_t sink = {.ctx = &c, .write = cap_write};
        e = fulta_arc_extract(arc, link, &sink);
        CHECK(e == FULTA_ARC_OK, "symlink extract: %s", fulta_arc_strerror(e));
        CHECK(c.n == strlen("real.txt") && memcmp(c.buf, "real.txt", c.n) == 0,
              "symlink target is the data (got '%.*s')", (int)c.n, c.buf);
    }
    if (real != SIZE_MAX) {
        cap_t c = {0};
        fulta_arc_sink_t sink = {.ctx = &c, .write = cap_write};
        e = fulta_arc_extract(arc, real, &sink);
        CHECK(e == FULTA_ARC_OK && c.n == strlen("the real file\n") && memcmp(c.buf, "the real file\n", c.n) == 0,
              "real.txt content round-trips");
    }
    fulta_arc_close(arc);
}

/* A ustar header with a full 155-byte prefix and 100-byte name: the assembled path is 256 chars. Regression for a
 * one-byte overflow of the name buffer (caught only by ASAN, not by GNU tar fixtures). */
static void test_max_name(void) {
    uint8_t h[512] = {0};
    memset(h, 'n', 100);                 /* name[100] */
    memset(h + 345, 'p', 155);           /* prefix[155] */
    memcpy(h + 108, "0000644", 7);       /* mode (optional) */
    memcpy(h + 124, "00000000000", 11);  /* size 0, octal */
    memcpy(h + 136, "00000000000", 11);  /* mtime 0 */
    h[156] = '0';                        /* regular file */
    memcpy(h + 257, "ustar", 5);         /* magic (POSIX) */
    memcpy(h + 263, "00", 2);            /* version */
    memset(h + 148, ' ', 8);             /* checksum field = spaces while summing */
    uint32_t sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    char cks[8];
    snprintf(cks, sizeof cks, "%06o", sum);
    memcpy(h + 148, cks, 6);
    h[154] = 0;
    h[155] = ' ';

    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(h, sizeof h, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "max-name ustar header opens (%s)", fulta_arc_strerror(e));
    if (e) return;
    CHECK(fulta_arc_count(arc) == 1, "one entry");
    const fulta_arc_entry_t *en = fulta_arc_entry(arc, 0);
    CHECK(en && strlen(en->name) == 256, "assembled prefix/name is 256 chars (got %zu)", en ? strlen(en->name) : 0);
    fulta_arc_close(arc);
}

/* A 512-byte block that is not a valid tar header (bad checksum) must not be opened as TAR. */
static void test_reject_nontar(void) {
    uint8_t blk[512];
    for (size_t i = 0; i < sizeof blk; i++) blk[i] = (uint8_t)(i * 7 + 1);   /* no valid ustar checksum */
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(blk, sizeof blk, NULL, &arc);
    CHECK(e != FULTA_ARC_OK, "a random 512-byte block is not opened as an archive (%s)", fulta_arc_strerror(e));
    if (e == FULTA_ARC_OK) fulta_arc_close(arc);
}

/* Count non-directory entries. */
static size_t count_files(fulta_arc_t *arc) {
    size_t f = 0;
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (!(fulta_arc_entry(arc, i)->flags & FULTA_ARC_ENTRY_DIR)) f++;
    return f;
}

/* A gzip/bzip2/xz stream wrapping a tar is transparently unwrapped: the archive opens as TAR and lists the tar's
 * members (reprobe layer). A gzip of a non-tar stays a single GZIP entry. */
static void test_reprobe(void) {
    const char *wrapped[] = {"gnu.tar.gz", "gnu.tar.bz2", "gnu.tar.xz", "gnu.tar.Z", "gnu.tar.lz"};
    for (size_t w = 0; w < sizeof wrapped / sizeof *wrapped; w++) {
        char path[4096];
        snprintf(path, sizeof path, "%s/tar/%s", t_fixtures(), wrapped[w]);
        FILE *f = fopen(path, "rb");
        if (!f) { SKIP("%s fixture not present", wrapped[w]); continue; }
        fclose(f);
        fulta_arc_t *arc = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, NULL, &arc);
        CHECK(e == FULTA_ARC_OK, "%s open: %s", wrapped[w], fulta_arc_strerror(e));
        if (e) continue;
        CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_TAR, "%s unwrapped to TAR (got %s)", wrapped[w],
              fulta_arc_format_name(fulta_arc_format(arc)));
        CHECK(count_files(arc) == 6, "%s: lists the tar's 6 files (got %zu)", wrapped[w], count_files(arc));
        size_t idx = SIZE_MAX;
        for (size_t i = 0; i < fulta_arc_count(arc); i++)
            if (strcmp(fulta_arc_entry(arc, i)->name, "hello.txt") == 0) idx = i;
        CHECK(idx != SIZE_MAX, "%s: hello.txt listed", wrapped[w]);
        if (idx != SIZE_MAX) {
            cap_t c = {0};
            fulta_arc_sink_t sink = {.ctx = &c, .write = cap_write};
            e = fulta_arc_extract(arc, idx, &sink);
            CHECK(e == FULTA_ARC_OK && c.n == 12 && memcmp(c.buf, "Hello, tar!\n", 12) == 0,
                  "%s: hello.txt extracts through the wrapper", wrapped[w]);
        }
        fulta_arc_close(arc);
    }
    /* memory-backed outer source: the caller's referencing memory source and the reprobe's owning memory source
     * coexist; the inner tar must read the owned buffer, not the caller's. */
    {
        char path[4096];
        snprintf(path, sizeof path, "%s/tar/gnu.tar.gz", t_fixtures());
        FILE *mf = fopen(path, "rb");
        if (mf) {
            fseek(mf, 0, SEEK_END);
            long sz = ftell(mf);
            fseek(mf, 0, SEEK_SET);
            uint8_t *mem = malloc(sz > 0 ? (size_t)sz : 1);
            size_t rd = mem && sz > 0 ? fread(mem, 1, (size_t)sz, mf) : 0;
            fclose(mf);
            if (mem && rd == (size_t)sz) {
                fulta_arc_t *arc = NULL;
                fulta_arc_err_t e = fulta_arc_open_memory(mem, rd, NULL, &arc);
                CHECK(e == FULTA_ARC_OK && arc && fulta_arc_format(arc) == FULTA_ARC_FORMAT_TAR,
                      "gnu.tar.gz from memory unwraps to TAR");
                if (!e && arc) {
                    CHECK(count_files(arc) == 6, "memory reprobe: 6 files");
                    fulta_arc_close(arc);
                }
            }
            free(mem);   /* the caller's buffer outlives the arc (closed above) */
        }
    }

    /* a gzip of a non-tar payload is not promoted: stays a single GZIP entry */
    char path[4096];
    snprintf(path, sizeof path, "%s/tar/plain.bin.gz", t_fixtures());
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        fulta_arc_t *arc = NULL;
        if (fulta_arc_open_file(path, NULL, &arc) == FULTA_ARC_OK) {
            CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_GZIP, "plain.bin.gz stays GZIP (not promoted)");
            CHECK(fulta_arc_count(arc) == 1, "plain.bin.gz is one entry");
            fulta_arc_close(arc);
        }
    }
}

int main(void) {
    test_symlink();
    test_max_name();
    test_reject_nontar();
    test_reprobe();
    return t_report("test_tar");
}
