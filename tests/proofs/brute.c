/* brute.c - exhaustive execution where the CBMC models do not fit this host (D-041): every input up to a small
 * length, run through the library's own code under ASan + UBSan (`make brute`), so any out-of-bounds access,
 * overflow or leak on any of them fails the run.
 *   RAR 4 Unicode names (rar4_name): every byte string of 0..3 bytes, with and without the Unicode flag
 *   Deflate and Deflate64 (fa_dec_deflate): every stream of 0..3 bytes, decoded to its end */
#include "../../src/core/internal.h"
#include "../../src/rar/rar.c"

#include <stdio.h>

static fulta_arc_err_t drain(fa_stream_t *s) {
    uint8_t out[4096];
    for (int k = 0; k < 64; k++) {                       /* 3 input bytes decode to at most a few hundred bytes */
        size_t got = 0;
        fulta_arc_err_t e = s->read(s, out, sizeof out, &got);
        if (e || !got) return e;
    }
    return FULTA_ARC_OK;
}

int main(void) {
    proven_allocator_t a = proven_heap_allocator();
    uint64_t names = 0, streams = 0;
    uint8_t b[3];
    for (unsigned len = 0; len <= 3; len++) {
        uint64_t total = (uint64_t)1 << (8 * len);
        for (uint64_t v = 0; v < total; v++) {
            for (unsigned i = 0; i < len; i++) b[i] = (uint8_t)(v >> (8 * i));
            uint8_t mem[256];
            for (int uni = 0; uni < 2; uni++) {
                proven_arena_t ar = proven_arena_create((proven_mem_mut_t){.ptr = mem, .size = sizeof mem});
                bool legacy;
                (void)rar4_name(a, &ar, b, len, uni, &legacy);
                names++;
            }
            for (int d64 = 0; d64 < 2; d64++) {
                fa_stream_t *in = NULL, *dec = NULL;
                if (fa_stream_memory(a, b, len, &in)) return 2;
                if (fa_dec_deflate(in, d64, UINT64_MAX, &dec) == FULTA_ARC_OK) {
                    (void)drain(dec);
                    fa_stream_destroy(dec);
                }
                streams++;
            }
        }
    }
    printf("brute: %llu RAR 4 names, %llu Deflate/Deflate64 streams: no fault\n", (unsigned long long)names,
           (unsigned long long)streams);
    return 0;
}
