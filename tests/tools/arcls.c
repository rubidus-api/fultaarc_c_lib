/* arcls.c - list every entry of each archive through FultaArc's public API, one tab-separated line per entry:
 *
 *     archive  kind  name  size  crc32  flags  status
 *
 * kind is F (file, symlink) or D (directory); size and crc32 are of the bytes fulta_arc_extract delivered; status is
 * "ok" or fulta_arc_strerror(). In the name, a byte below 0x20, DEL and '\' are written as \xNN (a stored name may
 * hold a newline or a terminal escape). An archive that does not open gives one line with kind "-" and its error. Used by
 * lhamanifest.py to pair FultaArc's entry names with the oracle's extracted files; the manifest's size and crc32 come
 * from the oracle, never from this tool. Build: cc -std=c23 -Iinclude tests/tools/arcls.c build/host/libfultaarc.a
 * (plus the sanitizer flags the library was built with). MIT. */
#include "fulta/arc.h"

#include <stdint.h>
#include <stdio.h>

typedef struct crc_sink { uint32_t crc; uint64_t n; } crc_sink_t;
static uint32_t g_table[256];

static fulta_arc_err_t crc_write(void *ctx, const void *data, size_t n) {
    crc_sink_t *c = ctx;
    const unsigned char *p = data;
    uint32_t v = ~c->crc;
    for (size_t i = 0; i < n; i++) v = g_table[(v ^ p[i]) & 0xFF] ^ (v >> 8);
    c->crc = ~v;
    c->n += n;
    return FULTA_ARC_OK;
}

int main(int argc, char **argv) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        g_table[i] = c;
    }
    for (int a = 1; a < argc; a++) {
        fulta_arc_t *arc = NULL;
        fulta_arc_err_t e = fulta_arc_open_file(argv[a], NULL, &arc);
        if (e) {
            printf("%s\t-\t\t0\t00000000\t0\t%s\n", argv[a], fulta_arc_strerror(e));
            continue;
        }
        for (size_t i = 0; i < fulta_arc_count(arc); i++) {
            const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
            bool dir = (en->flags & FULTA_ARC_ENTRY_DIR) != 0;
            crc_sink_t cs = { 0, 0 };
            fulta_arc_sink_t sink = { .ctx = &cs, .write = crc_write };
            e = dir ? FULTA_ARC_OK : fulta_arc_extract(arc, i, &sink);
            printf("%s\t%c\t", argv[a], dir ? 'D' : 'F');
            for (const unsigned char *p = (const unsigned char *)en->name; *p; p++) {
                if (*p < 0x20 || *p == 0x7F || *p == '\\') printf("\\x%02x", *p);
                else putchar(*p);
            }
            printf("\t%llu\t%08x\t%x\t%s\n", (unsigned long long)cs.n, cs.crc, en->flags,
                   e ? fulta_arc_strerror(e) : "ok");
        }
        fulta_arc_close(arc);
    }
    return 0;
}
