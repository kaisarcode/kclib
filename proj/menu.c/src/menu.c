/**
 * menu.c - Native application menu CLI.
 * Summary: Adapts command-line arguments to the public menu API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libmenu.h"

#include <stdio.h>
#include <string.h>

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_menu_help(const char *name) {
    printf("Usage:\n");
    printf("  %s <id> --name <name> --command <command> [options]\n", name);
    printf("  %s -d <id>\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -n, --name <name>               Set display name\n");
    printf("  -D, --description <text>        Set description\n");
    printf("  -c, --command <command>         Set command to execute\n");
    printf("  -i, --icon <path>               Set icon\n");
    printf("  -d, --delete <id>               Delete menu entry\n");
    printf("  -h, --help                      Show this help\n");
    printf("  -v, --version                   Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_menu_print_version(void) {
    printf("menu build %llu\n", (unsigned long long)kc_menu_version());
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_menu_entry_t entry = {0};
    int i;

    if (argc < 2) {
        kc_menu_help(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        kc_menu_help(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        kc_menu_print_version();
        return 0;
    }
    if (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--delete") == 0) {
        if (argc != 3) {
            fprintf(stderr, "menu: expected one id for %s\n", argv[1]);
            return 1;
        }
        return kc_menu_delete(argv[2]) == KC_MENU_OK ? 0 : 1;
    }

    entry.id = argv[1];
    for (i = 2; i < argc; i++) {
        const char **target = NULL;

        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_menu_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_menu_print_version();
            return 0;
        }
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            target = &entry.name;
        } else if (strcmp(argv[i], "-D") == 0 ||
                strcmp(argv[i], "--description") == 0) {
            target = &entry.description;
        } else if (strcmp(argv[i], "-c") == 0 ||
                strcmp(argv[i], "--command") == 0) {
            target = &entry.command;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--icon") == 0) {
            target = &entry.icon;
        } else {
            fprintf(stderr, "menu: unknown option '%s'\n", argv[i]);
            return 1;
        }

        if (++i >= argc) {
            fprintf(stderr, "menu: missing value for %s\n", argv[i - 1]);
            return 1;
        }
        *target = argv[i];
    }

    if (!entry.name || !entry.command) {
        fprintf(stderr, "menu: --name and --command are required\n");
        return 1;
    }

    return kc_menu_add(&entry) == KC_MENU_OK ? 0 : 1;
}
