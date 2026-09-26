/**
 * tray.c - Native system tray CLI.
 * Summary: Adapts command-line arguments to the public tray API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libtray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#else
#include <time.h>
#endif

typedef struct {
    const char *label;
    int separator;
    int quit;
} cli_item_t;
static atomic_int finished;

/**
 * Handle a menu selection in the CLI.
 * @param item Item handle.
 * @param data Callback data.
 * @return None.
 */
static void cli_action(kc_tray_item_t *item, void *data) {
    cli_item_t *entry = (cli_item_t *)data;
    (void)item;
    if (entry->quit) {
        atomic_store(&finished, 1);
#if defined(__APPLE__)
        [NSApp stop:nil];
#endif
    } else {
        puts(entry->label);
        fflush(stdout);
    }
}

/**
 * Print the CLI usage and options.
 * @param name Case or program name.
 * @return None.
 */
static void help(const char *name) {
    printf("Usage: %s [--icon PATH] [--tooltip TEXT] [--item TEXT | --sep | --quit TEXT]...\n"
            "  --item TEXT    Print TEXT when selected\n"
            "  --sep          Insert a separator\n"
            "  --quit TEXT    Exit when selected\n"
            "  -h, --help    Show help\n"
            "  -v, --version Show build version\n", name);
}

/**
 * Run the tray command or its contract tests.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status or test result.
 */
int main(int argc, char **argv) {
    kc_tray_options_t opts = {0};
    kc_tray_t *tray = NULL;
    cli_item_t *items;
    const char *env;
    int count = 0, i, status = 1;
    atomic_init(&finished, 0);
    items = (cli_item_t *)calloc((size_t)argc, sizeof(*items));
    if (!items) return 1;
    env = getenv("KC_TRAY_ICON");
    if (env && *env) opts.icon = env;
    env = getenv("KC_TRAY_TOOLTIP");
    if (env && *env) opts.tooltip = env;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            help(argv[0]); status = 0; goto done;
        }
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            printf("tray build %llu\n", (unsigned long long)kc_tray_version());
            status = 0; goto done;
        }
        if (!strcmp(argv[i], "--sep")) {
            items[count++].separator = 1;
            continue;
        }
        if (!strcmp(argv[i], "--icon") || !strcmp(argv[i], "--tooltip") ||
            !strcmp(argv[i], "--item") || !strcmp(argv[i], "--quit")) {
            const char *flag = argv[i++];
            const char *value;
            if (i >= argc || !argv[i][0]) {
                fprintf(stderr, "tray: missing value for %s\n", flag);
                goto done;
            }
            value = argv[i];
            if (!strcmp(flag, "--icon")) opts.icon = value;
            else if (!strcmp(flag, "--tooltip")) opts.tooltip = value;
            else {
                items[count].label = value;
                items[count].quit = !strcmp(flag, "--quit");
                count++;
            }
            continue;
        }
        fprintf(stderr, "tray: unknown option '%s'\n", argv[i]);
        goto done;
    }
    if (!count) { fprintf(stderr, "tray: no menu items configured\n"); goto done; }
    if (kc_tray_open(&tray, &opts) != KC_TRAY_OK) {
        fprintf(stderr, "tray: open failed\n"); goto done;
    }
    for (i = 0; i < count; i++) {
        kc_tray_item_t *child = NULL;
        int rc = items[i].separator ? kc_tray_add_separator(tray, &child) :
                    kc_tray_add_item(tray, &child, items[i].label, cli_action, &items[i]);
        if (rc != KC_TRAY_OK) {
            fprintf(stderr, "tray: %s\n", kc_tray_get_error(tray) ?
                    kc_tray_get_error(tray) : "menu insertion failed");
            goto done;
        }
    }
#if defined(__APPLE__)
    [NSApp run];
#else
    while (!atomic_load(&finished)) {
#if defined(_WIN32)
        Sleep(100);
#else
        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);
#endif
    }
#endif
    status = 0;
done:
    kc_tray_close(tray);
    free(items);
    return status;
}
