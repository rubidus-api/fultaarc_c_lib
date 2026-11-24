/* inflate_huff.c - CBMC proof: the Deflate Huffman builder and decoder (D-041) for every code-length vector of NS
 * symbols with lengths 0-MAXL and every next-bits pattern: no out-of-bounds access in build (including the 1024-entry
 * fast table), and decode returns only symbols the table holds. */
#ifndef NS
#define NS 8
#endif
#ifndef MAXL
#define MAXL 15
#endif
#include "../../src/codec/inflate.c"
void harness(void) {
    uint8_t len[NS];
    for (int i = 0; i < NS; i++) { len[i] = nondet_uchar(); __CPROVER_assume(len[i] <= MAXL); }
    static huff_t h;
    if (build(&h, len, NS, false) != FULTA_ARC_OK) return;
    static inflate_stream_t z;
    z.bitbuf = nondet_ulong();
    z.bitcnt = 64;
    int sym = -1;
    if (decode(&z, &h, &sym) == FULTA_ARC_OK) __CPROVER_assert(sym >= 0 && sym < NS && len[sym] != 0, "a real symbol");
}
