/**
 * test.c - libppn contract tests.
 * Summary: Tests validation, versioning, and optional native notification output.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libppn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/**
 * Run the ppn public contract tests.
 * @return Zero on success, nonzero on failure.
 */
int main(void) {
    kc_ppn_notification_t notification;
    const char *native;
    int total = 4;

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

    native = getenv("PPN_TEST_NOTIFY");
    if (native != NULL && strcmp(native, "1") == 0) {
        notification.title = "libppn.c";
        notification.message = "Native notification test";
        if (kc_ppn_show(&notification) != KC_PPN_OK) {
            return 1;
        }
        printf("[PASS] native: notification accepted by the operating system\n");
    }

    return 0;
}
