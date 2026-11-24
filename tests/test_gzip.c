/* test_gzip.c - FultaArc: gzip stream reader (.gz). MIT.
 * Vectors made with Python's gzip module: a member with FNAME="hello.txt" and a raw gzip.compress() member with no
 * name. Both hold the same 900-byte plaintext (CRC-32 0x56d00dc5). Checks detection, the one entry, the name
 * (FNAME, else derived from the archive name), the trailer CRC flag, and a byte-exact extract. */
#include "fulta/arc.h"
#include "testlib.h"

/* plaintext len 900 crc32 0x56d00dc5 */
static const unsigned char GZ_FNAME_BYTES[] = {31,139,8,8,141,124,107,90,2,255,104,101,108,108,111,46,116,120,116,0,11,201,72,85,40,44,205,76,206,86,72,42,202,47,207,83,72,203,175,80,200,42,205,45,40,86,200,47,75,45,82,40,1,74,231,36,86,85,42,164,228,167,235,113,133,140,42,30,85,60,170,152,218,138,1,197,13,208,86,132,3,0,0};
static const unsigned long GZ_FNAME_LEN = 83;
static const unsigned char GZ_RAW_BYTES[] = {31,139,8,0,162,227,204,106,2,255,11,201,72,85,40,44,205,76,206,86,72,42,202,47,207,83,72,203,175,80,200,42,205,45,40,86,200,47,75,45,82,40,1,74,231,36,86,85,42,164,228,167,235,113,133,140,42,30,85,60,170,152,218,138,1,197,13,208,86,132,3,0,0};
static const unsigned long GZ_RAW_LEN = 73;
static const unsigned long GZ_PLAIN_LEN = 900;

static void build_plain(unsigned char *p) {
    const char *line = "The quick brown fox jumps over the lazy dog.\n";
    size_t ll = 45;
    for (int i = 0; i < 20; i++) memcpy(p + (size_t)i * ll, line, ll);
}

static void check_member(const unsigned char *gz, unsigned long gzn, const char *archive_name,
                         const char *expect_name, const unsigned char *plain) {
    fulta_arc_options_t o = {0};
    o.name = archive_name;
    fulta_arc_t *a = NULL;
    CHECK(fulta_arc_open_memory(gz, gzn, &o, &a) == FULTA_ARC_OK && a, "%s: open", expect_name);
    if (!a) return;
    CHECK(fulta_arc_format(a) == FULTA_ARC_FORMAT_GZIP, "%s: format gzip", expect_name);
    CHECK(fulta_arc_count(a) == 1, "%s: one entry", expect_name);
    const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
    CHECK(e && e->name && strcmp(e->name, expect_name) == 0, "%s: name (got %s)", expect_name, e && e->name ? e->name : "(null)");
    CHECK(e && (e->flags & FULTA_ARC_ENTRY_HAS_CRC32) && e->crc32 == 0x56d00dc5u, "%s: trailer crc", expect_name);
    void *d = NULL; size_t dn = 0;
    CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) == FULTA_ARC_OK, "%s: extract", expect_name);
    CHECK(dn == GZ_PLAIN_LEN && d && memcmp(d, plain, GZ_PLAIN_LEN) == 0, "%s: bytes (got %zu)", expect_name, dn);
    fulta_arc_free(d);
    fulta_arc_close(a);
}

int main(void) {
    unsigned char plain[900];
    build_plain(plain);

    /* FNAME member: name comes from the gzip FNAME field, ignoring the archive name */
    check_member(GZ_FNAME_BYTES, GZ_FNAME_LEN, "whatever.gz", "hello.txt", plain);
    /* raw member: no FNAME, so the name is derived from the archive name with .gz stripped */
    check_member(GZ_RAW_BYTES, GZ_RAW_LEN, "/tmp/report.txt.gz", "report.txt", plain);
    /* raw member, .tgz archive name maps to .tar */
    check_member(GZ_RAW_BYTES, GZ_RAW_LEN, "backup.tgz", "backup.tar", plain);

    /* truncated: a header-only prefix must not crash or extract clean bytes */
    {
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_memory(GZ_FNAME_BYTES, 12, NULL, &a);
        int ok_fail = 1;
        if (e == FULTA_ARC_OK && a) {
            void *d = NULL; size_t dn = 0;
            ok_fail = (fulta_arc_extract_alloc(a, 0, &d, &dn) != FULTA_ARC_OK);
            fulta_arc_free(d);
        }
        CHECK(ok_fail, "truncated: no clean success");
        if (a) fulta_arc_close(a);
    }
    return t_report("test_gzip");
}
