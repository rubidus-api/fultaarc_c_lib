/* test_rar15.c - RAR 1.4 / 1.5 reader cases beyond the manifest (rar15.txt).
 *
 * The archives are the rar15 and rar15-enc3 clean rooms' (private fixtures/rar15, skipped when absent), plus
 * synthetic in-memory KAT cases; sizes and CRC-32s quoted here are from expected*.txt / gate measurements. The
 * cases: a RAR 1.5 encrypted archive (Store and methods 0x30..0x35) decodes with the right password and refuses a
 * wrong one or none (D-048); RAR 1.5 stream-cipher KATs (empty, "a", raw high bytes, 77-byte cutoff, 0-byte); a
 * RAR 1.4 encrypted solid archive decodes with the right password and refuses a wrong one or none (D-030); the entry
 * the room's known_gaps.txt names; a RAR 1.5 archive behind a self-extractor front; a volume set opened without its
 * other volumes; a damaged stream (an error, DAMAGED with keep_damaged); and routing: RAR 1.5 archives here, RAR
 * 2.0 and later still to the RAR 2-7 reader. */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers -------------------------------------------------------------------------------------------------- */

typedef struct { uint32_t crc; uint64_t n; } crc_sink_t;

static fulta_arc_err_t crc_write(void *ctx, const void *data, size_t size) {
    crc_sink_t *s = ctx;
    const uint8_t *p = data;
    uint32_t c = ~s->crc;
    for (size_t i = 0; i < size; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
    }
    s->crc = ~c;
    s->n += size;
    return FULTA_ARC_OK;
}

static int g_pw_calls;
static const char *const *g_pw_list;   /* the passwords the callback gives, in order; NULL-terminated */

static fulta_arc_err_t list_pw(void *ctx, uint32_t attempt, const char **out) {
    (void)ctx;
    g_pw_calls++;
    for (uint32_t k = 0; g_pw_list[k]; k++)
        if (k == attempt) { *out = g_pw_list[k]; return FULTA_ARC_OK; }
    return FULTA_ARC_ERR_PASSWORD_NEEDED;
}

static bool fixture(const char *name, char *path, size_t cap) {
    snprintf(path, cap, "%s/rar15/%s", t_fixtures(), name);
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("%s: fixtures/rar15 not present", name); return false; }
    fclose(f);
    return true;
}

static size_t find(const fulta_arc_t *a, const char *name) {
    for (size_t i = 0; i < fulta_arc_count(a); i++)
        if (strcmp(fulta_arc_entry(a, i)->name, name) == 0) return i;
    return SIZE_MAX;
}

/* Extract `name` and compare with RAR's own size and CRC-32. */
static void check_entry(fulta_arc_t *a, const char *label, const char *name, uint64_t size, uint32_t crc) {
    size_t i = find(a, name);
    CHECK(i != SIZE_MAX, "%s: %s not listed", label, name);
    if (i == SIZE_MAX) return;
    crc_sink_t cs = {0};
    fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
    fulta_arc_err_t e = fulta_arc_extract(a, i, &sink);
    CHECK(e == FULTA_ARC_OK && cs.n == size && cs.crc == crc, "%s|%s: %s, %llu bytes crc %08x, want %llu %08x", label,
          name, fulta_arc_strerror(e), (unsigned long long)cs.n, cs.crc, (unsigned long long)size, crc);
}

/* ---- cases ---------------------------------------------------------------------------------------------------- */

/* RAR 1.5: encrypted entry decodes with the right password, retries past wrong ones, refuses wrong or missing
 * callback (D-048). */
static void test_encrypted_15(void) {
    static const char *const right[] = {"a", NULL}, *const wrong_then_right[] = {"b", "a", NULL},
                             *const wrong[] = {"b", NULL};
    char path[8192];
    const char *name = "s0378_zeros_pw-a.rar";       /* RAR 1.55, `a -m0 -pa` (room step S0378) */
    if (!fixture(name, path, sizeof path)) return;
    struct { const char *const *list; fulta_arc_err_t want; const char *label; } cases[] = {
        {right, FULTA_ARC_OK, "right"},
        {wrong_then_right, FULTA_ARC_OK, "wrong, then right"},
        {wrong, FULTA_ARC_ERR_PASSWORD_WRONG, "wrong"},
        {NULL, FULTA_ARC_ERR_PASSWORD_NEEDED, "no callback"},
    };
    for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
        g_pw_calls = 0;
        g_pw_list = cases[c].list;
        fulta_arc_options_t opt = {.password = cases[c].list ? list_pw : NULL};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "%s (%s): open: %s", name, cases[c].label, fulta_arc_strerror(e));
        if (e) continue;
        CHECK(fulta_arc_format(a) == FULTA_ARC_FORMAT_RAR && fulta_arc_count(a) == 1, "%s: format / count", name);
        const fulta_arc_entry_t *en = fulta_arc_entry(a, 0);
        CHECK((en->flags & FULTA_ARC_ENTRY_ENCRYPTED) && !(en->flags & FULTA_ARC_ENTRY_UNSUPPORTED),
              "%s|%s: flags %#x", name, en->name, en->flags);
        if (cases[c].want == FULTA_ARC_OK) {
            check_entry(a, cases[c].label, "A.DAT", 64, 0x758d6336u);
            CHECK(g_pw_calls > 0, "%s (%s): password callback not called", name, cases[c].label);
        } else {
            crc_sink_t cs = {0};
            fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
            e = fulta_arc_extract(a, 0, &sink);
            CHECK(e == cases[c].want && cs.n == 0, "%s (%s)|%s: %s, %llu bytes", name, cases[c].label, en->name,
                  fulta_arc_strerror(e), (unsigned long long)cs.n);
        }
        fulta_arc_close(a);
    }
}

/* RAR 1.5 methods 0x30..0x35 (Store through Best) on encrypted multi-file synthetic fixtures from enc3 P4. */
static void test_encrypted_15_methods(void) {
    static const struct { const char *file, *pw; } cases[] = {
        {"enc3_m0_0.rar", "az7,y~}S7?w)}}vAAK"},
        {"enc3_m1_0.rar", "P(kN`$nY4QSpdfg.v,DkIBQ?#}MVR:2"},
        {"enc3_m2_0.rar", "_XBBG.,w6sM~9I0(X3.?SKOOn,FMNb-+az_6s-4vd~p1OGgp~?:56~`yl8X"},
        {"enc3_m3_0.rar", "E=,[p;[yeo.Q:i4DkIpsFzP*xm#"},
        {"enc3_m4_0.rar", "fl?KDMAL?6,=pYoF,x[Gk?VO*nty]uvTwS5yh=i`"},
        {"enc3_m5_0.rar", "7!ISj@I8.jnw72A"},
    };
    for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
        char path[8192];
        if (!fixture(cases[c].file, path, sizeof path)) continue;
        const char *right[] = {cases[c].pw, NULL};
        g_pw_list = right;
        fulta_arc_options_t opt = {.password = list_pw};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "%s: open: %s", cases[c].file, fulta_arc_strerror(e));
        if (e) continue;
        check_entry(a, cases[c].file, "T.TXT", 15360, 0xc2d40f28u);
        check_entry(a, cases[c].file, "I.PNG", 6447, 0x19cd3b82u);
        check_entry(a, cases[c].file, "X.EXE", 1024, 0x162a76f8u);
        fulta_arc_close(a);
    }
    char path[8192];
    if (fixture("enc3_m3_0.rar", path, sizeof path)) {
        static const char *const wrong[] = {"wrong_password", NULL};
        g_pw_list = wrong;
        fulta_arc_options_t opt = {.password = list_pw};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "enc3_m3_0.rar (wrong): open: %s", fulta_arc_strerror(e));
        if (!e) {
            crc_sink_t cs = {0};
            fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
            e = fulta_arc_extract(a, 0, &sink);
            CHECK(e == FULTA_ARC_ERR_PASSWORD_WRONG && cs.n == 0, "enc3_m3_0.rar (wrong): %s, %llu bytes",
                  fulta_arc_strerror(e), (unsigned long long)cs.n);
            fulta_arc_close(a);
        }
    }
}

static uint32_t crc32_mem(const uint8_t *p, size_t n) {
    crc_sink_t cs = {0};
    (void)crc_write(&cs, p, n);
    return cs.crc;
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Build a minimal RAR 1.5 encrypted Store archive in buf (size = 57 + plen) for 0x00 plaintext. */
static size_t build_rar15_enc_zeros(uint8_t *buf, const uint8_t *payload, uint32_t plen, uint32_t plain_crc) {
    static const uint8_t mark[7] = {'R', 'a', 'r', '!', 0x1a, 0x07, 0x00};
    memcpy(buf, mark, 7);
    uint8_t *mh = buf + 7;
    memset(mh, 0, 13);
    mh[2] = 0x73;
    put_u16(mh + 5, 13);
    put_u16(mh, (uint16_t)(crc32_mem(mh + 2, 11) & 0xffffu));

    uint8_t *fh = buf + 20;
    memset(fh, 0, 37);
    fh[2] = 0x74;
    put_u16(fh + 3, 0x8004u);        /* ADD_SIZE + PASSWORD */
    put_u16(fh + 5, 37);             /* 32 + 5 ("Z.BIN") */
    put_u32(fh + 7, plen);           /* pack_size */
    put_u32(fh + 11, plen);          /* unp_size */
    put_u32(fh + 16, plain_crc);     /* file_crc */
    fh[24] = 0x0f;                   /* version 1.5 */
    fh[25] = 0x30;                   /* store */
    put_u16(fh + 26, 5);             /* name_size */
    memcpy(fh + 32, "Z.BIN", 5);
    put_u16(fh, (uint16_t)(crc32_mem(fh + 2, 35) & 0xffffu));
    if (plen && payload) memcpy(fh + 37, payload, plen);
    return (size_t)57 + plen;
}

/* In-memory KATs verifying empty password, "a", raw high bytes, exact 77-byte cutoff, and 0-byte entry. */
static void test_encrypted_15_kats(void) {
    static const uint8_t kat_empty[16] = {
        0x22, 0x74, 0xe0, 0x27, 0xab, 0x03, 0xee, 0x65, 0x36, 0x27, 0x50, 0xe9, 0x14, 0xa8, 0x6a, 0x6c
    };
    static const uint8_t kat_a[16] = {
        0x75, 0x2f, 0xa5, 0x44, 0x49, 0x5d, 0x1c, 0x92, 0x57, 0xbd, 0x3d, 0x4d, 0x73, 0xa4, 0xf5, 0x4b
    };
    static const uint8_t kat_raw[16] = {
        0xad, 0x4f, 0xaf, 0x0c, 0xb1, 0x27, 0x3c, 0xcd, 0xe5, 0xd9, 0x66, 0x63, 0x3b, 0xa8, 0x85, 0xc8
    };
    static const uint8_t kat_77A[16] = {
        0x96, 0xc0, 0x70, 0xa3, 0xd6, 0x18, 0xae, 0xe0, 0xd9, 0x69, 0xb8, 0x68, 0xad, 0x0a, 0x0c, 0x13
    };
    uint8_t zeros[16] = {0};
    uint32_t zcrc = crc32_mem(zeros, sizeof zeros);

    char pw80_same[81], pw80_diff[81];
    memset(pw80_same, 'A', 80); pw80_same[80] = '\0';
    pw80_same[77] = 'B'; pw80_same[78] = 'C'; pw80_same[79] = 'D';
    memset(pw80_diff, 'A', 80); pw80_diff[80] = '\0';
    pw80_diff[76] = 'Z';

    struct { const char *pw; const uint8_t *kat; fulta_arc_err_t want; const char *label; } cases[] = {
        {"", kat_empty, FULTA_ARC_OK, "empty pw"},
        {"a", kat_a, FULTA_ARC_OK, "pw 'a'"},
        {"\x80\xaa\xff", kat_raw, FULTA_ARC_OK, "raw high bytes"},
        {pw80_same, kat_77A, FULTA_ARC_OK, "77-byte cutoff (bytes 77..79 ignored)"},
        {pw80_diff, kat_77A, FULTA_ARC_ERR_PASSWORD_WRONG, "77-byte cutoff (byte 76 matters)"},
    };
    for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
        uint8_t arc_buf[128];
        size_t arc_len = build_rar15_enc_zeros(arc_buf, cases[c].kat, 16, zcrc);
        const char *pws[] = {cases[c].pw, NULL};
        g_pw_list = pws;
        fulta_arc_options_t opt = {.password = list_pw};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_memory(arc_buf, arc_len, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "kat %s: open: %s", cases[c].label, fulta_arc_strerror(e));
        if (e) continue;
        crc_sink_t cs = {0};
        fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
        e = fulta_arc_extract(a, 0, &sink);
        if (cases[c].want == FULTA_ARC_OK) {
            CHECK(e == FULTA_ARC_OK && cs.n == 16 && cs.crc == zcrc, "kat %s: %s, %llu bytes crc %08x",
                  cases[c].label, fulta_arc_strerror(e), (unsigned long long)cs.n, cs.crc);
        } else {
            CHECK(e == cases[c].want && cs.n == 0, "kat %s: %s, %llu bytes", cases[c].label, fulta_arc_strerror(e),
                  (unsigned long long)cs.n);
        }
        fulta_arc_close(a);
    }

    /* 0-byte encrypted RAR 1.5 entry: needs callback, succeeds with password. */
    uint8_t empty_buf[64];
    size_t empty_len = build_rar15_enc_zeros(empty_buf, NULL, 0, 0);
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(empty_buf, empty_len, NULL, &a);
    CHECK(e == FULTA_ARC_OK, "0-byte enc (no cb): open: %s", fulta_arc_strerror(e));
    if (!e) {
        crc_sink_t cs = {0};
        fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
        CHECK(fulta_arc_extract(a, 0, &sink) == FULTA_ARC_ERR_PASSWORD_NEEDED, "0-byte enc requires callback");
        fulta_arc_close(a);
    }
    const char *pws[] = {"pw", NULL};
    g_pw_list = pws;
    fulta_arc_options_t opt = {.password = list_pw};
    a = NULL;
    e = fulta_arc_open_memory(empty_buf, empty_len, &opt, &a);
    CHECK(e == FULTA_ARC_OK, "0-byte enc (with cb): open: %s", fulta_arc_strerror(e));
    if (!e) {
        crc_sink_t cs = {0};
        fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
        e = fulta_arc_extract(a, 0, &sink);
        CHECK(e == FULTA_ARC_OK && cs.n == 0, "0-byte enc extract: %s", fulta_arc_strerror(e));
        fulta_arc_close(a);
    }
}

/* RAR 1.4 (RAR 1.40.2, solid, password `a`; expected14.txt): decoded with the right password, also when a wrong one
 * is offered first; a wrong password alone gives PASSWORD_WRONG and no bytes; no callback gives PASSWORD_NEEDED. The
 * entries are listed encrypted and not unsupported. */
static void test_encrypted_14(void) {
    static const char *const right[] = {"a", NULL}, *const wrong_then_right[] = {"b", "a", NULL},
                             *const wrong[] = {"b", NULL};
    char path[8192];
    const char *name = "s0389_14_enc_solid.rar";
    if (!fixture(name, path, sizeof path)) return;
    struct { const char *const *list; fulta_arc_err_t want; const char *label; } cases[] = {
        {right, FULTA_ARC_OK, "right"},
        {wrong_then_right, FULTA_ARC_OK, "wrong, then right"},
        {wrong, FULTA_ARC_ERR_PASSWORD_WRONG, "wrong"},
        {NULL, FULTA_ARC_ERR_PASSWORD_NEEDED, "no callback"},
    };
    for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
        g_pw_list = cases[c].list;
        fulta_arc_options_t opt = {.password = cases[c].list ? list_pw : NULL};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "%s (%s): open: %s", name, cases[c].label, fulta_arc_strerror(e));
        if (e) continue;
        for (size_t i = 0; i < fulta_arc_count(a); i++) {
            const fulta_arc_entry_t *en = fulta_arc_entry(a, i);
            CHECK((en->flags & FULTA_ARC_ENTRY_ENCRYPTED) && !(en->flags & FULTA_ARC_ENTRY_UNSUPPORTED),
                  "%s|%s: flags %#x", name, en->name, en->flags);
        }
        if (cases[c].want == FULTA_ARC_OK) {
            check_entry(a, cases[c].label, "A.TXT", 30000, 0xce63b09eu);
            check_entry(a, cases[c].label, "B.DAT", 20000, 0x368f21d1u);
        } else {
            for (size_t i = 0; i < fulta_arc_count(a); i++) {
                crc_sink_t cs = {0};
                fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
                e = fulta_arc_extract(a, i, &sink);
                /* the callback's attempt count runs over the archive (fa_password): once it has run out, a later
                 * entry finds no password to try and says PASSWORD_NEEDED, as with the other readers */
                bool ok = e == cases[c].want || (i > 0 && e == FULTA_ARC_ERR_PASSWORD_NEEDED);
                CHECK(ok && cs.n == 0, "%s (%s)|%s: %s, %llu bytes", name, cases[c].label,
                      fulta_arc_entry(a, i)->name, fulta_arc_strerror(e), (unsigned long long)cs.n);
            }
        }
        fulta_arc_close(a);
    }
}

/* The room's known_gaps.txt names s0200_two.rar|A.TXT as an entry its decoder got wrong; at the final state pinned
 * here it extracts to RAR's own size and CRC-32 (measured 2026-10-22), as does B.TXT. */
static void test_known_gap(void) {
    char path[8192];
    if (!fixture("s0200_two.rar", path, sizeof path)) return;
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_file(path, NULL, &a);
    CHECK(e == FULTA_ARC_OK, "s0200_two.rar: open: %s", fulta_arc_strerror(e));
    if (e) return;
    check_entry(a, "s0200_two.rar", "A.TXT", 3000, 0x9ac58245u);
    check_entry(a, "s0200_two.rar", "B.TXT", 2000, 0x88a438ecu);
    fulta_arc_close(a);
}

/* 64 bytes the room built ('MZ', 'RSFX') in front of s0006_text2000_m3.rar: found by the search pass. */
static void test_sfx_front(void) {
    char path[8192];
    if (!fixture("s0372_front64.exe", path, sizeof path)) return;
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_file(path, NULL, &a);
    CHECK(e == FULTA_ARC_OK, "s0372_front64.exe: open: %s", fulta_arc_strerror(e));
    if (e) return;
    check_entry(a, "s0372_front64.exe", "A.TXT", 2000, 0x35204594u);
    fulta_arc_close(a);
}

/* The first volume alone, from memory (no volume callback): the entry that runs on is split and reports the
 * missing volume, never wrong bytes. */
static void test_volume_missing(void) {
    char path[8192];
    if (!fixture("s0201_vol3.rar", path, sizeof path)) return;
    size_t n = 0;
    unsigned char *buf = t_read_file(path, &n);
    CHECK(buf != NULL, "s0201_vol3.rar: read");
    if (!buf) return;
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_memory(buf, n, NULL, &a);
    CHECK(e == FULTA_ARC_OK, "s0201_vol3.rar alone: open: %s", fulta_arc_strerror(e));
    if (!e) {
        size_t i = find(a, "R.TXT");
        CHECK(i != SIZE_MAX && (fulta_arc_entry(a, i)->flags & FULTA_ARC_ENTRY_SPLIT), "s0201_vol3.rar: R.TXT split");
        if (i != SIZE_MAX) {
            crc_sink_t cs = {0};
            fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
            e = fulta_arc_extract(a, i, &sink);
            CHECK(e == FULTA_ARC_ERR_VOLUME_MISSING && cs.n == 0, "s0201_vol3.rar|R.TXT alone: %s, %llu bytes",
                  fulta_arc_strerror(e), (unsigned long long)cs.n);
        }
        fulta_arc_close(a);
    }
    free(buf);
}

/* Bytes near the end of the compressed stream changed: an error, never OK; with keep_damaged, DAMAGED. */
static void test_damaged(void) {
    char path[8192];
    if (!fixture("s0006_text2000_m3.rar", path, sizeof path)) return;
    size_t n = 0;
    unsigned char *buf = t_read_file(path, &n);
    CHECK(buf != NULL && n > 64, "s0006_text2000_m3.rar: read");
    if (!buf || n <= 64) { free(buf); return; }
    for (size_t k = n - 40; k < n - 8; k++) buf[k] ^= 0x5A;
    for (int keep = 0; keep < 2; keep++) {
        fulta_arc_options_t opt = {.keep_damaged = keep != 0};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_memory(buf, n, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "damaged: open: %s", fulta_arc_strerror(e));
        if (e) continue;
        crc_sink_t cs = {0};
        fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
        e = fulta_arc_extract(a, 0, &sink);
        if (keep) CHECK(e == FULTA_ARC_DAMAGED, "damaged, keep_damaged: %s", fulta_arc_strerror(e));
        else CHECK(e == FULTA_ARC_ERR_CHECKSUM || e == FULTA_ARC_ERR_CORRUPT, "damaged: %s", fulta_arc_strerror(e));
        fulta_arc_close(a);
    }
    free(buf);
}

/* RAR 1.5 goes to this reader (method RAR1.5, RAR1.4 for the older format); a RAR 2.0 archive still opens with the
 * RAR 2-7 reader (method RAR2.0). */
static void test_routing(void) {
    char path[8192];
    struct { const char *name, *entry, *method; } cases[] = {
        {"s0006_text2000_m3.rar", "A.TXT", "RAR1.5"},
        {"s0383_14_text_m3.rar", "A.TXT", "RAR1.4"},
    };
    for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
        if (!fixture(cases[c].name, path, sizeof path)) continue;
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, NULL, &a);
        CHECK(e == FULTA_ARC_OK, "%s: open: %s", cases[c].name, fulta_arc_strerror(e));
        if (e) continue;
        size_t i = find(a, cases[c].entry);
        CHECK(i != SIZE_MAX && strcmp(fulta_arc_entry(a, i)->method, cases[c].method) == 0, "%s: method %s",
              cases[c].name, i != SIZE_MAX ? fulta_arc_entry(a, i)->method : "-");
        fulta_arc_close(a);
    }
    snprintf(path, sizeof path, "%s/rar/rar2_m1.rar", t_fixtures());
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("rar2_m1.rar: fixtures/rar not present"); return; }
    fclose(f);
    fulta_arc_t *a = NULL;
    fulta_arc_err_t e = fulta_arc_open_file(path, NULL, &a);
    CHECK(e == FULTA_ARC_OK, "rar2_m1.rar: open: %s", fulta_arc_strerror(e));
    if (e) return;
    CHECK(fulta_arc_count(a) > 0 && strcmp(fulta_arc_entry(a, 0)->method, "RAR2.0") == 0, "rar2_m1.rar: method %s",
          fulta_arc_count(a) ? fulta_arc_entry(a, 0)->method : "-");
    fulta_arc_close(a);
}

/* RAR 1.55 solid encrypted, multi-volume split encrypted, and >64 KiB solid+split encrypted archives (D-048). */
static void test_encrypted_15_solid_and_vol(void) {
    char path[8192];
    if (fixture("enc3_15_solid.rar", path, sizeof path)) {
        static const char *const pw[] = {"SolidPw15", NULL};
        g_pw_list = pw;
        fulta_arc_options_t opt = {.password = list_pw};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "enc3_15_solid.rar: open: %s", fulta_arc_strerror(e));
        if (!e) {
            CHECK(fulta_arc_count(a) == 3, "enc3_15_solid.rar: count %zu", fulta_arc_count(a));
            for (size_t i = 0; i < fulta_arc_count(a); i++) {
                uint32_t fl = fulta_arc_entry(a, i)->flags;
                CHECK((fl & FULTA_ARC_ENTRY_ENCRYPTED) && !(fl & FULTA_ARC_ENTRY_UNSUPPORTED),
                      "enc3_15_solid.rar[%zu]: flags %#x", i, fl);
                if (i == 0) CHECK(!(fl & FULTA_ARC_ENTRY_SOLID), "enc3_15_solid.rar[0]: not solid");
                else CHECK((fl & FULTA_ARC_ENTRY_SOLID) != 0, "enc3_15_solid.rar[%zu]: solid", i);
            }
            /* Extract out of order (B.TXT first) then C.BIN and A.TXT to test solid rewind + decryption. */
            check_entry(a, "enc3_15_solid.rar", "B.TXT", 6900, 0xcda7c7b9u);
            check_entry(a, "enc3_15_solid.rar", "C.BIN", 5000, 0x3bce376bu);
            check_entry(a, "enc3_15_solid.rar", "A.TXT", 7920, 0x1764d652u);
            fulta_arc_close(a);
        }
    }

    if (fixture("enc3_15_vol0.rar", path, sizeof path)) {
        static const char *const pw[] = {"VolStore", NULL};
        g_pw_list = pw;
        fulta_arc_options_t opt = {.password = list_pw};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "enc3_15_vol0.rar: open: %s", fulta_arc_strerror(e));
        if (!e) {
            size_t idx = find(a, "BIG.BIN");
            CHECK(idx != SIZE_MAX, "enc3_15_vol0.rar: BIG.BIN listed");
            if (idx != SIZE_MAX) {
                uint32_t fl = fulta_arc_entry(a, idx)->flags;
                uint32_t want = FULTA_ARC_ENTRY_SPLIT | FULTA_ARC_ENTRY_ENCRYPTED;
                CHECK((fl & want) == want && !(fl & FULTA_ARC_ENTRY_UNSUPPORTED),
                      "enc3_15_vol0.rar: flags %#x", fl);
            }
            check_entry(a, "enc3_15_vol0.rar", "BIG.BIN", 9000, 0x7e9782a4u);
            fulta_arc_close(a);
        }
    }

    if (fixture("enc3_15_svol.rar", path, sizeof path)) {
        static const char *const ok_pw[] = {"WrapVol99", NULL}, *const wtr_pw[] = {"Bad1", "Bad2", "WrapVol99", NULL},
                                 *const bad_pw[] = {"Bad1", NULL};
        struct { const char *const *list; fulta_arc_err_t want; const char *label; } cases[] = {
            {ok_pw, FULTA_ARC_OK, "right"},
            {wtr_pw, FULTA_ARC_OK, "wrong, then right"},
            {bad_pw, FULTA_ARC_ERR_PASSWORD_WRONG, "wrong"},
            {NULL, FULTA_ARC_ERR_PASSWORD_NEEDED, "no callback"},
        };
        for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
            g_pw_list = cases[c].list;
            fulta_arc_options_t opt = {.password = cases[c].list ? list_pw : NULL};
            fulta_arc_t *a = NULL;
            fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
            CHECK(e == FULTA_ARC_OK, "enc3_15_svol.rar (%s): open: %s", cases[c].label, fulta_arc_strerror(e));
            if (e) continue;
            if (cases[c].want == FULTA_ARC_OK) {
                check_entry(a, cases[c].label, "F3.DAT", 36800, 0x121fc387u);
            } else {
                size_t idx = find(a, "F3.DAT");
                CHECK(idx != SIZE_MAX, "enc3_15_svol.rar (%s): F3.DAT", cases[c].label);
                if (idx != SIZE_MAX) {
                    crc_sink_t cs = {0};
                    fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
                    e = fulta_arc_extract(a, idx, &sink);
                    CHECK(e == cases[c].want && cs.n == 0, "enc3_15_svol.rar (%s)|F3.DAT: %s, %llu bytes",
                          cases[c].label, fulta_arc_strerror(e), (unsigned long long)cs.n);
                }
            }
            fulta_arc_close(a);
        }

        /* Rewind order (F3 -> F1 -> F2 -> F3) on a single handle over >64 KiB solid + 5 volumes. */
        g_pw_list = ok_pw;
        fulta_arc_options_t opt = {.password = list_pw};
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &a);
        CHECK(e == FULTA_ARC_OK, "enc3_15_svol.rar (rewind): open: %s", fulta_arc_strerror(e));
        if (!e) {
            check_entry(a, "rewind", "F3.DAT", 36800, 0x121fc387u);
            check_entry(a, "rewind", "F1.DAT", 36800, 0x5233e924u);
            check_entry(a, "rewind", "F2.DAT", 36800, 0x6bc7d85eu);
            check_entry(a, "rewind", "F3.DAT", 36800, 0x121fc387u);
            fulta_arc_close(a);
        }
    }
}

int main(void) {
    test_encrypted_15();
    test_encrypted_15_methods();
    test_encrypted_15_kats();
    test_encrypted_15_solid_and_vol();
    test_encrypted_14();
    test_known_gap();
    test_sfx_front();
    test_volume_missing();
    test_damaged();
    test_routing();
    return t_report("test_rar15");
}
