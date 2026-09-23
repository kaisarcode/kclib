/**
 * test.c - libinit public API contract tests.
 * Summary: Tests the normalized startup registry API and grouped CLI contract.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libinit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef INIT_TEST_CLI
#define INIT_TEST_CLI ""
#endif

static int test_case_total;
static int test_case_current;

typedef int (*case_fn)(void);

/**
 * Print one top-level test result.
 * @param fail Failure count.
 * @param name Canonical case name.
 * @param detail Case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf(
        "[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail
    );
}

/**
 * Run one top-level case.
 * @param rc Failure accumulator.
 * @param fn Case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verify one integer result.
 * @param label Check label.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return Failure count.
 */
static int expect_int(const char *label, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", label, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verify one condition.
 * @param label Check label.
 * @param condition Condition.
 * @return Failure count.
 */
static int expect_true(const char *label, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", label);
        return 1;
    }
    return 0;
}

/**
 * Build one temporary fixture directory.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int fixture_create(char *out, size_t cap) {
    char path[1024];
    FILE *file;
#ifdef _WIN32
    char temp[MAX_PATH];
    DWORD size;

    size = GetTempPathA((DWORD)sizeof(temp), temp);
    if (size == 0 || size >= (DWORD)sizeof(temp)) return 1;
    if ((size_t)snprintf(
            out,
            cap,
            "%skc-init-test-%lu",
            temp,
            (unsigned long)_getpid()
        ) >= cap) {
        return 1;
    }
    (void)RemoveDirectoryA(out);
    if (!CreateDirectoryA(out, NULL)) return 1;
#define FIXTURE_SEP "\\"
#else
    if ((size_t)snprintf(
            out,
            cap,
            "/tmp/kc-init-test-%lu",
            (unsigned long)getpid()
        ) >= cap) {
        return 1;
    }
    (void)rmdir(out);
    if (mkdir(out, 0700) != 0) return 1;
#define FIXTURE_SEP "/"
#endif

    if ((size_t)snprintf(
            path,
            sizeof(path),
            "%s" FIXTURE_SEP "test-entry",
            out
        ) >= sizeof(path)) {
        return 1;
    }
    file = fopen(path, "w");
    if (!file) return 1;
    fputs("echo init-test", file);
    fclose(file);

    if ((size_t)snprintf(
            path,
            sizeof(path),
            "%s" FIXTURE_SEP "test-entry.user",
            out
        ) >= sizeof(path)) {
        return 1;
    }
    file = fopen(path, "w");
    if (!file) return 1;
    fputs("tester", file);
    fclose(file);

    if ((size_t)snprintf(
            path,
            sizeof(path),
            "%s" FIXTURE_SEP "test-entry.backend",
            out
        ) >= sizeof(path)) {
        return 1;
    }
    file = fopen(path, "w");
    if (!file) return 1;
    fputs("1", file);
    fclose(file);

    return 0;
}

/**
 * Remove one temporary fixture directory.
 * @param dir Fixture path.
 * @return None.
 */
static void fixture_remove(const char *dir) {
    static const char *files[] = {
        "test-entry",
        "test-entry.user",
        "test-entry.backend"
    };
    char path[1024];
    size_t i;

    if (!dir || !dir[0]) return;
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if ((size_t)snprintf(
                path,
                sizeof(path),
                "%s" FIXTURE_SEP "%s",
                dir,
                files[i]
            ) < sizeof(path)) {
#ifdef _WIN32
            (void)DeleteFileA(path);
#else
            (void)remove(path);
#endif
        }
    }
#ifdef _WIN32
    (void)RemoveDirectoryA(dir);
#else
    (void)rmdir(dir);
#endif
}

/**
 * Test kc_init_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_open(void) {
    const char *name = "kc_init_open";
    const char *detail = "opens defaults and plain options";
    kc_init_options_t options = {0};
    kc_init_t *init;
    int fail;

    init = NULL;
    fail = 0;
    fail += expect_int(
        "open rejects NULL output",
        KC_INIT_ERROR,
        kc_init_open(NULL, NULL)
    );
    fail += expect_int(
        "open defaults",
        KC_INIT_OK,
        kc_init_open(&init, NULL)
    );
    fail += expect_true("default context exists", init != NULL);
    kc_init_close(init);

    options.dir = ".";
    init = NULL;
    fail += expect_int(
        "open explicit dir",
        KC_INIT_OK,
        kc_init_open(&init, &options)
    );
    fail += expect_true(
        "explicit dir copied",
        init != NULL && strcmp(kc_init_path(init), ".") == 0
    );
    kc_init_close(init);

#ifndef _WIN32
    options.backend = "__invalid_backend__";
    init = NULL;
    fail += expect_int(
        "explicit invalid backend rejected",
        KC_INIT_ERROR,
        kc_init_open(&init, &options)
    );
#endif

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_set(void) {
    const char *name = "kc_init_set";
    const char *detail = "validates registration names and one-line commands";
    kc_init_options_t options = {0};
    kc_init_t *init;
    int fail;

    options.dir = ".";
    init = NULL;
    fail = expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, &options));

    if (init) {
        fail += expect_int(
            "NULL key rejected",
            KC_INIT_ERROR,
            kc_init_set(init, NULL, "echo ok")
        );
        fail += expect_int(
            "path traversal key rejected",
            KC_INIT_ERROR,
            kc_init_set(init, "../bad", "echo ok")
        );
        fail += expect_int(
            "NULL command rejected",
            KC_INIT_ERROR,
            kc_init_set(init, "entry", NULL)
        );
        fail += expect_int(
            "multiline command rejected",
            KC_INIT_ERROR,
            kc_init_set(init, "entry", "echo a\necho b")
        );
    }
    fail += expect_int(
        "NULL context rejected",
        KC_INIT_ERROR,
        kc_init_set(NULL, "entry", "echo ok")
    );

    kc_init_close(init);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_exec(void) {
    const char *name = "kc_init_exec";
    const char *detail = "distinguishes a missing registration";
    kc_init_options_t options = {0};
    kc_init_t *init;
    char dir[1024];
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    options.dir = dir;

    if (!fail) {
        fail += expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, &options));
    }
    if (init) {
        fail += expect_int(
            "missing entry reports NOT_FOUND",
            KC_INIT_NOT_FOUND,
            kc_init_exec(init, "missing-entry")
        );
        fail += expect_int(
            "invalid key reports ERROR",
            KC_INIT_ERROR,
            kc_init_exec(init, "../bad")
        );
    }
    fail += expect_int(
        "NULL context rejected",
        KC_INIT_ERROR,
        kc_init_exec(NULL, "entry")
    );

    kc_init_close(init);
    if (dir[0]) fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_list.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_list(void) {
    const char *name = "kc_init_list";
    const char *detail = "returns owned entries and hides metadata sidecars";
    kc_init_options_t options = {0};
    kc_init_entry_t *entries;
    kc_init_t *init;
    char dir[1024] = {0};
    size_t count;
    int fail;

    entries = NULL;
    init = NULL;
    count = 0U;
    fail = fixture_create(dir, sizeof(dir));
    options.dir = dir;

    if (!fail) {
        fail += expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, &options));
    }
    if (init) {
        fail += expect_int(
            "list all succeeds",
            KC_INIT_OK,
            kc_init_list(init, NULL, &entries, &count)
        );
        fail += expect_true("one logical entry returned", count == 1U);
        if (count == 1U) {
            fail += expect_true(
                "entry key",
                strcmp(entries[0].key, "test-entry") == 0
            );
            fail += expect_true(
                "entry user",
                strcmp(entries[0].user, "tester") == 0
            );
            fail += expect_true(
                "entry command",
                strcmp(entries[0].cmd, "echo init-test") == 0
            );
        }
        kc_init_free(entries);
        entries = NULL;
        count = 99U;

        fail += expect_int(
            "missing filtered list succeeds",
            KC_INIT_OK,
            kc_init_list(init, "missing-entry", &entries, &count)
        );
        fail += expect_true("missing filtered list is empty", count == 0U);
        fail += expect_true("missing filtered list pointer is NULL", entries == NULL);

        fail += expect_int(
            "list requires out_entries",
            KC_INIT_ERROR,
            kc_init_list(init, NULL, NULL, &count)
        );
        fail += expect_int(
            "list requires out_count",
            KC_INIT_ERROR,
            kc_init_list(init, NULL, &entries, NULL)
        );
    }

    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_delete.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_delete(void) {
    const char *name = "kc_init_delete";
    const char *detail = "treats missing entries as a successful no-op";
    kc_init_options_t options = {0};
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    options.dir = dir;

    if (!fail) {
        fail += expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, &options));
    }
    if (init) {
        fail += expect_int(
            "missing delete succeeds",
            KC_INIT_OK,
            kc_init_delete(init, "missing-entry")
        );
        fail += expect_int(
            "invalid delete key rejected",
            KC_INIT_ERROR,
            kc_init_delete(init, "../bad")
        );
    }
    fail += expect_int(
        "NULL context rejected",
        KC_INIT_ERROR,
        kc_init_delete(NULL, "entry")
    );

    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_path.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_path(void) {
    const char *name = "kc_init_path";
    const char *detail = "returns the copied metadata directory";
    kc_init_options_t options = {0};
    kc_init_t *init;
    int fail;

    options.dir = ".";
    init = NULL;
    fail = expect_true("path NULL", kc_init_path(NULL) == NULL);
    fail += expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, &options));
    fail += expect_true(
        "path matches",
        init != NULL && strcmp(kc_init_path(init), ".") == 0
    );
    kc_init_close(init);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_error(void) {
    const char *name = "kc_init_error";
    const char *detail = "exposes context error text after a failed operation";
    kc_init_options_t options = {0};
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = expect_true("error NULL", kc_init_error(NULL) == NULL);
    fail += fixture_create(dir, sizeof(dir));
    options.dir = dir;

    if (!fail) {
        fail += expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, &options));
    }
    if (init) {
        fail += expect_true("fresh error is NULL", kc_init_error(init) == NULL);
        fail += expect_int(
            "missing exec",
            KC_INIT_NOT_FOUND,
            kc_init_exec(init, "missing-entry")
        );
        fail += expect_true("failed operation sets error", kc_init_error(init) != NULL);
    }

    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_free(void) {
    const char *name = "kc_init_free";
    const char *detail = "releases list allocations and accepts NULL";
    kc_init_options_t options = {0};
    kc_init_entry_t *entries;
    kc_init_t *init;
    char dir[1024] = {0};
    size_t count;
    int fail;

    entries = NULL;
    init = NULL;
    count = 0U;
    fail = fixture_create(dir, sizeof(dir));
    options.dir = dir;

    if (!fail) {
        fail += expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, &options));
    }
    if (init) {
        fail += expect_int(
            "list allocates",
            KC_INIT_OK,
            kc_init_list(init, "test-entry", &entries, &count)
        );
        fail += expect_true("one entry allocated", entries != NULL && count == 1U);
    }

    kc_init_free(entries);
    kc_init_free(NULL);
    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_close(void) {
    const char *name = "kc_init_close";
    const char *detail = "releases a context and accepts NULL";
    kc_init_t *init;
    int fail;

    init = NULL;
    fail = expect_int("open succeeds", KC_INIT_OK, kc_init_open(&init, NULL));
    fail += expect_true("context exists", init != NULL);
    kc_init_close(init);
    kc_init_close(NULL);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_version(void) {
    const char *name = "kc_init_version";
    const char *detail = "returns a non-zero generated build version";
    int fail;

    fail = expect_true("version non-zero", kc_init_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test the grouped CLI contract.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_cli(void) {
    const char *name = "kc_init_cli";
    const char *detail = "covers help, version, errors, and local listing";
    char dir[1024] = {0};
    char command[4096];
    int fail;
    int rc;

    fail = 0;
    if (INIT_TEST_CLI[0] == '\0') {
        case_result(1, name, detail);
        return 1;
    }

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --help > NUL 2>&1", INIT_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --help > /dev/null 2>&1", INIT_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI help succeeds", rc == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --version > NUL 2>&1", INIT_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --version > /dev/null 2>&1", INIT_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI version succeeds", rc == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --unknown > NUL 2>&1", INIT_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --unknown > /dev/null 2>&1", INIT_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI unknown option fails", rc != 0);

    fail += fixture_create(dir, sizeof(dir));
    if (!fail) {
#ifdef _WIN32
        snprintf(
            command,
            sizeof(command),
            "\"%s\" --dir \"%s\" --list > NUL 2>&1",
            INIT_TEST_CLI,
            dir
        );
#else
        snprintf(
            command,
            sizeof(command),
            "\"%s\" --dir \"%s\" --list > /dev/null 2>&1",
            INIT_TEST_CLI,
            dir
        );
#endif
        rc = system(command);
        fail += expect_true("CLI local list succeeds", rc == 0);
    }

    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Run all test cases.
 * @return Failure count.
 */
static int case_all(void) {
    int rc;

    rc = 0;
    test_case_total = 11;
    test_case_current = 0;
    run_case(&rc, case_kc_init_open);
    run_case(&rc, case_kc_init_set);
    run_case(&rc, case_kc_init_exec);
    run_case(&rc, case_kc_init_list);
    run_case(&rc, case_kc_init_delete);
    run_case(&rc, case_kc_init_path);
    run_case(&rc, case_kc_init_error);
    run_case(&rc, case_kc_init_free);
    run_case(&rc, case_kc_init_close);
    run_case(&rc, case_kc_init_version);
    run_case(&rc, case_kc_init_cli);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Test executable entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_init_open") == 0) return case_kc_init_open();
    if (strcmp(argv[1], "kc_init_set") == 0) return case_kc_init_set();
    if (strcmp(argv[1], "kc_init_exec") == 0) return case_kc_init_exec();
    if (strcmp(argv[1], "kc_init_list") == 0) return case_kc_init_list();
    if (strcmp(argv[1], "kc_init_delete") == 0) return case_kc_init_delete();
    if (strcmp(argv[1], "kc_init_path") == 0) return case_kc_init_path();
    if (strcmp(argv[1], "kc_init_error") == 0) return case_kc_init_error();
    if (strcmp(argv[1], "kc_init_free") == 0) return case_kc_init_free();
    if (strcmp(argv[1], "kc_init_close") == 0) return case_kc_init_close();
    if (strcmp(argv[1], "kc_init_version") == 0) return case_kc_init_version();
    if (strcmp(argv[1], "kc_init_cli") == 0) return case_kc_init_cli();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
