/* test_bzip2.c - FultaArc: bzip2 stream reader (.bz2). MIT.
 * Vector made with Python's bz2.compress (level 9) of a 1050-byte compressible plaintext (CRC-32 0xfe2c8bcd).
 * Checks detection, the one entry, the name derived from the archive name (bzip2 stores none), a byte-exact
 * extract (the vendored bzip2 verifies its own CRCs), and that a corrupted stream does not extract clean bytes. */
#include "fulta/arc.h"
#include "testlib.h"

/* plain len 1050 crc32 0xfe2c8bcd comp len 102 */
static const unsigned char BZ[] = {66,90,104,57,49,65,89,38,83,89,143,213,97,170,0,0,134,157,128,64,1,16,0,33,0,62,38,94,16,32,0,144,41,147,19,32,200,193,53,84,9,144,100,211,212,202,27,195,228,53,135,80,238,31,97,234,28,67,72,115,67,56,109,13,97,149,14,97,180,49,12,67,104,119,14,33,212,60,195,120,105,12,225,136,98,25,195,56,126,134,33,252,93,201,20,225,66,66,63,85,134,168};
static const unsigned long BZ_LEN = 102;
static const unsigned long PLAIN_LEN = 1050;

static void build_plain(unsigned char *p) {
    const char *u = "FultaArc bzip2 stream reader test. ";
    size_t ul = 35;
    for (int i = 0; i < 30; i++) memcpy(p + (size_t)i * ul, u, ul);
}

int main(void) {
    unsigned char plain[1050];
    build_plain(plain);

    fulta_arc_options_t o = {0};
    o.name = "payload.bz2";
    fulta_arc_t *a = NULL;
    CHECK(fulta_arc_open_memory(BZ, BZ_LEN, &o, &a) == FULTA_ARC_OK && a, "open");
    if (a) {
        CHECK(fulta_arc_format(a) == FULTA_ARC_FORMAT_BZIP2, "format bzip2");
        CHECK(fulta_arc_count(a) == 1, "one entry");
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && e->name && strcmp(e->name, "payload") == 0, "name (got %s)", e && e->name ? e->name : "(null)");
        void *d = NULL; size_t dn = 0;
        CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) == FULTA_ARC_OK, "extract");
        CHECK(dn == PLAIN_LEN && d && memcmp(d, plain, PLAIN_LEN) == 0, "bytes (got %zu)", dn);
        fulta_arc_free(d);
        fulta_arc_close(a);
    }

    /* .tbz2 archive name maps to .tar */
    o.name = "set.tbz2"; a = NULL;
    CHECK(fulta_arc_open_memory(BZ, BZ_LEN, &o, &a) == FULTA_ARC_OK && a, "open tbz2");
    if (a) {
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && e->name && strcmp(e->name, "set.tar") == 0, "tbz2 name (got %s)", e && e->name ? e->name : "(null)");
        fulta_arc_close(a);
    }

    /* corrupted payload: flip a byte in the compressed body; must not extract clean bytes */
    {
        unsigned char bad[102];
        memcpy(bad, BZ, BZ_LEN);
        bad[40] ^= 0xFF;
        a = NULL;
        if (fulta_arc_open_memory(bad, BZ_LEN, NULL, &a) == FULTA_ARC_OK && a) {
            void *d = NULL; size_t dn = 0;
            fulta_arc_err_t r = fulta_arc_extract_alloc(a, 0, &d, &dn);
            CHECK(r != FULTA_ARC_OK || dn != PLAIN_LEN || memcmp(d, plain, PLAIN_LEN) != 0, "corrupt: not clean");
            fulta_arc_free(d);
            fulta_arc_close(a);
        } else {
            t_pass++;   /* refusing at open is also acceptable */
        }
    }
    return t_report("test_bzip2");
}
