/* test_encrypt.c - FultaArc writing encrypted archives (D-016). Writes a ZIP with WinZip AES (128/192/256) and
 * reads it back with the library's own, corpus-validated reader: the right password extracts to the original bytes,
 * a wrong one is rejected, and two writes use different salts. 7z encryption (7zAES) is a later increment. */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>

typedef struct { uint8_t *d; size_t n, cap; } cap_t;
static fulta_arc_err_t cap_write(void *ctx, const void *data, size_t n) {
    cap_t *c = ctx;
    if (c->n + n > c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 4096;
        while (nc < c->n + n) nc *= 2;
        uint8_t *nd = realloc(c->d, nc);
        if (!nd) return FULTA_ARC_ERR_NOMEM;
        c->d = nd; c->cap = nc;
    }
    memcpy(c->d + c->n, data, n); c->n += n; return FULTA_ARC_OK;
}
static fulta_arc_err_t cap_patch(void *ctx, uint64_t off, const void *data, size_t n) {
    cap_t *c = ctx;
    if (off + n > c->n) return FULTA_ARC_ERR_IO;
    memcpy(c->d + off, data, n); return FULTA_ARC_OK;
}

static const char *g_pw;
static fulta_arc_err_t pw_cb(void *ctx, uint32_t a, const char **o) {
    (void)ctx; if (a || !g_pw) return FULTA_ARC_ERR_PASSWORD_NEEDED; *o = g_pw; return FULTA_ARC_OK;
}

/* write one encrypted ZIP with the given scheme into *buf */
static fulta_arc_err_t write_enc(cap_t *buf, fulta_arc_encrypt_t scheme, const char *pw,
                                 const uint8_t *data, size_t n, const char *name) {
    fulta_arc_write_entry_t e = {.name = name, .data = data, .size = n, .mtime = 1600000000};
    fulta_arc_write_sink_t sink = {.ctx = buf, .write = cap_write, .patch = cap_patch};
    fulta_arc_write_options_t o = {.level = 6, .password = pw, .encryption = scheme};
    return fulta_arc_write(FULTA_ARC_FORMAT_ZIP, &e, 1, &o, &sink);
}

static void roundtrip(fulta_arc_encrypt_t scheme, const char *label) {
    uint8_t data[3000];
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 31 + 7);
    cap_t buf = {0};
    CHECK(write_enc(&buf, scheme, "s3cr3t!", data, sizeof data, "doc.bin") == FULTA_ARC_OK, "%s: write", label);
    if (!buf.d) { free(buf.d); return; }

    /* the entry is marked encrypted */
    fulta_arc_t *a = NULL;
    fulta_arc_options_t rp = {.password = pw_cb};
    g_pw = "s3cr3t!";
    CHECK(fulta_arc_open_memory(buf.d, buf.n, &rp, &a) == FULTA_ARC_OK && a, "%s: reopen", label);
    if (a) {
        const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
        CHECK(e && (e->flags & FULTA_ARC_ENTRY_ENCRYPTED), "%s: entry is flagged encrypted", label);
        void *d = NULL; size_t dn = 0;
        CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) == FULTA_ARC_OK && dn == sizeof data && d &&
              memcmp(d, data, dn) == 0, "%s: extracts to the original bytes", label);
        fulta_arc_free(d);
        fulta_arc_close(a);
    }

    /* wrong password is rejected (fresh handle) */
    g_pw = "wrong";
    CHECK(fulta_arc_open_memory(buf.d, buf.n, &rp, &a) == FULTA_ARC_OK && a, "%s: reopen 2", label);
    if (a) {
        fulta_arc_err_t ce = fulta_arc_check_password(a, 0);
        CHECK(ce == FULTA_ARC_ERR_PASSWORD_WRONG, "%s: wrong password -> WRONG (got %s)", label, fulta_arc_strerror(ce));
        fulta_arc_close(a);
    }
    free(buf.d);
}

/* 7z 7zAES: write two files in one (solid) folder, read both back, reject a wrong password. `method` exercises the
   inner coder under the AES capture path (DEFAULT, LZMA2, BZip2, ...). */
static void roundtrip_7z_mfs(int level, fulta_arc_method_t method, fulta_arc_filter_t filter,
                             fulta_arc_solid_t solid, const char *label) {
    uint8_t A[9000], B[1500];
    for (size_t i = 0; i < sizeof A; i++) A[i] = (uint8_t)(i * 17 + 3);
    for (size_t i = 0; i < sizeof B; i++) B[i] = (uint8_t)(i ^ 0x5a);
    fulta_arc_write_entry_t e[2] = {
        {.name = "p/a.bin", .data = A, .size = sizeof A, .mtime = 1600000000},
        {.name = "p/b.bin", .data = B, .size = sizeof B, .mtime = 1600000001},
    };
    cap_t buf = {0};
    fulta_arc_write_sink_t sink = {.ctx = &buf, .write = cap_write, .patch = cap_patch};
    fulta_arc_write_options_t o = {.level = level, .method = method, .filter = filter, .filter_delta_dist = 4,
                                   .solid = solid, .password = "Zee7Pass", .encryption = FULTA_ARC_ENCRYPT_7Z_AES256};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, e, 2, &o, &sink) == FULTA_ARC_OK, "%s: write 7z", label);
    if (!buf.d) { free(buf.d); return; }

    fulta_arc_t *a = NULL;
    fulta_arc_options_t rp = {.password = pw_cb};
    g_pw = "Zee7Pass";
    CHECK(fulta_arc_open_memory(buf.d, buf.n, &rp, &a) == FULTA_ARC_OK && a, "%s: reopen 7z", label);
    if (a) {
        CHECK(fulta_arc_count(a) == 2, "%s: two entries", label);
        void *d = NULL; size_t dn = 0;
        CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) == FULTA_ARC_OK && dn == sizeof A && d && memcmp(d, A, dn) == 0,
              "%s: a.bin bytes", label); fulta_arc_free(d);
        d = NULL; dn = 0;
        CHECK(fulta_arc_extract_alloc(a, 1, &d, &dn) == FULTA_ARC_OK && dn == sizeof B && d && memcmp(d, B, dn) == 0,
              "%s: b.bin bytes", label); fulta_arc_free(d);
        fulta_arc_close(a);
    }
    g_pw = "nope";
    if (fulta_arc_open_memory(buf.d, buf.n, &rp, &a) == FULTA_ARC_OK && a) {
        void *d = NULL; size_t dn = 0;
        CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) != FULTA_ARC_OK, "%s: wrong password fails to extract", label);
        fulta_arc_free(d);
        fulta_arc_close(a);
    }
    free(buf.d);
}

static void roundtrip_7z_mf(int level, fulta_arc_method_t method, fulta_arc_filter_t filter, const char *label) {
    roundtrip_7z_mfs(level, method, filter, FULTA_ARC_SOLID_DEFAULT, label);
}
static void roundtrip_7z_m(int level, fulta_arc_method_t method, const char *label) {
    roundtrip_7z_mf(level, method, FULTA_ARC_FILTER_NONE, label);
}
static void roundtrip_7z(int level, const char *label) { roundtrip_7z_m(level, FULTA_ARC_METHOD_DEFAULT, label); }

int main(void) {
    roundtrip(FULTA_ARC_ENCRYPT_ZIP_AES128, "aes128");
    roundtrip(FULTA_ARC_ENCRYPT_ZIP_AES192, "aes192");
    roundtrip(FULTA_ARC_ENCRYPT_ZIP_AES256, "aes256");
    roundtrip(FULTA_ARC_ENCRYPT_DEFAULT, "default");
    roundtrip_7z(5, "7z-lzma");
    roundtrip_7z(0, "7z-store");
    roundtrip_7z_m(6, FULTA_ARC_METHOD_LZMA2, "7z-lzma2+aes");
    roundtrip_7z_m(6, FULTA_ARC_METHOD_BZIP2, "7z-bzip2+aes");
    roundtrip_7z_m(6, FULTA_ARC_METHOD_DEFLATE, "7z-deflate+aes");
    roundtrip_7z_m(6, FULTA_ARC_METHOD_PPMD, "7z-ppmd+aes");
    /* a content filter chained before the main coder, then 7zAES over it (the 3-coder folder): decode is
       packed -> AES -> main -> filter -> plaintext */
    roundtrip_7z_mf(6, FULTA_ARC_METHOD_LZMA2, FULTA_ARC_FILTER_BCJ_X86, "7z-bcj+lzma2+aes");
    roundtrip_7z_mf(6, FULTA_ARC_METHOD_LZMA,  FULTA_ARC_FILTER_DELTA,   "7z-delta+lzma+aes");
    /* SOLID_OFF + AES: each file is its own folder with its own 7zAES coder (salt/iv) */
    roundtrip_7z_mfs(6, FULTA_ARC_METHOD_LZMA2, FULTA_ARC_FILTER_NONE, FULTA_ARC_SOLID_OFF, "7z-solidoff+aes");

    /* distinct salts: two writes of the same input differ */
    uint8_t x[64]; for (size_t i = 0; i < sizeof x; i++) x[i] = (uint8_t)i;
    cap_t b1 = {0}, b2 = {0};
    write_enc(&b1, FULTA_ARC_ENCRYPT_ZIP_AES256, "pw", x, sizeof x, "x");
    write_enc(&b2, FULTA_ARC_ENCRYPT_ZIP_AES256, "pw", x, sizeof x, "x");
    CHECK(b1.d && b2.d && b1.n == b2.n && memcmp(b1.d, b2.d, b1.n) != 0, "two encrypted writes differ (random salt)");
    free(b1.d); free(b2.d);

    /* a 7z scheme on a ZIP, and a ZIP scheme on a 7z, are both rejected */
    cap_t b3 = {0};
    CHECK(write_enc(&b3, FULTA_ARC_ENCRYPT_7Z_AES256, "pw", x, sizeof x, "x") == FULTA_ARC_ERR_INVALID_ARG,
          "7z scheme on ZIP -> INVALID_ARG");
    free(b3.d);
    cap_t b4 = {0};
    fulta_arc_write_entry_t e7 = {.name = "x", .data = x, .size = sizeof x};
    fulta_arc_write_sink_t s7 = {.ctx = &b4, .write = cap_write, .patch = cap_patch};
    fulta_arc_write_options_t o7 = {.level = 6, .password = "pw", .encryption = FULTA_ARC_ENCRYPT_ZIP_AES256};
    CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, &e7, 1, &o7, &s7) == FULTA_ARC_ERR_INVALID_ARG,
          "ZIP scheme on 7z -> INVALID_ARG");
    free(b4.d);

    return t_report("test_encrypt");
}
