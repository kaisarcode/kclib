/**
 * init.c - Persistent Startup Registration CLI
 * Summary: Command-line interface for init.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libinit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#endif

#define KC_INIT_BUF 4096

/**
 * Validate one registration name.
 * @param name Registration name.
 * @return 1 when valid, otherwise 0.
 */
static int kc_init_valid_name(const char *name) {
    const unsigned char *p;

    if (!name || !name[0]) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (p = (const unsigned char *)name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '.' || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

/**
 * Join command arguments into one command string.
 * @param out Output buffer.
 * @param cap Output buffer capacity.
 * @param argv Argument vector.
 * @param first First command argument.
 * @param argc Argument count.
 * @return Zero on success, nonzero on overflow.
 */
static int kc_init_join_args(
    char *out,
    size_t cap,
    char **argv,
    int first,
    int argc
) {
    size_t pos;
    int i;

    if (!out || cap == 0U) return 1;
    out[0] = '\0';
    pos = 0U;

    for (i = first; i < argc; i++) {
        size_t size = strlen(argv[i]);

        if (i > first) {
            if (pos + 1U >= cap) return 1;
            out[pos++] = ' ';
        }
        if (size >= cap - pos) return 1;
        memcpy(out + pos, argv[i], size);
        pos += size;
        out[pos] = '\0';
    }
    return 0;
}

/**
 * Print command usage.
 * @return None.
 */
static void kc_init_help(void) {
    printf("Usage: init <name> [command...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  init <name> <cmd>    Register or replace a startup entry\n");
    printf("  init -l [name]       List registrations\n");
    printf("  init <name> -l       List one registration\n");
    printf("  init -d <name>       Remove a registration\n");
    printf("  init <name> -d       Remove a registration\n");
    printf("\n");
    printf("Options:\n");
    printf("  -l, --list           List registrations\n");
    printf("  -d, --delete         Remove a registration\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
}

/**
 * Print build version.
 * @return None.
 */
static void kc_init_cli_version(void) {
    printf("init build %llu\n", (unsigned long long)kc_init_version());
}

#ifndef _WIN32
/**
 * Re-execute the current command with sudo when required.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return None.
 */
static void kc_init_elevate(int argc, char **argv) {
    char **next;
    char exe[KC_INIT_BUF];
    ssize_t size;
    int i;

    if (geteuid() == 0) return;
    if (system("command -v sudo >/dev/null 2>&1") != 0) return;

    size = readlink("/proc/self/exe", exe, sizeof(exe) - 1U);
    if (size <= 0) return;
    exe[size] = '\0';

    next = (char **)calloc((size_t)argc + 2U, sizeof(char *));
    if (!next) return;
    next[0] = "sudo";
    next[1] = exe;
    for (i = 1; i < argc; i++) next[i + 1] = argv[i];
    next[argc + 1] = NULL;

    (void)execvp("sudo", next);
    free(next);
}
#endif

/**
 * Print all startup registrations.
 * @param filter Optional registration name.
 * @return Process status.
 */
static int kc_init_cli_list(const char *filter) {
    kc_init_entry_t *entries;
    size_t count;
    size_t i;

    entries = NULL;
    count = 0U;

    if (filter) {
        kc_init_entry_t *entry = NULL;
        int rc = kc_init_get(filter, &entry);

        if (rc == KC_INIT_NOT_FOUND) return 0;
        if (rc != KC_INIT_OK) return 1;
        if (entry->user && entry->user[0]) {
            printf(
                "%s\t[%s]\t%s\n",
                entry->name,
                entry->user,
                entry->cmd
            );
        } else {
            printf("%s\n", entry->name);
        }
        kc_init_free(entry);
        return 0;
    }

    if (kc_init_list(&entries, &count) != KC_INIT_OK) return 1;
    for (i = 0; i < count; i++) {
        if (entries[i].user && entries[i].user[0]) {
            printf(
                "%s\t[%s]\t%s\n",
                entries[i].name,
                entries[i].user,
                entries[i].cmd
            );
        } else {
            printf("%s\n", entries[i].name);
        }
    }
    kc_init_free(entries);
    return 0;
}

/**
 * Execute the command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    const char *name;
    int i;

    i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_init_help();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_init_cli_version();
            return 0;
        }
        break;
    }

    if (i >= argc) {
        kc_init_help();
        return 1;
    }

    if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
        if (i + 1 == argc) return kc_init_cli_list(NULL);
        if (i + 2 == argc && kc_init_valid_name(argv[i + 1])) {
            return kc_init_cli_list(argv[i + 1]);
        }
        return 1;
    }

    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
        if (i + 2 != argc || !kc_init_valid_name(argv[i + 1])) return 1;
#ifndef _WIN32
        kc_init_elevate(argc, argv);
#endif
        return kc_init_delete(argv[i + 1]) == KC_INIT_OK ? 0 : 1;
    }

    name = argv[i];
    if (!kc_init_valid_name(name)) return 1;

    if (i + 1 < argc &&
            (strcmp(argv[i + 1], "-l") == 0 ||
            strcmp(argv[i + 1], "--list") == 0)) {
        return i + 2 == argc ? kc_init_cli_list(name) : 1;
    }

    if (i + 1 < argc &&
            (strcmp(argv[i + 1], "-d") == 0 ||
            strcmp(argv[i + 1], "--delete") == 0)) {
        if (i + 2 != argc) return 1;
#ifndef _WIN32
        kc_init_elevate(argc, argv);
#endif
        return kc_init_delete(name) == KC_INIT_OK ? 0 : 1;
    }

    if (i + 1 >= argc) {
        fprintf(stderr, "init: missing command\n");
        return 1;
    }

    {
        char command[KC_INIT_BUF];
        kc_init_options_t options;

        if (kc_init_join_args(command, sizeof(command), argv, i + 1, argc) != 0) {
            fprintf(stderr, "init: command is too long\n");
            return 1;
        }
        memset(&options, 0, sizeof(options));
        options.cmd = command;
#ifndef _WIN32
        kc_init_elevate(argc, argv);
#endif
        return kc_init_create(name, &options) == KC_INIT_OK ? 0 : 1;
    }
}
