/**
 * test.c - libinit public API contract tests.
 * Summary: Tests persistent startup registration and the grouped CLI contract.
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
 * Set the test metadata directory.
 * @param dir Directory path.
 * @return Zero on success, nonzero on failure.
 */
static int fixture_set_dir(const char *dir) {
#ifdef _WIN32
    return _putenv_s("KC_INIT_DIR", dir) == 0 ? 0 : 1;
#else
    return setenv("KC_INIT_DIR", dir, 1) == 0 ? 0 : 1;
#endif
}

/**
 * Build one fixture child path.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @param dir Fixture directory.
 * @param name Child file name.
 * @return Zero on success, nonzero on overflow.
 */
static int fixture_path(
    char *out,
    size_t cap,
    const char *dir,
    const char *name
) {
#ifdef _WIN32
    return (size_t)snprintf(out, cap, "%s\\%s", dir, name) < cap ? 0 : 1;
#else
    return (size_t)snprintf(out, cap, "%s/%s", dir, name) < cap ? 0 : 1;
#endif
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
        if (fixture_path(
                path,
                sizeof(path),
                dir,
                files[i]
            ) != 0) {
            continue;
        }
#ifdef _WIN32
        (void)DeleteFileA(path);
#else
        (void)remove(path);
#endif
    }
#ifdef _WIN32
    (void)RemoveDirectoryA(dir);
#else
    (void)rmdir(dir);
#endif
}

/**
 * Build one temporary metadata fixture.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @return Zero on success, nonzero on failure.
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
    fixture_remove(out);
    if (!CreateDirectoryA(out, NULL)) return 1;
    if (fixture_path(
            path,
            sizeof(path),
            out,
            "test-entry"
        ) != 0) {
        return 1;
    }
#else
    if ((size_t)snprintf(
            out,
            cap,
            "/tmp/kc-init-test-%lu",
            (unsigned long)getpid()
        ) >= cap) {
        return 1;
    }
    fixture_remove(out);
    if (mkdir(out, 0700) != 0) return 1;
    if (fixture_path(
            path,
            sizeof(path),
            out,
            "test-entry"
        ) != 0) {
        return 1;
    }
#endif

    file = fopen(path, "w");
    if (!file) return 1;
    fputs("echo init-test", file);
    fclose(file);

    if (fixture_path(
            path,
            sizeof(path),
            out,
            "test-entry.user"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "w");
    if (!file) return 1;
    fputs("tester", file);
    fclose(file);

    if (fixture_path(
            path,
            sizeof(path),
            out,
            "test-entry.backend"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "w");
    if (!file) return 1;
    fputs("1", file);
    fclose(file);

    return fixture_set_dir(out);
}

/**
 * Test kc_init_create.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_create(void) {
    const char *name = "kc_init_create";
    const char *detail = "validates startup registration input";
    kc_init_options_t options;
    int fail;

    memset(&options, 0, sizeof(options));
    fail = 0;
    fail += expect_int(
        "NULL options rejected",
        KC_INIT_ERROR,
        kc_init_create("entry", NULL)
    );
    fail += expect_int(
        "NULL command rejected",
        KC_INIT_ERROR,
        kc_init_create("entry", &options)
    );
    options.cmd = "echo ok";
    fail += expect_int(
        "invalid name rejected",
        KC_INIT_ERROR,
        kc_init_create("../bad", &options)
    );

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_open.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_open(void) {
    const char *name = "kc_init_open";
    const char *detail = "opens one persistent registration";
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) {
        fail += expect_int(
            "existing entry opens",
            KC_INIT_OK,
            kc_init_open(&init, "test-entry")
        );
    }
    fail += expect_true("handle exists", init != NULL);
    kc_init_close(init);
    init = NULL;
    fail += expect_int(
        "missing entry reports NOT_FOUND",
        KC_INIT_NOT_FOUND,
        kc_init_open(&init, "missing-entry")
    );
    fail += expect_int(
        "invalid name rejected",
        KC_INIT_ERROR,
        kc_init_open(&init, "../bad")
    );

    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_list.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_list(void) {
    const char *name = "kc_init_list";
    const char *detail = "lists logical registrations without metadata sidecars";
    kc_init_entry_t *entries;
    char dir[1024] = {0};
    size_t count;
    int fail;

    entries = NULL;
    count = 0U;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) {
        fail += expect_int(
            "list succeeds",
            KC_INIT_OK,
            kc_init_list(&entries, &count)
        );
        fail += expect_true("one entry", count == 1U);
        if (count == 1U) {
            fail += expect_true(
                "name matches",
                strcmp(entries[0].name, "test-entry") == 0
            );
            fail += expect_true(
                "user matches",
                strcmp(entries[0].user, "tester") == 0
            );
            fail += expect_true(
                "command matches",
                strcmp(entries[0].cmd, "echo init-test") == 0
            );
        }
    }
    kc_init_free(entries);
    fixture_remove(dir);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_delete.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_delete(void) {
    const char *name = "kc_init_delete";
    const char *detail = "treats a missing registration as a no-op";
    char dir[1024] = {0};
    int fail;

    fail = fixture_create(dir, sizeof(dir));
    if (!fail) {
        fail += expect_int(
            "missing delete succeeds",
            KC_INIT_OK,
            kc_init_delete("missing-entry")
        );
    }
    fail += expect_int(
        "invalid name rejected",
        KC_INIT_ERROR,
        kc_init_delete("../bad")
    );
    fixture_remove(dir);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_set_cmd.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_set_cmd(void) {
    const char *name = "kc_init_set_cmd";
    const char *detail = "validates command updates on an opened entry";
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) fail += kc_init_open(&init, "test-entry") != KC_INIT_OK;
    if (init) {
        fail += expect_int(
            "NULL command rejected",
            KC_INIT_ERROR,
            kc_init_set_cmd(init, NULL)
        );
        fail += expect_int(
            "multiline command rejected",
            KC_INIT_ERROR,
            kc_init_set_cmd(init, "echo a\necho b")
        );
    }
    fail += expect_int(
        "NULL handle rejected",
        KC_INIT_ERROR,
        kc_init_set_cmd(NULL, "echo ok")
    );

    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_get_cmd.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_get_cmd(void) {
    const char *name = "kc_init_get_cmd";
    const char *detail = "returns the persisted startup command";
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) fail += kc_init_open(&init, "test-entry") != KC_INIT_OK;
    fail += expect_true(
        "command matches",
        init && strcmp(kc_init_get_cmd(init), "echo init-test") == 0
    );
    fail += expect_true("NULL handle", kc_init_get_cmd(NULL) == NULL);

    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_get_user.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_get_user(void) {
    const char *name = "kc_init_get_user";
    const char *detail = "returns the persisted registration user";
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) fail += kc_init_open(&init, "test-entry") != KC_INIT_OK;
    fail += expect_true(
        "user matches",
        init && strcmp(kc_init_get_user(init), "tester") == 0
    );
    fail += expect_true("NULL handle", kc_init_get_user(NULL) == NULL);

    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_error.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_error(void) {
    const char *name = "kc_init_error";
    const char *detail = "returns NULL for a fresh valid handle";
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) fail += kc_init_open(&init, "test-entry") != KC_INIT_OK;
    fail += expect_true("fresh error NULL", kc_init_error(init) == NULL);
    fail += expect_true("NULL handle", kc_init_error(NULL) == NULL);

    kc_init_close(init);
    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_free.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_free(void) {
    const char *name = "kc_init_free";
    const char *detail = "releases list allocations and accepts NULL";
    kc_init_entry_t *entries;
    char dir[1024] = {0};
    size_t count;
    int fail;

    entries = NULL;
    count = 0U;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) fail += kc_init_list(&entries, &count) != KC_INIT_OK;
    fail += expect_true("allocation exists", entries != NULL && count == 1U);
    kc_init_free(entries);
    kc_init_free(NULL);
    fixture_remove(dir);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_close.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_init_close(void) {
    const char *name = "kc_init_close";
    const char *detail = "releases a local handle and accepts NULL";
    kc_init_t *init;
    char dir[1024] = {0};
    int fail;

    init = NULL;
    fail = fixture_create(dir, sizeof(dir));
    if (!fail) fail += kc_init_open(&init, "test-entry") != KC_INIT_OK;
    fail += expect_true("handle exists", init != NULL);
    kc_init_close(init);
    kc_init_close(NULL);
    fixture_remove(dir);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_init_version.
 * @return Zero on success, nonzero on failure.
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
 * @return Zero on success, nonzero on failure.
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
    fail += expect_true("help succeeds", system(command) == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --version > NUL 2>&1", INIT_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --version > /dev/null 2>&1", INIT_TEST_CLI);
#endif
    fail += expect_true("version succeeds", system(command) == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --unknown > NUL 2>&1", INIT_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --unknown > /dev/null 2>&1", INIT_TEST_CLI);
#endif
    fail += expect_true("unknown option fails", system(command) != 0);

    fail += fixture_create(dir, sizeof(dir));
    if (!fail) {
#ifdef _WIN32
        rc = (int)_spawnl(
            _P_WAIT,
            INIT_TEST_CLI,
            INIT_TEST_CLI,
            "--dir",
            dir,
            "--list",
            NULL
        );
#else
        snprintf(
            command,
            sizeof(command),
            "\"%s\" --dir \"%s\" --list > /dev/null 2>&1",
            INIT_TEST_CLI,
            dir
        );
        rc = system(command);
#endif
        fail += expect_true("local list succeeds", rc == 0);
    }

    fixture_remove(dir);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Run all contract cases.
 * @return Failure count.
 */
static int case_all(void) {
    int rc;

    rc = 0;
    test_case_total = 12;
    test_case_current = 0;
    run_case(&rc, case_kc_init_create);
    run_case(&rc, case_kc_init_open);
    run_case(&rc, case_kc_init_list);
    run_case(&rc, case_kc_init_delete);
    run_case(&rc, case_kc_init_set_cmd);
    run_case(&rc, case_kc_init_get_cmd);
    run_case(&rc, case_kc_init_get_user);
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
    if (argc != 2) return 2;
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_init_create") == 0) return case_kc_init_create();
    if (strcmp(argv[1], "kc_init_open") == 0) return case_kc_init_open();
    if (strcmp(argv[1], "kc_init_list") == 0) return case_kc_init_list();
    if (strcmp(argv[1], "kc_init_delete") == 0) return case_kc_init_delete();
    if (strcmp(argv[1], "kc_init_set_cmd") == 0) return case_kc_init_set_cmd();
    if (strcmp(argv[1], "kc_init_get_cmd") == 0) return case_kc_init_get_cmd();
    if (strcmp(argv[1], "kc_init_get_user") == 0) return case_kc_init_get_user();
    if (strcmp(argv[1], "kc_init_error") == 0) return case_kc_init_error();
    if (strcmp(argv[1], "kc_init_free") == 0) return case_kc_init_free();
    if (strcmp(argv[1], "kc_init_close") == 0) return case_kc_init_close();
    if (strcmp(argv[1], "kc_init_version") == 0) return case_kc_init_version();
    if (strcmp(argv[1], "kc_init_cli") == 0) return case_kc_init_cli();
    return 2;
}
