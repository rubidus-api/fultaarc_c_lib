/* test_zstd.c - FultaArc: Zstandard stream reader (.zst). MIT.
 * Vector made with the vendored zstd (ZSTD_compress level 6) of a 980-byte plaintext (CRC-32 0x71d5da1d). Checks
 * detection, the one entry, the name derived from the archive name (.zst stores none), a byte-exact extract, and
 * that a corrupted body does not extract clean bytes. */
#include "fulta/arc.h"
#include "testlib.h"

static const unsigned char ZS[] = {40,181,47,253,96,212,2,101,1,0,52,2,70,117,108,116,97,65,114,99,32,120,122,47,122,115,116,100,32,115,116,114,101,97,109,32,116,101,115,116,32,108,105,110,101,46,32,1,0,115,109,42,245,4};
static const unsigned long ZS_LEN = 54;
static const unsigned long PLAIN_LEN = 980;

static void build_plain(unsigned char *p) {
    const char *u = "FultaArc xz/zstd stream test line. ";
    size_t ul = 35;
    for (int i = 0; i < 28; i++) memcpy(p + (size_t)i * ul, u, ul);
}

int main(void) {
    unsigned char plain[980];
    build_plain(plain);

    fulta_arc_options_t o = {0};
    o.name = "blob.zst";
    fulta_arc_t *a = NULL;
    CHECK(fulta_arc_open_memory(ZS, ZS_LEN, &o, &a) == FULTA_ARC_OK && a, "open");
    if (a) {
        CHECK(fulta_arc_format(a) == FULTA_ARC_FORMAT_ZSTD, "format zstd");
        CHECK(fulta_arc_count(a) == 1, "one entry");
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && e->name && strcmp(e->name, "blob") == 0, "name (got %s)", e && e->name ? e->name : "(null)");
        void *d = NULL; size_t dn = 0;
        CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) == FULTA_ARC_OK, "extract");
        CHECK(dn == PLAIN_LEN && d && memcmp(d, plain, PLAIN_LEN) == 0, "bytes (got %zu)", dn);
        fulta_arc_free(d);
        fulta_arc_close(a);
    }

    /* .tzst archive name maps to .tar */
    o.name = "vol.tzst"; a = NULL;
    CHECK(fulta_arc_open_memory(ZS, ZS_LEN, &o, &a) == FULTA_ARC_OK && a, "open tzst");
    if (a) {
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && e->name && strcmp(e->name, "vol.tar") == 0, "tzst name (got %s)", e && e->name ? e->name : "(null)");
        fulta_arc_close(a);
    }

    /* corrupted body: must not extract clean bytes */
    {
        unsigned char bad[54];
        memcpy(bad, ZS, ZS_LEN);
        bad[30] ^= 0xFF;
        a = NULL;
        if (fulta_arc_open_memory(bad, ZS_LEN, NULL, &a) == FULTA_ARC_OK && a) {
            void *d = NULL; size_t dn = 0;
            fulta_arc_err_t r = fulta_arc_extract_alloc(a, 0, &d, &dn);
            CHECK(r != FULTA_ARC_OK || dn != PLAIN_LEN || memcmp(d, plain, PLAIN_LEN) != 0, "corrupt: not clean");
            fulta_arc_free(d);
            fulta_arc_close(a);
        } else t_pass++;
    }
    return t_report("test_zstd");
}
