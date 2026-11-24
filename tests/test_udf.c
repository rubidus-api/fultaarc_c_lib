/* test_udf.c - UDF reader cases beyond the 7z-validated manifest (udf.txt): the format is reported as UDF, a
 * 16-bit OSTA CS0 name decodes to UTF-8, and a descriptor whose CRC-ITU is corrupted is rejected (the reader
 * validates every tag it follows). See D-020, T023. */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SS 2048

static unsigned char *load(const char *rel, size_t *len) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", t_fixtures(), rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)n;
    return b;
}

static int find_entry(fulta_arc_t *arc, const char *name) {
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (strcmp(fulta_arc_entry(arc, i)->name, name) == 0) return (int)i;
    return -1;
}

/* CRC-16/XMODEM = the ECMA-167 descriptor CRC-ITU (verify: "123456789" -> 0x31c3). */
static unsigned short crc16x(const unsigned char *p, size_t n) {
    unsigned short c = 0;
    for (size_t i = 0; i < n; i++) { c ^= (unsigned short)p[i] << 8; for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (unsigned short)((c << 1) ^ 0x1021) : (unsigned short)(c << 1); }
    return c;
}
/* Re-sign the descriptor whose tag starts at byte offset `off`: recompute its CRC over the body and its checksum,
 * so a *crafted* (hostile but well-formed) descriptor passes the reader's validation and actually reaches the
 * parser — otherwise every mutation is rejected at the tag and the parser stays unfuzzed. */
static void resign(unsigned char *b, size_t off) {
    unsigned crc_len = b[off + 10] | (b[off + 11] << 8);
    unsigned short crc = crc16x(b + off + 16, crc_len);
    b[off + 8] = crc & 0xff; b[off + 9] = crc >> 8;
    unsigned sum = 0;
    for (int i = 0; i < 16; i++) if (i != 4) sum += b[off + i];
    b[off + 4] = sum & 0xff;
}

/* basic.udf maker layout (udfmake.py): partition starts at sector 257; partition block 1 = root File Entry, and
 * the root files are hello.txt (fe block 3) then data.bin (fe block 5). */
#define UDF_ROOT_FE_SECTOR  (257 + 1)
#define UDF_DATABIN_FE_SECTOR (257 + 5)

static void test_format_and_tree(void) {
    size_t n = 0;
    unsigned char *u = load("udf/basic.udf", &n);
    if (!u) { SKIP("basic.udf not present"); return; }
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(u, n, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "basic.udf opens (%s)", fulta_arc_strerror(e));
    if (!e) {
        CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_UDF, "detected as UDF (not ISO) — UDF precedence");
        CHECK(find_entry(arc, "hello.txt") >= 0, "a root file is listed");
        CHECK(find_entry(arc, "subdir/inner.txt") >= 0, "a subdirectory file is listed by full path");
        fulta_arc_close(arc);
    }
    free(u);
}

static void test_unicode_name(void) {
    size_t n = 0;
    unsigned char *u = load("udf/uni.udf", &n);
    if (!u) { SKIP("uni.udf not present"); return; }
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_memory(u, n, NULL, &arc) == FULTA_ARC_OK) {
        CHECK(find_entry(arc, "héllo.txt") >= 0, "a 16-bit CS0 name decodes to UTF-8");
        fulta_arc_close(arc);
    } else {
        CHECK(0, "uni.udf opens");
    }
    free(u);
}

/* The Anchor Volume Descriptor Pointer is at sector 256; corrupting a byte of its body breaks the descriptor CRC,
 * which the reader validates, so the volume is rejected rather than followed into garbage. */
static void test_crc_rejection(void) {
    size_t n = 0;
    unsigned char *u = load("udf/basic.udf", &n);
    if (!u) { SKIP("basic.udf not present"); return; }
    if (n > 256 * SS + 40) u[256 * SS + 20] ^= 0xFF;   /* flip a byte inside the AVDP (the main-VDS location) */
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(u, n, NULL, &arc);
    CHECK(e != FULTA_ARC_OK, "a corrupted anchor descriptor (bad CRC) is rejected (%s)", fulta_arc_strerror(e));
    if (e == FULTA_ARC_OK) fulta_arc_close(arc);
    free(u);
}

/* Robustness: UDF images are always ≥ ~513 KB (the anchor is fixed at sector 256), past test_robust's 256 KB
 * input cap, so the fuzzing lives here — bounded single-byte flips and truncations of basic.udf, each opened and
 * fully extracted under ASAN/UBSAN. The parser must never crash, loop, or over-read on a mutated image. */
static fulta_arc_err_t fuzz_sink(void *ctx, const void *d, size_t n) { (void)d; *(size_t *)ctx += n; return FULTA_ARC_OK; }

static unsigned rng = 0x9e3779b9u;
static unsigned nextr(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void test_fuzz(void) {
    size_t n = 0;
    unsigned char *base = load("udf/basic.udf", &n);
    if (!base) { SKIP("basic.udf not present"); return; }
    unsigned char *m = malloc(n);
    int crashed = 0;
    for (int it = 0; it < 400 && !crashed; it++) {
        memcpy(m, base, n);
        size_t len = n;
        if (it % 3 == 0) { len = 1 + nextr() % n; }                      /* truncate */
        else { for (int j = 0, c = 1 + (int)(nextr() % 8); j < c; j++) m[nextr() % n] ^= (unsigned char)(1u << (nextr() % 8)); }
        fulta_arc_t *arc = NULL;
        if (fulta_arc_open_memory(m, len, NULL, &arc) == FULTA_ARC_OK) {
            for (size_t i = 0; i < fulta_arc_count(arc) && i < 256; i++) {
                size_t got = 0; fulta_arc_sink_t s = {&got, fuzz_sink};
                (void)fulta_arc_extract(arc, i, &s);
            }
            fulta_arc_close(arc);
        }
    }
    CHECK(!crashed, "400 mutated UDF images open+extract without crashing (ASAN/UBSAN)");
    free(m); free(base);
}

/* A crafted (validly-signed) File Entry whose allocation-descriptor length reaches the end of the block must not
 * over-read the File Entry's own buffer. len_ad is at File Entry body offset 156 (tag 261); ad_off = 160, so
 * len_ad = 2048-160 passed the old `ad_off+len_ad<=2048` guard while the reader then read 16 bytes (the tag) past
 * the block. The fixed guard (16+ad_off+len_ad<=2048) rejects it; this asserts no over-read and a clean failure. */
static void test_ad_bound(void) {
    size_t n = 0;
    unsigned char *u = load("udf/basic.udf", &n);
    if (!u) { SKIP("basic.udf not present"); return; }
    size_t fe = (size_t)UDF_ROOT_FE_SECTOR * SS;
    if (fe + 16 + 160 + 1 >= n) { free(u); SKIP("layout"); return; }
    unsigned len_ad = SS - 160;                        /* 16 + 160 + (2048-160) = 2064 > 2048 */
    u[fe + 16 + 156] = len_ad & 0xff; u[fe + 16 + 157] = (len_ad >> 8) & 0xff;
    u[fe + 16 + 158] = 0; u[fe + 16 + 159] = 0;
    resign(u, fe);
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(u, n, NULL, &arc);        /* must not over-read (ASAN) */
    CHECK(e != FULTA_ARC_OK, "an over-long allocation-descriptor length is rejected (%s)", fulta_arc_strerror(e));
    if (e == FULTA_ARC_OK) fulta_arc_close(arc);
    free(u);
}

/* A file whose declared information length exceeds what its extents provide must fail extraction, not silently
 * return a short buffer with OK. data.bin's File Entry info_length is at body offset 40 (tag 261); we enlarge it. */
static void test_short_extent(void) {
    size_t n = 0;
    unsigned char *u = load("udf/basic.udf", &n);
    if (!u) { SKIP("basic.udf not present"); return; }
    size_t fe = (size_t)UDF_DATABIN_FE_SECTOR * SS;
    if (fe + 16 + 48 >= n) { free(u); SKIP("layout"); return; }
    for (int i = 0; i < 8; i++) u[fe + 16 + 40 + i] = (i == 1) ? 0x80 : 0;   /* info_length := 0x8000 = 32768 */
    resign(u, fe);
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_memory(u, n, NULL, &arc) != FULTA_ARC_OK) { CHECK(0, "patched image still opens"); free(u); return; }
    int i = find_entry(arc, "data.bin");
    CHECK(i >= 0 && fulta_arc_entry(arc, (size_t)i)->size == 32768, "declared size is the enlarged value");
    if (i >= 0) {
        size_t got = 0; fulta_arc_sink_t s = {&got, fuzz_sink};
        fulta_arc_err_t xe = fulta_arc_extract(arc, (size_t)i, &s);
        CHECK(xe != FULTA_ARC_OK, "a file shorter than its declared size fails extraction, not a silent short read (%s)",
              fulta_arc_strerror(xe));
    }
    fulta_arc_close(arc);
    free(u);
}

int main(void) {
    test_format_and_tree();
    test_unicode_name();
    test_crc_rejection();
    test_ad_bound();
    test_short_extent();
    test_fuzz();
    return t_report("test_udf");
}
