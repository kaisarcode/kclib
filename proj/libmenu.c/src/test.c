/**
 * test.c - libmenu public API contract tests.
 * Summary: Validates application menu registration.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
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
 * Run one top-level contract case.
 * @param rc Failure accumulator.
 * @param fn Case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verify one boolean condition.
 * @param label Check label.
 * @param condition Condition to verify.
 * @return Failure count.
 */
static int expect_true(const char *label, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", label);
        return 1;
    }
    return 0;
}

#ifndef _WIN32
static char test_data_home[512];

/**
 * Create and activate an isolated XDG data home.
 * @return Zero on success, nonzero on failure.
 */
static int test_prepare_home(void) {
    char pattern[] = "/tmp/kc-menu-test-XXXXXX";
    char *dir = mkdtemp(pattern);

    if (!dir) return 1;
    if (strlen(dir) + 1U > sizeof(test_data_home)) return 1;
    strcpy(test_data_home, dir);
    return setenv("XDG_DATA_HOME", test_data_home, 1) == 0 ? 0 : 1;
}

/**
 * Check whether one Linux desktop entry exists.
 * @param id Menu entry identifier.
 * @return Nonzero when the desktop entry exists.
 */
static int test_linux_entry_exists(const char *id) {
    char path[1024];
    struct stat st;

    snprintf(path, sizeof(path), "%s/applications/%s.desktop", test_data_home, id);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
#endif

/**
 * Build one valid menu test entry.
 * @param id Menu entry identifier.
 * @return Initialized menu entry.
 */
static kc_menu_entry_t test_entry(const char *id) {
    kc_menu_entry_t entry = {
        id,
        "KC Menu Contract",
        "Contract test entry",
#ifdef _WIN32
        "cmd.exe /C exit 0",
#else
        "/bin/true --contract",
#endif
        NULL,
        NULL
    };

    return entry;
}

#ifdef _WIN32
/**
 * Check whether one categorized Windows shortcut exists.
 * @param category Start Menu category folder.
 * @param name Shortcut display name.
 * @return Nonzero when the shortcut exists.
 */
static int test_windows_entry_exists(
    const char *category,
    const char *name
) {
    PWSTR programs = NULL;
    wchar_t path[MAX_PATH * 2];
    wchar_t wcategory[256];
    wchar_t wname[256];
    int exists = 0;

    if (MultiByteToWideChar(
            CP_UTF8, 0, category, -1, wcategory, 256
        ) <= 0) {
        return 0;
    }
    if (MultiByteToWideChar(
            CP_UTF8, 0, name, -1, wname, 256
        ) <= 0) {
        return 0;
    }
    if (FAILED(SHGetKnownFolderPath(
            &FOLDERID_Programs,
            0,
            NULL,
            &programs
        ))) {
        return 0;
    }
    if (swprintf(
            path,
            sizeof(path) / sizeof(path[0]),
            L"%ls\\%ls\\%ls.lnk",
            programs,
            wcategory,
            wname
        ) > 0) {
        DWORD attr = GetFileAttributesW(path);
        exists = attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }
    CoTaskMemFree(programs);
    return exists;
}
#endif

#ifndef _WIN32
/**
 * Check whether one Linux desktop entry contains text.
 * @param id Menu entry identifier.
 * @param text Expected text.
 * @return Nonzero when the text is present.
 */
static int test_linux_entry_contains(const char *id, const char *text) {
    char path[1024];
    char buffer[4096];
    FILE *file;
    size_t size;

    snprintf(path, sizeof(path), "%s/applications/%s.desktop", test_data_home, id);
    file = fopen(path, "rb");
    if (!file) return 0;
    size = fread(buffer, 1, sizeof(buffer) - 1U, file);
    fclose(file);
    buffer[size] = '\0';
    return strstr(buffer, text) != NULL;
}
#endif

/**
 * Test kc_menu_add.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_menu_add(void) {
    const char *name = "kc_menu_add";
    const char *detail = "adds one application menu entry";
    kc_menu_entry_t entry = test_entry("kc-menu-add");
    int fail = 0;

    entry.category = "Network";

#ifndef _WIN32
    if (test_data_home[0] == '\0') {
        fail += expect_true(
            "prepare isolated XDG data home",
            test_prepare_home() == 0
        );
    }
#endif

    (void)kc_menu_delete(entry.id);
    fail += expect_true("add succeeds", kc_menu_add(&entry) == KC_MENU_OK);
    fail += expect_true(
        "duplicate id is rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );
#ifdef _WIN32
    fail += expect_true(
        "shortcut exists in category",
        test_windows_entry_exists(entry.category, entry.name)
    );
#else
    fail += expect_true(
        "desktop entry exists",
        test_linux_entry_exists(entry.id)
    );
    fail += expect_true(
        "desktop entry stores name",
        test_linux_entry_contains(entry.id, "Name=KC Menu Contract")
    );
    fail += expect_true(
        "desktop entry stores description",
        test_linux_entry_contains(entry.id, "Comment=Contract test entry")
    );
    fail += expect_true(
        "desktop entry stores command",
        test_linux_entry_contains(entry.id, "Exec=/bin/true --contract")
    );
    fail += expect_true(
        "desktop entry stores category",
        test_linux_entry_contains(entry.id, "Categories=Network;")
    );
#endif
    (void)kc_menu_delete(entry.id);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_menu_delete.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_menu_delete(void) {
    const char *name = "kc_menu_delete";
    const char *detail = "deletes entries and treats missing ids as a no-op";
    kc_menu_entry_t entry = test_entry("kc-menu-delete");
    int fail = 0;

#ifndef _WIN32
    if (test_data_home[0] == '\0') {
        fail += expect_true(
            "prepare isolated XDG data home",
            test_prepare_home() == 0
        );
    }
#endif

    (void)kc_menu_delete(entry.id);
    fail += expect_true("fixture add succeeds", kc_menu_add(&entry) == KC_MENU_OK);
    fail += expect_true(
        "delete existing succeeds",
        kc_menu_delete(entry.id) == KC_MENU_OK
    );
#ifndef _WIN32
    fail += expect_true(
        "desktop entry is gone",
        !test_linux_entry_exists(entry.id)
    );
#endif
    fail += expect_true(
        "delete missing succeeds",
        kc_menu_delete(entry.id) == KC_MENU_OK
    );

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test menu input validation.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_menu_validation(void) {
    const char *name = "kc_menu_validation";
    const char *detail = "rejects invalid identifiers and required values";
    kc_menu_entry_t entry = test_entry("kc-menu-validation");
    int fail = 0;

    fail += expect_true(
        "NULL entry rejected",
        kc_menu_add(NULL) == KC_MENU_ERROR
    );

    entry.id = NULL;
    fail += expect_true(
        "NULL id rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("");
    fail += expect_true(
        "empty id rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("../bad");
    fail += expect_true(
        "invalid id rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.name = NULL;
    fail += expect_true(
        "NULL name rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.name = "";
    fail += expect_true(
        "empty name rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.command = NULL;
    fail += expect_true(
        "NULL command rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.command = "";
    fail += expect_true(
        "empty command rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.command = "one\ntwo";
    fail += expect_true(
        "multiline command rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.description = "one\ntwo";
    fail += expect_true(
        "multiline description rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.category = "";
    fail += expect_true(
        "empty category rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.category = "Bad/Category";
    fail += expect_true(
        "category path rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    entry = test_entry("kc-menu-validation");
    entry.category = "One;Two";
    fail += expect_true(
        "multiple categories rejected",
        kc_menu_add(&entry) == KC_MENU_ERROR
    );

    fail += expect_true(
        "delete NULL id rejected",
        kc_menu_delete(NULL) == KC_MENU_ERROR
    );
    fail += expect_true(
        "delete invalid id rejected",
        kc_menu_delete("../bad") == KC_MENU_ERROR
    );

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_menu_version.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_menu_version(void) {
    int fail = expect_true("version is nonzero", kc_menu_version() != 0U);

    case_result(
        fail,
        "kc_menu_version",
        "returns the generated build version"
    );
    return fail == 0 ? 0 : 1;
}

/**
 * Run all menu contract cases.
 * @return Failure count.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 4;
    test_case_current = 0;

    run_case(&rc, case_kc_menu_add);
    run_case(&rc, case_kc_menu_delete);
    run_case(&rc, case_kc_menu_validation);
    run_case(&rc, case_kc_menu_version);

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
        fprintf(
            stderr,
            "test case: expected one argument, got %d\n",
            argc - 1
        );
        return 2;
    }

    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_menu_add") == 0) {
        return case_kc_menu_add();
    }
    if (strcmp(argv[1], "kc_menu_delete") == 0) {
        return case_kc_menu_delete();
    }
    if (strcmp(argv[1], "kc_menu_validation") == 0) {
        return case_kc_menu_validation();
    }
    if (strcmp(argv[1], "kc_menu_version") == 0) {
        return case_kc_menu_version();
    }

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
