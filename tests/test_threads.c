/* test_threads.c - archives on different threads are independent (D-041, T029).
 *
 * Eight threads start at one barrier and each opens and extracts the same set of archives, which between them build
 * every table the library makes on first use: CRC-32 (ZIP, 7z, RAR's own), CRC-16 (LHA), CRC-64 (xz in ZIP), the AES
 * tables and the AES-NI probe (WinZip AES, 7zAES). Each extraction must succeed with its checksum. Under
 * ThreadSanitizer (`make tsan`) the first-use tables showed a race before fa_once. POSIX threads, or Win32 threads on Windows. */
#define _POSIX_C_SOURCE 200809L   /* pthread barriers */
#include "fulta/arc.h"
#include "testlib.h"

#include <stdatomic.h>
#if defined(_WIN32)
#include <windows.h>
typedef HANDLE thr_t;
static volatile LONG g_ready;                      /* a start line: every thread spins until all have started */
#define START_WAIT(n) do { InterlockedIncrement(&g_ready); while (g_ready < (n)) { } } while (0)
#else
#include <pthread.h>
typedef pthread_t thr_t;
static pthread_barrier_t g_start;
#define START_WAIT(n) pthread_barrier_wait(&g_start)
#endif

#define THREADS 8

static const char *const FILES[] = {
    "zip7z/archives/zip_aes128.zip", "zip7z/archives/zip_aes256.zip", "zip7z/archives/7z_aes.7z",
    "zip7z/archives/zip_xz.zip", "rar/rar.rar", "lha/explzh_723/h0_lh5.lzh",
};
static unsigned char *g_data[sizeof FILES / sizeof *FILES];
static size_t g_size[sizeof FILES / sizeof *FILES];
static atomic_int g_fail, g_done;

static fulta_arc_err_t pw(void *ctx, uint32_t attempt, const char **out) {
    (void)ctx;
    static const char *const p[] = {"pass1234", "password", "pass"};
    if (attempt >= 3) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *out = p[attempt];
    return FULTA_ARC_OK;
}
static fulta_arc_err_t null_write(void *ctx, const void *d, size_t n) { (void)ctx; (void)d; (void)n; return FULTA_ARC_OK; }

static void work(void) {
    START_WAIT(THREADS);
    for (size_t k = 0; k < sizeof FILES / sizeof *FILES; k++) {
        if (!g_data[k]) continue;
        fulta_arc_options_t o = {.password = pw, .name = FILES[k]};
        fulta_arc_t *a = NULL;
        if (fulta_arc_open_memory(g_data[k], g_size[k], &o, &a) != FULTA_ARC_OK) { atomic_fetch_add(&g_fail, 1); continue; }
        fulta_arc_sink_t s = {.write = null_write};
        for (size_t i = 0; i < fulta_arc_count(a); i++) {
            fulta_arc_err_t e = fulta_arc_extract(a, i, &s);
            if (e) { atomic_fetch_add(&g_fail, 1); fprintf(stderr, "%s #%zu: %s\n", FILES[k], i, fulta_arc_strerror(e)); }
            else atomic_fetch_add(&g_done, 1);
        }
        fulta_arc_close(a);
    }
}
#if defined(_WIN32)
static DWORD WINAPI worker(LPVOID arg) { (void)arg; work(); return 0; }
#else
static void *worker(void *arg) { (void)arg; work(); return NULL; }
#endif

int main(void) {
    size_t have = 0;
    for (size_t k = 0; k < sizeof FILES / sizeof *FILES; k++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", t_fixtures(), FILES[k]);
        g_data[k] = t_read_file(path, &g_size[k]);
        if (g_data[k]) have++;
        else SKIP("%s not present", FILES[k]);
    }
    if (have) {
        thr_t t[THREADS];
#if defined(_WIN32)
        for (int i = 0; i < THREADS; i++) t[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        WaitForMultipleObjects(THREADS, t, TRUE, INFINITE);
        for (int i = 0; i < THREADS; i++) CloseHandle(t[i]);
#else
        pthread_barrier_init(&g_start, NULL, THREADS);
        for (int i = 0; i < THREADS; i++) pthread_create(&t[i], NULL, worker, NULL);
        for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
        pthread_barrier_destroy(&g_start);
#endif
        CHECK(atomic_load(&g_fail) == 0, "%d extractions failed across %d threads", atomic_load(&g_fail), THREADS);
        CHECK(atomic_load(&g_done) > 0, "extractions ran (%d)", atomic_load(&g_done));
    }
    for (size_t k = 0; k < sizeof FILES / sizeof *FILES; k++) free(g_data[k]);
    return t_report("test_threads");
}
