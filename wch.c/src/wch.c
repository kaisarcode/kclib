/**
 * wch.c - Managed File Watcher CLI
 * Summary: Command-line interface for named resident watchers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libwch.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define KC_WCH_PATH_MAX MAX_PATH
#else
#include <unistd.h>
#define KC_WCH_PATH_MAX 4096
#endif

#define KC_WCH_CMD_MAX 4096

int kc_wch_internal_serve(const char *dir, const char *name);

/**
 * Print command usage information.
 * @return None.
 */
static void kc_wch_help(void) {
    printf("Usage: wch <name> [options] <path> <command...>\n");
    printf("\n");
    printf("Commands:\n");
    printf("  wch <name> <path> <cmd>       Register or replace a watcher\n");
    printf("  wch <name> -r <path> <cmd>    Register a recursive watcher\n");
    printf("  wch -l [name]                 List watchers\n");
    printf("  wch <name> -l                 List one watcher\n");
    printf("  wch -d <name>                 Delete a watcher\n");
    printf("  wch <name> -d                 Delete a watcher\n");
    printf("\n");
    printf("Options:\n");
    printf("  -r, --recursive               Watch directories recursively\n");
    printf("  -l, --list                    List watchers\n");
    printf("  -d, --delete                  Stop and delete a watcher\n");
    printf("  -h, --help                    Show this help\n");
    printf("  -v, --version                 Show version\n");
}

/**
 * Print the binary version.
 * @return None.
 */
static void kc_wch_print_version(void) {
    printf("wch build %llu\n", (unsigned long long)kc_wch_version());
}

/**
 * Validate one CLI watcher name.
 * @param name Watcher name.
 * @return Nonzero when valid.
 */
static int kc_wch_valid_name(const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0' || strlen(name) >= 128U) {
        return 0;
    }
    for (i = 0U; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];

        if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.') {
            return 0;
        }
    }
    return 1;
}

/**
 * Resolve the CLI runtime directory.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_runtime_dir(char *out, size_t cap) {
    const char *override = getenv("KC_WCH_DIR");

    if (override != NULL && override[0] != '\0') {
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : 1;
    }
#ifdef _WIN32
    {
        char temp[MAX_PATH];
        DWORD size = GetTempPathA((DWORD)sizeof(temp), temp);

        if (size == 0 || size >= (DWORD)sizeof(temp)) return 1;
        return (size_t)snprintf(
            out,
            cap,
            "%swch.c",
            temp
        ) < cap ? 0 : 1;
    }
#else
    {
        const char *xdg = getenv("XDG_RUNTIME_DIR");

        if (xdg != NULL && xdg[0] != '\0') {
            return (size_t)snprintf(
                out,
                cap,
                "%s/wch.c",
                xdg
            ) < cap ? 0 : 1;
        }
        return (size_t)snprintf(
            out,
            cap,
            "/tmp/wch.c-%lu",
            (unsigned long)getuid()
        ) < cap ? 0 : 1;
    }
#endif
}

/**
 * Join command arguments with spaces.
 * @param out Output command buffer.
 * @param cap Output buffer capacity.
 * @param argv Argument vector.
 * @param from First command argument.
 * @param argc Argument count.
 * @return Zero on success, nonzero on overflow.
 */
static int kc_wch_join_args(
    char *out,
    size_t cap,
    char **argv,
    int from,
    int argc
) {
    size_t pos = 0U;
    int i;

    if (cap == 0U) return 1;
    out[0] = '\0';
    for (i = from; i < argc; i++) {
        int written;

        if (pos > 0U) {
            if (pos + 1U >= cap) return 1;
            out[pos++] = ' ';
        }
        written = snprintf(
            out + pos,
            cap - pos,
            "%s",
            argv[i]
        );
        if (written < 0 || (size_t)written >= cap - pos) return 1;
        pos += (size_t)written;
    }
    return 0;
}

/**
 * Print registered watcher entries.
 * @param dir Runtime directory.
 * @param filter Optional watcher name.
 * @return Process status.
 */
static int kc_wch_print_list(
    const char *dir,
    const char *filter
) {
    kc_wch_entry_t *entries = NULL;
    size_t count = 0U;
    size_t i;

    if (kc_wch_list(dir, &entries, &count) != KC_WCH_OK) return 1;
    for (i = 0U; i < count; i++) {
        if (filter != NULL &&
                strcmp(filter, entries[i].name) != 0) {
            continue;
        }
        printf(
            "%s\t%s\t%s\t%s\t%s\n",
            entries[i].name,
            entries[i].running ? "running" : "stopped",
            entries[i].recursive ? "recursive" : "direct",
            entries[i].path,
            entries[i].cmd
        );
    }
    kc_wch_free(entries);
    return 0;
}

/**
 * Run the managed watcher command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    char dir[KC_WCH_PATH_MAX];

    if (kc_wch_runtime_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "wch: cannot resolve runtime directory\n");
        return 1;
    }
    if (argc < 2) {
        kc_wch_help();
        return 1;
    }

    if (strcmp(argv[1], "--_serve") == 0) {
        if (argc != 4 || !kc_wch_valid_name(argv[2])) return 1;
        return kc_wch_internal_serve(argv[3], argv[2]);
    }

    if (strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0) {
        kc_wch_help();
        return 0;
    }
    if (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0) {
        kc_wch_print_version();
        return 0;
    }
    if (strcmp(argv[1], "-l") == 0 ||
            strcmp(argv[1], "--list") == 0) {
        if (argc == 2) return kc_wch_print_list(dir, NULL);
        if (argc == 3 && kc_wch_valid_name(argv[2])) {
            return kc_wch_print_list(dir, argv[2]);
        }
        return 1;
    }
    if (strcmp(argv[1], "-d") == 0 ||
            strcmp(argv[1], "--delete") == 0) {
        if (argc != 3 || !kc_wch_valid_name(argv[2])) return 1;
        return kc_wch_delete(argv[2], dir) == KC_WCH_OK ? 0 : 1;
    }

    if (!kc_wch_valid_name(argv[1])) {
        fprintf(stderr, "wch: invalid watcher name\n");
        return 1;
    }

    if (argc == 3 &&
        (strcmp(argv[2], "-l") == 0 ||
        strcmp(argv[2], "--list") == 0)) {
        return kc_wch_print_list(dir, argv[1]);
    }
    if (argc == 3 &&
        (strcmp(argv[2], "-d") == 0 ||
        strcmp(argv[2], "--delete") == 0)) {
        return kc_wch_delete(argv[1], dir) == KC_WCH_OK ? 0 : 1;
    }

    {
        kc_wch_options_t options;
        char command[KC_WCH_CMD_MAX];
        int recursive = 0;
        int index = 2;

        memset(&options, 0, sizeof(options));
        if (index < argc &&
            (strcmp(argv[index], "-r") == 0 ||
            strcmp(argv[index], "--recursive") == 0)) {
            recursive = 1;
            index++;
        }
        if (index >= argc) {
            fprintf(stderr, "wch: missing path\n");
            return 1;
        }
        if (index + 1 >= argc) {
            fprintf(stderr, "wch: missing command\n");
            return 1;
        }
        if (kc_wch_join_args(
                command,
                sizeof(command),
                argv,
                index + 1,
                argc
            ) != 0) {
            fprintf(stderr, "wch: command too long\n");
            return 1;
        }

        options.path = argv[index];
        options.cmd = command;
        options.dir = dir;
        options.recursive = recursive;
        if (kc_wch_create(argv[1], &options) != KC_WCH_OK) {
            fprintf(stderr, "wch: failed to register watcher\n");
            return 1;
        }
    }

    return 0;
}
