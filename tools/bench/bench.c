/* bench.c - FultaArc: time the extraction of every file of an archive into a sink that drops the bytes. MIT.
 *
 *   bench [-p PASSWORD] [-r REPEAT] [-o DIR] ARCHIVE...
 *
 * Prints, per archive: files, bytes out, the best wall time of REPEAT runs (default 3) and MiB/s of output. The
 * archive is opened anew for each run, so opening and listing are inside the time. Clock: proven_time_now().
 * With -o every file is written into DIR (flat: the last part of its name; DIR must exist), for a comparison with
 * tools that extract to disk; without it the bytes are dropped. */
#include "fulta/arc.h"

#include "proven/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_pw;
static fulta_arc_err_t pw(void *c, uint32_t a, const char **o) {
    (void)c;
    if (a || !g_pw) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *o = g_pw;
    return FULTA_ARC_OK;
}
static fulta_arc_err_t drop(void *c, const void *d, size_t n) {
    (void)d;
    *(uint64_t *)c += n;
    return FULTA_ARC_OK;
}
typedef struct { uint64_t *bytes; FILE *f; } file_sink_t;
static fulta_arc_err_t to_file(void *c, const void *d, size_t n) {
    file_sink_t *s = c;
    *s->bytes += n;
    return fwrite(d, 1, n, s->f) == n ? FULTA_ARC_OK : FULTA_ARC_ERR_IO;
}
static const char *g_out;

int main(int argc, char **argv) {
    int repeat = 3, k = 1, bad = 0;
    for (; k + 1 < argc && argv[k][0] == '-'; k += 2) {
        if (!strcmp(argv[k], "-p")) g_pw = argv[k + 1];
        else if (!strcmp(argv[k], "-r")) repeat = atoi(argv[k + 1]);
        else if (!strcmp(argv[k], "-o")) g_out = argv[k + 1];
    }
    for (; k < argc; k++) {
        double best = 1e30;
        uint64_t bytes = 0;
        size_t files = 0;
        fulta_arc_err_t err = FULTA_ARC_OK;
        for (int r = 0; r < repeat && !err; r++) {
            proven_time_t t0 = proven_time_now();
            fulta_arc_options_t opt = {.password = pw};
            fulta_arc_t *a = NULL;
            err = fulta_arc_open_file(argv[k], &opt, &a);
            if (err) break;
            bytes = 0;
            files = 0;
            for (size_t i = 0; i < fulta_arc_count(a) && !err; i++) {
                if (fulta_arc_entry(a, i)->flags & FULTA_ARC_ENTRY_DIR) continue;
                if (g_out) {
                    const char *nm = fulta_arc_entry(a, i)->name, *sl = strrchr(nm, '/');
                    char path[4096];
                    snprintf(path, sizeof path, "%s/%s", g_out, sl ? sl + 1 : nm);
                    file_sink_t fs = {&bytes, fopen(path, "wb")};
                    if (!fs.f) { err = FULTA_ARC_ERR_IO; break; }
                    fulta_arc_sink_t s = {&fs, to_file};
                    err = fulta_arc_extract(a, i, &s);
                    if (fclose(fs.f) && !err) err = FULTA_ARC_ERR_IO;
                } else {
                    fulta_arc_sink_t s = {&bytes, drop};
                    err = fulta_arc_extract(a, i, &s);
                }
                files++;
            }
            fulta_arc_close(a);
            double sec = (double)(proven_time_now() - t0) / 1e9;
            if (sec < best) best = sec;
        }
        const char *name = strrchr(argv[k], '/');
        name = name ? name + 1 : argv[k];
        if (err) { printf("%-22s ERROR %s\n", name, fulta_arc_strerror(err)); bad = 1; continue; }
        printf("%-22s %3zu files %11llu bytes %8.3f s %8.1f MiB/s\n", name, files, (unsigned long long)bytes, best,
               (double)bytes / 1048576.0 / best);
    }
    return bad;
}
