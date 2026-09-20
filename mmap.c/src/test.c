/**
 * test.c - libmmap public API tests.
 * Summary: Tests each public libmmap function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libmmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#define getpid _getpid
#define write_file(path, data, len) write_file_win(path, data, len)
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define write_file(path, data, len) write_file_posix(path, data, len)
#endif

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one size_t result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_size(const char *name, size_t expected, size_t actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %zu, got %zu\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Test behavior detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Creates a temporary file path.
 * @param out Output buffer.
 * @param cap Output buffer capacity.
 * @param suffix Suffix for the filename.
 * @return 0 on success, 1 on failure.
 */
static int temp_path(char *out, size_t cap, const char *suffix) {
#ifdef _WIN32
    char tmp[MAX_PATH];

    if (GetTempPathA((DWORD)sizeof(tmp), tmp) == 0) return 1;
    if ((size_t)snprintf(out, cap, "%skc-mmap-test-%ld-%s.bin",
            tmp, (long)getpid(), suffix) >= cap) return 1;
#else
    const char *base;

    base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') base = "/tmp";
    if ((size_t)snprintf(out, cap, "%s/kc-mmap-test-%ld-%s.bin",
            base, (long)getpid(), suffix) >= cap) return 1;
#endif
    return 0;
}

#ifndef _WIN32
/**
 * Writes bytes to a file on POSIX.
 * @param path File path.
 * @param data Data to write.
 * @param len Data length.
 * @return 0 on success, 1 on failure.
 */
static int write_file_posix(const char *path, const void *data, size_t len) {
    int fd;
    ssize_t n;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 1;
    n = write(fd, data, len);
    close(fd);
    return n == (ssize_t)len ? 0 : 1;
}
#endif

#ifdef _WIN32
/**
 * Writes bytes to a file on Windows.
 * @param path File path.
 * @param data Data to write.
 * @param len Data length.
 * @return 0 on success, 1 on failure.
 */
static int write_file_win(const char *path, const void *data, size_t len) {
    HANDLE h;
    DWORD bw;

    h = CreateFileA(path, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 1;
    WriteFile(h, data, (DWORD)len, &bw, NULL);
    CloseHandle(h);
    return bw == (DWORD)len ? 0 : 1;
}
#endif

/**
 * Tests kc_mmap_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mmap_version(void) {
    const char *name = "kc_mmap_version";
    const char *detail = "returns a non-zero build timestamp";
    int fail = expect_true("kc_mmap_version returns non-zero build timestamp",
        kc_mmap_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mmap_open(void) {
    const char *name = "kc_mmap_open";
    const char *detail = "validates arguments and maps files";
    kc_mmap_t *ctx = NULL;
    char path[512];
    int fail = 0;

    fail += expect_int("open(NULL, path) returns ERROR", KC_MMAP_ERROR,
        kc_mmap_open(NULL, "nonexistent"));
    fail += expect_int("open(out, NULL) returns ERROR", KC_MMAP_ERROR,
        kc_mmap_open(&ctx, NULL));
    fail += expect_true("open validation leaves output NULL", ctx == NULL);
    fail += expect_int("open nonexistent file returns ERROR", KC_MMAP_ERROR,
        kc_mmap_open(&ctx, "/tmp/kc-mmap-nonexistent-xyz"));
    fail += expect_true("open failure leaves output NULL", ctx == NULL);
    if (temp_path(path, sizeof(path), "open") != 0) return 1;
    if (write_file(path, "hello", 5) != 0) return 1;
    fail += expect_int("open valid file returns OK", KC_MMAP_OK,
        kc_mmap_open(&ctx, path));
    fail += expect_true("open creates valid context", ctx != NULL);
    kc_mmap_close(ctx);
    ctx = NULL;
    if (write_file(path, "", 0) != 0) return 1;
    fail += expect_int("open empty file returns OK", KC_MMAP_OK,
        kc_mmap_open(&ctx, path));
    fail += expect_true("open empty file creates valid context", ctx != NULL);
    kc_mmap_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_data.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mmap_data(void) {
    const char *name = "kc_mmap_data";
    const char *detail = "exposes mapped bytes";
    kc_mmap_t *ctx = NULL;
    char path[512];
    int fail = 0;

    fail += expect_true("data(NULL) returns NULL", kc_mmap_data(NULL) == NULL);
    if (temp_path(path, sizeof(path), "data") != 0) return 1;
    if (write_file(path, "content", 7) != 0) return 1;
    if (kc_mmap_open(&ctx, path) != KC_MMAP_OK) return 1;
    fail += expect_true("data returns non-NULL pointer",
        kc_mmap_data(ctx) != NULL);
    fail += expect_true("data content matches written bytes",
        memcmp(kc_mmap_data(ctx), "content", 7) == 0);
    kc_mmap_close(ctx);
    if (temp_path(path, sizeof(path), "data-empty") != 0) return 1;
    if (write_file(path, "", 0) != 0) return 1;
    if (kc_mmap_open(&ctx, path) != KC_MMAP_OK) return 1;
    fail += expect_true("data on empty file returns NULL",
        kc_mmap_data(ctx) == NULL);
    kc_mmap_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_size.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mmap_size(void) {
    const char *name = "kc_mmap_size";
    const char *detail = "reports mapped file length";
    kc_mmap_t *ctx = NULL;
    char path[512];
    int fail = 0;

    fail += expect_size("size(NULL) returns 0", 0, kc_mmap_size(NULL));
    if (temp_path(path, sizeof(path), "size") != 0) return 1;
    if (write_file(path, "12345", 5) != 0) return 1;
    if (kc_mmap_open(&ctx, path) != KC_MMAP_OK) return 1;
    fail += expect_size("size returns file length", 5, kc_mmap_size(ctx));
    kc_mmap_close(ctx);
    if (temp_path(path, sizeof(path), "size-empty") != 0) return 1;
    if (write_file(path, "", 0) != 0) return 1;
    if (kc_mmap_open(&ctx, path) != KC_MMAP_OK) return 1;
    fail += expect_size("size empty file returns 0", 0, kc_mmap_size(ctx));
    kc_mmap_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mmap_close(void) {
    const char *name = "kc_mmap_close";
    const char *detail = "releases mapped context";
    kc_mmap_t *ctx = NULL;
    char path[512];
    int fail = 0;

    kc_mmap_close(NULL);
    fail += expect_true("close(NULL) does not crash", 1);
    if (temp_path(path, sizeof(path), "close") != 0) return 1;
    if (write_file(path, "close-test", 10) != 0) return 1;
    if (kc_mmap_open(&ctx, path) != KC_MMAP_OK) return 1;
    kc_mmap_close(ctx);
    fail += expect_true("close releases context", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests multiple contexts coexist with isolated state.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mmap_multictx(void) {
    const char *name = "kc_mmap_multictx";
    const char *detail = "contexts stay independent";
    kc_mmap_t *a = NULL;
    kc_mmap_t *b = NULL;
    char path_a[512];
    char path_b[512];
    int fail = 0;

    if (temp_path(path_a, sizeof(path_a), "multi-a") != 0) return 1;
    if (temp_path(path_b, sizeof(path_b), "multi-b") != 0) return 1;
    if (write_file(path_a, "alpha", 5) != 0) return 1;
    if (write_file(path_b, "beta-data", 9) != 0) return 1;
    if (kc_mmap_open(&a, path_a) != KC_MMAP_OK) return 1;
    if (kc_mmap_open(&b, path_b) != KC_MMAP_OK) {
        kc_mmap_close(a);
        return 1;
    }
    fail += expect_size("a size matches its file", 5, kc_mmap_size(a));
    fail += expect_true("a data matches its file",
        memcmp(kc_mmap_data(a), "alpha", 5) == 0);
    fail += expect_size("b size matches its file", 9, kc_mmap_size(b));
    fail += expect_true("b data matches its file",
        memcmp(kc_mmap_data(b), "beta-data", 9) == 0);
    kc_mmap_close(a);
    fail += expect_true("b data remains available after closing a",
        kc_mmap_data(b) != NULL);
    fail += expect_true("b bytes remain valid after closing a",
        memcmp(kc_mmap_data(b), "beta-data", 9) == 0);
    fail += expect_size("b size remains valid after closing a", 9,
        kc_mmap_size(b));
    kc_mmap_close(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 6;
    test_case_current = 0;
    run_case(&rc, case_kc_mmap_version);
    run_case(&rc, case_kc_mmap_open);
    run_case(&rc, case_kc_mmap_data);
    run_case(&rc, case_kc_mmap_size);
    run_case(&rc, case_kc_mmap_close);
    run_case(&rc, case_kc_mmap_multictx);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_mmap_version") == 0) return case_kc_mmap_version();
    if (strcmp(argv[1], "kc_mmap_open") == 0) return case_kc_mmap_open();
    if (strcmp(argv[1], "kc_mmap_data") == 0) return case_kc_mmap_data();
    if (strcmp(argv[1], "kc_mmap_size") == 0) return case_kc_mmap_size();
    if (strcmp(argv[1], "kc_mmap_close") == 0) return case_kc_mmap_close();
    if (strcmp(argv[1], "kc_mmap_multictx") == 0) return case_kc_mmap_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
