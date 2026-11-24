/* test_alloc.c - the per-archive codec memory budget (fulta_arc_options.max_memory).
 *
 * Writes a 7z archive with each heap-hungry codec (LZMA, LZMA2, PPMd), then reads it back three ways:
 *   - max_memory = 0 (unlimited): extracts, bytes match;
 *   - max_memory = large: extracts, bytes match (the budget does not disturb a legitimate decode);
 *   - max_memory = 1 (no codec allocation allowed): the decode is refused with FULTA_ARC_ERR_LIMIT, not NOMEM,
 *     and nothing crashes or leaks (run under ASAN/UBSAN).
 * The budget wraps the vendored codecs' own buffers (PPMd models, LZMA state/window), which are where an archive's
 * declared sizes turn into real allocation. See D-021. */
#include "fulta/arc.h"
#include "testlib.h"

#include "core/internal.h"          /* fa_budget_t, fa_budget_as_allocator, FA_ALIGN */
#include "proven/alloc_check.h"     /* proven's own wrong-allocator / double-free / leak checker */
#include "proven/heap.h"
#include "proven/panic.h"

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

/* Extract entry 0 with the given budget; report the error and (on success) whether the bytes match `want`. */
static fulta_arc_err_t extract_with_budget(const unsigned char *arcbytes, size_t arclen, uint64_t max_memory,
                                           const unsigned char *want, size_t wantlen, int *bytes_ok) {
    *bytes_ok = 0;
    fulta_arc_options_t opt = {.max_memory = max_memory};
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(arcbytes, arclen, &opt, &arc);
    if (e) return e;
    void *d = NULL;
    size_t dn = 0;
    e = fulta_arc_extract_alloc(arc, 0, &d, &dn);
    if (!e) *bytes_ok = (dn == wantlen && (wantlen == 0 || memcmp(d, want, wantlen) == 0));
    fulta_arc_free(d);   /* public contract: extract_alloc memory (default allocator) is released with fulta_arc_free */
    fulta_arc_close(arc);
    return e;
}

/* The budget as a proven allocator, exercised behind proven's own alloc_check wrapper. alloc_check records every
 * block its inner allocator hands out and panics on a double free, a foreign pointer, or a wrong old_size/align --
 * exactly the wrong-allocator bugs the budget's size-prefix scheme could introduce. A correct budget shows zero
 * panics, zero alloc_check faults, zero live blocks at the end, and its own live-byte counter back at 0. */
static int t_panics;
static void count_panic(const char *m) { (void)m; t_panics++; }

/* End-to-end proof that a codec's bridge pairs every allocation with a matching free through the SAME hook: route
 * the archive's budget backing through proven's alloc_check, extract entry 0, then CLOSE, and only then assert no
 * faults and no live blocks. (Assert after close, not after extract: a solid-folder decoder may legitimately keep
 * its buffers live between an archive's entries, until close.) This catches a block freed via the wrong path even
 * when the heap would tolerate it -- which a bytes-match test cannot. Runs for every codec, re-proving the LZMA
 * bridge too. max_memory stays 0 (unlimited) so the decode runs to completion. */
static void extract_checked(const unsigned char *arcbytes, size_t arclen, const char *label) {
    static proven_alloc_check_entry_t rec[128];
    proven_alloc_check_t chk = {0};
    int panics0 = t_panics;
    proven_set_panic_handler(count_panic);

    fulta_arc_options_t opt = {.max_memory = 0};
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(arcbytes, arclen, &opt, &arc);
    CHECK(e == FULTA_ARC_OK, "%s: open for alloc_check (%s)", label, fulta_arc_strerror(e));
    if (e) { proven_set_panic_handler(NULL); return; }

    /* swap the codec budget's backing for a checked one AFTER open (open's own header decode has already run and
     * freed on the plain backing); from here every codec block is recorded by alloc_check. */
    arc->budget.backing = proven_alloc_check_wrap(&chk, proven_heap_allocator(), rec, 128);
    void *d = NULL;
    size_t dn = 0;
    e = fulta_arc_extract_alloc(arc, 0, &d, &dn);
    CHECK(e == FULTA_ARC_OK, "%s: checked extract (%s)", label, fulta_arc_strerror(e));
    fulta_arc_free(d);
    fulta_arc_close(arc);   /* all codec blocks must be freed by now */

    CHECK(chk.faults == 0, "%s: alloc_check no faults (double-free/foreign/old_size)", label);
    CHECK(proven_alloc_check_live(&chk) == 0, "%s: alloc_check no leaked codec blocks", label);
    CHECK(t_panics == panics0, "%s: no panics during checked extract", label);
    proven_set_panic_handler(NULL);
}

/* A caller-supplied allocator (increment 3): counts what the codec budget routes through it. The budget asks for
 * FA_ALIGN-aligned blocks; malloc already meets that, so align is ignored here. Only alloc/free are exercised by the
 * codec-budget path (realloc completes the public contract but the budget reallocs by alloc+copy+free). */
typedef struct { int allocs, frees; } counter_t;
static void *ca_alloc(void *ctx, size_t size, size_t align) {
    (void)align;
    counter_t *c = ctx;
    void *p = malloc(size);
    if (p) c->allocs++;
    return p;
}
static void *ca_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align) {
    (void)old_size; (void)align;
    counter_t *c = ctx;
    void *p = realloc(ptr, new_size);
    if (p && !ptr) c->allocs++;
    return p;
}
static void ca_free(void *ctx, void *ptr) {
    counter_t *c = ctx;
    if (ptr) c->frees++;
    free(ptr);
}

/* A caller allocator actually backs the codec buffers and every block it hands out comes back to it. */
static void test_user_allocator(const unsigned char *arcbytes, size_t arclen,
                                const unsigned char *want, size_t wantlen) {
    counter_t c = {0};
    fulta_arc_options_t opt = {
        .max_memory = (uint64_t)256 << 20,
        .allocator = {.alloc = ca_alloc, .realloc = ca_realloc, .free = ca_free, .ctx = &c},
    };
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(arcbytes, arclen, &opt, &arc);
    CHECK(e == FULTA_ARC_OK, "user-alloc: open (%s)", fulta_arc_strerror(e));
    if (e) return;
    void *d = NULL;
    size_t dn = 0;
    e = fulta_arc_extract_alloc(arc, 0, &d, &dn);
    CHECK(e == FULTA_ARC_OK, "user-alloc: extract (%s)", fulta_arc_strerror(e));
    CHECK(e == FULTA_ARC_OK && dn == wantlen && memcmp(d, want, wantlen) == 0, "user-alloc: bytes match");
    CHECK(c.allocs > 0, "user-alloc: custom allocator observed in use (%d allocs)", c.allocs);
    ca_free(&c, d);   /* the block came from the caller's allocator: released through it */
    fulta_arc_close(arc);
    CHECK(c.allocs == c.frees, "user-alloc: every block freed through it (%d alloc / %d free)", c.allocs, c.frees);
}

/* Supplying one or two of the three allocator functions is a caller bug, refused at open. */
static void test_partial_allocator(const unsigned char *arcbytes, size_t arclen) {
    fulta_arc_options_t opt = {.allocator = {.alloc = ca_alloc}};   /* realloc + free left NULL */
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(arcbytes, arclen, &opt, &arc);
    CHECK(e == FULTA_ARC_ERR_INVALID_ARG, "partial allocator refused at open (%s)", fulta_arc_strerror(e));
    if (!e) fulta_arc_close(arc);
}

/* The sticky `exceeded` flag is re-armed on each extract (D-021 increment-2 fix): a budget trip on one extract must
 * not mislabel the next. Poison the flag by hand, then a legitimate extract must both succeed and clear it. */
static void test_exceeded_reset(const unsigned char *arcbytes, size_t arclen) {
    fulta_arc_options_t opt = {.max_memory = (uint64_t)256 << 20};
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(arcbytes, arclen, &opt, &arc);
    CHECK(e == FULTA_ARC_OK, "reset: open (%s)", fulta_arc_strerror(e));
    if (e) return;
    arc->budget.exceeded = true;   /* simulate a prior extract's budget trip */
    void *d = NULL;
    size_t dn = 0;
    e = fulta_arc_extract_alloc(arc, 0, &d, &dn);
    CHECK(e == FULTA_ARC_OK, "reset: extract after a prior trip still succeeds (%s)", fulta_arc_strerror(e));
    CHECK(!arc->budget.exceeded, "reset: extract re-armed the budget (exceeded cleared)");
    fulta_arc_free(d);
    fulta_arc_close(arc);
}

static void test_budget_trait(void) {
    proven_set_panic_handler(count_panic);
    static proven_alloc_check_entry_t rec[64];
    proven_alloc_check_t chk = {0};
    proven_allocator_t checked = proven_alloc_check_wrap(&chk, proven_heap_allocator(), rec, 64);
    CHECK(proven_alloc_is_valid(checked), "alloc_check wrap is valid");

    fa_budget_t b = {.backing = checked, .limit = 0};
    proven_allocator_t a = fa_budget_as_allocator(&b);

    void *p[8] = {0};
    for (int i = 0; i < 8; i++) {
        proven_result_mem_mut_t r = a.alloc_fn(a.ctx, (size_t)64 << i, FA_ALIGN);
        CHECK(r.err == PROVEN_OK, "budget alloc #%d", i);
        p[i] = r.err == PROVEN_OK ? r.value.ptr : NULL;
    }
    CHECK(b.live > 0, "live bytes counted");
    CHECK(chk.live == 8, "alloc_check recorded all 8 (the checked backing is really in use)");
    /* the completeness path: realloc grows a block (budget: alloc new, copy, free old) */
    proven_result_mem_mut_t rr = a.realloc_fn(a.ctx, p[0], 64, 8192, FA_ALIGN);
    CHECK(rr.err == PROVEN_OK, "budget realloc");
    if (rr.err == PROVEN_OK) p[0] = rr.value.ptr;
    for (int i = 0; i < 8; i++) a.free_fn(a.ctx, p[i]);

    CHECK(b.live == 0, "budget live counter back to 0 after all frees");
    CHECK(chk.faults == 0, "alloc_check: no wrong-allocator / double-free / old_size faults");
    CHECK(proven_alloc_check_live(&chk) == 0, "alloc_check: no leaked blocks");
    CHECK(t_panics == 0, "no panics");

    /* a limit refuses once passed, and the refusal is attributable to the budget (exceeded), not a backing OOM */
    fa_budget_t lb = {.backing = proven_heap_allocator(), .limit = 4096};
    proven_allocator_t la = fa_budget_as_allocator(&lb);
    proven_result_mem_mut_t ok = la.alloc_fn(la.ctx, 1024, FA_ALIGN);
    CHECK(ok.err == PROVEN_OK, "within-limit alloc ok");
    proven_result_mem_mut_t no = la.alloc_fn(la.ctx, 1u << 20, FA_ALIGN);
    CHECK(no.err == PROVEN_ERR_NOMEM && lb.exceeded, "over-limit alloc refused with exceeded set");
    la.free_fn(la.ctx, ok.value.ptr);
    CHECK(lb.live == 0, "limited budget live back to 0");

    proven_set_panic_handler(NULL);
}

int main(void) {
    test_budget_trait();

    /* A compressible-but-nontrivial payload, big enough that each codec really allocates its model/window. */
    static unsigned char payload[131072];
    for (size_t i = 0; i < sizeof payload; i++)
        payload[i] = (unsigned char)((i * 31 + (i >> 3)) ^ (i >> 7));

    fulta_arc_write_entry_t en[] = {
        {.name = "data.bin", .data = payload, .size = sizeof payload, .mtime = 1700000000},
    };

    const fulta_arc_method_t methods[] = {FULTA_ARC_METHOD_LZMA, FULTA_ARC_METHOD_LZMA2,
                                          FULTA_ARC_METHOD_PPMD, FULTA_ARC_METHOD_BZIP2};
    const char *mnames[] = {"LZMA", "LZMA2", "PPMD", "BZIP2"};

    for (size_t mi = 0; mi < sizeof methods / sizeof *methods; mi++) {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t wo = {.level = 6, .method = methods[mi]};
        fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_7Z, en, 1, &wo, &sink);
        CHECK(e == FULTA_ARC_OK, "%s: write %s", mnames[mi], fulta_arc_strerror(e));
        if (e) { free(m.p); continue; }

        int ok = 0;

        /* 1. unlimited: extracts and matches */
        e = extract_with_budget(m.p, m.n, 0, payload, sizeof payload, &ok);
        CHECK(e == FULTA_ARC_OK, "%s: unlimited extract %s", mnames[mi], fulta_arc_strerror(e));
        CHECK(ok, "%s: unlimited bytes match", mnames[mi]);

        /* 2. a generous budget: still extracts and matches (the budget must not disturb a legitimate decode) */
        e = extract_with_budget(m.p, m.n, (uint64_t)256 << 20, payload, sizeof payload, &ok);
        CHECK(e == FULTA_ARC_OK, "%s: 256 MiB extract %s", mnames[mi], fulta_arc_strerror(e));
        CHECK(ok, "%s: 256 MiB bytes match", mnames[mi]);

        /* 3. no codec allocation allowed: refused with LIMIT (the budget's doing), not NOMEM; no crash/leak */
        e = extract_with_budget(m.p, m.n, 1, payload, sizeof payload, &ok);
        CHECK(e == FULTA_ARC_ERR_LIMIT, "%s: tiny budget -> LIMIT, got %s", mnames[mi], fulta_arc_strerror(e));
        CHECK(!ok, "%s: tiny budget yields no bytes", mnames[mi]);

        /* 4. the codec's alloc/free hook pairs correctly (proven's own alloc_check) */
        extract_checked(m.p, m.n, mnames[mi]);

        /* 5. (once, on the LZMA 7z) the sticky-exceeded reset and the caller-supplied allocator */
        if (mi == 0) {
            test_exceeded_reset(m.p, m.n);
            test_user_allocator(m.p, m.n, payload, sizeof payload);
            test_partial_allocator(m.p, m.n);
        }

        free(m.p);
    }

    /* zstd is a ZIP method (93), not a stock-7z coder, so exercise its bridge through a ZIP archive. */
    {
        mem_t m = {0};
        fulta_arc_write_sink_t sink = {.ctx = &m, .write = m_write, .patch = m_patch};
        fulta_arc_write_options_t wo = {.level = 6, .method = FULTA_ARC_METHOD_ZSTD};
        fulta_arc_err_t e = fulta_arc_write(FULTA_ARC_FORMAT_ZIP, en, 1, &wo, &sink);
        CHECK(e == FULTA_ARC_OK, "ZSTD(zip): write %s", fulta_arc_strerror(e));
        if (!e) {
            int ok = 0;
            e = extract_with_budget(m.p, m.n, 0, payload, sizeof payload, &ok);
            CHECK(e == FULTA_ARC_OK, "ZSTD(zip): unlimited extract %s", fulta_arc_strerror(e));
            CHECK(ok, "ZSTD(zip): unlimited bytes match");
            e = extract_with_budget(m.p, m.n, (uint64_t)256 << 20, payload, sizeof payload, &ok);
            CHECK(e == FULTA_ARC_OK, "ZSTD(zip): 256 MiB extract %s", fulta_arc_strerror(e));
            CHECK(ok, "ZSTD(zip): 256 MiB bytes match");
            e = extract_with_budget(m.p, m.n, 1, payload, sizeof payload, &ok);
            CHECK(e == FULTA_ARC_ERR_LIMIT, "ZSTD(zip): tiny budget -> LIMIT, got %s", fulta_arc_strerror(e));
            extract_checked(m.p, m.n, "ZSTD(zip)");
        }
        free(m.p);
    }

    return t_report("test_alloc");
}
