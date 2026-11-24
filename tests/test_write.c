/* test_write.c - write ZIP and 7z archives (levels 0, 1, 6, 9, Zip64 forced, folders, empty files, UTF-8 names)
 * and read them back through FultaArc's readers, byte for byte. */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>

typedef struct mem { unsigned char *p; size_t n, cap; } mem_t;

static fulta_arc_err_t m_write(void *ctx, const void *d, size_t n) {
    mem_t *m = ctx;
    if (m->n + n > m->cap) {
        size_t c = m->cap ? m->cap * 2 : 4096;
        while (c < m->n + n) c *= 2;
        unsigned char *p = realloc(m->p, c);
        if (!p) return FULTA_ARC_ERR_NOMEM;
        m->p = p;
        m->cap = c;
    }
    memcpy(m->p + m->n, d, n);
    m->n += n;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t m_patch(void *ctx, uint64_t off, const void *d, size_t n) {
    mem_t *m = ctx;
    if (off + n > m->n) return FULTA_ARC_ERR_IO;
    memcpy(m->p + off, d, n);
    return FULTA_ARC_OK;
}

int main(void) {
    static unsigned char big[300000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (unsigned char)((i * 7) ^ (i >> 5));
    for (size_t i = 0; i + 5 < sizeof big; i += 64) {   /* scatter x86 CALL rel32 so branch filters have real work */
        big[i] = 0xE8; big[i + 1] = (unsigned char)i; big[i + 2] = (unsigned char)(i >> 8); big[i + 3] = 0; big[i + 4] = 0;
    }
    const char *txt = "hello, FultaArc\n";
    fulta_arc_write_entry_t en[] = {
        {.name = "a.txt", .data = txt, .size = strlen(txt), .mtime = 1700000000},
        {.name = "dir/", .is_dir = 1},
        {.name = "dir/big.bin", .data = big, .size = sizeof big, .mtime = 1700000000},
        {.name = "empty.txt", .size = 0},
        {.name = "\xed\x95\x9c\xea\xb8\x80.txt", .data = txt, .size = 5},
    };
    size_t n = sizeof en / sizeof *en;
    fulta_arc_format_t fmts[2] = {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_FORMAT_7Z};
    int levels[5] = {0, 1, 6, 9, -1};
    for (int f = 0; f < 2; f++)
        for (int l = 0; l < 5; l++) {
            mem_t m = {0};
            fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
            fulta_arc_write_options_t o = {.level = levels[l] < 0 ? 6 : levels[l], .zip64 = levels[l] < 0};
            fulta_arc_err_t e = fulta_arc_write(fmts[f], en, n, &o, &sink);
            CHECK(e == FULTA_ARC_OK, "%s level %d: write %s", fulta_arc_format_name(fmts[f]), levels[l], fulta_arc_strerror(e));
            fulta_arc_t *arc = NULL;
            e = fulta_arc_open_memory(m.p, m.n, NULL, &arc);
            CHECK(e == FULTA_ARC_OK, "%s level %d: open %s", fulta_arc_format_name(fmts[f]), levels[l], fulta_arc_strerror(e));
            if (!e) {
                CHECK(fulta_arc_count(arc) == n, "count %zu", fulta_arc_count(arc));
                for (size_t i = 0; i < fulta_arc_count(arc) && i < n; i++) {
                    const fulta_arc_entry_t *x = fulta_arc_entry(arc, i);
                    char want[64];
                    snprintf(want, sizeof want, "%s", en[i].name);
                    size_t L = strlen(want);
                    if (L && want[L - 1] == '/') want[L - 1] = 0;
                    CHECK(strcmp(x->name, want) == 0, "name %s vs %s", x->name, want);
                    CHECK(!!(x->flags & FULTA_ARC_ENTRY_DIR) == !!(en[i].is_dir), "%s dir flag", x->name);
                    void *d = NULL;
                    size_t dn = 0;
                    e = fulta_arc_extract_alloc(arc, i, &d, &dn);
                    CHECK(e == FULTA_ARC_OK, "%s extract %s", x->name, fulta_arc_strerror(e));
                    if (!e && !en[i].is_dir) CHECK(dn == en[i].size && (dn == 0 || memcmp(d, en[i].data, dn) == 0), "%s data", x->name);
                    fulta_arc_free(d);
                }
                fulta_arc_close(arc);
            }
            free(m.p);
        }
    fulta_arc_write_entry_t bad[] = {{.name = "../x"}, {.name = "/abs"}, {.name = "a//b"}, {.name = "C:x"}};
    for (size_t i = 0; i < 4; i++) {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, &bad[i], 1, NULL, &sink) == FULTA_ARC_ERR_BAD_NAME, "bad name %s", bad[i].name);
        free(m.p);
    }

    /* ---- RFC-0004 phase 1: the method/filter selector (no new methods yet) ---- */
    static unsigned char zbuf[20000];               /* highly compressible */
    for (size_t i = 0; i < sizeof zbuf; i++) zbuf[i] = (unsigned char)(i & 1);
    fulta_arc_write_entry_t one[] = {{.name = "z.bin", .data = zbuf, .size = sizeof zbuf, .mtime = 1700000000}};

    struct { fulta_arc_format_t fmt; fulta_arc_method_t method; const char *tag; bool expect_small; } ok[] = {
        {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_METHOD_STORE,   "zip store",   false},
        {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_METHOD_DEFLATE, "zip deflate", true},
        {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_METHOD_BZIP2,   "zip bzip2",   true},
        {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_METHOD_LZMA,    "zip lzma",    true},
        {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_METHOD_PPMD,    "zip ppmd",    true},
        {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_METHOD_ZSTD,    "zip zstd",    true},
        {FULTA_ARC_FORMAT_7Z,  FULTA_ARC_METHOD_STORE,   "7z copy",     false},
        {FULTA_ARC_FORMAT_7Z,  FULTA_ARC_METHOD_LZMA,    "7z lzma",     true},
    };
    for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t o = {.level = 6, .method = ok[i].method};
        fulta_arc_err_t e = fulta_arc_write(ok[i].fmt, one, 1, &o, &sink);
        CHECK(e == FULTA_ARC_OK, "%s: write %s", ok[i].tag, fulta_arc_strerror(e));
        fulta_arc_t *arc = NULL;
        if (!e) e = fulta_arc_open_memory(m.p, m.n, NULL, &arc);
        CHECK(e == FULTA_ARC_OK && arc, "%s: open %s", ok[i].tag, fulta_arc_strerror(e));
        if (!e && arc) {
            void *d = NULL; size_t dn = 0;
            e = fulta_arc_extract_alloc(arc, 0, &d, &dn);
            CHECK(e == FULTA_ARC_OK && dn == sizeof zbuf && memcmp(d, zbuf, dn) == 0, "%s: round-trip", ok[i].tag);
            if (ok[i].fmt == FULTA_ARC_FORMAT_ZIP) {   /* ZIP reports packed_size per entry; 7z may be 0 (folder) */
                uint64_t ps = fulta_arc_entry(arc, 0)->packed_size;
                if (ok[i].expect_small) CHECK(ps > 0 && ps < sizeof zbuf / 4, "%s: compressed (packed %llu)", ok[i].tag, (unsigned long long)ps);
                else CHECK(ps >= sizeof zbuf, "%s: stored (packed %llu)", ok[i].tag, (unsigned long long)ps);
            }
            fulta_arc_free(d);
            fulta_arc_close(arc);
        }
        free(m.p);
    }

    /* PPMd on incompressible input: exercises the growable byte-out (PPMd expands) and still round-trips. */
    {
        static unsigned char rnd[8000];
        uint32_t st = 0x12345678u;
        for (size_t i = 0; i < sizeof rnd; i++) { st = st * 1664525u + 1013904223u; rnd[i] = (unsigned char)(st >> 24); }
        fulta_arc_write_entry_t re[] = {{.name = "r.bin", .data = rnd, .size = sizeof rnd, .mtime = 1700000000}};
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t o = {.level = 9, .method = FULTA_ARC_METHOD_PPMD};
        fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_ZIP, re, 1, &o, &sink);
        CHECK(e == FULTA_ARC_OK, "zip ppmd incompressible: write %s", fulta_arc_strerror(e));
        fulta_arc_t *arc = NULL;
        if (!e) e = fulta_arc_open_memory(m.p, m.n, NULL, &arc);
        CHECK(e == FULTA_ARC_OK && arc, "zip ppmd incompressible: open %s", fulta_arc_strerror(e));
        if (!e && arc) {
            void *d = NULL; size_t dn = 0;
            e = fulta_arc_extract_alloc(arc, 0, &d, &dn);
            CHECK(e == FULTA_ARC_OK && dn == sizeof rnd && memcmp(d, rnd, dn) == 0, "zip ppmd incompressible: round-trip");
            fulta_arc_free(d);
            fulta_arc_close(arc);
        }
        free(m.p);
    }

    /* 7z method selection over the full multi-entry set (dir + empty + UTF-8 name): byte round-trip and the
       reader's reported coder name must match what we asked for (a round-trip alone cannot tell LZMA2 from LZMA). */
    struct { fulta_arc_method_t method; const char *coder; } m7[] = {
        {FULTA_ARC_METHOD_STORE,   "Copy"},
        {FULTA_ARC_METHOD_LZMA,    "LZMA"},
        {FULTA_ARC_METHOD_LZMA2,   "LZMA2"},
        {FULTA_ARC_METHOD_BZIP2,   "BZip2"},
        {FULTA_ARC_METHOD_DEFLATE, "Deflate"},
        {FULTA_ARC_METHOD_PPMD,    "PPMd"},
    };
    for (size_t k = 0; k < sizeof m7 / sizeof *m7; k++) {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t o = {.level = 6, .method = m7[k].method};
        fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_7Z, en, n, &o, &sink);
        CHECK(e == FULTA_ARC_OK, "7z %s: write %s", m7[k].coder, fulta_arc_strerror(e));
        fulta_arc_t *arc = NULL;
        if (!e) e = fulta_arc_open_memory(m.p, m.n, NULL, &arc);
        CHECK(e == FULTA_ARC_OK && arc, "7z %s: open %s", m7[k].coder, fulta_arc_strerror(e));
        if (!e && arc) {
            CHECK(fulta_arc_count(arc) == n, "7z %s: count", m7[k].coder);
            for (size_t i = 0; i < fulta_arc_count(arc) && i < n; i++) {
                const fulta_arc_entry_t *x = fulta_arc_entry(arc, i);
                void *d = NULL; size_t dn = 0;
                fulta_arc_err_t xe = fulta_arc_extract_alloc(arc, i, &d, &dn);
                CHECK(xe == FULTA_ARC_OK, "7z %s: extract %s", m7[k].coder, fulta_arc_strerror(xe));
                if (!xe && !en[i].is_dir) CHECK(dn == en[i].size && (dn == 0 || memcmp(d, en[i].data, dn) == 0), "7z %s: %s data", m7[k].coder, en[i].name);
                /* the big non-empty file carries the folder's coder; assert the reader names it as asked */
                if (!xe && i == 2) CHECK(x->method && strcmp(x->method, m7[k].coder) == 0, "7z %s: coder name is %s (got %s)", m7[k].coder, m7[k].coder, x->method ? x->method : "(null)");
                fulta_arc_free(d);
            }
            fulta_arc_close(arc);
        }
        free(m.p);
    }

    /* RFC-0004 phase 6: 7z content filters chained before the main coder. Each round-trips through our reader and
       the reader names the filter in the coder chain (Delta/BCJ by name; the branch family reports "Filter", so the
       per-architecture oracle is 7z l -slt, run manually). Delta covers the distance boundaries 1/4/256. The filter
       transform's correctness is cross-checked with system 7z t (a different codec); a round-trip through our own
       reader uses the same vendored filter both ways and cannot catch a transform bug on its own. */
    struct { fulta_arc_filter_t filter; uint32_t dist; fulta_arc_method_t method; const char *tok; const char *tag; } flt[] = {
        {FULTA_ARC_FILTER_DELTA,    4, FULTA_ARC_METHOD_LZMA2, "Delta",  "delta:4 + lzma2"},
        {FULTA_ARC_FILTER_DELTA,    1, FULTA_ARC_METHOD_LZMA,  "Delta",  "delta:1 + lzma"},
        {FULTA_ARC_FILTER_DELTA,  256, FULTA_ARC_METHOD_LZMA2, "Delta",  "delta:256 + lzma2"},
        {FULTA_ARC_FILTER_BCJ_X86,  0, FULTA_ARC_METHOD_LZMA2, "BCJ",    "bcj-x86 + lzma2"},
        {FULTA_ARC_FILTER_BCJ_X86,  0, FULTA_ARC_METHOD_STORE, "BCJ",    "bcj-x86 + store"},
        {FULTA_ARC_FILTER_ARM,      0, FULTA_ARC_METHOD_LZMA2, "Filter", "arm + lzma2"},
        {FULTA_ARC_FILTER_ARM64,    0, FULTA_ARC_METHOD_LZMA2, "Filter", "arm64 + lzma2"},
        {FULTA_ARC_FILTER_PPC,      0, FULTA_ARC_METHOD_LZMA,  "Filter", "ppc + lzma"},
        {FULTA_ARC_FILTER_SPARC,    0, FULTA_ARC_METHOD_LZMA2, "Filter", "sparc + lzma2"},
        {FULTA_ARC_FILTER_IA64,     0, FULTA_ARC_METHOD_BZIP2, "Filter", "ia64 + bzip2"},
        {FULTA_ARC_FILTER_ARMT,     0, FULTA_ARC_METHOD_LZMA2, "Filter", "armt + lzma2"},
        {FULTA_ARC_FILTER_RISCV,    0, FULTA_ARC_METHOD_LZMA2, "Filter", "riscv + lzma2"},
        {FULTA_ARC_FILTER_RISCV,    0, FULTA_ARC_METHOD_STORE, "Filter", "riscv + store"},
        /* BCJ2: the 4-input/1-output filter. `big` carries scattered E8 CALLs, so the call/jump streams are non-empty;
           the sub-streams are each compressed by `method` (STORE leaves them raw), RC is stored. */
        {FULTA_ARC_FILTER_BCJ2,     0, FULTA_ARC_METHOD_LZMA2, "BCJ2",   "bcj2 + lzma2"},
        {FULTA_ARC_FILTER_BCJ2,     0, FULTA_ARC_METHOD_LZMA,  "BCJ2",   "bcj2 + lzma"},
        {FULTA_ARC_FILTER_BCJ2,     0, FULTA_ARC_METHOD_BZIP2, "BCJ2",   "bcj2 + bzip2"},
        {FULTA_ARC_FILTER_BCJ2,     0, FULTA_ARC_METHOD_STORE, "BCJ2",   "bcj2 + store"},
    };
    for (size_t k = 0; k < sizeof flt / sizeof *flt; k++) {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t o = {.level = 6, .method = flt[k].method, .filter = flt[k].filter, .filter_delta_dist = flt[k].dist};
        fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_7Z, en, n, &o, &sink);
        CHECK(e == FULTA_ARC_OK, "7z %s: write %s", flt[k].tag, fulta_arc_strerror(e));
        fulta_arc_t *arc = NULL;
        if (!e) e = fulta_arc_open_memory(m.p, m.n, NULL, &arc);
        CHECK(e == FULTA_ARC_OK && arc, "7z %s: open %s", flt[k].tag, fulta_arc_strerror(e));
        if (!e && arc) {
            CHECK(fulta_arc_count(arc) == n, "7z %s: count", flt[k].tag);
            for (size_t i = 0; i < fulta_arc_count(arc) && i < n; i++) {
                const fulta_arc_entry_t *x = fulta_arc_entry(arc, i);
                void *d = NULL; size_t dn = 0;
                fulta_arc_err_t xe = fulta_arc_extract_alloc(arc, i, &d, &dn);
                CHECK(xe == FULTA_ARC_OK, "7z %s: extract %s", flt[k].tag, fulta_arc_strerror(xe));
                if (!xe && !en[i].is_dir) CHECK(dn == en[i].size && (dn == 0 || memcmp(d, en[i].data, dn) == 0), "7z %s: %s data", flt[k].tag, en[i].name);
                if (!xe && i == 2) CHECK(x->method && strstr(x->method, flt[k].tok), "7z %s: coder chain names %s (got %s)", flt[k].tag, flt[k].tok, x->method ? x->method : "(null)");
                fulta_arc_free(d);
            }
            fulta_arc_close(arc);
        }
        free(m.p);
    }

    /* BCJ2 on data with no x86 branches: the CALL and JUMP streams are empty (0-byte packed streams). Exercise both
       STORE (raw empty packs) and LZMA2 (a compressor over empty input), round-tripping through our reader. */
    {
        const char *plain = "The quick brown fox jumps over the lazy dog, repeatedly and without any E8 bytes. ";
        char nobr[4000];
        for (size_t i = 0; i < sizeof nobr; i++) nobr[i] = plain[i % 82];
        fulta_arc_write_entry_t nb[1] = {{.name = "text.dat", .data = nobr, .size = sizeof nobr, .mtime = 1700000000}};
        fulta_arc_method_t subs[2] = {FULTA_ARC_METHOD_STORE, FULTA_ARC_METHOD_LZMA2};
        for (int j = 0; j < 2; j++) {
            mem_t m = {0};
            fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
            fulta_arc_write_options_t o = {.level = 6, .method = subs[j], .filter = FULTA_ARC_FILTER_BCJ2};
            fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_7Z, nb, 1, &o, &sink);
            CHECK(e == FULTA_ARC_OK, "7z bcj2 no-branch[%d]: write %s", j, fulta_arc_strerror(e));
            fulta_arc_t *arc = NULL;
            if (!e) e = fulta_arc_open_memory(m.p, m.n, NULL, &arc);
            CHECK(e == FULTA_ARC_OK && arc, "7z bcj2 no-branch[%d]: open", j);
            if (!e && arc) {
                void *d = NULL; size_t dn = 0;
                fulta_arc_err_t xe = fulta_arc_extract_alloc(arc, 0, &d, &dn);
                CHECK(xe == FULTA_ARC_OK && dn == sizeof nobr && d && memcmp(d, nobr, dn) == 0,
                      "7z bcj2 no-branch[%d]: round-trip", j);
                fulta_arc_free(d);
                fulta_arc_close(arc);
            }
            free(m.p);
        }
    }

    /* SOLID_OFF (RFC-0004 §5a): one folder per file. Each file's bytes round-trip, the entries land in distinct
       solid groups (vs one shared group when solid), and the result is editable in place (Rubraview #2). Covered
       across a plain method, a content filter, and BCJ2 — all compose with non-solid. */
    struct { fulta_arc_method_t method; fulta_arc_filter_t filter; const char *tag; } so[] = {
        {FULTA_ARC_METHOD_LZMA2, FULTA_ARC_FILTER_NONE,    "off lzma2"},
        {FULTA_ARC_METHOD_STORE, FULTA_ARC_FILTER_NONE,    "off store"},
        {FULTA_ARC_METHOD_LZMA2, FULTA_ARC_FILTER_BCJ_X86, "off bcj+lzma2"},
        {FULTA_ARC_METHOD_LZMA2, FULTA_ARC_FILTER_BCJ2,    "off bcj2"},
    };
    for (size_t k = 0; k < sizeof so / sizeof *so; k++) {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t o = {.level = 6, .method = so[k].method, .filter = so[k].filter, .solid = FULTA_ARC_SOLID_OFF};
        fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_7Z, en, n, &o, &sink);
        CHECK(e == FULTA_ARC_OK, "7z solid-off %s: write %s", so[k].tag, fulta_arc_strerror(e));
        fulta_arc_t *arc = NULL;
        if (!e) e = fulta_arc_open_memory(m.p, m.n, NULL, &arc);
        CHECK(e == FULTA_ARC_OK && arc, "7z solid-off %s: open", so[k].tag);
        if (!e && arc) {
            CHECK(fulta_arc_editable(arc) == FULTA_ARC_EDIT_OK, "7z solid-off %s: editable in place", so[k].tag);
            uint32_t g0 = 0, g2 = 0; int seen0 = 0, seen2 = 0;
            for (size_t i = 0; i < fulta_arc_count(arc) && i < n; i++) {
                const fulta_arc_entry_t *x = fulta_arc_entry(arc, i);
                void *d = NULL; size_t dn = 0;
                fulta_arc_err_t xe = fulta_arc_extract_alloc(arc, i, &d, &dn);
                CHECK(xe == FULTA_ARC_OK, "7z solid-off %s: extract %s", so[k].tag, fulta_arc_strerror(xe));
                if (!xe && !en[i].is_dir) CHECK(dn == en[i].size && (dn == 0 || memcmp(d, en[i].data, dn) == 0), "7z solid-off %s: %s data", so[k].tag, en[i].name);
                if (i == 0) { g0 = x->solid_group; seen0 = 1; }
                if (i == 2) { g2 = x->solid_group; seen2 = 1; }
                fulta_arc_free(d);
            }
            CHECK(seen0 && seen2 && g0 != g2, "7z solid-off %s: entries in distinct solid groups", so[k].tag);
            fulta_arc_close(arc);
        }
        free(m.p);
    }

    /* validation: impossible method x format = INVALID_ARG; valid-but-not-yet = UNSUPPORTED; filter bounds */
    struct { fulta_arc_format_t fmt; fulta_arc_method_t method; fulta_arc_filter_t filter; uint32_t dist; fulta_arc_err_t want; const char *tag; } bad2[] = {
        {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_METHOD_LZMA2, 0, 0, FULTA_ARC_ERR_INVALID_ARG,  "zip+lzma2 invalid"},
        {FULTA_ARC_FORMAT_7Z,  FULTA_ARC_METHOD_ZSTD,  0, 0, FULTA_ARC_ERR_INVALID_ARG,  "7z+zstd invalid"},
        {FULTA_ARC_FORMAT_7Z,  FULTA_ARC_METHOD_DEFAULT, FULTA_ARC_FILTER_DELTA, 300, FULTA_ARC_ERR_INVALID_ARG, "7z+delta dist 300 invalid"},
    };
    for (size_t i = 0; i < sizeof bad2 / sizeof *bad2; i++) {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t o = {.level = 6, .method = bad2[i].method, .filter = bad2[i].filter, .filter_delta_dist = bad2[i].dist};
        CHECK(fulta_arc_write(bad2[i].fmt, one, 1, &o, &sink) == bad2[i].want, "%s", bad2[i].tag);
        free(m.p);
    }

    /* BCJ2 + password is refused this increment (per-packed-stream AES is a later step) */
    {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t o = {.level = 6, .filter = FULTA_ARC_FILTER_BCJ2, .password = "pw",
                                       .encryption = FULTA_ARC_ENCRYPT_7Z_AES256};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, one, 1, &o, &sink) == FULTA_ARC_ERR_UNSUPPORTED, "7z bcj2+password -> UNSUPPORTED");
        free(m.p);
    }
    return t_report("test_write");
}
