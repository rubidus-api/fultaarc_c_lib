/* fuzz_open.c - libFuzzer harness: every reader on untrusted bytes, then the writer on what they produced.
 *
 * The first input byte picks the knobs (password, code page, keep_damaged, a convert to ZIP or 7z), the rest is the
 * archive. Every entry is extracted into a sink that stops at 4 MiB; names are walked to their NUL. Memory is capped
 * with max_memory/max_dictionary so a declared-huge dictionary is a LIMIT, not an out-of-memory abort, and each input
 * is cancelled after 2 s of CPU: a small archive that honestly declares gigabytes (a decompression bomb) is the
 * caller's to stop, through cancel or the sink, and is not a finding.
 *
 *   make fuzz            build/fuzz/fuzz_open (clang, ASan + UBSan + libFuzzer)
 *   build/fuzz/fuzz_open -max_len=131072 -timeout=15 -rss_limit_mb=2500 CORPUS_DIR
 * A corpus: the fixtures, each prefixed by a knob byte (0x00, 0x21, 0xA1 cover reading, convert to ZIP, to 7z).
 */
#include "fulta/arc.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#define SINK_CAP (UINT64_C(4) << 20)

static clock_t g_start;
static int over_time(void *ctx) { (void)ctx; return clock() - g_start > 2 * CLOCKS_PER_SEC; }

static fulta_arc_err_t cap_write(void *ctx, const void *data, size_t n) {
    uint64_t *total = ctx;
    if (n && !data) __builtin_trap();
    *total += n;
    return *total > SINK_CAP ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_OK;
}

static fulta_arc_err_t pw_cb(void *ctx, uint32_t attempt, const char **out) {
    (void)ctx;
    if (attempt >= 1) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *out = "password";
    return FULTA_ARC_OK;
}

static int wprogress(void *ctx, uint64_t done, uint64_t total) { (void)ctx; (void)done; (void)total; return over_time(NULL); }

static fulta_arc_err_t w_cap(void *ctx, const void *d, size_t n) {
    (void)d;
    uint64_t *t = ctx;
    *t += n;
    return *t > (UINT64_C(16) << 20) ? FULTA_ARC_ERR_LIMIT : FULTA_ARC_OK;
}
static fulta_arc_err_t w_patch(void *ctx, uint64_t off, const void *d, size_t n) {
    (void)d;
    uint64_t *t = ctx;
    return off + n > *t ? FULTA_ARC_ERR_IO : FULTA_ARC_OK;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    g_start = clock();
    if (size < 1) return 0;
    uint8_t k = data[0];
    data++; size--;
    fulta_arc_options_t o = {
        .max_memory = UINT64_C(256) << 20, .max_dictionary = UINT64_C(64) << 20,
        .password = (k & 1) ? pw_cb : NULL,
        .codepage = (fulta_arc_codepage_t)((k >> 1) & 3),          /* AUTO, UTF8, 437, 949 */
        .keep_damaged = (k & 8) != 0,
        .name = "fuzz.bin",
        .cancel = over_time,
    };
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_memory(data, size, &o, &arc) != FULTA_ARC_OK) { fulta_arc_close(arc); return 0; }
    size_t n = fulta_arc_count(arc);
    for (size_t i = 0; i < n && i < 4096; i++) {
        const fulta_arc_entry_t *e = fulta_arc_entry(arc, i);
        if (!e || !e->name || !e->method) __builtin_trap();
        volatile size_t l = strlen(e->name) + strlen(e->method);
        for (size_t j = 0; j < e->name_raw_size; j++) l += e->name_raw[j];
        (void)l;
        if (e->name[0] == '/') __builtin_trap();                   /* never absolute */
        uint64_t total = 0;
        fulta_arc_sink_t s = {.ctx = &total, .write = cap_write};
        (void)fulta_arc_extract(arc, i, &s);
        if (k & 16) (void)fulta_arc_check_password(arc, i);
    }
    (void)fulta_arc_entry(arc, n);                                 /* out of range: NULL */
    if (k & 32) {                                                  /* the writer on untrusted names and data */
        fulta_arc_edit_t *ed = NULL;
        if (fulta_arc_edit_begin(arc, &ed) == FULTA_ARC_OK) {
            if (n && (k & 64)) (void)fulta_arc_edit_remove(ed, 0);
            uint64_t t = 0;
            fulta_arc_write_sink_t ws = {.ctx = &t, .write = w_cap, .patch = w_patch};
            fulta_arc_write_options_t wo = {.level = 1, .progress = wprogress};
            (void)fulta_arc_edit_write(ed, (k & 128) ? FULTA_ARC_FORMAT_7Z : FULTA_ARC_FORMAT_ZIP, &ws, &wo);
            fulta_arc_edit_free(ed);
        }
    }
    fulta_arc_close(arc);
    return 0;
}
