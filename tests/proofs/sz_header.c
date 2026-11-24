/* sz_header.c - CBMC proof: the 7z header readers over every N-byte header (D-041): rd_num, rd_bits, rd_skip and
 * parse_folder (coders, streams, binds, packed indexes, the main coder). Checked: no out-of-bounds access, and a
 * folder that parses has every bind and packed index inside its coder/stream counts. Bound: every N-byte header
 * whose coder count is at most MAXC (the model of 64 coders x 64 binds does not finish). */
#include "support.h"
#include "../../src/core/util.c"
#include "../../src/7z/sevenzip.c"
#ifndef N
#define N 10
#endif
void harness(void) {
    uint8_t h[N];
    for (size_t i = 0; i < N; i++) h[i] = nondet_uchar();
#ifndef MAXC
#define MAXC 2
#endif
    __CPROVER_assume(h[0] <= MAXC);                    /* the coder count (one-byte vint) */
    rd_t r = {h, N, 0, false, proven_heap_allocator()};
    sz_folder_t f = {0};
    fulta_arc_err_t e = parse_folder(&r, &f);
    __CPROVER_assert(r.pos <= r.n, "reader never passes its end");
    if (e == FULTA_ARC_OK) {
        __CPROVER_assert(f.main < f.ncoders, "main coder in range");
        for (uint32_t i = 0; i < f.nbinds; i++)
            __CPROVER_assert(f.binds[i][0] < f.nin && f.binds[i][1] < f.ncoders, "bind in range");
        for (uint32_t i = 0; i < f.npacked; i++) __CPROVER_assert(f.packed[i] < f.nin, "packed index in range");
    }
}
