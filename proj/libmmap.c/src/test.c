/**
 * test.c - libmmap public API tests.
 * Summary: Contract tests for the file-backed mmap value API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmmap.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints one canonical test-case result line.
 * @param fail Nonzero when the case failed.
 * @param name Canonical test-case name.
 * @param description Human-readable test-case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, description);
}

typedef int (*case_fn)(void);

/**
 * Executes one test case and accumulates its result.
 * @param rc Aggregate failed-case count.
 * @param fn Test-case function to execute.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean test expectation.
 * @param name Expectation description.
 * @param condition Nonzero when the expectation is satisfied.
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
 * Verifies one integer test expectation.
 * @param name Expectation description.
 * @param expected Expected integer value.
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
 * Creates a unique temporary file path.
 * @param out Destination buffer.
 * @param cap Destination buffer size.
 * @param suffix Test-specific suffix.
 * @return 0 on success, 1 on failure.
 */
static int temp_path(char *out, size_t cap, const char *suffix) {
#ifdef _WIN32
    char base[MAX_PATH];
    DWORD len = GetTempPathA((DWORD)sizeof(base), base);
    if (len == 0 || len >= sizeof(base)) return 1;
    return (size_t)snprintf(out, cap, "%skc-mmap-%lu-%s.bin",
        base, (unsigned long)GetCurrentProcessId(), suffix) >= cap;
#else
    const char *base = getenv("TMPDIR");
    if (!base || !base[0]) base = "/tmp";
    return (size_t)snprintf(out, cap, "%s/kc-mmap-%ld-%s.bin",
        base, (long)getpid(), suffix) >= cap;
#endif
}

/**
 * Writes exact bytes to a file.
 * @param path Destination path.
 * @param data Source bytes.
 * @param size Byte count.
 * @return 0 on success, 1 on failure.
 */
static int write_file(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");

    if (!file) return 1;
    if (size > 0U && fwrite(data, 1U, size, file) != size) {
        fclose(file);
        return 1;
    }
    return fclose(file) == 0 ? 0 : 1;
}

/**
 * Checks whether a path exists as a readable file.
 * @param path File path.
 * @return Nonzero when the file exists.
 */
static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");

    if (!file) return 0;
    fclose(file);
    return 1;
}

/**
 * Tests kc_mmap_open.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_open(void) {
    char missing[512];
    char existing[512];
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(missing, sizeof(missing), "open-missing") ||
        temp_path(existing, sizeof(existing), "open-existing")) return 1;
    remove(missing);
    remove(existing);
    if (write_file(existing, "AB", 2U)) return 1;

    fail += expect_int("open NULL out", KC_MMAP_ERROR,
        kc_mmap_open(NULL, existing));
    fail += expect_int("open NULL path", KC_MMAP_ERROR,
        kc_mmap_open(&map, NULL));
    fail += expect_int("open empty path", KC_MMAP_ERROR,
        kc_mmap_open(&map, ""));

    fail += expect_int("open missing path", KC_MMAP_OK,
        kc_mmap_open(&map, missing));
    fail += expect_int("missing path get", KC_MMAP_NOT_FOUND,
        kc_mmap_get(map, &data, &size));
    kc_mmap_close(map);
    map = NULL;

    fail += expect_int("open existing path", KC_MMAP_OK,
        kc_mmap_open(&map, existing));
    fail += expect_int("existing path get", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("existing data matches",
        size == 2U && data != NULL && memcmp(data, "AB", 2U) == 0);

    kc_mmap_close(map);
    remove(existing);
    case_result(fail, "kc_mmap_open",
        "opens existing and missing file-backed values");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_get.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_get(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    const void *data = (const void *)1;
    size_t size = 99U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "get")) return 1;
    remove(path);
    if (write_file(path, "", 0U)) return 1;

    fail += expect_int("get NULL map", KC_MMAP_ERROR,
        kc_mmap_get(NULL, &data, &size));
    fail += expect_true("get NULL map clears outputs",
        data == NULL && size == 0U);

    fail += expect_int("open empty file", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("get empty value", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("empty value has zero transport size",
        size == 0U);

    fail += expect_int("get NULL data output", KC_MMAP_ERROR,
        kc_mmap_get(map, NULL, &size));
    fail += expect_int("get NULL size output", KC_MMAP_ERROR,
        kc_mmap_get(map, &data, NULL));

    kc_mmap_close(map);
    remove(path);
    case_result(fail, "kc_mmap_get",
        "distinguishes an empty value from no value");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_set.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_set(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "set")) return 1;
    remove(path);

    fail += expect_int("open missing path", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("set A", KC_MMAP_OK,
        kc_mmap_set(map, "A", 1U));
    fail += expect_int("get A", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("A visible before save",
        size == 1U && memcmp(data, "A", 1U) == 0);
    fail += expect_true("set does not save implicitly", !file_exists(path));

    fail += expect_int("replace with AB", KC_MMAP_OK,
        kc_mmap_set(map, "AB", 2U));
    fail += expect_int("get AB", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("replacement visible",
        size == 2U && memcmp(data, "AB", 2U) == 0);

    fail += expect_int("set NULL", KC_MMAP_OK,
        kc_mmap_set(map, NULL, 0U));
    fail += expect_int("get after NULL set", KC_MMAP_NOT_FOUND,
        kc_mmap_get(map, &data, &size));
    fail += expect_int("NULL with nonzero size fails", KC_MMAP_ERROR,
        kc_mmap_set(map, NULL, 1U));

    kc_mmap_close(map);
    remove(path);
    case_result(fail, "kc_mmap_set",
        "replaces the in-memory value without implicit persistence");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_save.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_save(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    kc_mmap_t *verify = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "save")) return 1;
    remove(path);

    fail += expect_int("open missing path", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("save initial null", KC_MMAP_OK,
        kc_mmap_save(map));
    fail += expect_int("initial null save invalidates instance", KC_MMAP_ERROR,
        kc_mmap_set(map, "AB", 2U));
    kc_mmap_close(map);
    map = NULL;

    fail += expect_int("reopen missing path", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("set AB", KC_MMAP_OK,
        kc_mmap_set(map, "AB", 2U));
    fail += expect_int("save AB", KC_MMAP_OK, kc_mmap_save(map));
    fail += expect_true("save creates file", file_exists(path));

    fail += expect_int("open saved value", KC_MMAP_OK,
        kc_mmap_open(&verify, path));
    fail += expect_int("get saved value", KC_MMAP_OK,
        kc_mmap_get(verify, &data, &size));
    fail += expect_true("saved bytes match",
        size == 2U && memcmp(data, "AB", 2U) == 0);
    kc_mmap_close(verify);
    verify = NULL;

    fail += expect_int("set zero-byte string", KC_MMAP_OK,
        kc_mmap_set(map, "", 0U));
    fail += expect_int("save zero-byte string", KC_MMAP_OK, kc_mmap_save(map));
    fail += expect_int("open zero-byte saved value", KC_MMAP_OK,
        kc_mmap_open(&verify, path));
    fail += expect_int("get zero-byte saved value", KC_MMAP_OK,
        kc_mmap_get(verify, &data, &size));
    fail += expect_true("zero-byte file remains a value", size == 0U);
    kc_mmap_close(verify);
    verify = NULL;

    fail += expect_int("set null", KC_MMAP_OK,
        kc_mmap_set(map, NULL, 0U));
    fail += expect_int("get null before save", KC_MMAP_NOT_FOUND,
        kc_mmap_get(map, &data, &size));
    fail += expect_int("save null", KC_MMAP_OK, kc_mmap_save(map));
    fail += expect_true("saving null removes file", !file_exists(path));
    fail += expect_int("save invalidated null instance", KC_MMAP_ERROR,
        kc_mmap_save(map));

    kc_mmap_close(map);
    case_result(fail, "kc_mmap_save",
        "persists values and turns saved null into deletion");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_del.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_del(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "del")) return 1;
    remove(path);
    if (write_file(path, "AB", 2U)) return 1;

    fail += expect_int("open existing value", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("delete value", KC_MMAP_OK, kc_mmap_del(map));
    fail += expect_true("delete removes backing file", !file_exists(path));
    fail += expect_int("get after delete fails", KC_MMAP_ERROR,
        kc_mmap_get(map, &data, &size));
    fail += expect_int("set after delete fails", KC_MMAP_ERROR,
        kc_mmap_set(map, "A", 1U));
    fail += expect_int("save after delete fails", KC_MMAP_ERROR,
        kc_mmap_save(map));
    fail += expect_int("delete after delete fails", KC_MMAP_ERROR,
        kc_mmap_del(map));
    kc_mmap_close(map);

    map = NULL;
    fail += expect_int("reopen deleted path", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("reopened deleted path has no value",
        KC_MMAP_NOT_FOUND, kc_mmap_get(map, &data, &size));
    fail += expect_int("delete missing backing file", KC_MMAP_OK,
        kc_mmap_del(map));
    kc_mmap_close(map);

    case_result(fail, "kc_mmap_del",
        "deletes persistence and invalidates the instance");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_close.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_close(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    int fail = 0;

    if (temp_path(path, sizeof(path), "close")) return 1;
    remove(path);

    kc_mmap_close(NULL);
    fail += expect_int("open value", KC_MMAP_OK, kc_mmap_open(&map, path));
    kc_mmap_close(map);

    case_result(fail, "kc_mmap_close",
        "releases instances and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_version.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_version(void) {
    int fail = expect_true("version nonzero", kc_mmap_version() != 0U);

    case_result(fail, "kc_mmap_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all public contract cases.
 * @return Failed case count.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 7;
    test_case_current = 0;

    run_case(&rc, case_kc_mmap_open);
    run_case(&rc, case_kc_mmap_get);
    run_case(&rc, case_kc_mmap_set);
    run_case(&rc, case_kc_mmap_save);
    run_case(&rc, case_kc_mmap_del);
    run_case(&rc, case_kc_mmap_close);
    run_case(&rc, case_kc_mmap_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_mmap_open") == 0) return case_kc_mmap_open();
    if (strcmp(argv[1], "kc_mmap_get") == 0) return case_kc_mmap_get();
    if (strcmp(argv[1], "kc_mmap_set") == 0) return case_kc_mmap_set();
    if (strcmp(argv[1], "kc_mmap_save") == 0) return case_kc_mmap_save();
    if (strcmp(argv[1], "kc_mmap_del") == 0) return case_kc_mmap_del();
    if (strcmp(argv[1], "kc_mmap_close") == 0) return case_kc_mmap_close();
    if (strcmp(argv[1], "kc_mmap_version") == 0) return case_kc_mmap_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
