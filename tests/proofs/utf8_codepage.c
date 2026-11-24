/* utf8_codepage.c - CBMC proof: the code-page decoders over every byte string of up to N bytes (D-041). Checked: no
 * out-of-bounds read of the input or of the tables (the DBCS pair index is bounded by the lead-byte range), every
 * byte written stays inside the output, and the output is valid UTF-8 (the library's fa_utf8_valid), for UTF-8,
 * CP437 and the DBCS page CP949. The growable buffer is replaced by a fixed array that asserts its bounds. */
#include "../../src/core/internal.h"
#define fa_buf_append util_buf_append
#define fa_buf_byte util_buf_byte
#define fa_buf_free util_buf_free
#define fa_utf8_put util_utf8_put
#include "../../src/core/util.c"
#undef fa_buf_append
#undef fa_buf_byte
#undef fa_buf_free
#undef fa_utf8_put

#ifndef N
#define N 4
#endif
#define OUTCAP (4 * N + 8)
static uint8_t out[OUTCAP];
static size_t outn;
fulta_arc_err_t fa_buf_append(fa_buf_t *b, proven_mem_view_t d) {
    (void)b;
    __CPROVER_assert(d.size <= OUTCAP - outn, "output stays in bounds");
    for (size_t i = 0; i < d.size; i++) out[outn++] = d.ptr[i];
    return FULTA_ARC_OK;
}
fulta_arc_err_t fa_buf_byte(fa_buf_t *b, uint8_t v) { return fa_buf_append(b, fa_bytes(&v, 1)); }
void fa_buf_free(fa_buf_t *b) { (void)b; }
fulta_arc_err_t fa_utf8_put(fa_buf_t *b, uint32_t cp) {   /* util.c's encoder, writing into the array above */
    uint8_t t[4]; size_t n;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) { t[0] = (uint8_t)cp; n = 1; }
    else if (cp < 0x800) { t[0] = (uint8_t)(0xC0 | (cp >> 6)); t[1] = (uint8_t)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { t[0] = (uint8_t)(0xE0 | (cp >> 12)); t[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); t[2] = (uint8_t)(0x80 | (cp & 0x3F)); n = 3; }
    else { t[0] = (uint8_t)(0xF0 | (cp >> 18)); t[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F)); t[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F)); t[3] = (uint8_t)(0x80 | (cp & 0x3F)); n = 4; }
    return fa_buf_append(b, fa_bytes(t, n));
}
#include "../../src/text/codepage.c"
#include "../../src/text/cp_tables.c"

void harness(void) {
    uint8_t b[N];
    for (size_t i = 0; i < N; i++) b[i] = nondet_uchar();
    size_t n = nondet_size_t();
    __CPROVER_assume(n <= N);
    fa_buf_t buf = {0};
    unsigned k = nondet_unsigned();
    __CPROVER_assume(k < 3);
    dbcs_t d;
    if (k == 0) { if (fa_utf8_valid(fa_bytes(b, n))) (void)fa_buf_append(&buf, fa_bytes(b, n)); else (void)sbcs(&buf, b, n, fa_cp1252); }
    else if (k == 1) (void)sbcs(&buf, b, n, fa_cp437);
    else { (void)get_dbcs(FULTA_ARC_CP_949, &d); (void)dbcs_decode(&buf, b, n, &d); }
    __CPROVER_assert(fa_utf8_valid(fa_bytes(out, outn)), "the decoded name is valid UTF-8");
}
