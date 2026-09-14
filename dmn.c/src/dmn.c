/**
 * dmn.c - IPC Daemon Manager
 * Summary: Command line interface for the dmn tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libdmn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/select.h>
#include <unistd.h>
#endif

#define KC_DMN_BUF 4096

/**
 * Joins argv[from..to) into buf with space separators.
 * @param buf Output buffer.
 * @param cap Buffer capacity.
 * @param argv Argument vector.
 * @param from Start index.
 * @param to End index.
 * @return Bytes written excluding null terminator.
 */
static size_t kc_dmn_join_args(char *buf, size_t cap, char **argv, int from, int to) {
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
 * Prints command usage to standard output.
 * @return None.
 */
static void kc_dmn_help(void) {
    printf("Usage: dmn <name> [command...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  dmn <name> <cmd>     Start or update a daemon\n");
    printf("  dmn -d <name>        Delete a daemon\n");
    printf("  dmn <name> -d        Delete a daemon\n");
    printf("  dmn -l [name]        List daemons\n");
    printf("  dmn <name> -l        List one daemon\n");
    printf("  dmn <name>           Relay stdin to daemon\n");
    printf("  dmn <name> -s <sig>  Send signal to a daemon\n");
    printf("\n");
    printf("Options:\n");
    printf("  -l, --list           List daemons\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
}

/**
 * Prints version to standard output.
 * @return None.
 */
static void kc_dmn_print_version(void) {
    printf("dmn build %llu\n", (unsigned long long)kc_dmn_version());
}

/**
 * List callback for CLI - prints key and socket path.
 * @param key Daemon key name.
 * @param sock Socket path.
 * @param userdata Unused.
 * @return None.
 */
static void kc_dmn_cli_list_cb(const char *key, const char *sock, void *userdata) {
    (void)userdata;
    printf("%s\t%s\n", key, sock);
}

/**
 * Executes the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_dmn_options_t opts = kc_dmn_options_default();
    int i = 1;

    kc_dmn_options_load_env(&opts);

    if (i >= argc) {
        kc_dmn_help();
        kc_dmn_options_free(&opts);
        return 1;
    }

    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
        kc_dmn_help();
        kc_dmn_options_free(&opts);
        return 0;
    }

    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
        kc_dmn_print_version();
        kc_dmn_options_free(&opts);
        return 0;
    }

#ifdef _WIN32
    if (strcmp(argv[i], "--_serve") == 0) {
        char cmd[KC_DMN_BUF];

        if (i + 2 >= argc) {
            kc_dmn_options_free(&opts);
            return 1;
        }

        kc_dmn_join_args(cmd, sizeof(cmd), argv, i + 2, argc);
        {
            int rc = kc_dmn_serve(argv[i + 1], cmd) == KC_DMN_OK ? 0 : 1;
            kc_dmn_options_free(&opts);
            return rc;
        }
    }
#endif

    if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
        const char *key = (i + 2 == argc) ? argv[i + 1] : NULL;
        kc_dmn_t *ctx = NULL;
        int rc;

        if (i + 2 < argc) {
            kc_dmn_options_free(&opts);
            return 1;
        }

        if (kc_dmn_open(&ctx, &opts) != KC_DMN_OK) {
            fprintf(stderr, "dmn: cannot resolve runtime directory\n");
            kc_dmn_options_free(&opts);
            return 1;
        }

        rc = kc_dmn_list(ctx, key, kc_dmn_cli_list_cb, NULL);
        kc_dmn_close(ctx);
        kc_dmn_options_free(&opts);
        return rc == KC_DMN_OK ? 0 : 1;
    }

    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
        kc_dmn_t *ctx = NULL;
        int rc;

        if (i + 2 != argc) {
            kc_dmn_options_free(&opts);
            return 1;
        }

        if (kc_dmn_open(&ctx, &opts) != KC_DMN_OK) {
            fprintf(stderr, "dmn: cannot resolve runtime directory\n");
            kc_dmn_options_free(&opts);
            return 1;
        }

        rc = kc_dmn_delete(ctx, argv[i + 1]);
        kc_dmn_close(ctx);
        kc_dmn_options_free(&opts);
        return rc == KC_DMN_OK ? 0 : 1;
    }

    if (argv[i][0] == '-') {
        kc_dmn_help();
        kc_dmn_options_free(&opts);
        return 1;
    }

    if (i + 1 < argc) {
        if (strcmp(argv[i + 1], "-d") == 0 || strcmp(argv[i + 1], "--delete") == 0) {
            kc_dmn_t *ctx = NULL;
            int rc;

            if (i + 2 != argc) {
                kc_dmn_options_free(&opts);
                return 1;
            }

            if (kc_dmn_open(&ctx, &opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(&opts);
                return 1;
            }

            rc = kc_dmn_delete(ctx, argv[i]);
            kc_dmn_close(ctx);
            kc_dmn_options_free(&opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }

        if (strcmp(argv[i + 1], "-l") == 0 || strcmp(argv[i + 1], "--list") == 0) {
            kc_dmn_t *ctx = NULL;
            int rc;

            if (i + 2 != argc) {
                kc_dmn_options_free(&opts);
                return 1;
            }

            if (kc_dmn_open(&ctx, &opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(&opts);
                return 1;
            }

            rc = kc_dmn_list(ctx, argv[i], kc_dmn_cli_list_cb, NULL);
            kc_dmn_close(ctx);
            kc_dmn_options_free(&opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }

        if (strcmp(argv[i + 1], "-s") == 0 || strcmp(argv[i + 1], "--signal") == 0) {
            kc_dmn_t *ctx = NULL;
            int rc;
            int signo = atoi(argv[i + 2]);

            if (i + 3 != argc) {
                kc_dmn_options_free(&opts);
                return 1;
            }

            if (kc_dmn_open(&ctx, &opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(&opts);
                return 1;
            }

            rc = kc_dmn_signal(ctx, argv[i], signo);
            kc_dmn_close(ctx);
            kc_dmn_options_free(&opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }

        {
            kc_dmn_t *ctx = NULL;
            char cmd[KC_DMN_BUF];
            int rc;

            kc_dmn_join_args(cmd, sizeof(cmd), argv, i + 1, argc);

            if (kc_dmn_open(&ctx, &opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(&opts);
                return 1;
            }

            rc = kc_dmn_update(ctx, argv[i], cmd);
            kc_dmn_close(ctx);
            kc_dmn_options_free(&opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }
    }

    {
        kc_dmn_t *ctx = NULL;

        if (kc_dmn_open(&ctx, &opts) != KC_DMN_OK) {
            fprintf(stderr, "dmn: cannot resolve runtime directory\n");
            kc_dmn_options_free(&opts);
            return 1;
        }
#ifndef _WIN32
        {
            int handle = -1;
            char buf[4096];
            int stdin_open = 1;
            ssize_t n;

            if (kc_dmn_connect(ctx, argv[i], &handle) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot connect to '%s'\n", argv[i]);
                kc_dmn_close(ctx);
                kc_dmn_options_free(&opts);
                return 1;
            }
            while (1) {
                fd_set fds;
                int max_fd = handle > STDIN_FILENO ? handle : STDIN_FILENO;
                int ready;

                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                FD_SET(handle, &fds);
                ready = select(max_fd + 1, &fds, NULL, NULL, NULL);
                if (ready < 0) break;
                if (stdin_open && FD_ISSET(STDIN_FILENO, &fds)) {
                    n = read(STDIN_FILENO, buf, sizeof(buf));
                    if (n <= 0) {
                        stdin_open = 0;
                    } else if (kc_dmn_send(handle, buf, (size_t)n) != KC_DMN_OK) {
                        break;
                    } else if (memchr(buf, 4, (size_t)n) != NULL) {
                        stdin_open = 0;
                    }
                }
                if (FD_ISSET(handle, &fds)) {
                    n = kc_dmn_recv(handle, buf, sizeof(buf));
                    if (n <= 0) break;
                    if (write(STDOUT_FILENO, buf, (size_t)n) != n) break;
                }
                if (!stdin_open) break;
            }
            kc_dmn_disconnect(handle);
        }
#else
        {
            int rc = kc_dmn_relay(ctx, argv[i]);
            kc_dmn_close(ctx);
            kc_dmn_options_free(&opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }
#endif
        kc_dmn_close(ctx);
        kc_dmn_options_free(&opts);
        return 0;
    }
}
