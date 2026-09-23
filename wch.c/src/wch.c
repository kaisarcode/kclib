/**
 * wch.c - File and directory change notification CLI.
 * Summary: Watches files/directories and emits add, upd, and del events.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libwch.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [options] <path>\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  <path>          Path to file or directory\n");
    printf("  -h, --help      Show this help\n");
    printf("  -v, --version   Show version\n");
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
 * Maps one event type to its CLI label.
 * @param type Event type constant.
 * @return Event label.
 */
static const char *kc_wch_event_label(int type) {
    if (type == KC_WCH_UPD) return "upd";
    if (type == KC_WCH_DEL) return "del";
    return "add";
}

/**
 * Writes one asynchronous watcher event.
 * @param event Event received from the watcher.
 * @param userdata Unused callback value.
 * @return None.
 */
static void kc_wch_cli_event(const kc_wch_event_t *event, void *userdata) {
    (void)userdata;

    if (event == NULL || event->path == NULL) return;
    printf("%s:%s\n", kc_wch_event_label(event->type), event->path);
    fflush(stdout);
}

/**
 * Keeps the CLI process alive while the watcher worker runs.
 * @return None.
 */
static void kc_wch_cli_wait(void) {
#ifdef _WIN32
    Sleep(INFINITE);
#else
    for (;;) pause();
#endif
}

/**
 * Runs the wch command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    const char *path = NULL;
    kc_wch_options_t options = { 0 };
    kc_wch_t *w = NULL;
    int i = 1;

    if (i < argc && argv[i][0] != '-') {
        path = argv[i++];
    }

    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            return 0;
        }
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursive") == 0) {
            options.recursive = 1;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        } else {
            fprintf(stderr, "wch: unknown option '%s'\n", argv[i]);
            return 1;
        }
        i++;
    }

    if (path == NULL) {
        fprintf(stderr, "wch: missing path\n");
        return 1;
    }

    if (kc_wch_open(&w, path, &options) != KC_WCH_OK) {
        fprintf(stderr, "wch: failed to open watcher\n");
        return 1;
    }

    if (kc_wch_on(w, kc_wch_cli_event, NULL) != KC_WCH_OK) {
        fprintf(stderr, "wch: failed to start watcher\n");
        kc_wch_close(w);
        return 1;
    }

    kc_wch_cli_wait();
    kc_wch_close(w);
    return 0;
}
