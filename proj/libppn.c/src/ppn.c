/**
 * ppn.c - Native Popup Notification CLI
 * Summary: Command-line interface backed by libppn.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libppn.h"

#include <stdio.h>
#include <string.h>

/**
 * Print command usage.
 * @return None.
 */
static void kc_cli_help(void) {
    printf("Usage: ppn <title> <message>\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Print the linked libppn build version.
 * @return None.
 */
static void kc_cli_version(void) {
    printf("ppn build %llu\n", (unsigned long long)kc_ppn_version());
}

/**
 * Execute the command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    kc_ppn_notification_t notification;

    if (argc == 2 &&
            (strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0)) {
        kc_cli_help();
        return 0;
    }
    if (argc == 2 &&
            (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0)) {
        kc_cli_version();
        return 0;
    }
    if (argc < 2) {
        kc_cli_help();
        return 1;
    }
    if (argc != 3) {
        fprintf(stderr, "ppn: expected title and message\n");
        return 1;
    }
    if (argv[1][0] == '-') {
        fprintf(stderr, "ppn: unknown option: %s\n", argv[1]);
        return 1;
    }
    if (argv[2][0] == '-') {
        fprintf(stderr, "ppn: unknown option: %s\n", argv[2]);
        return 1;
    }
    if (argv[1][0] == '\0') {
        fprintf(stderr, "ppn: title must not be empty\n");
        return 1;
    }
    if (argv[2][0] == '\0') {
        fprintf(stderr, "ppn: message must not be empty\n");
        return 1;
    }

    notification.title = argv[1];
    notification.message = argv[2];

    if (kc_ppn_show(&notification) != KC_PPN_OK) {
        fprintf(stderr, "ppn: failed to show notification\n");
        return 1;
    }
    return 0;
}
