/**
 * libr.c - Summary of the functionality
 * Summary: Command line interface for the libr tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "liblibr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <stdbool.h>

/**
 * Reads one request from standard input up to a delimiter byte.
 * @param out_text Destination pointer for the allocated text.
 * @param until Delimiter byte.
 * @param at_eof Destination flag set when stdin reaches EOF.
 * @return 0 on success, or -1 on failure.
 */
static int kc_libr_read_request(char **out_text, int until, int *at_eof) {
    char *data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int ch;

    if (!out_text || !at_eof) {
        return -1;
    }

    *out_text = NULL;
    *at_eof = 0;

    while ((ch = fgetc(stdin)) != EOF) {
        if (ch == (unsigned char)until) {
            break;
        }
        if (length + 2 > capacity) {
            size_t next_cap = capacity ? capacity * 2 : 4096;
            char *next_data = (char *)realloc(data, next_cap);
            if (!next_data) {
                free(data);
                return -1;
            }
            data = next_data;
            capacity = next_cap;
        }
        data[length++] = (char)ch;
    }

    if (ch == EOF) {
        if (ferror(stdin)) {
            free(data);
            return -1;
        }
        *at_eof = 1;
    }

    if (length == 0) {
        free(data);
        *out_text = NULL;
        return 0;
    }

    data[length] = '\0';
    *out_text = data;
    return 0;
}

/**
 * Checks whether stdout is attached to a terminal.
 * @return 1 when stdout is a terminal, 0 otherwise.
 */
static int kc_libr_stdout_is_terminal(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

/**
 * Writes the response delimiter and terminal newline.
 * @param until Delimiter byte.
 * @return 0 on success, -1 on failure.
 */
static int kc_libr_write_response_delimiter(int until) {
    if (fputc(until, stdout) == EOF) {
        return -1;
    }
    if (kc_libr_stdout_is_terminal() && fputc('\n', stdout) == EOF) {
        return -1;
    }
    fflush(stdout);
    return 0;
}

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [set|get] [input] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    -p, --param <val>   Set parameter value\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("libr build %llu\n", (unsigned long long)kc_libr_version());
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_libr_options_t *opts = kc_libr_options_default();
    if (!opts) {
        fprintf(stderr, "libr: failed to allocate options\n");
        return 1;
    }

    const char *text = NULL;
    int i = 1;

    const bool has_verb = (i < argc) &&
        (strcmp(argv[i], "set") == 0 || strcmp(argv[i], "get") == 0);
    if (has_verb) {
        i++;
    }

    const bool has_positional = (i < argc && argv[i][0] != '-');
    if (has_positional) {
        text = argv[i++];
    }

    while (i < argc) {
        const char *arg = argv[i];
        const bool is_help = (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0);
        const bool is_version = (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0);
        const bool is_param = (strcmp(arg, "-p") == 0 || strcmp(arg, "--param") == 0);

        if (is_help) {
            kc_print_help(argv[0]);
            kc_libr_options_free(opts);
            return 0;
        }
        if (is_version) {
            kc_print_version();
            kc_libr_options_free(opts);
            return 0;
        }
        if (is_param) {
            if (++i >= argc) {
                fprintf(stderr, "libr: missing value for %s\n", argv[i - 1]);
                kc_libr_options_free(opts);
                return 1;
            }
            if (kc_libr_options_set(opts, "param", argv[i]) != KC_LIBR_OK) {
                fprintf(stderr, "libr: failed to set param\n");
                kc_libr_options_free(opts);
                return 1;
            }
        } else {
            fprintf(stderr, "libr: unknown option '%s'\n", argv[i]);
            kc_libr_options_free(opts);
            return 1;
        }
        i++;
    }

    kc_libr_t *ctx = NULL;
    if (kc_libr_open(&ctx, opts) != KC_LIBR_OK) {
        fprintf(stderr, "libr: open failed\n");
        kc_libr_options_free(opts);
        return 1;
    }
    kc_libr_options_free(opts);

    int rc = 0;
    const int delimiter = 4;

    if (text) {
        const bool exec_failed = kc_libr_exec(ctx, text) != KC_LIBR_OK;
        const bool delim_failed = kc_libr_write_response_delimiter(delimiter) != 0;
        if (exec_failed) {
            fprintf(stderr, "libr: exec failed\n");
            rc = 1;
        } else if (delim_failed) {
            fprintf(stderr, "libr: failed to write delimiter\n");
            rc = 1;
        }
    } else {
        while (1) {
            char *request = NULL;
            int at_eof = 0;

            const int read_rc = kc_libr_read_request(&request, delimiter, &at_eof);
            if (read_rc != 0) {
                fprintf(stderr, "libr: failed to read request\n");
                rc = 1;
                break;
            }

            const bool no_more_input = (!request && at_eof);
            if (no_more_input) {
                break;
            }

            const bool empty_request = (!request);
            if (empty_request) {
                continue;
            }

            const bool exec_failed = kc_libr_exec(ctx, request) != KC_LIBR_OK;
            free(request);
            if (exec_failed) {
                fprintf(stderr, "libr: exec failed\n");
                rc = 1;
                break;
            }

            const bool delim_failed = kc_libr_write_response_delimiter(delimiter) != 0;
            if (delim_failed) {
                fprintf(stderr, "libr: failed to write delimiter\n");
                rc = 1;
                break;
            }

            if (at_eof) {
                break;
            }
        }
    }

    kc_libr_close(ctx);
    return rc;
}
