/* test_xz.c - FultaArc: xz stream reader (.xz). MIT.
 * Vector made with Python's lzma (FORMAT_XZ, preset 6) of a 980-byte plaintext (CRC-32 0x71d5da1d). Checks
 * detection, the one entry, the name derived from the archive name (.xz stores none), a byte-exact extract (the
 * xz decoder checks its own CRCs), and that a corrupted body does not extract clean bytes. */
#include "fulta/arc.h"
#include "testlib.h"

static const unsigned char XZ[] = {253,55,122,88,90,0,0,4,230,214,180,70,2,0,33,1,22,0,0,0,116,47,229,163,224,3,211,0,47,93,0,35,29,73,135,119,98,220,187,22,241,97,129,47,25,201,173,118,88,108,198,235,180,61,52,232,25,250,177,28,215,96,100,117,78,232,251,103,126,84,22,24,180,158,26,87,159,0,0,0,7,103,110,45,81,43,201,58,0,1,75,212,7,0,0,0,82,207,104,71,177,196,103,251,2,0,0,0,0,4,89,90};
static const unsigned long XZ_LEN = 112;
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
    o.name = "report.xz";
    fulta_arc_t *a = NULL;
    CHECK(fulta_arc_open_memory(XZ, XZ_LEN, &o, &a) == FULTA_ARC_OK && a, "open");
    if (a) {
        CHECK(fulta_arc_format(a) == FULTA_ARC_FORMAT_XZ, "format xz");
        CHECK(fulta_arc_count(a) == 1, "one entry");
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && e->name && strcmp(e->name, "report") == 0, "name (got %s)", e && e->name ? e->name : "(null)");
        void *d = NULL; size_t dn = 0;
        CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) == FULTA_ARC_OK, "extract");
        CHECK(dn == PLAIN_LEN && d && memcmp(d, plain, PLAIN_LEN) == 0, "bytes (got %zu)", dn);
        fulta_arc_free(d);
        fulta_arc_close(a);
    }

    /* .txz archive name maps to .tar */
    o.name = "src.txz"; a = NULL;
    CHECK(fulta_arc_open_memory(XZ, XZ_LEN, &o, &a) == FULTA_ARC_OK && a, "open txz");
    if (a) {
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && e->name && strcmp(e->name, "src.tar") == 0, "txz name (got %s)", e && e->name ? e->name : "(null)");
        fulta_arc_close(a);
    }

    /* corrupted body: must not extract clean bytes */
    {
        unsigned char bad[112];
        memcpy(bad, XZ, XZ_LEN);
        bad[40] ^= 0xFF;
        a = NULL;
        if (fulta_arc_open_memory(bad, XZ_LEN, NULL, &a) == FULTA_ARC_OK && a) {
            void *d = NULL; size_t dn = 0;
            fulta_arc_err_t r = fulta_arc_extract_alloc(a, 0, &d, &dn);
            CHECK(r != FULTA_ARC_OK || dn != PLAIN_LEN || memcmp(d, plain, PLAIN_LEN) != 0, "corrupt: not clean");
            fulta_arc_free(d);
            fulta_arc_close(a);
        } else t_pass++;
    }
    return t_report("test_xz");
}
