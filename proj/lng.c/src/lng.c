/**
 * lng.c - Summary of the functionality
 * Summary: Command line interface for the lng tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "liblng.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define KC_LNG_READ  _read
#define KC_LNG_STDIN_FD  0
#define KC_LNG_STDOUT_FD 1
typedef long long kc_lng_ssize_t;
#else
#include <unistd.h>
#define KC_LNG_READ  read
#define KC_LNG_STDIN_FD  STDIN_FILENO
#define KC_LNG_STDOUT_FD STDOUT_FILENO
typedef ssize_t kc_lng_ssize_t;
#endif

/**
 * Reads all available input from a descriptor into an owned buffer.
 * @param fd Source descriptor.
 * @param out Receives the allocated buffer (owned by caller).
 * @return 0 on success, or -1 on failure.
 */
static int kc_lng_read_all(int fd, char **out) {
    char *buf = NULL;
    size_t used = 0;
    size_t cap = 0;
    unsigned char c;
    kc_lng_ssize_t n;

    if (!out) return -1;
    *out = NULL;

    for (;;) {
        n = KC_LNG_READ(fd, &c, 1);
        if (n < 0) { free(buf); return -1; }
        if (n == 0) break;
        if (used + 2 > cap) {
            cap = cap == 0 ? 256 : cap * 2;
            char *p = (char *)realloc(buf, cap);
            if (!p) { free(buf); return -1; }
            buf = p;
        }
        buf[used++] = c;
    }

    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) return -1;
    }
    buf[used] = '\0';
    *out = buf;
    return 0;
}

/**
 * Parses one integer CLI value.
 * @param text Input text.
 * @param out Output integer pointer.
 * @return 1 on success, 0 on failure.
 */
static int kc_lng_parse_int(const char *text, int *out) {
    char *end;
    long value;

    if (!text || !out) {
        return 0;
    }

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    if (value < -2147483647L - 1L || value > 2147483647L) {
        return 0;
    }

    *out = (int)value;
    return 1;
}

/**
 * Parses one floating-point CLI value.
 * @param text Input text.
 * @param out Output double pointer.
 * @return 1 on success, 0 on failure.
 */
static int kc_lng_parse_double(const char *text, double *out) {
    char *end;
    double value;

    if (!text || !out) {
        return 0;
    }

    errno = 0;
    value = strtod(text, &end);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = value;
    return 1;
}

/**
 * Prints the compact command help.
 * @return No return value.
 */
static void kc_lng_help(void) {
    printf("Usage:\n");
    printf("  lng [options] [text]\n\n");
    printf("Options:\n");
    printf("  --threshold, -t <n>  Minimum score threshold\n");
    printf("  --limit, -l <n>      Maximum number of results\n");
    printf("  -h, --help           Show help\n");
    printf("  -v, --version        Show version\n\n");
    printf("Examples:\n");
    printf("  lng \"hello world\"\n");
    printf("  printf 'hola mundo' | lng -l 3\n");
}

/**
 * Prints the binary version.
 * @return No return value.
 */
static void kc_lng_cli_version(void) {
    printf("lng build %llu\n", (unsigned long long)kc_lng_version());
}

/**
 * Prints one CLI error followed by help, then returns 1.
 * @param message Error text.
 * @return Always 1.
 */
static int kc_lng_fail_usage(const char *message) {
    fprintf(stderr, "lng: %s\n\n", message);
    kc_lng_help();
    return 1;
}

/**
 * Standalone entry point.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    const char *text;
    double threshold;
    int threshold_set;
    int limit;
    int limit_set;
    int i;
    int exit_code;
    char *input;
    kc_lng_result_t *results;
    size_t count;
    size_t j;
    int rc;

    text = NULL;
    threshold = 0.0;
    threshold_set = 0;
    limit = 0;
    limit_set = 0;
    exit_code = 0;
    input = NULL;
    results = NULL;
    count = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            kc_lng_help();
            goto cleanup;
        }

        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            kc_lng_cli_version();
            goto cleanup;
        }

        if (strcmp(argv[i], "--threshold") == 0 || strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                exit_code = kc_lng_fail_usage("Missing value for --threshold.");
                goto cleanup;
            }

            if (!kc_lng_parse_double(argv[i + 1], &threshold)) {
                exit_code = kc_lng_fail_usage("Invalid value for --threshold.");
                goto cleanup;
            }

            threshold_set = 1;
            i++;
            continue;
        }

        if (strcmp(argv[i], "--limit") == 0 || strcmp(argv[i], "-l") == 0) {
            if (i + 1 >= argc) {
                exit_code = kc_lng_fail_usage("Missing value for --limit.");
                goto cleanup;
            }

            if (!kc_lng_parse_int(argv[i + 1], &limit)) {
                exit_code = kc_lng_fail_usage("Invalid value for --limit.");
                goto cleanup;
            }

            limit_set = 1;
            i++;
            continue;
        }

        if (argv[i][0] == '-') {
            exit_code = kc_lng_fail_usage("Unknown argument.");
            goto cleanup;
        }

        if (text != NULL) {
            exit_code = kc_lng_fail_usage("Too many positional arguments.");
            goto cleanup;
        }

        text = argv[i];
    }

    if (limit_set && limit < 1) {
        limit = 1;
    }

    if (limit_set && limit > 32) {
        limit = 32;
    }

    input = NULL;

    {
        kc_lng_options_t options = {0};
        size_t limit_value = 0;

        if (threshold_set) {
            options.threshold = &threshold;
        }
        if (limit_set) {
            limit_value = (size_t)limit;
            options.limit = &limit_value;
        }

        if (text) {
            rc = kc_lng_detect(text, &options, &results, &count);
            if (rc != KC_LNG_OK) {
                fprintf(stderr, "lng: detection failed\n");
                kc_lng_free(results);
                results = NULL;
                exit_code = 1;
                goto cleanup;
            }
        } else {
            if (kc_lng_read_all(KC_LNG_STDIN_FD, &input) != 0) {
                fprintf(stderr, "lng: failed to read stdin\n");
                free(input);
                input = NULL;
                kc_lng_free(results);
                results = NULL;
                exit_code = 1;
                goto cleanup;
            }

            if (input == NULL || input[0] == '\0') {
                free(input);
                input = NULL;
                kc_lng_free(results);
                results = NULL;
                goto cleanup;
            }

            rc = kc_lng_detect(input, &options, &results, &count);
            free(input);
            input = NULL;
            if (rc != KC_LNG_OK) {
                fprintf(stderr, "lng: detection failed\n");
                kc_lng_free(results);
                results = NULL;
                exit_code = 1;
                goto cleanup;
            }
        }

        for (j = 0; j < count; j++) {
            if ((!limit_set ? 1 : limit) == 1) {
                printf("%s\n", results[j].code);
            } else {
                printf("%s: %.4f\n", results[j].code, results[j].score);
            }
        }

        kc_lng_free(results);
        results = NULL;
    }

cleanup:
    free(input);
    kc_lng_free(results);
    return exit_code;
}
