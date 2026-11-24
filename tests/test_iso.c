/* test_iso.c - ISO 9660 reader safety cases not covered by the 7z-validated manifest (iso.txt): a directory cycle
 * must terminate and still list the real files once, and a root record pointing outside the image is rejected. The
 * fixtures are patched copies of basic.iso in memory. See D-020, T021. */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BS 2048

static unsigned char *load(const char *rel, size_t *len) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", t_fixtures(), rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n > 0 ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *len = (size_t)n;
    return b;
}

/* basic.iso: PVD at sector 16, root directory at sector 20. The root extent holds ".", "..", then the file/dir
 * records. The first subdirectory record (name "SUB") has its extent-LBA field (LE) 2 bytes into the record. */
static void test_cycle(void) {
    size_t n = 0;
    unsigned char *iso = load("iso/basic.iso", &n);
    if (!iso) { SKIP("basic.iso not present"); return; }

    /* point the "SUB" subdirectory's extent back at the root (sector 20) -> a cycle. Find SUB's record in the root
     * extent and overwrite its LE extent LBA with 20. The root dir sector begins at 20*BS; records are ".", "..",
     * then the entries. We scan for a directory record whose identifier is "SUB". */
    size_t root = 20 * BS;
    size_t p = root;
    int patched = 0;
    while (p + 33 < root + BS && iso[p]) {
        unsigned reclen = iso[p];
        unsigned lenfi = iso[p + 32];
        if (reclen < 33 || lenfi == 0) { p += reclen ? reclen : 1; continue; }
        if (lenfi == 3 && memcmp(iso + p + 33, "SUB", 3) == 0 && (iso[p + 25] & 0x02)) {
            iso[p + 2] = 20; iso[p + 3] = 0; iso[p + 4] = 0; iso[p + 5] = 0;   /* LE extent LBA = 20 (root) */
            patched = 1;
            break;
        }
        p += reclen;
    }
    CHECK(patched, "found the SUB directory record to patch");

    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(iso, n, NULL, &arc);   /* must terminate, not hang or OOM */
    CHECK(e == FULTA_ARC_OK, "cyclic iso still opens (%s)", fulta_arc_strerror(e));
    if (!e) {
        /* the root's own files are still listed once; the visited-set stops the cycle */
        int hello = 0;
        for (size_t i = 0; i < fulta_arc_count(arc); i++)
            if (strcmp(fulta_arc_entry(arc, i)->name, "HELLO.TXT") == 0) hello++;
        CHECK(hello == 1, "HELLO.TXT listed exactly once despite the cycle (got %d)", hello);
        fulta_arc_close(arc);
    }
    free(iso);
}

static void test_root_out_of_range(void) {
    size_t n = 0;
    unsigned char *iso = load("iso/basic.iso", &n);
    if (!iso) { SKIP("basic.iso not present"); return; }
    /* root directory record is at PVD offset 156; its LE extent LBA is 2 bytes in. Set it huge. */
    size_t rr = 16 * BS + 156 + 2;
    iso[rr] = 0xFF; iso[rr + 1] = 0xFF; iso[rr + 2] = 0xFF; iso[rr + 3] = 0x7F;
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(iso, n, NULL, &arc);
    CHECK(e != FULTA_ARC_OK, "root LBA out of range is rejected (%s)", fulta_arc_strerror(e));
    if (e == FULTA_ARC_OK) fulta_arc_close(arc);
    free(iso);
}

/* --- Joliet + Rock Ridge (joliet.iso / rr.iso / prec.iso, made by isomake.build_ext, 7z-validated) ----------- */

typedef struct { unsigned char buf[256]; size_t n; } cap_t;
static fulta_arc_err_t cap_write(void *ctx, const void *d, size_t n) {
    cap_t *c = ctx;
    if (c->n + n <= sizeof c->buf) { memcpy(c->buf + c->n, d, n); c->n += n; }
    return FULTA_ARC_OK;
}

static int find_entry(fulta_arc_t *arc, const char *name) {
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (strcmp(fulta_arc_entry(arc, i)->name, name) == 0) return (int)i;
    return -1;
}

static fulta_arc_t *open_fixture(const char *rel) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", t_fixtures(), rel);
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_file(path, NULL, &arc)) return NULL;
    return arc;
}

static void test_joliet(void) {
    fulta_arc_t *arc = open_fixture("iso/joliet.iso");
    if (!arc) { SKIP("joliet.iso not present"); return; }
    /* the UCS-2 name decodes to UTF-8 with the non-ASCII bytes intact */
    CHECK(find_entry(arc, "héllo wörld.txt") >= 0, "Joliet UCS-2 name decoded to UTF-8");
    CHECK(find_entry(arc, "SUBDIR/Inner File.txt") >= 0, "Joliet subdirectory walked with long names");
    fulta_arc_close(arc);
}

static void test_rr_symlink(void) {
    fulta_arc_t *arc = open_fixture("iso/rr.iso");
    if (!arc) { SKIP("rr.iso not present"); return; }
    CHECK(find_entry(arc, "subdir/inner long name.txt") >= 0, "Rock Ridge NM lowercases the directory name");
    int i = find_entry(arc, "link-to-readme");
    CHECK(i >= 0, "Rock Ridge NM-named symlink is listed");
    if (i >= 0) {
        cap_t c = {0};
        fulta_arc_sink_t s = {&c, cap_write};
        fulta_arc_err_t e = fulta_arc_extract(arc, (size_t)i, &s);
        CHECK(e == FULTA_ARC_OK, "symlink extract (%s)", fulta_arc_strerror(e));
        CHECK(c.n == 17 && memcmp(c.buf, "read me first.txt", 17) == 0,
              "SL target reassembled as content (got %zu bytes)", c.n);
    }
    fulta_arc_close(arc);
}

static void test_rr_ce_name(void) {
    fulta_arc_t *arc = open_fixture("iso/rr.iso");
    if (!arc) { SKIP("rr.iso not present"); return; }
    /* the NM that overflows the record continues in a CE area; the reader must follow it and reassemble the
     * whole name (this is the part 7z's reader stops short of). */
    const char *full = "a_very_long_rock_ridge_name_that_overflows_the_record_system_use_area_and_spills_"
                       "into_a_continuation_block_via_CE_so_the_reader_follows_it.txt";
    CHECK(find_entry(arc, full) >= 0, "Rock Ridge CE continuation followed, long name reassembled");
    fulta_arc_close(arc);
}

static void test_precedence(void) {
    fulta_arc_t *arc = open_fixture("iso/prec.iso");
    if (!arc) { SKIP("prec.iso not present"); return; }
    /* prec.iso carries both trees with DISAGREEING names; we prefer Rock Ridge (7z would show the Joliet name). */
    CHECK(find_entry(arc, "rr-wins.txt") >= 0, "Rock Ridge name preferred over Joliet");
    CHECK(find_entry(arc, "joliet-loses.txt") < 0, "the Joliet name is not also listed");
    fulta_arc_close(arc);
}

static void test_reloc(void) {
    /* reloc.iso: a deep directory moved under RR_MOVED with a CL placeholder at its real parent. A correct RRIP
     * reader reaches it as deep/<file> through the CL and hides the RR_MOVED artifacts (7z does NOT do this - it
     * shows rr_moved/deep/... and the placeholder as a 0-byte file; so this is maker-known, not 7z-validated). */
    fulta_arc_t *arc = open_fixture("iso/reloc.iso");
    if (!arc) { SKIP("reloc.iso not present"); return; }
    int i = find_entry(arc, "deep/relocated file.txt");
    CHECK(i >= 0, "CL relocation followed: deep/<file> reconstructed");
    /* the CL walk and the normal walk coexist: deep/ has two files, root has one plain file */
    CHECK(fulta_arc_count(arc) == 3, "exactly the 3 real files are listed (got %zu)", fulta_arc_count(arc));
    CHECK(find_entry(arc, "deep/second.txt") >= 0, "the relocated directory's second file is listed");
    CHECK(find_entry(arc, "top.txt") >= 0, "a plain root file is still listed alongside the CL walk");
    /* the placeholder must NOT appear as a 0-byte `deep` file, and no RR_MOVED artifact may leak (the 7z failures) */
    CHECK(find_entry(arc, "deep") < 0, "the CL placeholder is not listed as a bare file");
    for (size_t j = 0; j < fulta_arc_count(arc); j++)
        CHECK(strncmp(fulta_arc_entry(arc, j)->name, "rr_moved", 8) != 0 &&
              strncmp(fulta_arc_entry(arc, j)->name, "RR_MOVED", 8) != 0, "no RR_MOVED artifact is listed");
    if (i >= 0) {
        cap_t c = {0};
        fulta_arc_sink_t s = {&c, cap_write};
        fulta_arc_err_t e = fulta_arc_extract(arc, (size_t)i, &s);
        CHECK(e == FULTA_ARC_OK && c.n == 20 && memcmp(c.buf, "relocated deep file\n", 20) == 0,
              "relocated file extracts its content (%s, %zu bytes)", fulta_arc_strerror(e), c.n);
    }
    fulta_arc_close(arc);
}

/* zisofs.iso: transparently-compressed files. The listing must show the UNCOMPRESSED size (read from the zisofs
 * header, not the compressed extent) and the method "zisofs"; content is checked by the manifest. */
static void test_zisofs(void) {
    fulta_arc_t *arc = open_fixture("iso/zisofs.iso");
    if (!arc) { SKIP("zisofs.iso not present"); return; }
    int i = find_entry(arc, "ZBIG.BIN");
    CHECK(i >= 0, "a zisofs file is listed");
    if (i >= 0) {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, (size_t)i);
        CHECK(en->size == 70000, "zisofs entry reports the uncompressed size (got %llu)", (unsigned long long)en->size);
        CHECK(en->packed_size < en->size, "the compressed extent is smaller than the uncompressed size");
        CHECK(en->method && strcmp(en->method, "zisofs") == 0, "method is zisofs (%s)", en->method ? en->method : "?");
    }
    fulta_arc_close(arc);
}

int main(void) {
    test_cycle();
    test_root_out_of_range();
    test_zisofs();
    test_joliet();
    test_rr_symlink();
    test_rr_ce_name();
    test_precedence();
    test_reloc();
    return t_report("test_iso");
}
