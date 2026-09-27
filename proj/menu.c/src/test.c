/**
 * test.c - libmenu public API and CLI contract tests.
 * Summary: Validates application menu registration and command-line behavior.
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
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef KC_MENU_TEST_CLI
#define KC_MENU_TEST_CLI ""
#endif

static int test_case_total = 0;
static int test_case_current = 0;

static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

#ifndef _WIN32
static char test_data_home[512];

static int test_prepare_home(void) {
    char pattern[] = "/tmp/kc-menu-test-XXXXXX";
    char *dir = mkdtemp(pattern);

    if (!dir) return 1;
    if (strlen(dir) + 1U > sizeof(test_data_home)) return 1;
    strcpy(test_data_home, dir);
    return setenv("XDG_DATA_HOME", test_data_home, 1) == 0 ? 0 : 1;
}

static int test_linux_entry_exists(const char *id) {
    char path[1024];
    struct stat st;

    snprintf(path, sizeof(path), "%s/applications/%s.desktop", test_data_home, id);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
#endif

static int case_kc_menu_add_delete(void) {
    const char *name = "kc_menu_add_delete";
    const char *detail = "adds, replaces, and deletes one application menu entry";
    kc_menu_entry_t entry = {
        "kc-menu-contract",
        "KC Menu Contract",
        "Contract test entry",
#ifdef _WIN32
        "cmd.exe /C exit 0",
#else
        "/bin/true --contract",
#endif
        NULL
    };
    int fail = 0;

#ifndef _WIN32
    if (test_data_home[0] == '\0') {
        fail += expect_true("prepare isolated XDG data home", test_prepare_home() == 0);
    }
#endif

    fail += expect_true("add rejects NULL", kc_menu_add(NULL) == KC_MENU_ERROR);
    fail += expect_true("delete rejects invalid id",
        kc_menu_delete("../bad") == KC_MENU_ERROR);

    if (!fail) {
        fail += expect_true("add succeeds", kc_menu_add(&entry) == KC_MENU_OK);
#ifndef _WIN32
        fail += expect_true("desktop entry exists", test_linux_entry_exists(entry.id));
#endif
        entry.description = "Replacement description";
        fail += expect_true("replacement add succeeds",
            kc_menu_add(&entry) == KC_MENU_OK);
        fail += expect_true("delete succeeds",
            kc_menu_delete(entry.id) == KC_MENU_OK);
        fail += expect_true("missing delete is a no-op",
            kc_menu_delete(entry.id) == KC_MENU_OK);
#ifndef _WIN32
        fail += expect_true("desktop entry is gone", !test_linux_entry_exists(entry.id));
#endif
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_menu_version(void) {
    const char *name = "kc_menu_version";
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version is nonzero", kc_menu_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

#ifndef _WIN32
static int test_cli_status(const char *args) {
    char command[2048];
    int status;

    snprintf(command, sizeof(command), "\"%s\" %s >/dev/null 2>/dev/null",
        KC_MENU_TEST_CLI, args);
    status = system(command);
    if (status == -1 || !WIFEXITED(status)) return 255;
    return WEXITSTATUS(status);
}
#else
static int test_cli_status(const char *args) {
    char command[4096];
    int status;

    snprintf(command, sizeof(command), "\"%s\" %s >NUL 2>NUL",
        KC_MENU_TEST_CLI, args);
    status = system(command);
    return status;
}
#endif

static int case_kc_menu_cli(void) {
    const char *name = "kc_menu_cli";
    const char *detail = "help, version, add, delete, and validation";
    int fail = 0;

    if (KC_MENU_TEST_CLI[0] == '\0') {
        case_result(0, name, detail);
        return 0;
    }

    fail += expect_true("CLI help exits 0", test_cli_status("--help") == 0);
    fail += expect_true("CLI version exits 0", test_cli_status("--version") == 0);
    fail += expect_true("CLI missing fields exits 1",
        test_cli_status("kc-menu-cli") != 0);

#ifndef _WIN32
    if (test_data_home[0] == '\0') {
        fail += expect_true("prepare isolated XDG data home", test_prepare_home() == 0);
    }
    fail += expect_true("CLI add exits 0",
        test_cli_status("kc-menu-cli --name \"KC Menu CLI\" --description \"CLI test\" --command /bin/true") == 0);
    fail += expect_true("CLI delete exits 0",
        test_cli_status("--delete kc-menu-cli") == 0);
#endif

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_all(void) {
    int rc = 0;

    test_case_total = KC_MENU_TEST_CLI[0] ? 3 : 2;
    test_case_current = 0;

    run_case(&rc, case_kc_menu_add_delete);
    run_case(&rc, case_kc_menu_version);
    if (KC_MENU_TEST_CLI[0]) run_case(&rc, case_kc_menu_cli);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_menu_add_delete") == 0) return case_kc_menu_add_delete();
    if (strcmp(argv[1], "kc_menu_version") == 0) return case_kc_menu_version();
    if (strcmp(argv[1], "kc_menu_cli") == 0) return case_kc_menu_cli();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
