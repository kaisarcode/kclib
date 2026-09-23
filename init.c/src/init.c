/**
 * init.c - Persistent Startup Registration
 * Summary: Command line interface for the init tool.
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
#include <unistd.h>
#endif

#define KC_INIT_BUF 4096

/**
 * Joins argv[from..to) into buf with space separators.
 * @param buf Output buffer.
 * @param cap Buffer capacity.
 * @param argv Argument vector.
 * @param from Start index.
 * @param to End index.
 * @return Bytes written excluding null terminator.
 */
static size_t kc_init_join_args(
    char *buf, size_t cap, char **argv, int from, int to
) {
    size_t pos = 0;
    int j;

    buf[0] = '\0';

    for (j = from; j < to; j++) {
        int n;

        if (pos > 0 && pos < cap - 1) {
            buf[pos++] = ' ';
        }

        n = snprintf(buf + pos, cap - pos, "%s", argv[j]);
        if (n < 0 || (size_t)n >= cap - pos) {
            break;
        }

        pos += (size_t)n;
    }

    return pos;
}

/**
 * List callback that prints one registration entry.
 * @param key Registration key name.
 * @param user User name.
 * @param cmd Command string.
 * @param userdata Unused.
 * @return None.
 */
static void kc_init_print_entry(const char *key, const char *user, const char *cmd, void *userdata) {
    (void)userdata;
    if (user && user[0])
        printf("%s\t[%s]\t%s\n", key, user, cmd);
    else
        printf("%s\n", key);
}

/**
 * Prints command usage to standard output.
 * @return None.
 */
static void kc_init_help(void) {
    printf("Usage: init <name> [command...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  init <name> <cmd>    Register or replace a startup entry\n");
    printf("  init <name>          Execute registered command immediately\n");
    printf("  init --list          List all registrations\n");
    printf("  init <name> --list   List one registration\n");
    printf("  init <name> --delete Remove a registration\n");
    printf("\n");
    printf("Options:\n");
    printf("  -l, --list           List registrations\n");
    printf("  -d, --delete         Remove a registration\n");
    printf("  --dir <path>         Metadata directory path\n");
    printf("  --backend <name>     Backend name (e.g. systemd, openrc)\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
}

/**
 * Prints version to standard output.
 * @return None.
 */
static void kc_init_cli_version(void) {
    printf("init build %llu\n", (unsigned long long)kc_init_version());
}

#ifndef _WIN32
/**
 * Re-executes the current process with sudo if not root.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return None.
 */
static void kc_init_elevate(int argc, char **argv) {
    char **new_argv;
    char exe[KC_INIT_BUF];
    ssize_t len;
    int i;

    if (geteuid() == 0) return;
    if (system("command -v sudo >/dev/null 2>&1") != 0) return;

    len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len <= 0) return;
    exe[len] = '\0';

    new_argv = (char **)calloc((size_t)argc + 2, sizeof(char *));
    if (!new_argv) return;

    new_argv[0] = "sudo";
    new_argv[1] = exe;
    for (i = 1; i < argc; i++) {
        new_argv[i + 1] = argv[i];
    }
    new_argv[argc + 1] = NULL;

    (void)execvp("sudo", new_argv);
    free(new_argv);
}
#endif

/**
 * Execute a list operation.
 * @param opts Options.
 * @param name Optional name filter.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_cli_list(const kc_init_options_t *opts, const char *name) {
    kc_init_t *ctx = NULL;
    int rc;

    rc = kc_init_open(&ctx, opts);
    if (rc != KC_INIT_OK) {
        fprintf(stderr, "init: cannot resolve metadata directory\n");
        return 1;
    }

    rc = kc_init_list(ctx, name, kc_init_print_entry, NULL);
    if (rc != KC_INIT_OK) {
        const char *err = kc_init_error(ctx);
        if (err) fprintf(stderr, "init: %s\n", err);
        kc_init_close(ctx);
        return 1;
    }

    kc_init_close(ctx);
    return 0;
}

/**
 * Execute a delete operation.
 * @param opts Options.
 * @param name Registration name.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_cli_delete(const kc_init_options_t *opts, const char *name) {
    kc_init_t *ctx = NULL;
    int rc;

    rc = kc_init_open(&ctx, opts);
    if (rc != KC_INIT_OK) {
        fprintf(stderr, "init: cannot resolve metadata directory\n");
        return 1;
    }

    rc = kc_init_delete(ctx, name);
    if (rc != KC_INIT_OK) {
        const char *err = kc_init_error(ctx);
        if (err) fprintf(stderr, "init: %s\n", err);
        kc_init_close(ctx);
        return 1;
    }

    kc_init_close(ctx);
    return 0;
}

/**
 * Execute an update operation.
 * @param opts Options.
 * @param name Registration name.
 * @param cmd Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_cli_update(const kc_init_options_t *opts, const char *name, const char *cmd) {
    kc_init_t *ctx = NULL;
    int rc;

    rc = kc_init_open(&ctx, opts);
    if (rc != KC_INIT_OK) {
        fprintf(stderr, "init: cannot resolve metadata directory\n");
        return 1;
    }

    rc = kc_init_set(ctx, name, cmd);
    if (rc != KC_INIT_OK) {
        const char *err = kc_init_error(ctx);
        if (err) fprintf(stderr, "init: %s\n", err);
        kc_init_close(ctx);
        return 1;
    }

    kc_init_close(ctx);
    return 0;
}

/**
 * Execute an exec operation.
 * @param opts Options.
 * @param name Registration name.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_cli_exec(const kc_init_options_t *opts, const char *name) {
    kc_init_t *ctx = NULL;
    int rc;

    rc = kc_init_open(&ctx, opts);
    if (rc != KC_INIT_OK) {
        fprintf(stderr, "init: cannot resolve metadata directory\n");
        return 1;
    }

    rc = kc_init_exec(ctx, name);
    if (rc != KC_INIT_OK) {
        const char *err = kc_init_error(ctx);
        if (err) fprintf(stderr, "init: %s\n", err);
        kc_init_close(ctx);
        return 1;
    }

    kc_init_close(ctx);
    return 0;
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_init_options_t options = {0};
    const char *env_value;
    int i;
    int status;

    i = 1;
    status = 1;

    env_value = getenv("KC_INIT_DIR");
    if (env_value) options.dir = env_value;

    env_value = getenv("KC_INIT_BACKEND");
    if (env_value) options.backend = env_value;

    if (i >= argc) {
        kc_init_help();
        return status;
    }

    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_init_help();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_init_cli_version();
            return 0;
        }
        if (strcmp(argv[i], "--dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "init: missing value for --dir\n");
                return 1;
            }
            options.dir = argv[i + 1];
            i += 2;
            continue;
        }
        if (strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "init: missing value for --backend\n");
                return 1;
            }
            options.backend = argv[i + 1];
            i += 2;
            continue;
        }
        break;
    }

    if (i >= argc) {
        kc_init_help();
        return status;
    }

    if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
        if (i + 1 == argc) {
            return kc_init_cli_list(&options, NULL);
        }
        if (i + 2 == argc) {
            return kc_init_cli_list(&options, argv[i + 1]);
        }
        fprintf(stderr, "init: --list accepts at most one name\n");
        return 1;
    }

    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
        if (i + 2 != argc) {
            fprintf(stderr, "init: --delete requires exactly one name\n");
            return 1;
        }
#ifndef _WIN32
        kc_init_elevate(argc, argv);
#endif
        return kc_init_cli_delete(&options, argv[i + 1]);
    }

    if (argv[i][0] == '-') {
        kc_init_help();
        return 1;
    }

    if (i + 1 < argc) {
        if (strcmp(argv[i + 1], "-d") == 0 ||
            strcmp(argv[i + 1], "--delete") == 0) {
            if (i + 2 != argc) return 1;
#ifndef _WIN32
            kc_init_elevate(argc, argv);
#endif
            return kc_init_cli_delete(&options, argv[i]);
        }

        if (strcmp(argv[i + 1], "-l") == 0 ||
            strcmp(argv[i + 1], "--list") == 0) {
            if (i + 2 != argc) return 1;
            return kc_init_cli_list(&options, argv[i]);
        }

        {
            char cmd[KC_INIT_BUF];

            kc_init_join_args(cmd, sizeof(cmd), argv, i + 1, argc);
#ifndef _WIN32
            kc_init_elevate(argc, argv);
#endif
            return kc_init_cli_update(&options, argv[i], cmd);
        }
    }

#ifndef _WIN32
    kc_init_elevate(argc, argv);
#endif
    return kc_init_cli_exec(&options, argv[i]);
}
