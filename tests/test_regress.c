/* test_regress.c - every input the fuzzer ever found, replayed through the fuzz harness under the sanitizers of
 * make test (D-041, T029). The harness is tests/fuzz/fuzz_open.c itself, built here without libFuzzer: each file in
 * FIXTURES/regression/fuzz is one input (first byte: the harness's knobs). A crash, leak or UB report fails the run;
 * the harness's own 2 s cancel bounds a decompression bomb. Add a finding by copying its file there. */
#include "fuzz/fuzz_open.c"
#include "testlib.h"

#include <dirent.h>

int main(void) {
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/regression/fuzz", t_fixtures());
    DIR *d = opendir(dir);
    if (!d) { SKIP("%s not present", dir); return t_report("test_regress"); }
    struct dirent *de;
    size_t n = 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || strstr(de->d_name, ".md")) continue;
        char path[2048];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        size_t len = 0;
        unsigned char *b = t_read_file(path, &len);
        if (!b) continue;
        LLVMFuzzerTestOneInput(b, len);   /* returns only if nothing tripped a sanitizer */
        free(b);
        n++;
        t_pass++;
    }
    closedir(d);
    CHECK(n > 0, "no regression inputs in %s", dir);
    printf("regress: %zu inputs\n", n);
    return t_report("test_regress");
}
