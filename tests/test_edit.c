/* test_edit.c - FultaArc archive editing (RV-090 / D-015): the ZIP copy route. Builds a source ZIP with the
 * library's own writer (real deflate entries), edits a copy (remove / data-replace / rename-only / add), and checks
 * the output reopens with the right names, bytes and CRCs — and that an untouched entry is copied, not re-encoded
 * (same method and packed_size as the source). */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>
#include <stdio.h>

/* a growing capture sink: write appends, patch overwrites earlier bytes */
typedef struct { uint8_t *d; size_t n, cap; } cap_t;
static fulta_arc_err_t cap_write(void *ctx, const void *data, size_t n) {
    cap_t *c = ctx;
    if (c->n + n > c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 4096;
        while (nc < c->n + n) nc *= 2;
        uint8_t *nd = realloc(c->d, nc);
        if (!nd) return FULTA_ARC_ERR_NOMEM;
        c->d = nd; c->cap = nc;
    }
    memcpy(c->d + c->n, data, n); c->n += n; return FULTA_ARC_OK;
}
static fulta_arc_err_t cap_patch(void *ctx, uint64_t off, const void *data, size_t n) {
    cap_t *c = ctx;
    if (off + n > c->n) return FULTA_ARC_ERR_IO;
    memcpy(c->d + off, data, n); return FULTA_ARC_OK;
}

/* a memory pull reader */
typedef struct { const uint8_t *d; uint64_t n, pos; } rd_t;
static fulta_arc_err_t rd_read(void *ctx, void *buf, size_t cap, size_t *got) {
    rd_t *r = ctx;
    uint64_t left = r->n - r->pos;
    size_t k = left < cap ? (size_t)left : cap;
    memcpy(buf, r->d + r->pos, k); r->pos += k; *got = k; return FULTA_ARC_OK;
}
static fulta_arc_reader_t mem_reader(const uint8_t *d, uint64_t n, rd_t *state) {
    *state = (rd_t){.d = d, .n = n, .pos = 0};
    return (fulta_arc_reader_t){.ctx = state, .read = rd_read, .size = n, .mtime = 1600000000};
}

/* read entry `i` fully into a malloc'd buffer */
static uint8_t *extract_all(fulta_arc_t *a, size_t i, size_t *n) {
    void *d = NULL; *n = 0;
    if (fulta_arc_extract_alloc(a, i, &d, n) != FULTA_ARC_OK) return NULL;
    return d;
}

static size_t find(fulta_arc_t *a, const char *name) {
    for (size_t i = 0; i < fulta_arc_count(a); i++)
        if (strcmp(fulta_arc_entry(a, i)->name, name) == 0) return i;
    return SIZE_MAX;
}

static int cancel_cb(void *ctx, uint64_t done, uint64_t total) { (void)ctx; (void)done; (void)total; return 1; }

/* ---- encrypted output from the convert route (D-016) ---- */

static const char *g_pw;
static fulta_arc_err_t pw_cb(void *ctx, uint32_t a, const char **o) {
    (void)ctx; if (a || !g_pw) return FULTA_ARC_ERR_PASSWORD_NEEDED; *o = g_pw; return FULTA_ARC_OK;
}

/* a pull reader that does not say its size up front */
static fulta_arc_reader_t unsized_reader(const uint8_t *d, uint64_t n, rd_t *state) {
    fulta_arc_reader_t r = mem_reader(d, n, state);
    r.size = UINT64_MAX;
    return r;
}

/* While a 7z convert spills, no name of the spill is left in the output's directory (POSIX: removed at the open). */
#ifndef _WIN32
#include <dirent.h>
static int g_spill_seen = -1;
static int spill_probe(void *ctx, uint64_t done, uint64_t total) {
    (void)done; (void)total;
    const char *prefix = ctx;
    size_t pl = strlen(prefix);
    DIR *d = opendir(".");
    if (!d) return 0;
    int seen = 0;
    for (struct dirent *e; (e = readdir(d));)
        if (strncmp(e->d_name, prefix, pl) == 0 && strstr(e->d_name, ".d-")) seen = 1;
    closedir(d);
    if (g_spill_seen < 1) g_spill_seen = seen;
    return 0;
}
#endif

/* Convert `src` (an open archive) into `fmt` with a password, adding an unsized entry, an empty file and a directory;
 * reopen and check every file is flagged encrypted, extracts to its bytes with the password, and fails without it. */
static void enc_convert(fulta_arc_t *src, fulta_arc_format_t fmt, const fulta_arc_write_options_t *o,
                        const char *const *names, const uint8_t *const *datas, const size_t *sizes, size_t nsrc,
                        const uint8_t *X, size_t xn, const char *label) {
    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_edit_begin(src, &ed) == FULTA_ARC_OK && ed, "%s: edit_begin", label);
    if (!ed) return;
    rd_t rx, re;
    fulta_arc_reader_t ax = unsized_reader(X, xn, &rx), ae = mem_reader(X, 0, &re);
    CHECK(fulta_arc_edit_add(ed, "unsized.bin", &ax) == FULTA_ARC_OK, "%s: add unsized", label);
    CHECK(fulta_arc_edit_add(ed, "empty.txt", &ae) == FULTA_ARC_OK, "%s: add empty", label);
    CHECK(fulta_arc_edit_add(ed, "dir/", NULL) == FULTA_ARC_OK, "%s: add dir", label);
    cap_t cv = {0};
    fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
    fulta_arc_err_t we = fulta_arc_edit_write(ed, fmt, &cs, o);
    CHECK(we == FULTA_ARC_OK, "%s: write (got %s)", label, fulta_arc_strerror(we));
    fulta_arc_edit_free(ed);
    if (we == FULTA_ARC_OK) {
        /* no plaintext of the first source entry in the output */
        if (sizes[0] >= 64) {
            bool leaked = false;
            for (size_t k = 0; k + 64 <= cv.n && !leaked; k++) leaked = memcmp(cv.d + k, datas[0], 64) == 0;
            CHECK(!leaked, "%s: no 64-byte run of plaintext in the output", label);
        }
        fulta_arc_options_t rp = {.password = pw_cb};
        fulta_arc_t *a = NULL;
        g_pw = o->password;
        CHECK(fulta_arc_open_memory(cv.d, cv.n, &rp, &a) == FULTA_ARC_OK && a, "%s: reopen", label);
        if (a) {
            CHECK(fulta_arc_format(a) == fmt, "%s: format", label);
            CHECK(fulta_arc_count(a) == nsrc + 3, "%s: %zu entries, got %zu", label, nsrc + 3, fulta_arc_count(a));
            for (size_t i = 0; i < nsrc; i++) {
                size_t k = find(a, names[i]);
                CHECK(k != SIZE_MAX && (fulta_arc_entry(a, k)->flags & FULTA_ARC_ENTRY_ENCRYPTED), "%s: %s encrypted", label, names[i]);
                if (k == SIZE_MAX) continue;
                if (fmt == FULTA_ARC_FORMAT_ZIP) CHECK(fulta_arc_check_password(a, k) == FULTA_ARC_OK, "%s: %s password ok", label, names[i]);
                size_t n; uint8_t *d = extract_all(a, k, &n);
                CHECK(d && n == sizes[i] && memcmp(d, datas[i], n) == 0, "%s: %s bytes", label, names[i]);
                fulta_arc_free(d);
            }
            size_t k = find(a, "unsized.bin"), n; uint8_t *d;
            CHECK(k != SIZE_MAX && (fulta_arc_entry(a, k)->flags & FULTA_ARC_ENTRY_ENCRYPTED), "%s: unsized encrypted", label);
            d = k != SIZE_MAX ? extract_all(a, k, &n) : NULL;
            CHECK(d && n == xn && memcmp(d, X, n) == 0, "%s: unsized bytes", label); fulta_arc_free(d);
            k = find(a, "empty.txt");
            d = k != SIZE_MAX ? extract_all(a, k, &n) : NULL;
            CHECK(k != SIZE_MAX && n == 0, "%s: empty file", label); fulta_arc_free(d);
            k = find(a, "dir");
            if (k == SIZE_MAX) k = find(a, "dir/");
            CHECK(k != SIZE_MAX && (fulta_arc_entry(a, k)->flags & FULTA_ARC_ENTRY_DIR), "%s: directory", label);
            fulta_arc_close(a);
        }
        g_pw = "wrong password";
        if (fulta_arc_open_memory(cv.d, cv.n, &rp, &a) == FULTA_ARC_OK && a) {
            size_t k = find(a, names[0]);
            if (fmt == FULTA_ARC_FORMAT_ZIP) {
                fulta_arc_err_t pe = fulta_arc_check_password(a, k);
                CHECK(pe == FULTA_ARC_ERR_PASSWORD_WRONG, "%s: wrong password -> WRONG (got %s)", label, fulta_arc_strerror(pe));
            }
            void *d = NULL; size_t n = 0;
            CHECK(k != SIZE_MAX && fulta_arc_extract_alloc(a, k, &d, &n) != FULTA_ARC_OK, "%s: wrong password fails to extract", label);
            fulta_arc_free(d);
            fulta_arc_close(a);
        }
        g_pw = NULL;
    }
    free(cv.d);
}

/* ---- the ZIP edit/convert routes honour opts->method (Rubraview request (4)) ---- */

/* Check one ZIP output: every file reopens with its bytes and the expected method name. */
static void zm_check(const cap_t *cv, const char *pw, const char *const *names, const uint8_t *const *datas,
                     const size_t *sizes, size_t n, const char *method, const char *label) {
    fulta_arc_options_t rp = {.password = pw_cb};
    fulta_arc_t *a = NULL;
    g_pw = pw;
    CHECK(fulta_arc_open_memory(cv->d, cv->n, &rp, &a) == FULTA_ARC_OK && a, "%s: reopen", label);
    if (a) {
        CHECK(fulta_arc_format(a) == FULTA_ARC_FORMAT_ZIP, "%s: is a ZIP", label);
        CHECK(fulta_arc_count(a) == n, "%s: %zu entries, got %zu", label, n, fulta_arc_count(a));
        for (size_t i = 0; i < n; i++) {
            size_t k = find(a, names[i]);
            CHECK(k != SIZE_MAX, "%s: %s present", label, names[i]);
            if (k == SIZE_MAX) continue;
            const fulta_arc_entry_t *e = fulta_arc_entry(a, k);
            bool dir = (e->flags & FULTA_ARC_ENTRY_DIR) != 0;
            char want[32];                               /* an encrypted entry reports "<method>:AES" */
            snprintf(want, sizeof want, "%s%s", sizes[i] ? method : "Store", pw && !dir ? ":AES" : "");
            CHECK(e->method && strcmp(e->method, want) == 0, "%s: %s method %s, got %s", label, names[i], want,
                  e->method ? e->method : "(null)");
            CHECK(!pw || dir || (e->flags & FULTA_ARC_ENTRY_ENCRYPTED), "%s: %s encrypted", label, names[i]);
            size_t got; uint8_t *d = extract_all(a, k, &got);
            CHECK(sizes[i] == 0 ? got == 0 : (d && got == sizes[i] && memcmp(d, datas[i], got) == 0), "%s: %s bytes",
                  label, names[i]);
            fulta_arc_free(d);
        }
        fulta_arc_close(a);
    }
    g_pw = NULL;
}

static void zip_methods(void) {
    /* compressible text past one 64 KiB chunk, and bytes that do not compress */
    size_t tn = 200000, rn = 70001;
    uint8_t *T = malloc(tn), *R = malloc(rn);
    if (!T || !R) { free(T); free(R); return; }
    uint32_t x = 12345;
    for (size_t i = 0; i < tn; i++) { x = x * 1103515245u + 12345u; T[i] = (uint8_t)("etaoin shrdlu\n"[(x >> 16) % 14]); }
    for (size_t i = 0; i < rn; i++) { x = x * 1103515245u + 12345u; R[i] = (uint8_t)(x >> 24); }
    static const uint8_t five[5] = {'h', 'e', 'l', 'l', 'o'};

    /* a solid 7z source: its entries are decoded and re-encoded (the LZMA route spills them) */
    fulta_arc_write_entry_t se[] = {
        {.name = "s/text.txt", .data = T, .size = tn, .mtime = 1500000000},
        {.name = "s/rand.bin", .data = R, .size = rn, .mtime = 1500000001},
    };
    cap_t s7 = {0}, sz = {0};
    fulta_arc_write_sink_t s7k = {.ctx = &s7, .write = cap_write, .patch = cap_patch};
    fulta_arc_write_sink_t szk = {.ctx = &sz, .write = cap_write, .patch = cap_patch};
    fulta_arc_write_options_t w6 = {.level = 6};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, se, 2, &w6, &s7k) == FULTA_ARC_OK, "zm: build 7z source");
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, NULL, 0, &w6, &szk) == FULTA_ARC_OK, "zm: build empty zip");
    fulta_arc_t *a7 = NULL, *az = NULL;
    CHECK(fulta_arc_open_memory(s7.d, s7.n, NULL, &a7) == FULTA_ARC_OK && a7, "zm: open 7z source");
    CHECK(fulta_arc_open_memory(sz.d, sz.n, NULL, &az) == FULTA_ARC_OK && az, "zm: open empty zip");

    static const struct { fulta_arc_method_t m; const char *name; } ms[] = {
        {FULTA_ARC_METHOD_STORE, "Store"}, {FULTA_ARC_METHOD_DEFLATE, "Deflate"}, {FULTA_ARC_METHOD_BZIP2, "BZip2"},
        {FULTA_ARC_METHOD_LZMA, "LZMA"}, {FULTA_ARC_METHOD_PPMD, "PPMd"}, {FULTA_ARC_METHOD_ZSTD, "Zstandard"},
    };
    for (size_t mi = 0; mi < sizeof ms / sizeof ms[0] && a7 && az; mi++) {
        for (int enc = 0; enc < 2; enc++) {
            const char *pw = enc ? "zm-pw" : NULL;
            fulta_arc_write_options_t o = {.level = 6, .method = ms[mi].m, .password = pw};
            char label[64];

            /* convert: 7z -> ZIP, the source's entries plus a sized and an unsized add */
            snprintf(label, sizeof label, "zm convert %s%s", ms[mi].name, enc ? " aes" : "");
            fulta_arc_edit_t *ed = NULL;
            CHECK(fulta_arc_edit_begin(a7, &ed) == FULTA_ARC_OK && ed, "%s: begin", label);
            if (!ed) continue;
            rd_t r1, r2, r3;
            fulta_arc_reader_t add1 = mem_reader(T, tn, &r1), add2 = unsized_reader(R, rn, &r2);
            fulta_arc_reader_t add3 = mem_reader(five, 0, &r3);
            CHECK(fulta_arc_edit_add(ed, "add/text.txt", &add1) == FULTA_ARC_OK, "%s: add sized", label);
            CHECK(fulta_arc_edit_add(ed, "add/rand.bin", &add2) == FULTA_ARC_OK, "%s: add unsized", label);
            CHECK(fulta_arc_edit_add(ed, "add/empty", &add3) == FULTA_ARC_OK, "%s: add empty", label);
            cap_t cv = {0};
            fulta_arc_write_sink_t ck = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t we = fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &ck, &o);
            CHECK(we == FULTA_ARC_OK, "%s: write (got %s)", label, fulta_arc_strerror(we));
            fulta_arc_edit_free(ed);
            if (we == FULTA_ARC_OK) {
                const char *nm[] = {"s/text.txt", "s/rand.bin", "add/text.txt", "add/rand.bin", "add/empty"};
                const uint8_t *dt[] = {T, R, T, R, five};
                size_t sizes[] = {tn, rn, tn, rn, 0};
                zm_check(&cv, pw, nm, dt, sizes, 5, ms[mi].name, label);
            }
            free(cv.d);

            /* copy route: packing into an empty ZIP (Rubraview's way of making an archive) */
            snprintf(label, sizeof label, "zm pack %s%s", ms[mi].name, enc ? " aes" : "");
            ed = NULL;
            CHECK(fulta_arc_edit_begin(az, &ed) == FULTA_ARC_OK && ed, "%s: begin", label);
            if (!ed) continue;
            add1 = mem_reader(T, tn, &r1); add2 = unsized_reader(five, sizeof five, &r2);
            CHECK(fulta_arc_edit_add(ed, "text.txt", &add1) == FULTA_ARC_OK, "%s: add", label);
            CHECK(fulta_arc_edit_add(ed, "five.txt", &add2) == FULTA_ARC_OK, "%s: add", label);
            CHECK(fulta_arc_edit_add(ed, "d/", NULL) == FULTA_ARC_OK, "%s: add dir", label);
            cv = (cap_t){0};
            we = fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &ck, &o);
            CHECK(we == FULTA_ARC_OK, "%s: write (got %s)", label, fulta_arc_strerror(we));
            fulta_arc_edit_free(ed);
            if (we == FULTA_ARC_OK) {
                const char *nm[] = {"text.txt", "five.txt", "d"};
                const uint8_t *dt[] = {T, five, NULL};
                size_t sizes[] = {tn, sizeof five, 0};
                zm_check(&cv, pw, nm, dt, sizes, 3, ms[mi].name, label);
            }
            free(cv.d);
        }
    }

    /* LZMA to a file: the source entries spill beside the output, and nothing is left behind */
    if (a7) {
        const char *fpath = "test_edit_zm.zip";
        fulta_arc_edit_t *ed = NULL;
        CHECK(fulta_arc_edit_begin(a7, &ed) == FULTA_ARC_OK && ed, "zm file: begin");
        if (ed) {
            fulta_arc_write_options_t o = {.level = 5, .method = FULTA_ARC_METHOD_LZMA};
#ifndef _WIN32
            o.progress = spill_probe; o.progress_ctx = (void *)fpath;
            g_spill_seen = -1;
#endif
            fulta_arc_err_t fe = fulta_arc_edit_write_file(ed, FULTA_ARC_FORMAT_ZIP, fpath, &o);
            CHECK(fe == FULTA_ARC_OK, "zm file: write (got %s)", fulta_arc_strerror(fe));
            fulta_arc_edit_free(ed);
#ifndef _WIN32
            CHECK(g_spill_seen == 0, "zm file: no spill name visible (%d)", g_spill_seen);
#endif
            fulta_arc_t *f = NULL;
            CHECK(fulta_arc_open_file(fpath, NULL, &f) == FULTA_ARC_OK && f, "zm file: reopen");
            if (f) {
                size_t k = find(f, "s/rand.bin"), n; uint8_t *d = k != SIZE_MAX ? extract_all(f, k, &n) : NULL;
                CHECK(d && n == rn && memcmp(d, R, n) == 0, "zm file: rand.bin bytes"); fulta_arc_free(d);
                fulta_arc_close(f);
            }
            remove(fpath);
        }
        /* cancel mid-way through an LZMA entry */
        ed = NULL;
        if (fulta_arc_edit_begin(a7, &ed) == FULTA_ARC_OK && ed) {
            fulta_arc_write_options_t o = {.level = 5, .method = FULTA_ARC_METHOD_LZMA, .progress = cancel_cb};
            cap_t cv = {0};
            fulta_arc_write_sink_t ck = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &ck, &o);
            CHECK(ce == FULTA_ARC_ERR_CANCELLED, "zm: LZMA cancel -> CANCELLED (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(ed);
            free(cv.d);
        }
        /* LZMA2 is not a ZIP method */
        ed = NULL;
        if (fulta_arc_edit_begin(a7, &ed) == FULTA_ARC_OK && ed) {
            fulta_arc_write_options_t o = {.level = 5, .method = FULTA_ARC_METHOD_LZMA2};
            cap_t cv = {0};
            fulta_arc_write_sink_t ck = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &ck, &o) == FULTA_ARC_ERR_INVALID_ARG,
                  "zm: LZMA2 into ZIP -> INVALID_ARG");
            fulta_arc_edit_free(ed);
            free(cv.d);
        }
    }
    fulta_arc_close(a7); fulta_arc_close(az);
    free(s7.d); free(sz.d); free(T); free(R);
}

int main(void) {
    /* highly compressible payloads so the writer picks deflate (method 8) */
    uint8_t A[500], B[4000], C[4000], B2[3000], N[2000];
    for (size_t i = 0; i < sizeof A; i++) A[i] = (uint8_t)(i & 7);
    for (size_t i = 0; i < sizeof B; i++) B[i] = (uint8_t)('b');
    for (size_t i = 0; i < sizeof C; i++) C[i] = (uint8_t)(i % 13);
    for (size_t i = 0; i < sizeof B2; i++) B2[i] = (uint8_t)(i % 5);
    for (size_t i = 0; i < sizeof N; i++) N[i] = (uint8_t)(i * 3);

    /* source archive, built by the library (real deflate) */
    fulta_arc_write_entry_t src[] = {
        {.name = "a.txt", .data = A, .size = sizeof A, .mtime = 1500000000},
        {.name = "b.txt", .data = B, .size = sizeof B, .mtime = 1500000001},
        {.name = "c.txt", .data = C, .size = sizeof C, .mtime = 1500000002},
    };
    cap_t srcbuf = {0};
    fulta_arc_write_sink_t ssink = {.ctx = &srcbuf, .write = cap_write, .patch = cap_patch};
    fulta_arc_write_options_t wo = {.level = 6};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, src, 3, &wo, &ssink) == FULTA_ARC_OK, "build source zip");

    fulta_arc_t *in = NULL;
    CHECK(fulta_arc_open_memory(srcbuf.d, srcbuf.n, NULL, &in) == FULTA_ARC_OK && in, "open source");
    if (!in) return t_report("test_edit");
    CHECK(fulta_arc_editable(in) == FULTA_ARC_EDIT_OK, "source zip is editable");
    size_t ci = find(in, "c.txt");
    uint64_t c_packed = fulta_arc_entry(in, ci)->packed_size;
    const char *c_method = fulta_arc_entry(in, ci)->method;

    /* edit: remove a.txt; replace b.txt data (keep name); rename c.txt -> sub/c.txt; add new.bin */
    fulta_arc_edit_t *ed = NULL;
    CHECK(fulta_arc_edit_begin(in, &ed) == FULTA_ARC_OK && ed, "edit_begin");
    rd_t rb2, rn;
    fulta_arc_reader_t repb = mem_reader(B2, sizeof B2, &rb2);
    fulta_arc_reader_t addn = mem_reader(N, sizeof N, &rn);
    CHECK(fulta_arc_edit_remove(ed, find(in, "a.txt")) == FULTA_ARC_OK, "remove a.txt");
    CHECK(fulta_arc_edit_replace(ed, find(in, "b.txt"), NULL, &repb) == FULTA_ARC_OK, "replace b.txt data");
    CHECK(fulta_arc_edit_replace(ed, find(in, "c.txt"), "sub/c.txt", NULL) == FULTA_ARC_OK, "rename c.txt");
    CHECK(fulta_arc_edit_add(ed, "new.bin", &addn) == FULTA_ARC_OK, "add new.bin");
    /* double remove is rejected */
    CHECK(fulta_arc_edit_remove(ed, find(in, "a.txt")) == FULTA_ARC_ERR_INVALID_ARG, "double remove rejected");

    cap_t out = {0};
    fulta_arc_write_sink_t osink = {.ctx = &out, .write = cap_write, .patch = cap_patch};
    CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &osink, &wo) == FULTA_ARC_OK, "edit_write zip");
    fulta_arc_edit_free(ed);

    fulta_arc_t *res = NULL;
    CHECK(fulta_arc_open_memory(out.d, out.n, NULL, &res) == FULTA_ARC_OK && res, "open edited");
    if (res) {
        CHECK(fulta_arc_count(res) == 3, "edited has 3 entries (a removed), got %zu", fulta_arc_count(res));
        CHECK(find(res, "a.txt") == SIZE_MAX, "a.txt is gone");
        /* order preserved: b.txt, sub/c.txt, then the appended new.bin */
        CHECK(strcmp(fulta_arc_entry(res, 0)->name, "b.txt") == 0, "order: b.txt first");
        CHECK(strcmp(fulta_arc_entry(res, 1)->name, "sub/c.txt") == 0, "order: sub/c.txt second");
        CHECK(strcmp(fulta_arc_entry(res, 2)->name, "new.bin") == 0, "order: new.bin appended last");

        size_t n; uint8_t *d;
        d = extract_all(res, 0, &n); CHECK(d && n == sizeof B2 && memcmp(d, B2, n) == 0, "b.txt has the new data"); fulta_arc_free(d);
        size_t rci = find(res, "sub/c.txt");
        d = extract_all(res, rci, &n); CHECK(d && n == sizeof C && memcmp(d, C, n) == 0, "sub/c.txt kept the old bytes"); fulta_arc_free(d);
        d = extract_all(res, find(res, "new.bin"), &n); CHECK(d && n == sizeof N && memcmp(d, N, n) == 0, "new.bin has its data"); fulta_arc_free(d);

        /* the renamed, untouched entry was copied, not re-encoded */
        CHECK(fulta_arc_entry(res, rci)->packed_size == c_packed, "sub/c.txt packed_size unchanged (copied, %llu vs %llu)",
              (unsigned long long)fulta_arc_entry(res, rci)->packed_size, (unsigned long long)c_packed);
        CHECK(strcmp(fulta_arc_entry(res, rci)->method, c_method) == 0, "sub/c.txt method unchanged (copied)");
        fulta_arc_close(res);
    }

    /* ---- regression: replace an entry with a ZERO-LENGTH reader on the ZIP copy route (rubraview double-free) ----
       a reader with .size == 0 whose read() returns *got = 0 made slurp_reader malloc(1) but keep cap == 0, so the
       first grow computed ncap = cap*2 = 0 and realloc(buf,0) freed buf, then the !nb branch freed it again. */
    {
        fulta_arc_t *in2 = NULL;
        CHECK(fulta_arc_open_memory(srcbuf.d, srcbuf.n, NULL, &in2) == FULTA_ARC_OK && in2, "open source (empty-reader case)");
        if (in2) {
            fulta_arc_edit_t *ez = NULL;
            CHECK(fulta_arc_edit_begin(in2, &ez) == FULTA_ARC_OK && ez, "edit_begin (empty reader)");
            rd_t re0; fulta_arc_reader_t empty = mem_reader(N, 0, &re0);   /* size 0, read() yields *got = 0 */
            CHECK(fulta_arc_edit_replace(ez, find(in2, "b.txt"), NULL, &empty) == FULTA_ARC_OK, "replace b.txt with empty reader");
            cap_t ov = {0};
            fulta_arc_write_sink_t os2 = {.ctx = &ov, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ze = fulta_arc_edit_write(ez, FULTA_ARC_FORMAT_ZIP, &os2, &wo);
            CHECK(ze == FULTA_ARC_OK, "edit_write with empty reader (no double-free) (got %s)", fulta_arc_strerror(ze));
            fulta_arc_edit_free(ez);
            if (ze == FULTA_ARC_OK) {
                fulta_arc_t *rz = NULL;
                CHECK(fulta_arc_open_memory(ov.d, ov.n, NULL, &rz) == FULTA_ARC_OK && rz, "reopen (empty reader)");
                if (rz) {
                    size_t bi = find(rz, "b.txt"); size_t n; uint8_t *d;
                    CHECK(bi != SIZE_MAX && fulta_arc_entry(rz, bi)->size == 0, "b.txt is now zero-length");
                    d = extract_all(rz, bi, &n); CHECK(n == 0, "b.txt extracts to 0 bytes"); fulta_arc_free(d);
                    fulta_arc_close(rz);
                }
            }
            free(ov.d);
            fulta_arc_close(in2);
        }
    }

    /* edit_write_file: temp + rename, then reopen from disk (reuse the still-open source handle) */
    CHECK(fulta_arc_edit_begin(in, &ed) == FULTA_ARC_OK && ed, "edit_begin for file");
    CHECK(fulta_arc_edit_remove(ed, find(in, "a.txt")) == FULTA_ARC_OK, "remove for file");
    const char *path = "./_test_edit_out.zip";
    fulta_arc_err_t fe = fulta_arc_edit_write_file(ed, FULTA_ARC_FORMAT_ZIP, path, &wo);
    CHECK(fe == FULTA_ARC_OK, "edit_write_file (got %s)", fulta_arc_strerror(fe));
    fulta_arc_edit_free(ed);
    if (fe == FULTA_ARC_OK) {
        fulta_arc_t *ff = NULL;
        CHECK(fulta_arc_open_file(path, NULL, &ff) == FULTA_ARC_OK && ff, "reopen written file");
        if (ff) { CHECK(fulta_arc_count(ff) == 2 && find(ff, "a.txt") == SIZE_MAX, "written file has 2 entries, a removed"); fulta_arc_close(ff); }
        remove(path);
    }
    /* ---- convert route: a solid 7z source re-encoded into a fresh ZIP ---- */
    {
        fulta_arc_write_entry_t s7[] = {
            {.name = "x.txt", .data = A, .size = sizeof A, .mtime = 1500000000},
            {.name = "y.txt", .data = B, .size = sizeof B, .mtime = 1500000001},
            {.name = "z.txt", .data = C, .size = sizeof C, .mtime = 1500000002},
        };
        cap_t s7buf = {0};
        fulta_arc_write_sink_t s7sink = {.ctx = &s7buf, .write = cap_write, .patch = cap_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, s7, 3, &wo, &s7sink) == FULTA_ARC_OK, "build source 7z");
        fulta_arc_t *z7 = NULL;
        CHECK(fulta_arc_open_memory(s7buf.d, s7buf.n, NULL, &z7) == FULTA_ARC_OK && z7, "open source 7z");
        if (z7) {
            fulta_arc_editable_t ev = fulta_arc_editable(z7);
            CHECK(ev == FULTA_ARC_EDIT_SOLID || ev == FULTA_ARC_EDIT_OK, "7z editable is solid/ok (%d)", (int)ev);
            fulta_arc_edit_t *e7 = NULL;
            CHECK(fulta_arc_edit_begin(z7, &e7) == FULTA_ARC_OK && e7, "edit_begin 7z");
            rd_t ra; fulta_arc_reader_t adda = mem_reader(N, sizeof N, &ra);
            CHECK(fulta_arc_edit_remove(e7, find(z7, "y.txt")) == FULTA_ARC_OK, "7z remove y.txt");
            CHECK(fulta_arc_edit_add(e7, "added.bin", &adda) == FULTA_ARC_OK, "7z add added.bin");
            cap_t cv = {0};
            fulta_arc_write_sink_t cvsink = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(e7, FULTA_ARC_FORMAT_ZIP, &cvsink, &wo);
            CHECK(ce == FULTA_ARC_OK, "convert 7z->zip (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(e7);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *cz = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &cz) == FULTA_ARC_OK && cz, "open converted zip");
                if (cz) {
                    CHECK(fulta_arc_format(cz) == FULTA_ARC_FORMAT_ZIP, "converted is zip");
                    CHECK(fulta_arc_count(cz) == 3, "converted has 3 entries (x,z,added), got %zu", fulta_arc_count(cz));
                    CHECK(find(cz, "y.txt") == SIZE_MAX, "y.txt gone in converted");
                    size_t n; uint8_t *d;
                    d = extract_all(cz, find(cz, "x.txt"), &n); CHECK(d && n == sizeof A && memcmp(d, A, n) == 0, "x.txt bytes round-trip"); fulta_arc_free(d);
                    d = extract_all(cz, find(cz, "z.txt"), &n); CHECK(d && n == sizeof C && memcmp(d, C, n) == 0, "z.txt bytes round-trip"); fulta_arc_free(d);
                    d = extract_all(cz, find(cz, "added.bin"), &n); CHECK(d && n == sizeof N && memcmp(d, N, n) == 0, "added.bin bytes"); fulta_arc_free(d);
                    fulta_arc_close(cz);
                }
            }
            free(cv.d);
            /* the source 7z bytes were not modified by the convert */
            CHECK(s7buf.n > 0, "source 7z buffer intact");
            fulta_arc_close(z7);
        }
        free(s7buf.d);
    }

    /* ---- convert route: an ALZ fixture -> ZIP; every entry must round-trip byte-identically (format breadth) ---- */
    {
        char p[512];
        snprintf(p, sizeof p, "%s/alz/deflate.alz", t_fixtures());
        fulta_arc_t *az = NULL;
        if (fulta_arc_open_file(p, NULL, &az) == FULTA_ARC_OK && az) {
            size_t nc = fulta_arc_count(az);
            fulta_arc_edit_t *ae = NULL;
            CHECK(fulta_arc_edit_begin(az, &ae) == FULTA_ARC_OK && ae, "edit_begin alz");
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(ae, FULTA_ARC_FORMAT_ZIP, &cs, &wo);
            CHECK(ce == FULTA_ARC_OK, "convert alz->zip (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(ae);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *cz = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &cz) == FULTA_ARC_OK && cz, "open alz-converted zip");
                if (cz) {
                    CHECK(fulta_arc_count(cz) == nc, "converted keeps the entry count (%zu vs %zu)", fulta_arc_count(cz), nc);
                    for (size_t i = 0; i < nc; i++) {
                        const fulta_arc_entry_t *se = fulta_arc_entry(az, i);
                        if (se->flags & FULTA_ARC_ENTRY_DIR) continue;
                        size_t sn = 0, zn = 0;
                        uint8_t *sd = extract_all(az, i, &sn);
                        size_t zi = find(cz, se->name);
                        uint8_t *zd = zi != SIZE_MAX ? extract_all(cz, zi, &zn) : NULL;
                        CHECK(sd && zd && sn == zn && memcmp(sd, zd, sn) == 0, "alz entry '%s' round-trips through convert", se->name);
                        fulta_arc_free(sd); fulta_arc_free(zd);   /* extract_alloc blocks: not the C library's */
                    }
                    fulta_arc_close(cz);
                }
            }
            free(cv.d);
            fulta_arc_close(az);
        }
    }

    /* ---- convert route: 7z TARGET (any source -> a fresh 7z); the pull-encoder is bridged via a disk spill ---- */
    {
        /* a solid 7z source, edited and re-encoded into a fresh 7z */
        fulta_arc_write_entry_t s7[] = {
            {.name = "x.txt", .data = A, .size = sizeof A, .mtime = 1500000000},
            {.name = "y.txt", .data = B, .size = sizeof B, .mtime = 1500000001},
            {.name = "z.txt", .data = C, .size = sizeof C, .mtime = 1500000002},
        };
        cap_t s7buf = {0};
        fulta_arc_write_sink_t s7sink = {.ctx = &s7buf, .write = cap_write, .patch = cap_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, s7, 3, &wo, &s7sink) == FULTA_ARC_OK, "build source 7z (to 7z)");
        fulta_arc_t *z7 = NULL;
        CHECK(fulta_arc_open_memory(s7buf.d, s7buf.n, NULL, &z7) == FULTA_ARC_OK && z7, "open source 7z (to 7z)");
        if (z7) {
            fulta_arc_edit_t *e7 = NULL;
            CHECK(fulta_arc_edit_begin(z7, &e7) == FULTA_ARC_OK && e7, "edit_begin 7z->7z");
            rd_t ra; fulta_arc_reader_t adda = mem_reader(N, sizeof N, &ra);
            CHECK(fulta_arc_edit_remove(e7, find(z7, "y.txt")) == FULTA_ARC_OK, "7z->7z remove y.txt");
            CHECK(fulta_arc_edit_add(e7, "added.bin", &adda) == FULTA_ARC_OK, "7z->7z add added.bin");
            cap_t cv = {0};
            fulta_arc_write_sink_t cvsink = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(e7, FULTA_ARC_FORMAT_7Z, &cvsink, &wo);
            CHECK(ce == FULTA_ARC_OK, "convert 7z->7z (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(e7);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *c7 = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &c7) == FULTA_ARC_OK && c7, "open converted 7z");
                if (c7) {
                    CHECK(fulta_arc_format(c7) == FULTA_ARC_FORMAT_7Z, "converted is 7z");
                    CHECK(fulta_arc_count(c7) == 3, "converted 7z has 3 entries (x,z,added), got %zu", fulta_arc_count(c7));
                    CHECK(find(c7, "y.txt") == SIZE_MAX, "y.txt gone in converted 7z");
                    size_t n; uint8_t *d;
                    d = extract_all(c7, find(c7, "x.txt"), &n); CHECK(d && n == sizeof A && memcmp(d, A, n) == 0, "7z x.txt bytes round-trip"); fulta_arc_free(d);
                    d = extract_all(c7, find(c7, "z.txt"), &n); CHECK(d && n == sizeof C && memcmp(d, C, n) == 0, "7z z.txt bytes round-trip"); fulta_arc_free(d);
                    d = extract_all(c7, find(c7, "added.bin"), &n); CHECK(d && n == sizeof N && memcmp(d, N, n) == 0, "7z added.bin bytes"); fulta_arc_free(d);
                    fulta_arc_close(c7);
                }
            }
            free(cv.d);
            fulta_arc_close(z7);
        }
        free(s7buf.d);
    }

    /* ---- convert route honours the selector (RFC-0004 step 2): a solid 7z re-encoded into a fresh 7z with an
            explicit method and SOLID_OFF is non-solid (one folder per file), editable, and round-trips ---- */
    {
        fulta_arc_write_entry_t s7[] = {
            {.name = "x.txt", .data = A, .size = sizeof A, .mtime = 1500000000},
            {.name = "y.txt", .data = B, .size = sizeof B, .mtime = 1500000001},
        };
        cap_t s7buf = {0};
        fulta_arc_write_sink_t s7sink = {.ctx = &s7buf, .write = cap_write, .patch = cap_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, s7, 2, &wo, &s7sink) == FULTA_ARC_OK, "build solid src 7z (selector)");
        fulta_arc_t *z7 = NULL;
        CHECK(fulta_arc_open_memory(s7buf.d, s7buf.n, NULL, &z7) == FULTA_ARC_OK && z7, "open solid src 7z (selector)");
        if (z7) {
            fulta_arc_edit_t *e7 = NULL;
            CHECK(fulta_arc_edit_begin(z7, &e7) == FULTA_ARC_OK && e7, "edit_begin convert selector");
            cap_t cv = {0};
            fulta_arc_write_sink_t cvsink = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_write_options_t so = {.level = 6, .method = FULTA_ARC_METHOD_LZMA2, .solid = FULTA_ARC_SOLID_OFF};
            fulta_arc_err_t ce = fulta_arc_edit_write(e7, FULTA_ARC_FORMAT_7Z, &cvsink, &so);
            CHECK(ce == FULTA_ARC_OK, "convert 7z->7z LZMA2+SOLID_OFF (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(e7);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *c7 = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &c7) == FULTA_ARC_OK && c7, "open converted selector 7z");
                if (c7) {
                    CHECK(fulta_arc_editable(c7) == FULTA_ARC_EDIT_OK, "converted SOLID_OFF 7z is editable");
                    size_t xi = find(c7, "x.txt"), yi = find(c7, "y.txt");
                    CHECK(xi != SIZE_MAX && strcmp(fulta_arc_entry(c7, xi)->method, "LZMA2") == 0, "converted x.txt uses LZMA2");
                    CHECK(xi != SIZE_MAX && yi != SIZE_MAX && fulta_arc_entry(c7, xi)->solid_group != fulta_arc_entry(c7, yi)->solid_group,
                          "converted entries in distinct solid groups");
                    size_t n; uint8_t *d;
                    d = extract_all(c7, xi, &n); CHECK(d && n == sizeof A && memcmp(d, A, n) == 0, "converted x.txt bytes"); fulta_arc_free(d);
                    d = extract_all(c7, yi, &n); CHECK(d && n == sizeof B && memcmp(d, B, n) == 0, "converted y.txt bytes"); fulta_arc_free(d);
                    fulta_arc_close(c7);
                }
            }
            free(cv.d);
            fulta_arc_close(z7);
        }
        free(s7buf.d);
    }

    /* ---- convert to 7z: a ZIP with an empty file and a directory, at deflate (L6) and store (L0=Copy) ---- */
    {
        fulta_arc_write_entry_t zz[] = {
            {.name = "d/", .data = NULL, .size = 0},                              /* a directory */
            {.name = "d/full.bin", .data = C, .size = sizeof C, .mtime = 1500000003},
            {.name = "d/empty.bin", .data = N, .size = 0, .mtime = 1500000004},   /* zero-byte file (emptyStream) */
        };
        cap_t zbuf = {0};
        fulta_arc_write_sink_t zsink = {.ctx = &zbuf, .write = cap_write, .patch = cap_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, zz, 3, &wo, &zsink) == FULTA_ARC_OK, "build source zip (empty+dir)");
        for (int lvl = 6; lvl >= 0; lvl -= 6) {                                   /* 6 = LZMA folder, 0 = Copy folder */
            fulta_arc_t *zin = NULL;
            CHECK(fulta_arc_open_memory(zbuf.d, zbuf.n, NULL, &zin) == FULTA_ARC_OK && zin, "open source zip (empty+dir) L%d", lvl);
            if (!zin) continue;
            fulta_arc_edit_t *ze = NULL;
            CHECK(fulta_arc_edit_begin(zin, &ze) == FULTA_ARC_OK && ze, "edit_begin zip->7z L%d", lvl);
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_write_options_t lo = {.level = lvl};
            fulta_arc_err_t ce = fulta_arc_edit_write(ze, FULTA_ARC_FORMAT_7Z, &cs, &lo);
            CHECK(ce == FULTA_ARC_OK, "convert zip->7z L%d (got %s)", lvl, fulta_arc_strerror(ce));
            fulta_arc_edit_free(ze);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *c7 = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &c7) == FULTA_ARC_OK && c7, "open zip->7z L%d", lvl);
                if (c7) {
                    CHECK(fulta_arc_count(c7) == 3, "zip->7z L%d keeps 3 entries, got %zu", lvl, fulta_arc_count(c7));
                    size_t n; uint8_t *d;
                    d = extract_all(c7, find(c7, "d/full.bin"), &n);
                    CHECK(d && n == sizeof C && memcmp(d, C, n) == 0, "zip->7z L%d full.bin round-trip", lvl); fulta_arc_free(d);
                    size_t ei = find(c7, "d/empty.bin");
                    CHECK(ei != SIZE_MAX, "zip->7z L%d empty.bin present", lvl);
                    void *ed = NULL; size_t en = 123;
                    fulta_arc_err_t xe = fulta_arc_extract_alloc(c7, ei, &ed, &en);
                    CHECK(xe == FULTA_ARC_OK && en == 0, "zip->7z L%d empty.bin extracts to 0 bytes (%s, n=%zu)",
                          lvl, fulta_arc_strerror(xe), en);
                    fulta_arc_free(ed);
                    fulta_arc_close(c7);
                }
            }
            free(cv.d);
            fulta_arc_close(zin);
        }
        free(zbuf.d);
    }

    /* ---- convert to 7z: an ALZ fixture; every non-dir entry must round-trip byte-identically (format breadth) ---- */
    {
        char p[512];
        snprintf(p, sizeof p, "%s/alz/deflate.alz", t_fixtures());
        fulta_arc_t *az = NULL;
        if (fulta_arc_open_file(p, NULL, &az) == FULTA_ARC_OK && az) {
            size_t nc = fulta_arc_count(az);
            fulta_arc_edit_t *ae = NULL;
            CHECK(fulta_arc_edit_begin(az, &ae) == FULTA_ARC_OK && ae, "edit_begin alz->7z");
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(ae, FULTA_ARC_FORMAT_7Z, &cs, &wo);
            CHECK(ce == FULTA_ARC_OK, "convert alz->7z (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(ae);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *c7 = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &c7) == FULTA_ARC_OK && c7, "open alz-converted 7z");
                if (c7) {
                    CHECK(fulta_arc_count(c7) == nc, "alz->7z keeps the entry count (%zu vs %zu)", fulta_arc_count(c7), nc);
                    for (size_t i = 0; i < nc; i++) {
                        const fulta_arc_entry_t *se = fulta_arc_entry(az, i);
                        if (se->flags & FULTA_ARC_ENTRY_DIR) continue;
                        size_t sn = 0, zn = 0;
                        uint8_t *sd = extract_all(az, i, &sn);
                        size_t zi = find(c7, se->name);
                        uint8_t *zd = zi != SIZE_MAX ? extract_all(c7, zi, &zn) : NULL;
                        CHECK(sd && zd && sn == zn && memcmp(sd, zd, sn) == 0, "alz entry '%s' round-trips through 7z convert", se->name);
                        fulta_arc_free(sd); fulta_arc_free(zd);   /* extract_alloc blocks: not the C library's */
                    }
                    fulta_arc_close(c7);
                }
            }
            free(cv.d);
            fulta_arc_close(az);
        }
    }

    /* ---- 7z COPY route (editable==OK, non-solid): untouched folders copied verbatim, adds re-encoded ---- */
    {
        /* a single-file 7z is non-solid (one folder, one file) => editable OK => copy route */
        fulta_arc_write_entry_t one[] = { {.name = "orig.bin", .data = C, .size = sizeof C, .mtime = 1500000005} };
        cap_t obuf = {0};
        fulta_arc_write_sink_t osk = {.ctx = &obuf, .write = cap_write, .patch = cap_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, one, 1, &wo, &osk) == FULTA_ARC_OK, "build 1-file 7z");
        fulta_arc_t *s1 = NULL;
        CHECK(fulta_arc_open_memory(obuf.d, obuf.n, NULL, &s1) == FULTA_ARC_OK && s1, "open 1-file 7z");
        if (s1) {
            CHECK(fulta_arc_editable(s1) == FULTA_ARC_EDIT_OK, "1-file 7z is editable OK (copy route)");
            unsigned long long src_packed = (unsigned long long)fulta_arc_entry(s1, 0)->packed_size;
            char src_method[32]; snprintf(src_method, sizeof src_method, "%s", fulta_arc_entry(s1, 0)->method);
            fulta_arc_edit_t *e1 = NULL;
            CHECK(fulta_arc_edit_begin(s1, &e1) == FULTA_ARC_OK && e1, "edit_begin 7z copy");
            CHECK(fulta_arc_edit_replace(e1, 0, "sub/orig.bin", NULL) == FULTA_ARC_OK, "rename orig (copy kept)");
            rd_t rn2; fulta_arc_reader_t addn = mem_reader(N, sizeof N, &rn2);
            CHECK(fulta_arc_edit_add(e1, "added.bin", &addn) == FULTA_ARC_OK, "add added.bin (encoded)");
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(e1, FULTA_ARC_FORMAT_7Z, &cs, &wo);
            CHECK(ce == FULTA_ARC_OK, "7z copy-route write (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(e1);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *r1 = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &r1) == FULTA_ARC_OK && r1, "reopen 7z copy result");
                if (r1) {
                    CHECK(fulta_arc_format(r1) == FULTA_ARC_FORMAT_7Z, "copy result is 7z");
                    CHECK(fulta_arc_count(r1) == 2, "copy result has 2 entries, got %zu", fulta_arc_count(r1));
                    size_t ri = find(r1, "sub/orig.bin");
                    CHECK(ri != SIZE_MAX, "renamed copied entry present");
                    if (ri != SIZE_MAX) {
                        size_t n; uint8_t *d;
                        d = extract_all(r1, ri, &n); CHECK(d && n == sizeof C && memcmp(d, C, n) == 0, "copied entry bytes round-trip"); fulta_arc_free(d);
                        CHECK((unsigned long long)fulta_arc_entry(r1, ri)->packed_size == src_packed,
                              "copied entry packed_size unchanged (%llu vs %llu)",
                              (unsigned long long)fulta_arc_entry(r1, ri)->packed_size, src_packed);
                        CHECK(strcmp(fulta_arc_entry(r1, ri)->method, src_method) == 0,
                              "copied entry method unchanged (%s vs %s)", fulta_arc_entry(r1, ri)->method, src_method);
                    }
                    size_t ai = find(r1, "added.bin");
                    if (ai != SIZE_MAX) { size_t n; uint8_t *d = extract_all(r1, ai, &n);
                        CHECK(d && n == sizeof N && memcmp(d, N, n) == 0, "added entry bytes"); fulta_arc_free(d); }
                    CHECK(fulta_arc_editable(r1) == FULTA_ARC_EDIT_OK, "copy result is itself non-solid/editable");
                    fulta_arc_close(r1);
                }
            }
            free(cv.d);
            fulta_arc_close(s1);
        }
        free(obuf.d);
    }

    /* ---- 7z COPY route: an all-empty source (zero folders) must produce a valid headerless-streams 7z ---- */
    {
        fulta_arc_write_entry_t em[] = { {.name = "d/", .size = 0}, {.name = "empty.txt", .data = N, .size = 0} };
        cap_t eb = {0};
        fulta_arc_write_sink_t es = {.ctx = &eb, .write = cap_write, .patch = cap_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, em, 2, &wo, &es) == FULTA_ARC_OK, "build all-empty 7z");
        fulta_arc_t *se = NULL;
        CHECK(fulta_arc_open_memory(eb.d, eb.n, NULL, &se) == FULTA_ARC_OK && se, "open all-empty 7z");
        if (se) {
            CHECK(fulta_arc_editable(se) == FULTA_ARC_EDIT_OK, "all-empty 7z editable OK");
            fulta_arc_edit_t *ee = NULL;
            CHECK(fulta_arc_edit_begin(se, &ee) == FULTA_ARC_OK && ee, "edit_begin all-empty");
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(ee, FULTA_ARC_FORMAT_7Z, &cs, &wo);
            CHECK(ce == FULTA_ARC_OK, "copy-route all-empty (zero folders) write (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(ee);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *re = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &re) == FULTA_ARC_OK && re, "reopen all-empty copy");
                if (re) { CHECK(fulta_arc_count(re) == 2, "all-empty copy keeps 2 entries, got %zu", fulta_arc_count(re)); fulta_arc_close(re); }
            }
            free(cv.d);
            fulta_arc_close(se);
        }
        free(eb.d);
    }

    /* ---- 7z COPY route breadth: a real non-solid fixture (mixed methods) edited; copied entries stay verbatim ---- */
    {
        char p[512];
        snprintf(p, sizeof p, "%s/zip7z/archives/7z_copy.7z", t_fixtures());
        fulta_arc_t *fx = NULL;
        if (fulta_arc_open_file(p, NULL, &fx) == FULTA_ARC_OK && fx && fulta_arc_editable(fx) == FULTA_ARC_EDIT_OK) {
            size_t nc0 = fulta_arc_count(fx);
            size_t rem = SIZE_MAX;
            for (size_t i = 0; i < nc0; i++) if (!(fulta_arc_entry(fx, i)->flags & FULTA_ARC_ENTRY_DIR)) { rem = i; break; }
            fulta_arc_edit_t *fe = NULL;
            CHECK(fulta_arc_edit_begin(fx, &fe) == FULTA_ARC_OK && fe, "edit_begin fixture 7z copy");
            if (rem != SIZE_MAX) CHECK(fulta_arc_edit_remove(fe, rem) == FULTA_ARC_OK, "fixture remove one");
            rd_t rn3; fulta_arc_reader_t addn = mem_reader(N, sizeof N, &rn3);
            CHECK(fulta_arc_edit_add(fe, "fulta-added.bin", &addn) == FULTA_ARC_OK, "fixture add one");
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_err_t ce = fulta_arc_edit_write(fe, FULTA_ARC_FORMAT_7Z, &cs, &wo);
            CHECK(ce == FULTA_ARC_OK, "fixture 7z copy-route write (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(fe);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *r = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &r) == FULTA_ARC_OK && r, "reopen fixture copy");
                if (r) {
                    CHECK(fulta_arc_count(r) == nc0, "fixture copy count stays (%zu vs %zu)", fulta_arc_count(r), nc0);
                    for (size_t i = 0; i < nc0; i++) {
                        if (i == rem) continue;
                        const fulta_arc_entry_t *se = fulta_arc_entry(fx, i);
                        if (se->flags & FULTA_ARC_ENTRY_DIR) continue;
                        size_t sn = 0, zn = 0;
                        uint8_t *sd = extract_all(fx, i, &sn);
                        size_t zi = find(r, se->name);
                        uint8_t *zd = zi != SIZE_MAX ? extract_all(r, zi, &zn) : NULL;
                        CHECK(sd && zd && sn == zn && memcmp(sd, zd, sn) == 0, "fixture copied entry '%s' round-trips", se->name);
                        if (zi != SIZE_MAX)
                            CHECK(fulta_arc_entry(r, zi)->packed_size == se->packed_size,
                                  "fixture entry '%s' packed_size verbatim", se->name);
                        fulta_arc_free(sd); fulta_arc_free(zd);   /* extract_alloc blocks: not the C library's */
                    }
                    size_t ai = find(r, "fulta-added.bin");
                    if (ai != SIZE_MAX) { size_t n; uint8_t *d = extract_all(r, ai, &n);
                        CHECK(d && n == sizeof N && memcmp(d, N, n) == 0, "fixture added entry bytes"); fulta_arc_free(d); }
                    fulta_arc_close(r);
                }
            }
            free(cv.d);
        }
        if (fx) fulta_arc_close(fx);
    }

    /* ---- 7z COPY route honours the method selector on added entries (RFC-0004 step 7b): the added entry gets the
            chosen coder while copied entries keep theirs verbatim ---- */
    {
        char p[512];
        snprintf(p, sizeof p, "%s/zip7z/archives/7z_copy.7z", t_fixtures());
        fulta_arc_t *fx = NULL;
        if (fulta_arc_open_file(p, NULL, &fx) == FULTA_ARC_OK && fx && fulta_arc_editable(fx) == FULTA_ARC_EDIT_OK) {
            /* a copied entry's original coder, to prove it is unchanged */
            const char *orig_method = NULL;
            for (size_t i = 0; i < fulta_arc_count(fx); i++)
                if (!(fulta_arc_entry(fx, i)->flags & FULTA_ARC_ENTRY_DIR)) { orig_method = fulta_arc_entry(fx, i)->method; break; }
            fulta_arc_edit_t *fe = NULL;
            CHECK(fulta_arc_edit_begin(fx, &fe) == FULTA_ARC_OK && fe, "edit_begin 7z copy method");
            rd_t rn4; fulta_arc_reader_t addn = mem_reader(N, sizeof N, &rn4);
            CHECK(fulta_arc_edit_add(fe, "added-lzma2.bin", &addn) == FULTA_ARC_OK, "add lzma2 entry");
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_write_options_t mo = {.level = 6, .method = FULTA_ARC_METHOD_LZMA2};
            fulta_arc_err_t ce = fulta_arc_edit_write(fe, FULTA_ARC_FORMAT_7Z, &cs, &mo);
            CHECK(ce == FULTA_ARC_OK, "7z copy-route write with method=LZMA2 (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(fe);
            if (ce == FULTA_ARC_OK) {
                fulta_arc_t *r = NULL;
                CHECK(fulta_arc_open_memory(cv.d, cv.n, NULL, &r) == FULTA_ARC_OK && r, "reopen 7z copy method");
                if (r) {
                    size_t ai = find(r, "added-lzma2.bin");
                    CHECK(ai != SIZE_MAX && strcmp(fulta_arc_entry(r, ai)->method, "LZMA2") == 0,
                          "added entry uses LZMA2 (got %s)", ai != SIZE_MAX ? fulta_arc_entry(r, ai)->method : "(none)");
                    if (ai != SIZE_MAX) { size_t n; uint8_t *d = extract_all(r, ai, &n);
                        CHECK(d && n == sizeof N && memcmp(d, N, n) == 0, "added LZMA2 entry bytes"); fulta_arc_free(d); }
                    /* a copied entry keeps its original coder */
                    size_t ci = SIZE_MAX;
                    for (size_t i = 0; i < fulta_arc_count(r); i++)
                        if (!(fulta_arc_entry(r, i)->flags & FULTA_ARC_ENTRY_DIR) && strcmp(fulta_arc_entry(r, i)->name, "added-lzma2.bin") != 0) { ci = i; break; }
                    if (ci != SIZE_MAX && orig_method)
                        CHECK(strcmp(fulta_arc_entry(r, ci)->method, orig_method) == 0, "copied entry method unchanged (%s)", fulta_arc_entry(r, ci)->method);
                    fulta_arc_close(r);
                }
            }
            free(cv.d);
        }
        if (fx) fulta_arc_close(fx);
    }

    /* ---- convert to 7z: cancel from the progress callback returns CANCELLED (and leaves no spill behind) ---- */
    {
        fulta_arc_t *zin = NULL;
        CHECK(fulta_arc_open_memory(srcbuf.d, srcbuf.n, NULL, &zin) == FULTA_ARC_OK && zin, "open src for 7z cancel");
        if (zin) {
            fulta_arc_edit_t *ze = NULL;
            CHECK(fulta_arc_edit_begin(zin, &ze) == FULTA_ARC_OK && ze, "edit_begin 7z cancel");
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_write_options_t co = {.level = 6, .progress = cancel_cb, .progress_ctx = NULL};
            fulta_arc_err_t ce = fulta_arc_edit_write(ze, FULTA_ARC_FORMAT_7Z, &cs, &co);
            CHECK(ce == FULTA_ARC_ERR_CANCELLED, "7z convert cancels (got %s)", fulta_arc_strerror(ce));
            fulta_arc_edit_free(ze);
            free(cv.d);
            fulta_arc_close(zin);
        }
    }

    /* ---- encrypted output (D-016): every route that writes, with a password, re-encodes and encrypts ---- */
    {
        uint8_t X[70000];
        for (size_t i = 0; i < sizeof X; i++) X[i] = (uint8_t)((i * 2654435761u) >> 13);   /* little redundancy */
        const char *zn[] = {"a.txt", "b.txt", "c.txt"};
        const uint8_t *zd[] = {A, B, C};
        const size_t zs[] = {sizeof A, sizeof B, sizeof C};
        /* ZIP source (srcbuf) -> 7z: the streaming 7zAES path at LZMA and at Copy, and the buffered SOLID_OFF path */
        fulta_arc_write_options_t o7 = {.level = 6, .password = "Zee7 \xEB\xB9\x84\xEB\xB0\x80", .encryption = FULTA_ARC_ENCRYPT_7Z_AES256};
        enc_convert(in, FULTA_ARC_FORMAT_7Z, &o7, zn, zd, zs, 3, X, sizeof X, "enc zip->7z lzma");
        fulta_arc_write_options_t o70 = {.level = 0, .password = "store-pw"};
        enc_convert(in, FULTA_ARC_FORMAT_7Z, &o70, zn, zd, zs, 3, X, sizeof X, "enc zip->7z copy");
        fulta_arc_write_options_t o7s = {.level = 6, .password = "solid-off", .method = FULTA_ARC_METHOD_LZMA2,
                                         .solid = FULTA_ARC_SOLID_OFF};
        enc_convert(in, FULTA_ARC_FORMAT_7Z, &o7s, zn, zd, zs, 3, X, sizeof X, "enc zip->7z lzma2 solid-off");
        /* ZIP source -> ZIP: the same format with a password takes the convert route, at each AES strength */
        fulta_arc_write_options_t oz = {.level = 6, .password = "zip-pw", .encryption = FULTA_ARC_ENCRYPT_ZIP_AES256};
        enc_convert(in, FULTA_ARC_FORMAT_ZIP, &oz, zn, zd, zs, 3, X, sizeof X, "enc zip->zip aes256");
        oz.encryption = FULTA_ARC_ENCRYPT_ZIP_AES128;
        enc_convert(in, FULTA_ARC_FORMAT_ZIP, &oz, zn, zd, zs, 3, X, sizeof X, "enc zip->zip aes128");
        oz.encryption = FULTA_ARC_ENCRYPT_ZIP_AES192; oz.level = 0;
        enc_convert(in, FULTA_ARC_FORMAT_ZIP, &oz, zn, zd, zs, 3, X, sizeof X, "enc zip->zip aes192 store");
        /* a solid 7z source -> ZIP with the default scheme (AES-256) */
        fulta_arc_write_entry_t s7[] = {
            {.name = "x.txt", .data = A, .size = sizeof A, .mtime = 1500000000},
            {.name = "y.txt", .data = X, .size = sizeof X, .mtime = 1500000001},
        };
        cap_t s7buf = {0};
        fulta_arc_write_sink_t s7sink = {.ctx = &s7buf, .write = cap_write, .patch = cap_patch};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, s7, 2, &wo, &s7sink) == FULTA_ARC_OK, "enc: build source 7z");
        fulta_arc_t *z7 = NULL;
        if (fulta_arc_open_memory(s7buf.d, s7buf.n, NULL, &z7) == FULTA_ARC_OK && z7) {
            const char *sn[] = {"y.txt", "x.txt"};
            const uint8_t *sd[] = {X, A};
            const size_t ss[] = {sizeof X, sizeof A};
            fulta_arc_write_options_t od = {.level = 6, .password = "from-7z"};
            enc_convert(z7, FULTA_ARC_FORMAT_ZIP, &od, sn, sd, ss, 2, X, sizeof X, "enc 7z->zip default");
            fulta_arc_close(z7);
        }
        free(s7buf.d);

        /* schemes must match the format; BCJ2 with 7zAES stays unsupported */
        fulta_arc_edit_t *ed = NULL;
        CHECK(fulta_arc_edit_begin(in, &ed) == FULTA_ARC_OK && ed, "enc: edit_begin for refusals");
        if (ed) {
            cap_t cv = {0};
            fulta_arc_write_sink_t cs = {.ctx = &cv, .write = cap_write, .patch = cap_patch};
            fulta_arc_write_options_t bad7 = {.level = 6, .password = "p", .encryption = FULTA_ARC_ENCRYPT_ZIP_AES128};
            CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_7Z, &cs, &bad7) == FULTA_ARC_ERR_INVALID_ARG, "enc: ZIP scheme on 7z -> INVALID_ARG");
            cv.n = 0;
            fulta_arc_write_options_t badz = {.level = 6, .password = "p", .encryption = FULTA_ARC_ENCRYPT_7Z_AES256};
            CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &cs, &badz) == FULTA_ARC_ERR_INVALID_ARG, "enc: 7z scheme on ZIP -> INVALID_ARG");
            cv.n = 0;
            fulta_arc_write_options_t bcj2 = {.level = 6, .password = "p", .filter = FULTA_ARC_FILTER_BCJ2};
            CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_7Z, &cs, &bcj2) == FULTA_ARC_ERR_UNSUPPORTED, "enc: BCJ2 + 7zAES -> UNSUPPORTED");
            free(cv.d);
            fulta_arc_edit_free(ed);
        }

        /* to a file: the spill leaves no name behind, during the write (POSIX) or after it */
        CHECK(fulta_arc_edit_begin(in, &ed) == FULTA_ARC_OK && ed, "enc: edit_begin for file");
        if (ed) {
            const char *fpath = "_test_edit_enc.7z";
            fulta_arc_write_options_t of = {.level = 6, .password = "file-pw"};
#ifndef _WIN32
            of.progress = spill_probe; of.progress_ctx = (void *)fpath;
            g_spill_seen = -1;
#endif
            fulta_arc_err_t fe = fulta_arc_edit_write_file(ed, FULTA_ARC_FORMAT_7Z, fpath, &of);
            CHECK(fe == FULTA_ARC_OK, "enc: edit_write_file 7z (got %s)", fulta_arc_strerror(fe));
            fulta_arc_edit_free(ed);
#ifndef _WIN32
            CHECK(g_spill_seen == 0, "enc: no spill name visible while converting (%d)", g_spill_seen);
            g_spill_seen = -1;
            spill_probe((void *)fpath, 0, 0);
            CHECK(g_spill_seen == 0, "enc: no spill name left after the write");
#endif
            fulta_arc_options_t rp = {.password = pw_cb};
            fulta_arc_t *ff = NULL;
            g_pw = "file-pw";
            CHECK(fulta_arc_open_file(fpath, &rp, &ff) == FULTA_ARC_OK && ff, "enc: reopen written 7z");
            if (ff) {
                size_t k = find(ff, "b.txt"), n; uint8_t *d = k != SIZE_MAX ? extract_all(ff, k, &n) : NULL;
                CHECK(d && n == sizeof B && memcmp(d, B, n) == 0, "enc: written 7z b.txt bytes"); fulta_arc_free(d);
                fulta_arc_close(ff);
            }
            g_pw = NULL;
            remove(fpath);
        }
    }

    zip_methods();

    /* many removals: removal marks the plan slot (O(1)); the writer sees the survivors once, in order */
    {
        enum { NM = 3000 };
        static fulta_arc_write_entry_t me[NM];
        static char mnames[NM][16];
        static uint8_t mdata[NM];
        for (int i = 0; i < NM; i++) {
            snprintf(mnames[i], sizeof mnames[i], "f%05d", i);
            mdata[i] = (uint8_t)i;
            me[i] = (fulta_arc_write_entry_t){.name = mnames[i], .data = &mdata[i], .size = 1};
        }
        cap_t mz = {0};
        fulta_arc_write_sink_t mk = {.ctx = &mz, .write = cap_write, .patch = cap_patch};
        fulta_arc_write_options_t mo = {.level = 1};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, me, NM, &mo, &mk) == FULTA_ARC_OK, "many: source");
        fulta_arc_t *ma = NULL;
        CHECK(fulta_arc_open_memory(mz.d, mz.n, NULL, &ma) == FULTA_ARC_OK && ma, "many: open");
        fulta_arc_edit_t *med = NULL;
        if (ma && fulta_arc_edit_begin(ma, &med) == FULTA_ARC_OK) {
            bool ok = true;
            for (int i = 0; i < NM; i += 2) ok = ok && fulta_arc_edit_remove(med, (size_t)i) == FULTA_ARC_OK;
            CHECK(ok, "many: remove every other entry");
            CHECK(fulta_arc_edit_remove(med, 0) == FULTA_ARC_ERR_INVALID_ARG, "many: a removed entry cannot be removed again");
            CHECK(fulta_arc_edit_replace(med, 2, "renamed", NULL) == FULTA_ARC_ERR_INVALID_ARG, "many: nor renamed");
            CHECK(fulta_arc_edit_replace(med, 1, "renamed", NULL) == FULTA_ARC_OK, "many: a survivor can be renamed");
            cap_t mo2 = {0};
            fulta_arc_write_sink_t mk2 = {.ctx = &mo2, .write = cap_write, .patch = cap_patch};
            CHECK(fulta_arc_edit_write(med, FULTA_ARC_FORMAT_ZIP, &mk2, &mo) == FULTA_ARC_OK, "many: write");
            fulta_arc_edit_free(med);
            fulta_arc_t *mb = NULL;
            CHECK(fulta_arc_open_memory(mo2.d, mo2.n, NULL, &mb) == FULTA_ARC_OK && mb, "many: reopen");
            if (mb) {
                CHECK(fulta_arc_count(mb) == NM / 2, "many: %d left, got %zu", NM / 2, fulta_arc_count(mb));
                bool order = fulta_arc_count(mb) == NM / 2 && strcmp(fulta_arc_entry(mb, 0)->name, "renamed") == 0;
                for (size_t i = 1; order && i < fulta_arc_count(mb); i++) {
                    char want[16]; snprintf(want, sizeof want, "f%05zu", 2 * i + 1);
                    order = strcmp(fulta_arc_entry(mb, i)->name, want) == 0;
                }
                CHECK(order, "many: the survivors in their order");
                fulta_arc_close(mb);
            }
            free(mo2.d);
        }
        fulta_arc_close(ma);
        free(mz.d);
    }

    fulta_arc_close(in);
    free(srcbuf.d); free(out.d);
    return t_report("test_edit");
}
