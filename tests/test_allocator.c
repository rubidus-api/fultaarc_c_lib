/* test_allocator.c - D-038: writing, editing and converting allocate only through the caller's allocator. The
 * allocator is proven's alloc_check in front of its heap, so every block must be one of its own, resized with its
 * true old size and released once; after each operation nothing may be left. Covers ZIP (every method, AES), 7z
 * (every method, the filters, BCJ2, SOLID_OFF, 7zAES, encrypted names), the ZIP and 7z copy and convert routes
 * (the LZMA and 7z spills) and split output. Reading under the same allocator is covered by test_manifests. */
#include "fulta/arc.h"
#include "testlib.h"

#include "proven/alloc_check.h"
#include "proven/heap.h"
#include "proven/panic.h"

#include <stdint.h>

static proven_alloc_check_t g_chk;
static proven_allocator_t g_checked;
static int g_panics;
static void count_panic(const char *msg) {
    if (g_panics++ < 5) fprintf(stderr, "alloc_check: %s\n", msg);
}
static unsigned long g_allocs;   /* blocks handed out by the caller's allocator: proof it is the one in use */
static void *ua_alloc(void *ctx, size_t size, size_t align) {
    (void)ctx;
    g_allocs++;
    proven_result_mem_mut_t r = g_checked.alloc_fn(g_checked.ctx, size, align);
    return r.err ? NULL : r.value.ptr;
}
static void *ua_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align) {
    (void)ctx;
    proven_result_mem_mut_t r = g_checked.realloc_fn(g_checked.ctx, ptr, old_size, new_size, align);
    return r.err ? NULL : r.value.ptr;
}
static void ua_free(void *ctx, void *ptr) { (void)ctx; g_checked.free_fn(g_checked.ctx, ptr); }
static const fulta_arc_allocator_t UA = {.alloc = ua_alloc, .realloc = ua_realloc, .free = ua_free};

/* the operation left the allocator as it found it: no fault, no live block; and it did use it */
static unsigned long g_allocs0;
static void clean(const char *label, size_t faults0) {
    CHECK(g_allocs > g_allocs0, "%s: the caller's allocator was used", label);
    g_allocs0 = g_allocs;
    CHECK(g_chk.faults == faults0, "%s: allocator faults %zu", label, (size_t)(g_chk.faults - faults0));
    CHECK(proven_alloc_check_live(&g_chk) == 0, "%s: %zu blocks left", label, (size_t)proven_alloc_check_live(&g_chk));
}

/* a growing sink whose own memory is the test's (plain C library), so only FultaArc's blocks are counted */
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

typedef struct { const uint8_t *d; uint64_t n, pos; } rd_t;
static fulta_arc_err_t rd_read(void *ctx, void *buf, size_t cap, size_t *got) {
    rd_t *r = ctx;
    uint64_t left = r->n - r->pos;
    size_t k = left < cap ? (size_t)left : cap;
    memcpy(buf, r->d + r->pos, k); r->pos += k; *got = k; return FULTA_ARC_OK;
}

static const char *g_pw;
static fulta_arc_err_t pw_cb(void *ctx, uint32_t a, const char **o) {
    (void)ctx; if (a || !g_pw) return FULTA_ARC_ERR_PASSWORD_NEEDED; *o = g_pw; return FULTA_ARC_OK;
}

/* the written archive reopens (with the same allocator) and every file has its bytes */
static void verify(const cap_t *c, const char *pw, const fulta_arc_write_entry_t *es, size_t n, const char *label) {
    fulta_arc_options_t o = {.allocator = UA, .password = pw_cb};
    g_pw = pw;
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(c->d, c->n, &o, &a);
    CHECK(e == FULTA_ARC_OK && a, "%s: reopen (%s)", label, fulta_arc_strerror(e));
    if (!a) return;
    for (size_t i = 0; i < n; i++) {
        if (es[i].is_dir) continue;
        size_t k = SIZE_MAX;
        for (size_t j = 0; j < fulta_arc_count(a); j++)
            if (strcmp(fulta_arc_entry(a, j)->name, es[i].name) == 0) k = j;
        CHECK(k != SIZE_MAX, "%s: %s listed", label, es[i].name);
        if (k == SIZE_MAX) continue;
        void *d = NULL; size_t dn = 0;
        e = fulta_arc_extract_alloc(a, k, &d, &dn);
        CHECK(e == FULTA_ARC_OK && dn == es[i].size && (dn == 0 || memcmp(d, es[i].data, dn) == 0), "%s: %s bytes (%s)",
              label, es[i].name, fulta_arc_strerror(e));
        ua_free(NULL, d);   /* the block came from our allocator */
    }
    fulta_arc_close(a);
}

int main(void) {
    static proven_alloc_check_entry_t rec[1 << 16];
    proven_set_panic_handler(count_panic);
    g_checked = proven_alloc_check_wrap(&g_chk, proven_heap_allocator(), rec, sizeof rec / sizeof rec[0]);

    size_t tn = 150000, rn = 40000;
    uint8_t *T = malloc(tn), *R = malloc(rn);
    if (!T || !R) return 1;
    uint32_t x = 99;
    for (size_t i = 0; i < tn; i++) { x = x * 1103515245u + 12345u; T[i] = (uint8_t)("etaoin shrdlu\n\xe8"[(x >> 16) % 15]); }
    for (size_t i = 0; i < rn; i++) { x = x * 1103515245u + 12345u; R[i] = (uint8_t)(x >> 24); }
    fulta_arc_write_entry_t es[] = {
        {.name = "a/text.txt", .data = T, .size = tn, .mtime = 1600000000},
        {.name = "a/rand.bin", .data = R, .size = rn, .mtime = 1600000001},
        {.name = "a/empty", .data = NULL, .size = 0},
        {.name = "a/sub/", .is_dir = 1},
    };
    const size_t ne = sizeof es / sizeof es[0];

    /* ---- fulta_arc_write ---- */
    static const fulta_arc_method_t zm[] = {FULTA_ARC_METHOD_STORE, FULTA_ARC_METHOD_DEFLATE, FULTA_ARC_METHOD_BZIP2,
                                            FULTA_ARC_METHOD_LZMA, FULTA_ARC_METHOD_PPMD, FULTA_ARC_METHOD_ZSTD};
    for (size_t i = 0; i < sizeof zm / sizeof zm[0]; i++)
        for (int enc = 0; enc < 2; enc++) {
            char label[64]; snprintf(label, sizeof label, "write zip m%d%s", (int)zm[i], enc ? " aes" : "");
            size_t f0 = g_chk.faults;
            cap_t c = {0};
            fulta_arc_write_sink_t sk = {.ctx = &c, .write = cap_write, .patch = cap_patch};
            fulta_arc_write_options_t o = {.level = 6, .method = zm[i], .password = enc ? "pw" : NULL, .allocator = UA};
            CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, es, ne, &o, &sk) == FULTA_ARC_OK, "%s: write", label);
            clean(label, f0);
            verify(&c, enc ? "pw" : NULL, es, ne, label);
            clean(label, f0);
            free(c.d);
        }
    struct { fulta_arc_method_t m; fulta_arc_filter_t f; fulta_arc_solid_t s; const char *pw; int names; } sz[] = {
        {FULTA_ARC_METHOD_DEFAULT, FULTA_ARC_FILTER_NONE, FULTA_ARC_SOLID_DEFAULT, NULL, 0},
        {FULTA_ARC_METHOD_LZMA2, FULTA_ARC_FILTER_NONE, FULTA_ARC_SOLID_OFF, NULL, 0},
        {FULTA_ARC_METHOD_PPMD, FULTA_ARC_FILTER_NONE, FULTA_ARC_SOLID_DEFAULT, NULL, 0},
        {FULTA_ARC_METHOD_BZIP2, FULTA_ARC_FILTER_DELTA, FULTA_ARC_SOLID_DEFAULT, NULL, 0},
        {FULTA_ARC_METHOD_DEFLATE, FULTA_ARC_FILTER_BCJ_X86, FULTA_ARC_SOLID_DEFAULT, NULL, 0},
        {FULTA_ARC_METHOD_LZMA, FULTA_ARC_FILTER_BCJ2, FULTA_ARC_SOLID_DEFAULT, NULL, 0},
        {FULTA_ARC_METHOD_STORE, FULTA_ARC_FILTER_RISCV, FULTA_ARC_SOLID_DEFAULT, NULL, 0},
        {FULTA_ARC_METHOD_LZMA2, FULTA_ARC_FILTER_NONE, FULTA_ARC_SOLID_DEFAULT, "pw7", 1},
        {FULTA_ARC_METHOD_LZMA, FULTA_ARC_FILTER_NONE, FULTA_ARC_SOLID_OFF, "pw7", 0},
    };
    for (size_t i = 0; i < sizeof sz / sizeof sz[0]; i++) {
        char label[64]; snprintf(label, sizeof label, "write 7z #%zu", i);
        size_t f0 = g_chk.faults;
        cap_t c = {0};
        fulta_arc_write_sink_t sk = {.ctx = &c, .write = cap_write, .patch = cap_patch};
        fulta_arc_write_options_t o = {.level = 5, .method = sz[i].m, .filter = sz[i].f, .filter_delta_dist = 2,
                                       .solid = sz[i].s, .password = sz[i].pw, .encrypt_names = sz[i].names,
                                       .allocator = UA};
        fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_7Z, es, ne, &o, &sk);
        CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
        clean(label, f0);
        if (!e) verify(&c, sz[i].pw, es, ne, label);
        clean(label, f0);
        free(c.d);
    }

    /* ---- edit: the copy route (an editable source) and the convert route (a solid source), with the archive and the
     * writer both on the checking allocator ---- */
    for (int src7 = 0; src7 < 2; src7++)
        for (int tgt7 = 0; tgt7 < 2; tgt7++)
            for (int enc = 0; enc < 2; enc++) {
                char label[64];
                snprintf(label, sizeof label, "edit %s->%s%s", src7 ? "7z" : "zip", tgt7 ? "7z" : "zip", enc ? " aes" : "");
                size_t f0 = g_chk.faults;
                cap_t s = {0};
                fulta_arc_write_sink_t ssk = {.ctx = &s, .write = cap_write, .patch = cap_patch};
                fulta_arc_write_options_t wo = {.level = 5, .allocator = UA};
                CHECK(fulta_arc_write(src7 ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP, es, ne, &wo, &ssk) == FULTA_ARC_OK,
                      "%s: source", label);
                fulta_arc_options_t ro = {.allocator = UA};
                fulta_arc_t *a = NULL;
                CHECK(fulta_arc_open_memory(s.d, s.n, &ro, &a) == FULTA_ARC_OK && a, "%s: open source", label);
                if (a) {
                    fulta_arc_edit_t *ed = NULL;
                    CHECK(fulta_arc_edit_begin(a, &ed) == FULTA_ARC_OK && ed, "%s: begin", label);
                    rd_t r1 = {T, 5000, 0}, r2 = {R, rn, 0};
                    fulta_arc_reader_t add1 = {.ctx = &r1, .read = rd_read, .size = 5000};
                    fulta_arc_reader_t add2 = {.ctx = &r2, .read = rd_read, .size = UINT64_MAX};
                    if (ed) {
                        CHECK(fulta_arc_edit_remove(ed, 0) == FULTA_ARC_OK, "%s: remove", label);
                        CHECK(fulta_arc_edit_add(ed, "new/one.txt", &add1) == FULTA_ARC_OK, "%s: add", label);
                        CHECK(fulta_arc_edit_add(ed, "new/two.bin", &add2) == FULTA_ARC_OK, "%s: add unsized", label);
                        cap_t c = {0};
                        fulta_arc_write_sink_t sk = {.ctx = &c, .write = cap_write, .patch = cap_patch};
                        fulta_arc_write_options_t o = {.level = 5, .password = enc ? "pe" : NULL, .allocator = UA,
                                                       .method = tgt7 ? FULTA_ARC_METHOD_DEFAULT : FULTA_ARC_METHOD_LZMA};
                        fulta_arc_err_t e = fulta_arc_edit_write(ed, tgt7 ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP, &sk, &o);
                        CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
                        fulta_arc_edit_free(ed);
                        fulta_arc_write_entry_t want[] = {
                            {.name = "a/rand.bin", .data = R, .size = rn},
                            {.name = "new/one.txt", .data = T, .size = 5000},
                            {.name = "new/two.bin", .data = R, .size = rn},
                        };
                        if (!e) verify(&c, enc ? "pe" : NULL, want, 3, label);
                        free(c.d);
                    }
                    fulta_arc_close(a);
                }
                clean(label, f0);
                free(s.d);
            }

    /* ---- split output to files, both formats ---- */
    for (int f = 0; f < 2; f++) {
        char label[32]; snprintf(label, sizeof label, "split %s", f ? "7z" : "zip");
        size_t f0 = g_chk.faults;
        const char *path = f ? "test_allocator_out.7z" : "test_allocator_out.zip";
        fulta_arc_write_options_t o = {.level = 5, .volume_size = 65536, .allocator = UA};
        fulta_arc_err_t e = fulta_arc_write_file(path, f ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP, es, ne, &o);
        CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
        clean(label, f0);
        char vname[96];
        for (uint32_t i = 0; i < 32; i++)
            if (!fulta_arc_volume_name(f ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP, path, i, 1000, vname, sizeof vname))
                remove(vname);
        remove(path);
    }

    /* a partial allocator is refused by the writer too */
    {
        cap_t c = {0};
        fulta_arc_write_sink_t sk = {.ctx = &c, .write = cap_write, .patch = cap_patch};
        fulta_arc_write_options_t o = {.level = 5, .allocator = {.alloc = ua_alloc}};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, es, ne, &o, &sk) == FULTA_ARC_ERR_INVALID_ARG,
              "partial writer allocator -> INVALID_ARG");
        free(c.d);
    }

    CHECK(g_panics == 0, "no alloc_check panics (%d)", g_panics);
    free(T); free(R);
    return t_report("test_allocator");
}
