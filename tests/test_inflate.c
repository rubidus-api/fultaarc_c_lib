/* test_inflate.c - FultaArc's Deflate decoder against miniz's encoder, read in many sizes (D-045).
 *
 * The decoder's hot loop writes to the caller's buffer only and copies a match from it, or from the 64 KiB window
 * for bytes of an earlier call; the window is brought up to date when the loop ends. These cases pin that: inputs
 * larger than the window, distances that reach back across read calls, overlapping runs (distance 1..7), every
 * compression level's block mix, and read sizes from 1 byte (never the hot loop) to whole megabytes, fixed and
 * varying. Each decode is compared with the input byte for byte. */
#include "core/internal.h"
#define MINIZ_NO_STDIO               /* as the library builds miniz (Makefile MINIZ_DEF) */
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include "testlib.h"

#include <stdint.h>

static uint32_t rng = 20261112;
static uint32_t next(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static unsigned char *make(int kind, size_t n) {
    unsigned char *p = malloc(n);
    static const char *w[] = {"window ", "block ", "match ", "literal ", "distance ", "length ", "tree ", "code "};
    size_t i = 0;
    while (i < n) {
        switch (kind) {
        case 0: {                                           /* words: short and mid matches */
            const char *s = w[next() % 8];
            while (*s && i < n) p[i++] = (unsigned char)*s++;
            break;
        }
        case 1:                                             /* runs: distances 1..7, overlapping copies */
            for (unsigned r = next() % 300, b = next() & 0xFF, d = 1 + next() % 7; r-- && i < n; i++)
                p[i] = i >= d ? p[i - d] : (unsigned char)b;
            break;
        case 2: p[i++] = (unsigned char)next(); break;      /* random: stored blocks */
        default:                                            /* far repeats: distances near 32 KiB */
            if (i > 40000 && next() % 4 == 0) {
                size_t from = i - 32768 + next() % 512;
                for (unsigned r = 3 + next() % 255; r-- && i < n; i++) p[i] = p[from++];
            } else p[i++] = (unsigned char)(next() % 7 ? 'a' + next() % 26 : next());
        }
    }
    return p;
}

/* decode `z` with reads of `step` bytes (0: varying 1..70000) and compare with `want` */
static bool decode_in_steps(const void *z, size_t zn, const unsigned char *want, size_t n, size_t step) {
    proven_allocator_t a = proven_heap_allocator();
    fa_stream_t *m, *d;
    if (fa_stream_memory(a, z, zn, &m) || fa_dec_deflate(m, false, n, &d)) return false;
    unsigned char *out = malloc(n + 1);
    size_t tot = 0, got = 0;
    bool ok = true;
    for (;;) {
        size_t s = step ? step : 1 + next() % 70000;
        if (s > n + 1 - tot) s = n + 1 - tot;
        if (d->read(d, out + tot, s, &got) != FULTA_ARC_OK) { ok = false; break; }
        if (!got) break;
        tot += got;
    }
    ok = ok && tot == n && memcmp(out, want, n) == 0;
    free(out);
    fa_stream_destroy(d);
    return ok;
}

int main(void) {
    static const size_t steps[] = {1, 3, 265, 266, 267, 300, 1000, 4096, 65535, 65536, 65537, 1 << 20, 0, 0};
    static const int levels[] = {1, 6, 9};
    static const size_t sizes[] = {0, 1, 300, 70000, 300000};
    static const char *kinds[] = {"words", "runs", "random", "far"};
    for (int k = 0; k < 4; k++)
        for (size_t si = 0; si < sizeof sizes / sizeof *sizes; si++)
            for (size_t li = 0; li < sizeof levels / sizeof *levels; li++) {
                size_t n = sizes[si];
                unsigned char *in = make(k, n ? n : 1);
                size_t zn = 0;
                int flags = (int)tdefl_create_comp_flags_from_zip_params(levels[li], -15, 0);
                void *z = tdefl_compress_mem_to_heap(in, n, &zn, flags);
                CHECK(z != NULL, "%s %zu L%d: compress", kinds[k], n, levels[li]);
                if (z) {
                    for (size_t t = 0; t < sizeof steps / sizeof *steps; t++)
                        CHECK(decode_in_steps(z, zn, in, n, steps[t]), "%s %zu bytes, level %d, reads of %zu",
                              kinds[k], n, levels[li], steps[t]);
                    mz_free(z);
                }
                free(in);
            }
    return t_report("test_inflate");
}
