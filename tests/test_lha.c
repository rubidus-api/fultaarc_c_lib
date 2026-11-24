/* test_lha.c - LHA/LZH reader cases beyond the manifest (lha.txt).
 *
 * Synthetic archives, built here as level-0 "-lh0-" entries from the clean room's container spec (§3: S = 22 + n,
 * byte sum of those S bytes, CRC-16 after the name; CRC-16 is reflected 0xA001, initial 0, §5.1): name decoding
 * (UTF-8, CP932 fallback, the 0x5C second-byte repair, explicit code pages), a data CRC mismatch (CHECKSUM, DAMAGED
 * with keep_damaged), truncation, a bad header sum, a damaged second entry, detection of non-LHA input that carries an
 * LHA-like pattern, an unknown method string, and the self-extractor gate (a stub kind it knows is searched behind,
 * an unknown prefix is not).
 *
 * Corpus archives the manifest excludes (fixtures/lha, the lhasa 0.6.0 test corpus; skipped when absent), each with
 * the check its exclusion reason calls for. Expected sizes and CRC-32s quoted here are the pinned lhasa binary's
 * (lhamanifest.py run, 2026-10-16), never FultaArc's, except for -lh2-/-lh3-, which the oracle refuses (see
 * test_lh2_lh3). The cases: -lh2-/-lh3-, a hostile -pm2- copy, truncation, MacLHA (FultaArc returns the
 * MacBinary-wrapped stream the archive stores; the oracle strips the wrapper), the Amiga self-extractor's stub
 * archive, nameless level-0 directories, hostile names (path escape, empty names, control bytes, symlinks), a
 * Unix symbolic link, and a level-2 Unix time and mode. See D-024, docs/research/formats/lha.md. */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers -------------------------------------------------------------------------------------------------- */

static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
    }
    return ~c;
}

/* Append one level-0 "-lh0-" entry (stored) to b. `crc_xor` damages the stored CRC-16. Returns the new length. */
static size_t put_entry(uint8_t *b, size_t at, const uint8_t *name, size_t n, const uint8_t *data, size_t len,
                        uint16_t crc_xor) {
    uint8_t *h = b + at;
    size_t S = 22 + n;
    h[0] = (uint8_t)S;
    memcpy(h + 2, "-lh0-", 5);
    for (int i = 0; i < 4; i++) { h[7 + i] = (uint8_t)(len >> (8 * i)); h[11 + i] = (uint8_t)(len >> (8 * i)); }
    uint32_t dos = (uint32_t)((2010 - 1980) << 9 | 1 << 5 | 1) << 16;   /* 2010-01-01 00:00:00 */
    for (int i = 0; i < 4; i++) h[15 + i] = (uint8_t)(dos >> (8 * i));
    h[19] = 0x20;
    h[20] = 0;                                                         /* level 0 */
    h[21] = (uint8_t)n;
    memcpy(h + 22, name, n);
    uint16_t c = crc16(data, len) ^ crc_xor;
    h[22 + n] = (uint8_t)c;
    h[23 + n] = (uint8_t)(c >> 8);
    unsigned sum = 0;
    for (size_t i = 0; i < S; i++) sum += h[2 + i];
    h[1] = (uint8_t)sum;
    memcpy(h + 2 + S, data, len);
    return at + 2 + S + len;
}

static size_t one_entry(uint8_t *b, const char *name, const char *data, uint16_t crc_xor) {
    size_t n = put_entry(b, 0, (const uint8_t *)name, strlen(name), (const uint8_t *)data, strlen(data), crc_xor);
    b[n++] = 0;                                                        /* end marker */
    return n;
}

typedef struct buf_sink { uint8_t *p; size_t n, cap; } buf_sink_t;
static fulta_arc_err_t buf_write(void *ctx, const void *d, size_t n) {
    buf_sink_t *s = ctx;
    if (s->n + n > s->cap) {
        size_t nc = s->cap ? s->cap : 4096;
        while (nc < s->n + n) nc *= 2;
        uint8_t *q = realloc(s->p, nc);
        if (!q) return FULTA_ARC_ERR_NOMEM;
        s->p = q;
        s->cap = nc;
    }
    memcpy(s->p + s->n, d, n);
    s->n += n;
    return FULTA_ARC_OK;
}

/* Extract entry i into a fresh buffer (caller frees out->p). */
static fulta_arc_err_t grab(fulta_arc_t *arc, size_t i, buf_sink_t *out) {
    memset(out, 0, sizeof *out);
    fulta_arc_sink_t sink = { .ctx = out, .write = buf_write };
    return fulta_arc_extract(arc, i, &sink);
}

static int find_entry(fulta_arc_t *arc, const char *name) {
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (strcmp(fulta_arc_entry(arc, i)->name, name) == 0) return (int)i;
    return -1;
}

static fulta_arc_t *open_mem(const uint8_t *b, size_t n, fulta_arc_codepage_t cp, fulta_arc_err_t *err) {
    fulta_arc_options_t opt = { .codepage = cp };
    fulta_arc_t *arc = NULL;
    *err = fulta_arc_open_memory(b, n, &opt, &arc);
    return arc;
}

/* One stored entry named by `raw` (bytes), opened with code page cp: expect UTF-8 name `want` and the flag. */
static void check_name(const char *what, const char *raw, fulta_arc_codepage_t cp, const char *want, bool not_unicode) {
    uint8_t b[512];
    size_t n = one_entry(b, raw, "hello\n", 0);
    fulta_arc_err_t e;
    fulta_arc_t *arc = open_mem(b, n, cp, &e);
    CHECK(e == FULTA_ARC_OK && arc, "%s: open: %s", what, fulta_arc_strerror(e));
    if (!arc) return;
    CHECK(fulta_arc_format(arc) == FULTA_ARC_FORMAT_LHA, "%s: format", what);
    CHECK(fulta_arc_count(arc) == 1, "%s: %zu entries", what, fulta_arc_count(arc));
    const fulta_arc_entry_t *en = fulta_arc_entry(arc, 0);
    CHECK(en && strcmp(en->name, want) == 0, "%s: name '%s', want '%s'", what, en ? en->name : "?", want);
    CHECK(en && !!(en->flags & FULTA_ARC_ENTRY_NAME_NOT_UNICODE) == not_unicode, "%s: NAME_NOT_UNICODE", what);
    buf_sink_t s;
    e = grab(arc, 0, &s);
    CHECK(e == FULTA_ARC_OK && s.n == 6 && memcmp(s.p, "hello\n", 6) == 0, "%s: extract: %s", what,
          fulta_arc_strerror(e));
    free(s.p);
    fulta_arc_close(arc);
}

/* ---- synthetic archives ---------------------------------------------------------------------------------------- */

static void test_names(void) {
    check_name("ASCII", "dir\\a.txt", FULTA_ARC_CP_AUTO, "dir/a.txt", false);
    check_name("UTF-8", "\xed\x95\x9c\xea\xb8\x80.txt", FULTA_ARC_CP_AUTO, "\xed\x95\x9c\xea\xb8\x80.txt", false);
    /* CP932 fallback: 日本.txt */
    check_name("CP932", "\x93\xfa\x96\x7b.txt", FULTA_ARC_CP_AUTO, "\xe6\x97\xa5\xe6\x9c\xac.txt", true);
    /* 0x5C second bytes: 表.txt (95 5C); ソフト\a.txt (83 5C, then a real separator); 十\十.txt (8F 5C 5C 8F 5C) */
    check_name("CP932 95 5C", "\x95\x5c.txt", FULTA_ARC_CP_AUTO, "\xe8\xa1\xa8.txt", true);
    check_name("CP932 83 5C + separator", "\x83\x5c\x83\x74\x83\x67\\a.txt", FULTA_ARC_CP_AUTO,
               "\xe3\x82\xbd\xe3\x83\x95\xe3\x83\x88/a.txt", true);
    check_name("CP932 8F 5C both sides", "\x8f\x5c\\\x8f\x5c.txt", FULTA_ARC_CP_AUTO,
               "\xe5\x8d\x81/\xe5\x8d\x81.txt", true);
    /* an explicit code page: Big5 許.txt (B3 5C) and GBK 乗.txt (81 5C); CP949 has no 0x5C second byte */
    check_name("CP950 B3 5C", "\xb3\x5c.txt", FULTA_ARC_CP_950, "\xe8\xa8\xb1.txt", true);
    check_name("CP936 81 5C", "\x81\x5c.txt", FULTA_ARC_CP_936, "\xe4\xb9\x97.txt", true);
    check_name("CP949", "\xc7\xd1.txt", FULTA_ARC_CP_949, "\xed\x95\x9c.txt", true);
}

static void test_damage(void) {
    uint8_t b[512];
    fulta_arc_err_t e;
    /* a wrong stored CRC-16: CHECKSUM, and with keep_damaged the bytes are delivered and DAMAGED returned */
    size_t n = one_entry(b, "a.txt", "hello\n", 0x0101);
    fulta_arc_t *arc = open_mem(b, n, FULTA_ARC_CP_AUTO, &e);
    CHECK(e == FULTA_ARC_OK, "crc: open: %s", fulta_arc_strerror(e));
    if (arc) {
        buf_sink_t s;
        e = grab(arc, 0, &s);
        CHECK(e == FULTA_ARC_ERR_CHECKSUM, "crc: extract gives %s, want checksum mismatch", fulta_arc_strerror(e));
        free(s.p);
        fulta_arc_close(arc);
    }
    fulta_arc_options_t kd = { .keep_damaged = true };
    arc = NULL;
    e = fulta_arc_open_memory(b, n, &kd, &arc);
    if (arc) {
        buf_sink_t s;
        e = grab(arc, 0, &s);
        CHECK(e == FULTA_ARC_DAMAGED && s.n == 6, "crc keep_damaged: %d, %zu bytes", (int)e, s.n);
        free(s.p);
        fulta_arc_close(arc);
    }
    /* data cut short: listed, extraction says TRUNCATED */
    n = one_entry(b, "a.txt", "hello world\n", 0);
    arc = open_mem(b, n - 6, FULTA_ARC_CP_AUTO, &e);
    CHECK(e == FULTA_ARC_OK && fulta_arc_count(arc) == 1, "truncated: open: %s", fulta_arc_strerror(e));
    if (arc) {
        buf_sink_t s;
        e = grab(arc, 0, &s);
        CHECK(e == FULTA_ARC_ERR_TRUNCATED, "truncated: extract gives %s", fulta_arc_strerror(e));
        free(s.p);
        fulta_arc_close(arc);
    }
    /* a wrong header sum on the first entry: not an archive (an LHA-like pattern may not claim a file) */
    n = one_entry(b, "a.txt", "hello\n", 0);
    b[1] ^= 0x55;
    arc = open_mem(b, n, FULTA_ARC_CP_AUTO, &e);
    CHECK(e == FULTA_ARC_ERR_NOT_ARCHIVE && !arc, "bad sum: %s", fulta_arc_strerror(e));
    fulta_arc_close(arc);
    /* a damaged second entry ends the listing and keeps the first */
    n = put_entry(b, 0, (const uint8_t *)"a.txt", 5, (const uint8_t *)"one\n", 4, 0);
    size_t second = n;
    n = put_entry(b, n, (const uint8_t *)"b.txt", 5, (const uint8_t *)"two\n", 4, 0);
    b[n++] = 0;
    b[second + 1] ^= 0x55;
    arc = open_mem(b, n, FULTA_ARC_CP_AUTO, &e);
    CHECK(e == FULTA_ARC_OK && fulta_arc_count(arc) == 1 && find_entry(arc, "a.txt") == 0,
          "damaged second entry: %s, %zu entries", fulta_arc_strerror(e), arc ? fulta_arc_count(arc) : 0);
    fulta_arc_close(arc);
}

static void test_detection(void) {
    fulta_arc_err_t e;
    /* "-lh5-" at offset 2 followed by noise: the pattern alone does not make an LHA archive. (Level 2 has no byte
     * sum; this noise is refused because its header-size field runs past the data - lhadec says "truncated" - not
     * by a header CRC. A structurally plausible level-2 header without the CRC unit 0x00 would be accepted on
     * structure alone; its data would still fail the CRC-16. See lha.md §4.2.) */
    uint8_t junk[64];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37 + 11);
    memcpy(junk + 2, "-lh5-", 5);
    junk[20] = 2;                                                      /* a level the sum check does not cover */
    fulta_arc_t *arc = open_mem(junk, sizeof junk, FULTA_ARC_CP_AUTO, &e);
    CHECK(!arc && e == FULTA_ARC_ERR_NOT_ARCHIVE, "pattern + noise: %s", fulta_arc_strerror(e));
    fulta_arc_close(arc);
    /* text that mentions the method string is not LHA */
    const char *txt = "The -lh5- method of LHA uses an 8 KiB window and static Huffman codes.\n";
    arc = open_mem((const uint8_t *)txt, strlen(txt), FULTA_ARC_CP_AUTO, &e);
    CHECK(!arc && e == FULTA_ARC_ERR_NOT_ARCHIVE, "text: %s", fulta_arc_strerror(e));
    fulta_arc_close(arc);

    /* self-extractor gate: an "MZ" stub is searched behind; the same bytes behind an unknown prefix are not */
    uint8_t b[1024];
    memset(b, 0x90, 300);
    b[0] = 'M';
    b[1] = 'Z';
    size_t n = put_entry(b, 300, (const uint8_t *)"a.txt", 5, (const uint8_t *)"hello\n", 6, 0);
    b[n++] = 0;
    arc = open_mem(b, n, FULTA_ARC_CP_AUTO, &e);
    CHECK(e == FULTA_ARC_OK && arc && fulta_arc_format(arc) == FULTA_ARC_FORMAT_LHA && fulta_arc_count(arc) == 1,
          "MZ stub: %s", fulta_arc_strerror(e));
    if (arc) {
        buf_sink_t s;
        e = grab(arc, 0, &s);
        CHECK(e == FULTA_ARC_OK && s.n == 6, "MZ stub: extract: %s", fulta_arc_strerror(e));
        free(s.p);
    }
    fulta_arc_close(arc);
    b[0] = 'Q';
    b[1] = 'Q';
    arc = open_mem(b, n, FULTA_ARC_CP_AUTO, &e);
    CHECK(!arc && e == FULTA_ARC_ERR_NOT_ARCHIVE, "unknown prefix: %s (want not an archive)", fulta_arc_strerror(e));
    fulta_arc_close(arc);
    CHECK(strcmp(fulta_arc_format_name(FULTA_ARC_FORMAT_LHA), "LHA") == 0, "format name");
}

/* ---- corpus archives excluded from the manifest ----------------------------------------------------------------- */

static fulta_arc_t *open_fixture(const char *rel, const fulta_arc_options_t *opt, bool *present) {
    char path[4096];
    snprintf(path, sizeof path, "%s/lha/%s", t_fixtures(), rel);
    FILE *f = fopen(path, "rb");
    *present = f != NULL;
    if (!f) return NULL;
    fclose(f);
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_file(path, opt, &arc);
    CHECK(e == FULTA_ARC_OK && arc && fulta_arc_format(arc) == FULTA_ARC_FORMAT_LHA, "%s: open: %s", rel,
          fulta_arc_strerror(e));
    return e ? NULL : arc;
}

/* The two corpus archives the oracle refuses: -lh2- and -lh3-, both made by UNLHA32 from the same LICENSE.MIT. No
 * independent decoder exists here, so the size and CRC-32 below are lhadec's output (room log L0019/L0020: tested
 * against UNLHA32 as an instrument). They are held by the stored CRC-16, which lhadec checks, and by the two methods
 * giving the same file. */
static void test_lh2_lh3(void) {
    static const char *const rel[] = {"unlha32/lh2.lzh", "unlha32/lh3.lzh"};
    static const char *const method[] = {"lh2", "lh3"};
    for (size_t k = 0; k < 2; k++) {
        bool present;
        fulta_arc_t *arc = open_fixture(rel[k], NULL, &present);
        if (!present) { SKIP("lha/%s not present", rel[k]); continue; }
        if (!arc) continue;
        int i = find_entry(arc, "LICENSE.MIT");
        CHECK(i >= 0, "%s: LICENSE.MIT listed", rel[k]);
        if (i >= 0) {
            const fulta_arc_entry_t *en = fulta_arc_entry(arc, (size_t)i);
            CHECK(!(en->flags & FULTA_ARC_ENTRY_UNSUPPORTED) && strcmp(en->method, method[k]) == 0, "%s: flags %x method %s",
                  rel[k], en->flags, en->method);
            buf_sink_t s;
            fulta_arc_err_t e = grab(arc, (size_t)i, &s);
            CHECK(e == FULTA_ARC_OK && s.n == 2853 && crc32_of(s.p, s.n) == 0x2cfe6fc4, "%s: extract gives %s, %zu bytes",
                  rel[k], fulta_arc_strerror(e), s.n);
            free(s.p);
        }
        fulta_arc_close(arc);
    }
}

/* A method string lhadec does not name ("-lh9-", synthetic): listed as unsupported, its extraction refused. */
static void test_unknown_method(void) {
    uint8_t b[256];
    size_t n = one_entry(b, "x.txt", "hello", 0);
    memcpy(b + 4, "h9", 2);                                            /* "-lh0-" -> "-lh9-", then the header sum again */
    unsigned sum = 0;
    for (size_t i = 0; i < b[0]; i++) sum += b[2 + i];
    b[1] = (uint8_t)sum;
    fulta_arc_err_t e;
    fulta_arc_t *arc = open_mem(b, n, FULTA_ARC_CP_AUTO, &e);
    CHECK(arc && e == FULTA_ARC_OK, "-lh9-: open: %s", fulta_arc_strerror(e));
    if (!arc) return;
    const fulta_arc_entry_t *en = fulta_arc_count(arc) == 1 ? fulta_arc_entry(arc, 0) : NULL;
    CHECK(en && (en->flags & FULTA_ARC_ENTRY_UNSUPPORTED) && strcmp(en->method, "lh9") == 0, "-lh9-: listed unsupported");
    if (en) {
        buf_sink_t s;
        e = grab(arc, 0, &s);
        CHECK(e == FULTA_ARC_ERR_UNSUPPORTED && s.n == 0, "-lh9-: extract gives %s", fulta_arc_strerror(e));
        free(s.p);
    }
    fulta_arc_close(arc);
}

/* regression/evil_pm2.lzh: a -pm2- copy reaching before the first output byte. The room's reader rejects it (methods
 * §8.6); the oracle fails on it too. */
static void test_evil_pm2(void) {
    bool present;
    fulta_arc_t *arc = open_fixture("regression/evil_pm2.lzh", NULL, &present);
    if (!present) { SKIP("lha/regression/evil_pm2.lzh not present"); return; }
    if (!arc) return;
    int i = find_entry(arc, "A");
    CHECK(i >= 0, "evil_pm2.lzh: A listed");
    if (i >= 0) {
        buf_sink_t s;
        fulta_arc_err_t e = grab(arc, (size_t)i, &s);
        CHECK(e == FULTA_ARC_ERR_CORRUPT, "evil_pm2.lzh: extract gives %s (want corrupt)", fulta_arc_strerror(e));
        free(s.p);
    }
    fulta_arc_close(arc);
}

static void test_truncated(void) {
    bool present;
    fulta_arc_t *arc = open_fixture("regression/truncated.lzh", NULL, &present);
    if (!present) { SKIP("lha/regression/truncated.lzh not present"); return; }
    if (!arc) return;
    int i = find_entry(arc, "GPL-2");
    CHECK(i >= 0, "truncated.lzh: GPL-2 listed");
    if (i >= 0) {
        buf_sink_t s;
        fulta_arc_err_t e = grab(arc, (size_t)i, &s);
        CHECK(e == FULTA_ARC_ERR_TRUNCATED, "truncated.lzh: extract gives %s", fulta_arc_strerror(e));
        free(s.p);
    }
    fulta_arc_close(arc);
}

/* MacLHA 2.24 stores each file as a MacBinary stream; the oracle strips that wrapper, FultaArc returns what is stored.
 * Observed on all ten: FultaArc's length is the oracle's plus 128, rounded up to a multiple of 128, and bytes
 * [128, 128 + oracle length) are the oracle's file. lhadec's CRC-16 check passing on the whole output is the archive's
 * own witness that the wrapped stream is what MacLHA stored. Oracle values: lhamanifest.py run, 2026-10-16. */
static void test_maclha(void) {
    static const struct { const char *rel, *name; size_t osize; uint32_t ocrc; } c[] = {
        {"maclha_224/l1_lh0.lzh", "gpl-2.gz", 6829, 0xe4690583}, {"maclha_224/l2_lh0.lzh", "gpl-2.gz", 6829, 0xe4690583},
        {"maclha_224/l1_lh1.lzh", "gpl-2", 18092, 0x4e46f4a1}, {"maclha_224/l2_lh1.lzh", "gpl-2", 18092, 0x4e46f4a1},
        {"maclha_224/l1_lh5.lzh", "gpl-2", 18092, 0x4e46f4a1}, {"maclha_224/l2_lh5.lzh", "gpl-2", 18092, 0x4e46f4a1},
        {"maclha_224/l1_subdir.lzh", "subdir/subdir2/hello.txt", 11, 0x0d4a1185},
        {"maclha_224/l2_subdir.lzh", "subdir/subdir2/hello.txt", 11, 0x0d4a1185},
        {"maclha_224/l1_full_subdir.lzh", "Untitled/subdir/subdir2/hello.txt", 11, 0x0d4a1185},
        {"maclha_224/l2_full_subdir.lzh", "Untitled/subdir/subdir2/hello.txt", 11, 0x0d4a1185},
    };
    for (size_t k = 0; k < sizeof c / sizeof *c; k++) {
        bool present;
        fulta_arc_t *arc = open_fixture(c[k].rel, NULL, &present);
        if (!present) { SKIP("lha/%s not present", c[k].rel); continue; }
        if (!arc) continue;
        int i = find_entry(arc, c[k].name);
        CHECK(i >= 0, "%s: %s listed", c[k].rel, c[k].name);
        if (i >= 0) {
            buf_sink_t s;
            fulta_arc_err_t e = grab(arc, (size_t)i, &s);
            size_t want = (128 + c[k].osize + 127) / 128 * 128;
            CHECK(e == FULTA_ARC_OK, "%s: extract: %s", c[k].rel, fulta_arc_strerror(e));
            CHECK(s.n == want && crc32_of(s.p + 128, c[k].osize) == c[k].ocrc, "%s: %zu bytes (want %zu), inner crc",
                  c[k].rel, s.n, want);
            free(s.p);
        }
        fulta_arc_close(arc);
    }
}

/* Archives where the oracle and FultaArc list differently, by documented policy. */
static void test_listing_policies(void) {
    bool present;
    /* the Amiga self-extractor keeps a small archive inside its stub; FultaArc lists it too (room container §6) */
    fulta_arc_t *arc = open_fixture("lha_amiga_122/sfx.run", NULL, &present);
    if (!present) SKIP("lha/lha_amiga_122/sfx.run not present");
    if (arc) {
        int u = find_entry(arc, "SFXUsage.txt"), g = find_entry(arc, "gpl-2");
        CHECK(u >= 0 && g >= 0 && fulta_arc_count(arc) == 2, "sfx.run: entries");
        if (g >= 0) {
            buf_sink_t s;
            fulta_arc_err_t e = grab(arc, (size_t)g, &s);
            CHECK(e == FULTA_ARC_OK && s.n == 18092 && crc32_of(s.p, s.n) == 0x4e46f4a1, "sfx.run: gpl-2 (oracle)");
            free(s.p);
        }
        if (u >= 0) {
            buf_sink_t s;
            fulta_arc_err_t e = grab(arc, (size_t)u, &s);
            CHECK(e == FULTA_ARC_OK, "sfx.run: SFXUsage.txt CRC-16: %s", fulta_arc_strerror(e));
            free(s.p);
        }
        fulta_arc_close(arc);
    }
    /* level-0 directory entries without names: the oracle lists nothing; FultaArc lists the file, CRC-16 holds */
    static const char *const h0[] = {"lha_unix114i/h0_subdir.lzh", "lha_osk_201/h0_subdir.lzh"};
    for (size_t k = 0; k < 2; k++) {
        arc = open_fixture(h0[k], NULL, &present);
        if (!present) { SKIP("lha/%s not present", h0[k]); continue; }
        if (!arc) continue;
        int i = find_entry(arc, "hello.txt");
        CHECK(i >= 0, "%s: hello.txt listed", h0[k]);
        if (i >= 0) {
            buf_sink_t s;
            fulta_arc_err_t e = grab(arc, (size_t)i, &s);
            CHECK(e == FULTA_ARC_OK, "%s: extract: %s", h0[k], fulta_arc_strerror(e));
            free(s.p);
        }
        fulta_arc_close(arc);
    }
    /* directories only */
    arc = open_fixture("regression/dir.lzh", NULL, &present);
    if (!present) SKIP("lha/regression/dir.lzh not present");
    if (arc) {
        CHECK(fulta_arc_count(arc) > 0, "dir.lzh: entries");
        for (size_t i = 0; i < fulta_arc_count(arc); i++)
            CHECK(fulta_arc_entry(arc, i)->flags & FULTA_ARC_ENTRY_DIR, "dir.lzh: %s is a directory",
                  fulta_arc_entry(arc, i)->name);
        fulta_arc_close(arc);
    }
}

/* The corpus' hostile archives: no listed name is absolute or holds a ".." component, every extraction returns, and a
 * symbolic link is reported as one (with its target as data) rather than followed. */
static void test_hostile(void) {
    static const char *const c[] = {"regression/badterm.lzh", "regression/dotdot.lzh", "regression/empty_fn.lzh",
                                    "regression/symlink1.lzh", "regression/symlink2.lzh", "regression/symlink3.lzh"};
    for (size_t k = 0; k < sizeof c / sizeof *c; k++) {
        bool present;
        fulta_arc_t *arc = open_fixture(c[k], NULL, &present);
        if (!present) { SKIP("lha/%s not present", c[k]); continue; }
        if (!arc) continue;
        for (size_t i = 0; i < fulta_arc_count(arc); i++) {
            const char *nm = fulta_arc_entry(arc, i)->name;
            bool dotdot = strcmp(nm, "..") == 0 || strncmp(nm, "../", 3) == 0 || strstr(nm, "/../") ||
                          (strlen(nm) >= 3 && strcmp(nm + strlen(nm) - 3, "/..") == 0);
            CHECK(nm[0] && nm[0] != '/' && !dotdot, "%s: unsafe name '%s'", c[k], nm);
            bool ctl = false;   /* badterm.lzh stores a terminal escape and a newline: '_' in the name (D-045) */
            for (const char *q = nm; *q; q++) ctl |= (unsigned char)*q < 0x20 || *q == 0x7F;
            CHECK(!ctl, "%s: a control byte in the name", c[k]);
            buf_sink_t s;
            (void)grab(arc, i, &s);
            free(s.p);
        }
        fulta_arc_close(arc);
    }
    bool present;
    fulta_arc_t *arc = open_fixture("regression/symlink2.lzh", NULL, &present);
    if (arc) {
        int i = find_entry(arc, "etc");
        CHECK(i >= 0 && (fulta_arc_entry(arc, (size_t)i)->flags & FULTA_ARC_ENTRY_SYMLINK), "symlink2: etc is a link");
        if (i >= 0) {
            buf_sink_t s;
            fulta_arc_err_t e = grab(arc, (size_t)i, &s);
            CHECK(e == FULTA_ARC_OK && s.n == 9 && memcmp(s.p, "../../etc", 9) == 0, "symlink2: target");
            free(s.p);
        }
        fulta_arc_close(arc);
    }
}

/* Unix fields: a symbolic link (a "-lhd-" entry named "name|target") and a level-2 entry's Unix time and mode; the
 * expected values are the oracle's header dump (dump-headers, lhasa 0.6.0). */
static void test_unix(void) {
    bool present;
    fulta_arc_t *arc = open_fixture("lha_unix114i/h1_symlink.lzh", NULL, &present);
    if (!present) SKIP("lha/lha_unix114i/h1_symlink.lzh not present");
    if (arc) {
        int i = find_entry(arc, "symlink");
        const fulta_arc_entry_t *en = i >= 0 ? fulta_arc_entry(arc, (size_t)i) : NULL;
        CHECK(en && (en->flags & FULTA_ARC_ENTRY_SYMLINK) && !(en->flags & FULTA_ARC_ENTRY_DIR) && en->size == 6,
              "h1_symlink: a 6-byte symbolic link, not a directory");
        if (en) {
            buf_sink_t s;
            fulta_arc_err_t e = grab(arc, (size_t)i, &s);
            CHECK(e == FULTA_ARC_OK && s.n == 6 && memcmp(s.p, "target", 6) == 0, "h1_symlink: target");
            free(s.p);
        }
        fulta_arc_close(arc);
    }
    arc = open_fixture("lha_unix114i/h2_lh5.lzh", NULL, &present);
    if (!present) SKIP("lha/lha_unix114i/h2_lh5.lzh not present");
    if (arc) {
        int i = find_entry(arc, "gpl-2");
        const fulta_arc_entry_t *en = i >= 0 ? fulta_arc_entry(arc, (size_t)i) : NULL;
        CHECK(en && (en->flags & FULTA_ARC_ENTRY_HAS_MTIME) && en->mtime == INT64_C(1262304000) * 1000000000,
              "h2_lh5: Unix time");
        CHECK(en && en->unix_mode == 0100444 && strcmp(en->method, "lh5") == 0, "h2_lh5: mode and method");
        fulta_arc_close(arc);
    }
}

int main(void) {
    test_names();
    test_damage();
    test_detection();
    test_unknown_method();
    test_lh2_lh3();
    test_evil_pm2();
    test_truncated();
    test_maclha();
    test_listing_policies();
    test_hostile();
    test_unix();
    return t_report("test_lha");
}
