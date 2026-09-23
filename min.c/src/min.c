/**
 * min.c - Asset Minifier
 * Summary: Command line interface for the min tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libmin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define KC_MIN_READ  _read
#define KC_MIN_WRITE _write
#define KC_MIN_STDIN_FD  0
#define KC_MIN_STDOUT_FD 1
typedef long long kc_min_ssize_t;
#else
#include <unistd.h>
#define KC_MIN_READ  read
#define KC_MIN_WRITE write
#define KC_MIN_STDIN_FD  STDIN_FILENO
#define KC_MIN_STDOUT_FD STDOUT_FILENO
typedef ssize_t kc_min_ssize_t;
#endif

/**
 * Reads all available input from a descriptor into an owned buffer.
 * @param fd Source descriptor.
 * @param out Receives the allocated buffer pointer (owned by caller).
 * @return 0 on success, or -1 on failure.
 */
static int kc_min_read_all(int fd, char **out) {
    char *buf = NULL;
    size_t used = 0;
    size_t cap = 0;
    unsigned char c;
    kc_min_ssize_t n;

    if (!out) {
        return -1;
    }

    *out = NULL;

    for (;;) {
        n = KC_MIN_READ(fd, &c, 1);

        if (n < 0) {
            free(buf);
            return -1;
        }

        if (n == 0) {
            break;
        }

        if (used + 2 > cap) {
            cap = cap == 0 ? 256 : cap * 2;
            char *p = (char *)realloc(buf, cap);
            if (!p) {
                free(buf);
                return -1;
            }
            buf = p;
        }

        buf[used++] = c;
    }

    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) {
            return -1;
        }
    }

    buf[used] = '\0';
    *out = buf;
    return 0;
}

/**
 * Convert a CLI mode name to a public minification mode.
 * @param name Mode name.
 * @return Public mode constant, or 0 for invalid input.
 */
static int kc_min_parse_mode(const char *name) {
    if (!name) {
        return 0;
    }
    if (strcmp(name, "css") == 0) {
        return KC_MIN_MODE_CSS;
    }
    if (strcmp(name, "js") == 0) {
        return KC_MIN_MODE_JS;
    }
    if (strcmp(name, "html") == 0) {
        return KC_MIN_MODE_HTML;
    }
    return 0;
}

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s <css|js|html> [options]\n", name);
    printf("\n");
    printf("Parameters:\n");
    printf("    css           Minify CSS input\n");
    printf("    js            Minify JavaScript input\n");
    printf("    html          Minify HTML input\n");
    printf("\n");
    printf("Options:\n");
    printf("    -h, --help    Show this help\n");
    printf("    -v, --version Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("min build %llu\n", (unsigned long long)kc_min_version());
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    const char *mode_name = NULL;
    char *input = NULL;
    char *output = NULL;
    int mode;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            return 0;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "min: unknown option '%s'\n", argv[i]);
            return 1;
        }
        if (!mode_name) {
            mode_name = argv[i];
        } else {
            fprintf(stderr, "min: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!mode_name) {
        fprintf(stderr, "min: mode is required\n");
        return 1;
    }

    mode = kc_min_parse_mode(mode_name);
    if (mode == 0) {
        fprintf(stderr, "min: invalid mode '%s'\n", mode_name);
        return 1;
    }

    if (kc_min_read_all(KC_MIN_STDIN_FD, &input) != 0) {
        fprintf(stderr, "min: failed to read input\n");
        return 1;
    }

    if (!input || input[0] == '\0') {
        free(input);
        return 0;
    }

    output = kc_min_minify(mode, input);
    if (!output) {
        fprintf(stderr, "min: execution failed\n");
        free(input);
        return 1;
    }

    fputs(output, stdout);

    kc_min_free(output);
    free(input);
    return 0;
}
