/**
 * menu.c - Native Application Menu CLI
 * Summary: Command-line interface backed by libmenu.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libmenu.h"

#include <stdio.h>
#include <string.h>

/**
 * Print command usage.
 * @return None.
 */
static void kc_cli_help(void) {
    printf("Usage: menu <id> [command] [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  menu <id> <cmd>       Register or replace a menu entry\n");
    printf("  menu <id> -d          Remove a menu entry\n");
    printf("\n");
    printf("Options:\n");
    printf("  --name <name>         Application name (defaults to id)\n");
    printf("  --description <text>  Application description\n");
    printf("  --icon <path>         Application icon path\n");
    printf("  --category <name>     Application menu category\n");
    printf("  -d, --delete          Remove a menu entry\n");
    printf("  -h, --help            Show this help\n");
    printf("  -v, --version         Show version\n");
}

/**
 * Print the linked libmenu build version.
 * @return None.
 */
static void kc_cli_version(void) {
    printf("menu build %llu\n", (unsigned long long)kc_menu_version());
}

/**
 * Add one application menu entry.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
static int kc_cli_add(int argc, char **argv) {
    kc_menu_entry_t entry;
    int i;

    if (argc < 3) {
        fprintf(stderr, "menu: missing command\n");
        return 1;
    }

    memset(&entry, 0, sizeof(entry));
    entry.id = argv[1];
    entry.name = entry.id;
    entry.command = argv[2];

    i = 3;
    while (i < argc) {
        if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 >= argc) return 1;
            entry.name = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--description") == 0) {
            if (i + 1 >= argc) return 1;
            entry.description = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--icon") == 0) {
            if (i + 1 >= argc) return 1;
            entry.icon = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--category") == 0) {
            if (i + 1 >= argc) return 1;
            entry.category = argv[i + 1];
            i += 2;
            continue;
        }

        fprintf(stderr, "menu: unknown option: %s\n", argv[i]);
        return 1;
    }

    if (kc_menu_add(&entry) != KC_MENU_OK) {
        fprintf(stderr, "menu: failed to add entry: %s\n", entry.id);
        return 1;
    }
    return 0;
}

/**
 * Delete one application menu entry.
 * @param id Application menu entry identifier.
 * @return Process status.
 */
static int kc_cli_delete(const char *id) {
    if (kc_menu_delete(id) != KC_MENU_OK) {
        fprintf(stderr, "menu: failed to delete entry: %s\n", id);
        return 1;
    }
    return 0;
}

/**
 * Execute the command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
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

    if (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--delete") == 0) {
        if (argc != 3) return 1;
        return kc_cli_delete(argv[2]);
    }

    if (argv[1][0] == '-') {
        fprintf(stderr, "menu: unknown option: %s\n", argv[1]);
        return 1;
    }

    if (argc >= 3 &&
            (strcmp(argv[2], "-d") == 0 ||
            strcmp(argv[2], "--delete") == 0)) {
        return argc == 3 ? kc_cli_delete(argv[1]) : 1;
    }

    return kc_cli_add(argc, argv);
}
