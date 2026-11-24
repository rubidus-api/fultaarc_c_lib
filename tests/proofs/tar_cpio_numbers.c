/* tar_cpio_numbers.c - CBMC proof: tar's octal/base-256 fields, pax decimals and records, and cpio's fields over
 * every N-byte input (D-041). Checked: no out-of-bounds access, no signed overflow (pax values saturate). */
#include "support.h"
#include "../../src/core/util.c"
#include "../../src/tar/tar.c"
#define cpio_close cpio_close_
#include "../../src/cpio/cpio.c"
#ifndef N
#define N 12
#endif
void harness(void) {
    uint8_t f[N + 1];
    for (size_t i = 0; i < N; i++) f[i] = nondet_uchar();
    f[N] = 0;
    uint64_t v;
    (void)tar_num(f, N, &v);
    (void)cpio_num(f, N < 11 ? N : 11, 8, &v);
    (void)cpio_num(f, N < 8 ? N : 8, 16, &v);
    int64_t d = tar_dec((const char *)f);
    (void)secs_ns(d);
    char *p = pax_get(proven_heap_allocator(), f, N, "size");
    if (p) fa_release(proven_heap_allocator(), p);
}
