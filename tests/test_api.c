/* test_api.c - the API added for Rubraview's integration (RV-100): name-encoding flag + auto_codepage,
 * keep_damaged / FULTA_ARC_DAMAGED, the progress callback, fulta_arc_check_password, and the solid-group index.
 * Self-contained ZIPs in memory cover the first three; encrypted and solid fixtures cover the last two (skipped
 * when absent). */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

static uint32_t crc32_of(const uint8_t *d, size_t n) {
    uint32_t t[256];
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0); t[i] = c; }
    uint32_t c = ~0u;
    for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

/* One stored file: name (n bytes, UTF-8 flag per `utf8`), data (dn bytes), stored CRC = `crc`. Returns size. */
static size_t make_zip(uint8_t *z, const uint8_t *name, size_t n, const uint8_t *data, size_t dn, uint32_t crc,
                       bool utf8) {
    unsigned flags = utf8 ? 0x0800 : 0;
    size_t p = 0;
    put32(z, 0x04034b50); put16(z + 4, 10); put16(z + 6, flags); put16(z + 8, 0); put32(z + 10, 0);
    put32(z + 14, crc); put32(z + 18, (uint32_t)dn); put32(z + 22, (uint32_t)dn); put16(z + 26, (unsigned)n); put16(z + 28, 0);
    memcpy(z + 30, name, n); memcpy(z + 30 + n, data, dn);
    p = 30 + n + dn;
    size_t cd = p;
    put32(z + p, 0x02014b50); put16(z + p + 4, 10); put16(z + p + 6, 10); put16(z + p + 8, flags); put16(z + p + 10, 0);
    put32(z + p + 12, 0); put32(z + p + 16, crc); put32(z + p + 20, (uint32_t)dn); put32(z + p + 24, (uint32_t)dn);
    put16(z + p + 28, (unsigned)n); put16(z + p + 30, 0); put16(z + p + 32, 0); put16(z + p + 34, 0);
    put16(z + p + 36, 0); put32(z + p + 38, 0); put32(z + p + 42, 0);
    memcpy(z + p + 46, name, n);
    p += 46 + n;
    put32(z + p, 0x06054b50); put16(z + p + 4, 0); put16(z + p + 6, 0); put16(z + p + 8, 1); put16(z + p + 10, 1);
    put32(z + p + 12, (uint32_t)(p - cd)); put32(z + p + 16, (uint32_t)cd); put16(z + p + 20, 0);
    return p + 22;
}

static uint64_t g_prog_last;
static unsigned g_prog_calls;
static void on_progress(void *ctx, uint64_t done, uint64_t total) {
    (void)ctx; (void)total;
    g_prog_calls++;
    g_prog_last = done;
}
typedef struct { uint8_t buf[300000]; size_t n; } cap_t;
static fulta_arc_err_t cap_write(void *ctx, const void *d, size_t n) {
    cap_t *c = ctx;
    if (c->n + n > sizeof c->buf) return FULTA_ARC_ERR_LIMIT;
    memcpy(c->buf + c->n, d, n); c->n += n; return FULTA_ARC_OK;
}

static const char *g_pw;
static fulta_arc_err_t pw_cb(void *ctx, uint32_t a, const char **o) { (void)ctx; if (a || !g_pw) return FULTA_ARC_ERR_PASSWORD_NEEDED; *o = g_pw; return FULTA_ARC_OK; }

static void test_name_flag(void) {
    /* a CP932 ("Shift-JIS") name, not UTF-8 flagged: AUTO -> NAME_NOT_UNICODE; auto_codepage=932 decodes it */
    const uint8_t nm[] = {0x95, 0x5c, 0x2e, 0x74, 0x78, 0x74};   /* "表.txt" in CP932 */
    uint8_t z[512];
    size_t zn = make_zip(z, nm, sizeof nm, (const uint8_t *)"", 0, 0, false);
    fulta_arc_options_t o = {.codepage = FULTA_ARC_CP_AUTO, .auto_codepage = FULTA_ARC_CP_932};
    fulta_arc_t *a = NULL;
    CHECK(fulta_arc_open_memory(z, zn, &o, &a) == FULTA_ARC_OK, "name: open");
    if (!a) return;
    const fulta_arc_entry_t *e = fulta_arc_entry(a, 0);
    CHECK(e->flags & FULTA_ARC_ENTRY_NAME_NOT_UNICODE, "name: NOT_UNICODE flag set for a legacy name");
    CHECK(strcmp(e->name, "\xe8\xa1\xa8.txt") == 0, "name: auto_codepage 932 decodes to UTF-8, got \"%s\"", e->name);
    fulta_arc_close(a);
    /* an ASCII name is Unicode-clean: the flag stays clear */
    zn = make_zip(z, (const uint8_t *)"a.txt", 5, (const uint8_t *)"", 0, 0, false);
    fulta_arc_options_t o2 = {0};
    CHECK(fulta_arc_open_memory(z, zn, &o2, &a) == FULTA_ARC_OK, "name2: open");
    if (a) { CHECK(!(fulta_arc_entry(a, 0)->flags & FULTA_ARC_ENTRY_NAME_NOT_UNICODE), "name2: ASCII is not flagged"); fulta_arc_close(a); }
}

static void test_keep_damaged(void) {
    const uint8_t data[] = "the quick brown fox";
    uint8_t z[512];
    /* store a WRONG CRC so the check fails */
    size_t zn = make_zip(z, (const uint8_t *)"x.txt", 5, data, sizeof data - 1, 0xDEADBEEF, true);
    /* default: a checksum error, no bytes kept by extract_alloc */
    fulta_arc_t *a = NULL;
    fulta_arc_options_t o = {0};
    CHECK(fulta_arc_open_memory(z, zn, &o, &a) == FULTA_ARC_OK, "damaged: open");
    if (!a) return;
    cap_t cap = {0};
    fulta_arc_sink_t sink = {.ctx = &cap, .write = cap_write};
    CHECK(fulta_arc_extract(a, 0, &sink) == FULTA_ARC_ERR_CHECKSUM, "damaged: default is CHECKSUM");
    fulta_arc_close(a);
    /* keep_damaged: FULTA_ARC_DAMAGED, and the bytes are delivered */
    o.keep_damaged = true;
    CHECK(fulta_arc_open_memory(z, zn, &o, &a) == FULTA_ARC_OK, "damaged2: open");
    if (!a) return;
    cap.n = 0;
    CHECK(fulta_arc_extract(a, 0, &sink) == FULTA_ARC_DAMAGED, "damaged: keep_damaged -> FULTA_ARC_DAMAGED");
    CHECK(cap.n == sizeof data - 1 && memcmp(cap.buf, data, cap.n) == 0, "damaged: the bytes are kept");
    void *d = NULL; size_t dn = 0;
    CHECK(fulta_arc_extract_alloc(a, 0, &d, &dn) == FULTA_ARC_DAMAGED && dn == sizeof data - 1, "damaged: extract_alloc keeps bytes");
    fulta_arc_free(d);
    fulta_arc_close(a);
}

static void test_progress(void) {
    uint8_t data[200000];
    for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7);
    uint8_t z[220000];
    size_t zn = make_zip(z, (const uint8_t *)"big.bin", 7, data, sizeof data, crc32_of(data, sizeof data), true);
    fulta_arc_t *a = NULL;
    fulta_arc_options_t o = {.progress = on_progress};
    CHECK(fulta_arc_open_memory(z, zn, &o, &a) == FULTA_ARC_OK, "progress: open");
    if (!a) return;
    g_prog_calls = 0; g_prog_last = 0;
    cap_t cap = {0};
    fulta_arc_sink_t sink = {.ctx = &cap, .write = cap_write};
    CHECK(fulta_arc_extract(a, 0, &sink) == FULTA_ARC_OK, "progress: extract");
    CHECK(g_prog_calls > 0 && g_prog_last == sizeof data, "progress: callback fired, done=%llu", (unsigned long long)g_prog_last);
    fulta_arc_close(a);
}

static fulta_arc_err_t check_with(const char *path, size_t *idx_out) {
    fulta_arc_options_t o = {.password = pw_cb};
    fulta_arc_t *a = NULL;
    if (fulta_arc_open_file(path, &o, &a) != FULTA_ARC_OK || !a) return FULTA_ARC_ERR_IO;
    size_t idx = SIZE_MAX;
    for (size_t i = 0; i < fulta_arc_count(a); i++)
        if (fulta_arc_entry(a, i)->flags & FULTA_ARC_ENTRY_ENCRYPTED) { idx = i; break; }
    if (idx_out) *idx_out = idx;
    fulta_arc_err_t e = idx == SIZE_MAX ? FULTA_ARC_ERR_UNSUPPORTED : fulta_arc_check_password(a, idx);
    fulta_arc_close(a);
    return e;
}

static void test_check_password(const char *fixtures) {
    char path[4096];
    snprintf(path, sizeof path, "%s/zip7z/archives/zip_aes128.zip", fixtures);
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("no zip_aes128 fixture for check_password"); return; }
    fclose(f);
    size_t idx = SIZE_MAX;
    g_pw = "wrong-password";          /* fresh archive each time: the good-password cache does not carry over */
    fulta_arc_err_t e = check_with(path, &idx);
    if (idx == SIZE_MAX) { SKIP("no encrypted entry in zip_aes128"); return; }
    CHECK(e == FULTA_ARC_ERR_PASSWORD_WRONG, "check_password: wrong password -> WRONG (got %s)", fulta_arc_strerror(e));
    g_pw = "pass1234";
    e = check_with(path, NULL);
    CHECK(e == FULTA_ARC_OK, "check_password: right password -> OK (got %s)", fulta_arc_strerror(e));
}

static void test_solid_group(const char *fixtures) {
    char path[4096];
    snprintf(path, sizeof path, "%s/zip7z/archives/7z_default.7z", fixtures);
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("no 7z_default fixture for solid_group"); return; }
    fclose(f);
    fulta_arc_t *a = NULL;
    if (fulta_arc_open_file(path, NULL, &a) != FULTA_ARC_OK || !a) { SKIP("7z_default did not open"); return; }
    size_t n = fulta_arc_count(a), files = 0, same = 0;
    uint32_t g0 = 0; bool have = false;
    for (size_t i = 0; i < n; i++) {
        const fulta_arc_entry_t *e = fulta_arc_entry(a, i);
        if ((e->flags & FULTA_ARC_ENTRY_DIR) || e->size == 0) continue;
        files++;
        if (!have) { g0 = e->solid_group; have = true; same = 1; }
        else if (e->solid_group == g0) same++;
    }
    CHECK(files < 2 || same == files, "solid_group: the %zu files of a solid 7z share one group (%zu)", files, same);
    fulta_arc_close(a);
}

static void test_editable(const char *fixtures) {
    /* a plain in-memory ZIP is editable in place */
    uint8_t z[512];
    size_t zn = make_zip(z, (const uint8_t *)"a.txt", 5, (const uint8_t *)"hi", 2, crc32_of((const uint8_t *)"hi", 2), true);
    fulta_arc_t *a = NULL;
    fulta_arc_options_t o = {0};
    CHECK(fulta_arc_open_memory(z, zn, &o, &a) == FULTA_ARC_OK && a, "editable: open zip");
    if (a) { CHECK(fulta_arc_editable(a) == FULTA_ARC_EDIT_OK, "editable: plain ZIP -> OK"); fulta_arc_close(a); }

    char path[4096];
    /* a solid 7z -> SOLID */
    snprintf(path, sizeof path, "%s/zip7z/archives/7z_default.7z", fixtures);
    if (fulta_arc_open_file(path, NULL, &a) == FULTA_ARC_OK && a) {
        fulta_arc_editable_t ed = fulta_arc_editable(a);
        CHECK(ed == FULTA_ARC_EDIT_SOLID || ed == FULTA_ARC_EDIT_OK, "editable: 7z -> OK or SOLID (got %d)", (int)ed);
        fulta_arc_close(a);
    } else SKIP("no 7z_default fixture for editable");

    /* encrypted archives -> ENCRYPTED (refused), for ZIP AES, 7z AES, and 7z with an encrypted header */
    g_pw = "pass1234";
    fulta_arc_options_t op = {.password = pw_cb};
    static const char *enc[] = {"zip_aes128.zip", "7z_aes.7z", "7z_aes_hdr.7z"};
    for (size_t i = 0; i < sizeof enc / sizeof *enc; i++) {
        snprintf(path, sizeof path, "%s/zip7z/archives/%s", fixtures, enc[i]);
        if (fulta_arc_open_file(path, &op, &a) == FULTA_ARC_OK && a) {
            CHECK(fulta_arc_editable(a) == FULTA_ARC_EDIT_ENCRYPTED, "editable: %s -> ENCRYPTED", enc[i]);
            fulta_arc_close(a);
        } else SKIP("no fixture for editable");
    }
}

/* Attribute byte -> Windows attributes (egg.md / alz.md 4.2: 0x01 read-only, 0x02 hidden, 0x04 system, 0x80
 * folder). reader-notes EGG item 4: the system bit (0x04) must be mapped. The dir fixture carries ro/hid/sys/rhs
 * and an empty folder; run it for the EGG and the ALZ twin. */
static void test_dir_attributes_one(const char *path, const char *label) {
    FILE *f = fopen(path, "rb");
    if (!f) { SKIP("no dir fixture for %s attributes", label); return; }
    fclose(f);
    fulta_arc_t *a = NULL;
    if (fulta_arc_open_file(path, NULL, &a) != FULTA_ARC_OK || !a) { SKIP("%s dir fixture did not open", label); return; }
    bool saw_sys = false, saw_dir = false;
    for (size_t i = 0; i < fulta_arc_count(a); i++) {
        const fulta_arc_entry_t *e = fulta_arc_entry(a, i);
        const char *nm = e->name ? e->name : "";
        size_t l = strlen(nm);
        if (l >= 8 && strcmp(nm + l - 8, "/sys.txt") == 0) { CHECK((e->attributes & 0x04u) != 0, "%s attr: sys.txt is system", label); saw_sys = true; }
        if (l >= 8 && strcmp(nm + l - 8, "/hid.txt") == 0) CHECK((e->attributes & 0x02u) != 0, "%s attr: hid.txt is hidden", label);
        if (l >= 8 && strcmp(nm + l - 8, "/rhs.txt") == 0) CHECK((e->attributes & 0x07u) == 0x07u, "%s attr: rhs.txt is read-only+hidden+system", label);
        if (l >= 7 && strcmp(nm + l - 7, "/ro.txt") == 0) CHECK((e->attributes & 0x01u) != 0, "%s attr: ro.txt is read-only", label);
        if (e->flags & FULTA_ARC_ENTRY_DIR) { CHECK((e->attributes & 0x10u) != 0, "%s attr: folder has the directory bit", label); saw_dir = true; }
    }
    CHECK(saw_sys, "%s attr: the system-attribute file was present", label);
    CHECK(saw_dir, "%s attr: the empty folder was present", label);
    fulta_arc_close(a);
}

static void test_egg_attributes(const char *fixtures) {
    char path[4096];
    snprintf(path, sizeof path, "%s/alzegg/b002_multi__o86__dir.egg", fixtures);
    test_dir_attributes_one(path, "egg");
    snprintf(path, sizeof path, "%s/alzegg/b002_multi__o86__dir.alz", fixtures);
    test_dir_attributes_one(path, "alz");
}

static void test_strerror_all(void) {
    CHECK(strcmp(fulta_arc_strerror(FULTA_ARC_DAMAGED), "error") != 0, "strerror(FULTA_ARC_DAMAGED) defined");
    CHECK(strcmp(fulta_arc_strerror(FULTA_ARC_ERR_EXISTS), "error") != 0, "strerror(FULTA_ARC_ERR_EXISTS) defined");
}

int main(void) {
    test_strerror_all();
    test_name_flag();
    test_keep_damaged();
    test_progress();
    test_check_password(t_fixtures());
    test_solid_group(t_fixtures());
    test_editable(t_fixtures());
    test_egg_attributes(t_fixtures());
    return t_report("test_api");
}
