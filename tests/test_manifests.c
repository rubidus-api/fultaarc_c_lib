/* test_manifests.c - open every archive a manifest names and check each listed entry's size and CRC-32.
 *
 * Manifests (archive|entry|size|crc32, '#' comments) are public; the archives are private fixtures:
 *   tests/manifests/zip7z.txt      -> FIXTURES/zip7z/archives/
 *   tests/manifests/<set>.txt      -> FIXTURES/<set>/            (more sets as the readers are ported)
 * An optional fifth field cp=NNN opens that archive with code page NNN (Windows numbers; 20866 = KOI8-R).
 * A missing fixture directory skips its set. Password for encrypted samples: "pass1234" (zip7z).
 * Every archive is opened with a caller allocator that is proven's alloc_check in front of its heap (D-038): every
 * block FultaArc makes, resizes and releases while reading must be one of its own, with its true size, and none may
 * be left after the close. */
#include "fulta/arc.h"
#include "testlib.h"

#include "proven/alloc_check.h"
#include "proven/heap.h"
#include "proven/panic.h"

#include <stdint.h>

/* ---- the checking allocator, as a caller would supply it ---- */
static proven_alloc_check_t g_chk;
static proven_allocator_t g_checked;
static int g_alloc_panics;
static void count_panic(const char *msg) {
    if (g_alloc_panics++ < 5) fprintf(stderr, "alloc_check: %s\n", msg);
}
static void *ua_alloc(void *ctx, size_t size, size_t align) {
    (void)ctx;
    proven_result_mem_mut_t r = g_checked.alloc_fn(g_checked.ctx, size, align);
    return r.err ? NULL : r.value.ptr;
}
static void *ua_realloc(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align) {
    (void)ctx;
    proven_result_mem_mut_t r = g_checked.realloc_fn(g_checked.ctx, ptr, old_size, new_size, align);
    return r.err ? NULL : r.value.ptr;
}
static void ua_free(void *ctx, void *ptr) { (void)ctx; g_checked.free_fn(g_checked.ctx, ptr); }
static const fulta_arc_allocator_t g_user_alloc = {.alloc = ua_alloc, .realloc = ua_realloc, .free = ua_free};

typedef struct crc_sink { uint32_t crc; uint64_t n; uint32_t table[256]; } crc_sink_t;

static fulta_arc_err_t crc_write(void *ctx, const void *data, size_t n) {
    crc_sink_t *c = ctx;
    const unsigned char *p = data;
    uint32_t v = ~c->crc;
    for (size_t i = 0; i < n; i++) v = c->table[(v ^ p[i]) & 0xFF] ^ (v >> 8);
    c->crc = ~v;
    c->n += n;
    return FULTA_ARC_OK;
}

static const char *const *g_passwords;   /* tried in order; NULL-terminated */
static fulta_arc_err_t pw_cb(void *ctx, uint32_t attempt, const char **out) {
    (void)ctx;
    for (uint32_t i = 0; g_passwords && g_passwords[i]; i++)
        if (i == attempt) { *out = g_passwords[i]; return FULTA_ARC_OK; }
    return FULTA_ARC_ERR_PASSWORD_NEEDED;
}

static bool big_ok(void) { const char *e = getenv("FULTA_ARC_BIG_TESTS"); return e && *e == '1'; }

typedef struct want { char entry[512]; unsigned long long size; unsigned crc; } want_t;

static bool g_exact;   /* the manifest lists every file of each archive */

static void check_archive(const char *dir, const char *archive, fulta_arc_codepage_t cp, want_t *w, size_t nw) {
    char path[8192];
    snprintf(path, sizeof path, "%s/%s", dir, archive);
    FILE *probe = fopen(path, "rb");
    if (!probe) { SKIP("%s: not present (optional fixture)", archive); return; }
    fclose(probe);
    fulta_arc_options_t opt = {.password = pw_cb, .codepage = cp, .allocator = g_user_alloc};
    size_t faults0 = g_chk.faults;
    fulta_arc_t *arc = NULL;
    fulta_arc_err_t e = fulta_arc_open_file(path, &opt, &arc);
    CHECK(e == FULTA_ARC_OK, "%s: open: %s", archive, fulta_arc_strerror(e));
    if (e) return;
    crc_sink_t cs;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        cs.table[i] = c;
    }
    size_t files = 0;
    for (size_t i = 0; i < fulta_arc_count(arc); i++)
        if (!(fulta_arc_entry(arc, i)->flags & FULTA_ARC_ENTRY_DIR)) files++;
    if (g_exact) CHECK(files == nw, "%s: %zu files listed, manifest has %zu", archive, files, nw);
    else CHECK(files >= nw, "%s: %zu files listed, manifest has %zu", archive, files, nw);
    for (size_t k = 0; k < nw; k++) {
        size_t idx = SIZE_MAX;
        for (size_t i = 0; i < fulta_arc_count(arc); i++)
            if (strcmp(fulta_arc_entry(arc, i)->name, w[k].entry) == 0) { idx = i; break; }
        CHECK(idx != SIZE_MAX, "%s: entry %s not found", archive, w[k].entry);
        if (idx == SIZE_MAX) continue;
        if (w[k].size > (1ull << 30) && !big_ok()) { SKIP("%s|%s: %llu bytes (FULTA_ARC_BIG_TESTS=1 to run)", archive, w[k].entry, w[k].size); continue; }
        cs.crc = 0;
        cs.n = 0;
        fulta_arc_sink_t sink = {.ctx = &cs, .write = crc_write};
        e = fulta_arc_extract(arc, idx, &sink);
        CHECK(e == FULTA_ARC_OK, "%s|%s: extract: %s", archive, w[k].entry, fulta_arc_strerror(e));
        if (e) continue;
        CHECK(cs.n == w[k].size && cs.crc == w[k].crc, "%s|%s: got %llu bytes crc %08x, want %llu %08x", archive,
              w[k].entry, (unsigned long long)cs.n, cs.crc, w[k].size, w[k].crc);
    }
    fulta_arc_close(arc);
    CHECK(g_chk.faults == faults0, "%s: allocator faults (wrong block, size or double free): %zu", archive,
          (size_t)(g_chk.faults - faults0));
    CHECK(proven_alloc_check_live(&g_chk) == 0, "%s: %zu blocks left after the close", archive,
          (size_t)proven_alloc_check_live(&g_chk));
}

static void run_set(const char *manifest, const char *dir, const char *const *passwords, bool exact) {
    g_exact = exact;
    FILE *f = fopen(manifest, "r");
    if (!f) { SKIP("no manifest %s", manifest); return; }
    g_passwords = passwords;
    want_t *w = NULL;
    size_t nw = 0, cap = 0;
    char cur[512] = "", line[2048];
    fulta_arc_codepage_t cur_cp = FULTA_ARC_CP_AUTO;
    int archives = 0;
    bool any_dir_checked = false, dir_ok = false;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char a[512], en[512];
        unsigned long long sz;
        unsigned crc;
        char *p1 = strchr(line, '|'), *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        if (!p1 || !p2) continue;
        snprintf(a, sizeof a, "%.*s", (int)(p1 - line), line);
        snprintf(en, sizeof en, "%.*s", (int)(p2 - p1 - 1), p1 + 1);
        if (sscanf(p2 + 1, "%llu|%x", &sz, &crc) != 2) continue;
        fulta_arc_codepage_t lcp = FULTA_ARC_CP_AUTO;
        const char *cpf = strstr(p2 + 1, "|cp=");
        if (cpf) {
            int n = atoi(cpf + 4);
            lcp = n == 932 ? FULTA_ARC_CP_932 : n == 936 ? FULTA_ARC_CP_936 : n == 949 ? FULTA_ARC_CP_949
                : n == 950 ? FULTA_ARC_CP_950 : n == 1252 ? FULTA_ARC_CP_1252 : n == 437 ? FULTA_ARC_CP_437 : n == 866 ? FULTA_ARC_CP_866
                : n == 1251 ? FULTA_ARC_CP_1251 : n == 20866 ? FULTA_ARC_CP_KOI8R : FULTA_ARC_CP_AUTO;
        }
        if (!any_dir_checked) {
            /* the set is present when its directory holds the first archive or any later one */
            char pth[8192];
            snprintf(pth, sizeof pth, "%s/%s", dir, a);
            FILE *t = fopen(pth, "rb");
            dir_ok = t != NULL;
            if (t) fclose(t);
            any_dir_checked = true;
            if (!dir_ok) { SKIP("fixtures for %s not present (%s)", manifest, dir); break; }
        }
        if (strcmp(a, cur) != 0) {
            if (nw) { check_archive(dir, cur, cur_cp, w, nw); archives++; }
            nw = 0;
            snprintf(cur, sizeof cur, "%s", a);
            cur_cp = lcp;
        }
        if (nw == cap) {
            cap = cap ? cap * 2 : 64;
            w = realloc(w, cap * sizeof *w);
        }
        snprintf(w[nw].entry, sizeof w[nw].entry, "%s", en);
        w[nw].size = sz;
        w[nw].crc = crc;
        nw++;
    }
    if (nw && dir_ok) { check_archive(dir, cur, cur_cp, w, nw); archives++; }
    fclose(f);
    free(w);
    if (dir_ok) printf("%s: %d archives checked\n", manifest, archives);
}

int main(void) {
    static proven_alloc_check_entry_t rec[1 << 17];
    proven_set_panic_handler(count_panic);
    g_checked = proven_alloc_check_wrap(&g_chk, proven_heap_allocator(), rec, sizeof rec / sizeof rec[0]);
    char dir[4096];
    static const char *const zip_pw[] = {"pass1234", NULL};
    static const char *const rar_pw[] = {"pass", "\xeb\xb9\x84\xeb\xb0\x80 \xeb\xb2\x88\xed\x98\xb8", "password", NULL};
    snprintf(dir, sizeof dir, "%s/zip7z/archives", t_fixtures());
    run_set("tests/manifests/zip7z.txt", dir, zip_pw, true);
    snprintf(dir, sizeof dir, "%s/rar", t_fixtures());
    run_set("tests/manifests/rar.txt", dir, rar_pw, false);
    static const char *const la_pw[] = {"12345678", "password", NULL};
    snprintf(dir, sizeof dir, "%s/libarchive", t_fixtures());
    run_set("tests/manifests/libarchive.txt", dir, la_pw, false);
    snprintf(dir, sizeof dir, "%s/pkzip", t_fixtures());
    run_set("tests/manifests/pkzip.txt", dir, zip_pw, true);
    snprintf(dir, sizeof dir, "%s/pkzip-made", t_fixtures());
    run_set("tests/manifests/pkzip-made.txt", dir, zip_pw, true);
    snprintf(dir, sizeof dir, "%s/legacy", t_fixtures());
    run_set("tests/manifests/legacy.txt", dir, zip_pw, true);
    /* The earlier ALZ and EGG baselines (Rubraview's ALZ fixtures; egg-verify's 601-archive pack). */
    static const char *const alz_pw[] = {"pass", "\xeb\xb9\x84\xeb\xb0\x80", NULL};
    snprintf(dir, sizeof dir, "%s/alz", t_fixtures());
    run_set("tests/manifests/alz.txt", dir, alz_pw, false);
    snprintf(dir, sizeof dir, "%s/egg", t_fixtures());
    run_set("tests/manifests/egg.txt", dir, zip_pw, false);
    /* ALZ + EGG, clean room fultaarc-cleanroom-alzegg. Per-archive passwords from its passwords.txt (CP949 bytes);
     * the reader tries each as CP949 then UTF-8. 비밀 = \xeb\xb9\x84\xeb\xb0\x80. Names UTF-8 (ALZ CP949 under AUTO). */
    static const char *const ae_pw[] = {"abc", "pass", "\xeb\xb9\x84\xeb\xb0\x80", "cafe", "a b c", "\"a b c\"", "??", NULL};
    snprintf(dir, sizeof dir, "%s/alzegg", t_fixtures());
    run_set("tests/manifests/alzegg.txt", dir, ae_pw, false);
    /* TAR (ustar/pax/GNU/v7), made by GNU tar; exact (every file listed). */
    snprintf(dir, sizeof dir, "%s/tar", t_fixtures());
    run_set("tests/manifests/tar.txt", dir, zip_pw, true);
    /* Unix compress (.Z): scratch encoder validated by `gzip -d`; one entry each, exact. */
    snprintf(dir, sizeof dir, "%s/compress", t_fixtures());
    run_set("tests/manifests/compress.txt", dir, zip_pw, true);
    /* lzip (.lz): LZMA1 payload from independent liblzma; one entry each, exact; trailer CRC verified on decode. */
    snprintf(dir, sizeof dir, "%s/lzip", t_fixtures());
    run_set("tests/manifests/lzip.txt", dir, zip_pw, true);
    /* ar (.a), made by GNU ar; exact (symbol tables / the // name table are not listed). */
    snprintf(dir, sizeof dir, "%s/ar", t_fixtures());
    run_set("tests/manifests/ar.txt", dir, zip_pw, true);
    /* cpio (newc/crc/odc/old-binary), made by tests/tools/cpioenc.py, validated by `7z x`; exact (dirs excluded). */
    snprintf(dir, sizeof dir, "%s/cpio", t_fixtures());
    run_set("tests/manifests/cpio.txt", dir, zip_pw, true);
    /* ISO 9660, made by tests/tools/isomake.py, validated by `7z x`; exact (directories excluded). */
    snprintf(dir, sizeof dir, "%s/iso", t_fixtures());
    run_set("tests/manifests/iso.txt", dir, zip_pw, true);
    /* CAB (Store + MSZIP), made by tests/tools/cabmake.py, validated by `7z x`/`7z t`; exact. */
    snprintf(dir, sizeof dir, "%s/cab", t_fixtures());
    run_set("tests/manifests/cab.txt", dir, zip_pw, true);
    /* UDF (ECMA-167), made by tests/tools/udfmake.py, validated by `7z x`; exact. */
    snprintf(dir, sizeof dir, "%s/udf", t_fixtures());
    run_set("tests/manifests/udf.txt", dir, zip_pw, true);
    /* LHA/LZH: the lhasa 0.6.0 test corpus; rows from the pinned lhasa binary (tests/tools/lhamanifest.py). */
    snprintf(dir, sizeof dir, "%s/lha", t_fixtures());
    run_set("tests/manifests/lha.txt", dir, zip_pw, true);
    /* RAR 1.4 / 1.5: the rar15 clean room's archives; rows from RAR's own extraction (tests/tools/rar15manifest.py). */
    snprintf(dir, sizeof dir, "%s/rar15", t_fixtures());
    run_set("tests/manifests/rar15.txt", dir, zip_pw, true);
    return t_report("test_manifests");
}
