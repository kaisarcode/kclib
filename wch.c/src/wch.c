/**
 * wch.c - File and directory change notification CLI
 * Summary: Watches files/directories and emits add, upd, and del events.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libwch.h"

#include <stdio.h>
#include <string.h>

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [options] <path>\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  <path>         Path to file or directory\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
    printf("  -r, --recursive Watch recursively\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s .\n", name);
    printf("  %s ./file.txt\n", name);
}

/**
 * Prints the binary version to stdout.
 * @return None.
 */
static void kc_print_version(void) {
    printf("wch build %llu\n", (unsigned long long)kc_wch_version());
}

/**
 * Map a normalized event type to its string label.
 * @param type Event type constant.
 * @return "add", "upd", or "del".
 */
static const char *kc_wch_event_label(int type) {
    if (type == KC_WCH_UPD) return "upd";
    if (type == KC_WCH_DEL) return "del";
    return "add";
}

/**
 * Entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    const char *path = NULL;
    int recursive = 0;
    int i = 1;

    if (i < argc && argv[i][0] != '-') {
        path = argv[i++];
    }

    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            return 0;
        } else if (strcmp(argv[i], "-r") == 0 ||
            strcmp(argv[i], "--recursive") == 0) {
            recursive = 1;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        } else {
            fprintf(stderr, "wch: unknown option '%s'\n", argv[i]);
            return 1;
        }
        i++;
    }

    if (!path) {
        fprintf(stderr, "wch: missing path\n");
        return 1;
    }

    kc_wch_t *w = NULL;
    if (kc_wch_open(&w, path, recursive) != KC_WCH_OK) {
        fprintf(stderr, "wch: failed to open watcher\n");
        return 1;
    }

    for (;;) {
        kc_wch_event_t ev;
        int rc = kc_wch_poll(w, &ev, -1);
        if (rc == KC_WCH_ERROR) break;
        if (rc == KC_WCH_EVENT && ev.path != NULL) {
            printf("%s:%s\n", kc_wch_event_label(ev.type), ev.path);
            fflush(stdout);
        }
    }

    kc_wch_close(w);
    return 0;
}
