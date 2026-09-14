/**
 * tpl.c - Template renderer.
 * Summary: Command line interface for the libtpl renderer.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libtpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define KC_TPL_READ  _read
#define KC_TPL_WRITE _write
#define KC_TPL_STDIN_FD  0
#define KC_TPL_STDOUT_FD 1
typedef long long kc_tpl_ssize_t;
#else
#include <unistd.h>
#define KC_TPL_READ  read
#define KC_TPL_WRITE write
#define KC_TPL_STDIN_FD  STDIN_FILENO
#define KC_TPL_STDOUT_FD STDOUT_FILENO
typedef ssize_t kc_tpl_ssize_t;
#endif

/**
 * Reads all available input from a descriptor into an owned buffer.
 * @param fd Source descriptor.
 * @param out Receives the allocated buffer pointer (owned by caller).
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_read_all(int fd, char **out) {
    char *buf = NULL;
    size_t used = 0;
    size_t cap = 0;
    unsigned char c;
    kc_tpl_ssize_t n;

    if (!out) {
        return KC_TPL_ERROR;
    }

    *out = NULL;

    for (;;) {
        n = KC_TPL_READ(fd, &c, 1);

        if (n < 0) {
            free(buf);
            return KC_TPL_ERROR;
        }

        if (n == 0) {
            break;
        }

        if (used + 2 > cap) {
            cap = cap == 0 ? 256 : cap * 2;
            char *p = (char *)realloc(buf, cap);
            if (!p) {
                free(buf);
                return KC_TPL_ERROR;
            }
            buf = p;
        }

        buf[used++] = c;
    }

    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) {
            return KC_TPL_ERROR;
        }
    }

    buf[used] = '\0';
    *out = buf;
    return KC_TPL_OK;
}

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --root <dir>        Base directory for includes (default: cwd)\n");
    printf("    --var <key=value>   Inject a template variable (repeatable)\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show version\n");
}

/**
 * Prints command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("tpl build %llu\n", (unsigned long long)kc_tpl_version());
}

/**
 * Executes the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_tpl_options_t opts = kc_tpl_options_default();
    char *input = NULL;
    char *root = NULL;
    kc_tpl_t *ctx = NULL;
    char *output = NULL;
    int i;
    int rc = 0;

    kc_tpl_options_load_env(&opts);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            kc_tpl_options_free(&opts);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            kc_tpl_options_free(&opts);
            return 0;
        } else if (strcmp(argv[i], "--root") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr, "tpl: missing value for --root\n");
                kc_tpl_options_free(&opts);
                return 1;
            }
            root = argv[i];
        } else if (strcmp(argv[i], "--var") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr, "tpl: missing value for --var\n");
                kc_tpl_options_free(&opts);
                return 1;
            }
        } else {
            fprintf(stderr, "tpl: unknown option '%s'\n", argv[i]);
            kc_tpl_options_free(&opts);
            return 1;
        }
    }

    if (kc_tpl_read_all(KC_TPL_STDIN_FD, &input) != KC_TPL_OK) {
        fprintf(stderr, "tpl: failed to read input\n");
        free(input);
        kc_tpl_options_free(&opts);
        return 1;
    }

    if (!input || input[0] == '\0') {
        free(input);
        kc_tpl_options_free(&opts);
        return 0;
    }

    if (opts.root) {
        root = opts.root;
    }

    if (root) {
        opts.root = root;
    }

    rc = kc_tpl_open(&ctx, &opts);
    if (rc != KC_TPL_OK) {
        fprintf(stderr, "tpl: failed to open context\n");
        free(input);
        kc_tpl_options_free(&opts);
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--var") == 0) {
            i++;
            if (i < argc) {
                char *pair = argv[i];
                char *eq = strchr(pair, '=');
                if (eq) {
                    *eq = '\0';
                    kc_tpl_set_var(ctx, pair, eq + 1);
                    *eq = '=';
                }
            }
        }
    }

    rc = kc_tpl_render_string(ctx, input, &output);
    free(input);
    kc_tpl_close(ctx);
    kc_tpl_options_free(&opts);

    if (rc != KC_TPL_OK) {
        if (output) free(output);
        return 1;
    }

    if (output) {
        rc = printf("%s", output);
        free(output);
        if (rc < 0) {
            return 1;
        }
    }

    return 0;
}
