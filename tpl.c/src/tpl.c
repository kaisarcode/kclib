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
    printf("Usage: %s [options] [source]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    -root, --root <dir>      Base directory for includes (default: .)\n");
    printf("    -var, --var <key=value>  Inject a template variable (repeatable)\n");
    printf("    -h, --help               Show this help\n");
    printf("    -v, --version            Show version\n");
    printf("\n");
    printf("Input:\n");
    printf("    source                    Optional template source; stdin is used when omitted\n");
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
    kc_tpl_options_t options;
    kc_tpl_var_t *vars = NULL;
    size_t var_count = 0U;
    const char *source_arg = NULL;
    char *stdin_source = NULL;
    const char *source;
    kc_tpl_t *tpl = NULL;
    char *output = NULL;
    const char *root = ".";
    int i;

    if (argc > 1) {
        vars = (kc_tpl_var_t *)calloc((size_t)argc, sizeof(kc_tpl_var_t));
        if (vars == NULL) {
            fprintf(stderr, "tpl: out of memory\n");
            return 1;
        }
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            free(vars);
            kc_print_help(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            free(vars);
            kc_print_version();
            return 0;
        }

        if (strcmp(argv[i], "-root") == 0 || strcmp(argv[i], "--root") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr, "tpl: missing value for root\n");
                free(vars);
                return 1;
            }
            root = argv[i];
            continue;
        }

        if (strcmp(argv[i], "-var") == 0 || strcmp(argv[i], "--var") == 0) {
            char *eq;

            i++;
            if (i >= argc) {
                fprintf(stderr, "tpl: missing value for var\n");
                free(vars);
                return 1;
            }

            eq = strchr(argv[i], '=');
            if (eq == NULL || eq == argv[i]) {
                fprintf(stderr, "tpl: invalid variable '%s'\n", argv[i]);
                free(vars);
                return 1;
            }

            *eq = '\0';
            vars[var_count].key = argv[i];
            vars[var_count].value = eq + 1;
            var_count++;
            continue;
        }

        if (argv[i][0] == '-') {
            fprintf(stderr, "tpl: unknown option '%s'\n", argv[i]);
            free(vars);
            return 1;
        }

        if (source_arg != NULL) {
            fprintf(stderr, "tpl: unexpected argument '%s'\n", argv[i]);
            free(vars);
            return 1;
        }

        source_arg = argv[i];
    }

    if (source_arg != NULL) {
        source = source_arg;
    } else {
        if (kc_tpl_read_all(KC_TPL_STDIN_FD, &stdin_source) != KC_TPL_OK) {
            fprintf(stderr, "tpl: failed to read input\n");
            free(vars);
            return 1;
        }
        source = stdin_source;
    }

    if (source == NULL || source[0] == '\0') {
        free(stdin_source);
        free(vars);
        return 0;
    }

    options.root = root;
    if (kc_tpl_open(&tpl, source, &options) != KC_TPL_OK) {
        fprintf(stderr, "tpl: failed to open template\n");
        free(stdin_source);
        free(vars);
        return 1;
    }

    output = kc_tpl_render(tpl, vars, var_count);
    if (output == NULL) {
        fprintf(stderr, "tpl: %s\n", kc_tpl_error(tpl));
        kc_tpl_close(tpl);
        free(stdin_source);
        free(vars);
        return 1;
    }

    if (fputs(output, stdout) == EOF) {
        kc_tpl_free(output);
        kc_tpl_close(tpl);
        free(stdin_source);
        free(vars);
        return 1;
    }

    kc_tpl_free(output);
    kc_tpl_close(tpl);
    free(stdin_source);
    free(vars);
    return 0;
}
