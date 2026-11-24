/* test_time.c - which entry times are local, and the local DOS time a ZIP writer puts down (D-040, T028).
 *
 *   reader: FULTA_ARC_ENTRY_MTIME_LOCAL on a ZIP DOS time with no UTC extra, ALZ, RAR 1.5-4, CAB, EGG, LHA level
 *           0/1; clear on 0x5455, on NTFS 0x000a (which outranks 0x5455 wherever it stands), on 7z, on RAR 5, on LHA
 *           level 2; RAR 4 EXT_TIME's fraction and RAR 5's nanoseconds, against 7z l -slt
 *   writer: the DOS field at the chosen offset (explicit, UTC, the machine's own), 0x5455 still UTC; options checked
 *   edit:   a copied DOS-only entry gains no 0x5455; a converted local entry is turned back into UTC; a copied entry
 *           keeps its NTFS 0x000a times (all three, 100 ns) in both headers */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>
#include <time.h>

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

static unsigned rd16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static void wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void wr32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i)); }

/* Seconds since 1970 of a DOS date and time, counted as if UTC (an independent count from the library's). */
static int64_t dos_secs(unsigned date, unsigned time) {
    int y = 1980 + (int)(date >> 9), m = (int)(date >> 5 & 15), d = (int)(date & 31);
    static const int before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int64_t days = 0;
    for (int k = 1970; k < y; k++) days += (k % 4 == 0 && (k % 100 != 0 || k % 400 == 0)) ? 366 : 365;
    days += before[m - 1] + (m > 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) + d - 1;
    return days * 86400 + (time >> 11) * 3600 + (time >> 5 & 63) * 60 + (time & 31) * 2;
}

static int64_t tm_secs(const struct tm *t) {          /* a broken-down time counted as if UTC */
    unsigned date = (unsigned)(t->tm_year + 1900 - 1980) << 9 | (unsigned)(t->tm_mon + 1) << 5 | (unsigned)t->tm_mday;
    unsigned time = (unsigned)t->tm_hour << 11 | (unsigned)t->tm_min << 5;
    return dos_secs(date, time) + t->tm_sec;
}

/* The DOS field of the first local header and of the first central header; false when one is missing. */
static bool dos_fields(const mem_t *z, int64_t *local_hdr, int64_t *central_hdr) {
    bool lf = false, cf = false;
    for (size_t i = 0; i + 46 <= z->n; i++) {
        if (!lf && !memcmp(z->p + i, "PK\3\4", 4)) { *local_hdr = dos_secs(rd16(z->p + i + 12), rd16(z->p + i + 10)); lf = true; }
        if (!cf && !memcmp(z->p + i, "PK\1\2", 4)) { *central_hdr = dos_secs(rd16(z->p + i + 14), rd16(z->p + i + 12)); cf = true; }
    }
    return lf && cf;
}

static size_t count_tag(const mem_t *z, unsigned tag) {   /* extra-field ids (0x5455 "UT") anywhere in the bytes */
    size_t k = 0;
    for (size_t i = 0; i + 4 <= z->n; i++) if (rd16(z->p + i) == tag && rd16(z->p + i + 2) == 5 && z->p[i + 4] == 1) k++;
    return k;
}

static uint32_t crc32_of(const unsigned char *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

/* One stored entry "t.txt" = "hi", the DOS time given, `extra` in the central header only. */
static mem_t hand_zip(unsigned dtime, unsigned ddate, const unsigned char *extra, unsigned xlen) {
    static const unsigned char data[2] = {'h', 'i'};
    const char *name = "t.txt";
    unsigned nlen = 5;
    uint32_t crc = crc32_of(data, 2);
    mem_t z = {0};
    unsigned char h[46];
    memset(h, 0, sizeof h);
    wr32(h, 0x04034b50); wr16(h + 4, 20); wr16(h + 10, dtime); wr16(h + 12, ddate);
    wr32(h + 14, crc); wr32(h + 18, 2); wr32(h + 22, 2); wr16(h + 26, nlen);
    m_write(&z, h, 30); m_write(&z, name, nlen); m_write(&z, data, 2);
    size_t cd = z.n;
    memset(h, 0, sizeof h);
    wr32(h, 0x02014b50); wr16(h + 4, 20); wr16(h + 6, 20); wr16(h + 12, dtime); wr16(h + 14, ddate);
    wr32(h + 16, crc); wr32(h + 20, 2); wr32(h + 24, 2); wr16(h + 28, nlen); wr16(h + 30, xlen);
    m_write(&z, h, 46); m_write(&z, name, nlen); if (xlen) m_write(&z, extra, xlen);
    size_t cdsize = z.n - cd;
    memset(h, 0, sizeof h);
    wr32(h, 0x06054b50); wr16(h + 8, 1); wr16(h + 10, 1); wr32(h + 12, (uint32_t)cdsize); wr32(h + 16, (uint32_t)cd);
    m_write(&z, h, 22);
    return z;
}

static const fulta_arc_entry_t *open_one(const mem_t *z, fulta_arc_t **arc) {
    *arc = NULL;
    if (fulta_arc_open_memory(z->p, z->n, NULL, arc) != FULTA_ARC_OK) return NULL;
    return fulta_arc_count(*arc) ? fulta_arc_entry(*arc, 0) : NULL;
}

#define T0 INT64_C(1700000000)                     /* 2023-11-14 22:13:20 UTC */

static mem_t write_one(const fulta_arc_write_options_t *o, fulta_arc_err_t *err) {
    mem_t z = {0};
    fulta_arc_write_sink_t s = {.ctx = &z, .write = m_write, .patch = m_patch};
    fulta_arc_write_entry_t e[] = {{.name = "a.txt", .data = "hello", .size = 5, .mtime = T0}};
    *err = fulta_arc_write(FULTA_ARC_FORMAT_ZIP, e, 1, o, &s);
    return z;
}

/* writer: the DOS field at the offset asked for; 0x5455 stays UTC and reads back exactly with the flag clear */
static void test_writer_offsets(void) {
    static const struct { fulta_arc_dos_time_t mode; int32_t off; int64_t want; } c[] = {
        {FULTA_ARC_DOS_TIME_OFFSET, 32400, T0 + 32400},     /* Korea: 2023-11-15 07:13:20 */
        {FULTA_ARC_DOS_TIME_OFFSET, -18000, T0 - 18000},    /* UTC-5 */
        {FULTA_ARC_DOS_TIME_OFFSET, 0, T0},
        {FULTA_ARC_DOS_TIME_UTC, 32400, T0},                /* the offset is ignored outside OFFSET */
    };
    for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
        fulta_arc_write_options_t o = {.level = 6, .dos_time = c[k].mode, .dos_utc_offset = c[k].off};
        fulta_arc_err_t err;
        mem_t z = write_one(&o, &err);
        CHECK(err == FULTA_ARC_OK, "case %zu: write %d", k, (int)err);
        int64_t lh = 0, ch = 0;
        CHECK(dos_fields(&z, &lh, &ch), "case %zu: headers", k);
        CHECK(lh == c[k].want && ch == c[k].want, "case %zu: DOS %lld/%lld, want %lld", k, (long long)lh,
              (long long)ch, (long long)c[k].want);
        CHECK(count_tag(&z, 0x5455) == 2, "case %zu: 0x5455 in both headers", k);
        fulta_arc_t *a;
        const fulta_arc_entry_t *e = open_one(&z, &a);
        CHECK(e && e->mtime == T0 * 1000000000 && !(e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL),
              "case %zu: read back UTC from 0x5455", k);
        fulta_arc_close(a);
        free(z.p);
    }
}

/* writer: the default is the machine's current offset, which the test takes from the C library on its own */
static void test_writer_machine_zone(void) {
    time_t now = time(NULL);
    struct tm lt = *localtime(&now), gt = *gmtime(&now);
    int64_t off = tm_secs(&lt) - tm_secs(&gt);
    off = (off + (off >= 0 ? 450 : -450)) / 900 * 900;
    fulta_arc_err_t err;
    mem_t z = write_one(NULL, &err);                   /* NULL options: LOCAL */
    int64_t lh = 0, ch = 0;
    CHECK(err == FULTA_ARC_OK && dos_fields(&z, &lh, &ch), "default write");
    CHECK(lh == T0 + off, "default DOS field %lld, want T0%+lld (this machine's offset)", (long long)lh,
          (long long)off);
    free(z.p);
    fulta_arc_write_options_t o = {.level = 6};        /* zeroed options: LOCAL too */
    z = write_one(&o, &err);
    CHECK(err == FULTA_ARC_OK && dos_fields(&z, &lh, &ch) && lh == T0 + off, "zeroed options: machine offset");
    free(z.p);
}

static void test_writer_refuses(void) {
    fulta_arc_err_t err;
    fulta_arc_write_options_t o = {.level = 6, .dos_time = (fulta_arc_dos_time_t)7};
    mem_t z = write_one(&o, &err);
    CHECK(err == FULTA_ARC_ERR_INVALID_ARG, "unknown dos_time mode: %d", (int)err);
    free(z.p);
    o = (fulta_arc_write_options_t){.level = 6, .dos_time = FULTA_ARC_DOS_TIME_OFFSET, .dos_utc_offset = 15 * 3600};
    z = write_one(&o, &err);
    CHECK(err == FULTA_ARC_ERR_INVALID_ARG, "offset beyond 14 h: %d", (int)err);
    free(z.p);
    o.dos_utc_offset = -14 * 3600;
    z = write_one(&o, &err);
    CHECK(err == FULTA_ARC_OK, "-14 h is in range: %d", (int)err);
    free(z.p);
}

/* reader: DOS only -> local; NTFS -> exact UTC, ahead of a 0x5455 that follows it; 0x5455 alone -> UTC */
static void test_reader_zip(void) {
    unsigned dt = 7u << 11 | 13u << 5 | 10u, dd = 43u << 9 | 11u << 5 | 15u;   /* 2023-11-15 07:13:20 */
    int64_t dos = dos_secs(dd, dt);
    fulta_arc_t *a;

    mem_t z = hand_zip(dt, dd, NULL, 0);
    const fulta_arc_entry_t *e = open_one(&z, &a);
    CHECK(e && (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) && (e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL)
          && e->mtime == dos * 1000000000, "DOS only: local wall clock, flagged");
    fulta_arc_close(a);
    free(z.p);

    /* NTFS FILETIME of 1700000000.1234567 s, then a 0x5455 claiming 1600000000: NTFS wins, to 100 ns */
    uint64_t ft = (uint64_t)(T0 + INT64_C(11644473600)) * 10000000u + 1234567u;
    unsigned char x[36 + 9];
    memset(x, 0, sizeof x);
    wr16(x, 0x000a); wr16(x + 2, 32); wr16(x + 8, 1); wr16(x + 10, 24);
    for (int i = 0; i < 3; i++) { wr32(x + 12 + 8 * i, (uint32_t)ft); wr32(x + 16 + 8 * i, (uint32_t)(ft >> 32)); }
    wr16(x + 36, 0x5455); wr16(x + 38, 5); x[40] = 1; wr32(x + 41, 1600000000u);
    z = hand_zip(dt, dd, x, sizeof x);
    e = open_one(&z, &a);
    CHECK(e && !(e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL) && e->mtime == T0 * 1000000000 + 123456700,
          "NTFS then 0x5455: NTFS time, not local (got %lld)", e ? (long long)e->mtime : 0LL);
    fulta_arc_close(a);
    free(z.p);

    z = hand_zip(dt, dd, x + 36, 9);                   /* 0x5455 alone */
    e = open_one(&z, &a);
    CHECK(e && !(e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL) && e->mtime == INT64_C(1600000000) * 1000000000,
          "0x5455 alone: UTC");
    fulta_arc_close(a);
    free(z.p);

    /* a truncated NTFS attribute (size 24 claimed, 8 present) is skipped: the DOS time stays, flagged */
    unsigned char bad[16];
    memset(bad, 0, sizeof bad);
    wr16(bad, 0x000a); wr16(bad + 2, 12); wr16(bad + 8, 1); wr16(bad + 10, 24);
    z = hand_zip(dt, dd, bad, sizeof bad);
    e = open_one(&z, &a);
    CHECK(e && (e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL) && e->mtime == dos * 1000000000, "truncated NTFS ignored");
    fulta_arc_close(a);
    free(z.p);
}

/* edit: copying a DOS-only entry keeps its DOS field and adds no 0x5455; converting it to 7z gives UTC */
static void test_edit_and_convert(void) {
    unsigned dt = 7u << 11 | 13u << 5 | 10u, dd = 43u << 9 | 11u << 5 | 15u;   /* 07:13:20 local, Korea */
    mem_t src = hand_zip(dt, dd, NULL, 0);
    fulta_arc_t *a;
    CHECK(open_one(&src, &a) != NULL, "open DOS-only source");
    fulta_arc_write_options_t o = {.level = 6, .dos_time = FULTA_ARC_DOS_TIME_OFFSET, .dos_utc_offset = 32400};

    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_edit_begin(a, &ed) == FULTA_ARC_OK, "edit begin");
    mem_t z = {0};
    fulta_arc_write_sink_t s = {.ctx = &z, .write = m_write, .patch = m_patch};
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &s, &o) == FULTA_ARC_OK, "edit write (copy route)");
    int64_t lh = 0, ch = 0;
    CHECK(dos_fields(&z, &lh, &ch) && lh == dos_secs(dd, dt) && ch == lh, "copy keeps the DOS field");
    CHECK(count_tag(&z, 0x5455) == 0, "copy adds no 0x5455 to a DOS-only entry");
    fulta_arc_t *b;
    const fulta_arc_entry_t *e = open_one(&z, &b);
    CHECK(e && (e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL), "the copy still reads as local");
    fulta_arc_close(b);
    free(z.p);

    z = (mem_t){0};
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_7Z, &s, &o) == FULTA_ARC_OK, "convert to 7z");
    e = open_one(&z, &b);
    CHECK(e && !(e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL) && e->mtime == T0 * 1000000000,
          "7z gets UTC: local - 9 h (got %lld)", e ? (long long)e->mtime : 0LL);
    fulta_arc_close(b);
    free(z.p);
    fulta_arc_edit_free(ed);
    fulta_arc_close(a);
    free(src.p);
}

/* the copy route keeps an entry's NTFS times as they are (it used to drop them, keeping only the DOS field) */
static void test_copy_keeps_ntfs(void) {
    unsigned char x[36] = {0x0a, 0x00, 32, 0, 0, 0, 0, 0, 1, 0, 24, 0};
    const uint64_t ft[3] = {UINT64_C(133432345671234567), UINT64_C(133432345680000001), UINT64_C(133000000000000009)};
    for (int k = 0; k < 3; k++) for (int i = 0; i < 8; i++) x[12 + 8 * k + i] = (unsigned char)(ft[k] >> (8 * i));
    mem_t src = hand_zip(7u << 11, 43u << 9 | 11u << 5 | 15u, x, sizeof x);
    fulta_arc_t *a;
    CHECK(open_one(&src, &a) != NULL, "open NTFS source");
    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_edit_begin(a, &ed) == FULTA_ARC_OK, "edit begin");
    mem_t z = {0};
    fulta_arc_write_sink_t s = {.ctx = &z, .write = m_write, .patch = m_patch};
    fulta_arc_write_options_t o = {.level = 6};
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &s, &o) == FULTA_ARC_OK, "edit write (copy route)");
    size_t seen = 0;
    for (size_t i = 0; i + sizeof x <= z.n; i++) if (!memcmp(z.p + i, x, sizeof x)) seen++;
    CHECK(seen == 2, "the 0x000a field, byte for byte, in the local and central headers (found %zu)", seen);
    fulta_arc_t *b;
    const fulta_arc_entry_t *e = open_one(&z, &b);
    int64_t want = (int64_t)(ft[0] / 10 - UINT64_C(11644473600000000)) * 1000 + (int64_t)(ft[0] % 10) * 100;
    CHECK(e && !(e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL) && e->mtime == want, "the copy reads the NTFS time (got %lld)",
          e ? (long long)e->mtime : 0LL);
    fulta_arc_close(b);
    free(z.p);
    fulta_arc_edit_free(ed);
    fulta_arc_close(a);
    free(src.p);
}

static bool open_fixture(const char *rel, fulta_arc_t **arc) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", t_fixtures(), rel);
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("%s not present", rel); return false; }
    fclose(f);
    fulta_arc_err_t e = fulta_arc_open_file(path, NULL, arc);
    CHECK(e == FULTA_ARC_OK, "%s: open %d", rel, (int)e);
    return e == FULTA_ARC_OK;
}

/* every format: the first entry with a time is flagged local exactly when the format stores local time */
static void test_formats(void) {
    static const struct { const char *rel; bool local; } c[] = {
        {"alz/alzip/a1237_deflate.alz", true},
        {"alzegg/b002_ext__o86__x_sys_m4.egg", true},
        {"rar15/s0200_two.rar", true},
        {"rar/rar2_m1.rar", true},                        /* RAR 2.x: FTIME */
        {"rar/rar3_m1.rar", true},                        /* RAR 3.x: FTIME */
        {"rar/rar5_arm.rar", false},                      /* RAR 5: Unix time */
        {"rar/rar5_fileattr.rar", false},                 /* RAR 5: FILETIME */
        {"cab/quantum10.cab", true},                      /* CAB: CFFILE date and time */
        {"lha/explzh_723/h0_lh5.lzh", true},              /* level 0: DOS time */
        {"lha/explzh_723/h2_lh5.lzh", false},             /* level 2: a UTC time beside it */
        {"zip7z/archives/zip_aes128.zip", false},         /* 7-Zip 25.01: NTFS 0x000a */
    };
    for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
        fulta_arc_t *a;
        if (!open_fixture(c[k].rel, &a)) continue;
        const fulta_arc_entry_t *e = NULL;
        for (size_t i = 0; i < fulta_arc_count(a); i++) {
            e = fulta_arc_entry(a, i);
            if (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) break;
            e = NULL;
        }
        CHECK(e != NULL, "%s: an entry with a time", c[k].rel);
        if (e) CHECK(((e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL) != 0) == c[k].local, "%s: MTIME_LOCAL %s", c[k].rel,
                     c[k].local ? "expected" : "unexpected");
        fulta_arc_close(a);
    }
}

/* 7-Zip's own ZIP: the NTFS time is the file's exact UTC time (7z l -slt: 2026-10-03 00:44:15.1094611 at +09:00),
   2 s and 9 h away from its DOS field (00:44:16) */
static void test_7zip_ntfs(void) {
    fulta_arc_t *a;
    if (!open_fixture("zip7z/archives/zip_aes128.zip", &a)) return;
    const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
    CHECK(e && e->mtime == INT64_C(1790955855109461100), "7-Zip NTFS time: got %lld", e ? (long long)e->mtime : 0LL);
    fulta_arc_close(a);
}

/* exact times, from 7z l -slt with TZ=UTC (7-Zip shows a local-time format's wall clock as it is) */
static void test_rar_cab_times(void) {
    static const struct { const char *rel; int64_t ns; } c[] = {
        {"rar/rar4_64bit_size.rar", INT64_C(1790893156699787900)},   /* 2026-10-01 22:19:16.6997879: EXT_TIME */
        {"rar/rar5_arm.rar", INT64_C(1538206123209742109)},          /* 2018-09-29 07:28:43.209742109: Unix + ns */
        {"cab/quantum10.cab", INT64_C(1792148218000000000)},         /* 2026-10-16 10:56:58 */
    };
    for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
        fulta_arc_t *a;
        if (!open_fixture(c[k].rel, &a)) continue;
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && (e->flags & FULTA_ARC_ENTRY_HAS_MTIME) && e->mtime == c[k].ns, "%s: mtime %lld, want %lld", c[k].rel,
              e ? (long long)e->mtime : 0LL, (long long)c[k].ns);
        fulta_arc_close(a);
    }
    /* the same file stored by RAR 4 (local, made at +09:00) and RAR 5 (UTC): 9 h apart, to RAR 4's 100 ns */
    fulta_arc_t *a4, *a5;
    if (!open_fixture("rar/rar4_enc_data.rar", &a4)) return;
    if (open_fixture("rar/rar5_enc_data.rar", &a5)) {
        const fulta_arc_entry_t *e4 = fulta_arc_entry(a4, 0), *e5 = fulta_arc_entry(a5, 0);
        int64_t d = e4 && e5 ? e4->mtime - INT64_C(32400000000000) - e5->mtime : INT64_MAX;
        CHECK(d > -100 && d <= 0, "RAR 4 local - 9 h = RAR 5 UTC (difference %lld ns)", (long long)d);
        fulta_arc_close(a5);
    }
    fulta_arc_close(a4);
}

/* conversion of a local-time format: ALZ -> ZIP writes the ALZ wall clock into the DOS field and UTC into 0x5455 */
static void test_alz_to_zip(void) {
    fulta_arc_t *a;
    if (!open_fixture("alz/alzip/a1237_deflate.alz", &a)) return;
    const fulta_arc_entry_t *src = fulta_arc_entry(a, 0);
    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_edit_begin(a, &ed) == FULTA_ARC_OK, "ALZ edit begin");
    fulta_arc_write_options_t o = {.level = 1, .dos_time = FULTA_ARC_DOS_TIME_OFFSET, .dos_utc_offset = 32400};
    mem_t z = {0};
    fulta_arc_write_sink_t s = {.ctx = &z, .write = m_write, .patch = m_patch};
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &s, &o) == FULTA_ARC_OK, "ALZ -> ZIP");
    int64_t lh = 0, ch = 0;
    CHECK(dos_fields(&z, &lh, &ch) && lh * 1000000000 == src->mtime, "DOS field = the ALZ wall clock");
    fulta_arc_t *b;
    const fulta_arc_entry_t *e = open_one(&z, &b);
    CHECK(e && !(e->flags & FULTA_ARC_ENTRY_MTIME_LOCAL) && e->mtime == src->mtime - INT64_C(32400) * 1000000000,
          "0x5455 = the ALZ wall clock - 9 h");
    fulta_arc_close(b);
    free(z.p);
    fulta_arc_edit_free(ed);
    fulta_arc_close(a);
}

int main(void) {
    test_writer_offsets();
    test_writer_machine_zone();
    test_writer_refuses();
    test_reader_zip();
    test_edit_and_convert();
    test_formats();
    test_7zip_ntfs();
    test_rar_cab_times();
    test_copy_keeps_ntfs();
    test_alz_to_zip();
    return t_report("test_time");
}
