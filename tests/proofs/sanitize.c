/* sanitize.c - CBMC proof: fa_sanitize_name over every byte string of up to N bytes (D-041).
 * Checked: no out-of-bounds access or overflow, and for every input the result is safe - not absolute, no drive,
 * no empty part, no part that is "." or that Windows reads as ".." (dots and spaces only), no backslash, no control
 * byte (C0 or DEL, D-045). */
#include "../../src/core/util.c"

#ifndef N
#define N 7
#endif

static bool dots_only(const char *p, size_t len) {
    for (size_t i = 0; i < len; i++) if (p[i] != '.' && p[i] != ' ') return false;
    return len > 0;
}

void harness(void) {
    char name[N + 1];
    for (size_t i = 0; i < N; i++) name[i] = nondet_char();
    name[N] = 0;
    bool ok = fa_sanitize_name(name);
    size_t n = strlen(name);
    if (!ok) return;
    __CPROVER_assert(n > 0, "a kept name is not empty");
    __CPROVER_assert(name[0] != '/', "not absolute");
    __CPROVER_assert(!(n >= 2 && name[1] == ':' && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))),
                     "no leading drive");
    size_t part = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || name[i] == '/') {
            size_t len = i - part;
            __CPROVER_assert(len > 0, "no empty part");
            __CPROVER_assert(!dots_only(name + part, len), "no part of only dots/spaces (., .., .. , . .)");
            part = i + 1;
        } else {
            __CPROVER_assert(name[i] != '\\', "no backslash");
            __CPROVER_assert((unsigned char)name[i] >= 0x20 && name[i] != 0x7F, "no control byte");
        }
    }
}
