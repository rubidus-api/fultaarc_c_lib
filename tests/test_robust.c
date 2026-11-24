/* test_robust.c - malformed input: every reader must return an error, never crash, hang or leak (run under the
 * sanitizers by make test).
 *
 * Inputs are the private fixtures (a missing directory skips): libarchive's test archives as they are (several are
 * built to crash readers), and small archives of every set, each also mutated in a fixed, seeded way (cut short,
 * bytes flipped, a run zeroed). Each input is opened from memory and every entry extracted into a sink that stops
 * after 64 MiB; FULTA_ARC_ROBUST=<n> makes n times as many mutants. Mutants are given no password. The check is only that each call returns. */
#include "fulta/arc.h"
#include "testlib.h"

#include <dirent.h>
#include <stdint.h>

#define MAX_INPUT (256u * 1024u)
#define SINK_CAP (UINT64_C(64) << 20)
static int g_mutants = 12;   /* FULTA_ARC_ROBUST=<n>: n times as many, from another seed (a longer run) */

static uint64_t g_rng = 0x9E3779B97F4A7C15u;
static uint32_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}

static fulta_arc_err_t pw_cb(void *ctx, uint32_t attempt, const char **out) {
    (void)ctx;
    static const char *const pw[] = {"password", "12345678", "pass1234", "pass"};
    if (attempt >= sizeof pw / sizeof *pw) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *out = pw[attempt];
    return FULTA_ARC_OK;
}

static fulta_arc_err_t cap_write(void *ctx, const void *data, size_t n) {
    (void)data;
    uint64_t *total = ctx;
    *total += n;
    return *total > SINK_CAP ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_OK;
}

static long g_inputs, g_opened, g_entries;

/* Passwords only for inputs as they are: key derivation is slow on purpose, and a mutant gains nothing from it. */
static void run_one(const uint8_t *data, size_t size, bool passwords) {
    fulta_arc_options_t opt = {.password = passwords ? pw_cb : NULL, .max_dictionary = UINT64_C(64) << 20};
    fulta_arc_t *arc = NULL;
    g_inputs++;
    if (fulta_arc_open_memory(data, size, &opt, &arc) != FULTA_ARC_OK) return;
    g_opened++;
    size_t n = fulta_arc_count(arc);
    for (size_t i = 0; i < n && i < 4096; i++) {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
        CHECK(en && en->name, "entry %zu has no name", i);
        if (!en || (en->flags & FULTA_ARC_ENTRY_DIR)) continue;
        uint64_t total = 0;
        fulta_arc_sink_t sink = {.ctx = &total, .write = cap_write};
        (void)fulta_arc_extract(arc, i, &sink);
        g_entries++;
    }
    fulta_arc_close(arc);
}

static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = malloc(MAX_INPUT + 1);
    size_t n = b ? fread(b, 1, MAX_INPUT + 1, f) : 0;
    fclose(f);
    if (!b || n == 0 || n > MAX_INPUT) { free(b); return NULL; }
    *size = n;
    return b;
}

static void mutate_and_run(const uint8_t *orig, size_t size) {
    uint8_t *m = malloc(size);
    if (!m) return;
    for (int k = 0; k < g_mutants; k++) {
        memcpy(m, orig, size);
        size_t len = size;
        switch (k % 3) {
        case 0: len = size > 1 ? 1 + rnd() % (size - 1) : size; break;                    /* cut short */
        case 1: for (int j = 0, c = 1 + (int)(rnd() % 8); j < c; j++) m[rnd() % size] ^= (uint8_t)(1u << (rnd() % 8)); break;
        default: {                                                                           /* a run zeroed */
            size_t at = rnd() % size, run = 1 + rnd() % 16;
            memset(m + at, 0, at + run <= size ? run : size - at);
        }
        }
        run_one(m, len, false);
    }
    free(m);
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* Every regular file in `dir` up to MAX_INPUT bytes, in name order; `stride` keeps every stride-th one. */
static void run_dir(const char *sub, bool as_is, size_t stride) {
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/%s", t_fixtures(), sub);
    DIR *d = opendir(dir);
    if (!d) { SKIP("fixtures %s not present", dir); return; }
    char **names = NULL;
    size_t n = 0, cap = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        if (n == cap) { cap = cap ? cap * 2 : 256; names = realloc(names, cap * sizeof *names); }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    long before = g_inputs;
    for (size_t i = 0; i < n; i++) {
        if (i % stride == 0) {
            char path[8192];
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            size_t size;
            uint8_t *b = load(path, &size);
            if (b) {
                if (as_is) run_one(b, size, true);
                mutate_and_run(b, size);
                CHECK(true, "%s", names[i]);
                free(b);
            }
        }
        free(names[i]);
    }
    free(names);
    printf("%s: %ld inputs\n", sub, g_inputs - before);
}


/* As run_dir, over every regular file under `sub` and its subdirectories (the LHA corpus is a tree of one directory
 * per archiver), in path order. */
static void collect_tree(const char *root, const char *rel, char ***names, size_t *n, size_t *cap) {
    char dir[8192];
    snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
    DIR *d = opendir(dir);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char sub[4096];
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        char full[8192];
        snprintf(full, sizeof full, "%s/%s", root, sub);
        DIR *t = opendir(full);
        if (t) { closedir(t); collect_tree(root, sub, names, n, cap); continue; }
        if (*n == *cap) { *cap = *cap ? *cap * 2 : 256; *names = realloc(*names, *cap * sizeof **names); }
        (*names)[(*n)++] = strdup(sub);
    }
    closedir(d);
}

static void run_tree(const char *sub, bool as_is) {
    char root[4096];
    snprintf(root, sizeof root, "%s/%s", t_fixtures(), sub);
    DIR *d = opendir(root);
    if (!d) { SKIP("fixtures %s not present", root); return; }
    closedir(d);
    char **names = NULL;
    size_t n = 0, cap = 0;
    collect_tree(root, "", &names, &n, &cap);
    qsort(names, n, sizeof *names, cmp_str);
    long before = g_inputs;
    for (size_t i = 0; i < n; i++) {
        char path[8192];
        snprintf(path, sizeof path, "%s/%s", root, names[i]);
        size_t size;
        uint8_t *b = load(path, &size);
        if (b) {
            if (as_is) run_one(b, size, true);
            mutate_and_run(b, size);
            CHECK(true, "%s", names[i]);
            free(b);
        }
        free(names[i]);
    }
    free(names);
    printf("%s: %ld inputs\n", sub, g_inputs - before);
}

int main(void) {
    const char *deep = getenv("FULTA_ARC_ROBUST");
    if (deep && atoi(deep) > 1) { g_mutants *= atoi(deep); g_rng += (uint64_t)atoi(deep); }
    run_dir("libarchive", true, 1);
    run_dir("zip7z/archives", false, 1);
    run_dir("rar", false, 1);
    run_dir("legacy", false, 1);
    run_dir("pkzip", false, 1);
    run_dir("pkzip-made", false, 1);
    run_dir("alz", true, 1);
    run_dir("egg", true, 1);
    run_dir("alzegg", true, 1);
    run_dir("tar", true, 1);
    run_dir("compress", true, 1);
    run_dir("lzip", true, 1);
    run_dir("ar", true, 1);
    run_dir("cpio", true, 1);
    run_dir("iso", true, 1);
    run_dir("cab", true, 1);
    run_dir("rar15", true, 1);   /* the rar15 clean room's archives (RAR 1.4 / 1.5) */
    run_tree("lha", true);   /* the lhasa corpus incl. its regression/ set (truncated, hostile names, evil_pm2) */
    printf("robust: %ld inputs, %ld opened, %ld extractions returned\n", g_inputs, g_opened, g_entries);
    return t_report("test_robust");
}
