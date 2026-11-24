/* support.h - what the proofs link in: the library's own allocation helpers and proven's heap and arena (the real
 * code, so allocation is modelled, not assumed), and a memchr model CBMC lacks. */
#ifndef FA_PROOF_SUPPORT_H
#define FA_PROOF_SUPPORT_H
#include "../../vendor/proven/platform/proven_sys_mem.c"
#include "../../vendor/proven/src/proven/heap.c"
#include "../../vendor/proven/src/proven/arena.c"
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return NULL;
}
#endif
