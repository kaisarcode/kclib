/**
 * test.c - libppn contract tests.
 * Summary: Tests validation, versioning, and native notification output.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libppn.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "ppn"
#endif
#endif

/**
 * Report one successful contract check.
 * @param index Current test index.
 * @param total Total test count.
 * @param name Test description.
 * @return Nothing.
 */
static void pass(int index, int total, const char *name) {
    printf("[%d/%d] [PASS] %s\n", index, total, name);
}

#ifndef __EMSCRIPTEN__
/**
 * Run the ppn CLI with output discarded.
 * @param args Arguments appended after the executable.
 * @return 0 on success, 1 on failure.
 */
static int ppn_cli_run(const char *args) {
    char command[4096];

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args);
#endif
    return system(command) == 0 ? 0 : 1;
}
#endif

/**
 * Run the ppn public contract tests.
 * @return Zero on success, nonzero on failure.
 */
int main(void) {
    kc_ppn_notification_t notification;
#ifdef __EMSCRIPTEN__
    int total = 5;
#else
    int total = 6;
#endif

    if (kc_ppn_show(NULL) != KC_PPN_ERROR) {
        return 1;
    }
    pass(1, total, "validation: null notification fails");

    notification.title = "";
    notification.message = "message";
    if (kc_ppn_show(&notification) != KC_PPN_ERROR) {
        return 1;
    }
    pass(2, total, "validation: empty title fails");

    notification.title = "Title";
    notification.message = "";
    if (kc_ppn_show(&notification) != KC_PPN_ERROR) {
        return 1;
    }
    pass(3, total, "validation: empty message fails");

    if (kc_ppn_version() == 0) {
        return 1;
    }
    pass(4, total, "version: build version is available");

    notification.title = "libppn.c";
    notification.message = "Native notification test";
    if (kc_ppn_show(&notification) != KC_PPN_OK) {
        return 1;
    }
    pass(5, total, "native: notification accepted by the operating system");

#ifndef __EMSCRIPTEN__
    if (ppn_cli_run("--help") || ppn_cli_run("--version") ||
        ppn_cli_run("--unknown") == 0 || ppn_cli_run("") == 0 ||
        ppn_cli_run("title") == 0 ||
        ppn_cli_run("title message extra") == 0) {
        return 1;
    }
    pass(6, total, "ppn_cli: validates notification command arguments");
#endif

    printf("%d passed, 0 failed\n", total);
    return 0;
}
