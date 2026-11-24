/* test_security.c - the 2026-11-10 security review's regressions (D-041, T029), and every public function given
 * hostile parameters.
 *
 *   7z:     EmptyFile/Anti before EmptyStream (was a heap over-read); counts a tiny header cannot hold (were
 *           allocations of hundreds of megabytes before the parse failed)
 *   names:  Windows forms of ".." and drive-relative names on reading; ZIP names over 65535 bytes and unsafe names
 *           on every write route (the edit routes stored them unchecked)
 *   limits: max_dictionary for RAR; ALZ's declared size against what its packed bytes can hold
 *   input:  a gzip name past its 4 KiB cap; pax numbers past 64 bits; a read error inside a .Z stream
 *   life:   a wrapper's decoder that fails to start (its input was destroyed twice)
 *   API:    NULLs, out-of-range indexes, nested calls from a callback, at every public function */
#define _POSIX_C_SOURCE 200809L   /* symlink() for the temporary-name test */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

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
    if (n) memcpy(m->p + m->n, d, n);
    m->n += n;
    return FULTA_ARC_OK;
}
static fulta_arc_err_t m_patch(void *ctx, uint64_t off, const void *d, size_t n) {
    mem_t *m = ctx;
    if (off + n > m->n) return FULTA_ARC_ERR_IO;
    memcpy(m->p + off, d, n);
    return FULTA_ARC_OK;
}
static fulta_arc_err_t null_write(void *ctx, const void *d, size_t n) { (void)ctx; (void)d; (void)n; return FULTA_ARC_OK; }

static void wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void wr32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i)); }
static void wr64(unsigned char *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * i)); }

static uint32_t crc32_of(const unsigned char *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

/* An allocator that records the largest single request, to show a hostile header no longer makes a big one. */
typedef struct peak { size_t max; } peak_t;
static void *pk_alloc(void *ctx, size_t size, size_t align) {
    (void)align;
    peak_t *p = ctx;
    if (size > p->max) p->max = size;
    return malloc(size);
}
static void *pk_realloc(void *ctx, void *ptr, size_t old, size_t size, size_t align) {
    (void)old; (void)align;
    peak_t *p = ctx;
    if (size > p->max) p->max = size;
    if (!size) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static void pk_free(void *ctx, void *ptr) { (void)ctx; free(ptr); }

/* A 7z archive whose (plain) header is `hdr`. */
static mem_t make_7z(const unsigned char *hdr, size_t n) {
    mem_t z = {0};
    unsigned char sh[32] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4};
    wr64(sh + 12, 0);                                  /* the header right after the signature header */
    wr64(sh + 20, n);
    wr32(sh + 28, crc32_of(hdr, n));
    wr32(sh + 8, crc32_of(sh + 12, 20));
    m_write(&z, sh, 32);
    m_write(&z, hdr, n);
    return z;
}

static void test_7z_property_order(void) {
    /* Header, FilesInfo: 40 files; EmptyFile (one byte, sized while no file was known to be empty) before
       EmptyStream (all 40 empty). The EmptyFile array has 1 element; 40 used to be read from it. */
    unsigned char h[] = {0x01, 0x05, 40,
                         0x0F, 1, 0xFF,
                         0x0E, 5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                         0x00, 0x00};
    mem_t z = make_7z(h, sizeof h);
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, NULL, &a);
    CHECK(e == FULTA_ARC_OK && fulta_arc_count(a) == 40, "7z EmptyFile before EmptyStream: open %d", (int)e);
    size_t dirs = 0;
    for (size_t i = 0; i < fulta_arc_count(a); i++)
        dirs += (fulta_arc_entry(a, i)->flags & FULTA_ARC_ENTRY_DIR) != 0;
    CHECK(dirs == 40, "bits past EmptyFile's own length read as not set (directories): %zu", dirs);
    fulta_arc_close(a);
    free(z.p);
}

static void test_7z_counts(void) {
    static const struct { const char *what; unsigned char h[16]; size_t n; } c[] = {
        /* MainStreamsInfo, PackInfo: pack_pos 0, 2^24 - 1 streams in a dozen bytes */
        {"pack streams", {0x01, 0x04, 0x06, 0x00, 0xE0, 0xFF, 0xFF, 0xFF, 0x09, 0x00, 0x00, 0x00}, 12},
        /* UnpackInfo, Folder: 2^24 - 1 folders */
        {"folders", {0x01, 0x04, 0x07, 0x0B, 0xE0, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00}, 11},
        /* FilesInfo: 2^24 - 1 files and nothing to describe them */
        {"files", {0x01, 0x05, 0xE0, 0xFF, 0xFF, 0xFF, 0x00, 0x00}, 8},
    };
    for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
        mem_t z = make_7z(c[k].h, c[k].n);
        peak_t pk = {0};
        fulta_arc_options_t o = {.allocator = {pk_alloc, pk_realloc, pk_free, &pk}};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, &o, &a);
        CHECK(e == FULTA_ARC_ERR_CORRUPT, "7z %s beyond the header: %d", c[k].what, (int)e);
        CHECK(pk.max < (1u << 20), "7z %s: largest allocation %zu bytes (a dozen-byte header)", c[k].what, pk.max);
        fulta_arc_close(a);
        free(z.p);
    }
}

/* A stored ZIP of named entries ("x" each), to read the names back as the reader makes them safe. */
static mem_t zip_named(const char *const *names, size_t count) {
    mem_t z = {0};
    size_t *off = calloc(count, sizeof *off);
    uint32_t crc = crc32_of((const unsigned char *)"x", 1);
    for (size_t i = 0; i < count; i++) {
        unsigned char h[30] = {0};
        size_t nl = strlen(names[i]);
        off[i] = z.n;
        wr32(h, 0x04034b50); wr16(h + 4, 20); wr32(h + 14, crc); wr32(h + 18, 1); wr32(h + 22, 1); wr16(h + 26, (unsigned)nl);
        m_write(&z, h, 30); m_write(&z, names[i], nl); m_write(&z, "x", 1);
    }
    size_t cd = z.n;
    for (size_t i = 0; i < count; i++) {
        unsigned char h[46] = {0};
        size_t nl = strlen(names[i]);
        wr32(h, 0x02014b50); wr16(h + 4, 20); wr16(h + 6, 20); wr32(h + 16, crc); wr32(h + 20, 1); wr32(h + 24, 1);
        wr16(h + 28, (unsigned)nl); wr32(h + 42, (uint32_t)off[i]);
        m_write(&z, h, 46); m_write(&z, names[i], nl);
    }
    unsigned char e[22] = {0};
    wr32(e, 0x06054b50); wr16(e + 8, (unsigned)count); wr16(e + 10, (unsigned)count);
    wr32(e + 12, (uint32_t)(z.n - cd)); wr32(e + 16, (uint32_t)cd);
    m_write(&z, e, 22);
    free(off);
    return z;
}

static void test_reader_names(void) {
    static const struct { const char *stored, *want; } c[] = {
        {"a/b.txt", "a/b.txt"},
        {"../evil", "_"},
        {"a/../../evil", "_"},
        {"a/.. /evil", "_"},                           /* Windows trims the space: ".." */
        {"a/../evil", "_"},
        {"a\\..\\evil", "_"},
        {"a/. ./evil", "_"},
        {"a/.../evil", "_"},                           /* trims to nothing: refused with the others */
        {"/etc/passwd", "etc/passwd"},
        {"C:/Windows/x", "Windows/x"},
        {"C:foo/x", "foo/x"},                          /* drive-relative on Windows */
        {"a/./b", "a/b"},
        {"a//b", "a/b"},
        {"a/b./c", "a/b./c"},                          /* a trailing dot on a real name is kept */
        {"a/C:x", "a/C:x"},                            /* only a leading drive is one */
        {"//.\\a:x", "x"},                            /* CBMC's find: empty and "." parts before the drive */
        {"C:D:x", "x"},                                /* drive after drive */
        {"a\nb\x1b[31m.txt", "a_b_[31m.txt"},         /* control bytes (C0, DEL) become '_' (D-045) */
        {"\x01/x\x7f", "_/x_"},
        {"\x01..", "_.."},                             /* not a dots-only part once a control byte is in it */
    };
    const char *names[sizeof c / sizeof c[0]];
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) names[i] = c[i].stored;
    mem_t z = zip_named(names, sizeof c / sizeof c[0]);
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, NULL, &a);
    CHECK(e == FULTA_ARC_OK && fulta_arc_count(a) == sizeof c / sizeof c[0], "names zip: %d", (int)e);
    for (size_t i = 0; a && i < fulta_arc_count(a); i++) {
        const fulta_arc_entry_t *en = fulta_arc_entry(a, i);
        CHECK(strcmp(en->name, c[i].want) == 0, "stored \"%s\": read as \"%s\", want \"%s\"", c[i].stored, en->name,
              c[i].want);
    }
    fulta_arc_close(a);
    free(z.p);
}

static fulta_arc_err_t one_read(void *ctx, void *buf, size_t cap, size_t *got) {
    int *done = ctx;
    *got = 0;
    if (*done || !cap) return FULTA_ARC_OK;
    ((char *)buf)[0] = 'y';
    *got = 1;
    *done = 1;
    return FULTA_ARC_OK;
}

static void test_writer_names(void) {
    /* fulta_arc_write: a name too long for ZIP's 16-bit field is refused, not cut */
    size_t L = 70000;
    char *longname = malloc(L + 1);
    for (size_t i = 0; i < L; i++) longname[i] = (i % 100 == 99) ? '/' : 'a';
    longname[L] = 0;
    mem_t z = {0};
    fulta_arc_write_sink_t s = {.ctx = &z, .write = m_write, .patch = m_patch};
    fulta_arc_write_entry_t we[] = {{.name = longname, .data = "x", .size = 1}};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, we, 1, NULL, &s) == FULTA_ARC_ERR_BAD_NAME, "ZIP name of 70000 bytes");
    free(z.p);
    z = (mem_t){0};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, we, 1, NULL, &s) == FULTA_ARC_OK, "7z takes a long name");
    free(z.p);
    static const char *const bad[] = {"../x", "a/../x", "a/.. /x", "/abs", "C:x", "a//b", "a\nb", "", "."};
    for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
        z = (mem_t){0};
        fulta_arc_write_entry_t b[] = {{.name = bad[k], .data = "x", .size = 1}};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, b, 1, NULL, &s) == FULTA_ARC_ERR_BAD_NAME, "write \"%s\"", bad[k]);
        free(z.p);
    }

    /* the edit routes: the same names refused at add and at rename; a long name refused at write */
    const char *ok[] = {"keep.txt"};
    mem_t src = zip_named(ok, 1);
    fulta_arc_t *a = NULL;
    CHECK(fulta_arc_open_memory(src.p, src.n, NULL, &a) == FULTA_ARC_OK, "edit source");
    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_edit_begin(a, &ed) == FULTA_ARC_OK, "edit begin");
    int done = 0;
    fulta_arc_reader_t rd = {.ctx = &done, .read = one_read, .size = 1};
    for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
        CHECK(fulta_arc_edit_add(ed, bad[k], &rd) == FULTA_ARC_ERR_BAD_NAME, "edit add \"%s\"", bad[k]);
        CHECK(fulta_arc_edit_replace(ed, 0, bad[k], NULL) == FULTA_ARC_ERR_BAD_NAME, "edit rename to \"%s\"", bad[k]);
    }
    CHECK(fulta_arc_edit_add(ed, longname, &rd) == FULTA_ARC_OK, "a long name is a valid name");
    z = (mem_t){0};
    s.ctx = &z;
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &s, NULL) == FULTA_ARC_ERR_BAD_NAME, "edit write: long ZIP name");
    free(z.p);
    fulta_arc_edit_free(ed);
    fulta_arc_close(a);
    free(src.p);
    free(longname);
}

static bool fixture(const char *rel, char *path, size_t cap) {
    snprintf(path, cap, "%s/%s", t_fixtures(), rel);
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("%s not present", rel); return false; }
    fclose(f);
    return true;
}

static void test_rar_dictionary(void) {
    char path[1024];
    if (!fixture("rar/rar2_m1.rar", path, sizeof path)) return;
    fulta_arc_options_t o = {.max_dictionary = 4096};  /* below every RAR dictionary */
    fulta_arc_t *a = NULL;
    if (fulta_arc_open_file(path, &o, &a) != FULTA_ARC_OK) { SKIP("%s did not open", path); return; }
    fulta_arc_sink_t s = {.write = null_write};
    size_t i = 0;
    while (i < fulta_arc_count(a) && (fulta_arc_entry(a, i)->flags & FULTA_ARC_ENTRY_DIR)) i++;
    fulta_arc_err_t e = fulta_arc_extract(a, i, &s);
    CHECK(e == FULTA_ARC_ERR_LIMIT, "RAR: max_dictionary 4 KiB refuses the window (%d)", (int)e);
    fulta_arc_close(a);
    CHECK(fulta_arc_open_file(path, NULL, &a) == FULTA_ARC_OK, "RAR reopen");
    CHECK(fulta_arc_extract(a, i, &s) == FULTA_ARC_OK, "RAR: the default 1 GiB lets it through");
    fulta_arc_close(a);
}

/* The fuzzer's find (2026-11-10): 144 bytes of ALZ whose entry declares 4 GiB, which was allocated whole. */
static void test_alz_declared_size(void) {
    char path[1024];
    if (!fixture("regression/alz_declared_4g.alz", path, sizeof path)) return;
    peak_t pk = {0};
    fulta_arc_options_t o = {.allocator = {pk_alloc, pk_realloc, pk_free, &pk}};
    fulta_arc_t *a = NULL;
    if (fulta_arc_open_file(path, &o, &a) != FULTA_ARC_OK) { SKIP("regression ALZ did not open"); return; }
    fulta_arc_sink_t s = {.write = null_write};
    for (size_t i = 0; i < fulta_arc_count(a); i++) (void)fulta_arc_extract(a, i, &s);
    CHECK(pk.max < (64u << 20), "ALZ: largest allocation %zu bytes for a 144-byte archive", pk.max);
    fulta_arc_close(a);
}

/* gzip: an FNAME longer than the 4 KiB kept used to make the scan skip a byte per byte past the cap (and could step
   over the NUL); the payload must start right after it. */
static void test_gzip_long_name(void) {
    mem_t z = {0};
    unsigned char h[10] = {0x1F, 0x8B, 8, 0x08, 0, 0, 0, 0, 0, 3};   /* FNAME */
    m_write(&z, h, 10);
    for (int i = 0; i < 5001; i++) m_write(&z, "a", 1);              /* odd: the old double step overran the NUL */
    m_write(&z, "", 1);
    static const unsigned char blk[] = {0x01, 0x05, 0x00, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o'};   /* stored */
    m_write(&z, blk, sizeof blk);
    unsigned char t[8];
    wr32(t, crc32_of((const unsigned char *)"hello", 5));
    wr32(t + 4, 5);
    m_write(&z, t, 8);
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, NULL, &a);
    CHECK(e == FULTA_ARC_OK && fulta_arc_count(a) == 1, "gzip with a 5001-byte name: open %d", (int)e);
    if (a) {
        CHECK(strlen(fulta_arc_entry(a, 0)->name) == 4096, "the name kept to 4096 bytes");
        void *d = NULL;
        size_t dn = 0;
        e = fulta_arc_extract_alloc(a, 0, &d, &dn);
        CHECK(e == FULTA_ARC_OK && dn == 5 && memcmp(d, "hello", 5) == 0, "payload after the long name: %d", (int)e);
        fulta_arc_free(d);
    }
    fulta_arc_close(a);
    free(z.p);
}

static void tar_header(unsigned char h[512], const char *name, char type, uint64_t size) {
    memset(h, 0, 512);
    snprintf((char *)h, 100, "%s", name);
    memcpy(h + 100, "0000644", 8);
    char sz[24];
    snprintf(sz, sizeof sz, "%011llo", (unsigned long long)(size & 07777777777ull));   /* 11 octal digits */
    memcpy(h + 124, sz, 11);
    memcpy(h + 136, "00000000000", 12);
    h[156] = (unsigned char)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    snprintf((char *)h + 148, 8, "%06o", sum);
}

/* pax: a time and a size past 64 bits are saturated, not wrapped (signed overflow under UBSan before). */
static void test_pax_numbers(void) {
    const char *rec[] = {"40 mtime=999999999999999999999999999999\n", "41 mtime=-999999999999999999999999999999\n"};
    for (int k = 0; k < 2; k++) {
        mem_t z = {0};
        unsigned char h[512], d[512];
        size_t rl = strlen(rec[k]);
        tar_header(h, "pax", 'x', rl);
        m_write(&z, h, 512);
        memset(d, 0, 512);
        memcpy(d, rec[k], rl);
        m_write(&z, d, 512);
        tar_header(h, "f.txt", '0', 1);
        m_write(&z, h, 512);
        memset(d, 0, 512);
        d[0] = 'x';
        m_write(&z, d, 512);
        memset(d, 0, 512);
        m_write(&z, d, 512);
        m_write(&z, d, 512);
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, NULL, &a);
        CHECK(e == FULTA_ARC_OK && fulta_arc_count(a) == 1, "pax record %d: open %d", k, (int)e);
        if (a) {
            int64_t mt = fulta_arc_entry(a, 0)->mtime;
            CHECK(k == 0 ? mt > 0 : mt < 0, "pax time %d saturates on its own side (%lld)", k, (long long)mt);
        }
        fulta_arc_close(a);
        free(z.p);
    }
}

/* A source that fails with an I/O error past `limit` bytes. */
typedef struct failing { const unsigned char *p; size_t n, limit; } failing_t;
static fulta_arc_err_t fail_read(void *ctx, uint64_t off, void *buf, size_t n, size_t *got) {
    failing_t *f = ctx;
    *got = 0;
    if (off >= f->limit) return FULTA_ARC_ERR_IO;   /* the bytes before `limit` come, then the error */
    if (n > f->limit - off) n = (size_t)(f->limit - off);
    if (off >= f->n) return FULTA_ARC_OK;
    if (n > f->n - off) n = (size_t)(f->n - off);
    memcpy(buf, f->p + off, n);
    *got = n;
    return FULTA_ARC_OK;
}

/* Unix compress: a read error of the input is reported, not taken for the end of the data. */
static void test_compress_read_error(void) {
    char path[1024];
    if (!fixture("compress/text.Z", path, sizeof path)) return;
    size_t n = 0;
    unsigned char *b = t_read_file(path, &n);
    if (!b || n < 200) { free(b); SKIP("compress/text.Z too small"); return; }
    failing_t f = {b, n, n};
    fulta_arc_source_t src = {.ctx = &f, .size = n, .read_at = fail_read};
    fulta_arc_t *a = NULL;
    fulta_arc_options_t o = {.name = "text.Z"};
    CHECK(fulta_arc_open(&src, &o, &a) == FULTA_ARC_OK, "open text.Z");
    if (a) {
        fulta_arc_sink_t s = {.write = null_write};
        CHECK(fulta_arc_extract(a, 0, &s) == FULTA_ARC_OK, "text.Z whole");
        f.limit = 2;                                   /* inside the header: the error, not "not an archive" */
        CHECK(fulta_arc_extract(a, 0, &s) == FULTA_ARC_ERR_IO, "a read error in the header is IO");
        f.limit = n / 2;                               /* from here the source fails half way */
        fulta_arc_err_t e = fulta_arc_extract(a, 0, &s);
        CHECK(e == FULTA_ARC_ERR_IO, "a read error half way is IO, not a short success (%d)", (int)e);
    }
    fulta_arc_close(a);
    free(b);
}

/* gzip/bzip2/zstd/lzip/compress: a decoder that fails to start destroys its input itself (the decoders own it); the
   wrapper readers destroyed it a second time. A budget too small for the decoder's state makes it fail to start. */
static void test_wrapper_start_failure(void) {
    static const unsigned char ZS[] = {40,181,47,253,96,212,2,101,1,0,52,2,70,117,108,116,97,65,114,99,32,120,122,47,
        122,115,116,100,32,115,116,114,101,97,109,32,116,101,115,116,32,108,105,110,101,46,32,1,0,115,109,42,245,4};
    fulta_arc_options_t o = {.max_memory = 256, .name = "blob.zst"};
    fulta_arc_t *a = NULL;
    fulta_arc_sink_t s = {.write = null_write};
    CHECK(fulta_arc_open_memory(ZS, sizeof ZS, &o, &a) == FULTA_ARC_OK, "zstd open");
    if (a) CHECK(fulta_arc_extract(a, 0, &s) == FULTA_ARC_ERR_LIMIT, "zstd under a 256-byte budget: LIMIT");
    fulta_arc_close(a);
    static const char *const rel[] = {"tar/gnu.tar.bz2", "lzip/small.lz"};
    for (size_t k = 0; k < 2; k++) {
        char path[1024];
        if (!fixture(rel[k], path, sizeof path)) continue;
        fulta_arc_options_t ob = {.max_memory = 256};
        a = NULL;
        CHECK(fulta_arc_open_file(path, &ob, &a) == FULTA_ARC_OK, "%s open", rel[k]);
        if (a) CHECK(fulta_arc_extract(a, 0, &s) == FULTA_ARC_ERR_LIMIT, "%s under a 256-byte budget: LIMIT", rel[k]);
        fulta_arc_close(a);
    }
}

/* CAB caches decoded folders on the archive's budget (D-041): under a max_memory that holds one folder but not all,
   the cache gives way and every entry still extracts, in any order. */
static void test_cab_budget(void) {
    static const char *const rel[] = {"cab/lzxbig.cab", "cab/mset1.cab", "cab/qset1.cab", "cab/lset1.cab", "cab/basic.cab",
                                      "cab/lzx.cab", "cab/next.cab"};
    int tested = 0;
    for (size_t k = 0; k < sizeof rel / sizeof *rel; k++) {
        char path[1024];
        if (!fixture(rel[k], path, sizeof path)) continue;
        fulta_arc_t *a = NULL;
        if (fulta_arc_open_file(path, NULL, &a) != FULTA_ARC_OK) continue;
        enum { MAXF = 64 };
        uint64_t fsize[MAXF] = {0};
        uint32_t fid[MAXF];
        int nf = 0;
        for (size_t i = 0; i < fulta_arc_count(a); i++) {
            const fulta_arc_entry_t *e = fulta_arc_entry(a, i);
            int j = 0;
            while (j < nf && fid[j] != e->solid_group) j++;
            if (j == nf && nf < MAXF) { fid[nf] = e->solid_group; nf++; }
            if (j < MAXF) fsize[j] += e->size;
        }
        fulta_arc_close(a);
        uint64_t big = 0;
        for (int j = 0; j < nf; j++) if (fsize[j] > big) big = fsize[j];
        if (nf < 2) continue;                          /* one folder: nothing to give way */
        fulta_arc_options_t o = {.max_memory = big + 4096};   /* one folder and the budget's block header */
        a = NULL;
        CHECK(fulta_arc_open_file(path, &o, &a) == FULTA_ARC_OK, "%s reopen", rel[k]);
        fulta_arc_sink_t s = {.write = null_write};
        int bad = 0;
        for (int round = 0; round < 2 && a; round++)
            for (size_t i = fulta_arc_count(a); i-- > 0;)
                if (fulta_arc_extract(a, i, &s) != FULTA_ARC_OK) bad++;
        CHECK(bad == 0, "%s (%d folders) under max_memory %llu: %d extractions failed", rel[k], nf,
              (unsigned long long)o.max_memory, bad);
        fulta_arc_close(a);
        tested++;
    }
    if (!tested) SKIP("no CAB fixture with two folders");
}

/* ALZ entries and AZO chunk buffers are on the budget: a 1 KiB max_memory refuses them (LIMIT, not NOMEM). */
static void test_budget_alz_azo(void) {
    static const char *const rel[] = {"alz/alzip/a1237_deflate.alz", "alzegg/b004_m3in__o86__half_016.egg"};
    for (size_t k = 0; k < 2; k++) {
        char path[1024];
        if (!fixture(rel[k], path, sizeof path)) continue;
        fulta_arc_options_t o = {.max_memory = 1024};
        fulta_arc_t *a = NULL;
        if (fulta_arc_open_file(path, &o, &a) != FULTA_ARC_OK) { SKIP("%s did not open", rel[k]); continue; }
        fulta_arc_sink_t s = {.write = null_write};
        size_t i = 0;
        while (i < fulta_arc_count(a) && fulta_arc_entry(a, i)->size < 2048) i++;
        if (i < fulta_arc_count(a)) {
            fulta_arc_err_t e = fulta_arc_extract(a, i, &s);
            CHECK(e == FULTA_ARC_ERR_LIMIT, "%s under 1 KiB: LIMIT (%s)", rel[k], fulta_arc_strerror(e));
        } else SKIP("%s: no entry of 2 KiB or more", rel[k]);
        fulta_arc_close(a);
        a = NULL;
        CHECK(fulta_arc_open_file(path, NULL, &a) == FULTA_ARC_OK && fulta_arc_extract(a, i < fulta_arc_count(a) ? i : 0, &s) == FULTA_ARC_OK,
              "%s unlimited: OK", rel[k]);
        fulta_arc_close(a);
    }
}

/* The edit's temporary file has a random name (D-041): a link planted under the old fixed name `<path>.fatmp`
   is not followed, and nothing is left beside the output. */
static void test_temp_name(void) {
#if defined(_WIN32)
    SKIP("symlinks: POSIX only");
#else
    const char *ok[] = {"keep.txt"};
    mem_t src = zip_named(ok, 1);
    char dir[] = "build/host/tests/tmpname";
    char out[256], planted[256], victim[256];
    snprintf(out, sizeof out, "%s.zip", dir);
    snprintf(planted, sizeof planted, "%s.zip.fatmp", dir);
    snprintf(victim, sizeof victim, "%s.victim", dir);
    FILE *v = fopen(victim, "wb");
    if (!v) { SKIP("cannot write %s", victim); free(src.p); return; }
    fputs("precious", v);
    fclose(v);
    (void)unlink(planted);
    if (symlink("tmpname.victim", planted) != 0) { SKIP("symlink failed"); free(src.p); return; }
    fulta_arc_t *a = NULL;
    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_open_memory(src.p, src.n, NULL, &a) == FULTA_ARC_OK && fulta_arc_edit_begin(a, &ed) == FULTA_ARC_OK, "edit");
    CHECK(fulta_arc_edit_write_file(ed, FULTA_ARC_FORMAT_ZIP, out, NULL) == FULTA_ARC_OK, "edit_write_file");
    size_t n = 0;
    unsigned char *b = t_read_file(victim, &n);
    CHECK(b && n == 8 && memcmp(b, "precious", 8) == 0, "the link's target is untouched");
    free(b);
    b = t_read_file(out, &n);
    CHECK(b && n > 22 && b[0] == 'P' && b[1] == 'K', "the output was written");
    free(b);
    fulta_arc_edit_free(ed);
    fulta_arc_close(a);
    (void)unlink(planted); (void)unlink(victim); (void)unlink(out);
    free(src.p);
#endif
}

/* A sink that tries to extract from the archive it is being fed by. */
typedef struct nest { fulta_arc_t *arc; fulta_arc_err_t got; int calls; } nest_t;
static fulta_arc_err_t nest_write(void *ctx, const void *d, size_t n) {
    (void)d; (void)n;
    nest_t *t = ctx;
    if (t->calls++ == 0) {
        fulta_arc_sink_t s = {.write = null_write};
        t->got = fulta_arc_extract(t->arc, 0, &s);
        fulta_arc_err_t c = fulta_arc_check_password(t->arc, 0);
        if (c != FULTA_ARC_ERR_INVALID_ARG && c != FULTA_ARC_ERR_UNSUPPORTED) t->got = c;
    }
    return FULTA_ARC_OK;
}

static void test_api_params(void) {
    fulta_arc_t *a = (fulta_arc_t *)0x1;               /* overwritten with NULL on every failure */
    fulta_arc_sink_t s = {.write = null_write};
    CHECK(fulta_arc_open(NULL, NULL, &a) == FULTA_ARC_ERR_INVALID_ARG, "open(NULL source)");
    fulta_arc_source_t nosrc = {0};
    CHECK(fulta_arc_open(&nosrc, NULL, &a) == FULTA_ARC_ERR_INVALID_ARG && a == NULL, "open(no read_at)");
    CHECK(fulta_arc_open_file(NULL, NULL, &a) == FULTA_ARC_ERR_INVALID_ARG, "open_file(NULL)");
    CHECK(fulta_arc_open_file("x", NULL, NULL) == FULTA_ARC_ERR_INVALID_ARG, "open_file(out NULL)");
    CHECK(fulta_arc_open_memory(NULL, 5, NULL, &a) == FULTA_ARC_ERR_INVALID_ARG, "open_memory(NULL, 5)");
    CHECK(fulta_arc_open_memory(NULL, 0, NULL, &a) == FULTA_ARC_ERR_NOT_ARCHIVE && a == NULL, "open_memory(NULL, 0)");
    CHECK(fulta_arc_open_memory("PK\5\6", 4, NULL, &a) != FULTA_ARC_OK && a == NULL, "a 4-byte ZIP");
    fulta_arc_options_t half = {.allocator = {.alloc = pk_alloc}};
    const char *ok[] = {"f.txt"};
    mem_t z = zip_named(ok, 1);
    CHECK(fulta_arc_open_memory(z.p, z.n, &half, &a) == FULTA_ARC_ERR_INVALID_ARG, "a partial allocator");
    CHECK(fulta_arc_source_file(NULL, &nosrc) == FULTA_ARC_ERR_INVALID_ARG, "source_file(NULL)");

    CHECK(fulta_arc_count(NULL) == 0 && fulta_arc_entry(NULL, 0) == NULL, "count/entry(NULL)");
    CHECK(fulta_arc_format(NULL) == FULTA_ARC_FORMAT_UNKNOWN, "format(NULL)");
    CHECK(fulta_arc_extract(NULL, 0, &s) == FULTA_ARC_ERR_INVALID_ARG, "extract(NULL)");
    CHECK(fulta_arc_check_password(NULL, 0) == FULTA_ARC_ERR_INVALID_ARG, "check_password(NULL)");
    CHECK(fulta_arc_editable(NULL) == FULTA_ARC_EDIT_FORMAT_NOT_WRITABLE, "editable(NULL)");
    void *d = (void *)0x1;
    size_t dn = 7;
    CHECK(fulta_arc_extract_alloc(NULL, 0, &d, &dn) == FULTA_ARC_ERR_INVALID_ARG, "extract_alloc(NULL arc)");
    fulta_arc_close(NULL);
    fulta_arc_free(NULL);
    fulta_arc_edit_free(NULL);
    CHECK(fulta_arc_strerror((fulta_arc_err_t)0x7FFF) != NULL && fulta_arc_format_name((fulta_arc_format_t)999) != NULL,
          "strerror/format_name of unknown values");

    CHECK(fulta_arc_open_memory(z.p, z.n, NULL, &a) == FULTA_ARC_OK, "open the ZIP");
    size_t n = fulta_arc_count(a);
    CHECK(fulta_arc_entry(a, n) == NULL && fulta_arc_entry(a, SIZE_MAX) == NULL, "entry out of range");
    CHECK(fulta_arc_extract(a, n, &s) == FULTA_ARC_ERR_INVALID_ARG, "extract out of range");
    CHECK(fulta_arc_extract(a, SIZE_MAX, &s) == FULTA_ARC_ERR_INVALID_ARG, "extract SIZE_MAX");
    CHECK(fulta_arc_extract(a, 0, NULL) == FULTA_ARC_ERR_INVALID_ARG, "extract(NULL sink)");
    fulta_arc_sink_t nos = {0};
    CHECK(fulta_arc_extract(a, 0, &nos) == FULTA_ARC_ERR_INVALID_ARG, "extract(sink without write)");
    CHECK(fulta_arc_check_password(a, n) == FULTA_ARC_ERR_INVALID_ARG, "check_password out of range");
    CHECK(fulta_arc_extract_alloc(a, 0, NULL, &dn) == FULTA_ARC_ERR_INVALID_ARG, "extract_alloc(NULL out)");
    CHECK(fulta_arc_extract_alloc(a, n, &d, &dn) == FULTA_ARC_ERR_INVALID_ARG && d == NULL && dn == 0,
          "extract_alloc out of range clears its outputs");

    nest_t t = {.arc = a, .got = FULTA_ARC_OK};
    fulta_arc_sink_t ns = {.ctx = &t, .write = nest_write};
    CHECK(fulta_arc_extract(a, 0, &ns) == FULTA_ARC_OK && t.got == FULTA_ARC_ERR_INVALID_ARG,
          "a nested extract from a sink is refused (%d)", (int)t.got);
    CHECK(fulta_arc_extract(a, 0, &s) == FULTA_ARC_OK, "and the archive works after it");

    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_edit_begin(NULL, &ed) == FULTA_ARC_ERR_INVALID_ARG, "edit_begin(NULL)");
    CHECK(fulta_arc_edit_begin(a, NULL) == FULTA_ARC_ERR_INVALID_ARG, "edit_begin(out NULL)");
    CHECK(fulta_arc_edit_begin(a, &ed) == FULTA_ARC_OK, "edit_begin");
    CHECK(fulta_arc_edit_remove(NULL, 0) == FULTA_ARC_ERR_INVALID_ARG, "edit_remove(NULL)");
    CHECK(fulta_arc_edit_remove(ed, n) == FULTA_ARC_ERR_INVALID_ARG, "edit_remove out of range");
    CHECK(fulta_arc_edit_remove(ed, 0) == FULTA_ARC_OK, "edit_remove");
    CHECK(fulta_arc_edit_remove(ed, 0) == FULTA_ARC_ERR_INVALID_ARG, "edit_remove twice");
    CHECK(fulta_arc_edit_replace(ed, 0, "n.txt", NULL) == FULTA_ARC_ERR_INVALID_ARG, "replace a removed entry");
    CHECK(fulta_arc_edit_replace(ed, SIZE_MAX, "n.txt", NULL) == FULTA_ARC_ERR_INVALID_ARG, "replace SIZE_MAX");
    CHECK(fulta_arc_edit_add(ed, NULL, NULL) == FULTA_ARC_ERR_INVALID_ARG, "edit_add(NULL name)");
    CHECK(fulta_arc_edit_add(NULL, "a", NULL) == FULTA_ARC_ERR_INVALID_ARG, "edit_add(NULL edit)");
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, NULL, NULL) == FULTA_ARC_ERR_INVALID_ARG, "edit_write(NULL sink)");
    mem_t out = {0};
    fulta_arc_write_sink_t ws = {.ctx = &out, .write = m_write, .patch = m_patch};
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_RAR, &ws, NULL) == FULTA_ARC_ERR_INVALID_ARG, "edit_write as RAR");
    fulta_arc_write_sink_t nopatch = {.ctx = &out, .write = m_write};
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &nopatch, NULL) == FULTA_ARC_ERR_INVALID_ARG, "sink without patch");
    CHECK(fulta_arc_edit_write_file(ed, FULTA_ARC_FORMAT_ZIP, NULL, NULL) == FULTA_ARC_ERR_INVALID_ARG, "edit_write_file(NULL)");
    CHECK(fulta_arc_edit_write_volumes(ed, FULTA_ARC_FORMAT_ZIP, NULL, NULL) == FULTA_ARC_ERR_INVALID_ARG, "edit_write_volumes(NULL)");
    fulta_arc_edit_free(ed);
    free(out.p);

    fulta_arc_write_entry_t we[] = {{.name = "a", .data = "x", .size = 1}};
    out = (mem_t){0};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, NULL, 1, NULL, &ws) == FULTA_ARC_ERR_INVALID_ARG, "write(NULL entries, 1)");
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, we, 1, NULL, NULL) == FULTA_ARC_ERR_INVALID_ARG, "write(NULL sink)");
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_CAB, we, 1, NULL, &ws) == FULTA_ARC_ERR_INVALID_ARG, "write as CAB");
    fulta_arc_write_entry_t nodata[] = {{.name = "a", .data = NULL, .size = 5}};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, nodata, 1, NULL, &ws) == FULTA_ARC_ERR_INVALID_ARG, "size without data");
    fulta_arc_write_entry_t noname[] = {{.name = NULL, .data = "x", .size = 1}};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, noname, 1, NULL, &ws) == FULTA_ARC_ERR_INVALID_ARG, "NULL name");
    fulta_arc_write_options_t bad_level = {.level = 1000};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, we, 1, &bad_level, &ws) == FULTA_ARC_OK, "an out-of-range level is 6");
    free(out.p);
    out = (mem_t){0};
    fulta_arc_write_options_t bad_method = {.method = (fulta_arc_method_t)99};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, we, 1, &bad_method, &ws) == FULTA_ARC_ERR_INVALID_ARG, "unknown method");
    fulta_arc_write_options_t bad_enc = {.password = "p", .encryption = (fulta_arc_encrypt_t)99};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, we, 1, &bad_enc, &ws) == FULTA_ARC_ERR_INVALID_ARG, "unknown encryption");
    fulta_arc_write_options_t bad_filter = {.filter = FULTA_ARC_FILTER_DELTA, .filter_delta_dist = 300};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, we, 1, &bad_filter, &ws) == FULTA_ARC_ERR_INVALID_ARG, "delta 300");
    fulta_arc_write_options_t vol = {.volume_size = 1};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, we, 1, &vol, &ws) == FULTA_ARC_ERR_INVALID_ARG, "volume_size on one sink");
    CHECK(fulta_arc_write_volumes(FULTA_ARC_FORMAT_ZIP, we, 1, NULL, NULL) == FULTA_ARC_ERR_INVALID_ARG, "write_volumes(NULL)");
    CHECK(fulta_arc_write_file(NULL, FULTA_ARC_FORMAT_ZIP, we, 1, NULL) == FULTA_ARC_ERR_INVALID_ARG, "write_file(NULL)");
    free(out.p);

    char nb[8];
    CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_ZIP, NULL, 0, 1, nb, sizeof nb) == FULTA_ARC_ERR_INVALID_ARG, "volume_name(NULL)");
    CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_ZIP, "a.zip", 1, 1, nb, sizeof nb) == FULTA_ARC_ERR_INVALID_ARG, "index >= count");
    CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_7Z, "long-name.7z", 0, 2, nb, sizeof nb) == FULTA_ARC_ERR_LIMIT, "small buffer");
    CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_7Z, "a.7z", 0, 2, nb, 0) == FULTA_ARC_ERR_LIMIT, "zero buffer");
    fulta_arc_close(a);
    free(z.p);
}

static int g_pw_count;
static fulta_arc_err_t always_wrong_pw(void *ctx, uint32_t attempt, const char **out) {
    (void)ctx; (void)attempt;
    g_pw_count++;
    *out = "wrong";
    return FULTA_ARC_OK;
}

typedef struct fail_alloc { int left, total; } fail_alloc_t;
static void *fa_test_alloc(void *ctx, size_t size, size_t align) {
    (void)align;
    fail_alloc_t *f = ctx;
    f->total++;
    if (f->left > 0 && --f->left == 0) return NULL;
    return malloc(size);
}
static void *fa_test_realloc(void *ctx, void *ptr, size_t old_s, size_t new_s, size_t align) {
    (void)old_s; (void)align;
    fail_alloc_t *f = ctx;
    f->total++;
    if (f->left > 0 && --f->left == 0) return NULL;
    if (!new_s) { free(ptr); return NULL; }
    return realloc(ptr, new_s);
}
static void fa_test_free(void *ctx, void *ptr) { (void)ctx; free(ptr); }

static void test_gzip_short_probe(void) {
    unsigned char *p = malloc(3);
    p[0] = 0x1F; p[1] = 0x8B; p[2] = 0x08;
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(p, 3, NULL, &a);
    CHECK(e == FULTA_ARC_ERR_NOT_ARCHIVE, "3-byte gzip buffer refused without OOB: %d", (int)e);
    fulta_arc_close(a);
    free(p);
}

static void test_7z_dup_props(void) {
    /* 1 file, 1 EmptyStream (all empty), 2x MTime (0x14), 2x WinAttrib (0x15) */
    static const unsigned char h[] = {
        0x01, 0x05, 1,
        0x0E, 1, 0x80,
        0x14, 10, 1, 0,  0,0,0,0,0,0,0,0,
        0x14, 10, 1, 0,  1,0,0,0,0,0,0,0,
        0x15, 6,  1, 0,  0x20,0,0,0,
        0x15, 6,  1, 0,  0x01,0,0,0,
        0x00, 0x00
    };
    mem_t z = make_7z(h, sizeof h);
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, NULL, &a);
    CHECK(e == FULTA_ARC_OK && fulta_arc_count(a) == 1, "7z duplicate 0x14/0x15 opens cleanly: %d", (int)e);
    fulta_arc_close(a);
    free(z.p);
}

static void test_7z_enc_header_limit(void) {
    /* kEncodedHeader (0x17) describing one 7zAES-encrypted folder whose unpacked header claims 4096 bytes.
       With max_memory = 200, header allocation succeeds, folder_to_memory hits LIMIT, and retry loop must not
       rewrite LIMIT to PASSWORD_WRONG or spam the callback 65 times. */
    static const unsigned char h[] = {
        0x17,
        0x06, 0x00, 0x01, 0x09, 16, 0x00,
        0x07, 0x0B, 0x01, 0x00,
        0x01, 0x04 | 0x20, 0x06, 0xF1, 0x07, 0x01, 0x01, 0x00,
        0x0C, 0x80 | 0x10, 0x00,   /* 4096 in 7z UINT64: 0x80 | 0x10? Wait: 4096 = 0x1000 -> 2 bytes: first=0x80|(0x10), ext=0x00 */
        0x00, 0x00
    };
    /* Wait: prepend 16 dummy encrypted bytes before the encoded header; pack_pos 0 -> at offset 32. */
    mem_t z = {0};
    unsigned char sh[32] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4};
    unsigned char body[16 + sizeof h] = {0};
    memcpy(body + 16, h, sizeof h);
    wr64(sh + 12, 16);
    wr64(sh + 20, sizeof h);
    wr32(sh + 28, crc32_of(h, sizeof h));
    wr32(sh + 8, crc32_of(sh + 12, 20));
    m_write(&z, sh, 32);
    m_write(&z, body, sizeof body);

    g_pw_count = 0;
    fulta_arc_options_t o = {.max_memory = 200, .password = always_wrong_pw};
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, &o, &a);
    CHECK(e == FULTA_ARC_ERR_LIMIT, "7z enc header exceeding max_memory -> LIMIT, got %s", fulta_arc_strerror(e));
    CHECK(g_pw_count == 0, "7z enc header LIMIT does not loop password callback (%d calls)", g_pw_count);
    fulta_arc_close(a);
    free(z.p);
}

static void test_zip_limits_and_pw_cap(void) {
    mem_t z = {0};
    fulta_arc_write_sink_t ws = {.ctx = &z, .write = m_write, .patch = m_patch};
    fulta_arc_write_entry_t we[] = {{.name = "secret.txt", .data = "hello world", .size = 11}};
    fulta_arc_write_options_t wo = {.password = "right", .encryption = FULTA_ARC_ENCRYPT_ZIP_AES128};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, we, 1, &wo, &ws) == FULTA_ARC_OK, "write encrypted zip");

    /* max_memory = 1 must stop oversized central directory allocation */
    fulta_arc_options_t lim_opt = {.max_memory = 1};
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(z.p, z.n, &lim_opt, &a);
    CHECK(e == FULTA_ARC_ERR_LIMIT, "zip CD past max_memory=1 -> LIMIT, got %s", fulta_arc_strerror(e));
    fulta_arc_close(a);

    /* extract with endlessly-failing password callback must terminate cleanly */
    g_pw_count = 0;
    fulta_arc_options_t pw_opt = {.password = always_wrong_pw};
    a = NULL;
    CHECK(fulta_arc_open_memory(z.p, z.n, &pw_opt, &a) == FULTA_ARC_OK, "open encrypted zip");
    if (a) {
        fulta_arc_sink_t s = {.write = null_write};
        e = fulta_arc_extract(a, 0, &s);
        CHECK(e == FULTA_ARC_ERR_PASSWORD_WRONG, "zip extract caps wrong passwords -> PASSWORD_WRONG (%s)",
              fulta_arc_strerror(e));
        CHECK(g_pw_count <= 66, "zip extract bounded password attempts (%d)", g_pw_count);
        fulta_arc_close(a);
    }
    free(z.p);
}

static void test_egg_oom_close(void) {
    /* Minimal valid 1-entry non-solid Store EGG archive */
    mem_t ebuf = {0};
    unsigned char hdr[18];
    wr32(hdr, 0x41474745u); wr16(hdr + 4, 0x0100u); wr32(hdr + 6, 1u); wr32(hdr + 10, 0u); wr32(hdr + 14, 0x08E28222u);
    m_write(&ebuf, hdr, sizeof hdr);
    unsigned char fhdr[16];
    wr32(fhdr, 0x0A8590E3u); wr32(fhdr + 4, 0u); wr64(fhdr + 8, 1u);
    m_write(&ebuf, fhdr, sizeof fhdr);
    unsigned char nhdr[12];
    wr32(nhdr, 0x0A8591ACu); nhdr[4] = 0; wr16(nhdr + 5, 1u); nhdr[7] = 'a'; wr32(nhdr + 8, 0x08E28222u);
    m_write(&ebuf, nhdr, sizeof nhdr);
    unsigned char bhdr[23];
    wr32(bhdr, 0x02B50C13u); bhdr[4] = 0; bhdr[5] = 0;
    wr32(bhdr + 6, 1u); wr32(bhdr + 10, 1u); wr32(bhdr + 14, 0u); wr32(bhdr + 18, 0x08E28222u); bhdr[22] = 'x';
    m_write(&ebuf, bhdr, sizeof bhdr);
    unsigned char end[4];
    wr32(end, 0x08E28222u);
    m_write(&ebuf, end, sizeof end);

    bool ok_seen = false;
    for (int k = 1; k <= 40; k++) {
        fail_alloc_t fa = {.left = k};
        fulta_arc_options_t o = {.allocator = {fa_test_alloc, fa_test_realloc, fa_test_free, &fa}};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t er = fulta_arc_open_memory(ebuf.p, ebuf.n, &o, &a);
        if (er == FULTA_ARC_OK) { ok_seen = true; fulta_arc_close(a); break; }
        CHECK(er == FULTA_ARC_ERR_NOMEM, "egg OOM at alloc #%d -> NOMEM (got %d)", k, (int)er);
        fulta_arc_close(a);
    }
    CHECK(ok_seen, "egg minimal synthetic archive succeeded once past OOM points");
    free(ebuf.p);
}

int main(void) {
    test_7z_property_order();
    test_7z_counts();
    test_reader_names();
    test_writer_names();
    test_rar_dictionary();
    test_alz_declared_size();
    test_gzip_long_name();
    test_pax_numbers();
    test_compress_read_error();
    test_wrapper_start_failure();
    test_cab_budget();
    test_budget_alz_azo();
    test_temp_name();
    test_api_params();
    test_gzip_short_probe();
    test_7z_dup_props();
    test_7z_enc_header_limit();
    test_zip_limits_and_pw_cap();
    test_egg_oom_close();
    return t_report("test_security");
}
