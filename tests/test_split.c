/* test_split.c - FultaArc split output (D-035): ZIP `name.z01 ... name.zip` and 7z `name.7z.001 ...` written through
 * a caller's volume sink and to files (temporaries renamed only on success), read back by FultaArc's own reader; a
 * header that would land on a volume boundary; a set of one volume; an older, longer set refused (EXISTS); cancel and
 * failure leaving nothing; and 7z header encryption (encrypt_names, D-036). */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdint.h>
#include <stdio.h>

/* ---- an in-memory volume sink ---- */

typedef struct { uint8_t *d; size_t n, cap; int closed; } mvol_t;
typedef struct { mvol_t v[256]; uint32_t n; int finished, ok; uint32_t count; int fail_at; } mset_t;

static fulta_arc_err_t mv_write(void *ctx, const void *data, size_t n) {
    mvol_t *v = ctx;
    if (v->closed) return FULTA_ARC_ERR_IO;
    if (v->n + n > v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 4096;
        while (nc < v->n + n) nc *= 2;
        uint8_t *nd = realloc(v->d, nc);
        if (!nd) return FULTA_ARC_ERR_NOMEM;
        v->d = nd; v->cap = nc;
    }
    memcpy(v->d + v->n, data, n); v->n += n; return FULTA_ARC_OK;
}
static fulta_arc_err_t mv_patch(void *ctx, uint64_t off, const void *data, size_t n) {
    mvol_t *v = ctx;
    if (v->closed || off + n > v->n) return FULTA_ARC_ERR_IO;
    memcpy(v->d + off, data, n); return FULTA_ARC_OK;
}
static fulta_arc_err_t ms_open(void *ctx, uint32_t index, fulta_arc_write_sink_t *out) {
    mset_t *s = ctx;
    if (index != s->n || index >= 256) return FULTA_ARC_ERR_INVALID_ARG;
    if (s->fail_at && (int)index + 1 == s->fail_at) return FULTA_ARC_ERR_IO;
    s->v[index] = (mvol_t){0};
    s->n++;
    *out = (fulta_arc_write_sink_t){.ctx = &s->v[index], .write = mv_write, .patch = mv_patch};
    return FULTA_ARC_OK;
}
static fulta_arc_err_t ms_close(void *ctx, uint32_t index) {
    mset_t *s = ctx;
    if (index >= s->n || s->v[index].closed) return FULTA_ARC_ERR_INVALID_ARG;
    s->v[index].closed = 1;
    return FULTA_ARC_OK;
}
static fulta_arc_err_t ms_finish(void *ctx, uint32_t count, int ok) {
    mset_t *s = ctx;
    s->finished++; s->ok = ok; s->count = count;
    return FULTA_ARC_OK;
}
static fulta_arc_volume_sink_t ms_sink(mset_t *s) {
    return (fulta_arc_volume_sink_t){.ctx = s, .open = ms_open, .close = ms_close, .finish = ms_finish};
}
static void ms_free(mset_t *s) { for (uint32_t i = 0; i < s->n; i++) free(s->v[i].d); s->n = 0; }

/* reading the set back: the first (7z) or last (ZIP) volume opened, the others through the volume callback */
typedef struct { mset_t *s; fulta_arc_format_t fmt; } rd_set_t;
static fulta_arc_err_t ms_volume(void *ctx, uint32_t index, const char *name, fulta_arc_source_t *out) {
    rd_set_t *r = ctx;
    (void)name;
    uint32_t i = r->fmt == FULTA_ARC_FORMAT_ZIP ? index - 1 : index;   /* ZIP asks for .z01 as volume 1 */
    if (i >= r->s->n) return FULTA_ARC_ERR_VOLUME_MISSING;
    *out = fulta_arc_source_memory(r->s->v[i].d, r->s->v[i].n);
    return out->ctx ? FULTA_ARC_OK : FULTA_ARC_ERR_NOMEM;
}

/* a memory pull reader */
typedef struct { const uint8_t *d; uint64_t n, pos; } rdm_t;
static fulta_arc_err_t rdm_read(void *ctx, void *buf, size_t cap, size_t *got) {
    rdm_t *r = ctx;
    uint64_t left = r->n - r->pos;
    size_t k = left < cap ? (size_t)left : cap;
    memcpy(buf, r->d + r->pos, k); r->pos += k; *got = k; return FULTA_ARC_OK;
}

static const char *g_pw;
static int g_attempts;
static fulta_arc_err_t pw_cb(void *ctx, uint32_t a, const char **o) {
    (void)ctx; g_attempts++;
    if (a || !g_pw) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *o = g_pw; return FULTA_ARC_OK;
}

static fulta_arc_err_t ms_reopen(mset_t *s, fulta_arc_format_t fmt, const char *pw, fulta_arc_t **out) {
    static rd_set_t r;
    r = (rd_set_t){s, fmt};
    uint32_t first = fmt == FULTA_ARC_FORMAT_ZIP ? s->n - 1 : 0;
    fulta_arc_source_t src = fulta_arc_source_memory(s->v[first].d, s->v[first].n);
    fulta_arc_options_t o = {.name = fmt == FULTA_ARC_FORMAT_ZIP ? "set.zip" : "set.7z.001", .volume = ms_volume,
                             .volume_ctx = &r, .password = pw_cb};
    g_pw = pw;
    return fulta_arc_open(&src, &o, out);
}

/* every file of `a` against the expected bytes */
static void check_bytes(fulta_arc_t *a, const char *const *names, const uint8_t *const *datas, const size_t *sizes,
                        size_t n, const char *label) {
    CHECK(fulta_arc_count(a) == n, "%s: %zu entries, got %zu", label, n, fulta_arc_count(a));
    for (size_t i = 0; i < n; i++) {
        size_t k = SIZE_MAX;
        for (size_t j = 0; j < fulta_arc_count(a); j++)
            if (strcmp(fulta_arc_entry(a, j)->name, names[i]) == 0) k = j;
        CHECK(k != SIZE_MAX, "%s: %s present", label, names[i]);
        if (k == SIZE_MAX) continue;
        void *d = NULL; size_t got = 0;
        fulta_arc_err_t e = fulta_arc_extract_alloc(a, k, &d, &got);
        CHECK(e == FULTA_ARC_OK && got == sizes[i] && (got == 0 || memcmp(d, datas[i], got) == 0), "%s: %s bytes (%s)",
              label, names[i], fulta_arc_strerror(e));
        fulta_arc_free(d);
    }
}

static int cancel_cb(void *ctx, uint64_t done, uint64_t total) {
    (void)total; return done >= *(uint64_t *)ctx;
}

static bool file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

int main(void) {
    size_t tn = 300000, rn = 150000;
    uint8_t *T = malloc(tn), *R = malloc(rn);
    if (!T || !R) return 1;
    uint32_t x = 4242;
    for (size_t i = 0; i < tn; i++) { x = x * 1103515245u + 12345u; T[i] = (uint8_t)("etaoin shrdlu\n"[(x >> 16) % 14]); }
    for (size_t i = 0; i < rn; i++) { x = x * 1103515245u + 12345u; R[i] = (uint8_t)(x >> 24); }
    static const uint8_t five[5] = {'h', 'e', 'l', 'l', 'o'};
    fulta_arc_write_entry_t es[] = {
        {.name = "d/text.txt", .data = T, .size = tn, .mtime = 1600000000},
        {.name = "d/rand.bin", .data = R, .size = rn, .mtime = 1600000001},
        {.name = "d/five.txt", .data = five, .size = sizeof five, .mtime = 1600000002},
        {.name = "d/empty.txt", .data = NULL, .size = 0},
    };
    const char *nm[] = {"d/text.txt", "d/rand.bin", "d/five.txt", "d/empty.txt"};
    const uint8_t *dt[] = {T, R, five, NULL};
    size_t sz[] = {tn, rn, sizeof five, 0};
    const fulta_arc_format_t fmts[] = {FULTA_ARC_FORMAT_ZIP, FULTA_ARC_FORMAT_7Z};
    const char *fn[] = {"zip", "7z"};

    /* ---- the volume sink: methods x formats x volume sizes ---- */
    const uint64_t vsizes[] = {65536, 65536 + 37, 100003, 1u << 22};
    for (int f = 0; f < 2; f++) {
        for (size_t vi = 0; vi < sizeof vsizes / sizeof vsizes[0]; vi++) {
            for (int store = 0; store < 2; store++) {
                char label[96];
                snprintf(label, sizeof label, "%s vol %llu %s", fn[f], (unsigned long long)vsizes[vi], store ? "store" : "default");
                mset_t s = {0};
                fulta_arc_volume_sink_t vs = ms_sink(&s);
                fulta_arc_write_options_t o = {.level = 6, .volume_size = vsizes[vi],
                                               .method = store ? FULTA_ARC_METHOD_STORE : FULTA_ARC_METHOD_DEFAULT};
                fulta_arc_err_t e = fulta_arc_write_volumes(fmts[f], es, 4, &o, &vs);
                CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
                CHECK(s.finished == 1 && s.ok && s.count == s.n, "%s: finish once, ok, count %u", label, s.count);
                bool sizes_ok = true, closed = true;
                for (uint32_t i = 0; i < s.n; i++) {
                    if (s.v[i].n > vsizes[vi] || s.v[i].n == 0) sizes_ok = false;
                    if (!s.v[i].closed) closed = false;
                }
                CHECK(sizes_ok, "%s: every volume non-empty and within the size", label);
                CHECK(closed, "%s: every volume closed", label);
                if (fmts[f] == FULTA_ARC_FORMAT_7Z)
                    for (uint32_t i = 0; i + 1 < s.n; i++)
                        CHECK(s.v[i].n == vsizes[vi], "%s: 7z volume %u is full", label, i);
                if (fmts[f] == FULTA_ARC_FORMAT_ZIP && s.n) {
                    static const uint8_t span[4] = {0x50, 0x4B, 0x07, 0x08}, pk00[4] = {0x50, 0x4B, 0x30, 0x30};
                    CHECK(memcmp(s.v[0].d, s.n > 1 ? span : pk00, 4) == 0, "%s: first-volume marker", label);
                }
                if (vsizes[vi] == (1u << 22)) CHECK(s.n == 1, "%s: fits one volume (%u)", label, s.n);
                else CHECK(s.n > 2, "%s: several volumes (%u)", label, s.n);
                fulta_arc_t *a = NULL;
                e = ms_reopen(&s, fmts[f], NULL, &a);
                CHECK(e == FULTA_ARC_OK && a, "%s: reopen (%s)", label, fulta_arc_strerror(e));
                if (a) { check_bytes(a, nm, dt, sz, 4, label); fulta_arc_close(a); }
                ms_free(&s);
            }
        }
    }

    /* ZIP: names long enough that a local or central header would straddle a boundary are moved whole */
    {
        enum { NE = 40 };
        fulta_arc_write_entry_t le[NE];
        char names[NE][300];
        for (int i = 0; i < NE; i++) {
            memset(names[i], 'a' + i % 26, 280);
            snprintf(names[i] + 280, 20, "/%02d.bin", i);
            le[i] = (fulta_arc_write_entry_t){.name = names[i], .data = R + i * 1000, .size = 3000 + (uint64_t)i * 97};
        }
        mset_t s = {0};
        fulta_arc_volume_sink_t vs = ms_sink(&s);
        fulta_arc_write_options_t o = {.level = 0, .volume_size = 65536};
        CHECK(fulta_arc_write_volumes(FULTA_ARC_FORMAT_ZIP, le, NE, &o, &vs) == FULTA_ARC_OK, "zip long names: write");
        /* each volume past the first starts with a local header, a central record, an end record, or data */
        fulta_arc_t *a = NULL;
        CHECK(ms_reopen(&s, FULTA_ARC_FORMAT_ZIP, NULL, &a) == FULTA_ARC_OK && a, "zip long names: reopen");
        if (a) {
            CHECK(fulta_arc_count(a) == NE, "zip long names: %d entries", NE);
            bool ok = true;
            for (size_t i = 0; i < fulta_arc_count(a) && i < NE; i++) {
                void *d = NULL; size_t got = 0;
                if (fulta_arc_extract_alloc(a, i, &d, &got) || got != le[i].size || memcmp(d, le[i].data, got)) ok = false;
                fulta_arc_free(d);
            }
            CHECK(ok, "zip long names: every entry's bytes");
            fulta_arc_close(a);
        }
        ms_free(&s);
    }

    /* the copy and convert routes through the volume sink, with encryption */
    {
        mset_t s = {0};
        fulta_arc_volume_sink_t vs = ms_sink(&s);
        fulta_arc_write_options_t o = {.level = 6, .volume_size = 65536};
        CHECK(fulta_arc_write_volumes(FULTA_ARC_FORMAT_ZIP, es, 4, &o, &vs) == FULTA_ARC_OK, "edit src: write");
        fulta_arc_t *src = NULL;
        CHECK(ms_reopen(&s, FULTA_ARC_FORMAT_ZIP, NULL, &src) == FULTA_ARC_OK && src, "edit src: reopen");
        if (src) {
            CHECK(fulta_arc_editable(src) == FULTA_ARC_EDIT_MULTIVOLUME, "edit src: a split ZIP converts");
            for (int f = 0; f < 2; f++) {
                char label[64];
                snprintf(label, sizeof label, "convert split->%s split aes", fn[f]);
                fulta_arc_edit_t *ed = NULL;
                CHECK(fulta_arc_edit_begin(src, &ed) == FULTA_ARC_OK && ed, "%s: begin", label);
                if (!ed) continue;
                mset_t t = {0};
                fulta_arc_volume_sink_t tv = ms_sink(&t);
                fulta_arc_write_options_t co = {.level = 6, .volume_size = 70000, .password = "cv-pw",
                                                .encrypt_names = fmts[f] == FULTA_ARC_FORMAT_7Z,
                                                .method = fmts[f] == FULTA_ARC_FORMAT_ZIP ? FULTA_ARC_METHOD_ZSTD
                                                                                         : FULTA_ARC_METHOD_DEFAULT};
                fulta_arc_err_t e = fulta_arc_edit_write_volumes(ed, fmts[f], &tv, &co);
                CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
                fulta_arc_edit_free(ed);
                fulta_arc_t *a = NULL;
                e = ms_reopen(&t, fmts[f], "cv-pw", &a);
                CHECK(e == FULTA_ARC_OK && a, "%s: reopen (%s)", label, fulta_arc_strerror(e));
                if (a) { check_bytes(a, nm, dt, sz, 4, label); fulta_arc_close(a); }
                ms_free(&t);
            }
            /* a single sink with volume_size is refused */
            fulta_arc_edit_t *ed = NULL;
            if (fulta_arc_edit_begin(src, &ed) == FULTA_ARC_OK && ed) {
                fulta_arc_write_sink_t one = {.ctx = &s.v[0], .write = mv_write, .patch = mv_patch};
                fulta_arc_write_options_t bo = {.level = 6, .volume_size = 65536};
                CHECK(fulta_arc_edit_write(ed, FULTA_ARC_FORMAT_ZIP, &one, &bo) == FULTA_ARC_ERR_INVALID_ARG,
                      "edit_write with volume_size on one sink -> INVALID_ARG");
                fulta_arc_edit_free(ed);
            }
            fulta_arc_close(src);
        }
        ms_free(&s);
    }

    /* the copy route into volumes: an editable ZIP and a SOLID_OFF 7z, as they are */
    for (int f = 0; f < 2; f++) {
        char label[64];
        snprintf(label, sizeof label, "copy route %s -> volumes", fn[f]);
        mvol_t one = {0};
        fulta_arc_write_sink_t sk = {.ctx = &one, .write = mv_write, .patch = mv_patch};
        fulta_arc_write_options_t wo = {.level = 6, .solid = FULTA_ARC_SOLID_OFF};
        CHECK(fulta_arc_write(fmts[f], es, 3, &wo, &sk) == FULTA_ARC_OK, "%s: source", label);
        fulta_arc_source_t src = fulta_arc_source_memory(one.d, one.n);
        fulta_arc_t *in = NULL;
        CHECK(fulta_arc_open(&src, NULL, &in) == FULTA_ARC_OK && in, "%s: open source", label);
        if (!in) { free(one.d); continue; }
        CHECK(fulta_arc_editable(in) == FULTA_ARC_EDIT_OK, "%s: editable in place", label);
        fulta_arc_edit_t *ed = NULL;
        CHECK(fulta_arc_edit_begin(in, &ed) == FULTA_ARC_OK && ed, "%s: begin", label);
        if (ed) {
            mset_t t = {0};
            fulta_arc_volume_sink_t tv = ms_sink(&t);
            fulta_arc_write_options_t co = {.level = 6, .volume_size = 65536};
            fulta_arc_err_t e = fulta_arc_edit_write_volumes(ed, fmts[f], &tv, &co);
            CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
            CHECK(t.n > 2, "%s: several volumes (%u)", label, t.n);
            fulta_arc_edit_free(ed);
            fulta_arc_t *a = NULL;
            CHECK(ms_reopen(&t, fmts[f], NULL, &a) == FULTA_ARC_OK && a, "%s: reopen", label);
            if (a) { check_bytes(a, nm, dt, sz, 3, label); fulta_arc_close(a); }
            ms_free(&t);
        }
        fulta_arc_close(in);
        free(one.d);
    }

    /* argument checks; a failing volume and a cancel are finished as not ok */
    {
        mset_t s = {0};
        fulta_arc_volume_sink_t vs = ms_sink(&s);
        fulta_arc_write_options_t small = {.level = 6, .volume_size = FULTA_ARC_VOLUME_MIN - 1};
        CHECK(fulta_arc_write_volumes(FULTA_ARC_FORMAT_7Z, es, 4, &small, &vs) == FULTA_ARC_ERR_INVALID_ARG,
              "volume below the minimum -> INVALID_ARG");
        fulta_arc_write_options_t none = {.level = 6};
        CHECK(fulta_arc_write_volumes(FULTA_ARC_FORMAT_7Z, es, 4, &none, &vs) == FULTA_ARC_ERR_INVALID_ARG,
              "volumes without volume_size -> INVALID_ARG");
        fulta_arc_write_sink_t one = {.ctx = &s.v[0], .write = mv_write, .patch = mv_patch};
        fulta_arc_write_options_t vo = {.level = 6, .volume_size = 65536};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, es, 4, &vo, &one) == FULTA_ARC_ERR_INVALID_ARG,
              "fulta_arc_write with volume_size -> INVALID_ARG");
        free(s.v[0].d); s = (mset_t){0};

        s.fail_at = 3;
        fulta_arc_err_t e = fulta_arc_write_volumes(FULTA_ARC_FORMAT_ZIP, es, 4, &vo, &vs);
        CHECK(e == FULTA_ARC_ERR_IO, "a volume that cannot open -> IO (%s)", fulta_arc_strerror(e));
        CHECK(s.finished == 1 && !s.ok, "a failed set is finished as not ok");
        ms_free(&s); s = (mset_t){0};

        uint64_t stop = 200000;
        fulta_arc_write_options_t co = {.level = 6, .volume_size = 65536, .progress = cancel_cb, .progress_ctx = &stop};
        e = fulta_arc_write_volumes(FULTA_ARC_FORMAT_7Z, es, 4, &co, &vs);
        CHECK(e == FULTA_ARC_ERR_CANCELLED, "cancel -> CANCELLED (%s)", fulta_arc_strerror(e));
        CHECK(s.finished == 1 && !s.ok, "a cancelled set is finished as not ok");
        ms_free(&s);
    }

    /* volume names */
    {
        char b[64];
        CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_7Z, "a/b.7z", 0, 3, b, sizeof b) == FULTA_ARC_OK && !strcmp(b, "a/b.7z.001"), "name 7z first");
        CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_7Z, "b.7z", 999, 1000, b, sizeof b) == FULTA_ARC_OK && !strcmp(b, "b.7z.1000"), "name 7z 1000");
        CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_ZIP, "a.b/c.zip", 0, 3, b, sizeof b) == FULTA_ARC_OK && !strcmp(b, "a.b/c.z01"), "name zip first");
        CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_ZIP, "c.zip", 2, 3, b, sizeof b) == FULTA_ARC_OK && !strcmp(b, "c.zip"), "name zip last");
        CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_ZIP, "a.b/c", 1, 3, b, sizeof b) == FULTA_ARC_OK && !strcmp(b, "a.b/c.z02"), "name zip no ext");
        CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_ZIP, "c.zip", 3, 3, b, sizeof b) == FULTA_ARC_ERR_INVALID_ARG, "name index past count");
        CHECK(fulta_arc_volume_name(FULTA_ARC_FORMAT_7Z, "long-name.7z", 0, 1, b, 8) == FULTA_ARC_ERR_LIMIT, "name buffer too small");
    }

    /* ---- files: temporaries renamed on success; EXISTS; cancel leaves nothing ---- */
    for (int f = 0; f < 2; f++) {
        char path[64], vname[96], label[64];
        snprintf(path, sizeof path, "test_split_out.%s", fn[f]);
        snprintf(label, sizeof label, "file %s", fn[f]);
        fulta_arc_write_options_t o = {.level = 6, .volume_size = 65536};
        fulta_arc_err_t e = fulta_arc_write_file(path, fmts[f], es, 4, &o);
        CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
        uint32_t n = 0;
        for (uint32_t i = 0; i < 64; i++) {           /* count the volumes by their names */
            if (fulta_arc_volume_name(fmts[f], path, i, fmts[f] == FULTA_ARC_FORMAT_ZIP ? 1000 : i + 1, vname, sizeof vname)) break;
            if (!file_exists(vname)) break;
            n++;
        }
        CHECK(n > 2, "%s: %u volumes on disk", label, n);
        if (fmts[f] == FULTA_ARC_FORMAT_ZIP) CHECK(file_exists(path), "%s: the .zip is there", label);
        snprintf(vname, sizeof vname, "%s.fatmp0", path);
        CHECK(!file_exists(vname), "%s: no temporary left", label);
        const char *first = path;
        char first7[96];
        if (fmts[f] == FULTA_ARC_FORMAT_7Z) { snprintf(first7, sizeof first7, "%s.001", path); first = first7; }
        fulta_arc_t *a = NULL;
        CHECK(fulta_arc_open_file(first, NULL, &a) == FULTA_ARC_OK && a, "%s: reopen", label);
        if (a) { check_bytes(a, nm, dt, sz, 4, label); fulta_arc_close(a); }

        /* an older, longer set: writing a shorter one under the same name is refused, nothing deleted */
        fulta_arc_write_options_t big = {.level = 6, .volume_size = 1u << 22};
        e = fulta_arc_write_file(path, fmts[f], es, 4, &big);
        CHECK(e == FULTA_ARC_ERR_EXISTS, "%s: a longer set there -> EXISTS (%s)", label, fulta_arc_strerror(e));
        if (fmts[f] == FULTA_ARC_FORMAT_ZIP) snprintf(vname, sizeof vname, "test_split_out.z01");
        else snprintf(vname, sizeof vname, "%s.002", path);
        CHECK(file_exists(vname), "%s: the old set's second volume is still there", label);
        snprintf(vname, sizeof vname, "%s.fatmp0", path);
        CHECK(!file_exists(vname), "%s: no temporary left after EXISTS", label);

        /* remove the set */
        for (uint32_t i = 0; i < n; i++)              /* ZIP: the n .zNN (count 1000 names them all .zNN) */
            if (!fulta_arc_volume_name(fmts[f], path, i, fmts[f] == FULTA_ARC_FORMAT_ZIP ? 1000 : i + 1, vname, sizeof vname))
                remove(vname);
        if (fmts[f] == FULTA_ARC_FORMAT_ZIP) remove(path);

        /* cancel half-way: no volume and no temporary */
        uint64_t stop = 250000;
        fulta_arc_write_options_t co = {.level = 6, .volume_size = 65536, .progress = cancel_cb, .progress_ctx = &stop};
        e = fulta_arc_write_file(path, fmts[f], es, 4, &co);
        CHECK(e == FULTA_ARC_ERR_CANCELLED, "%s: cancel (%s)", label, fulta_arc_strerror(e));
        bool any = false;
        for (uint32_t i = 0; i < 16; i++) {
            snprintf(vname, sizeof vname, "%s.fatmp%u", path, i);
            if (file_exists(vname)) any = true;
            if (!fulta_arc_volume_name(fmts[f], path, i, 100, vname, sizeof vname) && file_exists(vname)) any = true;
        }
        if (file_exists(path)) any = true;
        CHECK(!any, "%s: nothing left after a cancel", label);
    }

    /* Rubraview's way of packing: an empty archive, readers added, edit_write_file with volume_size */
    for (int f = 0; f < 2; f++) {
        for (int enc = 0; enc < 2; enc++) {
            char label[64], path[64], first[80];
            snprintf(label, sizeof label, "pack %s split%s", fn[f], enc ? " encrypted" : "");
            snprintf(path, sizeof path, "test_split_pack.%s", fn[f]);
            mvol_t one = {0};
            fulta_arc_write_sink_t sk = {.ctx = &one, .write = mv_write, .patch = mv_patch};
            fulta_arc_write_options_t wo = {.level = 6};
            CHECK(fulta_arc_write(fmts[f], NULL, 0, &wo, &sk) == FULTA_ARC_OK, "%s: empty archive", label);
            fulta_arc_source_t src = fulta_arc_source_memory(one.d, one.n);
            fulta_arc_t *in = NULL;
            CHECK(fulta_arc_open(&src, NULL, &in) == FULTA_ARC_OK && in, "%s: open empty", label);
            if (!in) { free(one.d); continue; }
            fulta_arc_edit_t *ed = NULL;
            CHECK(fulta_arc_edit_begin(in, &ed) == FULTA_ARC_OK && ed, "%s: begin", label);
            if (ed) {
                rdm_t r1 = {T, tn, 0}, r2 = {R, rn, 0}, r3 = {five, sizeof five, 0};
                fulta_arc_reader_t a1 = {.ctx = &r1, .read = rdm_read, .size = tn, .mtime = 1600000000};
                fulta_arc_reader_t a2 = {.ctx = &r2, .read = rdm_read, .size = UINT64_MAX, .mtime = 1600000001};
                fulta_arc_reader_t a3 = {.ctx = &r3, .read = rdm_read, .size = sizeof five};
                CHECK(fulta_arc_edit_add(ed, "d/text.txt", &a1) == FULTA_ARC_OK, "%s: add sized", label);
                CHECK(fulta_arc_edit_add(ed, "d/rand.bin", &a2) == FULTA_ARC_OK, "%s: add unsized", label);
                CHECK(fulta_arc_edit_add(ed, "d/five.txt", &a3) == FULTA_ARC_OK, "%s: add small", label);
                fulta_arc_write_options_t po = {.level = 6, .volume_size = 65536,
                                                .password = enc ? "pack-pw" : NULL,
                                                .encrypt_names = enc && fmts[f] == FULTA_ARC_FORMAT_7Z,
                                                .method = fmts[f] == FULTA_ARC_FORMAT_ZIP ? FULTA_ARC_METHOD_BZIP2
                                                                                         : FULTA_ARC_METHOD_LZMA2};
                fulta_arc_err_t e = fulta_arc_edit_write_file(ed, fmts[f], path, &po);
                CHECK(e == FULTA_ARC_OK, "%s: write (%s)", label, fulta_arc_strerror(e));
                fulta_arc_edit_free(ed);
                if (fmts[f] == FULTA_ARC_FORMAT_7Z) snprintf(first, sizeof first, "%s.001", path);
                else snprintf(first, sizeof first, "%s", path);
                fulta_arc_options_t ro = {.password = pw_cb};
                g_pw = enc ? "pack-pw" : NULL;
                fulta_arc_t *a = NULL;
                e = fulta_arc_open_file(first, &ro, &a);
                CHECK(e == FULTA_ARC_OK && a, "%s: reopen (%s)", label, fulta_arc_strerror(e));
                if (a) {
                    CHECK(fulta_arc_editable(a) == (enc ? FULTA_ARC_EDIT_ENCRYPTED : FULTA_ARC_EDIT_MULTIVOLUME),
                          "%s: split%s", label, enc ? " and encrypted" : "");
                    check_bytes(a, nm, dt, sz, 3, label);
                    fulta_arc_close(a);
                }
                if (enc && fmts[f] == FULTA_ARC_FORMAT_7Z) {
                    g_pw = NULL;
                    a = NULL;
                    e = fulta_arc_open_file(first, &ro, &a);
                    CHECK(e == FULTA_ARC_ERR_PASSWORD_NEEDED, "%s: names need the password (%s)", label, fulta_arc_strerror(e));
                    if (a) fulta_arc_close(a);
                }
                char vname[96];
                for (uint32_t i = 0; i < 32; i++)
                    if (!fulta_arc_volume_name(fmts[f], path, i, 1000, vname, sizeof vname)) remove(vname);
                remove(path);
            }
            fulta_arc_close(in);
            free(one.d);
        }
    }

    /* ---- 7z header encryption (encrypt_names) ---- */
    {
        mset_t s = {0};
        fulta_arc_volume_sink_t vs = ms_sink(&s);
        fulta_arc_write_options_t o = {.level = 6, .volume_size = 1u << 22, .password = "hdr-pw", .encrypt_names = 1};
        CHECK(fulta_arc_write_volumes(FULTA_ARC_FORMAT_7Z, es, 4, &o, &vs) == FULTA_ARC_OK, "mhe: write");
        bool leak = false;                            /* no name in the clear, in UTF-16LE as 7z stores it */
        static const uint8_t u16[] = {'t', 0, 'e', 0, 'x', 0, 't', 0, '.', 0, 't', 0};
        for (size_t k = 0; s.n && k + sizeof u16 <= s.v[0].n; k++) if (!memcmp(s.v[0].d + k, u16, sizeof u16)) leak = true;
        CHECK(!leak, "mhe: no name in the clear");
        fulta_arc_t *a = NULL;
        fulta_arc_err_t e = ms_reopen(&s, FULTA_ARC_FORMAT_7Z, NULL, &a);
        CHECK(e == FULTA_ARC_ERR_PASSWORD_NEEDED, "mhe: no password -> PASSWORD_NEEDED (%s)", fulta_arc_strerror(e));
        if (a) fulta_arc_close(a), a = NULL;
        g_attempts = 0;
        e = ms_reopen(&s, FULTA_ARC_FORMAT_7Z, "wrong", &a);
        /* the wrong one is tried, then the callback is asked again (attempt 1) and gives up */
        CHECK(e == FULTA_ARC_ERR_PASSWORD_NEEDED && g_attempts == 2, "mhe: wrong password refused, asked again (%s, %d)",
              fulta_arc_strerror(e), g_attempts);
        if (a) fulta_arc_close(a), a = NULL;
        e = ms_reopen(&s, FULTA_ARC_FORMAT_7Z, "hdr-pw", &a);
        CHECK(e == FULTA_ARC_OK && a, "mhe: right password opens (%s)", fulta_arc_strerror(e));
        if (a) { check_bytes(a, nm, dt, sz, 4, "mhe"); fulta_arc_close(a); }
        ms_free(&s);

        /* the single-file writer and SOLID_OFF; ZIP refuses; no password ignores it */
        mvol_t one = {0};
        fulta_arc_write_sink_t sk = {.ctx = &one, .write = mv_write, .patch = mv_patch};
        fulta_arc_write_options_t so = {.level = 6, .password = "p2", .encrypt_names = 1, .solid = FULTA_ARC_SOLID_OFF,
                                        .method = FULTA_ARC_METHOD_LZMA2};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, es, 4, &so, &sk) == FULTA_ARC_OK, "mhe solid-off: write");
        fulta_arc_source_t src = fulta_arc_source_memory(one.d, one.n);
        fulta_arc_options_t ro = {.password = pw_cb};
        g_pw = "p2";
        CHECK(fulta_arc_open(&src, &ro, &a) == FULTA_ARC_OK && a, "mhe solid-off: reopen");
        if (a) { check_bytes(a, nm, dt, sz, 4, "mhe solid-off"); fulta_arc_close(a); a = NULL; }
        free(one.d); one = (mvol_t){0};
        fulta_arc_write_options_t zo = {.level = 6, .password = "p", .encrypt_names = 1};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_ZIP, es, 4, &zo, &sk) == FULTA_ARC_ERR_INVALID_ARG, "mhe: ZIP -> INVALID_ARG");
        free(one.d); one = (mvol_t){0};
        fulta_arc_write_options_t np = {.level = 6, .encrypt_names = 1};
        CHECK(fulta_arc_write(FULTA_ARC_FORMAT_7Z, es, 4, &np, &sk) == FULTA_ARC_OK, "mhe without a password: plain");
        src = fulta_arc_source_memory(one.d, one.n);
        g_pw = NULL;
        CHECK(fulta_arc_open(&src, &ro, &a) == FULTA_ARC_OK && a, "mhe without a password: opens without one");
        if (a) fulta_arc_close(a);
        free(one.d);
    }

    free(T); free(R);
    return t_report("test_split");
}
