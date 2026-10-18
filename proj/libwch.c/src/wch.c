/**
 * wch.c - Resident file watcher command-line interface.
 * Summary: Manages libwch resident filesystem watchers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libwch.h"

#include <stdio.h>
#include <string.h>

#define WCH_CLI_BUF 4096

/**
 * Reports whether one watcher name is valid for the CLI.
 * @param name Watcher name.
 * @return Nonzero when valid.
 */
static int wch_cli_valid_name(const char *name)
{
    const unsigned char *p;

    if (!name || !name[0]) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (p = (const unsigned char *)name; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '.' || *p == '_' || *p == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

/**
 * Appends one argument to a command string.
 * @param out Output command buffer.
 * @param cap Output buffer capacity.
 * @param pos Current output length.
 * @param arg Argument to append.
 * @return Zero on success, nonzero on overflow.
 */
static int wch_cli_append_arg(
    char *out,
    size_t cap,
    size_t *pos,
    const char *arg
)
{
    size_t size;

    if (!out || !pos || !arg || cap == 0U) return 1;
    size = strlen(arg);
    if (*pos != 0U) {
        if (*pos + 1U >= cap) return 1;
        out[(*pos)++] = ' ';
    }
    if (size >= cap - *pos) return 1;
    memcpy(out + *pos, arg, size);
    *pos += size;
    out[*pos] = '\0';
    return 0;
}

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void wch_cli_usage(const char *name)
{
    printf("Usage: %s <id> <path> <command...>\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  %s <id> <path> <command...>  Register or replace a watcher\n", name);
    printf("  %s -l [id]                    List watchers\n", name);
    printf("  %s <id> -d                    Delete a watcher\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -r, --recursive    Watch directories recursively\n");
    printf("  -l, --list         List watchers\n");
    printf("  -d, --delete       Delete a watcher\n");
    printf("  -h, --help         Show this help\n");
    printf("  -v, --version      Show version\n");
}

/**
 * Prints the linked libwch build version.
 * @return None.
 */
static void wch_cli_version(void)
{
    printf("%llu\n", (unsigned long long)kc_wch_version());
}

/**
 * Prints one registered watcher.
 * @param entry Watcher entry.
 * @return None.
 */
static void wch_cli_print_entry(const kc_wch_entry_t *entry)
{
    if (!entry || !entry->name) return;
    printf(
        "%s\t%s\t%s\n",
        entry->name,
        entry->path ? entry->path : "",
        entry->cmd ? entry->cmd : ""
    );
}

/**
 * Lists all watchers or one named watcher.
 * @param filter Optional watcher name.
 * @return Process exit code.
 */
static int wch_cli_list(const char *filter)
{
    kc_wch_entry_t *entries = NULL;
    size_t count = 0;
    size_t i;

    if (kc_wch_list(&entries, &count) != KC_WCH_OK) return 1;
    for (i = 0; i < count; i++) {
        if (filter && strcmp(entries[i].name, filter) != 0) continue;
        wch_cli_print_entry(&entries[i]);
    }
    kc_wch_free(entries);
    return 0;
}

/**
 * Creates or replaces one resident watcher.
 * @param name Watcher name.
 * @param path Watched path.
 * @param command Event command.
 * @param recursive Nonzero enables recursive watching.
 * @return Process exit code.
 */
static int wch_cli_create(
    const char *name,
    const char *path,
    const char *command,
    int recursive
)
{
    kc_wch_options_t options;

    memset(&options, 0, sizeof(options));
    options.path = path;
    options.cmd = command;
    options.recursive = recursive;
    return kc_wch_create(name, &options) == KC_WCH_OK ? 0 : 1;
}

/**
 * Runs the wch CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    const char *name = NULL;
    const char *path = NULL;
    char command[WCH_CLI_BUF];
    size_t command_pos = 0U;
    int positional = 0;
    int recursive = 0;
    int list = 0;
    int remove = 0;
    int help = 0;
    int version = 0;
    int i;

    command[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-r") == 0 ||
                strcmp(argv[i], "--recursive") == 0) {
                recursive = 1;
            } else if (strcmp(argv[i], "-l") == 0 ||
                strcmp(argv[i], "--list") == 0) {
                list = 1;
            } else if (strcmp(argv[i], "-d") == 0 ||
                strcmp(argv[i], "--delete") == 0) {
                remove = 1;
            } else if (strcmp(argv[i], "-h") == 0 ||
                strcmp(argv[i], "--help") == 0) {
                help = 1;
            } else if (strcmp(argv[i], "-v") == 0 ||
                strcmp(argv[i], "--version") == 0) {
                version = 1;
            } else {
                return 1;
            }
            continue;
        }

        positional++;
        if (positional == 1) {
            name = argv[i];
        } else if (positional == 2) {
            path = argv[i];
        } else if (wch_cli_append_arg(
                command,
                sizeof(command),
                &command_pos,
                argv[i]
            ) != 0) {
            fprintf(stderr, "wch: command is too long\n");
            return 1;
        }
    }

    if (help) {
        wch_cli_usage(argv[0]);
        return 0;
    }
    if (version) {
        wch_cli_version();
        return 0;
    }

    if (list) {
        if (remove || recursive || positional > 1) return 1;
        if (name && !wch_cli_valid_name(name)) return 1;
        return wch_cli_list(name);
    }

    if (!name || !wch_cli_valid_name(name)) return 1;

    if (remove) {
        if (recursive || positional != 1) return 1;
        return kc_wch_delete(name) == KC_WCH_OK ? 0 : 1;
    }

    if (!path) return 1;
    if (command_pos == 0U) {
        fprintf(stderr, "wch: missing command\n");
        return 1;
    }

    return wch_cli_create(name, path, command, recursive);
}
