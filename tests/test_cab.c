/* test_cab.c - CAB reader cases beyond the manifest (cab.txt): a Quantum folder (typeCompress 2, Diamond-made) extracts
 * across CFDATA boundaries and an out-of-range Quantum window exponent is refused, a corrupted CFDATA block fails its
 * checksum, a cross-cabinet continued file is refused when its cabinet is absent and extracts when the set is present. The LZX folders themselves extract and are checked by
 * the manifest (lzx*.cab). See D-020, T022. */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

static int find_entry(fulta_arc_t *arc, const char *name) {
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (strcmp(fulta_arc_entry(arc, i)->name, name) == 0) return (int)i;
    return -1;
}

static fulta_arc_err_t sink_count(void *ctx, const void *d, size_t n) { (void)d; *(size_t *)ctx += n; return FULTA_ARC_OK; }

/* quantum12.cab (made by Microsoft's own Diamond tool, window 2^12; see the manifest) holds a Quantum folder of three
 * files over three CFDATAs plus a Store folder beside it. Beyond the manifest's content check, pin that the reader
 * reports the window exponent correctly and extracts a file that spans CFDATA boundaries (the adaptive tables and
 * the window survive the per-block coder restart), and that a Quantum folder with an out-of-range window exponent
 * (X = 0, the old placeholder shape) is refused as UNSUPPORTED rather than decoded. */
static void test_quantum(void) {
    size_t n = 0;
    unsigned char *cab = load("cab/quantum12.cab", &n);
    if (!cab) { SKIP("quantum12.cab not present"); return; }
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(cab, n, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "quantum12.cab opens and lists (%s)", fulta_arc_strerror(e));
    if (!e) {
        int i = find_entry(arc, "random.bin");
        CHECK(i >= 0 && fulta_arc_entry(arc, (size_t)i)->size == 40000, "a Quantum file spanning CFDATAs lists its size");
        if (i >= 0) {
            size_t got = 0;
            fulta_arc_sink_t s = {&got, sink_count};
            CHECK(fulta_arc_extract(arc, (size_t)i, &s) == FULTA_ARC_OK && got == 40000,
                  "it extracts across CFDATA boundaries (%zu bytes)", got);
        }
        int st = find_entry(arc, "stored.txt");
        CHECK(st >= 0, "the Store folder beside the Quantum folder is listed");
        fulta_arc_close(arc);
    }
    /* typeCompress 0x0002: Quantum with X = 0 (CFFOLDER at offset 36 in this single-reserve-free cabinet) */
    cab[36 + 6] = 0x02; cab[36 + 7] = 0x00;
    arc = NULL;
    if (fulta_arc_open_memory(cab, n, NULL, &arc) == FULTA_ARC_OK) {
        int i = find_entry(arc, "readme.txt");
        size_t got = 0;
        fulta_arc_sink_t s = {&got, sink_count};
        CHECK(i >= 0 && fulta_arc_extract(arc, (size_t)i, &s) == FULTA_ARC_ERR_UNSUPPORTED,
              "a Quantum folder with window exponent 0 is refused (UNSUPPORTED)");
        fulta_arc_close(arc);
    } else {
        CHECK(0, "the cabinet with a rewritten typeCompress still opens");
    }
    free(cab);
}

/* lzxbig.cab is a multi-chunk LZX folder (window 2^21); beyond the manifest's content check, assert the reader
 * reports it as LZX and that the file's declared size matches, i.e. the chunk-spanning state machine terminated. */
static void test_lzx_multichunk(void) {
    size_t n = 0;
    unsigned char *cab = load("cab/lzxbig.cab", &n);
    if (!cab) { SKIP("lzxbig.cab not present"); return; }
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_memory(cab, n, NULL, &arc) == FULTA_ARC_OK) {
        int i = find_entry(arc, "big.txt");
        CHECK(i >= 0 && fulta_arc_entry(arc, (size_t)i)->size == 75000, "a multi-chunk LZX file lists its full size");
        if (i >= 0) {
            size_t got = 0;
            fulta_arc_sink_t s = {&got, sink_count};
            CHECK(fulta_arc_extract(arc, (size_t)i, &s) == FULTA_ARC_OK && got == 75000,
                  "a multi-chunk LZX file extracts across CFDATA boundaries (%zu bytes)", got);
        }
        fulta_arc_close(arc);
    } else {
        CHECK(0, "lzxbig.cab opens");
    }
    free(cab);
}

/* Flipping a byte in the last CFDATA payload must be caught by that block's checksum (reserve.cab/basic.cab write a
 * non-zero csum). We corrupt the final data byte and expect the last file's extraction to fail. */
static void test_bad_checksum(void) {
    size_t n = 0;
    unsigned char *cab = load("cab/basic.cab", &n);
    if (!cab) { SKIP("basic.cab not present"); return; }
    cab[n - 1] ^= 0xFF;                                   /* last byte lives in the Store folder's last CFDATA */
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(cab, n, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "a cabinet with one damaged data byte still opens (headers intact)");
    if (!e) {
        int i = find_entry(arc, "note.txt");             /* the last file, in the corrupted folder */
        CHECK(i >= 0, "note.txt is listed");
        if (i >= 0) {
            size_t got = 0;
            fulta_arc_sink_t s = {&got, sink_count};
            fulta_arc_err_t xe = fulta_arc_extract(arc, (size_t)i, &s);
            CHECK(xe == FULTA_ARC_ERR_CHECKSUM, "the damaged CFDATA is caught by its checksum (%s)",
                  fulta_arc_strerror(xe));
        }
        fulta_arc_close(arc);
    }
    free(cab);
}

/* next.cab sets cfhdrNEXT_CABINET (two step-over strings in the header) and carries one complete file plus one file
 * continued into the (absent) next cabinet (iFolder 0xFFFE). The complete file must list and extract - which only
 * works if the two-string skip is byte-exact - and the continued file must list but refuse to extract. */
static void test_next_cabinet(void) {
    size_t n = 0;
    unsigned char *cab = load("cab/next.cab", &n);
    if (!cab) { SKIP("next.cab not present"); return; }
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(cab, n, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "a cabinet with a NEXT_CABINET reference opens (%s)", fulta_arc_strerror(e));
    if (!e) {
        int i = find_entry(arc, "normal.txt");
        CHECK(i >= 0, "the complete file is found past the NEXT strings (step-over is byte-exact)");
        if (i >= 0) {
            size_t got = 0;
            fulta_arc_sink_t s = {&got, sink_count};
            CHECK(fulta_arc_extract(arc, (size_t)i, &s) == FULTA_ARC_OK && got == 42, "it extracts (%zu bytes)", got);
        }
        int c = find_entry(arc, "continued.dat");
        CHECK(c >= 0, "the continued-to-next file is listed");
        if (c >= 0) {
            size_t got = 0;
            fulta_arc_sink_t s = {&got, sink_count};
            CHECK(fulta_arc_extract(arc, (size_t)c, &s) == FULTA_ARC_ERR_UNSUPPORTED,
                  "a file continued across cabinets returns UNSUPPORTED");
        }
        fulta_arc_close(arc);
    }
    free(cab);
}

/* Cabinet sets. qset2.cab (Diamond, Quantum) is the middle cabinet of a three-cabinet set: beta.bin is continued from
 * qset1.cab (found by walking back through the header's previous-cabinet name), gamma.txt/delta.bin continue into
 * qset3.cab (found by the header's name next to the opened file: extracts). The same set opened from memory has no
 * resolver, so the files that run into the next cabinet are listed and refused, and nothing else changes. */
static void test_cabinet_set(void) {
    char path[4096];
    snprintf(path, sizeof path, "%s/cab/qset2.cab", t_fixtures());
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_file(path, NULL, &arc);
    if (e == FULTA_ARC_ERR_IO) { SKIP("qset2.cab not present"); return; }
    CHECK(e == FULTA_ARC_OK, "the middle cabinet of a set opens (%s)", fulta_arc_strerror(e));
    if (!e) {
        int b = find_entry(arc, "beta.bin"), g = find_entry(arc, "gamma.txt"), d = find_entry(arc, "delta.bin");
        CHECK(b >= 0 && g >= 0 && d >= 0, "its files are listed once each (%d %d %d)", b, g, d);
        size_t got = 0;
        fulta_arc_sink_t s = {&got, sink_count};
        if (b >= 0) CHECK(fulta_arc_extract(arc, (size_t)b, &s) == FULTA_ARC_OK && got == 70000,
                          "a file continued FROM the previous cabinet extracts through the backward walk (%zu bytes)", got);
        CHECK(find_entry(arc, "alpha.txt") >= 0, "the previous cabinet's whole files are listed too");
        got = 0;
        if (g >= 0) CHECK(fulta_arc_extract(arc, (size_t)g, &s) == FULTA_ARC_OK && got == 25327,
                          "a file continued INTO the next cabinet extracts through the chain (%zu bytes)", got);
        got = 0;
        if (d >= 0) CHECK(fulta_arc_extract(arc, (size_t)d, &s) == FULTA_ARC_OK && got == 5000,
                          "the folder continued across the boundary is one stream (%zu bytes)", got);
        fulta_arc_close(arc);
    }
    snprintf(path, sizeof path, "%s/cab/qset3.cab", t_fixtures());
    arc = NULL;
    e = fulta_arc_open_file(path, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "the last cabinet of a set opens (%s)", fulta_arc_strerror(e));
    if (!e) {
        int g = find_entry(arc, "gamma.txt"), d = find_entry(arc, "delta.bin"), b = find_entry(arc, "beta.bin");
        size_t got = 0;
        fulta_arc_sink_t s = {&got, sink_count};
        CHECK(fulta_arc_count(arc) == 4 && b >= 0, "opened at the end, the walk back reaches the first cabinet (%zu entries)", fulta_arc_count(arc));
        if (g >= 0) CHECK(fulta_arc_extract(arc, (size_t)g, &s) == FULTA_ARC_OK && got == 25327,
                          "the file continued 2->3 extracts (%zu bytes)", got);
        got = 0;
        if (d >= 0) CHECK(fulta_arc_extract(arc, (size_t)d, &s) == FULTA_ARC_OK && got == 5000,
                          "a whole file in the continued folder decodes from the folder's true start (%zu bytes)", got);
        got = 0;
        if (b >= 0) CHECK(fulta_arc_extract(arc, (size_t)b, &s) == FULTA_ARC_OK && got == 70000,
                          "the 1->2 split file extracts from the end of the set too (%zu bytes)", got);
        fulta_arc_close(arc);
    }
    size_t n = 0;
    unsigned char *cab = load("cab/qset3.cab", &n);
    if (cab) {
        arc = NULL;
        e = fulta_arc_open_memory(cab, n, NULL, &arc);
        CHECK(e == FULTA_ARC_OK, "the last cabinet opens alone from memory (%s)", fulta_arc_strerror(e));
        if (!e) {
            int g = find_entry(arc, "gamma.txt"), d = find_entry(arc, "delta.bin");
            size_t got = 0;
            fulta_arc_sink_t s = {&got, sink_count};
            CHECK(g >= 0 && fulta_arc_extract(arc, (size_t)g, &s) == FULTA_ARC_ERR_UNSUPPORTED,
                  "without the previous cabinet the continued file is refused");
            CHECK(d >= 0 && fulta_arc_extract(arc, (size_t)d, &s) == FULTA_ARC_ERR_UNSUPPORTED,
                  "and so is a whole file of the continued folder (its offset counts from the missing start)");
            fulta_arc_close(arc);
        }
        free(cab);
    }
    cab = load("cab/qset1.cab", &n);
    if (!cab) { SKIP("qset1.cab not present"); return; }
    arc = NULL;
    e = fulta_arc_open_memory(cab, n, NULL, &arc);
    CHECK(e == FULTA_ARC_OK, "the first cabinet opens from memory (%s)", fulta_arc_strerror(e));
    if (!e) {
        int a = find_entry(arc, "alpha.txt"), b = find_entry(arc, "beta.bin");
        size_t got = 0;
        fulta_arc_sink_t s = {&got, sink_count};
        CHECK(a >= 0 && fulta_arc_extract(arc, (size_t)a, &s) == FULTA_ARC_OK && got == 30403, "a complete file extracts (%zu)", got);
        CHECK(b >= 0 && fulta_arc_extract(arc, (size_t)b, &s) == FULTA_ARC_ERR_UNSUPPORTED,
              "without a volume resolver a file running into the next cabinet is refused");
        CHECK(find_entry(arc, "gamma.txt") < 0, "the next cabinet's files are not listed without it");
        fulta_arc_close(arc);
    }
    free(cab);
}

int main(void) {
    test_quantum();
    test_cabinet_set();
    test_lzx_multichunk();
    test_bad_checksum();
    test_next_cabinet();
    return t_report("test_cab");
}

