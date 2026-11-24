/* rar_names.c - CBMC proof: RAR 4's encoded Unicode names (rar4_name) and RAR 5's vint/bytes5 over every N-byte
 * input (D-041). Checked: no out-of-bounds access in the decoding of any narrow-name + encoded-UTF-16 pair, for every
 * content of each length LEN (one run per length). */
#include "support.h"
#include "../../src/core/util.c"
#include "../../src/rar/rar.c"
#ifndef N
#define N 8
#endif
void harness(void) {
    uint8_t name[N];
    for (size_t i = 0; i < N; i++) name[i] = nondet_uchar();
#ifdef LEN
    size_t n = LEN;                                    /* one length per run (concrete sizes keep the model small);
                                                          run.sh proves every LEN from 0 to N */
#else
    size_t n = nondet_size_t();
    __CPROVER_assume(n <= N);
#endif
    uint8_t arena_mem[8 * N + 64];
    proven_arena_t ar = proven_arena_create((proven_mem_mut_t){.ptr = arena_mem, .size = sizeof arena_mem});
    bool legacy;
    proven_u8str_view_t v = rar4_name(proven_heap_allocator(), &ar, name, n, nondet_bool(), &legacy);
    (void)v;
    rd5_t r = {name, n, 0, false};
    uint64_t x = vint(&r);
    (void)x;
    const uint8_t *q = bytes5(&r, nondet_size_t());
    if (q) __CPROVER_assert(r.at <= r.n, "bytes5 stays inside");
}
