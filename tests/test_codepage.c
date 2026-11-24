/* test_codepage.c - names stored in a code page come out as the right UTF-8.
 *
 * Each case builds a one-entry stored ZIP in memory whose name holds the code page's bytes, opens it with that code
 * page and compares the name. Expected strings were checked against Python's codecs (an independent table). */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

/* A ZIP with one empty stored file named `name` (n bytes, flag bit 11 clear). Returns its size. */
static size_t make_zip(uint8_t *z, const uint8_t *name, size_t n) {
    size_t p = 0;
    put32(z, 0x04034b50); put16(z + 4, 10); put16(z + 6, 0); put16(z + 8, 0); put32(z + 10, 0);
    put32(z + 14, 0); put32(z + 18, 0); put32(z + 22, 0); put16(z + 26, (unsigned)n); put16(z + 28, 0);
    memcpy(z + 30, name, n);
    p = 30 + n;
    size_t cd = p;
    put32(z + p, 0x02014b50); put16(z + p + 4, 10); put16(z + p + 6, 10); put16(z + p + 8, 0); put16(z + p + 10, 0);
    put32(z + p + 12, 0); put32(z + p + 16, 0); put32(z + p + 20, 0); put32(z + p + 24, 0);
    put16(z + p + 28, (unsigned)n); put16(z + p + 30, 0); put16(z + p + 32, 0); put16(z + p + 34, 0);
    put16(z + p + 36, 0); put32(z + p + 38, 0); put32(z + p + 42, 0);
    memcpy(z + p + 46, name, n);
    p += 46 + n;
    put32(z + p, 0x06054b50); put16(z + p + 4, 0); put16(z + p + 6, 0); put16(z + p + 8, 1); put16(z + p + 10, 1);
    put32(z + p + 12, (uint32_t)(p - cd)); put32(z + p + 16, (uint32_t)cd); put16(z + p + 20, 0);
    return p + 22;
}

static void check(const char *label, fulta_arc_codepage_t cp, const char *bytes, const char *want) {
    uint8_t z[512];
    size_t size = make_zip(z, (const uint8_t *)bytes, strlen(bytes));
    fulta_arc_options_t opt = {.codepage = cp};
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(z, size, &opt, &arc);
    CHECK(e == FULTA_ARC_OK, "%s: open: %s", label, fulta_arc_strerror(e));
    if (e) return;
    CHECK(fulta_arc_count(arc) == 1 && strcmp(fulta_arc_entry(arc, 0)->name, want) == 0, "%s: got \"%s\", want \"%s\"",
          label, fulta_arc_count(arc) ? fulta_arc_entry(arc, 0)->name : "", want);
    fulta_arc_close(arc);
}

int main(void) {
    /* "Привет, мир ё.txt" in each Russian page */
    const char *want = "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82, \xd0\xbc\xd0\xb8\xd1\x80 \xd1\x91.txt";
    check("cp866", FULTA_ARC_CP_866, "\x8f\xe0\xa8\xa2\xa5\xe2, \xac\xa8\xe0 \xf1.txt", want);
    check("cp1251", FULTA_ARC_CP_1251, "\xcf\xf0\xe8\xe2\xe5\xf2, \xec\xe8\xf0 \xb8.txt", want);
    check("koi8r", FULTA_ARC_CP_KOI8R, "\xf0\xd2\xc9\xd7\xc5\xd4, \xcd\xc9\xd2 \xa3.txt", want);
    /* the pages already present, one character each: CP437 box drawing, CP1252 euro, CP949 hangul */
    check("cp437", FULTA_ARC_CP_437, "\xc9.txt", "\xe2\x95\x94.txt");
    check("cp1252", FULTA_ARC_CP_1252, "\x80.txt", "\xe2\x82\xac.txt");
    check("cp949", FULTA_ARC_CP_949, "\xc7\xd1.txt", "\xed\x95\x9c.txt");
    return t_report("test_codepage");
}
